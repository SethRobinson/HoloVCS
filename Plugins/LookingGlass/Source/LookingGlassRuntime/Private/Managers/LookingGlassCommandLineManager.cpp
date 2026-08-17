#include "Managers/LookingGlassCommandLineManager.h"
#include "LookingGlassSettings.h"
#include "Misc/LookingGlassLog.h"
#include "ILookingGlassRuntime.h"

#include "Runtime/Core/Public/Misc/CommandLine.h"


FLookingGlassCommandLineManager::FLookingGlassCommandLineManager()
{
}

FLookingGlassCommandLineManager::~FLookingGlassCommandLineManager()
{
}

bool FLookingGlassCommandLineManager::Init()
{
	ULookingGlassSettings* LookingGlassSettings = GetMutableDefault<ULookingGlassSettings>();

	uint32 WindowType = 0;
	uint32 LastExecutedPlayModeType = 0;
	uint32 LockInMainViewport = 0;
	uint32 PlacementInLookingGlass = 0;
	uint32 ScreenIndex = 0;
	uint32 QuiltMode = 0;

	int ShouldSave = 0;

	if (FParse::Value(FCommandLine::Get(), TEXT("hp_playmodetype="), LastExecutedPlayModeType))
	{
		LookingGlassSettings->LookingGlassWindowSettings.LastExecutedPlayModeType = static_cast<ELookingGlassModeType>(LastExecutedPlayModeType);
		ShouldSave++;
	}

	if (FParse::Value(FCommandLine::Get(), TEXT("hp_lockinmainviewport="), LockInMainViewport))
	{
		LookingGlassSettings->LookingGlassWindowSettings.bLockInMainViewport = !!LockInMainViewport;
		ShouldSave++;
	}

	if (FParse::Value(FCommandLine::Get(), TEXT("hp_playcement="), PlacementInLookingGlass))
	{
		//todo: validate, or use enum names here
		LookingGlassSettings->LookingGlassWindowSettings.PlacementMode = (ELookingGlassPlacement)PlacementInLookingGlass;
		ShouldSave++;
	}

	if (FParse::Value(FCommandLine::Get(), TEXT("hp_screenindex="), ScreenIndex))
	{
		LookingGlassSettings->LookingGlassWindowSettings.ScreenIndex = ScreenIndex;
		ShouldSave++;
	}

	if (FParse::Value(FCommandLine::Get(), TEXT("hp_quiltmode="), QuiltMode))
	{
		LookingGlassSettings->LookingGlassRenderingSettings.QuiltMode = (QuiltMode != 0);
		ShouldSave++;
	}

	if (ShouldSave > 0)
	{
		LookingGlassSettings->LookingGlassSave();
	}

	return true;
}
