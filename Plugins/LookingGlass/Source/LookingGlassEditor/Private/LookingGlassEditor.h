#pragma once

#include "ILookingGlassEditor.h"
#include "PropertyEditorDelegates.h"

#include "Blocks/LookingGlassBlocksInterface.h"

class FLookingGlassToolbar;
class FExtensibilityManager;

/**
 * @class	FLookingGlassEditorModule
 *
 * @brief	A LookingGlass editor module.
 * 			Editor UI and Editor commands
 */

class FLookingGlassEditorModule : public ILookingGlassEditor
{
public:

	/**
	 * @fn	virtual void FLookingGlassEditorModule::StartupModule() override;
	 *
	 * @brief	Called on LoadingPhase
	 */

	virtual void StartupModule() override;

	/**
	 * @fn	virtual void FLookingGlassEditorModule::ShutdownModule() override;
	 *
	 * @brief	Shutdown module when it is unloaded on on exit from the Game/Editor
	 */

	virtual void ShutdownModule() override;

private:

	/**
	 * @fn	void FLookingGlassEditorModule::AddEditorSettings();
	 *
	 * @brief	Adds editor settings to Unreal Engine project settings
	 */

	void AddEditorSettings();

	/**
	 * @fn	void FLookingGlassEditorModule::RemoveEditorSettings();
	 *
	 * @brief	Removes the editor settings from Unreal Engine project settings
	 */

	void RemoveEditorSettings();

	/**
	 * @fn	bool FLookingGlassEditorModule::OnSettingsSaved();
	 *
	 * @brief	Validation code in here and resave the settings in case an invalid
	 *
	 * @returns	True if it succeeds, false if it fails.
	 */

	bool OnSettingsSaved();

	/**
	 * @fn	void FLookingGlassEditorModule::OnPIEViewportStarted();
	 *
	 * @brief	Called when the PIE Viewport is created.
	 */

	void OnPIEViewportStarted();

	/**
	 * @fn	void FLookingGlassEditorModule::OnEndPIE(bool bIsSimulating);
	 *
	 * @brief	Called when the user closes the PIE instance window.
	 *
	 * @param	bIsSimulating	True if is simulating, false if not.
	 */

	void OnEndPIE(bool bIsSimulating);

	void OnEditorSelectionChanged(const TArray<UObject*>& NewSelection, bool bForceRefresh);

private:
	TSharedPtr<FLookingGlassToolbar> LookingGlassToolbar;

	TSet< FName > RegisteredPropertyTypes;

	bool bShouldStopPlaying = false;

public:
	virtual FLookingGlassBlocksInterface& GetBlocksInterface() override;

	FLookingGlassBlocksInterface BlocksInterface;
};
