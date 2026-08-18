// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Misc/EngineVersionComparison.h"
#include "Components/SceneCaptureComponent2D.h"

#include "LookingGlassSettings.h"

#include "LookingGlassSceneCaptureComponent2D.generated.h"

class FSceneInterface;
struct FSceneCaptureViewInfo;
class UMaterial;
class UTextureRenderTarget2D;
class ULookingGlassCameraComponent;
class ULookingGlassDrawFrustumComponent;


/**
 * Render configuration, which holds the texture and CaptureViewInfo. Represents a single line in quilt.
 */
struct FLookingGlassRenderingConfig
{
public:
	FLookingGlassRenderingConfig();

	~FLookingGlassRenderingConfig();

	/** Init should be called separately, because we do not control construction of UObject directly */
	void Init(UObject* Parent, uint32 InMinTextureIndex, uint32 InMaxTextureIndex, const FIntPoint& InViewSize);

	void AddReferencedObjects(FReferenceCollector& Collector);

	UTextureRenderTarget2D* GetRenderTarget() const { return RenderTarget; }

	TArray<FSceneCaptureViewInfo>& GetViewInfoArr() { return ViewInfoArr; }

	const TArray<FSceneCaptureViewInfo>& GetViewInfoArr() const { return ViewInfoArr; }

	int32 GetNumViews() const { return NumViews; }

	int32 GetFirstViewIndex() const { return FirstViewIndex; }

	int32 GetViewRows() const { return ViewRows; }

	int32 GetViewColumns() const { return ViewColumns; }

	// Resize the rendering target to match our needs
	//todo: resize it back to 1x1 when rendering stops (call ReduceMemoryUse)
	void PrepareRT();

	void ReduceMemoryUse();

	void Release();

	/** Maximal number of views rendered with a single scene render. On UE 5.8 every
	 *  BeginRenderingViewFamily call costs ~15ms of fixed overhead (scene captures are expected to
	 *  go through ISceneRenderBuilder now), so rendering the whole quilt as ONE multi-view family
	 *  instead of 6-9 families of 8 took the hologram from ~10 fps to real-time. Init() row-wraps
	 *  the views into a grid render target when one row would exceed the max texture width. */
	static constexpr uint8 MaxView = 64;

	static void CalculateViewRect(FIntRect& Rect, const FIntPoint& Size, int32 ViewRows, int32 ViewColumns, int32 ViewIndex);

	static void CalculateViewRect(float& U, float& V, float& SizeU, float& SizeV, int32 ViewRows, int32 ViewColumns, int32 ViewCount, int32 ViewIndex);

private:
	// TObjectPtr required: raw UObject* with AddReferencedObject crashes under incremental GC (on by default in newer engines)
	TObjectPtr<UTextureRenderTarget2D> RenderTarget;

	// Same as ViewInfoArr.Num(). Number of views is limited either by GMaxTextureDimensions or MaxViews.
	uint32 NumViews;

	int32 FirstViewIndex;

	// Size of RenderTarget used for rendering
	FIntPoint TextureSize;

	TArray<FSceneCaptureViewInfo> ViewInfoArr;

	int32 ViewRows;

	int32 ViewColumns;
};

/**
 * A list of FLookingGlassRenderingConfig objects
 */
struct FLookingGlassRenderingConfigs
{
	FLookingGlassRenderingConfigs()
	{}

	FLookingGlassRenderingConfigs(UObject* InOwner)
		: Owner(InOwner)
	{}

	// The outer object which is used for holding all render targets
	UObject* Owner;

	TArray<FLookingGlassRenderingConfig> Configs;

	// Recent TilingValues which were used for RebuildRenderConfigs
	FLookingGlassTilingQuality CachedTilingValues;

	void Build(const FLookingGlassTilingQuality& TilingValues, bool bSingleViewMode);

	void Release()
	{
		Configs.Empty();
	}

	void AddReferencedObjects(FReferenceCollector& Collector)
	{
		for (FLookingGlassRenderingConfig& Config : Configs)
		{
			Config.AddReferencedObjects(Collector);
		}
	}
};

/**
 * Capture looking glass multi views
 */
UCLASS(hidecategories=(Collision, Object, Physics, SceneComponent, Projection), ClassGroup=Rendering, editinlinenew, meta=(BlueprintSpawnableComponent))
class LOOKINGGLASSRUNTIME_API ULookingGlassSceneCaptureComponent2D : public USceneCaptureComponent2D
{
	GENERATED_BODY()

public:
	ULookingGlassSceneCaptureComponent2D();

