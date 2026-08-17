#pragma once

#include "CoreMinimal.h"
#include "Managers/ILookingGlassManager.h"

/**
 * @class	FLookingGlassCommandLineManager
 *
 * @brief	Manager for LookingGlass command lines.
 */

class FLookingGlassCommandLineManager : public ILookingGlassManager
{
public:
	FLookingGlassCommandLineManager();
	virtual ~FLookingGlassCommandLineManager();
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