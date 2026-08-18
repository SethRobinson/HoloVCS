#include "LookingGlassBridge.h"

#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Misc/ScopeLock.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "Templates/Atomic.h"
#include "Async/Async.h"
#include "Containers/Queue.h"

#if WITH_EDITOR
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#endif

#include "DynamicRHI.h"

// "bridge.h" includes windows headers, which aren't compliant with Unreal's strict coding standard - we should disable something first
// AllowWindowsPlatformTypes/HideWindowsPlatformTypes scrub the windows.h macros afterwards (PostWindowsApi
// undefines the Interlocked* family, which otherwise breaks engine headers in this unity TU on UE 5.7+)
#include "Windows/AllowWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START
#pragma warning(push)
#pragma warning(disable : 4191)
//#include "bridge.h"
#include "bridge_calibration_templates.h"
#pragma warning(pop)
THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"

// Undef some Windows.h defines which breaks compilation of Unreal engine
#undef GetEnvironmentVariable
#undef InterlockedIncrement
#undef InterlockedDecrement
#undef InterlockedExchange
#undef GetCurrentTime
#undef UpdateResource
#undef CaptureStackBackTrace
#undef MemoryBarrier
#undef GetClassName
#undef max

#define BRIDGE_VERSION_MAJOR	2
#define BRIDGE_VERSION_MINOR	4
#define BRIDGE_VERSION_BUILD	11

DEFINE_LOG_CATEGORY_STATIC(LogLookingGlassBridge, Log, All);

#define LOCTEXT_NAMESPACE "LookingGlassBridge"

static void ReportError(const FString& Message)
{
	UE_LOG(LogLookingGlassBridge, Error, TEXT("%s"), *Message);
#if WITH_EDITOR
	// May be called from the bridge thread - Slate notifications must run on the game thread
	AsyncTask(ENamedThreads::GameThread, [Message]()
	{
		if (!FSlateApplication::IsInitialized())
		{
			return;
		}
		FNotificationInfo Info(FText::FromString(Message));
		Info.ExpireDuration = 15.0f;
		Info.bUseSuccessFailIcons = true;
		Info.Image = FAppStyle::GetBrush(TEXT("MessageLog.Warning"));
		TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);
	});
#endif // WITH_EDITOR
}

bool FLookingGlassBridge::Initialize_BridgeThread()
{
	// Load the Bridge
	BridgeController = new ControllerWithCalibrationTemplates();
	if (!BridgeController->Initialize(TEXT("UnrealEnginePlugin")))
	{
		ReportError(TEXT("Bridge initialization failed"));
		delete BridgeController;
		return false;
	}

	// Verify installed version
	unsigned long Major = 0, Minor = 0, Build = 0;
	int32 NumPostfixChars = 0;
	BridgeController->GetBridgeVersion(&Major, &Minor, &Build, &NumPostfixChars, nullptr);

	int32 Version = Major * 1000000 + Minor * 1000 + Build;
	int32 Desired = BRIDGE_VERSION_MAJOR * 1000000 + BRIDGE_VERSION_MINOR * 1000 + BRIDGE_VERSION_BUILD;
	if (Version < Desired)
	{
		ReportError(FString::Printf(
			TEXT("The installed Looking Glass Bridge has version %d.%d.%d, required version is %d.%d.%d, please update!"),
			Major, Minor, Build, BRIDGE_VERSION_MAJOR, BRIDGE_VERSION_MINOR, BRIDGE_VERSION_BUILD));
		return false;
	}

	bInitialized = true;

	int32 TemplateCount;
	BridgeController->GetCalibrationTemplateCount(&TemplateCount);

	for (int32 TemplateIndex = 0; TemplateIndex < TemplateCount; TemplateIndex++)
	{
		// Note: functions which returns strings doesn't null-terminate them.
		int32 CharsCount1 = 256, CharsCount2 = 256, CharsCount3 = 256;
		TCHAR Buffer1[256], Buffer2[256], Buffer3[256];
		BridgeController->GetCalibrationTemplateConfigVersion(TemplateIndex, &CharsCount1, Buffer1);
		Buffer1[CharsCount1] = 0;

		BridgeController->GetCalibrationTemplateDeviceName(TemplateIndex, &CharsCount2, Buffer2);
		Buffer2[CharsCount2] = 0;

		if (BridgeController->GetCalibrationTemplateSerial(TemplateIndex, &CharsCount3, Buffer3))
		{
			Buffer3[CharsCount3] = 0;
		}
		else
		{
			// If "serial" is empty, the following function will return nothing
			continue;
		}

		FLGDeviceCalibration& Calibration = CalibrationTemplates.AddDefaulted_GetRef();
		Calibration.Name = Buffer2;
		Calibration.Serial = Buffer3;

		UE_LOG(LogLookingGlassBridge, Display, TEXT("Template %d: CfgVersion: %s, DeviceName: %s, Serial: %s"), TemplateIndex, Buffer1, Buffer2, Buffer3);

		// We should initialize values with zeros, because in some cases values aren't changed at all
		int InvView = 0, CellPatternMode = 0, NumberOfCells = 0;
		float Fringe = 0;
		BridgeController->GetCalibrationTemplate(
			TemplateIndex,
			&Calibration.Center,
			&Calibration.Pitch,
			&Calibration.Slope,
			&Calibration.Width,
			&Calibration.Height,
			&Calibration.DPI,
			&Calibration.FlipX,
			&InvView,
			&Calibration.ViewCone,
			&Fringe,
			&CellPatternMode,
			&NumberOfCells, nullptr);
		UE_LOG(LogLookingGlassBridge, Display, TEXT("  Center=%g, Pitch=%g, Slope=%g, DPI=%g, FlipX=%g, Width=%d, Height=%d, Aspect=%g"),
			Calibration.Center, Calibration.Pitch, Calibration.Slope, Calibration.DPI, Calibration.FlipX, Calibration.Width, Calibration.Height, Calibration.Aspect);
	}

	ReadDisplays_BridgeThread();

	if (Displays.Num() == 0)
	{
		ReportError(TEXT("No Looking Glass displays found"));
	}

	return true;
}

