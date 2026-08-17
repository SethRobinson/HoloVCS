// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LookingGlassLibrary.generated.h"

class ULookingGlassSettings;

/**
 * 
 */
UCLASS()

/**
 * @class	ULookingGlassLibrary
 *
 * @brief	LookingGlass blueprint libaray
 * 			For static access to davice data from any blueprins
 */

class LOOKINGGLASSRUNTIME_API ULookingGlassLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, BlueprintCallable, meta = (DisplayName = "Get LookingGlass settings"), Category = "LookingGlass")

	/**
	 * @fn	static ULookingGlassSettings* ULookingGlassLibrary::GetLookingGlassSettings();
	 *
	 * @brief	Gets all LookingGlass runtime settings  
	 *
	 * @returns	Null if it fails, else the LookingGlass settings.
	 */

	static ULookingGlassSettings* GetLookingGlassSettings();

};

