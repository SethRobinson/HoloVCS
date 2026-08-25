// Fill out your copyright notice in the Description page of Project Settings.
#include "LibretroManagerActor.h"
#include "AudioDevice.h"
#include "PlayerPawn.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/PointLight.h"
#include "Engine/DirectionalLight.h"
#include "Components/MeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Engine.h"
#include "TimerManager.h"
#include "EngineUtils.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Modules/ModuleManager.h"
#include "StatusDisplayActor.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Engine/GameViewportClient.h"
#include "GenericPlatform/GenericWindow.h"
#include "GameFramework/GameUserSettings.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

//Keyboard input all routes through the UE game window; when any OTHER window of this process has
//focus (the Looking Glass Bridge window - which is what you naturally click on), every hotkey and
//game control goes dead. Watch for that and bounce focus straight back to the game window.
//Focus belonging to a different app (browser etc) is left alone.
static void KeepGameWindowFocused()
{
#if PLATFORM_WINDOWS
	if (!GEngine || !GEngine->GameViewport) return;
	TSharedPtr<SWindow> pWindow = GEngine->GameViewport->GetWindow();
	if (!pWindow.IsValid() || !pWindow->GetNativeWindow().IsValid()) return;
	HWND hGameWnd = (HWND)pWindow->GetNativeWindow()->GetOSWindowHandle();
	if (!hGameWnd) return;

	HWND hForeground = GetForegroundWindow();
	if (!hForeground || hForeground == hGameWnd) return;

	DWORD foregroundPid = 0;
	GetWindowThreadProcessId(hForeground, &foregroundPid);
	if (foregroundPid != GetCurrentProcessId()) return; //user is in another app, leave them be

	SetForegroundWindow(hGameWnd);
	LogMsg("Bounced focus from the Bridge window back to the game window");
#endif
}

void FitLookingGlassCaptureToLayers(UWorld* pWorld);
static float GetLookingGlassDeviceAspect(UWorld* pWorld);

//Console twin of the [ and ] hotkeys so the automation harness can drive the depth spread
//headlessly (exec holo.DepthScale 2)
static FAutoConsoleCommand CCmdHoloDepthScale(
	TEXT("holo.DepthScale"),
	TEXT("Set the 3D depth spread multiplier, same as the [ and ] hotkeys. Usage: holo.DepthScale 1.5"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args)
	{
		if (args.Num() < 1 || !g_pLibretroManager || !g_pLibretroManager->m_pLibretroManagedActor) return;
		g_pLibretroManager->m_pLibretroManagedActor->SetUserDepthScale(FCString::Atof(*args[0]));
	}));

EPixelFormat TEX_PIXEL_FORMAT = EPixelFormat::PF_B8G8R8A8;
// Sets default values
ALibretroManagerActor::ALibretroManagerActor()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	//LogMsg("Tick interval was %f", GetActorTickInterval());
}

const bool C_INIT_TEXTURES_EVERY_FRAME = true; //slightly slower, but safer in theory?
 