void FLookingGlassBridge::ReadDisplays_BridgeThread()
{
	Displays.Empty();

	if (BridgeController == nullptr)
	{
		return;
	}

	int32 NumDisplays = 0;
	BridgeController->GetDisplays(&NumDisplays, nullptr);
	if (NumDisplays == 0)
	{
		return;
	}

	TArray<unsigned long> DisplayIds;
	DisplayIds.SetNumZeroed(NumDisplays);
	BridgeController->GetDisplays(&NumDisplays, DisplayIds.GetData());

	Displays.Empty(NumDisplays);

	for (unsigned long DisplayId : DisplayIds)
	{
		const int32 BufferSize = 256;
		TCHAR Buffer[BufferSize];
		FLGDeviceCalibration& Display = Displays.AddDefaulted_GetRef();

		int32 TempInt = BufferSize;
		BridgeController->GetDeviceSerialForDisplay(DisplayId, &TempInt, Buffer);
		Display.Serial = Buffer;

		TempInt = BufferSize;
		BridgeController->GetDeviceNameForDisplay(DisplayId, &TempInt, Buffer);
		Display.Name = Buffer;

		int InvView = 0, CellPatternMode = 0, NumberOfCells = 0;
		float Fringe = 0;
		BridgeController->GetCalibrationForDisplay(DisplayId,
			&Display.Center,
			&Display.Pitch,
			&Display.Slope,
			&Display.Width,
			&Display.Height,
			&Display.DPI,
			&Display.FlipX,
			&InvView,
			&Display.ViewCone,
			&Fringe,
			&CellPatternMode,
			&NumberOfCells,
			nullptr);

		BridgeController->GetDisplayAspectForDisplay(DisplayId, &Display.Aspect);

		long WinX = 0, WinY = 0;
		BridgeController->GetWindowPositionForDisplay(DisplayId, &WinX, &WinY);
		Display.XPos = (int32)WinX;
		Display.YPos = (int32)WinY;

		UE_LOG(LogLookingGlassBridge, Display, TEXT("Display %s (%s): Center=%g, Pitch=%g, Slope=%g, DPI=%g, FlipX=%g, Width=%d, Height=%d, Aspect=%g, Pos=%d,%d"),
			*Display.Name, *Display.Serial, Display.Center, Display.Pitch, Display.Slope, Display.DPI, Display.FlipX,
			Display.Width, Display.Height, Display.Aspect, Display.XPos, Display.YPos);
	}
}

static_assert(sizeof(int32) == sizeof(WINDOW_HANDLE));

static void LKGBridgeDiag(const FString& Message)
{
	// Shipping builds have no UE log - append to a diag file next to the top-level exe
	const FString Line = FString::Printf(TEXT("%s (%s)\n"), *Message, *FDateTime::Now().ToString());
	FFileHelper::SaveStringToFile(Line, *(FPaths::RootDir() / TEXT("lkg_diag.txt")),
		FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);
	UE_LOG(LogLookingGlassBridge, Warning, TEXT("%s"), *Message);
}

/**
 * The one thread every Bridge SDK call runs on (the SDK has hard thread affinity to whichever
 * thread initialized the controller, and its calls stall for 0.7-20 seconds in the field - on
 * the game thread those stalls froze the whole game). The thread pumps Windows messages so the
 * bridge window it creates behaves, drains queued commands, and presents the latest pending
 * quilt frame; stalls only ever block this thread.
 */
