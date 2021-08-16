//  Programmer(s):  Seth A. Robinson (seth@rtsoft.com)

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include <string>
using namespace std;

#include "StatusDisplayActor.generated.h"

class UTextRenderComponent;

void ShowStatusMessage(string message, float forcedTimeToShow = 0);

UCLASS()
class HOLOVCS_API AStatusDisplayActor : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AStatusDisplayActor();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:

	// Called every frame
	virtual void Tick(float DeltaTime) override;
	void SetTextTimer(double timer);
	double m_viewTimer = 0;
	UTextRenderComponent* m_pTextComponent = NULL;
};
