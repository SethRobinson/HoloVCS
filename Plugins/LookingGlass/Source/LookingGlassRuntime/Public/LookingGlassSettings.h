 // Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "InputCoreTypes.h"

#include "LookingGlassSettings.generated.h"


/**
 * @enum	ELookingGlassModeType
 *
 * @brief	Values that represent LookingGlass mode types
 */

UENUM(BlueprintType, meta = (ScriptName = "LookingGlassPlayMode"))
enum class ELookingGlassModeType : uint8
{
	PlayMode_InSeparateWindow	UMETA(DisplayName = "In Separate Window"),
	PlayMode_InMainViewport		UMETA(DisplayName = "In Main Viewport")
};


// Tiling Quality Settings Enum (For UI)

/**
 * @enum	ELookingGlassQualitySettings
 *
 * @brief	Values that represent LookingGlass quality settings
 */

UENUM(BlueprintType, meta = (ScriptName = "LookingGlassQualitySettings"))
enum class ELookingGlassQualitySettings : uint8
{
	Q_Automatic 			UMETA(DisplayName = "Automatic"),
	Q_Portrait 				UMETA(DisplayName = "Portrait"),
	Q_FourK 				UMETA(DisplayName = "16 inch (4K)"),
	Q_EightK 				UMETA(DisplayName = "32 inch (8K)"),
	Q_65_Inch				UMETA(DisplayName = "65 inch"),
	Q_Prototype				UMETA(DisplayName = "Prototype"),
	Q_GoPortrait			UMETA(DisplayName = "Go Portrait"),
	Q_Kiosk					UMETA(DisplayName = "Kiosk"),
	Q_16_Portrait			UMETA(DisplayName = "16 inch Portrait"),
	Q_16_Landscape			UMETA(DisplayName = "16 inch Landscape"),
	Q_32_Portrait			UMETA(DisplayName = "32 inch Portrait"),
	Q_32_Landscape			UMETA(DisplayName = "32 inch Landscape"),
	Q_EightPointNineLegacy 	UMETA(DisplayName = "8.9 inch (Legacy)"),
	Q_Custom				UMETA(DisplayName = "Custom")
};


// Tiling Quality Settings Structure
// Default values are set for "Go Portrait" device
USTRUCT(BlueprintType)
struct FLookingGlassTilingQuality
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "TilingSettings", meta = (HideEditConditionToggle, EditCondition = "bTilingEditable", ClampMin = "1", ClampMax = "16", UIMin = "1", UIMax = "16"))
	int32 TilesX = 11;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "TilingSettings", meta = (HideEditConditionToggle, EditCondition = "bTilingEditable", ClampMin = "1", ClampMax = "160", UIMin = "1", UIMax = "16"))
	int32 TilesY = 6;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "TilingSettings", meta = (HideEditConditionToggle, EditCondition = "bTilingEditable", ClampMin = "512", ClampMax = "8192", UIMin = "512", UIMax = "8192"))
	int32 QuiltW = 4092;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "TilingSettings", meta = (HideEditConditionToggle, EditCondition = "bTilingEditable", ClampMin = "512", ClampMax = "8192", UIMin = "512", UIMax = "8192"))
	int32 QuiltH = 4092;

	// Aspect ratio of the camera. Value 0 has special meaning - the aspect will be taken from the device
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "TilingSettings", meta = (HideEditConditionToggle, EditCondition = "bTilingEditable", UIMin = "0.05", UIMax = "20"))
	float Aspect = 0.5625f;

	// Hidden property used to enable/disable editing of other properties
	UPROPERTY()
	bool bTilingEditable = false;

	int32 TileSizeX = 0;

	int32 TileSizeY = 0;

	float PortionX = 0.f;

	float PortionY = 0.f;

	UPROPERTY(VisibleAnywhere, Category = "TilingSettings")
	FString Name = TEXT("Default");

	FLookingGlassTilingQuality() {}

	FLookingGlassTilingQuality(FString InName, int InTilesX, int InTilesY, int InQuiltW, int InQuiltH, float InAspect, bool InEditable = false)
		: TilesX(InTilesX)
		, TilesY(InTilesY)
		, QuiltW(InQuiltW)
		, QuiltH(InQuiltH)
		, Aspect(InAspect)
		, bTilingEditable(InEditable)
		, Name(InName)
	{
		Setup();
	}

	void Setup()
	{
		// Compute tile size, in pixels
		TileSizeX = QuiltW / TilesX;
		TileSizeY = QuiltH / TilesY;
		// Compute fraction of quilt which is used for rendering
		PortionX = (float)TilesX * TileSizeX / (float)QuiltW;
		PortionY = (float)TilesY * TileSizeY / (float)QuiltH;
	}

	int32 GetNumTiles() const
	{
		return TilesX * TilesY;
	}

	bool operator==(const FLookingGlassTilingQuality& Other) const
	{
		return TilesX == Other.TilesX && TilesY == Other.TilesY && QuiltW == Other.QuiltW && QuiltH == Other.QuiltH;
	}
};

