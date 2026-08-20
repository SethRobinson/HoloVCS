#include "LookingGlassRuntime.h"
#include "LookingGlassSettings.h"

#include "LookingGlassBridge.h"

#include "Render/SLookingGlassViewport.h"

#include "Render/LookingGlassViewportClient.h"

#include "Game/LookingGlassCapture.h"

#include "Misc/LookingGlassHelpers.h"
#include "Misc/LookingGlassLog.h"
#include "Game/LookingGlassSceneCaptureComponent2D.h"

#include "Managers/LookingGlassCommandLineManager.h"
#include "Managers/LookingGlassLaunchManager.h"

#include "Async/Async.h"
#include "Slate/SceneViewport.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameUserSettings.h"
#include "UnrealEngine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/GameEngine.h"

#include "Interfaces/IPluginManager.h"
#include "ShaderCore.h" // AddShaderSourceDirectoryMapping

#if WITH_EDITOR
#include "ISequencerObjectChangeListener.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "ISequencerModule.h"
#endif

#define LOCTEXT_NAMESPACE "FLookingGlassRuntimeModule"

#define PLUGIN_NAME TEXT("LookingGlass")

// Self-rendered lenticular output: the plugin's own window sits on the device and runs the
// lenticular shader directly, so Bridge is only consulted for calibration at boot. The Bridge
// interop draw path (0) blocks 0.7-20s per call in the field (see docs/lkg_bridge_stall_report.md).
static TAutoConsoleVariable<int32> CVarLKGSelfRender(
	TEXT("lkg.SelfRender"), 1,
	TEXT("1 = render the lenticular device output in our own window (no per-frame Bridge IPC), 0 = Bridge interop draws the quilt"));

bool LookingGlassSelfRenderEnabled()
{
	return CVarLKGSelfRender.GetValueOnGameThread() != 0;
}

void FLookingGlassRuntimeModule::StartupModule()
{
	// The lenticular global shader lives in the plugin's Shaders dir; map it before the
	// global shader map compiles (this module loads at PostConfigInit)
	FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(PLUGIN_NAME)->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/LookingGlass"), PluginShaderDir);

	// Create all managers
	Managers.Add(LookingGlassLaunchManager = MakeShareable(new FLookingGlassLaunchManager()));
	Managers.Add(LookingGlassCommandLineManager = MakeShareable(new FLookingGlassCommandLineManager()));

	UGameViewportClient::OnViewportCreated().AddRaw(this, &FLookingGlassRuntimeModule::OnGameViewportCreated);

	// Generate default calibration, it will be changed when Play button is clicked
	MakeDefaultCalibration();

	FCoreDelegates::GetOnPostEngineInit().AddLambda([this]()
		{
			// Commandlets (e.g. the UAT cook) have no Slate application and need none of this;
			// FSlateApplication::Get() below would assert and kill the cook.
			if (!FSlateApplication::IsInitialized())
			{
				return;
			}

			// Init all managers
			InitAllManagers();

			// Init all settings
			GetDefault<ULookingGlassSettings>()->PostEngineInit();

			// Initialize Bridge here - just to let error popups to appear with no fuss.
			Bridge.Initialize();

			// Capture message about adding/removing a new display
			TSharedPtr<class GenericApplication> PlatformApplication = FSlateApplication::Get().GetPlatformApplication();
			PlatformApplication->OnDisplayMetricsChanged().AddRaw(this, &FLookingGlassRuntimeModule::OnDisplayMetricsChanged);

#if WITH_EDITOR
			// Register a function which will capture events of creating sequencer. We'll need to get access to the Sequencer
			// module here, and in 5.1 this will cause the crash, as something in the engine is not initialized at a time of
			// loading this plugin. So, we'll delay this till the engine initialization completed.
			ISequencerModule& SequencerModule = FModuleManager::Get().LoadModuleChecked<ISequencerModule>("Sequencer");
			OnSequencerCreatedHandle = SequencerModule.RegisterOnSequencerCreated(
				FOnSequencerCreated::FDelegate::CreateRaw(this, &FLookingGlassRuntimeModule::OnSequencerCreated));
#endif // WITH_EDITOR
		});

	FString PluginBaseDir = IPluginManager::Get().FindPlugin(PLUGIN_NAME)->GetBaseDir();
	LookingGlassLoader.LoadDLL(PluginBaseDir);
}

