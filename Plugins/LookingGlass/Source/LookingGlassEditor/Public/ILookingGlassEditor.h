#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"


//-------------------------------------------------------------------------------------------------
// ILookingGlassEditor Module
//-------------------------------------------------------------------------------------------------

/**
 * @class	ILookingGlassEditor
 *
 * @brief	The public interface to this module.  In most cases, this interface is only public to
 * 			sibling modules within this plugin.
 */

class ILookingGlassEditor : public IModuleInterface
{
public:

	/**
	 * @fn	static inline ILookingGlassEditor& ILookingGlassEditor::Get()
	 *
	 * @brief	Singleton-like access to this module's interface.  This is just for convenience!
	 * 			Beware of calling this during the shutdown phase, though.  Your module might have
	 * 			been unloaded already.
	 *
	 * @returns	Returns singleton instance, loading the module on demand if needed.
	 */

	static inline ILookingGlassEditor& Get()
	{
		return FModuleManager::LoadModuleChecked< ILookingGlassEditor >("LookingGlassEditor");
	}

	/**
	 * @fn	static inline bool ILookingGlassEditor::IsAvailable()
	 *
	 * @brief	Checks to see if this module is loaded and ready.  It is only valid to call Get() if
	 * 			IsAvailable() returns true.
	 *
	 * @returns	True if the module is loaded and ready to use.
	 */

	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("LookingGlassEditor");
	}

	virtual class FLookingGlassBlocksInterface& GetBlocksInterface() = 0;
};
