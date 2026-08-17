#include "LookingGlassBridge.h"

#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"

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
	FNotificationInfo Info(FText::FromString(Message));
	Info.ExpireDuration = 15.0f;
	Info.bUseSuccessFailIcons = true;
	Info.Image = FAppStyle::GetBrush(TEXT("MessageLog.Warning"));
	TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);
#endif // WITH_EDITOR
}

bool FLookingGlassBridge::Initialize()
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

	ReadDisplays();

	if (Displays.Num() == 0)
	{
		ReportError(TEXT("No Looking Glass displays found"));
	}

	return true;
}

void FLookingGlassBridge::ReadDisplays()
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
	}
}

void FLookingGlassBridge::Shutdown()
{
	if (BridgeController != nullptr)
	{
		BridgeController->Uninitialize();
		delete BridgeController;
		BridgeController = nullptr;
	}
}

static_assert(sizeof(int32) == sizeof(WINDOW_HANDLE));

void FLookingGlassBridge::StartRendering()
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

void FLookingGlassBridge::StopRendering()
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

void FLookingGlassBridge::DrawTexture(void* Texture, int32 QuiltDX, int32 QuiltDY, float Aspect)
{
	check(bInitialized);

	// Just in case - check if there's already a registered texture
	if (TextureRegistered != Texture && TextureRegistered != nullptr)
	{
		BridgeController->UnregisterTextureDX(LGWindow, (IUnknown*)TextureRegistered);
		TextureRegistered = nullptr;
	}

	if (TextureRegistered == nullptr)
	{
		BridgeController->RegisterTextureDX(LGWindow, (IUnknown*)Texture);
		TextureRegistered = Texture;
	}

	BridgeController->DrawInteropQuiltTextureDX(LGWindow, (IUnknown*)TextureRegistered, QuiltDX, QuiltDY, Aspect, 1.0f);
}

#undef LOCTEXT_NAMESPACE