void FLookingGlassRuntimeModule::ShutdownModule()
{
#if WITH_EDITOR
	ISequencerModule* SequencerModulePtr = FModuleManager::Get().GetModulePtr<ISequencerModule>("Sequencer");
	if (SequencerModulePtr)
	{
		SequencerModulePtr->UnregisterOnSequencerCreated(OnSequencerCreatedHandle);
	}
#endif

	// Release all managers
	for (auto Mng : Managers)
	{
		Mng->Release();
		Mng.Reset();
	}
	Managers.Empty();

	Bridge.Shutdown();

	// Release LookingGlassCore.dll when all manager were destroyed
	LookingGlassLoader.ReleaseDLL();
}

void FLookingGlassRuntimeModule::OnDisplayMetricsChanged(const FDisplayMetrics& InDisplayMetrics)
{
	// Ensure the following code will be executed in context of the main thread, and not in the message handler. Without
	// that, reconfiguring the display when device is connected or disconnected may cause assertions and/or crashes.
	AsyncTask(ENamedThreads::GameThread, [this, InDisplayMetrics]()
		{
			// Reload calibration only if amount of displays has been changed
			if (InDisplayMetrics.MonitorInfo.Num() != NumMonitors)
			{
				PrepareDisplays();
			}

			NumMonitors = InDisplayMetrics.MonitorInfo.Num();
			ULookingGlassSceneCaptureComponent2D::UpdateTilingPropertiesForAllComponents();
		});
}

void FLookingGlassRuntimeModule::PrepareDisplays()
{
	Bridge.RequestReadDisplays();
}

#if WITH_EDITOR

// This is only the valid way of knowing if any sequencer is open: to hook RegisterOnSequencerCreated and remember objects as
// weak pointers. When sequencer is closed, no event is called, however we can validate weak pointer to see if sequencer is still alive.
void FLookingGlassRuntimeModule::OnSequencerCreated(TSharedRef<ISequencer> InSequencer)
{
	Sequencers.Add(TWeakPtr<ISequencer>(InSequencer));
}

bool FLookingGlassRuntimeModule::HasActiveSequencers()
{
	while (Sequencers.IsValidIndex(0))
	{
		if (Sequencers[0].IsValid())
		{
			return true;
		}
		// Cleanup dead pointers
		Sequencers.RemoveAt(0);
	}
	return false;
}

#endif // WITH_EDITOR

