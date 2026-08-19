//  ***************************************************************
//  HoloHUD - 2D-window renderer for the help screen (see HelpScreen.h)
//  -------------------------------------------------------------
//  Draws the help text from HelpScreen as a dimmed two-column canvas panel.  Serves the flat
//  build's viewport and the LKG build's main 2D window (HUD PostRender still runs there even
//  with bDisableWorldRendering set).  Installed at runtime via ClientSetHUD from the pawn's
//  BeginPlay, so no GameMode/ini/Blueprint changes are needed.  The hologram itself gets the
//  help from the plugin's per-tile quilt overlay instead.
//  ***************************************************************

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "HoloHUD.generated.h"

UCLASS()
class HOLOVCS_API AHoloHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
};
