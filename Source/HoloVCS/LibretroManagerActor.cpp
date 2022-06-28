// Fill out your copyright notice in the Description page of Project Settings.
#include "LibretroManagerActor.h"
#include "AudioDevice.h"
#include "PlayerPawn.h"
#include "Components/PointLightComponent.h"

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

 
bool ALibretroManagerActor::SetupLayer(LayerInfo* pLayer, char* pActorName, int layerWidth, int layerHeight)
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
	}
	else
	{
		return false;
	}
	
	UMeshComponent* pComp1 = (UMeshComponent*)pActor->GetComponentByClass(UMeshComponent::StaticClass());
	if (pComp1)
	{
		switch (m_curLightingMode)
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

	float step = m_total3dDepth / (float)GetLayerCount();
	float startingZ =  (step * (GetLayerCount()/2));

	//Prepare each layer we're going to dynamically write visuals to
	for (int i = 0; i < m_layerCount; i++)
	{
		if (!SetupLayer(&m_layerInfo[i], (char*)(string("Layer") + toString(i)).c_str(), m_layerWidth, m_layerHeight))
		{
			LogMsg("Error setting up layer");
		}
		
		m_layerInfo[i].SetLayerPosZ(startingZ+ (-m_depthOffsetForAllLayers));
		startingZ -= step;
	}

	auto pLight = g_pLibretroManager->m_pLibretroManagedActor->m_pLight->FindComponentByClass<UPointLightComponent>();

	if (m_curLightingMode == LIGHTING_MODE_NONE)
	{
		pLight->SetVisibility(false);
	}
	else
	{
		pLight->SetVisibility(true);
	}

	m_libretroManager.m_pPlayerPawn->SetTintBG(m_bg_color, m_bg_color_strength, m_bgAllowShadows);


	SetScaleLayersXY(m_coreLayerScale.X, m_coreLayerScale.Y);
	SetLayersPosXY(m_corePosition.X, m_corePosition.Y);
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

	FAudioDeviceHandle AudioDevice = GEngine->GetMainAudioDevice();
	FAudioDevice* MainAudioDevice = GEngine->GetMainAudioDeviceRaw();
	LogMsg("Main audio device sample rate is %f", MainAudioDevice->GetSampleRate());

	if (m_pRTAudioBufferComponent == NULL)
	{
		m_pRTAudioBufferComponent = NewObject<USynthComponentRTAudioBuffer>(this, USynthComponentRTAudioBuffer::StaticClass());
		m_pRTAudioBufferComponent->Initialize();
		m_pRTAudioBufferComponent->Start(); //Note, this requires Seth's bugfixed unreal source which can't be legally shared to be able to change sample rate on the fly.
		//as a work around, you have to change the windows target overall mixing framerate.  See the USynthComponentRTAudioBuffer source for more info
	}

	LogMsg("Started audio renderer thread");

	InitLayers();
	 
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

	//internal FPS counter for some reason, not really needed
	m_framesRendered = 0;
	m_timeOfNextFPSUpdate = GetWorld()->GetRealTimeSeconds() + 1.0f;

	OnWasRestartedInEditor();
	m_libretroManager.Init(this);

	if (!m_libretroManager.IsCoreLoaded())
	{
		return;
	}

#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
	//Kill splash screen
	TArray<AActor*> actors;
	AddActorsByTag(&actors, GetWorld(), "SplashScreen");
	
	if (actors.Num() > 0)
	{
		actors[0]->Destroy();
	}

#endif

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
			m_pRTAudioBufferComponent->Start(sampleRate);
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

	if (m_timeOfNextFPSUpdate < GetWorld()->GetRealTimeSeconds())
	{
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

	for (int i = 0; i < m_layerInfo.size(); i++)
	{
		if (m_layerInfo[i].GetPixelBuffer())
		{
		
			if (C_INIT_TEXTURES_EVERY_FRAME)
			{
				FUpdateTextureRegion2D* pRegionTemp = new FUpdateTextureRegion2D(0, 0, 0, 0, m_layerInfo[i].m_texWidth, m_layerInfo[i].m_texHeight);

				uint8* pTexTemp = new uint8[m_layerInfo[i].mDataSize];
				memcpy(pTexTemp, m_layerInfo[i].m_pTextData, m_layerInfo[i].mDataSize);

				m_layerInfo[i].m_pDynamicTexture->UpdateTextureRegions(0, 1, pRegionTemp, m_layerInfo[i].m_texWidth * 4, 4, (uint8*)pTexTemp,
					[](auto pTexTemp, auto pRegionTemp)
					{
						delete pTexTemp;
						delete pRegionTemp;
					});
			}
			else
			{
				//simple way
				m_layerInfo[i].m_pDynamicTexture->UpdateTextureRegions(0, 1, m_layerInfo[i].mUpdateTextureRegion, m_layerInfo[i].m_texWidth * 4, 4, m_layerInfo[i].m_pTextData);
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