class FLookingGlassBridgeThread : public FRunnable
{
public:
	FLookingGlassBridgeThread(FLookingGlassBridge* InBridge)
		: Bridge(InBridge)
	{
		WakeEventHandle = CreateEventW(nullptr, 0, 0, nullptr);
		Thread = FRunnableThread::Create(this, TEXT("LookingGlassBridge"), 0, TPri_AboveNormal);
	}

	virtual ~FLookingGlassBridgeThread()
	{
		bQuit = true;
		SetEvent(WakeEventHandle);
		if (Thread)
		{
			Thread->WaitForCompletion();
			delete Thread;
		}
		CloseHandle(WakeEventHandle);
	}

	/** Fire-and-forget command */
	void Enqueue(TUniqueFunction<void()> Function)
	{
		Commands.Enqueue(MoveTemp(Function));
		SetEvent(WakeEventHandle);
	}

	/** Runs the command on the bridge thread and blocks until it finishes (boot/shutdown only) */
	void EnqueueAndWait(TUniqueFunction<void()> Function)
	{
		FEvent* DoneEvent = FPlatformProcess::GetSynchEventFromPool(false);
		Enqueue([&Function, DoneEvent]()
		{
			Function();
			DoneEvent->Trigger();
		});
		DoneEvent->Wait();
		FPlatformProcess::ReturnSynchEventToPool(DoneEvent);
	}

	/** Latest-wins frame submission */
	void QueueDraw(void* Texture, int32 QuiltDX, int32 QuiltDY, float Aspect)
	{
		{
			FScopeLock Lock(&PendingLock);
			PendingTexture = Texture;
			PendingTiles = FIntPoint(QuiltDX, QuiltDY);
			PendingAspect = Aspect;
			bHasPending = true;
		}
		SetEvent(WakeEventHandle);
	}

	virtual uint32 Run() override
	{
		while (!bQuit)
		{
			// Pump messages for any windows created on this thread (the bridge window), and wake
			// on either a new message or our event
			MSG Msg;
			while (PeekMessageW(&Msg, nullptr, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&Msg);
				DispatchMessageW(&Msg);
			}

			// Drain commands
			TUniqueFunction<void()> Command;
			while (Commands.Dequeue(Command))
			{
				Command();
				Command = nullptr;
			}

			// Present the newest pending frame, if any
			void* Texture = nullptr;
			FIntPoint Tiles(1, 1);
			float Aspect = 1.0f;
			bool bDraw = false;
			{
				FScopeLock Lock(&PendingLock);
				if (bHasPending)
				{
					Texture = PendingTexture;
					Tiles = PendingTiles;
					Aspect = PendingAspect;
					bHasPending = false;
					bDraw = true;
				}
			}
			if (bDraw && Bridge->bInitialized)
			{
				DrawStartTime = FPlatformTime::Seconds();
				bDrawInFlight = true;
				if (!Bridge->IsRendering())
				{
					Bridge->StartRendering_BridgeThread();
				}
				Bridge->DrawTexture_BridgeThread(Texture, Tiles.X, Tiles.Y, Aspect);
				bDrawInFlight = false;
				continue; // check for newer work before sleeping
			}

			MsgWaitForMultipleObjects(1, &WakeEventHandle, 0, 100, QS_ALLINPUT);
		}
		return 0;
	}

public:
	// Mid-stall diagnostics: which thread to stack-walk and whether a draw is currently blocked
	TAtomic<bool> bDrawInFlight { false };
	TAtomic<double> DrawStartTime { 0.0 };
	uint32 GetThreadId() const { return Thread ? Thread->GetThreadID() : 0; }

private:
	FLookingGlassBridge* Bridge = nullptr;
	FRunnableThread* Thread = nullptr;
	HANDLE WakeEventHandle = nullptr;
	TQueue<TUniqueFunction<void()>, EQueueMode::Mpsc> Commands;
	FCriticalSection PendingLock;
	void* PendingTexture = nullptr;
	FIntPoint PendingTiles = FIntPoint(1, 1);
	float PendingAspect = 1.0f;
	bool bHasPending = false;
	TAtomic<bool> bQuit { false };
};

bool FLookingGlassBridge::Initialize()
{
	// Idempotent: StartPlayer initializes on demand (game viewports are created before
	// PostEngineInit), and the PostEngineInit hook may then call this a second time
	if (bInitialized)
	{
		return true;
	}

	if (BridgeThread == nullptr)
	{
		BridgeThread = new FLookingGlassBridgeThread(this);
	}

	// Block once at boot - after this, every Bridge interaction is asynchronous
	bool bResult = false;
	BridgeThread->EnqueueAndWait([this, &bResult]()
	{
		bResult = Initialize_BridgeThread();
	});
	return bResult;
}

