#include "Game/LookingGlassSceneCaptureComponent2D.h"
#include "Game/LookingGlassDrawFrustumComponent.h"
#include "LookingGlassBridge.h"
#include "LookingGlassSettings.h"
#include "Misc/LookingGlassLog.h"
#include "Misc/LookingGlassStats.h"
#include "ILookingGlassRuntime.h" // for Editor/GameLookingGlassCaptureComponents

#include "SceneInterface.h"
#include "Engine/World.h"
#include "Math/UnrealMathUtility.h"
#include "Engine/TextureRenderTarget2D.h"

// For focus plane mesh and component
#include "UObject/ConstructorHelpers.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/CollisionProfile.h"

static ELookingGlassQualitySettings GlobalTilingQuailty = ELookingGlassQualitySettings::Q_GoPortrait;
static bool IsGlobalTilingQualitySet = false;

ULookingGlassSceneCaptureComponent2D::ULookingGlassSceneCaptureComponent2D()
	: RenderingConfigs(this)
{
	TextureTarget = nullptr;
	SetHiddenInGame(false);
	bCaptureEveryFrame = false;
	bUseCustomProjectionMatrix = true;
	bUseRayTracingIfEnabled = true;
	bAlwaysPersistRenderingState = true;
	CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;

#if WITH_EDITOR
	DrawFrustum = nullptr;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/ArtTools/RenderToTexture/Meshes/S_1_Unit_Plane.S_1_Unit_Plane"));
	FocusPlaneVisualizationMesh = PlaneMesh.Object;

	static ConstructorHelpers::FObjectFinder<UMaterial> PlaneMat(TEXT("/Engine/EngineDebugMaterials/M_SimpleUnlitTranslucent.M_SimpleUnlitTranslucent"));
	FocusPlaneVisualizationMaterial = PlaneMat.Object;
#endif // WITH_EDITOR

	// Init Config Values
	const ULookingGlassSettings* LookingGlassSettings = GetDefault<ULookingGlassSettings>();
	TilingValues = LookingGlassSettings->_GoPortrait_Settings;
	CustomTilingValues = LookingGlassSettings->CustomSettings;
}

void ULookingGlassSceneCaptureComponent2D::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	ULookingGlassSceneCaptureComponent2D* This = CastChecked<ULookingGlassSceneCaptureComponent2D>(InThis);
	This->RenderingConfigs.AddReferencedObjects(Collector);
	Super::AddReferencedObjects(InThis, Collector);
}

void ULookingGlassSceneCaptureComponent2D::OnRegister()
{
	Super::OnRegister();

#if WITH_EDITOR
	if (DrawFrustum == nullptr)
	{
		if (AActor* MyOwner = GetOwner())
		{
			// Note: if we'll use SetIsVisualizationComponent(true), this component will be hidden in actor's component tree.
			// Things which affects representation in tree:
			// - RF_Transactional | RF_TextExportTransient in NewObject
			// - SetIsVisualizationComponent(true)
			DrawFrustum = NewObject<ULookingGlassDrawFrustumComponent>(MyOwner, NAME_None, RF_Transactional | RF_TextExportTransient);
			DrawFrustum->SetupAttachment(this);
			DrawFrustum->SetIsVisualizationComponent(true);
			DrawFrustum->CreationMethod = CreationMethod;
			DrawFrustum->RegisterComponentWithWorld(GetWorld());
		}
	}

	if (GIsEditor)
	{
		ILookingGlassRuntime::Get().EditorLookingGlassCaptureComponents.Insert(this, 0);
		// Reset scale if it was changed by property editor
		SetRelativeScale3D(FVector::OneVector);
	}

	RefreshVisualRepresentation();
#endif // WITH_EDITOR

	UpdateTilingProperties();
}

void ULookingGlassSceneCaptureComponent2D::OnUnregister()
{
	Super::OnUnregister();

#if WITH_EDITOR
	if (GIsEditor)
	{
		ILookingGlassRuntime::Get().EditorLookingGlassCaptureComponents.Remove(this);
	}
#endif // WITH_EDITOR
}

void ULookingGlassSceneCaptureComponent2D::SetActiveCaptureComponent()
{
	// Re-insert this component to be first in LookingGlass components list
	auto& LookingGlassRuntime = ILookingGlassRuntime::Get();

	LookingGlassRuntime.GameLookingGlassCaptureComponents.Remove(this);
	LookingGlassRuntime.GameLookingGlassCaptureComponents.Insert(this, 0);

#if WITH_EDITOR
	if (GIsEditor)
	{
		LookingGlassRuntime.EditorLookingGlassCaptureComponents.Remove(this);
		LookingGlassRuntime.EditorLookingGlassCaptureComponents.Insert(this, 0);
	}
#endif // WITH_EDITOR
}

