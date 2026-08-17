/*=============================================================================
	LookingGlassDrawFrsutumComponent.cpp: ULookingGlassDrawFrsutumComponent implementation.
=============================================================================*/

#include "Game/LookingGlassDrawFrustumComponent.h"

#include "Misc/LookingGlassLog.h"

#include "PrimitiveViewRelevance.h"
#include "PrimitiveSceneProxy.h"
#include "Engine/CollisionProfile.h"
#include "SceneManagement.h"


/** Represents a draw frustum to the scene manager. */
class FLookingGlassDrawFrustumSceneProxy final : public FPrimitiveSceneProxy
{
public:
	SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}

	/**
	* Initialization constructor.
	* @param	InComponent - game component to draw in the scene
	*/
	FLookingGlassDrawFrustumSceneProxy(const ULookingGlassDrawFrustumComponent* InComponent)
		: FPrimitiveSceneProxy(InComponent)
		, FrustumColor(InComponent->FrustumColor)
		, MidFrustumColor(InComponent->MidFrustumColor)
		, MidPlaneLineLength(InComponent->MidPlaneLineLength)
		, FrustumLineThickness(InComponent->FrustumLineThickness)
		, MidLineThickness(InComponent->MidLineThickness)
		, FrustumAngle(InComponent->FrustumAngle)
		, FrustumAspectRatio(InComponent->FrustumAspectRatio)
		, FrustumStartDist(InComponent->FrustumStartDist)
		, FrustumEndDist(InComponent->FrustumEndDist)
		, FrustumMidDist(InComponent->FrustumMidDist)
		, VerticalAngle(InComponent->VerticalAngle)
	{
		bWillEverBeLit = false;
	}

	// FPrimitiveSceneProxy interface.

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_DrawFrustumSceneProxy_DrawDynamicElements);

		FVector Direction(1, 0, 0);
		FVector LeftVector(0, 1, 0);
		FVector UpVector(0, 0, 1);

		FVector Verts[20];

		float HozLength = 0.0f;
		float VertLength = 0.0f;

		const float HozHalfAngleInRadians = FMath::DegreesToRadians(FrustumAngle * 0.5f);
		const float CenterHozLength = FrustumMidDist * FMath::Tan(HozHalfAngleInRadians);
		const float CenterVertLength = CenterHozLength / FrustumAspectRatio;

		const float VerticalAngleRadians = FMath::DegreesToRadians(VerticalAngle);
		const float VertOffset = (FrustumMidDist * FMath::Tan(VerticalAngleRadians)) / FrustumAspectRatio;

		const float NearFactor_Loc = 1.0f - (FrustumStartDist / FrustumMidDist);
		const float FarFactor_Loc = (FrustumEndDist / FrustumMidDist) - 1.0f;

		const float LineLength = 1.0f - MidPlaneLineLength;


		if (FrustumAngle > 0.0f)
		{
			HozLength = FrustumStartDist * FMath::Tan(HozHalfAngleInRadians);
			VertLength = HozLength / FrustumAspectRatio;
		}
		else
		{
			const float OrthoWidth = (FrustumAngle == 0.0f) ? 1000.0f : -FrustumAngle;
			HozLength = OrthoWidth * 0.5f;
			VertLength = HozLength / FrustumAspectRatio;
		}


		// near plane verts													   // Vertical angle offset
		Verts[0] = (Direction * FrustumStartDist) + ((UpVector * VertLength) + (UpVector * VertOffset * NearFactor_Loc)) + (LeftVector * HozLength);
		Verts[1] = (Direction * FrustumStartDist) + ((UpVector * VertLength) + (UpVector * VertOffset * NearFactor_Loc)) - (LeftVector * HozLength);
		Verts[2] = (Direction * FrustumStartDist) - ((UpVector * VertLength) - (UpVector * VertOffset * NearFactor_Loc)) - (LeftVector * HozLength);
		Verts[3] = (Direction * FrustumStartDist) - ((UpVector * VertLength) - (UpVector * VertOffset * NearFactor_Loc)) + (LeftVector * HozLength);

		if (FrustumAngle > 0.0f)
		{
			HozLength = FrustumEndDist * FMath::Tan(HozHalfAngleInRadians);
			VertLength = HozLength / FrustumAspectRatio;
		}

		// far plane verts													  // Vertical angle offset
		Verts[4] = (Direction * FrustumEndDist) + ((UpVector * VertLength) - (UpVector * VertOffset * FarFactor_Loc)) + (LeftVector * HozLength);
		Verts[5] = (Direction * FrustumEndDist) + ((UpVector * VertLength) - (UpVector * VertOffset * FarFactor_Loc)) - (LeftVector * HozLength);
		Verts[6] = (Direction * FrustumEndDist) - ((UpVector * VertLength) + (UpVector * VertOffset * FarFactor_Loc)) - (LeftVector * HozLength);
		Verts[7] = (Direction * FrustumEndDist) - ((UpVector * VertLength) + (UpVector * VertOffset * FarFactor_Loc)) + (LeftVector * HozLength);

		// mid plane verts
		Verts[8] =  (Direction * FrustumMidDist) + (UpVector * CenterVertLength) +				(LeftVector * CenterHozLength);
		Verts[9] =  (Direction * FrustumMidDist) + (UpVector * CenterVertLength) -				(LeftVector * CenterHozLength);
		Verts[10] = (Direction * FrustumMidDist) - (UpVector * CenterVertLength) -				(LeftVector * CenterHozLength);
		Verts[11] = (Direction * FrustumMidDist) - (UpVector * CenterVertLength) +				(LeftVector * CenterHozLength);

		Verts[12] = (Direction * FrustumMidDist) + (UpVector * CenterVertLength) +				(LeftVector * CenterHozLength * LineLength);
		Verts[13] = (Direction * FrustumMidDist) + (UpVector * CenterVertLength * LineLength) + (LeftVector * CenterHozLength);

		Verts[14] = (Direction * FrustumMidDist) + (UpVector * CenterVertLength) -				(LeftVector * CenterHozLength * LineLength);
		Verts[15] = (Direction * FrustumMidDist) + (UpVector * CenterVertLength * LineLength) - (LeftVector * CenterHozLength);

		Verts[16] = (Direction * FrustumMidDist) - (UpVector * CenterVertLength) -				(LeftVector * CenterHozLength * LineLength);
		Verts[17] = (Direction * FrustumMidDist) - (UpVector * CenterVertLength * LineLength) - (LeftVector * CenterHozLength);

		Verts[18] = (Direction * FrustumMidDist) - (UpVector * CenterVertLength) +				(LeftVector * CenterHozLength * LineLength);
		Verts[19] = (Direction * FrustumMidDist) - (UpVector * CenterVertLength * LineLength) + (LeftVector * CenterHozLength);

		for (int32 X = 0; X < 20; ++X)
		{
			Verts[X] = GetLocalToWorld().TransformPosition(Verts[X]);
		}

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (VisibilityMap & (1 << ViewIndex))
			{
				FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);
				const FSceneView* View = Views[ViewIndex];

				const uint8 DepthPriorityGroup = GetDepthPriorityGroup(View);
				PDI->DrawLine(Verts[0], Verts[1], FrustumColor, DepthPriorityGroup, FrustumLineThickness);
				PDI->DrawLine(Verts[1], Verts[2], FrustumColor, DepthPriorityGroup, FrustumLineThickness);
				PDI->DrawLine(Verts[2], Verts[3], FrustumColor, DepthPriorityGroup, FrustumLineThickness);
				PDI->DrawLine(Verts[3], Verts[0], FrustumColor, DepthPriorityGroup, FrustumLineThickness);

				PDI->DrawLine(Verts[4], Verts[5], FrustumColor, DepthPriorityGroup, FrustumLineThickness);
				PDI->DrawLine(Verts[5], Verts[6], FrustumColor, DepthPriorityGroup, FrustumLineThickness);
				PDI->DrawLine(Verts[6], Verts[7], FrustumColor, DepthPriorityGroup, FrustumLineThickness);
				PDI->DrawLine(Verts[7], Verts[4], FrustumColor, DepthPriorityGroup, FrustumLineThickness);

				PDI->DrawLine(Verts[0], Verts[4], FrustumColor, DepthPriorityGroup, FrustumLineThickness);
				PDI->DrawLine(Verts[1], Verts[5], FrustumColor, DepthPriorityGroup, FrustumLineThickness);
				PDI->DrawLine(Verts[2], Verts[6], FrustumColor, DepthPriorityGroup, FrustumLineThickness);
				PDI->DrawLine(Verts[3], Verts[7], FrustumColor, DepthPriorityGroup, FrustumLineThickness);

				PDI->DrawLine(Verts[8], Verts[12], MidFrustumColor, DepthPriorityGroup, MidLineThickness);
				PDI->DrawLine(Verts[8], Verts[13], MidFrustumColor, DepthPriorityGroup, MidLineThickness);
				PDI->DrawLine(Verts[9], Verts[14], MidFrustumColor, DepthPriorityGroup, MidLineThickness);
				PDI->DrawLine(Verts[9], Verts[15], MidFrustumColor, DepthPriorityGroup, MidLineThickness);
				PDI->DrawLine(Verts[10], Verts[16], MidFrustumColor, DepthPriorityGroup, MidLineThickness);
				PDI->DrawLine(Verts[10], Verts[17], MidFrustumColor, DepthPriorityGroup, MidLineThickness);
				PDI->DrawLine(Verts[11], Verts[18], MidFrustumColor, DepthPriorityGroup, MidLineThickness);
				PDI->DrawLine(Verts[11], Verts[19], MidFrustumColor, DepthPriorityGroup, MidLineThickness);
			}
		}
	}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
		FPrimitiveViewRelevance Result;
		Result.bDrawRelevance = IsShown(View) && true;
		Result.bDynamicRelevance = true;
		Result.bShadowRelevance = IsShadowCast(View);
		Result.bEditorPrimitiveRelevance = UseEditorCompositing(View);
		return Result;
	}

	virtual uint32 GetMemoryFootprint(void) const override { return(sizeof(*this) + GetAllocatedSize()); }
	uint32 GetAllocatedSize(void) const { return(FPrimitiveSceneProxy::GetAllocatedSize()); }