void FLookingGlassRuntimeModule::StartPlayer(ELookingGlassModeType LookingGlassModeType)
{
	DISPLAY_HOLOPLAY_FUNC_TRACE(LookingGlassLogPlayer);

	if (bIsPlaying)
	{
		return;
	}

	PrepareDisplays();

	bIsRenderingOnDevice = true;

	const EAutoCenter AutoCenter = EAutoCenter::None;
	EWindowMode::Type WindowType = EWindowMode::Fullscreen;

	// Set Current LookingGlassModeType
	CurrentLookingGlassModeType = LookingGlassModeType;
	const ULookingGlassSettings* LookingGlassSettings = GetDefault<ULookingGlassSettings>();

	// Start all managers
	{
		bool Result = true;
		auto it = Managers.CreateIterator();
		while(Result && it)
		{
			Result = Result && (*it)->OnStartPlayer(CurrentLookingGlassModeType);
			++it;
		}

		if (!Result)
		{
			UE_LOG(LookingGlassLogPlayer, Verbose, TEXT("Error during StartPlayer managers"));
		}
	}

	const FLookingGlassWindowSettings& WindowSettings = LookingGlassSettings->LookingGlassWindowSettings;
	bLockInMainViewport = WindowSettings.bLockInMainViewport || LookingGlassModeType == ELookingGlassModeType::PlayMode_InMainViewport;
	int32 ScreenIndex = WindowSettings.ScreenIndex;

	switch (LookingGlassModeType)
	{
	case ELookingGlassModeType::PlayMode_InSeparateWindow:
	{
		bIsDestroyWindowRequested = false;

		// Fix resizing of rendering texture outside of the editor
		// Always grow scene render targets outside of the editor.
		if (!GIsEditor)
		{
			IConsoleManager& ConsoleMan = IConsoleManager::Get();

			static IConsoleVariable* CVarSceneTargetsResizeMethodForceOverride = ConsoleMan.FindConsoleVariable(TEXT("r.SceneRenderTargetResizeMethodForceOverride"));
			static const int32 SceneTargetsResizeMethodForceOverrideVal = 1;
			CVarSceneTargetsResizeMethodForceOverride->Set(SceneTargetsResizeMethodForceOverrideVal);

			static IConsoleVariable* CVarSceneRenderTargetResizeMethod = ConsoleMan.FindConsoleVariable(TEXT("r.SceneRenderTargetResizeMethod"));
			static const int32 SceneRenderTargetResizeMethodVal = 2;
			CVarSceneRenderTargetResizeMethod->Set(SceneRenderTargetResizeMethodVal);
		}

		// This location should be off-screen, unless there's a monitor with negative Y position. This hides
		// ViewportClient window when rendering on device. There's 'LookingGlassWindow->HideWindow()' below, however
		// it hides window - but not the viewport itself.
		FVector2D ClientSize(100, 100);
		FVector2D ScreenPosition(100, -200);

		// StartPlayer runs at game-viewport creation, which happens BEFORE the PostEngineInit
		// hook that initializes the Bridge - initialize on demand (idempotent, blocks only at
		// boot) so the device list is populated when we decide where the window goes
		if (!Bridge.bInitialized)
		{
			Bridge.Initialize();
		}

		ELookingGlassPlacement PlacementMode = WindowSettings.PlacementMode;
		// If there's no displays with selected ScreenIndex, use debug window
		if (PlacementMode == ELookingGlassPlacement::Automatic && !Bridge.Displays.IsValidIndex(ScreenIndex))
		{
			// No devices found, replace "Automatic" mode with "AlwaysDebugWindow"
			PlacementMode = ELookingGlassPlacement::AlwaysDebugWindow;
			bIsRenderingOnDevice = false;
			FLookingGlassBridge::Diag(FString::Printf(TEXT("NO DEVICE for screen index %d (%d found): falling back to the debug quilt window on the desktop"),
				ScreenIndex, Bridge.Displays.Num()));
		}

		if (PlacementMode == ELookingGlassPlacement::AlwaysDebugWindow)
		{
			ClientSize = WindowSettings.DebugWindowLocation.ClientSize;
			ScreenPosition = WindowSettings.DebugWindowLocation.ScreenPosition;
			WindowType = EWindowMode::Windowed;
			bIsRenderingOnDevice = false;
		}

		bool bSelfRender = false;
		if (bIsRenderingOnDevice)
		{
			CurrentCalibration = Bridge.Displays[ScreenIndex];

			bSelfRender = LookingGlassSelfRenderEnabled();
			if (bSelfRender)
			{
				// Our own borderless window covers the device and runs the lenticular shader;
				// the Bridge window (whose per-frame draws stall) is never created
				ClientSize = FVector2D(CurrentCalibration.Width, CurrentCalibration.Height);
				ScreenPosition = FVector2D(CurrentCalibration.XPos, CurrentCalibration.YPos);
				WindowType = EWindowMode::WindowedFullscreen;
				FLookingGlassBridge::Diag(FString::Printf(TEXT("Self-render window on device '%s' (serial '%s') at %d,%d size %dx%d"),
					*CurrentCalibration.Name, *CurrentCalibration.Serial,
					CurrentCalibration.XPos, CurrentCalibration.YPos, CurrentCalibration.Width, CurrentCalibration.Height));
			}
			else
			{
				FLookingGlassBridge::Diag(FString::Printf(TEXT("Bridge interop window on device '%s' (lkg.SelfRender 0)"), *CurrentCalibration.Name));
			}
		}
		else
		{
			MakeDefaultCalibration();
		}
		// Update tiling settings, mainly needed for "Automatic" preset
		ULookingGlassSceneCaptureComponent2D::UpdateTilingPropertiesForAllComponents();

		LookingGlassWindow = SNew(SWindow)
			.Type(EWindowType::Normal)
			.Style(&FCoreStyle::Get().GetWidgetStyle<FWindowStyle>("Window"))
			.ClientSize(ClientSize)
			.AdjustInitialSizeAndPositionForDPIScale(false)
			.Title(FText::FromString(TEXT("LookingGlass Window")))
			.FocusWhenFirstShown(!bSelfRender)
			.ScreenPosition(ScreenPosition)
			.UseOSWindowBorder(false)
			.CreateTitleBar(!bSelfRender)
			.LayoutBorder(bSelfRender ? FMargin(0) : FMargin(5, 5, 5, 5))
			.AutoCenter(AutoCenter)
			.SaneWindowPlacement(AutoCenter == EAutoCenter::None)
			.SizingRule(bSelfRender ? ESizingRule::FixedSize : ESizingRule::UserSized)
			// Self-render window opens without focus, so make it topmost or it starts (and
			// stays) behind whatever desktop windows sit on the device
			.IsTopmostWindow(bSelfRender ? true : WindowSettings.bToptmostDebugWindow);

		// Always make a ViewportClient, because rendering code is located in this class.
		// In self-render mode the window IS the device output, so it stays visible.
		if (bIsRenderingOnDevice && !bSelfRender)
		{
			LookingGlassWindow->HideWindow();
		}

		// 1. Add new window
		const bool bShowImmediately = false;
		FSlateApplication::Get().AddWindow(LookingGlassWindow.ToSharedRef(), bShowImmediately);

		// 2. Set window mode
		// Do not set fullscreen mode here
		// The window mode will be set properly later
		if (WindowType == EWindowMode::Fullscreen)
		{
			LookingGlassWindow->SetWindowMode(EWindowMode::WindowedFullscreen);
		}
		else
		{
			LookingGlassWindow->SetWindowMode(WindowType);
		}

		// 3. Show window
		// No need to show window in off-screen rendering mode as it does not render to screen
		if (FSlateApplication::Get().IsRenderingOffScreen())
		{
			FSlateApplicationBase::Get().GetRenderer()->CreateViewport(LookingGlassWindow.ToSharedRef());
		}
		else
		{
			LookingGlassWindow->ShowWindow();
		}

		// 4. Tick now to force a redraw of the window and ensure correct fullscreen application
		FSlateApplication::Get().Tick();

		// 5. Add viewport to window
		LookingGlassViewport = SNew(SLookingGlassViewport);
		LookingGlassViewport->GetLookingGlassViewportClient()->SetViewportWindow(LookingGlassWindow);
		LookingGlassWindow->SetContent(LookingGlassViewport.ToSharedRef());
		LookingGlassWindow->SlatePrepass();

		if (WindowType != LookingGlassWindow->GetWindowMode())
		{
			LookingGlassWindow->SetWindowMode(WindowType);
			LookingGlassWindow->ReshapeWindow(ScreenPosition, ClientSize);
			FVector2D NewViewportSize = LookingGlassWindow->GetViewportSize();
			LookingGlassViewport->GetSceneViewport()->UpdateViewportRHI(false, NewViewportSize.X, NewViewportSize.Y, WindowType, PF_Unknown);
			LookingGlassViewport->GetSceneViewport()->Invalidate();

			// Resize backbuffer
			FVector2D NewBackbufferSize = LookingGlassWindow->IsMirrorWindow() ? ClientSize : NewViewportSize;
			FSlateApplicationBase::Get().GetRenderer()->UpdateFullscreenState(LookingGlassWindow.ToSharedRef(), NewBackbufferSize.X, NewBackbufferSize.Y);
		}

		// 6. Add window delegates
		OnWindowClosedDelegate.BindRaw(this, &FLookingGlassRuntimeModule::OnWindowClosed);
		LookingGlassWindow->SetOnWindowClosed(OnWindowClosedDelegate);

		break;
	}
	case ELookingGlassModeType::PlayMode_InMainViewport:
	{
		if (GEngine->GameViewport)
		{
			TSharedPtr<SWindow> GameViewportWindow = GEngine->GameViewport->GetWindow();

			if (WindowSettings.PlacementMode == ELookingGlassPlacement::Automatic)
			{
#if 0 // Disabled because of Bridge - it should handle everything; clean up code when things will be confirmed working
				FVector2D ClientSize;
				FVector2D ScreenPosition;

				const auto& DisplaySettings = GetLookingGlassDisplayManager()->GetDisplaySettings();
				const auto& CalibrationSettings = GetLookingGlassDisplayManager()->GetCalibrationSettings();

				ClientSize.X = CalibrationSettings.ScreenWidth;
				ClientSize.Y = CalibrationSettings.ScreenHeight;

				ScreenPosition.X = DisplaySettings.LKGxpos;
				ScreenPosition.Y = DisplaySettings.LKGypos;

				struct Local
				{
					static void RequestResolutionChange(const FVector2D& InClientSize, const FVector2D InScreenPosition, TSharedPtr<SWindow> Window)
					{
						bool bNewModeApplied = false;
						auto WindowMode = Window->GetWindowMode();

						UGameEngine* GameEngine = Cast<UGameEngine>(GEngine);
						if (GameEngine)
						{
							UGameUserSettings* UserSettings = GameEngine->GetGameUserSettings();
							if (UserSettings != nullptr)
							{
								UserSettings->SetScreenResolution(FIntPoint(InClientSize.X, InClientSize.Y));
								UserSettings->SetFullscreenMode(WindowMode);
								UserSettings->ConfirmVideoMode();
								UserSettings->ApplySettings(false);
								bNewModeApplied = true;
							}
						}

						if (!bNewModeApplied)
						{
							FSystemResolution::RequestResolutionChange(InClientSize.X, InClientSize.Y, Window->GetWindowMode());
						}

						Window->ReshapeWindow(InScreenPosition, InClientSize);
					}
				};
#if WITH_EDITOR
				auto LastExecutedPlayModeType = GetDefault<ULevelEditorPlaySettings>()->LastExecutedPlayModeType;

				// Check placement if bIsStandaloneGame or bIsGameMode
				if (bIsStandaloneGame || bIsGameMode)
				{
					Local::RequestResolutionChange(ClientSize, ScreenPosition, GameViewportWindow);
				}

				//  if Editor Floating window
				else if (LastExecutedPlayModeType == EPlayModeType::PlayMode_InEditorFloating)
				{
					FMargin Border = GameViewportWindow->GetWindowBorderSize();
					ScreenPosition.X -= Border.Left;
					ClientSize.X += Border.Left + Border.Right;
					ScreenPosition.Y -= Border.Top;
					ClientSize.Y += Border.Top + Border.Bottom;

					GameViewportWindow->ReshapeWindow(ScreenPosition, ClientSize);
				}
#else
				{
					Local::RequestResolutionChange(ClientSize, ScreenPosition, GameViewportWindow);
				}
#endif
#endif // 0
			}
			GEngine->GameViewport->OnCloseRequested().AddRaw(this, &FLookingGlassRuntimeModule::OnGameViewportCloseRequested);

			// Create and assign viewport to window
			LookingGlassViewport = SNew(SLookingGlassViewport);
			LookingGlassViewport->GetLookingGlassViewportClient()->SetViewportWindow(GEngine->GameViewport->GetWindow());

			GEngine->GameViewport->AddViewportWidgetContent(LookingGlassViewport.ToSharedRef());
			GEngine->GameViewport->bDisableWorldRendering = true;
		}
		break;
	}
	default:
		return;
	}

	bIsPlaying = true;
}

