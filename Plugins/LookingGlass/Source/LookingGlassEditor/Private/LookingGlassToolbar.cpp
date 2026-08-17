#include "LookingGlassToolbar.h"
#include "LookingGlassEditorCommands.h"

#include "ILookingGlassRuntime.h"
#include "LookingGlassSettings.h"
#include "ILookingGlassEditor.h"

#include "LevelEditor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Modules/ModuleManager.h"
#include "Internationalization/Internationalization.h"
#include "Widgets/SWidget.h"
#include "Widgets/Input/SSpinBox.h"

#include "Widgets/SBoxPanel.h"

#include "Runtime/Launch/Resources/Version.h"

#if ENGINE_MAJOR_VERSION >= 5
#include "ToolMenuEntry.h"
#include "ToolMenus.h"
#endif

#define LOCTEXT_NAMESPACE "LookingGlassToolbarEditor"

FLookingGlassToolbar::FLookingGlassToolbar()
{
	ExtendLevelEditorToolbar();
}

FLookingGlassToolbar::~FLookingGlassToolbar()
{
	if (UObjectInitialized() && LevelToolbarExtender.IsValid() && !IsEngineExitRequested())
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
		if (LevelEditorModule.GetToolBarExtensibilityManager().IsValid())
		{
			LevelEditorModule.GetToolBarExtensibilityManager()->RemoveExtender(LevelToolbarExtender);
		}
	}
}

void FLookingGlassToolbar::ExtendLevelEditorToolbar()
{
	check(!LevelToolbarExtender.IsValid());

#if ENGINE_MAJOR_VERSION < 5
	// Create Toolbar extension
	LevelToolbarExtender = MakeShareable(new FExtender);

	LevelToolbarExtender->AddToolBarExtension(
		"Game",
		EExtensionHook::After,
		FLookingGlassToolbarCommand::Get().CommandActionList,
		FToolBarExtensionDelegate::CreateRaw(this, &FLookingGlassToolbar::FillToolbar)
	);

	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(LevelToolbarExtender);
#else
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	TSharedPtr<FUICommandList> CommandList = FLookingGlassToolbarCommand::Get().CommandActionList;

	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.User");
	FToolMenuSection& Section = Menu->FindOrAddSection("LookingGlass");

	// Combined Play/Close button. It is required to be combined, instead of showing/hiding separate buttons, in order to make the following
	// ComboButton always visible. It was always visible in UE4, however in UE5 it is always bound to the previous button, and inherits
	// its visibility flags.
	FToolMenuEntry LookingGlassButtonEntry = FToolMenuEntry::InitToolBarButton(
		FLookingGlassToolbarCommand::Get().RepeatLastPlay,
		TAttribute< FText >::Create(TAttribute< FText >::FGetter::CreateStatic(&FLookingGlassToolbar::GetRepeatLastPlayName)),
		TAttribute< FText >::Create(TAttribute< FText >::FGetter::CreateStatic(&FLookingGlassToolbar::GetRepeatLastPlayToolTip)),
		TAttribute< FSlateIcon >::Create(TAttribute< FSlateIcon >::FGetter::CreateStatic(&FLookingGlassToolbar::GetRepeatLastPlayIcon))
	);
	LookingGlassButtonEntry.SetCommandList(CommandList);

	// Combo button
	const FToolMenuEntry LookingGlassComboEntry = FToolMenuEntry::InitComboButton(
		"LookingGlassMenu",
		FUIAction(),
		FOnGetContent::CreateStatic(&FLookingGlassToolbar::GenerateMenuContent, FLookingGlassToolbarCommand::Get().CommandActionList.ToSharedRef(), LevelEditorModule.GetMenuExtensibilityManager()->GetAllExtenders()),
		LOCTEXT("PlayCombo_Label", "Active Play Mode"),
		LOCTEXT("PIEComboToolTip", "Change Play Mode and Play Settings"),
		FSlateIcon(),
		true //bInSimpleComboBox
	);

	Section.AddEntry(LookingGlassButtonEntry);
	Section.AddEntry(LookingGlassComboEntry);
#endif
}

