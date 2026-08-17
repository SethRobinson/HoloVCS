// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Game/LookingGlassSceneCaptureComponent2D.h"
#include "Game/LookingGlassCapture.h"

#include "Engine/Engine.h"
#include "EngineModule.h" // for GetRendererModule()

#include "SceneInterface.h"
#include "GameFramework/WorldSettings.h"
#include "Engine/TextureRenderTarget2D.h"
#include "TextureResource.h"
#include "Components/PrimitiveComponent.h"
#include "LegacyScreenPercentageDriver.h"
#include "Rendering/MotionVectorSimulation.h"
#include "GenerateMips.h"
#include "CanvasTypes.h"
#include "RenderGraphBuilder.h"

#include "Runtime/Launch/Resources/Version.h"

// This function is heavily based on SetupViewFamilyForSceneCapture() from SceneCaptureRendering.cpp
static void SetupViewVamilyForSceneCapture(
	FSceneViewFamily& ViewFamily,
	USceneCaptureComponent2D* SceneCaptureComponent,
	const TArrayView<const FSceneCaptureViewInfo> Views,
	float MaxViewDistance,
	bool bCaptureSceneColor,
	bool bIsPlanarReflection,
	FPostProcessSettings* PostProcessSettings,
	float PostProcessBlendWeight,
	const AActor* ViewActor)
{
	check(!ViewFamily.GetScreenPercentageInterface());

	// Ensure that the views for this scene capture reflect any simulated camera motion for this frame
	TOptional<FTransform> PreviousTransform = FMotionVectorSimulation::Get().GetPreviousTransform(SceneCaptureComponent);
	FPlane ClipPlane = FPlane(SceneCaptureComponent->ClipPlaneBase, SceneCaptureComponent->ClipPlaneNormal.GetSafeNormal());

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		const FSceneCaptureViewInfo& SceneCaptureViewInfo = Views[ViewIndex];

		FSceneViewInitOptions ViewInitOptions;
		ViewInitOptions.SetViewRectangle(SceneCaptureViewInfo.ViewRect);
		ViewInitOptions.ViewFamily = &ViewFamily;
		ViewInitOptions.ViewActor = ViewActor;
		ViewInitOptions.ViewOrigin = SceneCaptureViewInfo.ViewLocation;
		ViewInitOptions.ViewRotationMatrix = SceneCaptureViewInfo.ViewRotationMatrix;
		ViewInitOptions.BackgroundColor = FLinearColor::Black;
		ViewInitOptions.OverrideFarClippingPlaneDistance = MaxViewDistance;
		ViewInitOptions.StereoPass = SceneCaptureViewInfo.StereoPass;
		ViewInitOptions.SceneViewStateInterface = SceneCaptureComponent->GetViewState(ViewIndex);
		ViewInitOptions.ProjectionMatrix = SceneCaptureViewInfo.ProjectionMatrix;
		ViewInitOptions.LODDistanceFactor = FMath::Clamp(SceneCaptureComponent->LODDistanceFactor, .01f, 100.0f);

		if (ViewFamily.Scene->GetWorld() != nullptr && ViewFamily.Scene->GetWorld()->GetWorldSettings() != nullptr)
		{
			ViewInitOptions.WorldToMetersScale = ViewFamily.Scene->GetWorld()->GetWorldSettings()->WorldToMeters;
		}
#if ((ENGINE_MAJOR_VERSION < 5) || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1))
		ViewInitOptions.StereoIPD = SceneCaptureViewInfo.StereoIPD * ( ViewInitOptions.WorldToMetersScale / 100.0f );
#endif
		

		if (bCaptureSceneColor)
		{
			ViewFamily.EngineShowFlags.PostProcessing = 0;
			ViewInitOptions.OverlayColor = FLinearColor::Black;
		}

		FSceneView* View = new FSceneView(ViewInitOptions);

		View->bIsSceneCapture = true;
		View->bSceneCaptureUsesRayTracing = SceneCaptureComponent->bUseRayTracingIfEnabled;
		// Note: this has to be set before EndFinalPostprocessSettings
		View->bIsPlanarReflection = bIsPlanarReflection;
		// Needs to be reconfigured now that bIsPlanarReflection has changed.
		View->SetupAntiAliasingMethod();

		{
			if (PreviousTransform.IsSet())
			{
				View->PreviousViewTransform = PreviousTransform.GetValue();
			}

			View->bCameraCut = SceneCaptureComponent->bCameraCutThisFrame;

			if (SceneCaptureComponent->bEnableClipPlane)
			{
				View->GlobalClippingPlane = ClipPlane;
				// Jitter can't be removed completely due to the clipping plane
				View->bAllowTemporalJitter = false;
			}
		}

		// Since 5.4 the following code originated from GetShowOnlyAndHiddenComponents()
		check(SceneCaptureComponent);
		for (auto It = SceneCaptureComponent->HiddenComponents.CreateConstIterator(); It; ++It)
		{
			// If the primitive component was destroyed, the weak pointer will return NULL.
			UPrimitiveComponent* PrimitiveComponent = It->Get();
			if (PrimitiveComponent)
			{
#if ENGINE_MAJOR_VERSION < 5 || ENGINE_MINOR_VERSION >= 4
				View->HiddenPrimitives.Add(PrimitiveComponent->GetPrimitiveSceneId());
#else
				View->HiddenPrimitives.Add(PrimitiveComponent->ComponentId);
#endif
			}
		}

		for (auto It = SceneCaptureComponent->HiddenActors.CreateConstIterator(); It; ++It)
		{
			AActor* Actor = *It;

			if (Actor)
			{
				for (UActorComponent* Component : Actor->GetComponents())
				{
					if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Component))
					{
#if ENGINE_MAJOR_VERSION < 5 || ENGINE_MINOR_VERSION >= 4
						View->HiddenPrimitives.Add(PrimComp->GetPrimitiveSceneId());
#else
						View->HiddenPrimitives.Add(PrimComp->ComponentId);
#endif
					}
				}
			}
		}

		if (SceneCaptureComponent->PrimitiveRenderMode == ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList)
		{
			View->ShowOnlyPrimitives.Emplace();

			for (auto It = SceneCaptureComponent->ShowOnlyComponents.CreateConstIterator(); It; ++It)
			{
				// If the primitive component was destroyed, the weak pointer will return NULL.
				UPrimitiveComponent* PrimitiveComponent = It->Get();
				if (PrimitiveComponent)
				{
#if ENGINE_MAJOR_VERSION < 5 || ENGINE_MINOR_VERSION >= 4
					View->HiddenPrimitives.Add(PrimitiveComponent->GetPrimitiveSceneId());
#else
					View->HiddenPrimitives.Add(PrimitiveComponent->ComponentId);
#endif
				}
			}

			for (auto It = SceneCaptureComponent->ShowOnlyActors.CreateConstIterator(); It; ++It)
			{
				AActor* Actor = *It;

				if (Actor)
				{
					for (UActorComponent* Component : Actor->GetComponents())
					{
						if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Component))
						{
#if ENGINE_MAJOR_VERSION < 5 || ENGINE_MINOR_VERSION >= 4
							View->HiddenPrimitives.Add(PrimComp->GetPrimitiveSceneId());
#else
							View->HiddenPrimitives.Add(PrimComp->ComponentId);
#endif
						}
					}
				}
			}
		}
		else if (SceneCaptureComponent->ShowOnlyComponents.Num() > 0 || SceneCaptureComponent->ShowOnlyActors.Num() > 0)
		{
			static bool bWarned = false;

			if (!bWarned)
			{
				UE_LOG(LogTemp, Log, TEXT("Scene Capture has ShowOnlyComponents or ShowOnlyActors ignored by the PrimitiveRenderMode setting! %s"), *SceneCaptureComponent->GetPathName());
				bWarned = true;
			}
		}

		ViewFamily.Views.Add(View);

		View->StartFinalPostprocessSettings(SceneCaptureViewInfo.ViewLocation);
		View->OverridePostProcessSettings(*PostProcessSettings, PostProcessBlendWeight);
		View->EndFinalPostprocessSettings(ViewInitOptions);
	}
}

