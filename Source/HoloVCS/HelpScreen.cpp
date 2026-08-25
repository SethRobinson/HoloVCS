#include "HelpScreen.h"
#include "LibretroManagerActor.h"
#include "Components/TextRenderComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Shared/UnrealMisc.h"

extern string G_VERSION_STRING;

AHelpScreenActor::AHelpScreenActor()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* pRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = pRoot;

	//the component only carries the string to the renderers; it must never draw as world geometry
	m_pTextComponent = CreateDefaultSubobject<UTextRenderComponent>(TEXT("HelpText"));
	m_pTextComponent->SetupAttachment(pRoot);
	m_pTextComponent->SetHiddenInGame(true);
	//UTextRenderComponent defaults its Text to "Text" - the sprite-quilt renderer treats ANY
	//non-empty text on this actor as "help is up" (layers skipped, text drawn as the help
	//title), which blacked out every layer in runs where the help was never shown/hidden
	m_pTextComponent->SetText(FText::GetEmpty());

	Tags.Add(FName("HelpScreen"));
}

void HelpScreen::Init(ALibretroManagerActor* pActor)
{
	m_pManagerActor = pActor;
	if (!pActor || !pActor->GetWorld()) return;

	//parked far below the world as belt-and-braces on top of SetHiddenInGame
	m_pHelpActor = pActor->GetWorld()->SpawnActor<AHelpScreenActor>(FVector(0, 0, -100000), FRotator::ZeroRotator);

#if UE_BUILD_SHIPPING
	//startup auto-show on the first live frame (the help fully covers the game now, so there
	//is nothing to wait for).  Dev builds never auto-show (automation workflow).
	m_autoShowFramesLeft = 1;
#endif
}

void HelpScreen::Show()
{
	if (m_bVisible) return;
	if (GFrameCounter == m_lastHideFrame && m_lastHideFrame != 0) return; //the key that closed us must not reopen us
	m_autoShowFramesLeft = 0;
	if (!m_pManagerActor) return;
	LibretroManager* pManager = &m_pManagerActor->m_libretroManager;

	m_bWasPausedBeforeShow = pManager->GetGamePaused();
	m_bVisible = true;
	m_lastShowFrame = GFrameCounter;
	m_helpText = BuildHelpText();
	SetComponentText(m_helpText);
	pManager->SetGamePaused(true);
	LogMsg("Help shown");
}

void HelpScreen::Hide()
{
	if (!m_bVisible) return;
	if (GFrameCounter == m_lastShowFrame) return; //the key that opened us must not instantly close us

	//clear visibility BEFORE touching pause so the SetGamePaused unpause hook sees us closed
	m_bVisible = false;
	m_lastHideFrame = GFrameCounter;
	m_helpText.Empty();
	SetComponentText(m_helpText);

	if (m_pManagerActor)
	{
		m_pManagerActor->m_libretroManager.SetGamePaused(m_bWasPausedBeforeShow);
	}
	else
	{
		LogMsg("HelpScreen::Hide: NO manager actor, pause not restored!");
	}
	LogMsg("Help hidden (restored pause %d)", (int)m_bWasPausedBeforeShow);
}

void HelpScreen::Toggle()
{
	if (m_bVisible) Hide(); else Show();
}

void HelpScreen::NotifyExternallyUnpaused()
{
	//someone else unpaused (P key, rom switch/reset, harness unpause) - just get out of the way
	m_bVisible = false;
	m_lastHideFrame = GFrameCounter;
	m_helpText.Empty();
	SetComponentText(m_helpText);
	LogMsg("Help hidden (game unpaused)");
}

void HelpScreen::TickAutoShow()
{
	if (m_autoShowFramesLeft <= 0) return;
	m_autoShowFramesLeft--;
	if (m_autoShowFramesLeft == 0)
	{
		Show();
	}
}

FString HelpScreen::BuildHelpText() const
{
	//Format both renderers agree on: first line = title, "key\taction" = two-column row,
	//no tab = centered line, empty line = half-row spacing
	FString s;
	s += FString(G_VERSION_STRING.c_str()).ToUpper() + TEXT(" - CONTROLS\n");
	s += TEXT("\n");
	s += TEXT("WASD / Arrows\tMove (D-pad / Circle Pad)\n");
	s += TEXT("Space\tA button\n");
	s += TEXT("Ctrl\tB button\n");
	s += TEXT("C\tX button (3DS)\n");
	s += TEXT("Q / E\tL / R buttons\n");
	s += TEXT("Enter\tStart\n");
	s += TEXT("Tab\tSelect\n");
	s += TEXT("Mouse / R-stick\tTouch cursor (3DS)\n");
	s += TEXT("Click / R-trigger\tTouch tap (3DS)\n");
	s += TEXT("\n");
	s += TEXT(", / .\tPrevious / next game\n");
	s += TEXT("R\tReset game\n");
	s += TEXT("P\tPause\n");
	s += TEXT("F / G\tSave / load state\n");
	s += TEXT("Hold Start + LT / RT\tReset / next game (pad)\n");
	s += TEXT("Hold Start + LB / RB\tSave / load state (pad)\n");
	s += TEXT("\n");
	s += TEXT("[ / ]\tLess / more 3D depth\n");
	s += TEXT("= / -\tZoom in / out\n");
	s += TEXT("0\tToggle FPS cap\n");
	s += TEXT("1-5\tFrameskip\n");
	s += TEXT("6 / 7 / 8\tSmoothing / shadows / lighting\n");
	s += TEXT("\n");
	s += TEXT("Press any key to close");
	return s;
}

void HelpScreen::SetComponentText(const FString& text)
{
	if (m_pHelpActor.IsValid() && m_pHelpActor->m_pTextComponent)
	{
		m_pHelpActor->m_pTextComponent->SetText(FText::FromString(text));
	}
}