void ULookingGlassSceneCaptureComponent2D::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	Super::OnComponentDestroyed(bDestroyingHierarchy);

	RenderingConfigs.Release();

#if WITH_EDITOR
	if (DrawFrustum)
	{
		DrawFrustum->DestroyComponent();
	}
#endif
}

#if WITH_EDITOR
void ULookingGlassSceneCaptureComponent2D::RefreshVisualRepresentation()
{
	if (DrawFrustum != nullptr)
	{
		float AspectRatio = GetAspectRatio();

		DrawFrustum->FrustumAngle = FOV;
		DrawFrustum->FrustumStartDist = NearClipPlane;
		DrawFrustum->FrustumMidDist = GetCameraDistance();
		DrawFrustum->FrustumEndDist = FarClipPlane;
		DrawFrustum->VerticalAngle = 0.0f;
		DrawFrustum->FrustumAspectRatio = AspectRatio;
		DrawFrustum->MidPlaneLineLength = MidPlaneLineLength;
		DrawFrustum->FrustumLineThickness = FrustumLineThickness;
		DrawFrustum->MidLineThickness = MidLineThickness;
		DrawFrustum->MarkRenderStateDirty();
		DrawFrustum->FrustumColor = FrustumColor;
		DrawFrustum->MidFrustumColor = MidPlaneFrustumColor;
	}
}

void ULookingGlassSceneCaptureComponent2D::CreateDebugFocusPlane()
{
	if (AActor* const MyOwner = GetOwner())
	{
		if (DebugFocusPlaneComponent == nullptr)
		{
			DebugFocusPlaneComponent = NewObject<UStaticMeshComponent>(MyOwner, NAME_None, RF_Transient | RF_Transactional | RF_TextExportTransient);
			DebugFocusPlaneComponent->SetupAttachment(this);
			DebugFocusPlaneComponent->SetIsVisualizationComponent(true);
			DebugFocusPlaneComponent->SetStaticMesh(FocusPlaneVisualizationMesh);
			DebugFocusPlaneComponent->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
			DebugFocusPlaneComponent->bHiddenInGame = false;
			DebugFocusPlaneComponent->CastShadow = false;
			DebugFocusPlaneComponent->CreationMethod = CreationMethod;
			DebugFocusPlaneComponent->bSelectable = false;

			DebugFocusPlaneComponent->SetRelativeScale3D_Direct(FVector(10000.f, 10000.f, 1.f));
			DebugFocusPlaneComponent->SetRelativeRotation_Direct(FRotator(90.f, 0.f, 0.f));

			DebugFocusPlaneComponent->RegisterComponentWithWorld(GetWorld());

			DebugFocusPlaneMID = DebugFocusPlaneComponent->CreateAndSetMaterialInstanceDynamicFromMaterial(0, FocusPlaneVisualizationMaterial);
			if (DebugFocusPlaneMID)
			{
				DebugFocusPlaneMID->SetVectorParameterValue(FName(TEXT("Color")), DebugFocusPlaneColor.ReinterpretAsLinear());
			}
		}
	}
}

void ULookingGlassSceneCaptureComponent2D::DestroyDebugFocusPlane()
{
	if (DebugFocusPlaneComponent)
	{
		DebugFocusPlaneComponent->SetVisibility(false);
		DebugFocusPlaneComponent = nullptr;

		DebugFocusPlaneMID = nullptr;
	}
}

void ULookingGlassSceneCaptureComponent2D::UpdateDebugFocusPlane()
{
	if (FocusPlaneVisualizationMesh && DebugFocusPlaneComponent)
	{
		FVector const CamLocation = GetComponentTransform().GetLocation();
		FVector const CamDir = GetComponentTransform().GetRotation().Vector();

		UWorld const* const World = GetWorld();
		float const FocusDistance = GetCameraDistance() + DOFAdjust;
		FVector const FocusPoint = GetComponentTransform().GetLocation() + CamDir * FocusDistance;

		DebugFocusPlaneComponent->SetWorldLocation(FocusPoint);
	}
}
#endif // WITH_EDITOR

void ULookingGlassSceneCaptureComponent2D::BeginPlay()
{
	Super::BeginPlay();

	// Initialize TilingQuality (important when it's set to "Automatic")
	UpdateTilingProperties();

	ILookingGlassRuntime::Get().GameLookingGlassCaptureComponents.Insert(this, 0);
}

