

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "MyGameInstance.generated.h"

/**
 * 
 */
UCLASS()
class HOLOVCS_API UMyGameInstance : public UGameInstance
{
	GENERATED_BODY()

	
public:
	virtual void Init() override;
};