UENUM(BlueprintType, meta=(ScriptName = "LookingGlassPlacement"))
enum class ELookingGlassPlacement : uint8
{
	Automatic			UMETA(DisplayName="Automatically place on device"),
	AlwaysDebugWindow	UMETA(DisplayName="Always render in popup window")
};

USTRUCT(BlueprintType)
struct FLookingGlassWindowLocation
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Window")
	FIntPoint ClientSize;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Window")
	FIntPoint ScreenPosition;

	FLookingGlassWindowLocation()
		: ClientSize(2560, 1600)
		, ScreenPosition(2560, 0)
	{
	}

	FLookingGlassWindowLocation(FIntPoint InClientSize, FIntPoint InScreenPosition)
		: ClientSize(InClientSize)
		, ScreenPosition(InScreenPosition)
	{
	}
};

/**
 * @struct	FLookingGlassWindowSettings
 *
 * @brief	A LookingGlass window settings.
 */

USTRUCT(BlueprintType)
struct FLookingGlassWindowSettings
{
	GENERATED_BODY()

	// Where to place rendering window
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "LookingGlass|Window")
	ELookingGlassPlacement PlacementMode = ELookingGlassPlacement::Automatic;

	// Index of LookingGlass device where we'll render
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "LookingGlass|Window")
	int32 ScreenIndex = 0;

	// Size and location of the debug window, which is used when device is not present
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "LookingGlass|Window")
	FLookingGlassWindowLocation DebugWindowLocation = FLookingGlassWindowLocation(FIntPoint(800, 800), FIntPoint(200, 200));

	// Create topmost debug window
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "LookingGlass|Window")
	bool bToptmostDebugWindow = false;

	UPROPERTY()
	ELookingGlassModeType LastExecutedPlayModeType = ELookingGlassModeType::PlayMode_InSeparateWindow;

	UPROPERTY()
	bool bLockInMainViewport = false;
};


/**
 * @struct	FLookingGlassScreenshotSettings
 *
 * @brief	A LookingGlass screenshot settings.
 */

USTRUCT(BlueprintType)
struct FLookingGlassScreenshotSettings
{
	GENERATED_BODY()

	// Prefix of the generated screenshot file name
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "LookingGlass|Screenshot Settings")
	FString FileName = "";

	// When true, screen shots will be saved as jpg files
	UPROPERTY( BlueprintReadOnly, EditAnywhere, Category = "LookingGlass|Screenshot Settings" )
	bool UseJPG = false;

	// Quality of JPG screenshot
	UPROPERTY( BlueprintReadOnly, EditAnywhere, Category = "LookingGlass|Screenshot Settings" )
	int JpegQuality = 0;

	// Hotkey used to activate this screenshot
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "LookingGlass|Screenshot Settings")
	FKey InputKey = EKeys::F9;

	// Resolution of the generated image file
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "LookingGlass|Screenshot Settings", Meta = (EditCondition = "bResolutionVisible", HideEditConditionToggle, EditConditionHides, ClampMin = "0", ClampMax = "10000", UIMin = "0", UIMax = "10000"))
	FIntPoint Resolution = FIntPoint(0, 0);

	// Hidden property used to control visibility of "Resolution" property
	UPROPERTY()
	bool bResolutionVisible = false;

	FLookingGlassScreenshotSettings() {}

	FLookingGlassScreenshotSettings(FString InFileName, FKey InInputKey, int32 InScreenshotResolutionX = 0, int32 InScreenshotResolutionY = 0)
	{
		FileName = InFileName;
		InputKey = InInputKey;
		Resolution.X = InScreenshotResolutionX;
		Resolution.Y = InScreenshotResolutionY;
		bResolutionVisible = (InScreenshotResolutionX | InScreenshotResolutionY) != 0;
	}
};