	// UObject garbage collector helper
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

#if UE_VERSION_OLDER_THAN(5, 7, 0)
	// UActorComponent::PostInterpChange was removed along with the legacy interp system in UE 5.7
	virtual void PostInterpChange(FProperty* PropertyThatChanged) override;
#endif

#if WITH_EDITOR
	void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;

	void PostEditComponentMove(bool bFinished) override;
#endif

	//~ Begin UActorComponent Interface
	/** All the time we render everything manually, never call deferred rendering */
	virtual void SendRenderTransform_Concurrent() override
	{
		USceneCaptureComponent::SendRenderTransform_Concurrent();
	}

	// Called when component is created
	virtual void OnRegister() override;

	// Called when component is destroyed
	virtual void OnUnregister() override;

	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	// Called when the game ends
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent Interface

	//~ Begin USceneCaptureComponent Interface
	virtual void UpdateSceneCaptureContents(FSceneInterface* Scene, class ISceneRenderBuilder& SceneRenderBuilder);
	//~ End USceneCaptureComponent Interface

	/** Top-level rendering function for making a hologram picture */
	void RenderViews();

	/** Top-level rendering function for making a 2D picture */
	void Render2DView(int32 SizeX = -1, int32 SizeY = -1);

	// Change current tiling settings and do all the required refresh work
	void SetTilingProperties(ELookingGlassQualitySettings InTilingQuailty);

	// Calls UpdateTilingProperties for all existing components
	static void UpdateTilingPropertiesForAllComponents();

	// Refresh current display tiling settings
	void UpdateTilingProperties();

	// Get ELookingGlassQualitySettings depending on the device type
	static ELookingGlassQualitySettings GetAutomaticTilingQuality();

	const FLookingGlassTilingQuality& GetTilingValues() { return TilingValues; }

	// UFUNCTION so the game module can call it via ProcessEvent without a compile-time
	// plugin dependency (used by the capture auto-fit to frame for the device aspect)
	UFUNCTION(BlueprintCallable, Category = "LookingGlass")
	float GetAspectRatio() const;

	UTextureRenderTarget2D* GetTextureTarget2DRendering() const { return TextureTarget2DRendering; }

	//todo: remove?
	UTexture2D* GetOverrideQuiltTexture2D() { return OverrideQuiltTexture2D; }

	float GetCameraDistance() const;

	const FLookingGlassRenderingConfigs& GetRenderingConfigs() const { return RenderingConfigs; }

	static void SetGlobalTilingProperties(ELookingGlassQualitySettings InTilingQuailty);

	static void ResetGlobalTilingProperties();

public:
	// Vertical size of the capture region in Unreal units (cm)
	UPROPERTY(Interp, BlueprintSetter = SetSize, EditAnywhere, BlueprintReadWrite, Category = "CaptureSettings", meta = (ClampMin = "0.1", UIMin = "0.1", UIMax = "2000.0"))
	float Size = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CaptureSettings", meta = (ClampMin = "0"))
	float NearClipFactor = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CaptureSettings")
	bool bUseFarClipPlane = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CaptureSettings",  meta = (EditCondition = "bUseFarClipPlane", ClampMin = "0"))
	float FarClipFactor = 1.5f;

	// Horizontal field of view angle of the camera
	UPROPERTY(Interp, EditAnywhere, BlueprintReadWrite, Category = "CaptureSettings", meta = (ClampMin = "8.0", ClampMax = "90.0", UIMin = "8.0", UIMax = "90.0"))
	float FOV = 14.0f;

	// When disabled, the plugin will render 8 pictures at a time. Enable it if you're experiencing issues with rendering (e.g. out of VRAM problem), with a cost of slower performance.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TilingSettings")
	bool bSingleViewMode = true;

	// A static replacement for Quilt image.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "QuiltSettings")
	UTexture2D* OverrideQuiltTexture2D = nullptr;

	// Customizable Tiling Settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TilingSettings")
	ELookingGlassQualitySettings TilingQuality = ELookingGlassQualitySettings::Q_Automatic;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TilingSettings", meta = (ShowOnlyInnerProperties))
	FLookingGlassTilingQuality TilingValues;

	// Customized TilingValues for Q_Custom.
	FLookingGlassTilingQuality CustomTilingValues;

	// Force Depth of Field effect to be focused on LookingGlassCapture location
	UPROPERTY(Interp, EditAnywhere, BlueprintReadOnly, Category = "PostProcessing")
	bool bOverrideDOF = false;

	// Distance adjustment for OverrideDOF mode
	UPROPERTY(Interp, EditAnywhere, BlueprintReadOnly, Category = "PostProcessing", meta = (editcondition = "bOverrideDOF"))
	float DOFAdjust = 0.0f;