void FLookingGlassBridge::Shutdown()
{
	if (BridgeThread != nullptr)
	{
		BridgeThread->EnqueueAndWait([this]()
		{
			if (bInitialized)
			{
				StopRendering_BridgeThread();
			}
			if (BridgeController != nullptr)
			{
				BridgeController->Uninitialize();
				delete BridgeController;
				BridgeController = nullptr;
			}
		});
		delete BridgeThread;
		BridgeThread = nullptr;
	}
}

void FLookingGlassBridge::RequestReadDisplays()
{
	if (BridgeThread != nullptr && bInitialized)
	{
		BridgeThread->Enqueue([this]()
		{
			ReadDisplays_BridgeThread();
		});
	}
}

void FLookingGlassBridge::RequestStopRendering()
{
	if (BridgeThread != nullptr && bInitialized)
	{
		BridgeThread->Enqueue([this]()
		{
			StopRendering_BridgeThread();
		});
	}
}

void FLookingGlassBridge::QueueDraw(void* Texture, int32 QuiltDX, int32 QuiltDY, float Aspect)
{
	if (BridgeThread != nullptr && bInitialized)
	{
		BridgeThread->QueueDraw(Texture, QuiltDX, QuiltDY, Aspect);
	}
}

bool FLookingGlassBridge::IsDrawStuck(double StuckSeconds, uint32& OutBridgeThreadId) const
{
	if (BridgeThread == nullptr || !BridgeThread->bDrawInFlight)
	{
		return false;
	}
	OutBridgeThreadId = BridgeThread->GetThreadId();
	return (FPlatformTime::Seconds() - BridgeThread->DrawStartTime) > StuckSeconds;
}

void FLookingGlassBridge::StartRendering_BridgeThread()
{
	check(bInitialized);

	//todo: DeviceIndex - pass it as 4th param of instance_window_dx()
	if (LGWindow == NoWindow)
	{
		BridgeController->InstanceWindowDX((IUnknown*)GDynamicRHI->RHIGetNativeDevice(), (WINDOW_HANDLE*)&LGWindow);
	}
	else
	{
		BridgeController->ShowWindow(LGWindow, true);
	}
}

void FLookingGlassBridge::StopRendering_BridgeThread()
{
	check(bInitialized);

	if (TextureRegistered != nullptr)
	{
		BridgeController->UnregisterTextureDX(LGWindow, (IUnknown*)TextureRegistered);
		TextureRegistered = nullptr;
	}
	if (LGWindow != NoWindow)
	{
		BridgeController->ShowWindow(LGWindow, false);
	}
}

void FLookingGlassBridge::DrawTexture_BridgeThread(void* Texture, int32 QuiltDX, int32 QuiltDY, float Aspect)
{
	check(bInitialized);

	// Fine-grained stall diagnostics: each sub-call is timed separately so lkg_diag.txt can name
	// the exact Bridge API that stalled (DrawInteropQuiltTextureDX measured at up to 19.75s)
	double T0 = FPlatformTime::Seconds();

	// Just in case - check if there's already a registered texture
	if (TextureRegistered != Texture && TextureRegistered != nullptr)
	{
		LKGBridgeDiag(FString::Printf(TEXT("Bridge texture handle CHANGED %p -> %p, re-registering"), TextureRegistered, Texture));
		BridgeController->UnregisterTextureDX(LGWindow, (IUnknown*)TextureRegistered);
		TextureRegistered = nullptr;
		const double Elapsed = FPlatformTime::Seconds() - T0;
		if (Elapsed > 0.25)
		{
			LKGBridgeDiag(FString::Printf(TEXT("Bridge UnregisterTextureDX stalled for %.2f seconds"), Elapsed));
		}
	}

	T0 = FPlatformTime::Seconds();
	if (TextureRegistered == nullptr)
	{
		BridgeController->RegisterTextureDX(LGWindow, (IUnknown*)Texture);
		TextureRegistered = Texture;
		const double Elapsed = FPlatformTime::Seconds() - T0;
		if (Elapsed > 0.25)
		{
			LKGBridgeDiag(FString::Printf(TEXT("Bridge RegisterTextureDX stalled for %.2f seconds"), Elapsed));
		}
	}

	T0 = FPlatformTime::Seconds();
	BridgeController->DrawInteropQuiltTextureDX(LGWindow, (IUnknown*)TextureRegistered, QuiltDX, QuiltDY, Aspect, 1.0f);
	{
		const double Elapsed = FPlatformTime::Seconds() - T0;
		if (Elapsed > 0.25)
		{
			LKGBridgeDiag(FString::Printf(TEXT("Bridge DrawInteropQuiltTextureDX stalled for %.2f seconds - on bridge thread, game unaffected"), Elapsed));
		}
	}
}

#undef LOCTEXT_NAMESPACE
