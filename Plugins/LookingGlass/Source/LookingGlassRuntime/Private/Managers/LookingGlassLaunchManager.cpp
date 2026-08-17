#include "Managers/LookingGlassLaunchManager.h"

#include "LookingGlassSettings.h"

#include "Misc/LookingGlassLog.h"

#include "Runtime/Core/Public/Misc/CommandLine.h"


FLookingGlassLaunchManager::FLookingGlassLaunchManager()
{
}

FLookingGlassLaunchManager::~FLookingGlassLaunchManager()
{
}

bool FLookingGlassLaunchManager::Init()
{
	ULookingGlassSettings* LookingGlassSettings = GetMutableDefault<ULookingGlassSettings>();
	ULookingGlassLaunchSettings* LookingGlassLaunchSettings = GetMutableDefault<ULookingGlassLaunchSettings>();

// First run at the build
#if WITH_EDITOR
#else
	if (0 == LookingGlassLaunchSettings->LaunchCounter)
	{
		LookingGlassSettings->LookingGlassWindowSettings.bLockInMainViewport = true;
		LookingGlassSettings->LookingGlassSave();
	}
#endif

	// Increament Launch Conter
	LookingGlassLaunchSettings->LaunchCounter++;

	// Save Launch Config
	LookingGlassLaunchSettings->SaveConfig();

	return true;
}