/**
 * @struct	FLookingGlassRenderingSettings
 *
 * @brief	A LookingGlass rendering settings.
 * 			Contains options for disable some part of rendering and manage rendering pipeline
 */

USTRUCT(BlueprintType)
struct FLookingGlassRenderingSettings
{
	GENERATED_BODY()

	// This property controls r.vsync engine's cvar
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "LookingGlass|Rendering")
	bool bVsync = true;

	// Render quilt instead of hologram
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "LookingGlass|Rendering")
	bool QuiltMode = false;

	// Render regular "2D" image instead of hologram
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "LookingGlass|Rendering")
	bool bRender2D = false;

	void UpdateVsync() const;
};


UENUM(BlueprintType, meta = (ScriptName = "LookingGlassPerformanceMode"))
enum class ELookingGlassPerformanceMode : uint8
{
	// Realtime hologram
	Realtime,
	// Realtime mode with switching to 2D when user is interacting in editor
	RealtimeAdaptive,
	// Render only when scene changed
	NonRealtime,
};

/**
 * @struct	FLookingGlassRenderingSettings
 *
 * @brief	A LookingGlass rendering settings.
 * 			Contains options for disable some part of rendering and manage rendering pipeline
 */

USTRUCT(BlueprintType)
struct FLookingGlassEditorSettings
{
	GENERATED_BODY()

	// Adaptive performance behavior in editor
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "LookingGlass|Editor Settings")
	ELookingGlassPerformanceMode PerformanceMode = ELookingGlassPerformanceMode::NonRealtime;

	// Delay in seconds, after which hologram will be updated in Non-Relatime mode when scene edited
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "LookingGlass|Editor Settings")
	float NonRealtimeUpdateDelay = 0.5f;

	// Log all http requests made by Blocks code
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "LookingGlass|Editor Settings")
	bool bDebugBlocksRequests = false;
};


/**
 * @class	ULookingGlassSettings
 *
 * @brief	All LookingGlass plugin settings
 */

