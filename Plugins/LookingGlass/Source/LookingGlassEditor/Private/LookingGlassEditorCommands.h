#pragma once

#include "LookingGlassEditorStyle.h"

#include "LookingGlassSettings.h"

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"

class FUICommandList;
class FUICommandInfo;

/**
 * @class	FLookingGlassToolbarCommand
 *
 * @brief	Handles Unreal Engine LookingGlass editor commands
 */

class FLookingGlassToolbarCommand : public TCommands<FLookingGlassToolbarCommand>
{
public:
	FLookingGlassToolbarCommand();

	/**
	 * @fn	virtual void FLookingGlassToolbarCommand::RegisterCommands() override;
	 *
	 * @brief	Registers the commands for Unreal Engine
	 */

	virtual void RegisterCommands() override;

private:
	static void RepeatLastPlay_Clicked();
	static bool RepeatLastPlay_CanExecute();

	static void PlayInLookingGlassWindow_Clicked();
	static void CloseLookingGlassWindow_Clicked();
	static void OpenLookingGlassSettings_Clicked();
	static void PlayInMainViewport_Clicked();

	static void OpenBlocksUI_Clicked();

	static bool PlayInModeIsChecked(ELookingGlassModeType PlayMode);

	static void OnTogglePlayInQuiltMode();
	static bool OnIsPlayInQuiltModeEnabled();

	static void OnTogglePlayIn2DMode();
	static bool OnIsPlayIn2DModeEnabled();

	static void OnToggleLockInMainViewport();
	static bool OnIsLockInMainViewport();	

	// Window placement
	static void SetPlacementMode(ELookingGlassPlacement PlacementMode);
	static bool IsPlacementMode(ELookingGlassPlacement PlacementMode);

	// Rendering performance modes
	static void SetPerformanceMode(ELookingGlassPerformanceMode Mode);
	static bool IsPerformanceMode(ELookingGlassPerformanceMode Mode);

	static bool IsPlaying();
	static bool IsNotPlaying();

	static bool CanExecutePlayInMainViewport();
	static bool CanExecutePlayInLookingGlassWindow();
	static bool CanExecuteCloseInLookingGlassWindow();

public:
	static int32 GetCurrentLookingGlassDisplayIndex();
	static void SetCurrentLookingGlassDisplayIndex(int32 Index, ETextCommit::Type CommitInfo);

	static void SetLastExecutedPlayMode(ELookingGlassModeType PlayMode);
	static const TSharedRef <FUICommandInfo> GetLastPlaySessionCommand();

public:
	TSharedPtr<FUICommandList>		CommandActionList;

	TSharedPtr<FUICommandInfo>		RepeatLastPlay;
	TSharedPtr<FUICommandInfo>		PlayInLookingGlassWindow;
	TSharedPtr<FUICommandInfo>		CloseLookingGlassWindow;
	TSharedPtr<FUICommandInfo>		OpenLookingGlassSettings;
	TSharedPtr<FUICommandInfo>		PlayInMainViewport;
	TSharedPtr<FUICommandInfo>		PlayInQuiltMode;
	TSharedPtr<FUICommandInfo>		PlayIn2DMode;
	TSharedPtr<FUICommandInfo>		LockInMainViewport;
	// Window placement
	TSharedPtr<FUICommandInfo>		PlacementInLookingGlassAuto;
	TSharedPtr<FUICommandInfo>		PlacementInLookingGlassDebug;
	// Rendering performance modes
	TSharedPtr<FUICommandInfo>		RealtimeMode;
	TSharedPtr<FUICommandInfo>		AdaptiveMode;
	TSharedPtr<FUICommandInfo>		NonRealtimeMode;
	// Blocks
	TSharedPtr<FUICommandInfo>		OpenBlocksUI;
};