// Called when the game ends
void ULookingGlassSceneCaptureComponent2D::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// It should automatically clean all render texture targets
	RenderingConfigs.Release();

	ILookingGlassRuntime::Get().GameLookingGlassCaptureComponents.Remove(this);
}

void ULookingGlassSceneCaptureComponent2D::UpdateSceneCaptureContents(FSceneInterface* Scene, class ISceneRenderBuilder& SceneRenderBuilder)
{
	// This function is called by USceneCaptureComponent2D::CaptureSceneDeferred() and UpdateDeferredCaptures()
	// when any property of this component is changed. This capture is useless because it is executed in a
	// different way, also it causes engine to crash with NULL render target. So, let's just declare an empty
	// implementation. The crash was caused by PostEditChangeProperty or OnRegister calls to parent class.

	if (bAllow2DCapture)
	{
		Super::UpdateSceneCaptureContents(Scene, SceneRenderBuilder);
	}
}

#if UE_VERSION_OLDER_THAN(5, 7, 0)
/**
 * This is called when property is modified by InterpPropertyTracks
 *
 * @param PropertyThatChanged	Property that changed
 */
void ULookingGlassSceneCaptureComponent2D::PostInterpChange(FProperty* PropertyThatChanged)
{
	if (PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(ULookingGlassSceneCaptureComponent2D, Size))
	{
		OnLookingGlassObjectChanged.ExecuteIfBound(this);
	}
	else
	{
		//if (ENGINE_MAJOR_VERSION < 5) {
			//Super::PostInterpChange(PropertyThatChanged);
		//}
	}
}
#endif // UE_VERSION_OLDER_THAN(5, 7, 0)

// This function is called when "Size" property is changed in blueprint or interpolated by movie sequence
void ULookingGlassSceneCaptureComponent2D::SetSize(float InSize)
{
	Size = InSize;
	UpdateCameraPosition();
#if WITH_EDITOR
	UpdateDebugFocusPlane();
#endif
	OnLookingGlassObjectChanged.ExecuteIfBound(this);
}

#if WITH_EDITOR

void ULookingGlassSceneCaptureComponent2D::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property != nullptr)
	{
		FName PropertyName = PropertyChangedEvent.Property->GetFName();

		if (PropertyName == GET_MEMBER_NAME_CHECKED(ULookingGlassSceneCaptureComponent2D, TilingQuality))
		{
			UpdateTilingProperties();
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(FLookingGlassTilingQuality, TilesX) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(FLookingGlassTilingQuality, TilesY) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(FLookingGlassTilingQuality, QuiltW) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(FLookingGlassTilingQuality, QuiltH) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(ULookingGlassSceneCaptureComponent2D, bSingleViewMode)
			)
		{
			// Reset our render textures and configuration after it
			RebuildRenderConfigs();
		}

		// Check if Game is running
		if (GetWorld() && GetWorld()->IsGameWorld())
		{
			UpdateCameraPosition();
		}

		// Focus plane controls
		bool bDrawFocusPlane = bDrawDebugFocusPlane && bOverrideDOF;
		if (bDrawFocusPlane && (DebugFocusPlaneComponent == nullptr))
		{
			CreateDebugFocusPlane();
		}
		else if (!bDrawFocusPlane && (DebugFocusPlaneComponent != nullptr))
		{
			DestroyDebugFocusPlane();
		}
		// set focus plane color in case that's what changed
		if (DebugFocusPlaneMID)
		{
			DebugFocusPlaneMID->SetVectorParameterValue(FName(TEXT("Color")), DebugFocusPlaneColor.ReinterpretAsLinear());
		}
		UpdateDebugFocusPlane();

		OnLookingGlassObjectChanged.ExecuteIfBound(this);
	}
}

void ULookingGlassSceneCaptureComponent2D::PostEditComponentMove(bool bFinished)
{
	Super::PostEditComponentMove(bFinished);

/*
	// This function is called when using gizmo to modify the component. We'll force
	// scale to identity value.
	FVector Scale = GetRelativeScale3D();
	if (!Scale.IsUnit())
	{
		// Performing reverse math from
		// - FLevelEditorViewportClient::ApplyDeltaToComponent
		// - UEditorEngine::ApplyDeltaToComponent
		//todo: Unfortunately it doesn't work without having direct access to editor internal objects, what's not possible in Runtime module
	}
*/
}

#endif // WITH_EDITOR

