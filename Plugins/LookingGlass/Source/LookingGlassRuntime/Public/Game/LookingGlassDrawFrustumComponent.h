// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"

#include "LookingGlassDrawFrustumComponent.generated.h"

class FPrimitiveSceneProxy;
class UTexture;


/**
 * @class	ULookingGlassDrawFrustumComponent
 *
 * @brief	Utility component for drawing a view frustum. Origin is at the component location,
 * 			frustum points down position X axis.
 */

UCLASS()
class ULookingGlassDrawFrustumComponent : public UPrimitiveComponent
{
	GENERATED_UCLASS_BODY()

	/** Distance to LookingGlass Draw Plane. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DrawFrustumComponent)
	float FrustumMidDist;

	/** Color to draw the wireframe frustum. */
	UPROPERTY(EditAnywhere, Category = DrawFrustumComponent)
	FColor FrustumColor;

	/** Line Thickness for Frustum. */
	UPROPERTY(EditAnywhere, Category = DrawFrustumComponent)
	float FrustumLineThickness;

	/** Color to draw the LookingGlassPlane. */
	UPROPERTY(EditAnywhere, Category = DrawFrustumComponent)
	FColor MidFrustumColor;

	/** Line Length for Mid Plane. */
	UPROPERTY(EditAnywhere, Category = DrawFrustumComponent)
	float MidPlaneLineLength;

	/** Line Thickness for Mid Plane. */
	UPROPERTY(EditAnywhere, Category = DrawFrustumComponent)
	float MidLineThickness;

	/** Angle of longest dimension of view shape.
	  * If the angle is 0 then an orthographic projection is used */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DrawFrustumComponent)
	float FrustumAngle;

	/** Ratio of horizontal size over vertical size. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DrawFrustumComponent)
	float FrustumAspectRatio;

	/** Distance from origin to start drawing the frustum. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DrawFrustumComponent)
	float FrustumStartDist;

	/** Distance from origin to stop drawing the frustum. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DrawFrustumComponent)
	float FrustumEndDist;

	/** Vertical Angle for Vertical Offset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DrawFrustumComponent)
	float VerticalAngle;

	//~ Begin UPrimitiveComponent Interface.
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ End UPrimitiveComponent Interface.
};
