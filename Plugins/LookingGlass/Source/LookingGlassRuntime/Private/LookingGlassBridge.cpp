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
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
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
#include <tlhelp32.h> // running-process scan in FindBridgeInstallCandidates
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

static void LKGBridgeDiag(const FString& Message);

/** Directory of every running LookingGlassBridge.exe (normally zero or one). */
static TArray<FString> FindRunningBridgeExeDirs()
{
	TArray<FString> Dirs;
	HANDLE Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (Snapshot == INVALID_HANDLE_VALUE)
	{
		return Dirs;
	}
	PROCESSENTRY32W Entry;
	Entry.dwSize = sizeof(Entry);
	if (Process32FirstW(Snapshot, &Entry))
	{
		do
		{
			if (FCString::Stricmp(Entry.szExeFile, TEXT("LookingGlassBridge.exe")) != 0)
			{
				continue;
			}
			HANDLE Process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, 0, Entry.th32ProcessID);
			if (Process == nullptr)
			{
				continue;
			}
			WCHAR PathBuffer[MAX_PATH * 2];
			DWORD PathLen = UE_ARRAY_COUNT(PathBuffer);
			if (QueryFullProcessImageNameW(Process, 0, PathBuffer, &PathLen))
			{
				Dirs.AddUnique(FPaths::GetPath(FString(PathBuffer)));
			}
			CloseHandle(Process);
		} while (Process32NextW(Snapshot, &Entry));
	}
	CloseHandle(Snapshot);
	return Dirs;
}

/** "Looking Glass Bridge 2.6.3" -> 2006003, anything unparseable -> 0 */
static int32 ParseBridgeFolderVersion(const FString& FolderName)
{
	FString VersionPart;
	if (!FolderName.Split(TEXT("Bridge "), nullptr, &VersionPart))
	{
		return 0;
	}
	TArray<FString> Parts;
	VersionPart.ParseIntoArray(Parts, TEXT("."));
	int32 Version = 0;
	for (int32 i = 0; i < 3; i++)
	{
		Version = Version * 1000 + (Parts.IsValidIndex(i) ? FCString::Atoi(*Parts[i]) : 0);
	}
	return Version;
}

/** Every "<Program Files>\Looking Glass\Looking Glass Bridge *" folder, highest version first. */
static TArray<FString> FindInstalledBridgeDirs()
{
	TArray<TPair<int32, FString>> Found;
	for (const TCHAR* EnvVar : { TEXT("ProgramFiles"), TEXT("ProgramFiles(x86)"), TEXT("ProgramW6432") })
	{
		const FString Root = FPlatformMisc::GetEnvironmentVariable(EnvVar);
		if (Root.IsEmpty())
		{
			continue;
		}
		const FString Parent = Root / TEXT("Looking Glass");
		TArray<FString> SubDirs;
		IFileManager::Get().FindFiles(SubDirs, *(Parent / TEXT("Looking Glass Bridge*")), false, true);
		for (const FString& SubDir : SubDirs)
		{
			Found.AddUnique(TPair<int32, FString>(ParseBridgeFolderVersion(SubDir), Parent / SubDir));
		}
	}
	Found.Sort([](const TPair<int32, FString>& A, const TPair<int32, FString>& B) { return A.Key > B.Key; });
	TArray<FString> Dirs;
	for (const auto& Pair : Found)
	{
		Dirs.AddUnique(Pair.Value);
	}
	return Dirs;
}