bool ALibretroManagerActor::SetupLayer(LayerInfo* pLayer, char* pActorName, int layerWidth, int layerHeight, int layerID)
{
	
	pLayer->m_texWidth = layerWidth;
	pLayer->m_texHeight = layerHeight;

	pLayer->m_texPitchBytes = pLayer->m_texWidth * 4; //we're assuming XRGBA
	LogMsg("Creating texture %d X %d", pLayer->m_texWidth, pLayer->m_texHeight);

	pLayer->mDataSize = pLayer->m_texWidth * pLayer->m_texHeight * 4;

	SAFE_DELETE_ARRAY(pLayer->m_pTextData);
	pLayer->m_pTextData = new uint8[pLayer->mDataSize];
	memset(pLayer->m_pTextData, 0, pLayer->mDataSize); //make it transparent

	if (pLayer->mUpdateTextureRegion) delete pLayer->mUpdateTextureRegion;
	pLayer->mUpdateTextureRegion = new FUpdateTextureRegion2D(0, 0, 0, 0, pLayer->m_texWidth, pLayer->m_texHeight);

	auto pActor = GetWorld()->SpawnActor<AActor>(m_layerTemplate); // Spawn object
	
	auto pParentActor = GetActorByTag(GetWorld(), "LayerFolder");

	if (pActor)
	{
		LogMsg("Created %s", pActorName);
		pActor->AttachToActor(pParentActor, FAttachmentTransformRules::KeepRelativeTransform);
		pActor->SetActorRelativeScale3D(FVector(m_coreLayerScale.X, m_coreLayerScale.Y, 1));
		
	
		pLayer->m_pActor = pActor;
		#if WITH_EDITOR
		pActor->SetActorLabel(pActorName);
#endif
		//pActor->SetActorLabel(pActorName);
			//pLayer->m_HasDoneFirstTimeInit = true;
		pLayer->m_vStartingPos = pLayer->m_pActor->GetActorLocation();
	
	
		//Set the Receive CSM Shadows flag on this actor? How to do it here?
		UMeshComponent* pComp1 = (UMeshComponent*)pActor->GetComponentByClass(UMeshComponent::StaticClass());
		if (pComp1)
		{
			//LogMsg("Setting CSM flag");
			
			pComp1->bReceiveMobileCSMShadows = !g_pLibretroManager->m_profManager.m_layerSetupInfo[layerID].m_bIgnoreShadows;
		}
		else
		{
			LogMsg("Can't find mesh to set CSM flag");
		}
	}
	else
	{
		return false;
	}
	
	UMeshComponent* pComp1 = (UMeshComponent*)pActor->GetComponentByClass(UMeshComponent::StaticClass());
	if (pComp1)
	{
		//per-layer unlit override (3DS backdrop band): the material choice drives both the
		//flat scene AND the LKG sprite path, so this one swap disables lighting and shadow
		//stamps on the layer everywhere.  Survives the 8-key toggle because that just
		//re-runs InitLayers -> SetupLayer.
		if (g_pLibretroManager->m_profManager.m_layerSetupInfo[layerID].m_bUnlit)
		{
			pComp1->SetMaterial(0, LayerMatNoLighting);
		}
		else switch (m_curLightingMode)
		{
		case LIGHTING_MODE_NORMAL:
			pComp1->SetMaterial(0, LayerMatNormal);
			break;

		case LIGHTING_MODE_NONE:
			pComp1->SetMaterial(0, LayerMatNoLighting);
			break;

		default:
			check(!"Uh oh");
		}
	
		// create dynamic texture
		pLayer->m_pDynamicTexture = UTexture2D::CreateTransient(pLayer->m_texWidth, pLayer->m_texHeight, TEX_PIXEL_FORMAT);
		
		pLayer->m_pDynamicTexture->SRGB = 0;
		pLayer->m_pDynamicTexture->CompressionSettings = TextureCompressionSettings::TC_EditorIcon;
		pLayer->m_pDynamicTexture->AddToRoot();
		
		//More pixel accurate with these enabled, but I kind of miss the black outlines we get without them
		pLayer->m_pDynamicTexture->Filter = pLayer->m_filterToUse;
		pLayer->m_pDynamicTexture->LODGroup = pLayer->m_LODGroupToUse;

		pLayer->m_pDynamicTexture->UpdateResource();
		pLayer->m_pDynamicTexture->RefreshSamplerStates();

		//Transient textures start as uninitialized VRAM. Upload the zeroed buffer once so layers
		//the current game never blits to are transparent instead of garbage - the noise hid in
		//the dim lit scene render but was glaring red/cyan static on the hologram.
		{
			FUpdateTextureRegion2D* pRegionTemp = new FUpdateTextureRegion2D(0, 0, 0, 0, pLayer->m_texWidth, pLayer->m_texHeight);
			uint8* pTexTemp = new uint8[pLayer->mDataSize];
			memcpy(pTexTemp, pLayer->m_pTextData, pLayer->mDataSize);
			pLayer->m_pDynamicTexture->UpdateTextureRegions(0, 1, pRegionTemp, pLayer->m_texWidth * 4, 4, pTexTemp,
				[](auto pTexData, auto pRegion)
				{
					delete[] pTexData;
					delete pRegion;
				});
		}
		pLayer->pUMatDyn = pComp1->CreateDynamicMaterialInstance(0, 0, "MatDyn");
		pLayer->pUMatDyn->SetTextureParameterValue(TEXT("Texture"), pLayer->m_pDynamicTexture);

		//if we needed to set custom props per layer?
		//auto pMat = m_pMesh->GetMaterial(0);
		//m_pBGMat = UMaterialInstanceDynamic::Create(pMat, NULL);
	}
	else
	{
		return false;
	}

	return true;
}


void ALibretroManagerActor::ScaleLayersXY(float scaleMod)
{
	//Good thing we've previously marked all things we want to scale with a tag called "Scalable"
	TArray<AActor*> actors;
	AddActorsByTag(&actors, GetWorld(), "Scalable");

	for (int i = 0; i < actors.Num(); i++)
	{
		FVector vScale = actors[i]->GetActorScale();
		//LogMsg((string("Scale is ") + toString(vScale)).c_str());
		vScale.X *= scaleMod;
		vScale.Y *= scaleMod;
		actors[i]->SetActorRelativeScale3D(vScale);
	}

}

void ALibretroManagerActor::SetLayersPosXY(float posX, float posY)
{
	//Good thing we've previously marked all things we want to scale with a tag called "Scalable"
	TArray<AActor*> actors;
	AddActorsByTag(&actors, GetWorld(), "Layers");
	//check(actors.Num() == 1);

	for (int i = 0; i < actors.Num(); i++)
	{
		FVector vPos = actors[i]->GetActorLocation();

		vPos.Y = posX;
		vPos.Z = posY;
		//LogMsg((string("Scale is ") + toString(vScale)).c_str());

		actors[i]->SetActorLocation(vPos);
	}

}

void ALibretroManagerActor::SetScaleLayersXY(float scaleX, float scaleY)
{
	//Good thing we've previously marked all things we want to scale with a tag called "Scalable"
	TArray<AActor*> actors;
	AddActorsByTag(&actors, GetWorld(), "Scalable");

	for (int i = 0; i < actors.Num(); i++)
	{
		FVector vScale = actors[i]->GetActorScale();
		//LogMsg((string("Scale is ") + toString(vScale)).c_str());
		vScale.X = scaleX;
		vScale.Y = scaleY;
		actors[i]->SetActorScale3D(vScale);
	}
}

