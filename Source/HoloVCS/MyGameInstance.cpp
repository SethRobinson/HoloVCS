#include "MyGameInstance.h"


//SETH:  If I don't set these, we can't get SetProcessDpiAwareness


#if PLATFORM_WINDOWS

#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"

#ifdef WINVER
#undef WINVER
#endif

#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif

#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
THIRD_PARTY_INCLUDES_START
#include "Windows/PreWindowsApi.h"
#include <objbase.h>
#include <assert.h>
#include <stdio.h>
#include "shellscalingapi.h"
#include "Windows/PostWindowsApi.h"
#include "Windows/MinWindows.h"

THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"
#endif
 
#include "Shared/UnrealMisc.h"

void UMyGameInstance::Init()
{
    UE_LOG(LogTemp, Warning, TEXT("Made it to Init"));
    
    UGameInstance::Init();

    //Window has not been created yet    
   // SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
   HRESULT r = SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
}
