#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"

class FRunnableThread;
class FEvent;
class FLookingGlassBridgeThread;

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
	// Desktop position of the display's top-left corner (for placing the self-render window)
	int32 XPos = 0;
	int32 YPos = 0;
};

/**
 * ALL Bridge SDK calls live on one dedicated thread (created in Initialize). The Bridge service
 * stalls its calls for 0.7-20 seconds in the field (measured; DrawInteropQuiltTextureDX itself),
 * and the SDK has hard thread affinity - every call must come from the thread that initialized
 * the controller. So the controller is initialized on our own bridge thread, the thread pumps
 * Windows messages for the window it creates, and stalls only ever block that thread: the game
 * keeps running and the hologram just pauses.
 *
 * Game-thread API: Initialize/Shutdown block (boot/exit only). QueueDraw is fire-and-forget
 * latest-wins. Displays/CalibrationTemplates are filled during the blocking Initialize and only
 * refreshed asynchronously on display hot-plug (rare; reads of stale data are harmless).
 */
struct FLookingGlassBridge
{
public:
	bool Initialize();

	void Shutdown();

	void RequestReadDisplays();

	void RequestStopRendering();

	bool IsRendering()
	{
		return (TextureRegistered != nullptr);
	}

	/** Queues the frame for the bridge thread and returns immediately (latest-wins). Also starts
	 *  the bridge window on first use. */
	void QueueDraw(void* Texture, int32 QuiltDX, int32 QuiltDY, float Aspect);

	/** True if a bridge draw call has been blocked for longer than StuckSeconds - used by the
	 *  mid-stall stack capture diagnostic */
	bool IsDrawStuck(double StuckSeconds, uint32& OutBridgeThreadId) const;

	/** Appends a line to lkg_diag.txt next to the top-level exe (the only plugin log that survives
	 *  Shipping builds, where UE_LOG is compiled out). Also UE_LOGs it. */
	static void Diag(const FString& Message);

	bool bInitialized = false;

	// Where bridge_inproc.dll was loaded from and the Bridge version it reported (diagnostics)
	FString InstallDir;
	FString VersionString;

	TArray<FLGDeviceCalibration> Displays;

	TArray<FLGDeviceCalibration> CalibrationTemplates;

protected:
	friend class FLookingGlassBridgeThread;

	// Everything below runs ONLY on the bridge thread
	bool Initialize_BridgeThread();
	void ReadDisplays_BridgeThread();
	void StartRendering_BridgeThread();
	void StopRendering_BridgeThread();
	void DrawTexture_BridgeThread(void* Texture, int32 QuiltDX, int32 QuiltDY, float Aspect);

	static const uint32 NoWindow = 0xffffffff;

	uint32 LGWindow = NoWindow;

	void* TextureRegistered = nullptr;

	class ControllerWithCalibrationTemplates* BridgeController = nullptr;

	FLookingGlassBridgeThread* BridgeThread = nullptr;
};