UCLASS(config = Engine, defaultconfig)
class LOOKINGGLASSRUNTIME_API ULookingGlassSettings : public UObject
{
public:
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, GlobalConfig, EditAnywhere, Category = "LookingGlass", Meta = (ShowOnlyInnerProperties))
	FLookingGlassWindowSettings LookingGlassWindowSettings;

	UPROPERTY(BlueprintReadOnly, GlobalConfig, EditAnywhere, Category = "LookingGlass|Editor Settings", Meta = (ShowOnlyInnerProperties))
	FLookingGlassEditorSettings LookingGlassEditorSettings;

	UPROPERTY(BlueprintReadOnly, GlobalConfig, EditAnywhere, Category = "LookingGlass|Screenshot Settings", Meta = (DisplayName = "Quilt Screenshot"))
	FLookingGlassScreenshotSettings LookingGlassScreenshotQuiltSettings = FLookingGlassScreenshotSettings("ScreenshotQuilt", EKeys::F9);

	UPROPERTY(BlueprintReadOnly, GlobalConfig, EditAnywhere, Category = "LookingGlass|Screenshot Settings", Meta = (DisplayName = "2D Screenshot"))
	FLookingGlassScreenshotSettings LookingGlassScreenshot2DSettings = FLookingGlassScreenshotSettings("Screenshot2D", EKeys::F8, 1280, 720);

    UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "LookingGlass|Tiling Settings", Meta = (DisplayName = "LookingGlass - Portrait"))
    FLookingGlassTilingQuality PortraitSettings = FLookingGlassTilingQuality("Portrait", 8, 6, 3360, 3360, 0.75f);

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "LookingGlass|Tiling Settings", Meta = (DisplayName = "LookingGlass - 16\" (4K)"))
	FLookingGlassTilingQuality _16In_Settings = FLookingGlassTilingQuality("4K Res", 5, 9, 4096, 4096, 1.77777f);

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "LookingGlass|Tiling Settings", Meta = (DisplayName = "LookingGlass - 32\" (8K)"))
	FLookingGlassTilingQuality _32In_Settings = FLookingGlassTilingQuality("8K Res", 5, 9, 8192, 8192, 1.77777f);

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "LookingGlass|Tiling Settings", Meta = (DisplayName = "LookingGlass - 65\""))
	FLookingGlassTilingQuality _65In_Settings = FLookingGlassTilingQuality("65 Inch", 8, 9, 8192, 8192, 1.77777f);

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "LookingGlass|Tiling Settings", Meta = (DisplayName = "LookingGlass - Prototype"))
	FLookingGlassTilingQuality _Prototype_Settings = FLookingGlassTilingQuality("Prototype", 5, 9, 4096, 4096, 1.77777f);

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "LookingGlass|Tiling Settings", Meta = (DisplayName = "LookingGlass - Go Portrait"))
	FLookingGlassTilingQuality _GoPortrait_Settings = FLookingGlassTilingQuality("Go Portrait", 11, 6, 4092, 4092, 0.5625f);

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "LookingGlass|Tiling Settings", Meta = (DisplayName = "LookingGlass - Kiosk"))
	FLookingGlassTilingQuality _Kiosk_Settings = FLookingGlassTilingQuality("Kiosk", 11, 6, 4096, 4096, 0.5625f);

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "LookingGlass|Tiling Settings", Meta = (DisplayName = "LookingGlass - 16\" Portrait"))
	FLookingGlassTilingQuality _16In_Portrait_Settings = FLookingGlassTilingQuality("16 Inch Portrait", 11, 6, 5995, 6000, 0.5625f);

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "LookingGlass|Tiling Settings", Meta = (DisplayName = "LookingGlass - 16\" Landscape"))
	FLookingGlassTilingQuality _16In_Landscape_Settings = FLookingGlassTilingQuality("16 Inch Landscape", 7, 7, 5999, 5999, 1.77777f);

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "LookingGlass|Tiling Settings", Meta = (DisplayName = "LookingGlass - 32\" Portrait"))
	FLookingGlassTilingQuality _32In_Portrait_Settings = FLookingGlassTilingQuality("32 Inch Portrait", 11, 6, 8184, 8184, 0.5625f);

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "LookingGlass|Tiling Settings", Meta = (DisplayName = "LookingGlass - 32\" Landscape"))
	FLookingGlassTilingQuality _32In_Landscape_Settings = FLookingGlassTilingQuality("32 Inch Landscape", 7, 7, 8190, 8190, 1.77777f);

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "LookingGlass|Tiling Settings", Meta = (DisplayName = "LookingGlass - 8.9\" (Legacy)"))
	FLookingGlassTilingQuality _8_9inLegacy_Settings = FLookingGlassTilingQuality("Extra Low", 5, 9, 4096, 4096, 1.6f);

	UPROPERTY(BlueprintReadOnly, GlobalConfig, EditAnywhere, Category = "LookingGlass|Tiling Settings", Meta = (DisplayName = "Custom"))
	FLookingGlassTilingQuality CustomSettings = FLookingGlassTilingQuality("Custom", 11, 6, 4092, 4092, 0.5625f, true);

	UPROPERTY(BlueprintReadOnly, GlobalConfig, EditAnywhere, Category = "LookingGlass", Meta = (ShowOnlyInnerProperties))
	FLookingGlassRenderingSettings LookingGlassRenderingSettings;

	ULookingGlassSettings()
	{
	}

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	FLookingGlassTilingQuality GetTilingQualityFor(ELookingGlassQualitySettings TilingSettings) const;

	/**
	 * @fn	virtual void ULookingGlassSettings::PostEngineInit() const;
	 *
	 * @brief	Called after engine initialized
	 */
	virtual void PostEngineInit() const
	{
		LookingGlassRenderingSettings.UpdateVsync();
	}

	/**
	 * @fn	void ULookingGlassSettings::LookingGlassSave()
	 *
	 * @brief	Custom UObject save
	 * 			in case of Build it will be save in Saved folder in Editor it will be stored in the Default config folder
	 */
	void LookingGlassSave();
};

UCLASS(config = Engine)
class LOOKINGGLASSRUNTIME_API ULookingGlassLaunchSettings : public UObject
{
public:
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Settings", GlobalConfig, VisibleAnywhere)
	int LaunchCounter = 0;
};
