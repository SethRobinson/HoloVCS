// Fill out your copyright notice in the Description page of Project Settings.
#include "LibretroManagerActor.h"
#include "AudioDevice.h"
#include "PlayerPawn.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/PointLight.h"
#include "Engine/DirectionalLight.h"
#include "Components/MeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
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

//3DS multiview convergence: where the zero-parallax (screen) plane sits within the scene's
//depth range.  0 = nearest content AT the screen (everything sinks behind), 1 = farthest
//content at the screen (everything pops out), core default 0.35.  -1 returns to the default.
static FAutoConsoleCommand CCmdHoloConvergence(
	TEXT("holo.Convergence"),
	TEXT("3DS multiview zero-parallax plane as a fraction of scene depth (0..1, -1 = core default 0.02 = near end, the real-3DS look). Usage: holo.Convergence 0.5"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args)
	{
		if (args.Num() < 1 || !g_pLibretroManager || !g_pLibretroManager->m_pLibretroManagedActor) return;
		ALibretroManagerActor* pActor = g_pLibretroManager->m_pLibretroManagedActor;
		if (pActor->RefusePausedHoloChange(true)) return;
		pActor->m_userConv01 = FCString::Atof(*args[0]);
		pActor->ApplyLayerDepth(); //pushes retro_holo_set_view_params with the new value
	}));

//Console twin of the = and - hotkeys.  The factor is applied inside the camera/capture
//fits (framing crop), so it survives resets, rom switches, and every refit.
static FAutoConsoleCommand CCmdHoloZoom(
	TEXT("holo.Zoom"),
	TEXT("Set the view zoom factor, same as the = and - hotkeys (0.2..5, 1 = default framing). Usage: holo.Zoom 1.3"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args)
	{
		if (args.Num() < 1 || !g_pLibretroManager || !g_pLibretroManager->m_pLibretroManagedActor) return;
		g_pLibretroManager->m_pLibretroManagedActor->SetUserZoom(FCString::Atof(*args[0]));
	}));

//Console twin of the fly-cam d-pad magnifier.  The harness cannot press a d-pad, and this is
//how a zoomed quilt inspection shot gets reproduced headlessly (pair it with holo.FlyPose).
//Only meaningful while the fly camera is out - leaving fly mode resets it to 1.
static FAutoConsoleCommand CCmdHoloFlyZoom(
	TEXT("holo.FlyZoom"),
	TEXT("Fly-camera magnifier: shrinks the capture framing with the focal plane pinned (0.2..20, 1 = off). Usage: holo.FlyZoom 8"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args)
	{
		if (args.Num() < 1 || !g_pLibretroManager || !g_pLibretroManager->m_pLibretroManagedActor) return;
		//route through the pawn when there is one, or its own copy of the factor goes stale and
		//the next d-pad press snaps back to whatever it last held
		if (g_pLibretroManager->m_pPlayerPawn)
		{
			g_pLibretroManager->m_pPlayerPawn->ApplyFlyZoom(FCString::Atof(*args[0]), true);
			return;
		}
		g_pLibretroManager->m_pLibretroManagedActor->SetFlyZoom(FCString::Atof(*args[0]));
	}));

//Console twin of the Shift+number debug visualization hotkeys (3DS only)
static FAutoConsoleCommand CCmdHoloViz(
	TEXT("holo.Viz"),
	TEXT("Toggle a 3DS debug view: wire, clay, unlit, depthbw, heat, rainbow, xray, off, or a raw numeric mask. Usage: holo.Viz wire"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args)
	{
		if (args.Num() < 1 || !g_pLibretroManager || !g_pLibretroManager->m_pLibretroManagedActor) return;
		ALibretroManagerActor* pActor = g_pLibretroManager->m_pLibretroManagedActor;
		FString s = args[0].ToLower();
		if (s == TEXT("off")) pActor->ClearHoloViz();
		else if (s == TEXT("wire")) pActor->ToggleHoloViz(HOLO_VIZ_WIREFRAME, "Wireframe");
		else if (s == TEXT("clay")) pActor->ToggleHoloViz(HOLO_VIZ_CLAY, "Clay (untextured)");
		else if (s == TEXT("unlit")) pActor->ToggleHoloViz(HOLO_VIZ_UNLIT, "3DS lighting off");
		else if (s == TEXT("depthbw")) pActor->ToggleHoloViz(HOLO_VIZ_DEPTH_GRAY, "Depth B&W");
		else if (s == TEXT("heat")) pActor->ToggleHoloViz(HOLO_VIZ_DEPTH_HEAT, "Depth heatmap");
		else if (s == TEXT("rainbow")) pActor->ToggleHoloViz(HOLO_VIZ_SLICE_RAINBOW, "Slice rainbow");
		else if (s == TEXT("xray")) pActor->ToggleHoloViz(HOLO_VIZ_XRAY, "X-ray (multiview only)");
		else
		{
			if (pActor->RefusePausedHoloChange(false)) return;
			pActor->m_holoVizFlags = (uint32)FCString::Atoi(*args[0]);
			pActor->ApplyHoloViz();
		}
	}));