	// Focal depth for OverrideDOF mode. Lower values makes more blurry images
	UPROPERTY(Interp, EditAnywhere, BlueprintReadOnly, Category = "PostProcessing", meta = (ClampMin = "0.1", ClampMax = "32.0", DisplayName = "Aperture (DoF strength)", editcondition = "bOverrideDOF"))
	float DepthOfFieldFstop = 1.0f;

	// True to draw a translucent plane at the current focus depth, for easy tweaking.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PostProcessing", meta = (editcondition = "bOverrideDOF"))
	bool bDrawDebugFocusPlane = false;

	// Allow to render motion blur based on PostProcessingVolume settings
	UPROPERTY(Interp, EditAnywhere, BlueprintReadOnly, Category = "PostProcessing")
	bool bEnableMotionBlur = false;

	// Container for rendering targets, plus viewport settings for each.
	FLookingGlassRenderingConfigs RenderingConfigs;

	/** Render target for 2D rendering camera. */
	UPROPERTY(transient)
	UTextureRenderTarget2D* TextureTarget2DRendering = nullptr;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CaptureSettings|Visual")
	FColor FrustumColor = FColor::Blue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CaptureSettings|Visual")
	FColor MidPlaneFrustumColor = FColor::White;

	// MidPlane Line Length for Frustum Component
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CaptureSettings|Visual", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float MidPlaneLineLength = 0.2f;

	// MidPlane Line Thickness for Frustum Component
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CaptureSettings|Visual", meta = (ClampMin = "0.0", ClampMax = "10.0", UIMin = "0.0", UIMax = "10.0"))
	float MidLineThickness;

	// Frustum Line Thickness for Frustum Component
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CaptureSettings|Visual", meta = (ClampMin = "0.0", ClampMax = "10.0", UIMin = "0.0", UIMax = "10.0"))
	float FrustumLineThickness;

	// For customizing the focus plane color, in case the default doesn't show up well in your scene.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CaptureSettings|Visual", meta = (EditCondition = "bDrawDebugFocusPlane"))
	FColor DebugFocusPlaneColor = FColor(102, 26, 204, 153);
#endif // WITH_EDITORONLY_DATA

	DECLARE_DELEGATE_OneParam(FLookingGlassObjectChangedDelegate, ULookingGlassSceneCaptureComponent2D*);
	FLookingGlassObjectChangedDelegate OnLookingGlassObjectChanged;

	UFUNCTION(BlueprintCallable, BlueprintSetter, Category = "LookingGlass")
	void SetSize(float InSize);

	UFUNCTION(BlueprintCallable, Category = "LookingGlass")
	void SetActiveCaptureComponent();

private:

#if WITH_EDITORONLY_DATA
	// The frustum component used to show visually where the camera field of view is
	ULookingGlassDrawFrustumComponent* DrawFrustum;

	UPROPERTY(transient)
	UStaticMesh* FocusPlaneVisualizationMesh;

	/** Material used for debug focus plane visualization */
	UPROPERTY(transient)
	UMaterial* FocusPlaneVisualizationMaterial;

	/** Component for the debug focus plane visualization */
	UPROPERTY(transient)
	UStaticMeshComponent* DebugFocusPlaneComponent;

	/** Dynamic material instance for the debug focus plane visualization */
	UPROPERTY(transient)
	UMaterialInstanceDynamic* DebugFocusPlaneMID;

	void RefreshVisualRepresentation();

	void CreateDebugFocusPlane();
	void DestroyDebugFocusPlane();
	void UpdateDebugFocusPlane();
#endif // WITH_EDITORONLY_DATA

	void SetupPostprocessing();

	void UpdateCameraPosition();

	/**
	 * Generates a projection matrix
	 *
	 * @param	OffsetX	The offset x coordinate.
	 * @param	OffsetY	The offset y coordinate.
	 *
	 * @returns	The projection matrix.
	 */
	FMatrix GenerateProjectionMatrix(float OffsetX, float OffsetY) const;

	/**
	 * Render the scene to the texture target immediately.
	 * This should not be used if bCaptureEveryFrame is enabled, or the scene capture will render redundantly.
	 */
	void CaptureLookingGlassScene(struct FLookingGlassRenderingConfig& RenderingConfig);

	// Start rendering
	static void UpdateLookingGlassSceneCaptureContents(USceneCaptureComponent2D* CaptureComponent, struct FLookingGlassRenderingConfig& RenderingConfig, FSceneInterface* Scene);

	void RebuildRenderConfigs()
	{
		TilingValues.Setup();
		RenderingConfigs.Build(TilingValues, bSingleViewMode);
	}

	// Flag telling that UpdateSceneCaptureContents() should pass execution to parent class
	bool bAllow2DCapture = false;

	float NearClipPlane = 0.f;

	float FarClipPlane = 0.f;
};