void FLookingGlassRuntimeModule::MakeDefaultCalibration()
{
	CurrentCalibration = FLGDeviceCalibration();
	// Fill Go Portrait calibration settings for no-device mode
	CurrentCalibration.Name = TEXT("Looking Glass Go Portrait");
	CurrentCalibration.Serial = TEXT("LKG-E");
	CurrentCalibration.Width = 1440;
	CurrentCalibration.Height = 2560;
	CurrentCalibration.Aspect = 0.5625f;
	CurrentCalibration.ViewCone = 54;
}

void FLookingGlassRuntimeModule::StopPlayer()
{
	DISPLAY_HOLOPLAY_FUNC_TRACE(LookingGlassLogPlayer);

	if (bIsPlaying == true)
	{
		// Stop all managers
		for (auto Mng : Managers)
		{
			Mng->OnStopPlayer();
		}

		auto PlayModeType = CurrentLookingGlassModeType;

		// Stop PlayInStandalon first
		if (bIsStandaloneGame || bIsCaptureStandaloneMovie || bLockInMainViewport)
		{
			PlayModeType = ELookingGlassModeType::PlayMode_InMainViewport;
		}

		switch (PlayModeType)
		{
		case ELookingGlassModeType::PlayMode_InSeparateWindow:
		{
			if (bIsDestroyWindowRequested == false)
			{
				bIsDestroyWindowRequested = true;

				if (FSlateApplication::IsInitialized())
				{
					LookingGlassWindow->RequestDestroyWindow();
				}
				else
				{
					LookingGlassWindow->DestroyWindowImmediately();
				}

				// Stop player
				OnWindowClosedDelegate.Unbind();

				LookingGlassViewport.Reset();
				LookingGlassWindow.Reset();
			}

			break;
		}
		case ELookingGlassModeType::PlayMode_InMainViewport:
		{
			if (GEngine->GameViewport)
			{
				GEngine->GameViewport->bDisableWorldRendering = false;
				GEngine->GameViewport->RemoveViewportWidgetContent(
					LookingGlassViewport.ToSharedRef()
				);

				LookingGlassViewport.Reset();
			}

			break;
		}
		default:
			return;
		}

		bIsPlaying = false;
	}
}