//Cutaway plane, the multiview successor of the band-mode layer peel (';' and ''' drive it
//on 3DS multiview; this cvar works in every capture mode for the harness)
static FAutoConsoleCommand CCmdHoloCutaway(
	TEXT("holo.Cutaway"),
	TEXT("3DS cutaway plane 0..1 (0 = off): discards geometry nearer than the plane so what's behind shows. Usage: holo.Cutaway 0.5"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args)
	{
		if (args.Num() < 1 || !g_pLibretroManager || !g_pLibretroManager->m_pLibretroManagedActor) return;
		ALibretroManagerActor* pActor = g_pLibretroManager->m_pLibretroManagedActor;
		if (pActor->RefusePausedHoloChange(false)) return;
		pActor->m_cutaway01 = FMath::Clamp(FCString::Atof(*args[0]), 0.0f, 1.0f);
		pActor->ApplyHoloViz();
	}));

//Console twin of the B key / bare left trigger (landscape Looking Glass panels): swap the
//display between the 3D top screen and the 3DS bottom screen.  Harness-drivable.
static FAutoConsoleCommand CCmdHoloBottomScreen(
	TEXT("holo.BottomScreen"),
	TEXT("Landscape 3DS: swap the display between the 3D top screen and the bottom screen, same as the B key (the left trigger shows the bottom screen only while held). Usage: holo.BottomScreen"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args)
	{
		if (!g_pLibretroManager || !g_pLibretroManager->m_pLibretroManagedActor) return;
		g_pLibretroManager->m_pLibretroManagedActor->ToggleBottomScreenFocus();
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
			pComp1->SetCastShadow(g_pLibretroManager->m_profManager.m_layerSetupInfo[layerID].m_bCastShadows);
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
			RepositionBottomScreen(); //deterministic placement, shared with the 1Hz self-heal
		}
	}

	//re-apply the debug layer peel (';' and ''' hotkeys) to the freshly spawned actors
	//BEFORE the fits below, so they see the final visibility/tag state
	SetLayersPeeled(m_layersPeeled);

	//One deterministic finish for every rebuild: re-spread to the current depth scale,
	//re-place the bottom screen, push the view params to the core (the per-system depth
	//default never used to arrive - the core sat at its own 1.0 until a hotkey press),
	//and run both camera/capture fits.
	ApplyLayerDepth();
	ApplyHoloViz(); //debug views survive rom switches and core reloads too
}

//Debug view: hide the N nearest layers so the back of the diorama is visible on the
//device.  Layer 0 is the DEEPEST, so peeling hides from the highest index down.  The
//sprite path skips hidden actors (the show-only gather checks IsHidden), so this works
//identically in the flat scene and the hologram.  The 3DS bottom-screen quad at index
//GetLayerCount() is not part of the top stack and stays visible.
void ALibretroManagerActor::SetLayersPeeled(int count)
{
	m_layersPeeled = FMath::Clamp(count, 0, GetLayerCount() - 1);
	for (int i = 0; i < FMath::Min((int)m_layerInfo.size(), GetLayerCount()); i++)
	{
		if (m_layerInfo[i].m_pActor)
		{
			const bool bPeeled = (i >= GetLayerCount() - m_layersPeeled);
			m_layerInfo[i].m_pActor->SetActorHiddenInGame(bPeeled);
			//Tag peeled actors so the capture fit keeps them in the framing AABB: peeling
			//must never move the focal plane or capture size (an off-focal-plane quilt
			//carrier picks up a whole-frame parallax no depth setting can remove).  Other
			//hidden actors (unused layers, another game's geometry) stay excluded.
			if (bPeeled)
			{
				m_layerInfo[i].m_pActor->Tags.AddUnique(FName(TEXT("PeelHidden")));
			}
			else
			{
				m_layerInfo[i].m_pActor->Tags.Remove(FName(TEXT("PeelHidden")));
			}
		}
	}
}

