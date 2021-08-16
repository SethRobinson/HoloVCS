// Fill out your copyright notice in the Description page of Project Settings.
#include "StatusDisplayActor.h"
#include "Shared/UnrealMisc.h"
#include "Components/TextRenderComponent.h"

// Sets default values
AStatusDisplayActor::AStatusDisplayActor()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	m_pTextComponent = (UTextRenderComponent*)GetComponentByClass(UTextRenderComponent::StaticClass());
	if (m_pTextComponent)
	{
		this->SetActorHiddenInGame(true);
	}
	else
	{
		LogMsg("Can't find status text component, maybe checking in the constructor is bad?");
	}
}

// Called when the game starts or when spawned
void AStatusDisplayActor::BeginPlay()
{
	Super::BeginPlay(); 
}

// Called every frame
void AStatusDisplayActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (m_viewTimer != 0)
	{
		if (m_viewTimer < FPlatformTime::Seconds())
		{
			m_viewTimer = 0;
			this->SetActorHiddenInGame(true);
		}
	}
}

void AStatusDisplayActor::SetTextTimer(double timer)
{
	m_viewTimer = FPlatformTime::Seconds() + timer;
	this->SetActorHiddenInGame(false);
}

//An easy way to show an occasional status message from anywhere.  (assuming it's called in a main thread)

void ShowStatusMessage(string message, float forcedTimeToShow)
{
	AStatusDisplayActor* pActor = (AStatusDisplayActor * ) GetActorByTag(GEngine->GetCurrentPlayWorld(), "StatusDisplayActor");

	if (!pActor)
	{
		LogMsg("Can't find an actor with name StatusDisplayActor! So much for showing in-game status messages");
		return;
	}
	
	UTextRenderComponent *pComp = (UTextRenderComponent*)pActor->GetComponentByClass(UTextRenderComponent::StaticClass());
	pComp->SetText(message.c_str());
	if (forcedTimeToShow == 0)
	{
		forcedTimeToShow = 2;
	}
	pActor->SetTextTimer(forcedTimeToShow);
}