void FLookingGlassRuntimeModule::RestartPlayer(ELookingGlassModeType LookingGlassModeType)
{
	StopPlayer();
	StartPlayer(LookingGlassModeType);
}

void FLookingGlassRuntimeModule::OnWindowClosed(const TSharedRef<SWindow>& Window)
{
	StopPlayer();
}

void FLookingGlassRuntimeModule::OnGameViewportCloseRequested(FViewport* InViewport)
{
	StopPlayer();
}

TSharedPtr<FLookingGlassCommandLineManager> FLookingGlassRuntimeModule::GetLookingGlassCommandLineManager()
{
	return LookingGlassCommandLineManager;
}

void FLookingGlassRuntimeModule::StartPlayerSeparateProccess()
{
	auto LookingGlassSettings = GetDefault<ULookingGlassSettings>();
	bLockInMainViewport = LookingGlassSettings->LookingGlassWindowSettings.bLockInMainViewport;
	auto LastExecutedPlayModeType = bLockInMainViewport ? ELookingGlassModeType::PlayMode_InMainViewport : LookingGlassSettings->LookingGlassWindowSettings.LastExecutedPlayModeType;

	// Self-render puts the lenticular output in the plugin's own window; main-viewport mode has
	// no such window, so it must run in separate-window mode regardless of saved/cooked config
	if (LookingGlassSelfRenderEnabled())
	{
		LastExecutedPlayModeType = ELookingGlassModeType::PlayMode_InSeparateWindow;
		bLockInMainViewport = false;
	}

#if WITH_EDITOR
	// Try to parse if this is standalone mode
	{
		FString StandaloneSessionName = "Play in Standalone Game";
		bIsStandaloneGame = FApp::GetSessionName() == StandaloneSessionName;
		if (bIsStandaloneGame)
		{
			StartPlayer(ELookingGlassModeType::PlayMode_InMainViewport);
			return;
		}
	}

	// Try to parse separate process in capture scene
	{
		FString CaptureStandaloneMovie;
		// Try to read screen model from command line
		FParse::Value(FCommandLine::Get(), TEXT("-MovieSceneCaptureManifest="), CaptureStandaloneMovie);

		bIsCaptureStandaloneMovie = !CaptureStandaloneMovie.IsEmpty();
		if (bIsCaptureStandaloneMovie)
		{
			StartPlayer(ELookingGlassModeType::PlayMode_InMainViewport);
			return;
		}
	}

	// Play in editor game mode
	{
		
		bool OnOff = true;
		bIsGameMode = FParse::Bool(FCommandLine::Get(), TEXT("-game"), OnOff);
		if (bIsGameMode)
		{
			StartPlayer(LastExecutedPlayModeType);
			return;
		}
	}

#else
	StartPlayer(LastExecutedPlayModeType);
#endif
}

void FLookingGlassRuntimeModule::OnGameViewportCreated()
{
	if (!GIsEditor)
	{
		// We're in game mode
		if (ensureMsgf(bSeparateProccessPlayerStarded == false, TEXT("StartPlayer in separate proccess was already called")))
		{
			// Init all managers
			InitAllManagers();

			StartPlayerSeparateProccess();
			bSeparateProccessPlayerStarded = true;
		}
	}
}

void FLookingGlassRuntimeModule::InitAllManagers()
{
	if (bIsManagerInit == false)
	{
		bIsManagerInit = true;

		bool Result = true;
		auto it = Managers.CreateIterator();
		while (Result && it)
		{
			Result = Result && (*it)->Init();
			++it;
		}

		if (!Result)
		{
			UE_LOG(LookingGlassLogPlayer, Verbose, TEXT("Error during initialize managers"));
		}
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FLookingGlassRuntimeModule, LookingGlassRuntime)