void ULookingGlassSceneCaptureComponent2D::CaptureLookingGlassScene(FLookingGlassRenderingConfig& RenderingConfig)
{
	UWorld* World = GetWorld();
	if (World && World->Scene && IsVisible())
	{
		//? We must push any deferred render state recreations before causing any rendering to happen, to make sure that deleted resource references are updated
		World->SendAllEndOfFrameUpdates();
		UpdateLookingGlassSceneCaptureContents(this, RenderingConfig, World->Scene);
	}
}

// Recalculates tiling values and sets new values used for frustum visualization
void ULookingGlassSceneCaptureComponent2D::UpdateCameraPosition()
{
	float CameraDistance = GetCameraDistance();

	// Compute clipping planes
	NearClipPlane = CameraDistance - NearClipFactor * Size;
	NearClipPlane = FMath::Clamp(NearClipPlane, 1.0f, CameraDistance);

	FarClipPlane = CameraDistance + FMath::Max(FarClipFactor, 0.0f) * Size;

#if WITH_EDITOR
	RefreshVisualRepresentation();
#endif // WITH_EDITOR
}

// Calculate and return matrix used for camera projection matrices
FMatrix ULookingGlassSceneCaptureComponent2D::GenerateProjectionMatrix(float OffsetX, float OffsetY) const
{
	float HalfFOV = FMath::DegreesToRadians(FOV) / 2.0f;
	float Aspect = GetAspectRatio();

	float MaxZ = 0.f;
	float MinZ = 0.f;
	if (bUseFarClipPlane)
	{
		MaxZ = ((NearClipPlane == FarClipPlane) ? 0.0f : NearClipPlane / (NearClipPlane - FarClipPlane));
		MinZ = ((NearClipPlane == FarClipPlane) ? NearClipPlane : -FarClipPlane * NearClipPlane / (NearClipPlane - FarClipPlane));
	}
	else
	{
		MinZ = NearClipPlane;
	}

	FMatrix PerspectiveMatrix(
		FPlane(1.0f / FMath::Tan(HalfFOV), 0.0f, 0.0f, 0.0f),
		FPlane(0.0f, Aspect / FMath::Tan(HalfFOV), 0.0f, 0.0f),
		FPlane(OffsetX, OffsetY, MaxZ, 1.0f),
		FPlane(0.0f, 0.0f, MinZ, 0.0f)
	);

	UE_LOG(LookingGlassLogGame, Verbose, TEXT("PerspectiveMatrix: %s"), *PerspectiveMatrix.ToString());

	return PerspectiveMatrix;
}

float ULookingGlassSceneCaptureComponent2D::GetAspectRatio() const
{
	const ULookingGlassSettings* LookingGlassSettings = GetDefault<ULookingGlassSettings>();

	float Aspect = 0.0f;

	if (TilingValues.Aspect == 0.0f)
	{
		// Compute aspect from device
		const FLGDeviceCalibration& Calibration = ILookingGlassRuntime::Get().GetCurrentCalibration();
		Aspect = Calibration.Aspect != 0 ? Calibration.Aspect : (float)Calibration.Width / Calibration.Height;
		// Older code: using TilingValues to get the aspect
		// Aspect =(float) TilingValues.TileSizeX / TilingValues.TileSizeY;
	}
	else
	{
		Aspect = TilingValues.Aspect;
	}

	// Clamp the value to something reasonable, including impossibility to set zero value
	Aspect = FMath::Clamp(Aspect, 0.05f, 20.0f);

	return Aspect;
}

float ULookingGlassSceneCaptureComponent2D::GetCameraDistance() const
{
	return Size / FMath::Tan(FMath::DegreesToRadians(FOV * 0.5f));
}

void FLookingGlassRenderingConfigs::Build(const FLookingGlassTilingQuality& TilingValues, bool bSingleViewMode)
{
	int32 NumTiles = TilingValues.GetNumTiles();
	int32 MaxViewCount = FLookingGlassRenderingConfig::MaxView;
	if (bSingleViewMode)
	{
		MaxViewCount = 1;
	}
	int32 NumConfiguraions = (NumTiles + MaxViewCount - 1) / MaxViewCount;

	// Do not rebuild render targets if nothing has been changed. Compare parameters which are considered
	// for building a new configuration set.
	if (CachedTilingValues == TilingValues && Configs.Num() == NumConfiguraions)
	{
		return;
	}
	CachedTilingValues = TilingValues;

	// Release previous setup for building a new one
	Release();

	FIntPoint ViewSize(TilingValues.TileSizeX, TilingValues.TileSizeY);

	uint32 MinTextureIndex = 0;

	for (int32 CurrentView = 0; CurrentView < NumTiles; CurrentView++)
	{
		// Add new render target
		if (((CurrentView + 1) % MaxViewCount == 0) ||		// end of row
			(CurrentView + 1 == NumTiles))					// the last tile
		{
			FLookingGlassRenderingConfig& Config = Configs.AddDefaulted_GetRef();
			Config.Init(Owner, MinTextureIndex, CurrentView, ViewSize);

			// Prepare for the next row
			MinTextureIndex = CurrentView + 1;
		}
	}
	check(Configs.Num() == NumConfiguraions);

	if (Configs.Num() == 0)
	{
		UE_LOG(LookingGlassLogGame, Warning, TEXT("Something went wrong, RenderingConfigs never should be 0"));
	}
}

