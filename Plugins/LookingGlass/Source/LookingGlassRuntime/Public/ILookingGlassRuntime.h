#pragma once

#include "CoreMinimal.h"

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

#include "ILookingGlassPlayer.h"

class ILookingGlassRuntime;
struct FLookingGlassBridge;
struct FLGDeviceCalibration;

//-------------------------------------------------------------------------------------------------
// ILookingGlassRuntime Module
//-------------------------------------------------------------------------------------------------

/**
 * @class	ILookingGlassRuntime
 *
 * @brief	The public interface to this module.  In most cases, this interface is only public to
 * 			sibling modules within this plugin.
 */

class ILookingGlassRuntime : public IModuleInterface, public ILookingGlassPlayer
{
public:

	/**
	 * @fn	static inline ILookingGlassRuntime& ILookingGlassRuntime::Get()
	 *
	 * @brief	Singleton-like access to this module's interface.  This is just for convenience!
	 * 			Beware of calling this during the shutdown phase, though.  Your module might have
	 * 			been unloaded already.
	 *
	 * @returns	Returns singleton instance, loading the module on demand if needed.
	 */

	static inline ILookingGlassRuntime& Get()
	{
		return FModuleManager::LoadModuleChecked< ILookingGlassRuntime >("LookingGlassRuntime");
	}

	virtual FLookingGlassBridge& GetBridge() = 0;

	virtual bool IsRenderingOnDevice() const = 0;

	virtual const FLGDeviceCalibration& GetCurrentCalibration() const = 0;

	/**
	 * @fn	static inline bool ILookingGlassRuntime::IsAvailable()
	 *
	 * @brief	Checks to see if this module is loaded and ready.  It is only valid to call Get() if
	 * 			IsAvailable() returns true.
	 *
	 * @returns	True if the module is loaded and ready to use.
	 */

	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("LookingGlassRuntime");
	}

#if WITH_EDITOR
	// Returns true if sequencer window is open
	virtual bool HasActiveSequencers()
	{
		return false;
	}
#endif
};
