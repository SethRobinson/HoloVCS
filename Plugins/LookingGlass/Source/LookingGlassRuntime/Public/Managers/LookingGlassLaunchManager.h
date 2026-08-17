#pragma once

#include "CoreMinimal.h"
#include "Managers/ILookingGlassManager.h"

/**
 * @class	FLookingGlassCommandLineManager
 *
 * @brief	Manager for LookingGlass command lines.
 */

class FLookingGlassLaunchManager : public ILookingGlassManager
{
public:
	FLookingGlassLaunchManager();
	virtual ~FLookingGlassLaunchManager();
	/** ILookingGlassManager Interface */

	/**
	 * @fn	virtual bool FLookingGlassCommandLineManager::Init() override;
	 *
	 * @brief	Initializes this Manager class
	 *
	 * @returns	True if it Initializes successful , false if it fails.
	 */

	virtual bool Init() override;
	/** ILookingGlassManager Interface */
};