void ULookingGlassSceneCaptureComponent2D::SetTilingProperties(ELookingGlassQualitySettings InTilingQuailty)
{
	if (TilingQuality != InTilingQuailty)
	{
		TilingQuality = InTilingQuailty;
		UpdateTilingProperties();
	}
}

void ULookingGlassSceneCaptureComponent2D::UpdateTilingProperties()
{
	// Save edited CustomTilingValues for later use
	if (TilingValues.Name == "Custom")
	{
		CustomTilingValues = TilingValues;
	}

	// Get the current tiling setup enum value, and resolve "automatic" setting
	ELookingGlassQualitySettings TilingSettings = TilingQuality;
	if (IsGlobalTilingQualitySet)
	{
		TilingSettings = GlobalTilingQuailty;
	}

	// Handle the "automatic" setting separately. Recognize the current device.
	if (TilingSettings == ELookingGlassQualitySettings::Q_Automatic)
	{
		TilingSettings = GetAutomaticTilingQuality();
	}

	if (TilingSettings == ELookingGlassQualitySettings::Q_Custom)
	{
		// Use custom values stored in this component
		TilingValues = CustomTilingValues;
		TilingValues.Setup();
	}
	else
	{
		const ULookingGlassSettings* LookingGlassSettings = GetDefault<ULookingGlassSettings>();
		TilingValues = LookingGlassSettings->GetTilingQualityFor(TilingSettings);
	}

	// Reset our render textures and configuration after it
	RebuildRenderConfigs();

	UpdateCameraPosition();
}

void ULookingGlassSceneCaptureComponent2D::UpdateTilingPropertiesForAllComponents()
{
	auto& LookingGlassRuntime = ILookingGlassRuntime::Get();

	for (auto& It : LookingGlassRuntime.GameLookingGlassCaptureComponents)
	{
		if (It.IsValid())
		{
			It->UpdateTilingProperties();
		}
	}
#if WITH_EDITOR
	if (GIsEditor)
	{
		for (auto& It : LookingGlassRuntime.EditorLookingGlassCaptureComponents)
		{
			if (It.IsValid())
			{
				It->UpdateTilingProperties();
			}
		}
	}
#endif // WITH_EDITOR
}

void ULookingGlassSceneCaptureComponent2D::SetGlobalTilingProperties(ELookingGlassQualitySettings InTilingQuailty)
{
	GlobalTilingQuailty = InTilingQuailty;
	IsGlobalTilingQualitySet = true;
	UpdateTilingPropertiesForAllComponents();
}

void ULookingGlassSceneCaptureComponent2D::ResetGlobalTilingProperties()
{
	IsGlobalTilingQualitySet = false;
	UpdateTilingPropertiesForAllComponents();
}