bool FLookingGlassBridge::Initialize_BridgeThread()
{
	LKGBridgeDiag(TEXT("---- Bridge boot ----"));

	// Load the Bridge. The SDK's own Controller::Initialize can only find bridge_inproc.dll through
	// the PER-USER %APPDATA%\Looking Glass\Bridge\settings.json "install_locations" list, which the
	// installer only writes for the account that ran it (a laptop where Bridge was installed from
	// another account had Bridge running fine, yet that lookup failed and the plugin silently fell
	// back to the debug quilt window). So try, in order: a command-line override, the SDK lookup,
	// the folder of a RUNNING LookingGlassBridge.exe, and a Program Files scan.
	BridgeController = new ControllerWithCalibrationTemplates();

	TArray<TPair<FString, FString>> Candidates; // (source, dir)
	FString OverrideDir;
	if (FParse::Value(FCommandLine::Get(), TEXT("lkgbridgedir="), OverrideDir))
	{
		Candidates.Add(TPair<FString, FString>(TEXT("-lkgbridgedir"), OverrideDir.TrimQuotes()));
	}
	{
		const FString SettingsPath = FString(BridgeController->SettingsPath().c_str());
		const bool bSettingsExists = IFileManager::Get().FileExists(*SettingsPath);
		if (FParse::Param(FCommandLine::Get(), TEXT("lkgnosettings")))
		{
			LKGBridgeDiag(FString::Printf(TEXT("settings.json lookup skipped (-lkgnosettings): %s"), *SettingsPath));
		}
		else
		{
			// (the SDK hands back the raw JSON text, backslashes still escaped)
			const FString SettingsDir = FString(BridgeController->BridgeInstallLocation(::BridgeVersion).c_str()).Replace(TEXT("\\\\"), TEXT("\\"));
			LKGBridgeDiag(FString::Printf(TEXT("settings.json %s: %s -> install_locations%s"),
				bSettingsExists ? TEXT("found") : TEXT("MISSING"), *SettingsPath,
				SettingsDir.IsEmpty() ? TEXT(" gave no usable path") : *(TEXT(": ") + SettingsDir)));
			if (!SettingsDir.IsEmpty())
			{
				Candidates.Add(TPair<FString, FString>(TEXT("settings.json"), SettingsDir));
			}
		}
	}
	for (const FString& Dir : FindRunningBridgeExeDirs())
	{
		Candidates.Add(TPair<FString, FString>(TEXT("running LookingGlassBridge.exe"), Dir));
	}
	for (const FString& Dir : FindInstalledBridgeDirs())
	{
		Candidates.Add(TPair<FString, FString>(TEXT("Program Files scan"), Dir));
	}

	bool bLoaded = false;
	TSet<FString> Tried;
	for (const auto& Candidate : Candidates)
	{
		FString Dir = FPaths::ConvertRelativePathToFull(Candidate.Value);
		FPaths::NormalizeDirectoryName(Dir);
		if (Tried.Contains(Dir.ToLower()))
		{
			continue;
		}
		Tried.Add(Dir.ToLower());

		if (!IFileManager::Get().FileExists(*(Dir / TEXT("bridge_inproc.dll"))))
		{
			LKGBridgeDiag(FString::Printf(TEXT("candidate (%s) %s: no bridge_inproc.dll there, skipped"), *Candidate.Key, *Dir));
			continue;
		}

		// initialize_bridge also fails when the Bridge service isn't up yet (it is often starting
		// alongside the game at login) - give each real install a couple of chances.
		for (int32 Attempt = 0; Attempt < 3 && !bLoaded; Attempt++)
		{
			if (Attempt > 0)
			{
				FPlatformProcess::Sleep(1.0f);
			}
			bLoaded = BridgeController->InitializeWithPath(TEXT("UnrealEnginePlugin"), std::wstring(*Dir));
		}
		LKGBridgeDiag(FString::Printf(TEXT("candidate (%s) %s: initialize_bridge %s"), *Candidate.Key, *Dir, bLoaded ? TEXT("OK") : TEXT("FAILED")));
		if (bLoaded)
		{
			InstallDir = Dir;
			break;
		}
	}

	if (!bLoaded)
	{
		ReportError(TEXT("Bridge initialization failed"));
		LKGBridgeDiag(Candidates.Num() == 0
			? TEXT("Bridge initialization failed: no Looking Glass Bridge installation found at all (is Bridge installed and running?)")
			: TEXT("Bridge initialization failed: no candidate loaded (is the Looking Glass Bridge tray app running?)"));
		delete BridgeController;
		BridgeController = nullptr;
		return false;
	}

	// Verify installed version
	unsigned long Major = 0, Minor = 0, Build = 0;
	int32 NumPostfixChars = 0;
	BridgeController->GetBridgeVersion(&Major, &Minor, &Build, &NumPostfixChars, nullptr);
	VersionString = FString::Printf(TEXT("%d.%d.%d"), Major, Minor, Build);
	LKGBridgeDiag(FString::Printf(TEXT("Bridge %s loaded from %s"), *VersionString, *InstallDir));

	int32 Version = Major * 1000000 + Minor * 1000 + Build;
	int32 Desired = BRIDGE_VERSION_MAJOR * 1000000 + BRIDGE_VERSION_MINOR * 1000 + BRIDGE_VERSION_BUILD;
	if (Version < Desired)
	{
		const FString Msg = FString::Printf(
			TEXT("The installed Looking Glass Bridge has version %d.%d.%d, required version is %d.%d.%d, please update!"),
			Major, Minor, Build, BRIDGE_VERSION_MAJOR, BRIDGE_VERSION_MINOR, BRIDGE_VERSION_BUILD);
		ReportError(Msg);
		LKGBridgeDiag(Msg);
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
		LKGBridgeDiag(TEXT("Bridge reports NO Looking Glass displays (check the USB cable and that Bridge itself shows the display)"));
	}

	return true;
}