static void CreateSceneRendererForSceneCapture(
	FSceneInterface* Scene,
	USceneCaptureComponent2D* SceneCaptureComponent,
	FRenderTarget* RenderTarget,
	float MaxViewDistance,
	bool bCaptureSceneColor,
	FPostProcessSettings* PostProcessSettings,
	float PostProcessBlendWeight,
	const AActor* ViewActor,
	FLookingGlassRenderingConfig& RenderingConfig
)
{
	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		RenderTarget,
		Scene,
		SceneCaptureComponent->ShowFlags)
		.SetResolveScene(!bCaptureSceneColor)
		.SetRealtimeUpdate(SceneCaptureComponent->bCaptureEveryFrame || SceneCaptureComponent->bAlwaysPersistRenderingState));


	SetupViewVamilyForSceneCapture(
		ViewFamily,
		SceneCaptureComponent,
		MakeArrayView(RenderingConfig.GetViewInfoArr().GetData(), RenderingConfig.GetViewInfoArr().Num()),
		MaxViewDistance,
		bCaptureSceneColor,
		/* bIsPlanarReflection = */ false,
		PostProcessSettings,
		PostProcessBlendWeight,
		ViewActor);

	// Screen percentage is still not supported in scene capture.
	ViewFamily.EngineShowFlags.ScreenPercentage = false;
	ViewFamily.SetScreenPercentageInterface(new FLegacyScreenPercentageDriver(
		ViewFamily, /* GlobalResolutionFraction = */ 1.0f, /* AllowPostProcessSettingsScreenPercentage = */ false));

	ViewFamily.SceneCaptureSource = SceneCaptureComponent->CaptureSource;
	ViewFamily.SceneCaptureCompositeMode = SceneCaptureComponent->CompositeMode;

	// Reset scene capture's camera cut.
	SceneCaptureComponent->bCameraCutThisFrame = false;

	auto FeatureLevel = ViewFamily.GetFeatureLevel();

	if (UTextureRenderTarget2D* TextureRenderTarget = SceneCaptureComponent->TextureTarget)
	{
		const bool bGenerateMips = TextureRenderTarget->bAutoGenerateMips;
		FGenerateMipsParams GenerateMipsParams{ TextureRenderTarget->MipsSamplerFilter == TF_Nearest ? SF_Point : (TextureRenderTarget->MipsSamplerFilter == TF_Trilinear ? SF_Trilinear : SF_Bilinear),
			TextureRenderTarget->MipsAddressU == TA_Wrap ? AM_Wrap : (TextureRenderTarget->MipsAddressU == TA_Mirror ? AM_Mirror : AM_Clamp),
			TextureRenderTarget->MipsAddressV == TA_Wrap ? AM_Wrap : (TextureRenderTarget->MipsAddressV == TA_Mirror ? AM_Mirror : AM_Clamp) };

		FTextureRenderTargetResource* TextureRenderTargetResource = TextureRenderTarget->GameThread_GetRenderTargetResource();

		ENQUEUE_RENDER_COMMAND(GenerateMips)(
			[TextureRenderTargetResource, bGenerateMips, GenerateMipsParams, FeatureLevel](FRHICommandListImmediate& RHICmdList)
		{
			if (bGenerateMips)
			{
#if (ENGINE_MAJOR_VERSION == 4) && (ENGINE_MINOR_VERSION < 26)
				FGenerateMips::Execute(RHICmdList, TextureRenderTargetResource->GetRenderTargetTexture(), GenerateMipsParams);
#else
				// Just embedded the older version of FGenerateMips::Execute, which became deprecated in 4.26 and removed in 5.1
				TRefCountPtr<IPooledRenderTarget> PooledRenderTarget = CreateRenderTarget(TextureRenderTargetResource->GetRenderTargetTexture(), TEXT("MipGeneration"));
				FRDGBuilder GraphBuilder(RHICmdList);
				//FGenerateMips::Execute(GraphBuilder, GraphBuilder.RegisterExternalTexture(PooledRenderTarget), GenerateMipsParams);
				GraphBuilder.Execute();
#endif
			}
		}
		);
	}

	FGameTime GameTime = FGameTime::CreateUndilated(FApp::GetCurrentTime() - GStartTime, FApp::GetDeltaTime());

	FCanvas Canvas(RenderTarget, nullptr, GameTime, Scene->GetFeatureLevel());
	GetRendererModule().BeginRenderingViewFamily(&Canvas, &ViewFamily);
}

void ULookingGlassSceneCaptureComponent2D::UpdateLookingGlassSceneCaptureContents(USceneCaptureComponent2D* CaptureComponent, FLookingGlassRenderingConfig& RenderingConfig, FSceneInterface* Scene)
{
	check(CaptureComponent);

	if (UTextureRenderTarget2D* TextureRenderTarget = CaptureComponent->TextureTarget)
	{
		const bool bUseSceneColorTexture = CaptureComponent->CaptureSource != SCS_FinalColorLDR &&
			CaptureComponent->CaptureSource != SCS_FinalColorHDR;

		CreateSceneRendererForSceneCapture(
			Scene,
			CaptureComponent,
			TextureRenderTarget->GameThread_GetRenderTargetResource(),
			CaptureComponent->MaxViewDistanceOverride,
			bUseSceneColorTexture,
			&CaptureComponent->PostProcessSettings,
			CaptureComponent->PostProcessBlendWeight,
			CaptureComponent->GetViewOwner(),
			RenderingConfig
		);
	}
}