ELookingGlassQualitySettings ULookingGlassSceneCaptureComponent2D::GetAutomaticTilingQuality()
{
	const FLGDeviceCalibration& Calibration = ILookingGlassRuntime::Get().GetCurrentCalibration();

	// Recognize the device by its Serial

	if (Calibration.Serial.Contains(TEXT("Looking Glass - Portrait")) ||
		Calibration.Serial.Contains(TEXT("PORT")) ||
		Calibration.Serial.Contains(TEXT("Portrait")))
	{
		return ELookingGlassQualitySettings::Q_Portrait;
	}

	if (Calibration.Serial.Contains(TEXT("Looking Glass - 16\"")) ||
		Calibration.Serial.Contains(TEXT("LKG-A")) ||
		Calibration.Serial.Contains(TEXT("LKG-4K")))
	{
		return ELookingGlassQualitySettings::Q_FourK;
	}

	if (Calibration.Serial.Contains(TEXT("Looking Glass - 32\"")) ||
		Calibration.Serial.Contains(TEXT("LKG-B")) ||
		Calibration.Serial.Contains(TEXT("LKG-8K")))
	{
		return ELookingGlassQualitySettings::Q_EightK;
	}

	if (Calibration.Serial.Contains(TEXT("Looking Glass - 65\"")) ||
		Calibration.Serial.Contains(TEXT("LKG-D")))
	{
		return ELookingGlassQualitySettings::Q_65_Inch;
	}

	if (Calibration.Serial.Contains(TEXT("LKG-Q")))
	{
		return ELookingGlassQualitySettings::Q_Prototype;
	}

	if (Calibration.Serial.Contains(TEXT("LKG-E")))
	{
		return ELookingGlassQualitySettings::Q_GoPortrait;
	}

	if (Calibration.Serial.Contains(TEXT("LKG-F")))
	{
		return ELookingGlassQualitySettings::Q_Kiosk;
	}

	if (Calibration.Serial.Contains(TEXT("LKG-H")))
	{
		return ELookingGlassQualitySettings::Q_16_Portrait;
	}

	if (Calibration.Serial.Contains(TEXT("LKG-J")))
	{
		return ELookingGlassQualitySettings::Q_16_Landscape;
	}

	if (Calibration.Serial.Contains(TEXT("LKG-K")))
	{
		return ELookingGlassQualitySettings::Q_32_Portrait;
	}

	if (Calibration.Serial.Contains(TEXT("LKG-L")))
	{
		return ELookingGlassQualitySettings::Q_32_Landscape;
	}

	if (Calibration.Serial.Contains(TEXT("Looking Glass - 8.9\"")) ||
		Calibration.Serial.Contains(TEXT("LKG-2K")))
	{
		return ELookingGlassQualitySettings::Q_EightPointNineLegacy;
	}

	// Fallback to Portrait
	return ELookingGlassQualitySettings::Q_Portrait;
}

void ULookingGlassSceneCaptureComponent2D::SetupPostprocessing()
{
	// Setup all render targets
	float CamDistance = GetCameraDistance();

	if (bOverrideDOF)
	{
		// Focus Depth of Field effect on target point
		PostProcessBlendWeight = 1.0f;
		PostProcessSettings.bOverride_DepthOfFieldFocalDistance = true;
		PostProcessSettings.DepthOfFieldFocalDistance = CamDistance + DOFAdjust;
		PostProcessSettings.bOverride_DepthOfFieldFstop = true;
		PostProcessSettings.DepthOfFieldFstop = DepthOfFieldFstop;
	}
	else
	{
		// Reset Depth of Field settings
		PostProcessBlendWeight = 0.0f;
		PostProcessSettings.bOverride_DepthOfFieldFocalDistance = false;
		PostProcessSettings.DepthOfFieldFocalDistance = 0;
		PostProcessSettings.bOverride_DepthOfFieldFstop = false;
	}

	ShowFlags.EnableAdvancedFeatures();
	ShowFlags.SetTemporalAA(false);

#if ENGINE_MAJOR_VERSION >= 5
	ShowFlags.SetLumenGlobalIllumination(true);
	ShowFlags.SetLumenReflections(true);
#endif

	// Enable or disable motion blur rendering
	ShowFlags.SetMotionBlur(bEnableMotionBlur);
}