// Per-serial disk cache of the last good calibration. The values are static per-device EEPROM
// data, so a cached copy is exactly as good as a live read - and Bridge sometimes serves a
// display whose calibration is ALL ZEROS (serial/name/position intact) after an abnormal game
// exit (an Alt-F4 kill mid-session) leaves the device's USB calibration interface wedged until
// it re-enumerates (replug or reboot). Without the fallback that boot creates a 0x0 self-render
// window = nothing on the panel at all.
static FString CalibCacheFilePath()
{
	return FPaths::ProjectSavedDir() / TEXT("lkg_calibration_cache.txt");
}

static TMap<FString, FLGDeviceCalibration> LoadCalibrationCache()
{
	TMap<FString, FLGDeviceCalibration> Cache;
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *CalibCacheFilePath()))
	{
		return Cache;
	}
	for (const FString& Line : Lines)
	{
		TArray<FString> Parts;
		Line.ParseIntoArray(Parts, TEXT("|"), false);
		if (Parts.Num() < 11)
		{
			continue;
		}
		FLGDeviceCalibration C;
		C.Serial = Parts[0];
		C.Name = Parts[1];
		C.Center = FCString::Atof(*Parts[2]);
		C.Pitch = FCString::Atof(*Parts[3]);
		C.Slope = FCString::Atof(*Parts[4]);
		C.DPI = FCString::Atof(*Parts[5]);
		C.FlipX = FCString::Atof(*Parts[6]);
		C.Width = FCString::Atoi(*Parts[7]);
		C.Height = FCString::Atoi(*Parts[8]);
		C.Aspect = FCString::Atof(*Parts[9]);
		C.ViewCone = FCString::Atof(*Parts[10]);
		if (!C.Serial.IsEmpty() && C.Width > 0 && C.Height > 0)
		{
			Cache.Add(C.Serial, C);
		}
	}
	return Cache;
}

static void SaveCalibrationCache(const TMap<FString, FLGDeviceCalibration>& Cache)
{
	FString Out;
	for (const auto& Pair : Cache)
	{
		const FLGDeviceCalibration& C = Pair.Value;
		Out += FString::Printf(TEXT("%s|%s|%.9g|%.9g|%.9g|%.9g|%.9g|%d|%d|%.9g|%.9g\n"),
			*C.Serial, *C.Name, C.Center, C.Pitch, C.Slope, C.DPI, C.FlipX, C.Width, C.Height, C.Aspect, C.ViewCone);
	}
	FFileHelper::SaveStringToFile(Out, *CalibCacheFilePath());
}