//3DS multiview (mode 2): the quilt carrier is a layer-like quad at m_layerInfo[count+1]
//whose texture holds the packed per-view quilt.  It sits AT the layer stack's center
//depth = the capture's focal plane, so the LKG sprite path projects it with ZERO added
//parallax (the parallax is baked into the per-view images); tag "HoloQuilt" plus custom
//primitive data floats 4-6 (viewCount/cols/rows) tell the plugin to blit one view per
//lens tile from it.  Lazy: the quilt dimensions arrive with the first core delivery.
LayerInfo* ALibretroManagerActor::EnsureQuiltCarrier(int quiltW, int quiltH, int viewCount, int cols, int rows)
{
	const int carrierIdx = GetLayerCount() + 1;
	if ((int)m_layerInfo.size() <= carrierIdx)
	{
		m_layerInfo.resize(carrierIdx + 1);
	}
	LayerInfo* pQ = &m_layerInfo[carrierIdx];
	if (pQ->m_pActor && (int)pQ->m_texWidth == quiltW && (int)pQ->m_texHeight == quiltH)
	{
		if (!m_bQuiltCarrierActive)
		{
			//re-arm after a dormant spell (SetQuiltCarrierActive(false) zeroed float 4)
			UMeshComponent* pMesh = (UMeshComponent*)pQ->m_pActor->GetComponentByClass(UMeshComponent::StaticClass());
			if (pMesh) pMesh->SetCustomPrimitiveDataFloat(4, (float)viewCount);
			m_bQuiltCarrierActive = true;
		}
		return pQ;
	}
	if (pQ->m_pActor)
	{
		pQ->m_pActor->Destroy();
		pQ->m_pActor = nullptr;
		pQ->Cleanup();
	}
	if (!SetupLayer(pQ, (char*)"LayerHoloQuilt", quiltW, quiltH, carrierIdx))
	{
		LogMsg("EnsureQuiltCarrier: SetupLayer failed");
		return nullptr;
	}
	pQ->m_bIsQuiltCarrier = true;

	AActor* pActor = pQ->m_pActor;
	FVector vPos = pActor->GetActorLocation();
	float midX = vPos.X;
	if (GetLayerCount() > 0 && m_layerInfo[0].m_pActor && m_layerInfo[GetLayerCount() - 1].m_pActor)
	{
		midX = 0.5f * (m_layerInfo[0].m_pActor->GetActorLocation().X +
			m_layerInfo[GetLayerCount() - 1].m_pActor->GetActorLocation().X);
	}
	vPos.X = midX;
	vPos.Y = m_corePosition.X;
	vPos.Z = m_corePosition.Y;
	pActor->SetActorLocation(vPos);
	pActor->Tags.AddUnique(FName(TEXT("HoloQuilt")));

	UMeshComponent* pMesh = (UMeshComponent*)pActor->GetComponentByClass(UMeshComponent::StaticClass());
	if (pMesh)
	{
		//full content rect (floats 0-3, never alpha-scanned) + the quilt metadata
		pMesh->SetCustomPrimitiveDataFloat(0, 0.0f);
		pMesh->SetCustomPrimitiveDataFloat(1, 0.0f);
		pMesh->SetCustomPrimitiveDataFloat(2, 1.0f);
		pMesh->SetCustomPrimitiveDataFloat(3, 1.0f);
		pMesh->SetCustomPrimitiveDataFloat(4, (float)viewCount);
		pMesh->SetCustomPrimitiveDataFloat(5, (float)cols);
		pMesh->SetCustomPrimitiveDataFloat(6, (float)rows);
		//keep the raw quilt collage out of the flat/2D-spectator scene render; the
		//sprite path reads the component directly and ignores this flag
		pMesh->SetVisibleInSceneCaptureOnly(true);
		pMesh->SetCastShadow(false);
		pMesh->bReceiveMobileCSMShadows = false;
	}
	m_bQuiltCarrierActive = true;

	//the capture's show-only list was built before this actor existed; the reposition runs
	//first so a carrier born while the landscape bottom-screen focus is active starts
	//hidden and the fit doesn't frame it
	RepositionBottomScreen();
	FitLookingGlassCaptureToLayers(GetWorld());
	LogMsg("Quilt carrier ready: %dx%d px, %d views (%dx%d tiles) at focal X %.1f",
		quiltW, quiltH, viewCount, cols, rows, midX);
	return pQ;
}

void ALibretroManagerActor::SetQuiltCarrierActive(bool bActive)
{
	if (m_bQuiltCarrierActive == bActive) return;
	const int carrierIdx = GetLayerCount() + 1;
	if ((int)m_layerInfo.size() <= carrierIdx || !m_layerInfo[carrierIdx].m_pActor) return;
	m_bQuiltCarrierActive = bActive;
	LogMsg("Quilt carrier -> %s", bActive ? "ACTIVE" : "dormant (flat middle-band fallback)");
	if (!bActive)
	{
		//float 4 (viewCount) doubles as the plugin's draw enable; 0 = skip the quilt
		//blit so the flat middle-band composite shows through (2D screens).  The
		//re-activate path runs through EnsureQuiltCarrier, which restores the value.
		UMeshComponent* pMesh = (UMeshComponent*)m_layerInfo[carrierIdx].m_pActor->GetComponentByClass(UMeshComponent::StaticClass());
		if (pMesh) pMesh->SetCustomPrimitiveDataFloat(4, 0.0f);
	}
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

	//multiview quilt carrier: keep it AT the respread stack's center depth (= the focal
	//plane after the refit below) so its per-view blit stays parallax-free
	const int carrierIdx = GetLayerCount() + 1;
	if ((int)m_layerInfo.size() > carrierIdx && m_layerInfo[carrierIdx].m_pActor &&
		GetLayerCount() > 0 && m_layerInfo[0].m_pActor && m_layerInfo[GetLayerCount() - 1].m_pActor)
	{
		FVector vPos = m_layerInfo[carrierIdx].m_pActor->GetActorLocation();
		vPos.X = 0.5f * (m_layerInfo[0].m_pActor->GetActorLocation().X +
			m_layerInfo[GetLayerCount() - 1].m_pActor->GetActorLocation().X);
		m_layerInfo[carrierIdx].m_pActor->SetActorLocation(vPos);
	}

	//multiview (mode 2): the hologram's parallax lives in the core's per-view shear now -
	//push the depth knob live through the ABI v4 export (the core ignores repeats)
	PushHoloViewParams();

	//3DS: keep the bottom screen parked correctly (cheap and idempotent)
	RepositionBottomScreen();

	//the spread changed: reframe the flat camera (or it crops a deeper stack) and the Looking
	//Glass capture (which also re-parks the LayerBG wall behind the new deepest layer)
	if (m_libretroManager.m_pPlayerPawn)
	{
		m_libretroManager.m_pPlayerPawn->FitFlatCameraToLayers();
	}
	FitLookingGlassCaptureToLayers(GetWorld());
}

