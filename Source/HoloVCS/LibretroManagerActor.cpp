// Fill out your copyright notice in the Description page of Project Settings.
#include "LibretroManagerActor.h"
#include "AudioDevice.h"

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

 
bool ALibretroManagerActor::SetupLayer(LayerInfo* pLayer, char* pActorName)
{
	//hardcoded to work with the Stella 2600 emulator, but in theory with some tweaks we could support any
	//libretro core

	pLayer->m_texWidth = 256;
	pLayer->m_texHeight = 256;

	pLayer->m_texPitchBytes = pLayer->m_texWidth * 4; //we're assuming XRGBA
	LogMsg("Creating texture %d X %d", pLayer->m_texWidth, pLayer->m_texHeight);

	pLayer->mDataSize = pLayer->m_texWidth * pLayer->m_texHeight * 4;

	SAFE_DELETE_ARRAY(pLayer->m_pTextData);
	pLayer->m_pTextData = new uint8[pLayer->mDataSize];
	memset(pLayer->m_pTextData, 0, pLayer->mDataSize); //make it transparent

	if (pLayer->mUpdateTextureRegion) delete pLayer->mUpdateTextureRegion;
	pLayer->mUpdateTextureRegion = new FUpdateTextureRegion2D(0, 0, 0, 0, pLayer->m_texWidth, pLayer->m_texHeight);

	auto pActor = GetActorByTag(GetWorld(), pActorName);
	if (pActor)
	{
		LogMsg("Found %s", pActorName);
	}
	else
	{
		return false;
	}

	UMeshComponent* pComp1 = (UMeshComponent*)pActor->GetComponentByClass(UMeshComponent::StaticClass());
	if (pComp1)
	{
	
		// create dynamic texture
		pLayer->m_pDynamicTexture = UTexture2D::CreateTransient(pLayer->m_texWidth, pLayer->m_texHeight, TEX_PIXEL_FORMAT);
		
		pLayer->m_pDynamicTexture->SRGB = 0;
		pLayer->m_pDynamicTexture->CompressionSettings = TextureCompressionSettings::TC_EditorIcon;
		pLayer->m_pDynamicTexture->AddToRoot();
		
		//More pixel accurate with these enabled, but I kind of miss the black outlines we get without them
		//pLayer->m_pDynamicTexture->Filter = TextureFilter::TF_Nearest;
		//pLayer->m_pDynamicTexture->LODGroup = TEXTUREGROUP_Pixels2D;

		pLayer->m_pDynamicTexture->UpdateResource();
		pLayer->m_pDynamicTexture->RefreshSamplerStates();
		pLayer->pUMatDyn = pComp1->CreateDynamicMaterialInstance(0, 0, "MatDyn");
		pLayer->pUMatDyn->SetTextureParameterValue(TEXT("Texture"), pLayer->m_pDynamicTexture);
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
		actors[i]->SetActorScale3D(vScale);
	}

}

void ALibretroManagerActor::SetLayersPosXY(float posX, float posY)
{
	//Good thing we've previously marked all things we want to scale with a tag called "Scalable"
	TArray<AActor*> actors;
	AddActorsByTag(&actors, GetWorld(), "Layers");
	check(actors.Num() == 1);

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

	
	//Prepare each layer we're going to dynamically write visuals to
	for (int i = 0; i < C_LAYER_COUNT; i++)
	{
		if (!SetupLayer(&m_layerInfo[i], (char*)(string("Layer") + toString(i)).c_str()))
		{
			LogMsg("Error setting up layer");
		}
	}


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


#if UE_BUILD_DEBUG
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
 
void ALibretroManagerActor::KillStuff()
{
	for (int i = 0; i < C_LAYER_COUNT; i++)
	{
		if (m_layerInfo[i].m_pDynamicTexture)
		{
			m_layerInfo[i].m_pDynamicTexture->ReleaseResource();
			m_layerInfo[i].m_pDynamicTexture = 0;
			SAFE_DELETE_ARRAY(m_layerInfo[i].m_pTextData);
		}
	}  
}

void ALibretroManagerActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	LogMsg("Ending play...");
	m_libretroManager.Kill();
	KillStuff();
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

	for (int i = 0; i < C_LAYER_COUNT; i++)
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