void ALibretroManagerActor::InitLayers()
{
	int deleteCount = DeleteActorsByTag(GetWorld(), "Layers");
	LogMsg("Initting %d new %d, %d layers, deleted %d layers.  ",
		 GetLayerCount(), m_layerWidth, m_layerHeight, deleteCount);
	CleanupLayerMemory();

	m_layerInfo.clear();
	m_layerInfo.resize(GetLayerCount());
	SetTextureSmoothingToUse(m_setTextureSmoothing);
	if (!g_pLibretroManager) return;

	float step = (m_total3dDepth * m_userDepthScale) / (float)GetLayerCount();
	float startingZ =  (step * (GetLayerCount()/2));

	//Prepare each layer we're going to dynamically write visuals to
	for (int i = 0; i < m_layerCount; i++)
	{
		if (!SetupLayer(&m_layerInfo[i], (char*)(string("Layer") + toString(i)).c_str(), m_layerWidth, m_layerHeight, i))
		{
			LogMsg("Error setting up layer");
		}
		
		m_layerInfo[i].SetLayerPosZ(startingZ+ (-m_depthOffsetForAllLayers));
		startingZ -= step;

	
	}

	//The old build's rig is a POINT light in front of the diorama (real shadow maps project
	//each sprite onto every layer behind it, sized by the projection) - the port had swapped it
	//for a straight-on directional whose shadows hide exactly behind their casters. Restore the
	//point light as the light of record; the port's helper directional stays off when it exists.
	ULightComponent* pLight = NULL;
	if (m_pLight)
	{
		pLight = m_pLight->FindComponentByClass<UPointLightComponent>();
	}
	if (!pLight)
	{
		for (TActorIterator<APointLight> itPL(GetWorld()); itPL; ++itPL)
		{
			pLight = itPL->GetLightComponent();
			break;
		}
	}
	if (pLight)
	{
		//The map saved the light as Static, which contributes NOTHING at runtime without baked
		//lighting (the 2D view looked unlit and shadow-free no matter what we toggled)
		pLight->SetMobility(EComponentMobility::Movable);
		//Tight bias or the shadow test skips right past the NES diorama's 2-unit layer gaps
		pLight->ShadowBias = 0.05f;
		pLight->ShadowSlopeBias = 0.15f;
		pLight->MarkRenderStateDirty();
		pLight->SetVisibility(m_curLightingMode != LIGHTING_MODE_NONE);
		for (TActorIterator<ADirectionalLight> itDL(GetWorld()); itDL; ++itDL)
		{
			itDL->GetLightComponent()->SetVisibility(false);
		}
	}
	else
	{
		//no point light in this map - drive whatever directional exists, as before
		for (TActorIterator<ADirectionalLight> itDL(GetWorld()); itDL; ++itDL)
		{
			itDL->GetLightComponent()->SetVisibility(m_curLightingMode != LIGHTING_MODE_NONE);
			break;
		}
	}

	//print m_bg_color's values to logmsg
	LogMsg("BG color is %f, %f, %f", m_bg_color.X, m_bg_color.Y, m_bg_color.Z);

	m_libretroManager.m_pPlayerPawn->SetTintBG(m_bg_color, m_bg_color_strength, m_bgAllowShadows);


	SetScaleLayersXY(m_coreLayerScale.X, m_coreLayerScale.Y);
	SetLayersPosXY(m_corePosition.X, m_corePosition.Y);

	//3DS: a dedicated quad for the BOTTOM screen, parked below the top-screen layer stack
	//(a perfect fit for the portrait Looking Glass Go; other displays just letterbox more).
	//It lives at m_layerInfo[GetLayerCount()], past the depth slices, so the layer-depth
	//spread and the holo slice blits never touch it; the holo callback blits its 320x240
	//image directly.  Created AFTER SetScaleLayersXY/SetLayersPosXY, which slam every
	//"Layers"-tagged actor to the shared scale/position.
	if (g_pLibretroManager->m_emulatorType == EMULATOR_3DS)
	{
		m_layerInfo.resize(GetLayerCount() + 1);
		LayerInfo* pBottom = &m_layerInfo[GetLayerCount()];
		if (SetupLayer(pBottom, (char*)"LayerBottomScreen", 320, 240, GetLayerCount()))
		{
			pBottom->SetLayerPosZ(-m_depthOffsetForAllLayers); //middle of the depth spread
			AActor* pActor = pBottom->m_pActor;
			//narrower than the 400px-wide top screen at the same pixel density
			FVector vScale = pActor->GetActorScale3D();
			vScale.X = m_coreLayerScale.X * 320.0f / 400.0f;
			vScale.Y = m_coreLayerScale.Y;
			pActor->SetActorScale3D(vScale);
			//world Y = horizontal, world Z = vertical; one screen-height down plus a gap.
			//On the portrait Go (device aspect ~0.56) the hologram frame is bound by WIDTH,
			//so there's free vertical room for a real 80px gap between the screens.  Squarer
			//panels (Portrait 0.75 etc) are height-bound - a big gap would shrink both
			//screens to fit, so they keep the near-touching layout.
			const float quadWorldHeight = pActor->GetComponentsBoundingBox().GetSize().Z;
			float gapFactor = 1.04f;
			const float deviceAspect = GetLookingGlassDeviceAspect(GetWorld());
			if (deviceAspect > 0.0f && deviceAspect < 0.6f)
			{
				gapFactor = 1.0f + 80.0f / 240.0f; //80 bottom-screen pixels of daylight
			}
			FVector vPos = pActor->GetActorLocation();
			vPos.Y = m_corePosition.X;
			vPos.Z = m_corePosition.Y - quadWorldHeight * gapFactor;
			pActor->SetActorLocation(vPos);
		}
	}

	//layers moved/scaled, so reframe the flat camera (does nothing on LG hardware)
	if (m_libretroManager.m_pPlayerPawn)
	{
		m_libretroManager.m_pPlayerPawn->FitFlatCameraToLayers();
	}

	//and reframe the Looking Glass capture actor if one exists (hardware map only)
	FitLookingGlassCaptureToLayers(GetWorld());
}

