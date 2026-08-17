#pragma once

#include "ILookingGlassRuntime.h"

#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"

#include "LookingGlassBridge.h"

class FViewport;

class FLookingGlassCommandLineManager;
class FLookingGlassLaunchManager;
class ILookingGlassManager;
class ISequencer;

#define WITH_HOLOPLAYCORE
/**
 * @class	FLookingGlassLoader
 *
 * @brief	A LookingGlass DLL loader.
 */

class FLookingGlassLoader
{
public:

	/**
	 * @fn	bool FLookingGlassLoader::LoadDLL()
	 *
	 * @brief	Loads the LookingGlass calibration DLL. This is required only for working in Editor and Building Game
	 *
	 * @returns	True if it succeeds, false if it fails.
	 */

	bool LoadDLL(const FString& PluginBaseDir)
	{
#if defined(WITH_HOLOPLAYCORE)
		FString LookingGlassName = "LookingGlassCore";
#else
		FString LookingGlassName = "LookingGlass";
#endif
		FString PlatformName;
		FString LookingGlassDll;
#if PLATFORM_WINDOWS
		PlatformName = "Win64";
		LookingGlassDll = LookingGlassName + TEXT(".dll");
#elif PLATFORM_LINUX
		PlatformName = "linux64";
#elif PLATFORM_MAC
		PlatformName = "osx";
#endif // PLATFORM_WINDOWS

		// Pick the DLL from plugin's binaries directory (copied there from Build.cs using "RuntimeDependencies" function)
		FString DllPath = FPaths::Combine(PluginBaseDir, TEXT("Binaries"), PlatformName);

		FPlatformProcess::PushDllDirectory(*DllPath);
		FString DllFilePath = FPaths::Combine(DllPath, LookingGlassDll);
		LookingGlassDLL = FPlatformProcess::GetDllHandle(*DllFilePath);
		FPlatformProcess::PopDllDirectory(*DllPath);

		return LookingGlassDLL != nullptr;
	}

	/**
	 * @fn	bool FLookingGlassLoader::ReleaseDLL()
	 *
	 * @brief	Releases the LookingGlass calibration DLL
	 *
	 * @returns	True if it succeeds, false if it fails.
	 */

	bool ReleaseDLL()
	{
		if (LookingGlassDLL != nullptr)
		{
			FPlatformProcess::FreeDllHandle(LookingGlassDLL);

			return true;
		}

		return false;
	}

protected:
	void* LookingGlassDLL = nullptr;
};

/**
 * @class	FLookingGlassRuntimeModule
 *
 * @brief	A LookingGlass runtime module.
 * 			This is Main runtime plugin module
 */

class FLookingGlassRuntimeModule : public ILookingGlassRuntime
{
public:
	/**
	 * @fn	virtual void FLookingGlassRuntimeModule::StartupModule() override;
	 *
	 * @brief	Startup module, called on engine LoadingPhase
	 */

	virtual void StartupModule() override;

	/**
	 * @fn	virtual void FLookingGlassRuntimeModule::ShutdownModule() override;
	 *
	 * @brief	Shutdown module
	 */

	virtual void ShutdownModule() override;

	/**
	 * @fn	virtual void FLookingGlassRuntimeModule::StartPlayer(ELookingGlassModeType LookingGlassModeType) override;
	 *
	 * @brief	Starts a LookingGlass player
	 *
	 * @param	LookingGlassModeType	Type of the LookingGlass mode.
	 */

	virtual void StartPlayer(ELookingGlassModeType LookingGlassModeType) override;

	/**
	 * @fn	virtual void FLookingGlassRuntimeModule::StopPlayer() override;
	 *
	 * @brief	Stops a LookingGlass player
	 */

	virtual void StopPlayer() override;

	/**
	 * @fn	virtual void FLookingGlassRuntimeModule::RestartPlayer(ELookingGlassModeType LookingGlassModeType) override;
	 *
	 * @brief	Restart LookingGlass player
	 *
	 * @param	LookingGlassModeType	Type of the LookingGlass mode.
	 */

	virtual void RestartPlayer(ELookingGlassModeType LookingGlassModeType) override;

	/**
	 * @fn	virtual TSharedPtr<FLookingGlassCommandLineManager> FLookingGlassRuntimeModule::GetLookingGlassCommandLineManager() override;
	 *
	 * @brief	Gets LookingGlass command line manager
	 *
	 * @returns	The LookingGlass command line manager.
	 */

	virtual TSharedPtr<FLookingGlassCommandLineManager> GetLookingGlassCommandLineManager() override;

	virtual FOnLookingGlassInputKey& OnLookingGlassInputKeyDelegate() override { return OnLookingGlassInputKey; }

	virtual bool IsRenderingOnDevice() const override { return bIsRenderingOnDevice; }

	virtual const FLGDeviceCalibration& GetCurrentCalibration() const override { return CurrentCalibration; }

#if WITH_EDITOR
	virtual bool HasActiveSequencers() override;
#endif

	virtual FLookingGlassBridge& GetBridge() override
	{
		return Bridge;
	}

private:

	/**
	 * @fn	void FLookingGlassRuntimeModule::StartPlayerSeparateProccess();
	 *
	 * @brief	Starts player should be called when game is running in separate process or in the build
	 */

	void StartPlayerSeparateProccess();

	/**
	 * @fn	virtual void FLookingGlassRuntimeModule::OnWindowClosed(const TSharedRef<SWindow>& Window);
	 *
	 * @brief	Executes the window closed action
	 *
	 * @param	Window	The window.
	 */

	virtual void OnWindowClosed(const TSharedRef<SWindow>& Window);

	void OnDisplayMetricsChanged(const FDisplayMetrics& InDisplayMetrics);

	/**
	 * @fn	void FLookingGlassRuntimeModule::OnGameViewportCloseRequested(FViewport* InViewport);
	 *
	 * @brief	Executes the game viewport close requested action
	 *
	 * @param [in,out]	InViewport	If non-null, the in viewport.
	 */

	void OnGameViewportCloseRequested(FViewport* InViewport);

	/**
	 * @fn	void FLookingGlassRuntimeModule::OnGameViewportCreated();
	 *
	 * @brief	Executes the game viewport created action
	 * 			GEngine->GameViewport should be exists
	 */

	void OnGameViewportCreated();

	void PrepareDisplays();

	void InitAllManagers();

	void OnSequencerCreated(TSharedRef<ISequencer> InSequencer);

	void MakeDefaultCalibration();

	// Number of attached monitors. Used to recognize that need to re-read LG configuration.
	int32 NumMonitors = 0;

	bool bIsRenderingOnDevice = false;

	FLGDeviceCalibration CurrentCalibration;

	TSharedPtr<FLookingGlassCommandLineManager> LookingGlassCommandLineManager;
	TSharedPtr<FLookingGlassLaunchManager> LookingGlassLaunchManager;
	bool bSeparateProccessPlayerStarded = false;

	FLookingGlassBridge Bridge;
	FLookingGlassLoader LookingGlassLoader;
	FCriticalSection LookingGlassCritialSection;

	TArray<TSharedPtr<ILookingGlassManager>> Managers;
	bool bIsManagerInit = false;

#if WITH_EDITOR
	// Sequences which were captured with OnSequencerCreated()
	TArray<TWeakPtr<ISequencer>> Sequencers;
#endif

	// Delegates
	FOnLookingGlassInputKey OnLookingGlassInputKey;
#if WITH_EDITOR
	FDelegateHandle OnSequencerCreatedHandle;
#endif
};
