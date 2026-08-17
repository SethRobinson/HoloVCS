#include "LookingGlassEditor.h"

#include "LookingGlassEditorStyle.h"
#include "LookingGlassEditorCommands.h"
#include "LookingGlassToolbar.h"

#include "ILookingGlassRuntime.h"
#include "Misc/LookingGlassHelpers.h"
#include "LookingGlassSettings.h"
#include "Game/LookingGlassSceneCaptureComponent2D.h"

#include "ISettingsModule.h"
#include "ISettingsSection.h"
#include "ISettingsContainer.h"
#include "LevelEditor.h"

#include "Engine/GameViewportClient.h"
#include "Editor.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "FLookingGlassEditorModule"

void FLookingGlassEditorModule::StartupModule()
{
	// Initialize play button ui style
	FLookingGlassEditorStyle::Initialize();
	FLookingGlassEditorStyle::ReloadTextures();

	// Add LookingGlassToolbar section
	FLookingGlassToolbarCommand::Register();
	LookingGlassToolbar = MakeShareable(new FLookingGlassToolbar);

	// Add settings to project settings
	FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FLookingGlassEditorModule::AddEditorSettings);
	GetMutableDefault<ULookingGlassSettings>()->AddToRoot();

	UGameViewportClient::OnViewportCreated().AddRaw(this, &FLookingGlassEditorModule::OnPIEViewportStarted);
	FEditorDelegates::EndPIE.AddRaw(this, &FLookingGlassEditorModule::OnEndPIE);

	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	LevelEditorModule.OnActorSelectionChanged().AddRaw(this, &FLookingGlassEditorModule::OnEditorSelectionChanged);
}

void FLookingGlassEditorModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
	FLookingGlassEditorStyle::Shutdown();

	// Release Toolbar
	LookingGlassToolbar.Reset();

	// Release editor settings
	RemoveEditorSettings();

	// Stop player if running
	ILookingGlassRuntime::Get().StopPlayer();

	if (FModuleManager::Get().IsModuleLoaded("LevelEditor"))
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
		LevelEditorModule.OnActorSelectionChanged().RemoveAll(this);
	}
}

// Check MagicLeapHMD settings loading
void FLookingGlassEditorModule::AddEditorSettings()
{
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");

	if (SettingsModule)
	{
		ISettingsSectionPtr SettingsSection = SettingsModule->RegisterSettings("Project", "Plugins", "LookingGlass",
			LOCTEXT("LookingGlassSettingsName", "LookingGlass Plugin"),
			LOCTEXT("LookingGlassSettingsDescription", "Configure the LookingGlass plug-in."),
			GetMutableDefault<ULookingGlassSettings>()
		);

		if (SettingsSection.IsValid())
		{
			SettingsSection->OnModified().BindRaw(this, &FLookingGlassEditorModule::OnSettingsSaved);
		}
	}
}

void FLookingGlassEditorModule::RemoveEditorSettings()
{
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
	if (SettingsModule)
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "LookingGlass");
	}
}

bool FLookingGlassEditorModule::OnSettingsSaved()
{
	ULookingGlassSettings* LookingGlassSettings = GetMutableDefault<ULookingGlassSettings>();
	bool ResaveSettings = true;

	if (ResaveSettings)
	{
		LookingGlassSettings->LookingGlassSave();
	}

	return true;
}

void FLookingGlassEditorModule::OnPIEViewportStarted()
{
	if ((LookingGlass::GetMovieSceneCapture() != nullptr && LookingGlass::GetGameLookingGlassCaptureComponent() != nullptr)
		|| GetDefault<ULookingGlassSettings>()->LookingGlassWindowSettings.bLockInMainViewport)
	{
		// Just in case, stop existing player, as we'll probably change settings
		ILookingGlassRuntime::Get().StopPlayer();
		// Indicate that we should stop a player when this PIE session ends
		bShouldStopPlaying = true;
		// Start player for video capture, rendering capture to the main viewport
		ILookingGlassRuntime::Get().StartPlayer(ELookingGlassModeType::PlayMode_InMainViewport);
	}
}

void FLookingGlassEditorModule::OnEndPIE(bool bIsSimulating)
{
	if (bShouldStopPlaying)
	{
		// Video capture has been initiated, we should stop player, because we've started it
		bShouldStopPlaying = false;
		ILookingGlassRuntime::Get().StopPlayer();
	}
}

void FLookingGlassEditorModule::OnEditorSelectionChanged(const TArray<UObject*>& NewSelection, bool bForceRefresh)
{
	if (NewSelection.Num() == 1)
	{
		AActor* Actor = Cast<AActor>(NewSelection[0]);
		if (Actor != nullptr)
		{
			ULookingGlassSceneCaptureComponent2D* CaptureComponent = Actor->FindComponentByClass<ULookingGlassSceneCaptureComponent2D>();
			if (CaptureComponent)
			{
				// Make the selected component first, so it will be activated in LookingGlassRuntime
				ILookingGlassRuntime::Get().EditorLookingGlassCaptureComponents.Remove(CaptureComponent);
				ILookingGlassRuntime::Get().EditorLookingGlassCaptureComponents.Insert(CaptureComponent, 0);
			}
		}
	}
}

FLookingGlassBlocksInterface& FLookingGlassEditorModule::GetBlocksInterface()
{
	return BlocksInterface;
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FLookingGlassEditorModule, LookingGlassEditor)