//Re-spread the EXISTING layer actors to the current depth scale.  Positions are set absolutely
//from m_vStartingPos (captured at spawn, before the depth pass) because SetLayerPosZ is relative
//and only correct on freshly spawned actors.  Unlike InitLayers there's no respawn/texture
//recreation hitch, so this is safe to spam from a held-down hotkey.
void ALibretroManagerActor::ApplyLayerDepth()
{
	//at user scale 0 keep a microscopic spacing: reads as perfectly flat on the panel but
	//the 2D scene view's real quads never z-fight
	const float effectiveScale = FMath::Max(m_userDepthScale, 0.0025f);
	float step = (m_total3dDepth * effectiveScale) / (float)GetLayerCount();
	float depth = step * (GetLayerCount() / 2);

	//entries past GetLayerCount() are not depth slices (the 3DS bottom-screen quad) - leave them be
	for (int i = 0; i < FMath::Min((int)m_layerInfo.size(), GetLayerCount()); i++)
	{
		if (m_layerInfo[i].m_pActor)
		{
			FVector vPos = m_layerInfo[i].m_pActor->GetActorLocation();
			vPos.X = m_layerInfo[i].m_vStartingPos.X + depth - m_depthOffsetForAllLayers;
			m_layerInfo[i].m_pActor->SetActorLocation(vPos);
		}
		depth -= step;
	}

	//the spread changed: reframe the flat camera (or it crops a deeper stack) and the Looking
	//Glass capture (which also re-parks the LayerBG wall behind the new deepest layer)
	if (m_libretroManager.m_pPlayerPawn)
	{
		m_libretroManager.m_pPlayerPawn->FitFlatCameraToLayers();
	}
	FitLookingGlassCaptureToLayers(GetWorld());
}

void ALibretroManagerActor::SetUserDepthScale(float scale)
{
	//0 = completely flat is allowed (Seth request); a hair below 0.05 snaps to true 0 so
	//the [ key can land exactly on "no 3d at all"
	m_userDepthScale = FMath::Clamp(scale, 0.0f, 5.0f);
	if (m_userDepthScale < 0.05f) m_userDepthScale = 0.0f;
	ApplyLayerDepth();

	char st[64];
	snprintf(st, sizeof(st), "3D depth: %d%%", (int)roundf(m_userDepthScale * 100));
	ShowStatusMessage(st);
}

//The LookingGlassCapture actor in the map was placed/sized for the old 5.6-era world scale, and each
//emulator uses a wildly different scale now (NES ~41 units, Atari ~445, VB ~310), so a static capture
//can never frame them all.  Refit it to the current layer AABB whenever the layers rebuild.  The
//plugin classes are resolved by NAME so the game module keeps zero compile-time plugin dependency
//(this is a no-op in the flat build, where the actor doesn't exist).
//Device aspect (width/height) of the connected Looking Glass panel, or 0 when no capture
//actor exists (flat build / no plugin).  Lets layout adapt per panel: the portrait Go
//(~0.56) has spare vertical room that squarer panels don't.
static float GetLookingGlassDeviceAspect(UWorld* pWorld)
{
	if (!pWorld) return 0.0f;
	for (TActorIterator<AActor> it(pWorld); it; ++it)
	{
		if (it->GetClass()->GetName() != TEXT("LookingGlassCapture")) continue;
		for (UActorComponent* pComp : it->GetComponents())
		{
			if (pComp->GetClass()->GetName() != TEXT("LookingGlassSceneCaptureComponent2D")) continue;
			if (UFunction* pAspectFunc = pComp->FindFunction(TEXT("GetAspectRatio")))
			{
				struct { float ReturnValue; } aspectParams = { 0.0f };
				pComp->ProcessEvent(pAspectFunc, &aspectParams);
				if (aspectParams.ReturnValue > 0.05f) return aspectParams.ReturnValue;
			}
		}
	}
	return 0.0f;
}