// UE4 version of toolbar extender
void FLookingGlassToolbar::FillToolbar(FToolBarBuilder & ToolbarBuilder)
{
#if ENGINE_MAJOR_VERSION < 5
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");

	ToolbarBuilder.BeginSection("LookingGlassToolbar");
	{
		// Add a button to open a LookingGlass Window
		ToolbarBuilder.AddToolBarButton(
			FLookingGlassToolbarCommand::Get().RepeatLastPlay,
			NAME_None,
			TAttribute< FText >::Create(TAttribute< FText >::FGetter::CreateStatic(&FLookingGlassToolbar::GetRepeatLastPlayName)),
			TAttribute< FText >::Create(TAttribute< FText >::FGetter::CreateStatic(&FLookingGlassToolbar::GetRepeatLastPlayToolTip)),
			TAttribute< FSlateIcon >::Create(TAttribute< FSlateIcon >::FGetter::CreateStatic(&FLookingGlassToolbar::GetRepeatLastPlayIcon))
		);

		ToolbarBuilder.AddComboButton(
			FUIAction(),
			FOnGetContent::CreateStatic(&FLookingGlassToolbar::GenerateMenuContent, FLookingGlassToolbarCommand::Get().CommandActionList.ToSharedRef(), LevelEditorModule.GetMenuExtensibilityManager()->GetAllExtenders()),
			LOCTEXT("PlayCombo_Label", "Active Play Mode"),
			LOCTEXT("PIEComboToolTip", "Change Play Mode and Play Settings"),
			FSlateIcon(),
			true
		);
	}
	ToolbarBuilder.EndSection();
#endif
}

