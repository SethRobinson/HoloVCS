#pragma once

#include "CoreMinimal.h"

// Plugin-wide log categories
#if UE_BUILD_SHIPPING
DECLARE_LOG_CATEGORY_EXTERN(LookingGlassLogBlueprints, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LookingGlassLogGame, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LookingGlassLogManagers, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LookingGlassLogPlayer, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LookingGlassLogRender, Warning, Warning);
DECLARE_LOG_CATEGORY_EXTERN(LookingGlassLogInput, Warning, Warning);
DECLARE_LOG_CATEGORY_EXTERN(LookingGlassLogSettings, Warning, Warning);
#else
DECLARE_LOG_CATEGORY_EXTERN(LookingGlassLogBlueprints, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LookingGlassLogGame, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LookingGlassLogManagers, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LookingGlassLogPlayer, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LookingGlassLogRender, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LookingGlassLogInput, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LookingGlassLogSettings, Log, All);
#endif

#if UE_BUILD_SHIPPING
#define DISPLAY_HOLOPLAY_FUNC_TRACE(cat) ;
#else
#if PLATFORM_WINDOWS
#define DISPLAY_HOLOPLAY_FUNC_TRACE(cat)  UE_LOG(cat, VeryVerbose, TEXT(">> %s"), ANSI_TO_TCHAR(__FUNCTION__))
#else
#define DISPLAY_CLUSTER_FUNC_TRACE(cat) ;
#endif // PLATFORM_WINDOWS
#endif // UE_BUILD_SHIPPING