void FitLookingGlassCaptureToLayers(UWorld* pWorld)
{
	if (!pWorld) return;

	TArray<AActor*> layerActors;
	AddActorsByTag(&layerActors, pWorld, "Layers");
	if (layerActors.Num() == 0) return;

	//Bounds from VISIBLE primitives only - hidden actors (unused layers, another game's backdrop)
	//still count in GetActorBounds and were inflating the hologram frame (Pitfall filled only a
	//third of the screen because the box included invisible Castlevania-era geometry).
	FBox box(ForceInit);
	for (AActor* pActor : layerActors)
	{
		if (pActor->IsHidden()) { LogMsg("LKG fit: skipping hidden layer actor %s", TCHAR_TO_ANSI(*pActor->GetName())); continue; }
		FBox actorBox(ForceInit);
		for (UActorComponent* pC : pActor->GetComponents())
		{
			UPrimitiveComponent* pPrim = Cast<UPrimitiveComponent>(pC);
			if (!pPrim || !pPrim->IsRegistered() || !pPrim->IsVisible() || pPrim->bHiddenInGame) continue;
			actorBox += pPrim->Bounds.GetBox();
		}
		if (actorBox.IsValid)
		{
			box += actorBox;
			FVector c = actorBox.GetCenter(), s = actorBox.GetSize();
			LogMsg("LKG fit: %s center %.0f,%.0f,%.0f size %.0f x %.0f x %.0f",
				TCHAR_TO_ANSI(*pActor->GetName()), c.X, c.Y, c.Z, s.X, s.Y, s.Z);
		}
	}
	if (!box.IsValid) return;

	//The quads display the core's full max_width/max_height texture, but the game image only fills
	//base_width/base_height of it (anchored at the texture's top-left = world -Y/+Z as the capture
	//sees it).  Crop the framing box to the used portion or games like Pitfall (320x228 used of a
	//568x312 texture) render small and off-center on the hologram.
	//EXCEPTION: Virtual Boy delivers pre-split layers through the custom refresh callback and they
	//fill the whole texture - cropping there showed a quarter of the game zoomed in.  The 3DS
	//holo core's layers fill their 400x240 textures the same way (and its av_info geometry
	//describes the 400x480 two-screen composite, not the layers), so it skips the crop too.
	if (g_pLibretroManager && g_pLibretroManager->m_emulatorType != EMULATOR_VB &&
		g_pLibretroManager->m_emulatorType != EMULATOR_3DS)
	{
		auto& geo = g_pLibretroManager->m_game_av_info.geometry;
		if (geo.max_width > 0 && geo.max_height > 0 &&
			(geo.base_width < geo.max_width || geo.base_height < geo.max_height))
		{
			FVector vFullSize = box.GetSize();
			float fracW = (float)geo.base_width / (float)geo.max_width;
			float fracH = (float)geo.base_height / (float)geo.max_height;
			box.Max.Y = box.Min.Y + vFullSize.Y * fracW;
			box.Min.Z = box.Max.Z - vFullSize.Z * fracH;
			LogMsg("LKG fit: cropped to used texture area (%.0f%% x %.0f%%)", fracW * 100.0f, fracH * 100.0f);
		}
	}

	//One-shot lighting inventory - what light actors does this map actually have?
	static bool s_bLoggedLights = false;
	if (!s_bLoggedLights)
	{
		s_bLoggedLights = true;
		for (TActorIterator<AActor> itL(pWorld); itL; ++itL)
		{
			ULightComponent* pLC = itL->FindComponentByClass<ULightComponent>();
			if (!pLC) continue;
			FVector v = itL->GetActorLocation();
			LogMsg("LIGHT: %s (%s) at %.0f,%.0f,%.0f vis=%d castshadows=%d intensity=%.2f color=%.2f,%.2f,%.2f",
				TCHAR_TO_ANSI(*itL->GetName()), TCHAR_TO_ANSI(*pLC->GetClass()->GetName()),
				v.X, v.Y, v.Z, pLC->IsVisible() ? 1 : 0, pLC->CastShadows ? 1 : 0,
				pLC->Intensity, pLC->GetLightColor().R, pLC->GetLightColor().G, pLC->GetLightColor().B);
		}
	}

	//The LayerBG backdrop wall's map position was tuned for the NES layer span (about +-4 units)
	//and sat INSIDE the much wider Atari (+-40) and VB (+-200) stacks, where it covered every
	//layer behind it - Pitfall lost its background layer, VB games lost most of their layers.
	//Park it just behind whatever stack is active (this also fixes the flat 2D view).
	if (AActor* pBGWall = GetActorByTag(pWorld, "LayerBG"))
	{
		FVector vWallPos = box.GetCenter();
		vWallPos.X = box.Max.X + 5.0f;
		pBGWall->SetActorLocation(vWallPos);
		LogMsg("LKG fit: parked LayerBG wall at %.0f,%.0f,%.0f (stack back is %.0f)",
			vWallPos.X, vWallPos.Y, vWallPos.Z, box.Max.X);
	}

	for (TActorIterator<AActor> it(pWorld); it; ++it)
	{
		if (it->GetClass()->GetName() != TEXT("LookingGlassCapture")) continue;

		it->SetActorLocation(box.GetCenter());

		FVector vSize = box.GetSize();

		for (UActorComponent* pComp : it->GetComponents())
		{
			if (pComp->GetClass()->GetName() != TEXT("LookingGlassSceneCaptureComponent2D")) continue;

			//The map was saved with the 5.6-era "Custom" tiling (11x6, 4092x4092, 16:9-ish aspect).
			//Switch to the plugin's Automatic preset so it matches whatever device is connected
			//(Portrait: 48 views at 3360x3360, aspect 0.75 - a third fewer pixels per frame too).
			//Re-registering the component is the only reflection-safe way to run the plugin's
			//UpdateTilingProperties.  Pass -lkgmaptiling to keep the tiling saved in the map.
			static bool bTilingApplied = false;
			if (!bTilingApplied && !FParse::Param(FCommandLine::Get(), TEXT("lkgmaptiling")))
			{
				bTilingApplied = true;
				bool bSet = false;
				if (FProperty* pProp = FindFProperty<FProperty>(pComp->GetClass(), TEXT("TilingQuality")))
				{
					if (FEnumProperty* pEnumProp = CastField<FEnumProperty>(pProp))
					{
						pEnumProp->GetUnderlyingProperty()->SetIntPropertyValue(pEnumProp->ContainerPtrToValuePtr<void>(pComp), (int64)0); //0 = Q_Automatic
						bSet = true;
					}
					else if (FByteProperty* pByteProp = CastField<FByteProperty>(pProp))
					{
						pByteProp->SetPropertyValue_InContainer(pComp, 0);
						bSet = true;
					}
				}
				if (bSet)
				{
					//OnRegister runs the plugin's UpdateTilingProperties, which resolves Automatic via Bridge
					pComp->UnregisterComponent();
					pComp->RegisterComponent();
					LogMsg("LookingGlass tiling set to Automatic (device preset)");
				}
			}

			//The hologram should contain exactly the layer diorama plus the in-world status text -
			//the pawn's fullscreen tint plane and other scene junk live in the same world and the
			//fitted camera sits among them.  ShowOnly is the engine half of the capture component,
			//so no plugin dependency needed.
			if (USceneCaptureComponent2D* pCaptureComp = Cast<USceneCaptureComponent2D>(pComp))
			{
				//TEMP PERF TEST: capture raw scene color, skipping the per-view post pipeline
				if (FParse::Param(FCommandLine::Get(), TEXT("lkgscenecolor")))
				{
					pCaptureComp->CaptureSource = SCS_SceneColorHDR;
				}
				pCaptureComp->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
				pCaptureComp->ShowOnlyActors.Empty();
				for (AActor* pLayerActor : layerActors)
				{
					pCaptureComp->ShowOnlyActors.Add(pLayerActor);
				}
				if (AActor* pStatusActor = GetActorByTag(pWorld, "StatusDisplayActor"))
				{
					pCaptureComp->ShowOnlyActors.Add(pStatusActor);
				}
				//the help screen's invisible text carrier - the plugin harvests its string and
				//draws the help per quilt tile itself (see HelpScreen.h)
				if (AActor* pHelpActor = GetActorByTag(pWorld, "HelpScreen"))
				{
					pCaptureComp->ShowOnlyActors.Add(pHelpActor);
				}
				//The backdrop wall (moon picture etc, set per game profile) sits behind the layer
				//stack and must show through the layers' colorkey holes.  It's deliberately NOT in
				//the framing AABB above - it's much bigger than the game screen.  Hidden actors
				//stay out of the hologram too (3DS hides the wall - its capture has its own backdrop).
				if (AActor* pBGActor = GetActorByTag(pWorld, "LayerBG"))
				{
					if (!pBGActor->IsHidden())
					{
						pCaptureComp->ShowOnlyActors.Add(pBGActor);
						FVector vBGOrigin, vBGExtent;
						pBGActor->GetActorBounds(false, vBGOrigin, vBGExtent);
						LogMsg("LKG fit: added LayerBG backdrop (center %.0f,%.0f,%.0f extent %.0f x %.0f x %.0f)",
							vBGOrigin.X, vBGOrigin.Y, vBGOrigin.Z, vBGExtent.X, vBGExtent.Y, vBGExtent.Z);
					}
				}
			}

			//The capture's "Size" is the half-WIDTH of the frame at the focal plane (measured: Size
			//20.9 = NES quad edge-to-edge).  The visible height is width / device aspect (Portrait:
			//0.75, taller than wide), so fit whichever is binding: the width, or the height
			//converted into width units (halfH * aspect).  10% margin so the game doesn't run
			//edge-to-edge on the lens like it did before (old build kept a visible border).
			float deviceAspect = 0.75f;
			if (UFunction* pAspectFunc = pComp->FindFunction(TEXT("GetAspectRatio")))
			{
				struct { float ReturnValue; } aspectParams = { deviceAspect };
				pComp->ProcessEvent(pAspectFunc, &aspectParams);
				if (aspectParams.ReturnValue > 0.05f) deviceAspect = aspectParams.ReturnValue;
			}
			float captureSize = FMath::Max(vSize.Y, vSize.Z * deviceAspect) * 0.5f * 1.10f;
			FParse::Value(FCommandLine::Get(), TEXT("lkgsize="), captureSize); //tuning override, no rebuild needed

			if (UFunction* pFunc = pComp->FindFunction(TEXT("SetSize")))
			{
				struct { float InSize; } params = { captureSize };
				pComp->ProcessEvent(pFunc, &params);
				LogMsg("Fit LookingGlass capture: center %.0f,%.0f,%.0f capture size %.1f (aspect %.2f)",
					box.GetCenter().X, box.GetCenter().Y, box.GetCenter().Z, captureSize, deviceAspect);
			}
		}
	}
}

