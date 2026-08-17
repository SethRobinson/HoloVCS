#include "LookingGlassEditorCommands.h"

#include "ILookingGlassRuntime.h"
#include "ILookingGlassEditor.h"
#include "Blocks/LookingGlassBlocksInterface.h"

#include "SlateBasics.h"
#include "ISettingsModule.h"
#include "Engine/Engine.h"

#define LOCTEXT_NAMESPACE "FLookingGlassEditorModule"

FLookingGlassToolbarCommand::FLookingGlassToolbarCommand()
	: TCommands<FLookingGlassToolbarCommand>(
		"LookingGlass", // Context name for fast lookup
		NSLOCTEXT("LookingGlassToolbarCommands", "LookingGlassToolbar Commands", "LookingGlassToolbar Commands"), // Localized context name for displaying
		NAME_None,
		FLookingGlassEditorStyle::GetStyleSetName() // Icon Style Set
		)
{
}

void FLookingGlassToolbarCommand::SetLastExecutedPlayMode(ELookingGlassModeType PlayMode)
{
	ULookingGlassSettings* LookingGlassSettings = GetMutableDefault<ULookingGlassSettings>();
	LookingGlassSettings->LookingGlassWindowSettings.LastExecutedPlayModeType = PlayMode;

	LookingGlassSettings->LookingGlassSave();
}

const TSharedRef<FUICommandInfo> FLookingGlassToolbarCommand::GetLastPlaySessionCommand()
{
	const ULookingGlassSettings* LookingGlassSettings = GetDefault<ULookingGlassSettings>();

	const FLookingGlassToolbarCommand& Commands = FLookingGlassToolbarCommand::Get();
	TSharedRef < FUICommandInfo > Command = Commands.PlayInLookingGlassWindow.ToSharedRef();

	switch (LookingGlassSettings->LookingGlassWindowSettings.LastExecutedPlayModeType)
	{
	case ELookingGlassModeType::PlayMode_InMainViewport:
		Command = Commands.PlayInMainViewport.ToSharedRef();
		break;

	case ELookingGlassModeType::PlayMode_InSeparateWindow:
		Command = Commands.PlayInLookingGlassWindow.ToSharedRef();
		break;
	}

	return Command;
}

