#pragma once

#include "Stats/Stats.h"

DECLARE_STATS_GROUP(TEXT("LookingGlass_RenderThread"), STATGROUP_LookingGlass_RenderThread, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("CopyToQuiltShader"), STAT_CopyToQuiltShader_RenderThread, STATGROUP_LookingGlass_RenderThread);


DECLARE_STATS_GROUP(TEXT("LookingGlass_GameThread"), STATGROUP_LookingGlass_GameThread, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("Draw"), STAT_Draw_GameThread, STATGROUP_LookingGlass_GameThread);
DECLARE_CYCLE_STAT(TEXT("CaptureScene"), STAT_CaptureScene_GameThread, STATGROUP_LookingGlass_GameThread);
DECLARE_CYCLE_STAT(TEXT("DrawDebugParameters"), STAT_DrawDebugParameters_GameThread, STATGROUP_LookingGlass_GameThread);
