//  Programmer(s):  Seth A. Robinson (seth@rtsoft.com)

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Shared/UnrealMisc.h"
#include "LibretroManager.h"
#include "Engine/TextRenderActor.h"
#include "Components/TextRenderComponent.h"
#include "RTAudioBUffer.h"
#include "HoloVCS.h"
#include "Game/HoloPlayCapture.h"
#include "LibretroManagerActor.generated.h"

void OnWasRestartedInEditor();

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

	bool SetupLayer(LayerInfo* pLayer, char* pActorName, int layerWidth, int layerHeight);
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
	
	int m_layerCount = 5;
	float m_total3dDepth = 150;
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
	
	UPROPERTY(EditAnywhere)
	AHoloPlayCapture *m_pHoloPlayCapture = NULL;
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
	eLightingMode m_curLightingMode = LIGHTING_MODE_NORMAL;

protected:
	
};


