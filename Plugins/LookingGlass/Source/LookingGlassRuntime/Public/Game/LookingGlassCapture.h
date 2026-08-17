// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "LookingGlassSettings.h"

#include "LookingGlassCapture.generated.h"

class USpringArmComponent;
class ULookingGlassSceneCaptureComponent2D;


UCLASS(hidecategories = ("Actor", "LOD", "Cooking", "Rendering", "Replication", "Input"), meta = (AllowPrivateAccess = "true"))

class LOOKINGGLASSRUNTIME_API ALookingGlassCapture : public AActor
{
	GENERATED_BODY()
	
public:	
	ALookingGlassCapture();

#if WITH_EDITOR
	void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	virtual void Serialize(FArchive& Ar) override;

protected:

#if WITH_EDITORONLY_DATA
	// Legacy properties, moved to ULookingGlassSceneCaptureComponent2D. Here for backwards compatibility.

	UPROPERTY()
	float Size_DEPRECATED = 150.0f;

	UPROPERTY()
	float NearClipFactor_DEPRECATED = 1.0f;

	UPROPERTY()
	bool bUseFarClipPlane_DEPRECATED = false;

	UPROPERTY()
	float FarClipFactor_DEPRECATED = 1.5f;

	UPROPERTY()
	float FOV_DEPRECATED = 14.0f;

	UPROPERTY()
	UTexture2D* OverrideQuiltTexture2D_DEPRECATED = nullptr;

	UPROPERTY()
	bool bSingleViewMode_DEPRECATED = false;

	UPROPERTY()
	ELookingGlassQualitySettings TilingQuality_DEPRECATED = ELookingGlassQualitySettings::Q_Automatic;

	UPROPERTY()
	FLookingGlassTilingQuality TilingValues_DEPRECATED;
	
	void LoadLegacyProperties();
#endif // WITH_EDITORONLY_DATA

private:
	// Components declarations. Declared "private" and with "AllowPrivateAccess", so their properties are directly exposed
	// to actor's property editor.

	// Component which controls distance of camera from actor's location. Actor's location matches the camera focus point.
	// Note: we're not using "VisibleAnywhere" meta tag here to hide component's properties in editor
	UPROPERTY(Category = CameraActor, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraSpringArm = nullptr;

	// The primary component in this actor, render camera
	UPROPERTY(Category = CameraActor, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	ULookingGlassSceneCaptureComponent2D* RenderCamera = nullptr;

	void UpdateCameraPosition();
};