// Called by LookingGlassViewportClient used for capturing new snapshot of scene from SceneCapture (RenderCamera)
void ULookingGlassSceneCaptureComponent2D::RenderViews()
{
	SetupPostprocessing();

	// Release RT used for 2D rendering, if any
	TextureTarget2DRendering = nullptr;

	// Setup all render targets
	float CamDistance = GetCameraDistance();

	// Get the ViewCone value for current device
	const FLGDeviceCalibration& Calibration = ILookingGlassRuntime::Get().GetCurrentCalibration();
	float ViewCone = Calibration.ViewCone;
	float ViewConeSweep = CamDistance * FMath::Tan(FMath::DegreesToRadians(ViewCone));

	int32 NumTiles = TilingValues.GetNumTiles();

	// Compute rotation matrix
	FTransform Transform = GetComponentToWorld();

	// Remove the translation from Transform because we only need rotation.
	Transform.SetTranslation(FVector::ZeroVector);
	Transform.SetScale3D(FVector::OneVector);
	FMatrix ViewRotationMatrix = Transform.ToInverseMatrixWithScale();

	// Swap axis st. x=z,y=x,z=y (Unreal coordinate space) so that z is up
	ViewRotationMatrix = ViewRotationMatrix * FMatrix(
		FPlane(0, 0, 1, 0),
		FPlane(1, 0, 0, 0),
		FPlane(0, 1, 0, 0),
		FPlane(0, 0, 0, 1));

	for (FLookingGlassRenderingConfig& RenderingConfig : RenderingConfigs.Configs)
	{
		// Rendering target is initialized as 1x1 texture, so it won't take much space until rendering starts.
		// We should resize the target before rendering.
		RenderingConfig.PrepareRT();

		// Set render target texture to SceneCaptureComponent. The rendering code which is called from
		// this function receives RenderingConfig as input, but it relies on TextureTarget to be set
		TextureTarget = RenderingConfig.GetRenderTarget();

		int32 NumViews = RenderingConfig.GetNumViews();
		check(NumViews);

		for (int32 ViewIndex = 0; ViewIndex < NumViews; ++ViewIndex)
		{
			// If NumTiles is 1, take the center view
			float CurrentViewLerp = 0.f;
			if (NumTiles > 1)
			{
				CurrentViewLerp = (float)(RenderingConfig.GetFirstViewIndex() + ViewIndex) / (NumTiles - 1.f) - .5f;
			}

			float ViewOffsetX = CurrentViewLerp * ViewConeSweep;
			float ProjModifier = 1.0f / Size;
			float ProjOffsetX = ViewOffsetX * ProjModifier;

			UE_LOG(LookingGlassLogGame, Verbose, TEXT("ViewOffsetX: %f, ProjOffsetX: %f"), ViewOffsetX, ProjOffsetX);

			FTransform RelativeTransform = FTransform::Identity;
			RelativeTransform.SetTranslation(FVector(0.0f, ViewOffsetX, 0.0f));

			FTransform WorldTransform = GetComponentToWorld();
			WorldTransform = RelativeTransform * WorldTransform;
			FVector ViewLocation = WorldTransform.GetTranslation();

			FSceneCaptureViewInfo& ViewInfo = RenderingConfig.GetViewInfoArr()[ViewIndex];
			ViewInfo.ViewRotationMatrix = ViewRotationMatrix;
			ViewInfo.ViewLocation = ViewLocation;
			ViewInfo.ProjectionMatrix = GenerateProjectionMatrix(ProjOffsetX, 0.f);
		}

		// Render view
		SCOPE_CYCLE_COUNTER(STAT_CaptureScene_GameThread);
		CaptureLookingGlassScene(RenderingConfig);

		// Do not hold TextureTarget after rendering
		TextureTarget = nullptr;
	}
}

void ULookingGlassSceneCaptureComponent2D::Render2DView(int32 SizeX, int32 SizeY)
{
	SetupPostprocessing();

	const FLGDeviceCalibration& Calibration = ILookingGlassRuntime::Get().GetCurrentCalibration();

	// Check if the texture is exists
	if (TextureTarget2DRendering == nullptr)
	{
		TextureTarget2DRendering = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), UTextureRenderTarget2D::StaticClass());
		TextureTarget2DRendering->InitCustomFormat(Calibration.Width, Calibration.Height, PF_A16B16G16R16, false);
		TextureTarget2DRendering->ClearColor = FLinearColor::Red;
		TextureTarget2DRendering->UpdateResourceImmediate();
	}

	if (SizeX < 0 && SizeY < 0)
	{
		SizeX = Calibration.Width;
		SizeY = Calibration.Height;
	}

	if (SizeX != TextureTarget2DRendering->SizeX || SizeY != TextureTarget2DRendering->SizeY)
	{
		TextureTarget2DRendering->ResizeTarget(SizeX, SizeY);
		TextureTarget2DRendering->UpdateResourceImmediate();
		// Sync with rendering thread, in order to reset texture resolution
		FlushRenderingCommands();
	}

	// Set 2D texture
	TextureTarget = TextureTarget2DRendering;
	CustomProjectionMatrix = GenerateProjectionMatrix(0.f, 0.f);

	// Do the simple 2D rendering
	bAllow2DCapture = true;
	CaptureScene();
	bAllow2DCapture = false;

	// Do not hold TextureTarget after rendering
	TextureTarget = nullptr;
}


/*
 * FLookingGlassRenderingConfig
 */

void FLookingGlassRenderingConfig::CalculateViewRect(FIntRect& Rect, const FIntPoint& Size, int32 ViewRows, int32 ViewColumns, int32 ViewIndex)
{
	Rect.Min.X = (ViewIndex % ViewColumns) * Size.X;
	Rect.Max.X = Rect.Min.X + Size.X;
	int32 Row = ViewIndex / ViewColumns;
	Rect.Min.Y = Row * Size.Y;
	Rect.Max.Y = Rect.Min.Y + Size.Y;
}

