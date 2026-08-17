#pragma once

#include "CoreMinimal.h"

struct FLGDeviceCalibration
{
	// Human-readable device name
	FString Name;
	// Internal identifier of the device
	FString Serial;

	float Center = 0;
	float Pitch = 0;
	float Slope = 0;
	float DPI = 0;
	float FlipX = 0;
	int32 Width = 0;
	int32 Height = 0;
	float Aspect = 0;
	float ViewCone = 0;
};

struct FLookingGlassBridge
{
public:
	bool Initialize();

	void Shutdown();

	void ReadDisplays();

	bool IsRendering()
	{
		return (TextureRegistered != nullptr);
	}

	void StartRendering();

	void StopRendering();

	void DrawTexture(void* Texture, int32 QuiltDX, int32 QuiltDY, float Aspect);

	bool bInitialized = false;

	TArray<FLGDeviceCalibration> Displays;

	TArray<FLGDeviceCalibration> CalibrationTemplates;

protected:
	static const uint32 NoWindow = 0xffffffff;

	uint32 LGWindow = NoWindow;

	void* TextureRegistered = nullptr;

	class ControllerWithCalibrationTemplates* BridgeController = nullptr;
};
