#pragma once

#include "CoreMinimal.h"
#include "LookingGlassSettings.h"
#include "Tickable.h"

/**
 * @class	ILookingGlassManager
 *
 * @brief	Managers Interface, should be implemented at all children
 */

class ILookingGlassManager : public FTickableGameObject
{
public:

	/**
	 * @fn	virtual ILookingGlassManager::~ILookingGlassManager() = 0
	 *
	 * @brief	Destructor
	 */

	virtual ~ILookingGlassManager() = 0
	{ }

	/**
	 * @fn	virtual bool ILookingGlassManager::Init()
	 *
	 * @brief	Initializes this Manager class
	 *
	 * @returns	True if it Initializes successful , false if it fails.
	 */

	virtual bool Init() { return true; }

	/**
	 * @fn	virtual void ILookingGlassManager::Release()
	 *
	 * @brief	Releases Manager and all resources
	 */

	virtual void Release() { }

	/**
	 * @fn	virtual bool ILookingGlassManager::OnStartPlayer(ELookingGlassModeType LookingGlassModeType)
	 *
	 * @brief	Executes the start player action
	 *
	 * @param	LookingGlassModeType	Type of the LookingGlass mode.
	 *
	 * @returns	True if it succeeds, false if it fails.
	 */

	virtual bool OnStartPlayer(ELookingGlassModeType LookingGlassModeType) { return true; }

	/**
	 * @fn	virtual void ILookingGlassManager::OnStopPlayer()
	 *
	 * @brief	Executes the stop player action
	 */

	virtual void OnStopPlayer() {}

	// FTickableGameObject interface
	virtual void Tick(float DeltaTime) override {};

	virtual ETickableTickType GetTickableTickType() const override
	{
		return ETickableTickType::Always;
	}
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(UMIDIDeviceManager, STATGROUP_Tickables);
	}
	virtual bool IsTickableWhenPaused() const
	{
		return true;
	}
	virtual bool IsTickableInEditor() const
	{
		return true;
	}
};