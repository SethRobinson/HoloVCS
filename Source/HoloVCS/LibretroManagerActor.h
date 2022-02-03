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

class LayerInfo
{
public:

	~LayerInfo()
	{
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
	uint8* GetPixelBuffer() { return m_pTextData; };
};

const int C_LAYER_COUNT = 5;

UCLASS()
class HOLOVCS_API ALibretroManagerActor : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ALibretroManagerActor();

	bool SetupLayer(LayerInfo* pLayer, char* pActorName);
	USynthComponentRTAudioBuffer* m_pRTAudioBufferComponent = NULL;
	void SetSampleRate(int sampleRate);
	void KillStuff();
	void ScaleLayersXY(float scaleMod);
	void SetScaleLayersXY(float scaleX, float scaleY);
	void SetLayersPosXY(float posX, float posY);

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	
public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;
	LayerInfo* GetLayer(int index) { return &m_layerInfo[index]; }
	LibretroManager m_libretroManager;
	LayerInfo m_layerInfo[C_LAYER_COUNT];
	
	UPROPERTY(EditAnywhere)
	AHoloPlayCapture *m_pHoloPlayCapture = NULL;
	int m_framesRendered = 0;
	float m_timeOfNextFPSUpdate = 0;

protected:
	
};