//Push the current depth/convergence to the 3DS multiview core - in capture mode 2 the
//core's per-view shear is the ONLY parallax source, so a push that silently fails reads
//as "the depth keys do nothing".  Safe to call often (the core ignores repeated values).
bool ALibretroManagerActor::PushHoloViewParams()
{
	if (!g_pLibretroManager || g_pLibretroManager->m_holoCaptureMode != 2) return false;
	if (!g_pLibretroManager->m_core.retro_holo_set_view_params)
	{
		//mode 2 was negotiated, so a multiview-capable core asked for it - a missing
		//export means the loaded azahar_libretro.dll predates the view-params ABI
		if (!m_bWarnedNoViewParamExport)
		{
			m_bWarnedNoViewParamExport = true;
			LogMsg("WARNING: core negotiated multiview but exports no retro_holo_set_view_params - stale azahar_libretro.dll?");
			ShowStatusMessage("3D depth control unavailable (stale 3DS core DLL?)");
		}
		return false;
	}
	g_pLibretroManager->m_core.retro_holo_set_view_params(m_userDepthScale, m_userConv01);
	if (m_userDepthScale != m_lastPushedSep || m_userConv01 != m_lastPushedConv)
	{
		m_lastPushedSep = m_userDepthScale;
		m_lastPushedConv = m_userConv01;
		LogMsg("Multiview view params pushed: depth scale %.2f, convergence %.2f",
			m_userDepthScale, m_userConv01);
	}
	return true;
}

//3DS while PAUSED: the core only renders inside retro_run, so a viz/cutaway change (any
//capture mode) or a depth/convergence change (multiview, where the parallax lives in the
//core) has no frame to show itself on.  The savestate-pin re-render tried for this was
//cut by Seth as bad UI (seconds-long hitches), so the change is refused outright.
bool ALibretroManagerActor::RefusePausedHoloChange(bool bMode2Only)
{
	if (!g_pLibretroManager || g_pLibretroManager->m_emulatorType != EMULATOR_3DS) return false;
	if (bMode2Only && g_pLibretroManager->m_holoCaptureMode != 2) return false;
	if (!g_pLibretroManager->GetGamePaused()) return false;
	LogMsg("Refused holo change while paused");
	ShowStatusMessage("Can't change that while paused");
	return true;
}

//Landscape panels (device aspect > 1: the original 8.9" Looking Glass etc) run the
//one-screen-at-a-time 3DS layout; portrait panels and the flat build (aspect 0) keep the
//stacked two-screen layout.  -lkglandscape forces it on for testing without a device.
bool ALibretroManagerActor::IsLandscape3DSLayout()
{
	if (!g_pLibretroManager || g_pLibretroManager->m_emulatorType != EMULATOR_3DS) return false;
	static const bool bForced = FParse::Param(FCommandLine::Get(), TEXT("lkglandscape"));
	if (bForced) return true;
	return GetLookingGlassDeviceAspect(GetWorld()) > 1.0f;
}

//B key / holo.BottomScreen: on a landscape panel, toggle which 3DS screen owns the
//display.  The left trigger instead HOLDS the bottom screen in view (SetBottomScreenFocus
//from the pawn's press/release).  Everything else (visibility, placement, capture refit)
//is enforced by RepositionBottomScreen, which ApplyLayerDepth calls.
void ALibretroManagerActor::ToggleBottomScreenFocus()
{
	if (!g_pLibretroManager || g_pLibretroManager->m_emulatorType != EMULATOR_3DS)
	{
		ShowStatusMessage("Bottom screen toggle is 3DS-only");
		return;
	}
	if (!IsLandscape3DSLayout())
	{
		ShowStatusMessage("Both screens already shown (toggle is for landscape displays)");
		return;
	}
	SetBottomScreenFocus(!m_bBottomFocus3DS);
	ShowStatusMessage(m_bBottomFocus3DS ? "Bottom screen (B to return, or hold LT)" : "3D screen");
}

void ALibretroManagerActor::SetBottomScreenFocus(bool bBottom)
{
	if (!g_pLibretroManager || g_pLibretroManager->m_emulatorType != EMULATOR_3DS) return;
	if (!IsLandscape3DSLayout()) return;
	if (m_bBottomFocus3DS == bBottom) return;
	m_bBottomFocus3DS = bBottom;
	ApplyLayerDepth(); //repositions the bottom screen, enforces visibility, refits camera+capture
	LogMsg("Landscape 3DS: %s screen focus", m_bBottomFocus3DS ? "BOTTOM" : "top");
}

