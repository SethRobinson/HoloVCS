//  Programmer(s):  Seth A. Robinson (seth@rtsoft.com)

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/Texture2D.h"
#include "RHITypes.h" //FUpdateTextureRegion2D
#include "Shared/UnrealMisc.h"
#include "LibretroManager.h"
#include "Engine/TextRenderActor.h"
#include "Components/TextRenderComponent.h"
#include "RTAudioBUffer.h"
#include "HoloVCS.h"
//#include "Game/HoloPlayCapture.h"
#include "LibretroManagerActor.generated.h"

void OnWasRestartedInEditor();

//The connected Looking Glass device's quilt tile grid (from the plugin's resolved
//TilingValues, read by reflection; applies the Automatic preset first if needed).
//Returns false in the flat build / when the plugin is absent.  Used both for layout
//and to decide the 3DS holo capture mode (grid present = multiview default).
bool GetLookingGlassTiling(class UWorld* pWorld, int& tilesX, int& tilesY);

class UMaterial;

enum eLightingMode
{
	LIGHTING_MODE_NORMAL,
	LIGHTING_MODE_NONE,

	//add above ehre
	LIGHTING_MODE_COUNT

};

class LayerInfo
{
public:

	~LayerInfo()
	{
		//SAFE_DELETE_ARRAY(m_pTextData);
		
		//causes crashes sometimes, maybe we should be doing it between layer inits, but not at shutdown
	}

	void Cleanup()
	{
		SAFE_DELETE_ARRAY(m_pUploadBuffer[0]);
		SAFE_DELETE_ARRAY(m_pUploadBuffer[1]);
		
		if (m_pDynamicTexture)
		{
			m_pDynamicTexture->ReleaseResource();
			m_pDynamicTexture = 0;
		}
		SAFE_DELETE_ARRAY(m_pTextData);

	}

	uint8* m_pTextData = 0;
	UPROPERTY()
	UMaterialInstanceDynamic* pUMatDyn = 0;
	UPROPERTY()
	UTexture2D* m_pDynamicTexture = 0;
	UPROPERTY()
	FUpdateTextureRegion2D* mUpdateTextureRegion = 0;

	
	unsigned int m_texWidth = 256;
	unsigned int m_texHeight = 256;
	unsigned int m_texPitchBytes = 256 * 4;
	uint32 mDataSize;
	float m_distanceMod = 0;
	bool m_bUsedThisFrame = false;
	//3DS holo layered mode: per-layer upload gating.  m_bDirty = pixels changed since the
	//last GPU upload (only the 3DS holo path maintains it; other systems upload every
	//frame as before).  m_bHoloContent = the core's `used` flag from the last delivery,
	//so a used->unused transition clears the texture once instead of showing stale pixels.
	bool m_bDirty = true;
	bool m_bHoloContent = false;
	//multiview quilt carrier (3DS mode 2): the texture holds the packed per-view quilt
	//the LKG sprite path blits per tile.  Skips the per-frame alpha scan (4.6M texels).
	bool m_bIsQuiltCarrier = false;
	//ping-pong staging for UpdateTextureRegions (replaces a 384KB heap alloc per layer
	//per frame; two buffers because the render thread consumes the copy asynchronously)
	uint8* m_pUploadBuffer[2] = { 0, 0 };
	int m_uploadBufferIndex = 0;
	FVector m_vStartingPos; //remember so we can move back to it later
	uint8* GetPixelBuffer() { return m_pTextData; };
	TextureFilter m_filterToUse = TextureFilter::TF_Default;
	TextureGroup m_LODGroupToUse = TextureGroup::TEXTUREGROUP_World;
	
	AActor *m_pActor = NULL; //ourself
	bool m_HasDoneFirstTimeInit = false;
	void SetLayerPosZ(float amount); //between -10 and 10?  0 means in the middle in perfect focus

	
};

