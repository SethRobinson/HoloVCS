//  ***************************************************************
//  HelpScreen - dynamically drawn hotkey help, replaces the old bitmap splash
//  -------------------------------------------------------------
//  Owns the help state (visible or not, pause interplay, Shipping startup auto-show) and the
//  help text itself (BuildHelpText is the single source of truth for the key list).  The text
//  travels to the renderers through an invisible runtime-spawned actor tagged "HelpScreen":
//  the flat build's AHoloHUD reads it via g_pLibretroManager, and the Looking Glass plugin
//  finds the actor by tag and draws the text per quilt tile - same zero-compile-coupling trick
//  as the status text.  Non-empty component text = help is up.
//  ***************************************************************

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HelpScreen.generated.h"

class UTextRenderComponent;
class ALibretroManagerActor;

UCLASS()
class HOLOVCS_API AHelpScreenActor : public AActor
{
	GENERATED_BODY()

public:
	AHelpScreenActor();

	UPROPERTY()
	UTextRenderComponent* m_pTextComponent = NULL;
};

class HelpScreen
{
public:

	void Init(ALibretroManagerActor* pActor); //spawns the carrier actor; call before InitLayers
	void Show();
	void Hide(); //restores the pause state that existed before Show
	void Toggle();
	bool IsVisible() const { return m_bVisible; }
	void NotifyExternallyUnpaused(); //someone unpaused under us (P key, rom switch, harness) - just close
	void TickAutoShow(); //counts down the Shipping startup auto-show; call once per visible frame
	FString GetHelpText() const { return m_helpText; } //"" when hidden

private:

	FString BuildHelpText() const;
	void SetComponentText(const FString& text);

	TWeakObjectPtr<AHelpScreenActor> m_pHelpActor;
	ALibretroManagerActor* m_pManagerActor = NULL;
	bool m_bVisible = false;
	bool m_bWasPausedBeforeShow = false;
	int m_autoShowFramesLeft = 0; //>0 = counting down to the startup auto-show
	//One key event can hit several bindings (the specific handler plus the AnyKey catch-all);
	//these keep a same-frame show-then-hide (or hide-then-reshow) from cancelling itself.
	uint64 m_lastShowFrame = 0;
	uint64 m_lastHideFrame = 0;
	FString m_helpText; //cached built text while visible
};
