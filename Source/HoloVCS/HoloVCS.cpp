// Copyright Epic Games, Inc. All Rights Reserved.

#include "HoloVCS.h"
#include "Modules/ModuleManager.h"
#include "Misc/CoreDelegates.h"
#include "GameMapsSettings.h"
#include "HAL/IConsoleManager.h"

//The flat and Looking Glass uprojects share this Config dir, so GameDefaultMap can only point at one
//of them (the flat map).  When the LookingGlass plugin is present (only ever true in the hardware
//build) we redirect the boot map to the hardware map, so the packaged HoloVCS.exe works when simply
//double-clicked.  An explicit map on the command line still wins over GameDefaultMap.
class FHoloVCSGameModule : public FDefaultGameModuleImpl
{
	virtual void StartupModule() override
	{
		FCoreDelegates::GetOnPostEngineInit().AddLambda([]()
		{
			if (FModuleManager::Get().IsModuleLoaded("LookingGlassRuntime"))
			{
				//Content/NewMap.umap is only a redirector; the real hardware map lives in Maps/
				UGameMapsSettings::SetGameDefaultMap(TEXT("/Game/Maps/NewMap.NewMap"));

				//UE 5.8's default Lumen GI and reflections are pure waste on our sprite diorama
				//(they were also part of what dragged the old scene-capture quilt path to ~10 fps).
				//Virtual shadow maps STAY ON: the hologram renders via the sprite path (no scene
				//captures), so the only scene render is the single 2D window, and VSM is what lets
				//the point light resolve shadows across the NES diorama's 2-unit layer gaps -
				//legacy shadow maps swallow them in bias and the 2D view loses all its shadows.
				auto SetCVarInt = [](const TCHAR* name, int32 value)
				{
					if (IConsoleVariable* pVar = IConsoleManager::Get().FindConsoleVariable(name))
					{
						pVar->Set(value, ECVF_SetByGameOverride);
					}
				};
				SetCVarInt(TEXT("r.DynamicGlobalIlluminationMethod"), 0);
				SetCVarInt(TEXT("r.ReflectionMethod"), 0);
				//VSM flickers and blotches on our per-frame-updating masked layer textures; the
				//old 4.27 build used traditional per-object shadow maps, which work here too now
				//that the light gets a tight ShadowBias (the default bias swallowed the NES
				//diorama's 2-unit layer gaps, which is what VSM was briefly enabled to fix)
				SetCVarInt(TEXT("r.Shadow.Virtual.Enable"), 0);
			}
		});
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE( FHoloVCSGameModule, HoloVCS, "HoloVCS" );