UCLASS()
class HOLOVCS_API ALibretroManagerActor : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ALibretroManagerActor();

	bool SetupLayer(LayerInfo* pLayer, char* pActorName, int layerWidth, int layerHeight, int layerID);
	USynthComponentRTAudioBuffer* m_pRTAudioBufferComponent = NULL;
	void SetSampleRate(int sampleRate);
	void ScaleLayersXY(float scaleMod);
	void SetScaleLayersXY(float scaleX, float scaleY);
	void SetLayersPosXY(float posX, float posY);
	void InitLayers();
	int GetActiveLayerIDByDistanceMod(float mod); //returns -1 for none
	bool GetTextureSmoothingToUse() { return m_setTextureSmoothing; }

	void SetTextureSmoothingToUse(bool bfilteringOn);
	int GetUnusedLayerID(); //returns -1 for none
	int GetLayerCount() { return m_layerCount; }
	void SetUserDepthScale(float scale, bool bShowStatus = true); //clamps, re-spreads the live layers; bShowStatus=false for per-tick ramps (no text in GIF frames)
	void Toggle2D3D(); //'\' hotkey: 0% depth <-> the depth you were at (all systems)
	void ApplyLayerDepth(); //reposition existing layer actors to the current depth spread (no respawn)
	bool PushHoloViewParams(); //push depth/conv to the 3DS multiview core; warns ONCE if the export is missing (stale DLL)
	void SetUserZoom(float factor, bool bShowStatus = true); //user zoom as a persistent framing factor (= and - hotkeys, holo.Zoom)
	void RepositionBottomScreen(); //3DS: deterministic bottom-screen placement; idempotent, re-run from every depth/zoom apply
	//Landscape Looking Glass panels (the original 8.9" etc): stacking the two 3DS screens
	//vertically wastes a wide panel, so landscape shows ONE screen at a time - the 3D top
	//screen by default, and the B key / bare left trigger / holo.BottomScreen swap the
	//bottom screen in (and back).  Portrait panels and the flat build keep the stacked
	//layout and the toggle just explains itself.  -lkglandscape forces the layout on for
	//testing without a device.
	bool IsLandscape3DSLayout();
	void ToggleBottomScreenFocus();
	bool m_bBottomFocus3DS = false; //landscape only; SetEmulatorData resets it to the 3D screen on every game switch
	//3DS debug visualizations (Shift+number hotkeys, holo.Viz / holo.Cutaway console twins).
	//The mask + cutaway plane travel to the core through the optional retro_holo_set_debug
	//export; see cores/holo_abi/holo_layer_abi.h for the HOLO_VIZ_* bits.
	void ApplyHoloViz(); //push the current mask + cutaway to the core (core dedupes repeats)
	//3DS: refuse a change that would need a core re-render to show while the emulator is
	//paused (the core only renders inside retro_run; the savestate-pin refresh was cut as
	//bad UI).  Shows "Can't change that while paused" and returns true when refused.
	bool RefusePausedHoloChange(bool bMode2Only);
	void ToggleHoloViz(uint32 flag, const char* pName); //xor one bit, status text, push
	void ClearHoloViz(); //Shift+0: all debug views off, cutaway reset
	void NudgeCutaway(float delta); //';' and ''' in 3DS multiview: slide the cutaway plane
	void SetLayersPeeled(int count); //debug: hide the N nearest layers (';' and ''' hotkeys) to see the back
	int GetLayersPeeled() { return m_layersPeeled; }
	//3DS multiview (mode 2): (re)create the quilt carrier quad at m_layerInfo[count+1] -
	//a layer-like quad parked AT the focal plane whose texture holds the packed per-view
	//quilt; the LKG sprite path (tag "HoloQuilt") blits one view per lens tile from it.
	//Returns null on failure.  Lazy because the quilt dims arrive with the first delivery.
	LayerInfo* EnsureQuiltCarrier(int quiltW, int quiltH, int viewCount, int cols, int rows);
	//Toggle the carrier's draw without destroying it (quilt dormant on flat/2D screens).
	void SetQuiltCarrierActive(bool bActive);
	bool m_bQuiltCarrierActive = false;
	//Fly-cam support for the LKG build: the fly camera drives the hologram capture actor directly.
	//All three are no-ops / return false when no capture actor exists (flat build).
	void SetLKGCaptureFlyTransform(const FVector& pos, const FRotator& rot);
	bool GetLKGCaptureTransform(FVector& pos, FRotator& rot);
	void RefitLKGCapture(); //zero the capture's rotation and re-run the layer fit (fly-cam exit)
	//Fly-cam magnifier (d-pad up/down, holo.FlyZoom): shrinking the capture's Size pulls the
	//camera in along its own axis while the capture ACTOR - which IS the focal plane center,
	//the spring arm holds the camera behind it - stays put, so the magnified image stays SHARP
	//on the panel.  Flying closer instead moves content off the focal plane and it goes fuzzy.
	//Deliberately cheap: no refit, no logging, safe to call every frame from a held d-pad.
	void SetFlyZoom(float factor, bool bShowStatus = true);
	int m_layersPeeled = 0; //how many of the NEAREST layers are hidden; survives rom switches

	int m_layerCount = 5;
	float m_total3dDepth = 150;
	float m_userDepthScale = 1.0f; //user multiplier on m_total3dDepth ([ and ] hotkeys); SetEmulatorData resets it to the per-system default on every game switch
	float m_stashed3DDepth = 0.0f; //what the '\' 2D/3D toggle restores (the depth before it zeroed)
	//'{' and '}' hotkeys: nudge the multiview convergence (m_userConv01 below) - the
	//depth knob's true sibling: it moves the zero-parallax plane instead of scaling the
	//spread, so it changes how much of the scene POPS OUT vs sinks behind the screen.
	void NudgeConvergence(float delta);
	//3DS multiview zero-parallax plane as a fraction of the scene depth range (0 = nearest
	//content at the screen plane, 1 = farthest).  -1 keeps the core default (0.35).  Pushed
	//through retro_holo_set_view_params by ApplyLayerDepth; holo.Convergence sets it live.
	float m_userConv01 = -1.0f;
	//user zoom from the = and - keys, applied as a framing crop INSIDE the camera/capture
	//fits (an actor-scale zoom got normalized right back out by the AABB-driven fits, which
	//is why zoom appeared to reset on every depth press, rebuild, or R).  Resets to 1 on
	//every game switch like m_userDepthScale.
	float m_userZoomFactor = 1.0f;
	//Fly-cam-only magnifier, composed on top of m_userZoomFactor by the capture fit.  Reset to
	//1 whenever fly mode is entered or left, so an 8x inspection zoom can never leak into play.
	float m_flyZoomFactor = 1.0f;
	//The fitted capture Size BEFORE either zoom divides it, cached by
	//FitLookingGlassCaptureToLayers so SetFlyZoom can re-derive the size without a whole refit.
	float m_lastFitCaptureBaseSize = 0.0f;
	//3DS debug visualization state (HOLO_VIZ_* mask + cutaway plane); session-sticky within
	//3DS, cleared by SetEmulatorData when switching to another system
	uint32 m_holoVizFlags = 0;
	float m_cutaway01 = 0.0f;
	//ApplyHoloViz change-gated logging (the 1Hz self-heal re-pushes the same mask)
	uint32 m_lastAppliedVizMask = 0xFFFFFFFF;
	float m_lastAppliedCutaway = -1.0f;
	float m_depthOffsetForAllLayers = 0;
	bool m_setTextureSmoothing = false;
	int m_layerWidth = 256;
	int m_layerHeight = 256;
	FVector2D m_coreLayerScale = FVector2D(4.46, 2.965);
	FVector2D m_corePosition = FVector2D(0, 0);
	FVector m_bg_color = FVector(0, 0, 0);
	float m_bg_color_strength = 1;
	bool m_bgAllowShadows = true;


protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	void CleanupLayerMemory();
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;
	LayerInfo* GetLayer(int index) { 
		if (index < m_layerInfo.size())
		{
			return &m_layerInfo[index];
		}
		else
		{
			return NULL;
		}
	}
	LibretroManager m_libretroManager;
	std::vector<LayerInfo> m_layerInfo;
	
	//UPROPERTY(EditAnywhere)
//	AHoloPlayCapture *m_pHoloPlayCapture = NULL;
	
	UPROPERTY(EditAnywhere, Category = "Things to spawn")
		TSubclassOf<AActor> m_layerTemplate;
	UPROPERTY(EditAnywhere)
		AActor* m_pLight = NULL;

	UPROPERTY(EditAnywhere)
		UMaterial *LayerMatNormal;
	
	UPROPERTY(EditAnywhere)
		UMaterial* LayerMatNoLighting;

	int m_framesRendered = 0;
	float m_timeOfNextFPSUpdate = 0;
	//PushHoloViewParams bookkeeping: change-gated logging (ApplyLayerDepth runs per tick
	//during holo.DepthRamp) and the one-shot stale-DLL warning
	float m_lastPushedSep = -999.0f;
	float m_lastPushedConv = -999.0f;
	bool m_bWarnedNoViewParamExport = false;
	bool m_bShowLKGFPS = false; //fps readout on the in-world status text, on for Looking Glass builds
	eLightingMode m_curLightingMode = LIGHTING_MODE_NORMAL;

protected:
	
};