void FLookingGlassToolbarCommand::RegisterCommands()
{
	UI_COMMAND(RepeatLastPlay, "LookingGlass", "Repeat Last LookingGlass", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(PlayInLookingGlassWindow, "PlayInLookingGlassWindow", "Open LookingGlass Window (Lock must be unchecked)", EUserInterfaceActionType::Check, FInputChord());
	UI_COMMAND(CloseLookingGlassWindow, "CloseLookingGlassWindow", "Close LookingGlass Window", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(OpenLookingGlassSettings, "OpenLookingGlassSettings", "Open LookingGlass Settings", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(PlayInMainViewport, "PlayInMainViewport", "Play In Main Viewport (Game must be running)", EUserInterfaceActionType::Check, FInputChord());
	UI_COMMAND(PlayInQuiltMode, "Play in Quilt Mode", "If checked, quilt is rendered to viewport without lenticular shader", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(PlayIn2DMode, "Play in 2D Mode", "If checked, the regular \"2D\" mode is used for rendering", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(LockInMainViewport, "Lock in main viewport", "If checked, all play options will be locked. Rendering will be in main viewport", EUserInterfaceActionType::ToggleButton, FInputChord());
	// Placement modes
	UI_COMMAND(PlacementInLookingGlassAuto, "Auto Placement In LookingGlass", "LookingGlass screen will automatically be placed in Looking Glass display", EUserInterfaceActionType::RadioButton, FInputChord());
	UI_COMMAND(PlacementInLookingGlassDebug, "Use debug window for LookingGlass", "LookingGlass rendering will be performed in custom popup window", EUserInterfaceActionType::RadioButton, FInputChord());
	// Performance modes
	UI_COMMAND(RealtimeMode, "Realtime Mode", "Render hologram every frame", EUserInterfaceActionType::RadioButton, FInputChord());
	UI_COMMAND(AdaptiveMode, "Adaptive Mode", "Render hologram every frame, render 2D while scene is editing", EUserInterfaceActionType::RadioButton, FInputChord());
	UI_COMMAND(NonRealtimeMode, "Non-realtime Mode", "Render hologram only after scene changed", EUserInterfaceActionType::RadioButton, FInputChord());
	// Blocks
	UI_COMMAND(OpenBlocksUI, "Share content with Blocks", "Share pre-rendered scene in Blocks portal", EUserInterfaceActionType::Button, FInputChord());

	CommandActionList = MakeShareable(new FUICommandList);

	CommandActionList->MapAction(
		RepeatLastPlay,
		FExecuteAction::CreateStatic(&FLookingGlassToolbarCommand::RepeatLastPlay_Clicked),
		FCanExecuteAction(), // FCanExecuteAction::CreateStatic(&FLookingGlassToolbarCommand::RepeatLastPlay_CanExecute),
		FIsActionChecked() /*,
		FIsActionButtonVisible::CreateStatic(&FLookingGlassToolbarCommand::IsNotPlaying) */
	);

	CommandActionList->MapAction(
		PlayInLookingGlassWindow,
		FExecuteAction::CreateStatic(&FLookingGlassToolbarCommand::PlayInLookingGlassWindow_Clicked),
		FCanExecuteAction::CreateStatic(&FLookingGlassToolbarCommand::CanExecutePlayInLookingGlassWindow),
		FIsActionChecked::CreateStatic(&FLookingGlassToolbarCommand::PlayInModeIsChecked, ELookingGlassModeType::PlayMode_InSeparateWindow),
		FIsActionButtonVisible::CreateStatic(&FLookingGlassToolbarCommand::IsNotPlaying)
	);

	CommandActionList->MapAction(
		CloseLookingGlassWindow,
		FExecuteAction::CreateStatic(&FLookingGlassToolbarCommand::CloseLookingGlassWindow_Clicked),
		FCanExecuteAction::CreateLambda(&FLookingGlassToolbarCommand::CanExecuteCloseInLookingGlassWindow),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateStatic(&FLookingGlassToolbarCommand::IsPlaying)
	);

	CommandActionList->MapAction(
		PlayInMainViewport,
		FExecuteAction::CreateStatic(&FLookingGlassToolbarCommand::PlayInMainViewport_Clicked),
		FCanExecuteAction::CreateStatic(&FLookingGlassToolbarCommand::CanExecutePlayInMainViewport),
		FIsActionChecked::CreateStatic(&FLookingGlassToolbarCommand::PlayInModeIsChecked, ELookingGlassModeType::PlayMode_InMainViewport),
		FIsActionButtonVisible::CreateStatic(&FLookingGlassToolbarCommand::IsNotPlaying)
	);

	CommandActionList->MapAction(
		OpenLookingGlassSettings,
		FExecuteAction::CreateStatic(&FLookingGlassToolbarCommand::OpenLookingGlassSettings_Clicked),
		FCanExecuteAction()
	);

	CommandActionList->MapAction(
		PlayInQuiltMode,
		FExecuteAction::CreateStatic(&FLookingGlassToolbarCommand::OnTogglePlayInQuiltMode),
		FCanExecuteAction(),
		FIsActionChecked::CreateStatic(&FLookingGlassToolbarCommand::OnIsPlayInQuiltModeEnabled)
	);

	CommandActionList->MapAction(
		PlayIn2DMode,
		FExecuteAction::CreateStatic(&FLookingGlassToolbarCommand::OnTogglePlayIn2DMode),
		FCanExecuteAction(),
		FIsActionChecked::CreateStatic(&FLookingGlassToolbarCommand::OnIsPlayIn2DModeEnabled)
	);

	CommandActionList->MapAction(
		LockInMainViewport,
		FExecuteAction::CreateStatic(&FLookingGlassToolbarCommand::OnToggleLockInMainViewport),
		FCanExecuteAction::CreateStatic(&FLookingGlassToolbarCommand::IsNotPlaying),
		FIsActionChecked::CreateStatic(&FLookingGlassToolbarCommand::OnIsLockInMainViewport)
	);

	CommandActionList->MapAction(
		PlacementInLookingGlassAuto,
		FExecuteAction::CreateStatic(&FLookingGlassToolbarCommand::SetPlacementMode, ELookingGlassPlacement::Automatic),
		FCanExecuteAction::CreateStatic(&FLookingGlassToolbarCommand::IsNotPlaying),
		FIsActionChecked::CreateStatic(&FLookingGlassToolbarCommand::IsPlacementMode, ELookingGlassPlacement::Automatic)
	);

	CommandActionList->MapAction(
		PlacementInLookingGlassDebug,
		FExecuteAction::CreateStatic(&FLookingGlassToolbarCommand::SetPlacementMode, ELookingGlassPlacement::AlwaysDebugWindow),
		FCanExecuteAction::CreateStatic(&FLookingGlassToolbarCommand::IsNotPlaying),
		FIsActionChecked::CreateStatic(&FLookingGlassToolbarCommand::IsPlacementMode, ELookingGlassPlacement::AlwaysDebugWindow)
	);

	CommandActionList->MapAction(
		RealtimeMode,
		FExecuteAction::CreateStatic(&FLookingGlassToolbarCommand::SetPerformanceMode, ELookingGlassPerformanceMode::Realtime),
		FCanExecuteAction(),
		FIsActionChecked::CreateStatic(&FLookingGlassToolbarCommand::IsPerformanceMode, ELookingGlassPerformanceMode::Realtime)
	);

	CommandActionList->MapAction(
		AdaptiveMode,
		FExecuteAction::CreateStatic(&FLookingGlassToolbarCommand::SetPerformanceMode, ELookingGlassPerformanceMode::RealtimeAdaptive),
		FCanExecuteAction(),
		FIsActionChecked::CreateStatic(&FLookingGlassToolbarCommand::IsPerformanceMode, ELookingGlassPerformanceMode::RealtimeAdaptive)
	);

	CommandActionList->MapAction(
		NonRealtimeMode,
		FExecuteAction::CreateStatic(&FLookingGlassToolbarCommand::SetPerformanceMode, ELookingGlassPerformanceMode::NonRealtime),
		FCanExecuteAction(),
		FIsActionChecked::CreateStatic(&FLookingGlassToolbarCommand::IsPerformanceMode, ELookingGlassPerformanceMode::NonRealtime)
	);

	CommandActionList->MapAction(
		OpenBlocksUI,
		FExecuteAction::CreateStatic(&FLookingGlassToolbarCommand::OpenBlocksUI_Clicked),
		FCanExecuteAction()
	);
}

void FLookingGlassToolbarCommand::RepeatLastPlay_Clicked()
{
	if (!ILookingGlassRuntime::Get().IsPlaying())
	{
		// Play action
		ULookingGlassSettings* LookingGlassSettings = GetMutableDefault<ULookingGlassSettings>();

		// Grab the play command and execute it
		TSharedRef<FUICommandInfo> LastCommand = GetLastPlaySessionCommand();

		FLookingGlassToolbarCommand::Get().CommandActionList->ExecuteAction(LastCommand);
	}
	else
	{
		// Stop action
		ILookingGlassRuntime::Get().StopPlayer();
	}
}

bool FLookingGlassToolbarCommand::RepeatLastPlay_CanExecute()
{
	auto LookingGlassSettings = GetDefault<ULookingGlassSettings>();

	return FLookingGlassToolbarCommand::Get().CommandActionList->CanExecuteAction(GetLastPlaySessionCommand()) && 
		!LookingGlassSettings->LookingGlassWindowSettings.bLockInMainViewport;
}

void FLookingGlassToolbarCommand::PlayInLookingGlassWindow_Clicked()
{
	auto LookingGlassModeType = ELookingGlassModeType::PlayMode_InSeparateWindow;
	SetLastExecutedPlayMode(LookingGlassModeType);
	ILookingGlassRuntime::Get().StartPlayer(LookingGlassModeType);
}

void FLookingGlassToolbarCommand::CloseLookingGlassWindow_Clicked()
{
	ILookingGlassRuntime::Get().StopPlayer();
}

void FLookingGlassToolbarCommand::OpenLookingGlassSettings_Clicked()
{
	// Put your "OnButtonClicked" stuff here
	FName ProjectSettingsTabName("ProjectSettings");
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->ShowViewer("Project", "Plugins", "LookingGlass");
	}
}

void FLookingGlassToolbarCommand::OpenBlocksUI_Clicked()
{
	ILookingGlassEditor::Get().GetBlocksInterface().OpenBlocksWindow();
}

void FLookingGlassToolbarCommand::PlayInMainViewport_Clicked()
{
	auto LookingGlassModeType = ELookingGlassModeType::PlayMode_InMainViewport;
	SetLastExecutedPlayMode(LookingGlassModeType);
	ILookingGlassRuntime::Get().StartPlayer(LookingGlassModeType);
}

bool FLookingGlassToolbarCommand::PlayInModeIsChecked(ELookingGlassModeType PlayMode)
{
	bool result = (PlayMode == GetDefault<ULookingGlassSettings>()->LookingGlassWindowSettings.LastExecutedPlayModeType);

	return result;
}

void FLookingGlassToolbarCommand::OnTogglePlayInQuiltMode()
{
	ULookingGlassSettings* LookingGlassSettings = GetMutableDefault<ULookingGlassSettings>();
	LookingGlassSettings->LookingGlassRenderingSettings.QuiltMode ^= 1;

	LookingGlassSettings->LookingGlassSave();
}

bool FLookingGlassToolbarCommand::OnIsPlayInQuiltModeEnabled()
{
	return GetDefault<ULookingGlassSettings>()->LookingGlassRenderingSettings.QuiltMode;
}

void FLookingGlassToolbarCommand::OnTogglePlayIn2DMode()
{
	ULookingGlassSettings* LookingGlassSettings = GetMutableDefault<ULookingGlassSettings>();
	LookingGlassSettings->LookingGlassRenderingSettings.bRender2D ^= 1;

	LookingGlassSettings->LookingGlassSave();
}

bool FLookingGlassToolbarCommand::OnIsPlayIn2DModeEnabled()
{
	return GetDefault<ULookingGlassSettings>()->LookingGlassRenderingSettings.bRender2D;
}

void FLookingGlassToolbarCommand::OnToggleLockInMainViewport()
{
	ULookingGlassSettings* LookingGlassSettings = GetMutableDefault<ULookingGlassSettings>();
	LookingGlassSettings->LookingGlassWindowSettings.bLockInMainViewport ^= true;

	LookingGlassSettings->LookingGlassSave();
}

bool FLookingGlassToolbarCommand::OnIsLockInMainViewport()
{
	return GetDefault<ULookingGlassSettings>()->LookingGlassWindowSettings.bLockInMainViewport;
}

void FLookingGlassToolbarCommand::SetPlacementMode(ELookingGlassPlacement PlacementMode)
{
	ULookingGlassSettings* LookingGlassSettings = GetMutableDefault<ULookingGlassSettings>();
	LookingGlassSettings->LookingGlassWindowSettings.PlacementMode = PlacementMode;

	LookingGlassSettings->LookingGlassSave();
}

bool FLookingGlassToolbarCommand::IsPlacementMode(ELookingGlassPlacement PlacementMode)
{
	return GetDefault<ULookingGlassSettings>()->LookingGlassWindowSettings.PlacementMode == PlacementMode;
}

void FLookingGlassToolbarCommand::SetPerformanceMode(ELookingGlassPerformanceMode Mode)
{
	ULookingGlassSettings* LookingGlassSettings = GetMutableDefault<ULookingGlassSettings>();
	LookingGlassSettings->LookingGlassEditorSettings.PerformanceMode = Mode;

	LookingGlassSettings->LookingGlassSave();
}

bool FLookingGlassToolbarCommand::IsPerformanceMode(ELookingGlassPerformanceMode Mode)
{
	return GetDefault<ULookingGlassSettings>()->LookingGlassEditorSettings.PerformanceMode == Mode;
}

bool FLookingGlassToolbarCommand::IsPlaying()
{
	return ILookingGlassRuntime::Get().IsPlaying();
}

bool FLookingGlassToolbarCommand::IsNotPlaying()
{
	return !ILookingGlassRuntime::Get().IsPlaying();
}

bool FLookingGlassToolbarCommand::CanExecutePlayInMainViewport()
{
	return IsNotPlaying() && GEngine->GameViewport != nullptr && !GetDefault<ULookingGlassSettings>()->LookingGlassWindowSettings.bLockInMainViewport;
}

bool FLookingGlassToolbarCommand::CanExecutePlayInLookingGlassWindow()
{
	return IsNotPlaying() && !GetDefault<ULookingGlassSettings>()->LookingGlassWindowSettings.bLockInMainViewport;
}

bool FLookingGlassToolbarCommand::CanExecuteCloseInLookingGlassWindow()
{
	return IsPlaying() && !GetDefault<ULookingGlassSettings>()->LookingGlassWindowSettings.bLockInMainViewport;
}

int32 FLookingGlassToolbarCommand::GetCurrentLookingGlassDisplayIndex()
{
	auto LookingGlassSettings = GetDefault<ULookingGlassSettings>();

	return LookingGlassSettings->LookingGlassWindowSettings.ScreenIndex;
}

void FLookingGlassToolbarCommand::SetCurrentLookingGlassDisplayIndex(int32 Index, ETextCommit::Type CommitInfo)
{
	ULookingGlassSettings* LookingGlassSettings = GetMutableDefault<ULookingGlassSettings>();

	LookingGlassSettings->LookingGlassWindowSettings.ScreenIndex = Index;

	LookingGlassSettings->LookingGlassSave();
}

#undef LOCTEXT_NAMESPACE
