#pragma once

#include "Widgets/SWindow.h"
#include "LookingGlassSettings.h"
#include "Engine/EngineBaseTypes.h"

class SLookingGlassViewport;
class UTextureRenderTarget2D;
class ULookingGlassSceneCaptureComponent2D;
class FLookingGlassCommandLineManager;
class FViewport;

/**
 * @class	ILookingGlassPlayer
 *
 * @brief	A LookingGlass player interface. Should be implemented at all children
 */

//todo: strange class, used only as parent for ILookingGlassRuntime
class ILookingGlassPlayer
{
public:
	virtual ~ILookingGlassPlayer() {}

	/**
	 * @fn	virtual void ILookingGlassPlayer::StartPlayer(ELookingGlassModeType LookingGlassModeType) = 0;
	 *
	 * @brief	Starts a LookingGlass player
	 *
	 * @param	LookingGlassModeType	Type of the LookingGlass mode.
	 */

	virtual void StartPlayer(ELookingGlassModeType LookingGlassModeType) = 0;

	/**
	 * @fn	virtual void ILookingGlassPlayer::StopPlayer() = 0;
	 *
	 * @brief	Stops a LookingGlass player
	 */

	virtual void StopPlayer() = 0;

	/**
	 * @fn	virtual void ILookingGlassPlayer::RestartPlayer(ELookingGlassModeType LookingGlassModeType) = 0;
	 *
	 * @brief	Restart LookingGlass player
	 *
	 * @param	LookingGlassModeType	Type of the LookingGlass mode.
	 */

	virtual void RestartPlayer(ELookingGlassModeType LookingGlassModeType) = 0;

	/**
	 * @fn	virtual bool ILookingGlassPlayer::IsPlaying()
	 *
	 * @brief	Query if this LookingGlass is playing
	 *
	 * @returns	True if playing, false if not.
	 */

	virtual bool IsPlaying() { return bIsPlaying; }

	/**
	 * @fn	virtual bool ILookingGlassPlayer::IsDestroyWindowRequested()
	 *
	 * @brief	Query if this object is destroy window requested
	 *
	 * @returns	True if destroy window requested, false if not.
	 */

	virtual bool IsDestroyWindowRequested() { return bIsDestroyWindowRequested; }

	/**
	 * @fn	virtual bool ILookingGlassPlayer::IsStandaloneGame()
	 *
	 * @brief	Query if engine instance is standalone game
	 *
	 * @returns	True if standalone game, false if not.
	 */

	virtual bool IsStandaloneGame() { return bIsStandaloneGame; }

	/**
	 * @fn	virtual bool ILookingGlassPlayer::IsGameMode()
	 *
	 * @brief	Query if engine instance is game mode
	 *
	 * @returns	True if game mode, false if not.
	 */

	virtual bool IsGameMode() { return bIsGameMode; }

	/**
	 * @fn	virtual bool ILookingGlassPlayer::IsCaptureStandaloneMovie()
	 *
	 * @brief	Query if engine instance is capture standalone movie
	 *
	 * @returns	True if capture standalone movie, false if not.
	 */

	virtual bool IsCaptureStandaloneMovie() { return bIsCaptureStandaloneMovie; }

	/**
	 * @fn	virtual ELookingGlassModeType ILookingGlassPlayer::GetCurrentLookingGlassModeType()
	 *
	 * @brief	Gets current LookingGlass mode type
	 *
	 * @returns	The current LookingGlass mode type.
	 */

	virtual ELookingGlassModeType GetCurrentLookingGlassModeType() { return CurrentLookingGlassModeType; }

	/**
	 * @fn	virtual TSharedPtr<FLookingGlassCommandLineManager> ILookingGlassPlayer::GetLookingGlassCommandLineManager() = 0;
	 *
	 * @brief	Gets LookingGlass command line manager
	 *
	 * @returns	The LookingGlass command line manager.
	 */

	virtual TSharedPtr<FLookingGlassCommandLineManager> GetLookingGlassCommandLineManager() = 0;

	DECLARE_MULTICAST_DELEGATE_SixParams(FOnLookingGlassInputKey, FViewport*, int32, FKey, EInputEvent, float, bool);
	/**
	 * @fn	virtual FOnLookingGlassInputKey& ILookingGlassPlayer::OnLookingGlassInputKeyDelegate() = 0;
	 *
	 * @brief	Broadcast LookingGlass viewport input event 
	 *
	 * @returns Input delegate
	 */
	virtual FOnLookingGlassInputKey& OnLookingGlassInputKeyDelegate() = 0;

	TSharedPtr<SLookingGlassViewport> GetLookingGlassViewport()
	{
		return LookingGlassViewport;
	}

protected:

	/**
	 * @fn	virtual void ILookingGlassPlayer::OnWindowClosed(const TSharedRef<SWindow>& Window)
	 *
	 * @brief	Executes the window closed action
	 *
	 * @param	Window	The window.
	 */

	virtual void OnWindowClosed(const TSharedRef<SWindow>& Window) {};

protected:
	bool bIsPlaying = false;
	bool bIsDestroyWindowRequested = false;
	bool bIsStandaloneGame = false;
	bool bIsGameMode = false;
	bool bLockInMainViewport = false;

	bool bIsCaptureStandaloneMovie = false;
	ELookingGlassModeType CurrentLookingGlassModeType = ELookingGlassModeType::PlayMode_InSeparateWindow;

	TSharedPtr<SWindow> LookingGlassWindow = nullptr;
	TSharedPtr<SLookingGlassViewport> LookingGlassViewport = nullptr;

	FOnWindowClosed OnWindowClosedDelegate;

public:
#if WITH_EDITOR
	TArray<TWeakObjectPtr<ULookingGlassSceneCaptureComponent2D> > EditorLookingGlassCaptureComponents;
#endif
	TArray<TWeakObjectPtr<ULookingGlassSceneCaptureComponent2D> > GameLookingGlassCaptureComponents;
};