int ALibretroManagerActor::GetActiveLayerIDByDistanceMod(float mod)
{
	for (int i = 0; i < m_layerCount; i++)
	{
		if (!m_layerInfo[i].m_bUsedThisFrame) continue;
		if (m_layerInfo[i].m_distanceMod == mod)
		{
			//found a match
			return i;
		}
	}

	return -1; //can't find one
}

void ALibretroManagerActor::SetTextureSmoothingToUse(bool bfilteringOn)
{
	m_setTextureSmoothing = bfilteringOn;

	for (int i = 0; i < m_layerInfo.size(); i++)
	{
		if (bfilteringOn)
		{
			m_layerInfo[i].m_filterToUse = TextureFilter::TF_Default;
			m_layerInfo[i].m_LODGroupToUse = TextureGroup::TEXTUREGROUP_World;
		}
		else
		{
			m_layerInfo[i].m_filterToUse = TextureFilter::TF_Nearest;
			m_layerInfo[i].m_LODGroupToUse = TextureGroup::TEXTUREGROUP_Pixels2D;
		}
	}

}

int ALibretroManagerActor::GetUnusedLayerID()
{
	for (int i = 0; i < m_layerInfo.size(); i++)
	{
		if (!m_layerInfo[i].m_bUsedThisFrame) return i;
	}

	return -1; //can't find one
}