void FLookingGlassRenderingConfig::CalculateViewRect(float& U, float& V, float& SizeU, float& SizeV, int32 ViewRows, int32 ViewColumns, int32 ViewCount, int32 ViewIndex)
{
	SizeU = 1.f / ViewColumns;
	SizeV = 1.f / ViewRows;

	U = (ViewIndex % ViewColumns) * SizeU;
	int32 Row = ViewIndex / ViewColumns;
	V = Row * SizeV;
}

FLookingGlassRenderingConfig::FLookingGlassRenderingConfig()
	: RenderTarget(nullptr)
	, FirstViewIndex(0)
	, TextureSize(1, 1)
{
}

FLookingGlassRenderingConfig::~FLookingGlassRenderingConfig()
{
	Release();
}

void FLookingGlassRenderingConfig::Release()
{
	if (RenderTarget != nullptr && RenderTarget->IsValidLowLevel())
	{
		RenderTarget->ConditionalBeginDestroy();
	}

	RenderTarget = nullptr;
}

void FLookingGlassRenderingConfig::Init(UObject* Parent, uint32 InMinTextureIndex, uint32 InMaxTextureIndex, const FIntPoint& InViewSize)
{
	FirstViewIndex = InMinTextureIndex;
	NumViews = InMaxTextureIndex - InMinTextureIndex + 1;

	if (NumViews > 0)
	{
		static int32 GMaxTextureDimensionsLocal = (int32)GMaxTextureDimensions;
		check(InViewSize.X < GMaxTextureDimensionsLocal);
		check(InViewSize.Y < GMaxTextureDimensionsLocal);
		int32 TextureSizeX = float(InViewSize.X * NumViews);
		ViewRows = 1;
		if ((TextureSizeX - GMaxTextureDimensionsLocal) > 0)
		{
			ViewRows = FMath::RoundFromZero(float(NumViews) / float(GMaxTextureDimensionsLocal / InViewSize.X));
		}

		ViewColumns = FMath::RoundFromZero(float(NumViews) / float(ViewRows));
		TextureSize.X = InViewSize.X * ViewColumns;
		TextureSize.Y = InViewSize.Y * ViewRows;

		const FName TargetName = MakeUniqueObjectName(Parent, UTextureRenderTarget2D::StaticClass());
		// Make a new UTextureRenderTarget2D object. Note: adding RF_TextExportTransient to not let it go to Copy or Duplicate
		// operation, as this object will be regenerated by C++ code anyway. Plus, it bypasses a warning in UTextureRenderTarget2D::PostEditChangeProperty
		// saying (for each render target) that RT is very large and performance will be poor.
		RenderTarget = NewObject<UTextureRenderTarget2D>(Parent, TargetName, RF_Transient | RF_Transactional | RF_TextExportTransient);
		// Initialize with 1x1 texture. Resolution will be changed later, dynamically (in PrepareRT())
		RenderTarget->InitCustomFormat(1, 1, PF_A16B16G16R16, false);
		RenderTarget->ClearColor = FLinearColor::Red;
		RenderTarget->UpdateResourceImmediate();

		ViewInfoArr.AddZeroed(NumViews);
		for (int32 CaptureIndex = 0; CaptureIndex < ViewInfoArr.Num(); ++CaptureIndex)
		{
			ViewInfoArr[CaptureIndex].StereoPass = EStereoscopicPass::eSSP_FULL;

#if ((ENGINE_MAJOR_VERSION < 5) || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1))
			ViewInfoArr[CaptureIndex].StereoIPD = 0.0f;
#endif

			CalculateViewRect(ViewInfoArr[CaptureIndex].ViewRect, InViewSize, ViewRows, ViewColumns, CaptureIndex);
		}
	}
	else
	{
		UE_LOG(LookingGlassLogGame, Warning, TEXT("Wrong new number index"));
	}
}

void FLookingGlassRenderingConfig::AddReferencedObjects(FReferenceCollector& Collector)
{
	if (RenderTarget != nullptr)
	{
		Collector.AddReferencedObject(RenderTarget);
	}
}

void FLookingGlassRenderingConfig::PrepareRT()
{
	check(RenderTarget != nullptr);
	RenderTarget->ResizeTarget(TextureSize.X, TextureSize.Y);
}

void FLookingGlassRenderingConfig::ReduceMemoryUse()
{
	if (RenderTarget != nullptr)
	{
		RenderTarget->ResizeTarget(1, 1);
	}
}