void FLookingGlassBridge::ReadDisplays_BridgeThread()
{
	Displays.Empty();

	if (BridgeController == nullptr)
	{
		return;
	}

	// A wedged calibration read is usually persistent, but retry a couple of times anyway in
	// case it was a transient race (e.g. another client mid-read)
	for (int32 Attempt = 0; Attempt < 3; Attempt++)
	{
		if (Attempt > 0)
		{
			FPlatformProcess::Sleep(0.5f);
			LKGBridgeDiag(FString::Printf(TEXT("Re-reading displays (attempt %d) - a display had no calibration"), Attempt + 1));
		}
		Displays.Empty();

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

		bool bAnyMissingCalibration = false;
		for (unsigned long DisplayId : DisplayIds)
		{
			// Note: the string getters return the char count and do NOT null-terminate (the template
			// loop above already handles that; this one used to hand FString a raw uninitialized buffer)
			const int32 BufferSize = 256;
			TCHAR Buffer[BufferSize + 1];
			FLGDeviceCalibration& Display = Displays.AddDefaulted_GetRef();

			int32 TempInt = BufferSize;
			FMemory::Memzero(Buffer);
			if (BridgeController->GetDeviceSerialForDisplay(DisplayId, &TempInt, Buffer))
			{
				Buffer[FMath::Clamp(TempInt, 0, BufferSize)] = 0;
			}
			Display.Serial = Buffer;

			TempInt = BufferSize;
			FMemory::Memzero(Buffer);
			if (BridgeController->GetDeviceNameForDisplay(DisplayId, &TempInt, Buffer))
			{
				Buffer[FMath::Clamp(TempInt, 0, BufferSize)] = 0;
			}
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

			int DeviceType = -1;
			BridgeController->GetDeviceTypeForDisplay(DisplayId, &DeviceType);

			LKGBridgeDiag(FString::Printf(TEXT("Display %d: '%s' serial '%s' type %d: %dx%d at %d,%d, Center=%g, Pitch=%g, Slope=%g, DPI=%g, FlipX=%g, Aspect=%g, ViewCone=%g"),
				Displays.Num() - 1, *Display.Name, *Display.Serial, DeviceType, Display.Width, Display.Height, Display.XPos, Display.YPos,
				Display.Center, Display.Pitch, Display.Slope, Display.DPI, Display.FlipX, Display.Aspect, Display.ViewCone));
			if (Display.Width <= 0 || Display.Height <= 0)
			{
				LKGBridgeDiag(TEXT("  WARNING: calibration has no width/height - Bridge could not read this display's calibration"));
				bAnyMissingCalibration = true;
			}
		}

		if (!bAnyMissingCalibration)
		{
			break;
		}
	}

	// Calibration cache: save what came through, heal what did not
	TMap<FString, FLGDeviceCalibration> Cache = LoadCalibrationCache();
	bool bCacheDirty = false;
	for (FLGDeviceCalibration& Display : Displays)
	{
		if (Display.Serial.IsEmpty())
		{
			continue;
		}
		if (Display.Width > 0 && Display.Height > 0)
		{
			const FLGDeviceCalibration* Cached = Cache.Find(Display.Serial);
			if (Cached == nullptr || Cached->Width != Display.Width || Cached->Height != Display.Height ||
				Cached->Center != Display.Center || Cached->Pitch != Display.Pitch || Cached->Slope != Display.Slope ||
				Cached->DPI != Display.DPI || Cached->FlipX != Display.FlipX || Cached->Aspect != Display.Aspect ||
				Cached->ViewCone != Display.ViewCone)
			{
				Cache.Add(Display.Serial, Display);
				bCacheDirty = true;
			}
		}
		else if (const FLGDeviceCalibration* Cached = Cache.Find(Display.Serial))
		{
			// keep the live serial/name/window position; restore the static per-device values
			Display.Center = Cached->Center;
			Display.Pitch = Cached->Pitch;
			Display.Slope = Cached->Slope;
			Display.DPI = Cached->DPI;
			Display.FlipX = Cached->FlipX;
			Display.Width = Cached->Width;
			Display.Height = Cached->Height;
			Display.Aspect = Cached->Aspect;
			Display.ViewCone = Cached->ViewCone;
			LKGBridgeDiag(FString::Printf(TEXT("  RECOVERED with cached calibration for serial '%s' (%dx%d): the device would not serve it (a wedged USB calibration interface - replug the Looking Glass USB cable or reboot to heal it for other apps too)"),
				*Display.Serial, Display.Width, Display.Height));
		}
		else
		{
			LKGBridgeDiag(FString::Printf(TEXT("  no cached calibration for serial '%s' either - the panel will stay dark; replug the Looking Glass USB cable and restart the app"),
				*Display.Serial));
		}
	}
	if (bCacheDirty)
	{
		SaveCalibrationCache(Cache);
	}
}

static_assert(sizeof(int32) == sizeof(WINDOW_HANDLE));

void FLookingGlassBridge::Diag(const FString& Message)
{
	// Shipping builds have no UE log - append to a diag file next to the top-level exe
	const FString Line = FString::Printf(TEXT("%s (%s)\n"), *Message, *FDateTime::Now().ToString());
	FFileHelper::SaveStringToFile(Line, *(FPaths::RootDir() / TEXT("lkg_diag.txt")),
		FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);
	UE_LOG(LogLookingGlassBridge, Display, TEXT("%s"), *Message);
}

static void LKGBridgeDiag(const FString& Message)
{
	FLookingGlassBridge::Diag(Message);
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