// Called when the game starts or when spawned
void ALibretroManagerActor::BeginPlay()
{
	LogMsg("Setting up");
	Super::BeginPlay();

	//fps readout for the hologram test builds (Seth wants to watch perf on the device)
	m_bShowLKGFPS = FModuleManager::Get().IsModuleLoaded("LookingGlassRuntime");

	//(the Bridge window steals focus when it opens; the periodic KeepGameWindowFocused check in
	//Tick bounces it back, at boot and any time the user clicks the hologram window)

	//Shipping builds default to fullscreen, so the focus bounce slammed a fullscreen black window
	//over the whole main monitor. The LKG build's main window is just a controller/status window -
	//keep it small and windowed.
	if (m_bShowLKGFPS && GEngine)
	{
		if (UGameUserSettings* pSettings = GEngine->GetGameUserSettings())
		{
			if (pSettings->GetFullscreenMode() != EWindowMode::Windowed)
			{
				pSettings->SetFullscreenMode(EWindowMode::Windowed);
				pSettings->SetScreenResolution(FIntPoint(1280, 720));
				pSettings->ApplySettings(false);
				LogMsg("Forced windowed mode for the main window (LKG build)");
			}
		}

		//The hologram is the product - the main window is just an input/focus target, so skip
		//scene-rendering the world into it (that "2D spectator view" was never asked for, and
		//keeping its scene shadows presentable was a whole parallel workstream).  Pass
		//-lkg2dview to re-enable it for side-by-side debugging - comparing the scene render
		//against the panel is how several hologram bugs were found.
		if (FModuleManager::Get().IsModuleLoaded("LookingGlassRuntime") && GEngine->GameViewport)
		{
			GEngine->GameViewport->bDisableWorldRendering = !FParse::Param(FCommandLine::Get(), TEXT("lkg2dview"));
			if (GEngine->GameViewport->bDisableWorldRendering)
			{
				LogMsg("Main window world rendering off (LKG build) - pass -lkg2dview to get it back");
			}
		}
	}

	FAudioDeviceHandle AudioDevice = GEngine->GetMainAudioDevice();
	FAudioDevice* MainAudioDevice = GEngine->GetMainAudioDeviceRaw();
	LogMsg("Main audio device sample rate is %f", MainAudioDevice->GetSampleRate());

	//SETH Init audio
	if (m_pRTAudioBufferComponent == NULL)
	{
		m_pRTAudioBufferComponent = NewObject<USynthComponentRTAudioBuffer>(this, USynthComponentRTAudioBuffer::StaticClass());
		m_pRTAudioBufferComponent->Initialize(MainAudioDevice->GetSampleRate());
		m_pRTAudioBufferComponent->Start(); //Note, this requires Seth's bugfixed unreal source which can't be legally shared to be able to change sample rate on the fly.
		//as a work around, you have to change the windows target overall mixing framerate.  See the USynthComponentRTAudioBuffer source for more info
	}

	LogMsg("Started audio renderer thread");

	//spawn the help screen's carrier actor before the first InitLayers so the Looking Glass
	//show-only fit can pick it up
	m_libretroManager.m_helpScreen.Init(this);

	//The old bitmap splash quad is retired (HelpScreen draws the help dynamically now), but the
	//actor still lives in both maps - editing/resaving the umaps is a one-way door, so kill it
	//at runtime in every build instead
	{
		TArray<AActor*> splashActors;
		AddActorsByTag(&splashActors, GetWorld(), "SplashScreen");
		for (AActor* pSplash : splashActors)
		{
			pSplash->Destroy();
		}
	}

	InitLayers();
	
	/*
	if (!m_pHoloPlayCapture)
	{
		LogMsg("Couldn't find HoloPlayActor");
	}
	else
	{
		LogMsg("Found HoloPlayActor");
		//m_pHoloPlayCapture->EndPlay(EEndPlayReason::RemovedFromWorld);
		//m_pHoloPlayCapture->GetWorld()->DestroyActor(m_pHoloPlayCapture);
		//m_pHoloPlayCapture = NULL;
	}
	*/

	//internal FPS counter for some reason, not really needed
	m_framesRendered = 0;
	m_timeOfNextFPSUpdate = GetWorld()->GetRealTimeSeconds() + 1.0f;

	OnWasRestartedInEditor();
	m_libretroManager.Init(this);

	if (!m_libretroManager.IsCoreLoaded())
	{
		return;
	}
}

void ALibretroManagerActor::SetSampleRate(int sampleRate)
{
	
	if (!m_pRTAudioBufferComponent) return;
	LogMsg("Stopping sound");

	m_pRTAudioBufferComponent->Stop();
	
	
	FTimerHandle    handle;
	GetWorld()->GetTimerManager().SetTimer(handle, [this, sampleRate]()
		{
			LogMsg("Setting sample rate to %d", sampleRate);
			//we need to set set the sample rate on m_pRTAudioBufferComponent
			//TODO, reinit audio buffer?
			m_pRTAudioBufferComponent->SetSampleRate(sampleRate);
			m_pRTAudioBufferComponent->Start();

		}, 0.2f, false);

		

}
 
void ALibretroManagerActor::CleanupLayerMemory()
{
	for (int i = 0; i < m_layerInfo.size(); i++)
	{
		m_layerInfo[i].Cleanup();
	}
}

void ALibretroManagerActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	LogMsg("Ending play...");
	int deleteCount = DeleteActorsByTag(GetWorld(), "Layers");

	m_libretroManager.Kill();
	
	//CleanupLayerMemory();

	//causes crash if I clean up when hitting the X to close the editor
	
	Super::EndPlay(EndPlayReason);
}

using FDataCleanupFunc = TFunction<void(uint8*, const FUpdateTextureRegion2D*)>;

// Called every frame

void ALibretroManagerActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	//long-hitch detector: timestamp multi-second freezes so log.txt shows what surrounded them
	{
		static double s_lastTickTime = 0;
		double tickNow = FPlatformTime::Seconds();
		if (s_lastTickTime != 0 && tickNow - s_lastTickTime > 0.5)
		{
			LogMsg("HITCH: frame took %.2f seconds (previous tick to this one)", tickNow - s_lastTickTime);
		}
		else if (s_lastTickTime != 0 && tickNow - s_lastTickTime > 0.035 && g_pLibretroManager)
		{
			//audio-glitch hunting: anything over ~2 frames is a hole in the sound. Breakdown of the
			//PREVIOUS tick: emu = core updates, wait = pacing sleep, rest = engine (render/present/vsync/other)
			const double gap = tickNow - s_lastTickTime;
			LogMsg("STALL at %.3f: %.1fms between ticks (emu %.1fms, pace wait %.1fms, engine/other %.1fms)", tickNow, gap * 1000.0,
				g_pLibretroManager->m_lastEmuUpdateSeconds * 1000.0, g_pLibretroManager->m_lastPaceWaitSeconds * 1000.0,
				(gap - g_pLibretroManager->m_lastEmuUpdateSeconds - g_pLibretroManager->m_lastPaceWaitSeconds) * 1000.0);
		}
		s_lastTickTime = tickNow;
	}

	if (m_bShowLKGFPS)
	{
		//clicking the hologram window kills all input (it's a non-UE window); push focus back
		static double s_nextFocusCheck = 0;
		if (FPlatformTime::Seconds() > s_nextFocusCheck)
		{
			s_nextFocusCheck = FPlatformTime::Seconds() + 0.3;
			KeepGameWindowFocused();
		}
	}

	if (m_timeOfNextFPSUpdate < GetWorld()->GetRealTimeSeconds())
	{
		if (m_bShowLKGFPS)
		{
			//the sprite-quilt renderer draws its own fps counter top-left of each tile; this is just the log record
			const int underruns = m_pRTAudioBufferComponent && m_pRTAudioBufferComponent->GetBufferGenerator() ? m_pRTAudioBufferComponent->GetBufferGenerator()->TakeUnderrunCount() : 0;
			const int queued = m_pRTAudioBufferComponent && m_pRTAudioBufferComponent->GetBufferGenerator() ? m_pRTAudioBufferComponent->GetBufferGenerator()->GetSamplesQueued() : 0;
			LogMsg("%d FPS (audio: %d queued, %d underruns, %d dropped, %d catch-up frames)", m_framesRendered, queued, underruns,
				g_pLibretroManager->m_audioFramesDropped, g_pLibretroManager->m_catchUpFrames);
			g_pLibretroManager->m_audioFramesDropped = 0;
			g_pLibretroManager->m_catchUpFrames = 0;
		}
		m_framesRendered = 0;
		m_timeOfNextFPSUpdate = GetWorld()->GetRealTimeSeconds() + 1.0f;
	}

	m_framesRendered++;

	if (!m_libretroManager.IsCoreLoaded())
	{
		//LogMsg("Core not loaded!");
		return;
	}

	m_libretroManager.Update();

	//3DS holo mode maintains per-layer dirty flags in the layer callback, so unchanged
	//layers (and the many empty ones) skip both the GPU upload and the alpha scan below.
	//Other systems don't track dirtiness and upload every frame as before.
	const bool holoDirtyTracking = (g_pLibretroManager->m_emulatorType == EMULATOR_3DS) &&
		g_pLibretroManager->m_core.retro_set_video_refresh_holo != nullptr;

	for (int i = 0; i < m_layerInfo.size(); i++)
	{
		if (holoDirtyTracking && !m_layerInfo[i].m_bDirty)
		{
			continue; //pixels unchanged: texture and shadow rect are already correct
		}
		m_layerInfo[i].m_bDirty = false;

		if (m_layerInfo[i].GetPixelBuffer())
		{

			if (C_INIT_TEXTURES_EVERY_FRAME)
			{
				LayerInfo& layer = m_layerInfo[i];
				//ping-pong staging: the render thread consumes the buffer async, so the
				//one written last frame may still be in flight - never a fresh heap alloc
				uint8*& pStaging = layer.m_pUploadBuffer[layer.m_uploadBufferIndex & 1];
				layer.m_uploadBufferIndex++;
				if (!pStaging)
				{
					pStaging = new uint8[layer.mDataSize];
				}
				memcpy(pStaging, layer.m_pTextData, layer.mDataSize);

				FUpdateTextureRegion2D* pRegionTemp = new FUpdateTextureRegion2D(0, 0, 0, 0, layer.m_texWidth, layer.m_texHeight);
				layer.m_pDynamicTexture->UpdateTextureRegions(0, 1, pRegionTemp, layer.m_texWidth * 4, 4, pStaging,
					[](auto pTexTemp, auto pRegionTemp)
					{
						delete pRegionTemp; //staging buffer is owned by the LayerInfo
					});
			}
			else
			{
				//simple way
				m_layerInfo[i].m_pDynamicTexture->UpdateTextureRegions(0, 1, m_layerInfo[i].mUpdateTextureRegion, m_layerInfo[i].m_texWidth * 4, 4, m_layerInfo[i].m_pTextData);
			}

		}

		//Report this layer's populated texel bounds (as UV min/max in custom primitive data) so
		//the hologram's shadow stamps track actual pixels - the old build's per-pixel shadow
		//maps did this for free.  Empty layers report a zero rect and cast nothing.
		if (m_layerInfo[i].m_pActor && m_layerInfo[i].m_pTextData)
		{
			UMeshComponent* pComp = (UMeshComponent*)m_layerInfo[i].m_pActor->GetComponentByClass(UMeshComponent::StaticClass());
			if (pComp)
			{
				const int w = m_layerInfo[i].m_texWidth;
				const int h = m_layerInfo[i].m_texHeight;
				const uint8* pData = m_layerInfo[i].m_pTextData;
				int minX = w, minY = h, maxX = -1, maxY = -1;
				for (int y = 0; y < h; y++)
				{
					const uint8* pRow = pData + y * m_layerInfo[i].m_texPitchBytes + 3; //alpha of BGRA
					for (int x = 0; x < w; x++, pRow += 4)
					{
						if (*pRow)
						{
							if (x < minX) minX = x;
							if (x > maxX) maxX = x;
							if (y < minY) minY = y;
							maxY = y;
						}
					}
				}
				if (maxX < 0)
				{
					pComp->SetCustomPrimitiveDataFloat(0, 0.0f);
					pComp->SetCustomPrimitiveDataFloat(1, 0.0f);
					pComp->SetCustomPrimitiveDataFloat(2, 0.0f);
					pComp->SetCustomPrimitiveDataFloat(3, 0.0f);
				}
				else
				{
					pComp->SetCustomPrimitiveDataFloat(0, (float)minX / w);
					pComp->SetCustomPrimitiveDataFloat(1, (float)minY / h);
					pComp->SetCustomPrimitiveDataFloat(2, (float)(maxX + 1) / w);
					pComp->SetCustomPrimitiveDataFloat(3, (float)(maxY + 1) / h);
				}
			}
		}
	}

}

void LayerInfo::SetLayerPosZ(float amount)
{
	FVector vPos = m_pActor->GetActorLocation();
	vPos.X += amount;
	m_pActor->SetActorLocation(vPos);
}