private:
	FColor FrustumColor;
	FColor MidFrustumColor;
	float MidPlaneLineLength;
	float FrustumLineThickness;
	float MidLineThickness;
	float FrustumAngle;
	float FrustumAspectRatio;
	float FrustumStartDist;
	float FrustumEndDist;
	float FrustumMidDist;
	float VerticalAngle;
	float NearFactor;
	float FarFactor;
};

ULookingGlassDrawFrustumComponent::ULookingGlassDrawFrustumComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Set Defaults
	FrustumColor = FColor(255, 0, 255, 255);
	MidFrustumColor = FColor(255, 255, 255, 255);

	FrustumAngle = .0f;
	FrustumAspectRatio = .0f;
	FrustumStartDist = .0f;
	FrustumEndDist = .0f;
	FrustumMidDist = .0f;
	VerticalAngle = .0f;

	bUseEditorCompositing = true;
	bHiddenInGame = true;
	SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	SetGenerateOverlapEvents(false);
}

FPrimitiveSceneProxy* ULookingGlassDrawFrustumComponent::CreateSceneProxy()
{
	return new FLookingGlassDrawFrustumSceneProxy(this);
}


FBoxSphereBounds ULookingGlassDrawFrustumComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	return FBoxSphereBounds(LocalToWorld.TransformPosition(FVector::ZeroVector), FVector(FrustumEndDist), FrustumEndDist);
}