TSharedRef<SWidget> FLookingGlassToolbar::GenerateMenuContent(TSharedRef<FUICommandList> InCommandList, TSharedPtr<FExtender> Extender)
{
	struct FLocal
	{
		static void AddPlayModeMenuEntry(FMenuBuilder& MenuBuilder, ELookingGlassModeType PlayMode)
		{
			TSharedPtr<FUICommandInfo> PlayModeCommand;

			switch (PlayMode)
			{
			case ELookingGlassModeType::PlayMode_InSeparateWindow:
				PlayModeCommand = FLookingGlassToolbarCommand::Get().PlayInLookingGlassWindow;
				break;

			case ELookingGlassModeType::PlayMode_InMainViewport:
				PlayModeCommand = FLookingGlassToolbarCommand::Get().PlayInMainViewport;
				break;
			}

			if (PlayModeCommand.IsValid())
			{
				MenuBuilder.AddMenuEntry(PlayModeCommand);
			}
		}
	};

	bool bIsPlaying = ILookingGlassRuntime::Get().IsPlaying();
	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, InCommandList, Extender);

	if (!bIsPlaying)
	{
		MenuBuilder.BeginSection("LookingGlassModes", LOCTEXT("LookingGlassButtonModesSection", "Modes"));
		{
			FLocal::AddPlayModeMenuEntry(MenuBuilder, ELookingGlassModeType::PlayMode_InSeparateWindow);
			FLocal::AddPlayModeMenuEntry(MenuBuilder, ELookingGlassModeType::PlayMode_InMainViewport);
		}
		MenuBuilder.EndSection();
	}

	if (!bIsPlaying)
	{
		MenuBuilder.AddMenuEntry(FLookingGlassToolbarCommand::Get().LockInMainViewport);

		MenuBuilder.BeginSection("Placement Mode", LOCTEXT("LookingGlassPlacementSection", "Placement Mode"));
		MenuBuilder.AddMenuEntry(FLookingGlassToolbarCommand::Get().PlacementInLookingGlassAuto);
		MenuBuilder.AddMenuEntry(FLookingGlassToolbarCommand::Get().PlacementInLookingGlassDebug);
		MenuBuilder.EndSection();
	}

	MenuBuilder.BeginSection("Performance Mode", LOCTEXT("LookingGlassPerfSection", "Performance Mode"));
	MenuBuilder.AddMenuEntry(FLookingGlassToolbarCommand::Get().RealtimeMode);
	MenuBuilder.AddMenuEntry(FLookingGlassToolbarCommand::Get().AdaptiveMode);
	MenuBuilder.AddMenuEntry(FLookingGlassToolbarCommand::Get().NonRealtimeMode);
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("LookingGlass Play Settings", LOCTEXT("LookingGlassPlaySettings", "LookingGlass Play Settings"));
	{
		MenuBuilder.AddMenuEntry(FLookingGlassToolbarCommand::Get().PlayInQuiltMode);

		MenuBuilder.AddMenuEntry(FLookingGlassToolbarCommand::Get().PlayIn2DMode);

		MenuBuilder.AddMenuEntry(
			FLookingGlassToolbarCommand::Get().OpenLookingGlassSettings,
			NAME_None,
			LOCTEXT("OpenLookingGlassSettings_Label", "Settings"),
			LOCTEXT("OpenLookingGlassSettings_Tip", "Open LookingGlass Settings."),
			FSlateIcon(FLookingGlassEditorStyle::GetStyleSetName(), TEXT("LookingGlass.OpenSettings"))
			);

		MenuBuilder.EndSection();
	}

	MenuBuilder.BeginSection("Looking Glass Blocks", LOCTEXT("LookingGlassBlocks", "Looking Glass Blocks"));
	{
		MenuBuilder.AddMenuEntry(FLookingGlassToolbarCommand::Get().OpenBlocksUI);

		MenuBuilder.EndSection();
	}

	if (!bIsPlaying)
	{
		MenuBuilder.BeginSection("LookingGlass Play Display", LOCTEXT("LookingGlassPlayDisplaySection", "Display Options"));
		{
			TSharedRef<SWidget> DisplayIndex = SNew(SSpinBox<int32>)
				.MinValue(0)
				.MaxValue(3)
				.MinSliderValue(0)
				.MaxSliderValue(3)
				.ToolTipText(LOCTEXT("LookingGlassPlayDisplayToolTip", "LookingGlass display index"))
				.Value(FLookingGlassToolbarCommand::GetCurrentLookingGlassDisplayIndex())
				.OnValueCommitted_Static(&FLookingGlassToolbarCommand::SetCurrentLookingGlassDisplayIndex);

			MenuBuilder.AddWidget(DisplayIndex, LOCTEXT("LookingGlassDisplayIndexWidget", "Display Index"));
		}
		MenuBuilder.EndSection();
	}

	return MenuBuilder.MakeWidget();
}

FSlateIcon FLookingGlassToolbar::GetRepeatLastPlayIcon()
{
	if (!ILookingGlassRuntime::Get().IsPlaying())
	{
		// Play button
		return FLookingGlassToolbarCommand::GetLastPlaySessionCommand()->GetIcon();
	}
	else
	{
		// Stop button
		return FSlateIcon(FLookingGlassEditorStyle::GetStyleSetName(), TEXT("LookingGlass.CloseWindow"));
	}
}

FText FLookingGlassToolbar::GetRepeatLastPlayName()
{
	if (!ILookingGlassRuntime::Get().IsPlaying())
	{
		return LOCTEXT("RepeatLastPlay_Label", "Play");
	}
	else
	{
		return LOCTEXT("CloseLookingGlassWindow_Label", "Stop");
	}
}

FText FLookingGlassToolbar::GetRepeatLastPlayToolTip()
{
	if (!ILookingGlassRuntime::Get().IsPlaying())
	{
		return FLookingGlassToolbarCommand::GetLastPlaySessionCommand()->GetDescription();
	}
	else
	{
		return LOCTEXT("CloseLookingGlassWindow_Tip", "Close LookingGlass Window.");
	}
}

#undef LOCTEXT_NAMESPACE