//3DS: (re)park the bottom-screen quad one screen-height below the top-screen stack.
//Deterministic on purpose: the height comes from the mesh ASSET bounds through the live
//component transform (a just-spawned actor's cached world bounds can read zero, which
//used to land the bottom screen exactly ON the top screen), and the device-aspect gap is
//recomputed at call time because the plugin can resolve the panel aspect well after the
//first InitLayers.  Idempotent and cheap - called from every depth/zoom apply and the
//1Hz self-heal in Tick.
//LANDSCAPE PANELS show one screen at a time (see IsLandscape3DSLayout): this is also where
//that layout is enforced - the bottom quad hides while the 3D screen has the display, and
//with the bottom screen focused the top stack (band layers + multiview quilt carrier)
//hides and the bottom quad moves to the stack's center depth so the capture fit frames it
//alone, on the focal plane.
void ALibretroManagerActor::RepositionBottomScreen()
{
	if (!g_pLibretroManager || g_pLibretroManager->m_emulatorType != EMULATOR_3DS) return;
	if ((int)m_layerInfo.size() <= GetLayerCount()) return;
	AActor* pActor = m_layerInfo[GetLayerCount()].m_pActor;
	if (!pActor) return;

	const bool bLandscape = IsLandscape3DSLayout();
	const bool bBottomFocus = bLandscape && m_bBottomFocus3DS;

	//bottom quad: hidden in landscape while the 3D screen owns the display
	pActor->SetActorHiddenInGame(bLandscape && !bBottomFocus);

	//top stack: hidden while the bottom screen owns the display.  Restoring goes through
	//SetLayersPeeled so the debug peel state comes back exactly; while hidden here the
	//PeelHidden tags are stripped so the capture fit doesn't keep peeled layers in the
	//framing AABB (peel-tagged actors stay in the frame by design).
	if (bBottomFocus)
	{
		for (int i = 0; i < FMath::Min((int)m_layerInfo.size(), GetLayerCount()); i++)
		{
			if (m_layerInfo[i].m_pActor)
			{
				m_layerInfo[i].m_pActor->SetActorHiddenInGame(true);
				m_layerInfo[i].m_pActor->Tags.Remove(FName(TEXT("PeelHidden")));
			}
		}
	}
	else
	{
		SetLayersPeeled(m_layersPeeled); //idempotent; re-shows the top stack
	}
	const int carrierIdx = GetLayerCount() + 1;
	if ((int)m_layerInfo.size() > carrierIdx && m_layerInfo[carrierIdx].m_pActor)
	{
		m_layerInfo[carrierIdx].m_pActor->SetActorHiddenInGame(bBottomFocus);
	}

	if (bBottomFocus)
	{
		//center the bottom screen where the top stack lives: same mid-depth the quilt
		//carrier uses = the capture's focal plane after the refit, so it renders sharp
		FVector vPos = pActor->GetActorLocation();
		if (GetLayerCount() > 0 && m_layerInfo[0].m_pActor && m_layerInfo[GetLayerCount() - 1].m_pActor)
		{
			vPos.X = 0.5f * (m_layerInfo[0].m_pActor->GetActorLocation().X +
				m_layerInfo[GetLayerCount() - 1].m_pActor->GetActorLocation().X);
		}
		vPos.Y = m_corePosition.X;
		vPos.Z = m_corePosition.Y;
		pActor->SetActorLocation(vPos);
		return;
	}

	float quadWorldHeight = 0.0f;
	UStaticMeshComponent* pMesh = (UStaticMeshComponent*)pActor->GetComponentByClass(UStaticMeshComponent::StaticClass());
	if (pMesh && pMesh->GetStaticMesh())
	{
		FBox worldBox = pMesh->GetStaticMesh()->GetBoundingBox().TransformBy(pMesh->GetComponentTransform());
		quadWorldHeight = (float)worldBox.GetSize().Z;
	}
	if (quadWorldHeight <= 0.0f)
	{
		quadWorldHeight = (float)pActor->GetComponentsBoundingBox().GetSize().Z; //no static mesh - old path
	}
	if (quadWorldHeight <= 0.0f) return; //nothing sane to place against yet

	//world Y = horizontal, world Z = vertical; one screen-height down plus a gap.  On the
	//portrait Go (device aspect ~0.56) the hologram frame is bound by WIDTH, so there's
	//free vertical room for a real 80px gap between the screens.  Squarer panels
	//(Portrait 0.75 etc) are height-bound - a big gap would shrink both screens to fit,
	//so they keep the near-touching layout.
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

void ALibretroManagerActor::SetUserZoom(float factor, bool bShowStatus)
{
	m_userZoomFactor = FMath::Clamp(factor, 0.2f, 5.0f);
	ApplyLayerDepth(); //refits both views with the new framing, repositions the bottom screen

	if (!bShowStatus) return;
	char st[64];
	snprintf(st, sizeof(st), "Zoom: %d%%", (int)roundf(m_userZoomFactor * 100));
	ShowStatusMessage(st);
}

//Push the debug visualization mask + cutaway plane to the 3DS core.  No-op without the
//optional export (old DLL) or outside 3DS; the core dedupes repeated values.
void ALibretroManagerActor::ApplyHoloViz()
{
	if (!g_pLibretroManager || !g_pLibretroManager->m_core.retro_holo_set_debug) return;
	if (g_pLibretroManager->m_emulatorType != EMULATOR_3DS) return;
	uint32 mask = m_holoVizFlags;
	if (m_cutaway01 > 0.001f) mask |= HOLO_VIZ_CUTAWAY;
	//log-gated: the 1Hz self-heal re-push would spam the log otherwise, and a HELD cutaway
	//key sweeps the value every frame - log mask changes, 5% cutaway steps, and the
	//endpoints (off / 100%) only.  Every value still reaches the core.
	const bool bEndpointCrossed =
		((m_cutaway01 <= 0.001f) != (m_lastAppliedCutaway <= 0.001f)) ||
		((m_cutaway01 >= 0.999f) != (m_lastAppliedCutaway >= 0.999f));
	if (mask != m_lastAppliedVizMask || bEndpointCrossed ||
		FMath::Abs(m_cutaway01 - m_lastAppliedCutaway) >= 0.0495f)
	{
		LogMsg("Holo viz push: mask=0x%02x cutaway=%.2f", mask, m_cutaway01);
		m_lastAppliedVizMask = mask;
		m_lastAppliedCutaway = m_cutaway01;
	}
	g_pLibretroManager->m_core.retro_holo_set_debug(mask, m_cutaway01);
}

void ALibretroManagerActor::ToggleHoloViz(uint32 flag, const char* pName)
{
	if (!g_pLibretroManager) return;
	if (g_pLibretroManager->m_emulatorType != EMULATOR_3DS)
	{
		ShowStatusMessage("Debug views are 3DS-only");
		return;
	}
	if (RefusePausedHoloChange(false)) return;
	if (!g_pLibretroManager->m_core.retro_holo_set_debug)
	{
		ShowStatusMessage("This 3DS core DLL has no debug views (update azahar_libretro.dll)");
		return;
	}
	m_holoVizFlags ^= flag;
	//the two depth palettes are either/or
	if (flag == HOLO_VIZ_DEPTH_GRAY && (m_holoVizFlags & HOLO_VIZ_DEPTH_GRAY)) m_holoVizFlags &= ~HOLO_VIZ_DEPTH_HEAT;
	if (flag == HOLO_VIZ_DEPTH_HEAT && (m_holoVizFlags & HOLO_VIZ_DEPTH_HEAT)) m_holoVizFlags &= ~HOLO_VIZ_DEPTH_GRAY;
	ApplyHoloViz();
	char st[96];
	snprintf(st, sizeof(st), "%s %s", pName, (m_holoVizFlags & flag) ? "ON" : "off");
	ShowStatusMessage(st);
}

void ALibretroManagerActor::ClearHoloViz()
{
	if (RefusePausedHoloChange(false)) return;
	m_holoVizFlags = 0;
	m_cutaway01 = 0.0f;
	ApplyHoloViz();
	ShowStatusMessage("Debug views off");
}

void ALibretroManagerActor::NudgeCutaway(float delta, bool bContinuous)
{
	if (RefusePausedHoloChange(false)) return;
	m_cutaway01 = FMath::Clamp(m_cutaway01 + delta, 0.0f, 1.0f);
	//logged (status text is not) so the harness can prove which of ; and ' actually landed.
	//The per-frame held-key sweep (bContinuous) would be 60 lines/s here - it relies on
	//ApplyHoloViz's 5%-step log instead.
	if (!bContinuous) LogMsg("NudgeCutaway %+.2f -> %.2f", delta, m_cutaway01);
	ApplyHoloViz();
	char st[64];
	if (m_cutaway01 <= 0.001f)
	{
		snprintf(st, sizeof(st), "Cutaway off");
	}
	else
	{
		snprintf(st, sizeof(st), "Cutaway: %d%%", (int)roundf(m_cutaway01 * 100));
	}
	ShowStatusMessage(st);
}

void ALibretroManagerActor::SetUserDepthScale(float scale, bool bShowStatus)
{
	//in multiview the depth lives in the core, which cannot re-render while paused
	if (RefusePausedHoloChange(true)) return;
	//0 = completely flat is allowed (Seth request); a hair below 0.05 snaps to true 0 so
	//the [ key can land exactly on "no 3d at all"
	m_userDepthScale = FMath::Clamp(scale, 0.0f, 5.0f);
	if (m_userDepthScale < 0.05f) m_userDepthScale = 0.0f;
	ApplyLayerDepth();

	if (!bShowStatus) return; //holo.DepthRamp calls per tick - status text would land in every captured frame

	char st[64];
	snprintf(st, sizeof(st), "3D depth: %d%%", (int)roundf(m_userDepthScale * 100));
	ShowStatusMessage(st);
}

//'{' and '}': nudge the multiview convergence.  0 = nearest content AT the screen
//(everything sinks behind), 1 = farthest content at the screen (everything pops out);
//the core default is 0.02 = the near end, matching what real 3DS games do - measured
//from Metroid's own stereo pair, the game converges AT its nearest scene content
//(m_userConv01 -1 = keep default, so the first nudge starts from there).
//holo.Convergence remains the console twin (-1 restores the default).
void ALibretroManagerActor::NudgeConvergence(float delta)
{
	if (RefusePausedHoloChange(true)) return;
	const float cur = (m_userConv01 >= 0.0f) ? m_userConv01 : 0.02f;
	m_userConv01 = FMath::Clamp(cur + delta, 0.0f, 1.0f);
	ApplyLayerDepth(); //pushes the new value (and refreshes a paused screen)

	char st[64];
	snprintf(st, sizeof(st), "Convergence: %d%% pop-out", (int)roundf(m_userConv01 * 100));
	ShowStatusMessage(st);
}

//'\' hotkey: instant 2D/3D toggle.  Zeroing stashes the current depth so the next press
//restores exactly where you were; falls back to 100% if there is nothing to restore.
//Works for every system; on 3DS multiview it is refused while paused like the other
//depth keys (the core cannot re-render a paused frame).
void ALibretroManagerActor::Toggle2D3D()
{
	if (RefusePausedHoloChange(true)) return;
	if (m_userDepthScale > 0.0f)
	{
		m_stashed3DDepth = m_userDepthScale;
		SetUserDepthScale(0.0f, false);
		ShowStatusMessage("2D mode");
	}
	else
	{
		SetUserDepthScale(m_stashed3DDepth > 0.0f ? m_stashed3DDepth : 1.0f);
	}
}

//The LookingGlassCapture actor in the map was placed/sized for the old 5.6-era world scale, and each
//emulator uses a wildly different scale now (NES ~41 units, Atari ~445, VB ~310), so a static capture
//can never frame them all.  Refit it to the current layer AABB whenever the layers rebuild.  The
//plugin classes are resolved by NAME so the game module keeps zero compile-time plugin dependency
//(this is a no-op in the flat build, where the actor doesn't exist).
//Device aspect (width/height) of the connected Looking Glass panel, or 0 when no capture
//actor exists (flat build / no plugin).  Lets layout adapt per panel: the portrait Go
//(~0.56) has spare vertical room that squarer panels don't.
//The hologram capture actor, resolved by class name (zero compile-time plugin dependency).
//nullptr in the flat build / when the plugin is disabled.
static AActor* GetLookingGlassCaptureActor(UWorld* pWorld)
{
	if (!pWorld) return nullptr;
	for (TActorIterator<AActor> it(pWorld); it; ++it)
	{
		if (it->GetClass()->GetName() == TEXT("LookingGlassCapture")) return *it;
	}
	return nullptr;
}

//Push a new capture "Size" (the half-WIDTH of the frame at the focal plane) through the
//plugin's blueprint setter.  SetSize fires OnLookingGlassObjectChanged, which retargets the
//actor's spring arm to the new camera distance - so the actor, and therefore the FOCAL PLANE,
//never moves.  That is what makes the fly-cam magnifier stay sharp on the panel.
//Returns false when there is no capture actor (flat build).
static bool ApplyLookingGlassCaptureSize(UWorld* pWorld, float size)
{
	AActor* pCapture = GetLookingGlassCaptureActor(pWorld);
	if (!pCapture) return false;
	for (UActorComponent* pComp : pCapture->GetComponents())
	{
		if (pComp->GetClass()->GetName() != TEXT("LookingGlassSceneCaptureComponent2D")) continue;
		if (UFunction* pFunc = pComp->FindFunction(TEXT("SetSize")))
		{
			struct { float InSize; } params = { size };
			pComp->ProcessEvent(pFunc, &params);
			return true;
		}
	}
	return false;
}

static float GetLookingGlassDeviceAspect(UWorld* pWorld)
{
	AActor* pCapture = GetLookingGlassCaptureActor(pWorld);
	if (!pCapture) return 0.0f;
	for (UActorComponent* pComp : pCapture->GetComponents())
	{
		if (pComp->GetClass()->GetName() != TEXT("LookingGlassSceneCaptureComponent2D")) continue;
		if (UFunction* pAspectFunc = pComp->FindFunction(TEXT("GetAspectRatio")))
		{
			struct { float ReturnValue; } aspectParams = { 0.0f };
			pComp->ProcessEvent(pAspectFunc, &aspectParams);
			if (aspectParams.ReturnValue > 0.05f) return aspectParams.ReturnValue;
		}
	}
	return 0.0f;
}

//One-shot: switch the capture component to the plugin's Automatic tiling preset so it
//matches the connected device (Portrait: 48 views at 3360x3360, aspect 0.75).
//Re-registering the component is the only reflection-safe way to run the plugin's
//UpdateTilingProperties.  Pass -lkgmaptiling to keep the tiling saved in the map.
//Shared by the capture fit AND GetLookingGlassTiling, because on a boot straight into
//the 3DS the core asks for the tile grid BEFORE the first layer fit ever runs.
static void EnsureLookingGlassAutoTiling(UActorComponent* pComp)
{
	static bool bTilingApplied = false;
	if (bTilingApplied || FParse::Param(FCommandLine::Get(), TEXT("lkgmaptiling"))) return;
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

bool GetLookingGlassTiling(UWorld* pWorld, int& tilesX, int& tilesY)
{
	AActor* pCapture = GetLookingGlassCaptureActor(pWorld);
	if (!pCapture) return false;
	for (UActorComponent* pComp : pCapture->GetComponents())
	{
		if (pComp->GetClass()->GetName() != TEXT("LookingGlassSceneCaptureComponent2D")) continue;
		EnsureLookingGlassAutoTiling(pComp); //resolve the device preset before reading
		FStructProperty* pStructProp = FindFProperty<FStructProperty>(pComp->GetClass(), TEXT("TilingValues"));
		if (!pStructProp) return false;
		void* pStruct = pStructProp->ContainerPtrToValuePtr<void>(pComp);
		FIntProperty* pX = FindFProperty<FIntProperty>(pStructProp->Struct, TEXT("TilesX"));
		FIntProperty* pY = FindFProperty<FIntProperty>(pStructProp->Struct, TEXT("TilesY"));
		if (!pX || !pY) return false;
		tilesX = pX->GetPropertyValue_InContainer(pStruct);
		tilesY = pY->GetPropertyValue_InContainer(pStruct);
		return tilesX >= 1 && tilesY >= 1;
	}
	return false;
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
		//Peel-hidden actors (the ';' debug peel) stay IN the framing: peeling must never move
		//the focal plane or capture size, or the quilt carrier ends up off the focal plane and
		//the whole hologram picks up a parallax shift no depth setting can remove.
		const bool bPeelHidden = pActor->ActorHasTag(FName(TEXT("PeelHidden")));
		if (pActor->IsHidden() && !bPeelHidden) { LogMsg("LKG fit: skipping hidden layer actor %s", TCHAR_TO_ANSI(*pActor->GetName())); continue; }
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

	//While the fly cam is driving the capture, keep the list/size upkeep below but don't yank
	//the actor back to center (layer rebuilds - rom switches, depth ramps - land here mid-flight)
	const bool bFlyCamOut = g_pLibretroManager && g_pLibretroManager->m_pPlayerPawn &&
		g_pLibretroManager->m_pPlayerPawn->IsFlyCamEnabled();

	for (TActorIterator<AActor> it(pWorld); it; ++it)
	{
		if (it->GetClass()->GetName() != TEXT("LookingGlassCapture")) continue;

		if (!bFlyCamOut)
		{
			it->SetActorLocation(box.GetCenter());
		}

		FVector vSize = box.GetSize();

		for (UActorComponent* pComp : it->GetComponents())
		{
			if (pComp->GetClass()->GetName() != TEXT("LookingGlassSceneCaptureComponent2D")) continue;

			//The map was saved with the 5.6-era "Custom" tiling; switch to the device's
			//Automatic preset (one-shot, shared with GetLookingGlassTiling below).
			EnsureLookingGlassAutoTiling(pComp);

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
			//user zoom (= and - keys) as a framing crop: dividing the capture size zooms in
			//without touching any world quad.  Scaling the quads instead got normalized right
			//back out by this very fit, which is why zoom used to revert on every refit.
			//The fly-cam magnifier composes on top, and the pre-zoom size is cached so
			//SetFlyZoom can re-derive it live without paying for a whole refit.
			if (g_pLibretroManager && g_pLibretroManager->m_pLibretroManagedActor)
			{
				ALibretroManagerActor* pMgr = g_pLibretroManager->m_pLibretroManagedActor;
				pMgr->m_lastFitCaptureBaseSize = captureSize;
				captureSize /= FMath::Max(pMgr->m_userZoomFactor * pMgr->m_flyZoomFactor, 0.01f);
			}

			if (ApplyLookingGlassCaptureSize(pWorld, captureSize))
			{
				LogMsg("Fit LookingGlass capture: center %.0f,%.0f,%.0f capture size %.1f (aspect %.2f)",
					box.GetCenter().X, box.GetCenter().Y, box.GetCenter().Z, captureSize, deviceAspect);
			}
		}
	}
}

//While the fly cam is out on the LKG build it drives the hologram capture directly.  A rotated
//capture makes the sprite fast path bail to the scene-capture quilt (~13fps, accepted - this is
//a debug/exploration mode), so the device shows the diorama from any angle.
void ALibretroManagerActor::SetLKGCaptureFlyTransform(const FVector& pos, const FRotator& rot)
{
	if (AActor* pCapture = GetLookingGlassCaptureActor(GetWorld()))
	{
		pCapture->SetActorLocation(pos);
		pCapture->SetActorRotation(rot);
	}
}

bool ALibretroManagerActor::GetLKGCaptureTransform(FVector& pos, FRotator& rot)
{
	if (AActor* pCapture = GetLookingGlassCaptureActor(GetWorld()))
	{
		pos = pCapture->GetActorLocation();
		rot = pCapture->GetActorRotation();
		return true;
	}
	return false;
}

//Fly-cam magnifier: d-pad up/down while flying (holo.FlyZoom is the harness twin).  Shrinking
//the capture Size pulls the camera toward the focal plane, which the spring arm keeps pinned to
//the capture ACTOR - so the picture magnifies without anything leaving the focal plane and the
//panel stays sharp.  Flying closer with the stick does the opposite: content drifts off the
//focal plane and the lens reconstructs it blurry, which is what made the raw multiview quilt
//unreadable when zoomed.  Deliberately does NOT refit: FitLookingGlassCaptureToLayers logs a
//line per layer actor, which a held d-pad would turn into ~1500 log lines a second at 24 layers.
void ALibretroManagerActor::SetFlyZoom(float factor, bool bShowStatus)
{
	m_flyZoomFactor = FMath::Clamp(factor, 0.2f, 20.0f);
	if (m_lastFitCaptureBaseSize > 0.0f)
	{
		ApplyLookingGlassCaptureSize(GetWorld(),
			m_lastFitCaptureBaseSize / FMath::Max(m_userZoomFactor * m_flyZoomFactor, 0.01f));
	}

	if (!bShowStatus) return;
	char st[64];
	snprintf(st, sizeof(st), "Fly zoom: %d%%", (int)roundf(m_flyZoomFactor * 100));
	ShowStatusMessage(st);
}

//Fly-cam exit: back to the fitted framing.  The fit never touches rotation, and only an
//unrotated capture qualifies for the 60fps sprite path, so zero it explicitly.
void ALibretroManagerActor::RefitLKGCapture()
{
	if (AActor* pCapture = GetLookingGlassCaptureActor(GetWorld()))
	{
		pCapture->SetActorRotation(FRotator::ZeroRotator);
	}
	FitLookingGlassCaptureToLayers(GetWorld());
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

		//3DS 1Hz self-heal: the core dedupes repeated view-param/viz pushes and the bottom
		//screen reposition is idempotent, so this cheaply covers a late-resolving device
		//aspect and any future code path that forgets to re-push after a core reload
		if (g_pLibretroManager && g_pLibretroManager->m_emulatorType == EMULATOR_3DS &&
			m_libretroManager.IsCoreLoaded())
		{
			PushHoloViewParams();
			ApplyHoloViz();
			RepositionBottomScreen();
		}
	}

	m_framesRendered++;

	//Update() must run even with no core loaded: its first line serves the automation
	//harness (quit/rom switch/shots), and it early-outs internally right after. Gating it
	//behind IsCoreLoaded left the harness dead whenever a rom failed to load (e.g. the
	//3DS "really is encrypted" refusal), so only the frame/texture work below stays gated.
	m_libretroManager.Update();

	if (!m_libretroManager.IsCoreLoaded())
	{
		//LogMsg("Core not loaded!");
		return;
	}

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
		//The quilt carrier skips the scan (4.6M texels/frame, and its CPD floats carry the
		//quilt metadata instead of a content rect).
		if (!m_layerInfo[i].m_bIsQuiltCarrier && m_layerInfo[i].m_pActor && m_layerInfo[i].m_pTextData)
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
