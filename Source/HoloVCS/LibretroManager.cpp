#include "LibretroManager.h"
#include "LibretroManagerActor.h"
#include "PlayerPawn.h"
#include "StatusDisplayActor.h" //so we can show messages on screen
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

using std::vector;
using std::string;

//SETH:  If I don't set these, we can't get SetProcessDpiAwareness

#if UE_BUILD_DEVELOPMENT
const int C_DEFAULT_ROM_ID = 2;
bool g_loadStateOnFirstLoad = true;
string g_partialRomNameToLoadOnStartup = "astle";
//string g_partialRomNameToLoadOnStartup = "";
#else

const int C_DEFAULT_ROM_ID = 2;
bool g_loadStateOnFirstLoad = true;
string g_partialRomNameToLoadOnStartup = "astle";
#endif

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
//#include "Windows/PreWindowsApi.h"
//#include <objbase.h>
#include <assert.h>
#include <stdio.h>
//#include "shellscalingapi.h"
//#include "Windows/PostWindowsApi.h"
//#include "Windows/MinWindows.h"

THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"

#endif

#include "Shared/UnrealMisc.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/SWindow.h"
#include "GenericPlatform/GenericWindow.h"

#pragma warning(disable:4191)

//Hard-confine the OS cursor to the game window while the 3DS touch cursor is in use, so a
//click can never land on the desktop and steal focus.  The clip is only APPLIED when it is
//missing or wrong (Windows clears clips on focus changes) - never re-applied per frame,
//which is the documented ~90ms-stall trap (see AGENTS.md audio section).
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
void HoloConfineMouseToGameWindow(bool bEnable)
{
	static bool bWasClipped = false;
	HWND hwnd = NULL;
	if (GEngine && GEngine->GameViewport)
	{
		TSharedPtr<SWindow> pWindow = GEngine->GameViewport->GetWindow();
		if (pWindow.IsValid() && pWindow->GetNativeWindow().IsValid())
		{
			hwnd = (HWND)pWindow->GetNativeWindow()->GetOSWindowHandle();
		}
	}
	const bool bWant = bEnable && hwnd && GetForegroundWindow() == hwnd;
	if (!bWant)
	{
		if (bWasClipped)
		{
			ClipCursor(NULL);
			bWasClipped = false;
		}
		return;
	}
	RECT rClient;
	GetClientRect(hwnd, &rClient);
	POINT tl = { rClient.left, rClient.top };
	POINT br = { rClient.right, rClient.bottom };
	ClientToScreen(hwnd, &tl);
	ClientToScreen(hwnd, &br);
	RECT rWant = { tl.x, tl.y, br.x, br.y };
	RECT rCur;
	GetClipCursor(&rCur);
	if (!EqualRect(&rCur, &rWant))
	{
		ClipCursor(&rWant);
	}
	bWasClipped = true;
}
#include "Windows/HideWindowsPlatformTypes.h"
#else
void HoloConfineMouseToGameWindow(bool) {}
#endif

const unsigned short ASYNC_BUTTON_DOWN_MSB = 0x8000;

string G_VERSION_STRING = "HoloVCS V1.5";

LibretroManager* g_pLibretroManager = NULL; //I don't want to fool with caring how to get Unreal globals correctly
void retro_video_refresh_callback(const void* data, unsigned width, unsigned height, size_t pitch);
void retro_video_refresh_callback_ex(const void* data, unsigned width, unsigned height, size_t pitch, const void* extradata);
void retro_video_refresh_callback_holo(const void* data, unsigned width, unsigned height, size_t pitch, const HoloLayerInfo* info);
void DrawTouchCursor(uint8* pDstBase, int texWidth, int texHeight, int texPitchBytes);
#include <thread>

bool retro_environment_callback(unsigned cmd, void* data);

//when starting/stopping in the editor, globals don't get reverted back, so we'll do it manually and trust this is called at some point

void OnWasRestartedInEditor()
{
#if UE_BUILD_DEBUG
	g_loadStateOnFirstLoad = true;
#endif
}

void JoyPadButtonStates::Clear()
{
	for (int i = 0; i < C_MAX_JOYPAD_BUTTONS; i++)
	{
		m_button[i] = false;
	}
	m_axisLX = m_axisLY = m_axisRX = m_axisRY = 0;
}

LibretroManager::LibretroManager()
{
	for (int i = 0; i < C_SAVE_STATE_COUNT; i++)
	{
		m_pSaveStateBuffer[i] = NULL;
	}
	for (int i = 0; i < C_TOUCH_HISTORY; i++)
	{
		m_touchHistX[i] = m_touchX;
		m_touchHistY[i] = m_touchY;
	}
}

void LibretroManager::RecordTouchHistory()
{
	m_touchHistPos = (m_touchHistPos + 1) % C_TOUCH_HISTORY;
	m_touchHistX[m_touchHistPos] = m_touchX;
	m_touchHistY[m_touchHistPos] = m_touchY;

	//a held touch stays at the latch until the cursor clearly moves away - that's a
	//deliberate drag, so hand control back to the live position
	if (m_touchDown && m_touchLatched)
	{
		const float dx = m_touchX - m_touchLatchX;
		const float dy = m_touchY - m_touchLatchY;
		if (dx * dx + dy * dy > 15.0f * 15.0f) m_touchLatched = false;
	}
}

void LibretroManager::SetTouchDown(bool bDown)
{
	if (bDown && !m_touchDown)
	{
		const int framesBack = 5; //~83ms at 60fps, roughly the cursor's display latency
		const int idx = (m_touchHistPos - framesBack + C_TOUCH_HISTORY) % C_TOUCH_HISTORY;
		m_touchLatchX = m_touchHistX[idx];
		m_touchLatchY = m_touchHistY[idx];
		m_touchLatched = true;
		m_touchLastActiveTime = FPlatformTime::Seconds();
	}
	if (!bDown) m_touchLatched = false;
	m_touchDown = bDown;
}

float LibretroManager::GetTouchPointX()
{
	return (m_touchDown && m_touchLatched) ? m_touchLatchX : m_touchX;
}

float LibretroManager::GetTouchPointY()
{
	return (m_touchDown && m_touchLatched) ? m_touchLatchY : m_touchY;
}

LibretroManager::~LibretroManager()
{
	Kill();

	//Only clear the global if we're actually the one it points to.  Stale copies exist inside actor CDOs and
	//blueprint reinstancing garbage, and when GC purges one of those (first purge is ~61 secs in) it must not
	//null the global out from under the live instance.  (That was a crash on 5.8.)
	if (g_pLibretroManager == this) g_pLibretroManager = NULL;

	for (int i = 0; i < C_SAVE_STATE_COUNT; i++)
	{
		SAFE_DELETE_ARRAY(m_pSaveStateBuffer[i]);
	}
}

void LibretroManager::Kill()
{
	FreeEmulatorIfNeeded();
}

void LibretroManager::ModEmulatorType(int mod)
{
	LogMsg("Modding emu by %d", mod);
}

void LibretroManager::ModRom(int mod)
{
	LogMsg("Modding rom by %d", mod);
	m_activeRomIndex += mod;
	if (m_activeRomIndex >= m_romNameFileList.Num())
	{
		m_activeRomIndex = 0;
	}

	if (m_activeRomIndex < 0)
	{
		m_activeRomIndex = m_romNameFileList.Num() - 1;
	}

	//trigger the whole reload thing
	InitEmulator();
}
 
void LibretroManager::DisableBlitPass(int blitPassIndex)
{
	m_blitPass[blitPassIndex].m_bActive = false;
}

void LibretroManager::SetupBlitPass(int blitPassIndex, int layer, FIntRect srcRect, eColorKeyStyle colorKeyStyle, FLinearColor colorKey, FLinearColor colorKey2)
{
	m_blitPass[blitPassIndex].m_bActive = true;
	m_blitPass[blitPassIndex].m_bUseNesTileMask = false;
	m_blitPass[blitPassIndex].m_activeLayerIndex = layer;
	m_blitPass[blitPassIndex].m_blitColorKeyStyle = colorKeyStyle;
	m_blitPass[blitPassIndex].m_blitSrcRect = srcRect;
	m_blitPass[blitPassIndex].m_blitColorKey = colorKey;
	m_blitPass[blitPassIndex].m_blitColorKey2 = colorKey2;
	if (layer < m_pLibretroManagedActor->m_layerInfo.size())
		m_pLibretroManagedActor->GetLayer(layer)->m_bUsedThisFrame = true; //alerts the renderex system to it being used already
}

void LibretroManager::SetupNesTileFilteredBlitPass(int blitPassIndex, int layer, FIntRect srcRect,
	eColorKeyStyle colorKeyStyle, FLinearColor colorKey, FLinearColor colorKey2,
	const byte* pKeepList, int keepListSize)
{
	SetupBlitPass(blitPassIndex, layer, srcRect, colorKeyStyle, colorKey, colorKey2);
	BlitPass& blitPass = m_blitPass[blitPassIndex];
	blitPass.m_bUseNesTileMask = true;
	memset(blitPass.m_nesTileMask, 0, sizeof(blitPass.m_nesTileMask));

	for (int i = 0; i < keepListSize; i++)
	{
		const uint8 tile = pKeepList[i];
		blitPass.m_nesTileMask[tile >> 3] |= (uint8)(1U << (tile & 7));
	}
}

#define GET_VARIABLE_NAME(Variable) (#Variable)

//only have this if not static
#ifndef RT_STATIC_CORE

FARPROC MapFunction(HINSTANCE m_dllHandle, const char* varName)
{
	varName++;
	//never do it like this
	while (varName[-1] != '.')
	{
		varName++;
	}

	auto temp = GetProcAddress(m_dllHandle, varName);
	if (!temp)
	{
		LogMsg("Couldn't find func %s", varName);
	}

	return temp;
}
#endif


void LibretroManager::FreeEmulatorIfNeeded()
{
#ifdef RT_STATIC_CORE
	if (m_core.m_bActive)
	{
		m_core.retro_unload_game();
		m_core.retro_deinit();
		m_core.m_bActive = false;
	}
#else
	if (m_core.m_bActive && m_dllHandle != NULL)
	{
		LogMsg("Unloading emulator");
		m_core.retro_unload_game();
		m_core.retro_deinit();
		m_core.m_bActive = false;
	} 

	if (m_dllHandle)
	{
		LogMsg("Freeing emulator dll");
		FreeLibrary(m_dllHandle);
	}

	m_dllHandle = NULL;
	m_core.retro_set_holo_bg_tile_filter = nullptr;
	m_core.retro_get_holo_bg_tile_ids = nullptr;

#endif
}
 
bool LibretroManager::LoadCore(string fileName)
{
	FreeEmulatorIfNeeded();

#ifdef RT_STATIC_CORE

	if (fileName != "fceumm_libretro.dll")
	{
		LogMsg("Error, only fceumm is supported in static mode");
		//we don't support this emulator yet in static mode
		return false;
	}

	// Initialize the core's function pointers directly
	m_core.retro_api_version = retro_api_version;
	m_core.retro_get_system_info = retro_get_system_info;
	m_core.retro_init = retro_init;
	m_core.retro_deinit = retro_deinit;
	m_core.retro_reset = retro_reset;
	m_core.retro_set_environment = retro_set_environment;
	m_core.retro_set_video_refresh = retro_set_video_refresh;
	m_core.retro_set_audio_sample = retro_set_audio_sample;
	m_core.retro_set_audio_sample_batch = retro_set_audio_sample_batch;
	m_core.retro_set_input_poll = retro_set_input_poll;
	m_core.retro_set_input_state = retro_set_input_state;
	m_core.retro_load_game = retro_load_game;
	m_core.retro_get_system_av_info = retro_get_system_av_info;
	m_core.retro_run = retro_run;
	m_core.retro_unload_game = retro_unload_game;
	m_core.retro_serialize_size = retro_serialize_size;
	m_core.retro_serialize = retro_serialize;
	m_core.retro_unserialize = retro_unserialize;

	m_core.retro_set_environment(retro_environment_callback);


	// Call the core's initialization function
	m_core.retro_init();

#else

	m_dllHandle = LoadLibraryA(fileName.c_str());

	if (!m_dllHandle)
	{
		//In editor runs the .exe is UnrealEditor.exe way over in the engine dir, so the default search
		//path misses our project's Binaries dir.  Packaged builds find it bare because the game exe sits
		//right next to the core dlls.
		FString FullPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()) + TEXT("Binaries/Win64/") + fileName.c_str();
		string dllPath = StringCast<ANSICHAR>(*FullPath).Get();
		LogMsg("Trying %s instead", dllPath.c_str());
		m_dllHandle = LoadLibraryA(dllPath.c_str());
	}

	if (!m_dllHandle)
	{
		LogMsg("Couldn't load or find file %s - error %d", fileName.c_str(), GetLastError());
		return false;
	}

	m_core.retro_api_version = (decltype(m_core.retro_api_version))MapFunction(m_dllHandle, GET_VARIABLE_NAME(m_core.retro_api_version));
	m_core.retro_get_system_info = (decltype(m_core.retro_get_system_info))MapFunction(m_dllHandle, GET_VARIABLE_NAME(m_core.retro_get_system_info));
	m_core.retro_init = (decltype(m_core.retro_init))MapFunction(m_dllHandle, GET_VARIABLE_NAME(m_core.retro_init));
	m_core.retro_deinit = (decltype(m_core.retro_deinit))MapFunction(m_dllHandle, GET_VARIABLE_NAME(m_core.retro_deinit));
	m_core.retro_reset = (decltype(m_core.retro_reset))MapFunction(m_dllHandle, GET_VARIABLE_NAME(m_core.retro_reset)); 
	m_core.retro_set_environment = (decltype(m_core.retro_set_environment))MapFunction(m_dllHandle, GET_VARIABLE_NAME(m_core.retro_set_environment));
	m_core.retro_set_video_refresh = (decltype(m_core.retro_set_video_refresh))MapFunction(m_dllHandle, GET_VARIABLE_NAME(m_core.retro_set_video_refresh));
	m_core.retro_set_video_refresh_ex = (decltype(m_core.retro_set_video_refresh_ex))MapFunction(m_dllHandle, GET_VARIABLE_NAME(m_core.retro_set_video_refresh_ex));
	m_core.retro_set_audio_sample = (decltype(m_core.retro_set_audio_sample))MapFunction(m_dllHandle, GET_VARIABLE_NAME(m_core.retro_set_audio_sample));
	m_core.retro_set_audio_sample_batch = (decltype(m_core.retro_set_audio_sample_batch))MapFunction(m_dllHandle, GET_VARIABLE_NAME(m_core.retro_set_audio_sample_batch));
	m_core.retro_set_input_poll = (decltype(m_core.retro_set_input_poll))MapFunction(m_dllHandle, GET_VARIABLE_NAME(m_core.retro_set_input_poll));
	m_core.retro_set_input_state = (decltype(m_core.retro_set_input_state))MapFunction(m_dllHandle, GET_VARIABLE_NAME(m_core.retro_set_input_state));
	//Optional HoloVCS/FCEUmm extension.  Other cores do not export it, so resolve it without
	//MapFunction's missing-symbol warning and let the Zelda profile fail safe when absent.
	m_core.retro_set_holo_bg_tile_filter = (decltype(m_core.retro_set_holo_bg_tile_filter))GetProcAddress(m_dllHandle, "retro_set_holo_bg_tile_filter");
	m_core.retro_get_holo_bg_tile_ids = (decltype(m_core.retro_get_holo_bg_tile_ids))GetProcAddress(m_dllHandle, "retro_get_holo_bg_tile_ids");
	//Optional patched-Azahar extension (3DS depth-sliced layers); absence = flat fallback
	m_core.retro_set_video_refresh_holo = (decltype(m_core.retro_set_video_refresh_holo))GetProcAddress(m_dllHandle, "retro_set_video_refresh_holo");
	//ABI v4 sibling: live multiview separation/convergence (see ApplyLayerDepth)
	m_core.retro_holo_set_view_params = (retro_holo_set_view_params_t)GetProcAddress(m_dllHandle, "retro_holo_set_view_params");
	//ABI v4 sibling: debug visualization mask + cutaway plane (see ApplyHoloViz)
	m_core.retro_holo_set_debug = (retro_holo_set_debug_t)GetProcAddress(m_dllHandle, "retro_holo_set_debug");
	m_core.retro_load_game = (decltype(m_core.retro_load_game))MapFunction(m_dllHandle, GET_VARIABLE_NAME(m_core.retro_load_game));
	m_core.retro_get_system_av_info = (decltype(m_core.retro_get_system_av_info))MapFunction(m_dllHandle, GET_VARIABLE_NAME(m_core.retro_get_system_av_info));
	m_core.retro_run = (decltype(m_core.retro_run))MapFunction(m_dllHandle, GET_VARIABLE_NAME(m_core.retro_run));
	m_core.retro_unload_game = (decltype(m_core.retro_unload_game))MapFunction(m_dllHandle, GET_VARIABLE_NAME(m_core.retro_unload_game));
	m_core.retro_serialize_size = (decltype(m_core.retro_serialize_size))MapFunction(m_dllHandle, GET_VARIABLE_NAME(m_core.retro_serialize_size));
	m_core.retro_serialize = (decltype(m_core.retro_serialize))MapFunction(m_dllHandle, GET_VARIABLE_NAME(m_core.retro_serialize));
	m_core.retro_unserialize = (decltype(m_core.retro_unserialize))MapFunction(m_dllHandle, GET_VARIABLE_NAME(m_core.retro_unserialize));
#endif
	return true;
}

void libretro_log(enum retro_log_level level, const char* traceStr, ...)
{
	va_list argsVA;
	const int logSize = 1024 * 10;
	char buffer[logSize];
	memset((void*)buffer, 0, logSize);
	va_start(argsVA, traceStr);
	vsnprintf(buffer, logSize, traceStr, argsVA);
	va_end(argsVA);

	LogMsg("Libretro: %s", buffer);
}

bool retro_environment_callback(unsigned cmd, void* data)
{

	switch (cmd)
	{
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
	{
		struct retro_log_callback* log_data = (retro_log_callback*)data;
		log_data->log = libretro_log;
		break;
	}

	case RETRO_ENVIRONMENT_GET_VARIABLE:
	{
		retro_variable* pVar = (retro_variable*)data;

		if (strcmp(pVar->key, "stella_video_flags") == 0)
		{
			pVar->value = g_pLibretroManager->m_coreRenderFlags;
			return true;
		}
		else
			if (strcmp(pVar->key, "fceumm_video_flags") == 0)
			{
				pVar->value = g_pLibretroManager->m_coreRenderFlags;
				return true;
			}
			else
				if (strcmp(pVar->key, "vb_video_flags") == 0)
				{
					pVar->value = g_pLibretroManager->m_coreRenderFlags;
					return true;
				} else
					if (strcmp(pVar->key, "fceumm_nospritelimit") == 0)
					{
						pVar->value = "enabled"; //disable sprite limit
						return true;
					}


		//virtual boy vb settings
		
		if (strcmp(pVar->key, "vb_3dmode") == 0)
		{
			pVar->value = "3dlayered";
			//pVar->value = "anaglyph";
			return true;
		}

		if (strcmp(pVar->key, "vb_anaglyph_preset") == 0)
		{
			//if v_3dmode is set to anaglyph, we can set the mode here.  disabled means just a normal 2d version,
			//red & blue means setup for 3d glasses
			pVar->value = "disabled";
			//pVar->value = "red & blue";
			return true;
		}
		
		if (strcmp(pVar->key, "holo_3d_layer_count") == 0)
		{
			static char layers[12];
			snprintf(layers, 12, "%d", g_pLibretroManager->m_pLibretroManagedActor->GetLayerCount());
			pVar->value = layers;
			return true;
		}

		//3DS capture mode: 2 = MULTIVIEW (the core renders a true per-view quilt,
		//delivered via the ABI v4 quilt member - the default whenever the Looking
		//Glass plugin is active), 1 = the per-band layered capture (flat builds, and
		//-holobands for on-device A/B), 0 = the legacy single-depth-buffer CPU slice
		//(-hololegacy).  -holomultiview forces 2 anywhere (flat-build quilt debugging).
		if (strcmp(pVar->key, "holo_capture_mode") == 0)
		{
			int mode = 1;
			if (FParse::Param(FCommandLine::Get(), TEXT("hololegacy"))) mode = 0;
			else if (FParse::Param(FCommandLine::Get(), TEXT("holobands"))) mode = 1;
			else if (FParse::Param(FCommandLine::Get(), TEXT("holomultiview"))) mode = 2;
			else
			{
				int tilesX = 0, tilesY = 0;
				if (g_pLibretroManager->m_pLibretroManagedActor &&
					GetLookingGlassTiling(g_pLibretroManager->m_pLibretroManagedActor->GetWorld(), tilesX, tilesY))
				{
					mode = 2; //LKG plugin present: true multi-view is the default
				}
			}
			g_pLibretroManager->m_holoCaptureMode = mode;
			static char modeStr[4];
			snprintf(modeStr, sizeof(modeStr), "%d", mode);
			pVar->value = modeStr;
			LogMsg("3DS holo capture mode: %d", mode);
			return true;
		}

		//multiview only: the device tile layout, so the core renders exactly one view
		//per lens tile.  Falls back to the Portrait 8x6/48 when no device resolves.
		if (strcmp(pVar->key, "holo_view_count") == 0 ||
			strcmp(pVar->key, "holo_quilt_cols") == 0 ||
			strcmp(pVar->key, "holo_quilt_rows") == 0)
		{
			int tilesX = 8, tilesY = 6;
			if (g_pLibretroManager->m_pLibretroManagedActor)
			{
				GetLookingGlassTiling(g_pLibretroManager->m_pLibretroManagedActor->GetWorld(), tilesX, tilesY);
			}
			static char viewStr[12], colStr[12], rowStr[12];
			if (pVar->key[strlen("holo_")] == 'v') //holo_view_count
			{
				snprintf(viewStr, sizeof(viewStr), "%d", tilesX * tilesY);
				pVar->value = viewStr;
			}
			else if (strcmp(pVar->key, "holo_quilt_cols") == 0)
			{
				snprintf(colStr, sizeof(colStr), "%d", tilesX);
				pVar->value = colStr;
			}
			else
			{
				snprintf(rowStr, sizeof(rowStr), "%d", tilesY);
				pVar->value = rowStr;
			}
			return true;
		}

		    //here we change the palette to pure RGB, easier to setup colorkeys as I can also set
		    //mesen to "RGB (Nestopia)" and the palettes will exactly match
			if (strcmp(pVar->key, "fceumm_palette") == 0)
			{
				pVar->value = "rgb";
				return true;
			}

		//Azahar (3DS) core options - keys are prefixed "citra_".  With the patched holo core
		//(detected by its layer-callback export) we ask for OpenGL: the core then runs its
		//own offscreen GL context and delivers depth-sliced layers.  The stock core gets the
		//Software renderer instead (slow, flat, but works with zero GL plumbing).
		if (strcmp(pVar->key, "citra_graphics_api") == 0)
		{
			pVar->value = g_pLibretroManager->m_core.retro_set_video_refresh_holo ? "OpenGL" : "Software";
			return true;
		}

		//right stick is a pure C-stick; with the default "both" the core's own pointer
		//tracker also follows it and fights our mouse-driven touch cursor
		if (strcmp(pVar->key, "citra_analog_function") == 0)
		{
			pVar->value = "c_stick";
			return true;
		}

		return false;
	}
	break;

	case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
		//LogMsg("AV enabled msg");
		return false; //signal unhandled
		break;

	case RETRO_ENVIRONMENT_SET_VARIABLES:
	{
		const struct retro_variable* var = (retro_variable*)data;
		while (var->key != NULL)
		{
			LogMsg("Core says:  %s = %s", var->key, var->value);
			var++;
		}
		return false;
	}

	case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
	{
		const struct retro_input_descriptor* desc = NULL;
		desc = (const struct retro_input_descriptor*)data;
		LogMsg("Got input info about '%s':  Device %d, id %d", desc->description, desc->device, desc->id);
	}

	break;

	case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
		return false; //not handled
		break;

	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
	{
		retro_pixel_format* pixelFormat = (retro_pixel_format*)data;
		LogMsg("Trying to set pixel format to %d", (int)*pixelFormat);
		break;
	}

	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
	{
		//the Azahar core roots its whole user dir (game saves, config) here (appends
		//"Azahar/").  Point it at saves/ so the test bats' save-keep logic preserves 3DS
		//game saves across restages, same as the .sav0 state files.
		static string savePath;
		savePath = g_pLibretroManager->m_rootPath + "saves";
		*reinterpret_cast<const char**>(data) = savePath.c_str();
		break;
	}

	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
	{
		FString RelativePath = FPaths::ProjectDir();
		FString FullPath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*RelativePath);

		static string pPath = StringCast<ANSICHAR>(*FullPath).Get();
		*reinterpret_cast<const char**>(data) = (char*)pPath.c_str();
		LogMsg("Set system dir to %s", pPath.c_str());
		break;
	}

	case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
	{
		//The multi-pass systems re-read their render-flag variables every frame, so they get a
		//standing "yes".  The Azahar core treats "yes" as "re-parse and re-apply EVERY option"
		//each retro_run (settings churn + env-call log spam), so 3DS gets "no changes".
		bool* updated = (bool*)data;
		*updated = (g_pLibretroManager->m_emulatorType != EMULATOR_3DS);
	}
	break;

	case RETRO_ENVIRONMENT_SET_GEOMETRY:
	{
		retro_system_av_info* pInfo = (retro_system_av_info*)data;

		//the Azahar core re-sends SET_GEOMETRY every frame; only react (and log) on change,
		//or the audio buffer gets torn down and rebuilt 60 times a second
		static double lastAppliedRate = -1;
		if (pInfo->timing.sample_rate != lastAppliedRate)
		{
			lastAppliedRate = pInfo->timing.sample_rate;
			LogMsg("Update from core says screen is %d, %d, but max is %d, %d.", pInfo->geometry.base_width, pInfo->geometry.base_height,
				pInfo->geometry.max_width, pInfo->geometry.max_height);
			LogMsg("FPS %f, sample rate: %f", (float)pInfo->timing.fps, (float)pInfo->timing.sample_rate);
			g_pLibretroManager->m_pLibretroManagedActor->SetSampleRate(pInfo->timing.sample_rate);
		}
		break;
	}

	case RETRO_ENVIRONMENT_SET_MESSAGE:
	{
		retro_message* pMsg = (retro_message*)data;
		if (!pMsg || !pMsg->msg) return false;
		LogMsg("Core message: %s", pMsg->msg);
		float seconds = pMsg->frames / 60.0f;
		if (seconds < 4) seconds = 4; //keep short errors readable
		g_pLibretroManager->m_lastCoreMessage = pMsg->msg;
		ShowStatusMessage(pMsg->msg, seconds);
		break;
	}

	case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
	{
		retro_message_ext* pMsg = (retro_message_ext*)data;
		if (!pMsg || !pMsg->msg) return false;
		LogMsg("Core message: %s", pMsg->msg);
		if (pMsg->target != RETRO_MESSAGE_TARGET_LOG) //log-only messages stay off the OSD
		{
			float seconds = pMsg->duration / 1000.0f;
			if (seconds < 4) seconds = 4;
			g_pLibretroManager->m_lastCoreMessage = pMsg->msg;
			ShowStatusMessage(pMsg->msg, seconds);
		}
		break;
	}

	default:

		LogMsg("Got unhandled cmd %u", cmd);
		return false;
		break;
	}

	return true;
}

void LibretroManager::LoadRomList()
{
	string romFileName;

	FString FullPath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*FPaths::ProjectDir());
	m_rootPath = StringCast<ANSICHAR>(*FullPath).Get();

	// for android, the full path needs to be this app's sdcard data folder, where the user can add roms etc themselves
#if PLATFORM_ANDROID
	FString ExternalStoragePath = FPaths::Combine(FPaths::ProjectPersistentDownloadDir(), TEXT("Roms"));
	m_rootPath = StringCast<ANSICHAR>(*ExternalStoragePath).Get();
#endif

#if PLATFORM_WINDOWS
	FString testPath = FullPath + "nes";

	if (!FPaths::DirectoryExists(testPath))
	{
		m_rootPath += "../";
		LogMsg("Adding ../ to path due to detected release dir layout");
	}
#endif

	m_romNameFileList.Empty();
	m_emulatorIDList.Empty();

	FString staticResourcesPath = FPaths::ProjectContentDir() / TEXT("static_resources");

	for (int i = 0; i < EMULATOR_COUNT; i++)
	{
		SetEmulatorData((eEmulatorType)i);

		FString staticRomPath = staticResourcesPath / FString(m_romDir.c_str());

		// get list of roms and play the first one
		int romCountBeforeAdding = m_romNameFileList.Num();

		// Search in the static_resources path
		IFileManager::Get().FindFiles(m_romNameFileList, *staticRomPath, ANSI_TO_TCHAR(m_romFileExtension1.c_str()));
		IFileManager::Get().FindFiles(m_romNameFileList, *staticRomPath, ANSI_TO_TCHAR(m_romFileExtension2.c_str()));

		// if any of the roms found in the static rom path don't already exist in the root path, copy them over - this is done so platforms like Android that store things in a zip
		// can still include roms so my testing is easier.  retroarch can't load directly from the apk/zip, so the extra file copy is necessary.  Unfortunately if something already
		//exists with the same name it won't be updated.  Maybe I should just force a write every time

		for (int j = romCountBeforeAdding; j < m_romNameFileList.Num(); j++)
		{
			romFileName = std::string(TCHAR_TO_ANSI(*staticRomPath)) + "/" + TCHAR_TO_ANSI(*m_romNameFileList[j]);
			string romName = GetFileNameWithoutExtension(romFileName);
			string romNameWithExt = romName + m_romFileExtension1;
			string romNameWithExt2 = romName + m_romFileExtension2;

			if (!FPaths::FileExists(FPaths::Combine(FPaths::ProjectDir(), ANSI_TO_TCHAR(m_romDir.c_str()), ANSI_TO_TCHAR(romNameWithExt.c_str()))) &&
				!FPaths::FileExists(FPaths::Combine(FPaths::ProjectDir(), ANSI_TO_TCHAR(m_romDir.c_str()), ANSI_TO_TCHAR(romNameWithExt2.c_str()))))
			{
				// copy it over
				string dest = m_rootPath + m_romDir + "/" + TCHAR_TO_ANSI(*m_romNameFileList[j]);
				string src = std::string(TCHAR_TO_ANSI(*staticRomPath)) + "/" + TCHAR_TO_ANSI(*m_romNameFileList[j]);
				LogMsg("Copying %s to %s", src.c_str(), dest.c_str());
				IFileManager::Get().Copy(ANSI_TO_TCHAR(dest.c_str()), ANSI_TO_TCHAR(src.c_str()));
			}
		}
	}

	// clear data and do it again from the real dir
	m_romNameFileList.Empty();
	m_emulatorIDList.Empty();

	for (int i = 0; i < EMULATOR_COUNT; i++)
	{
		SetEmulatorData((eEmulatorType)i);

		FString romPath = FString(m_rootPath.c_str()) + FString(m_romDir.c_str()) + "/";

		// get list of roms and play the first one
		int romCountBeforeAdding = m_romNameFileList.Num();

		// Search in the default path
		IFileManager::Get().FindFiles(m_romNameFileList, *romPath, ANSI_TO_TCHAR(m_romFileExtension1.c_str()));
		IFileManager::Get().FindFiles(m_romNameFileList, *romPath, ANSI_TO_TCHAR(m_romFileExtension2.c_str()));

		int romsFound = m_romNameFileList.Num() - romCountBeforeAdding;

		LogMsg("Scanning %s dir (for %s), found %d roms (%d total)", TCHAR_TO_ANSI(*romPath), m_coreName.c_str(), romsFound, m_romNameFileList.Num());

		for (int j = 0; j < romsFound; j++)
		{
			m_emulatorIDList.Add(i);
		}
	}
}

bool LibretroManager::LoadRom(string fileName)
{
	m_lastCoreMessage.clear(); //so InitEmulator's failure branch only sees a message from THIS load

	retro_game_info ginfo;
	memset(&ginfo, 0, sizeof(ginfo));

	m_romDataArray.Empty();

	if (m_emulatorType == EMULATOR_3DS)
	{
		//3DS images are hundreds of MB and the Azahar core loads from ginfo.path itself
		//(need_fullpath), so never pull the whole file into RAM. Hash the first 1MB for the
		//game-profile key; the NCSD/NCCH headers in there identify the title and revision.
		TUniquePtr<IFileHandle> file(FPlatformFileManager::Get().GetPlatformFile().OpenRead(ANSI_TO_TCHAR(fileName.c_str())));
		if (!file)
		{
			LogMsg("Error loading rom (can't find it)");
			return false;
		}
		const int64 hashBytes = FMath::Min<int64>(file->Size(), 1024 * 1024);
		m_romDataArray.SetNumUninitialized((int32)hashBytes);
		file->Read(m_romDataArray.GetData(), hashBytes);
		m_romHash = TCHAR_TO_UTF8(*FMD5::HashBytes(m_romDataArray.GetData(), hashBytes));
		m_romDataArray.Empty();
		ginfo.data = NULL;
		ginfo.size = 0;
	}
	else
	{
		FFileHelper::LoadFileToArray(m_romDataArray, ANSI_TO_TCHAR(fileName.c_str()));
		ginfo.data = m_romDataArray.GetData();
		ginfo.size = m_romDataArray.Num();

		if (ginfo.size == 0 || ginfo.data == NULL)
		{
			LogMsg("Error loading rom (can't find it)");
			return false;
		}

		int headerSizeToSkipForRomHash = 0;

		//calculate checksum, needed to recognize which game we're running
		if (m_emulatorType == EMULATOR_NES)
		{
			headerSizeToSkipForRomHash = 16;
		}

		m_romHash = TCHAR_TO_UTF8(*FMD5::HashBytes((uint8*)&((byte*)ginfo.data)[headerSizeToSkipForRomHash], ginfo.size - headerSizeToSkipForRomHash));
	}

	ginfo.path = fileName.c_str();
	LogMsg("Loading rom %s, has a MD5 hash of %s", ginfo.path, m_romHash.c_str());
	if (!m_core.retro_load_game(&ginfo))
	{
		LogMsg("Error: loading rom");
		return false;
	}
	m_joyPad.Clear();

	memset(&m_game_av_info, 0, sizeof(m_game_av_info));
	m_core.retro_get_system_av_info(&m_game_av_info);
	LogMsg("Core says screen is %d, %d, but max is %d, %d.", m_game_av_info.geometry.base_width, m_game_av_info.geometry.base_height,
		m_game_av_info.geometry.max_width, m_game_av_info.geometry.max_height);

	//The 3DS core reports its 32728 Hz rate only here (never via SET_GEOMETRY, which is the
	//only place the older cores' rates got applied). Scoped to 3DS so NES's hardcoded 48000
	//override in SetEmulatorData keeps winning there.
	if (m_emulatorType == EMULATOR_3DS && m_game_av_info.timing.sample_rate > 0)
	{
		m_pLibretroManagedActor->SetSampleRate(m_game_av_info.timing.sample_rate);
	}

	m_profManager.InitGame(m_romHash);
	//ShowStatusMessage(fileName.c_str());
	return true;
}

void retro_audio_sample_callback(int16_t left, int16_t right)
{
	LogMsg("Got sample callback");
	return;
}

//Dynamic rate control (the RetroArch approach). The emulator is paced by the CPU clock and the
//sound card drains by its own DAC clock, and the two never agree exactly, so a plain queue drifts
//until it either overflows (drop a frame of audio = click) or runs dry (repeat a sample = click),
//every few seconds. Instead, each chunk is resampled by a tiny ratio driven by the queue fill so
//the fill hovers around the target: a little long -> play marginally faster, a little short ->
//marginally slower. The deviation stays under AUDIO_RATE_MAX_DEVIATION, far below audible pitch.
static const int AUDIO_TARGET_QUEUED_SAMPLES = 2400; //50ms at 48k, mono. Covers one 1024-sample mixer callback plus a frame of jitter.
static const float AUDIO_RATE_MAX_DEVIATION = 0.005f; //0.5%
static const int AUDIO_HARD_DROP_SAMPLES = AUDIO_TARGET_QUEUED_SAMPLES * 4; //safety valve only (pause/hitch recovery)

size_t retro_audio_sample_batch_callback(const int16_t* data, size_t frames)
{
	
	if (!g_pLibretroManager->m_useAudio) return frames; //this audio can be trashed, it's probably because I couldn't figure out how to turn it off when doing
														//some extra visual renders
	auto* pAudioBufferComp = g_pLibretroManager->m_pLibretroManagedActor->m_pRTAudioBufferComponent;

	if (pAudioBufferComp->GetBufferGenerator() && frames > 0)
	{
		RTBufferGenerator* pGen = pAudioBufferComp->GetBufferGenerator();
		const int curSamplesInBuffer = pGen->GetSamplesQueued();

		if (curSamplesInBuffer >= AUDIO_HARD_DROP_SAMPLES)
		{
			//way ahead (the mixer stalled or we were paused with audio still flowing); rate control
			//can't absorb this much, skip the frame outright
			g_pLibretroManager->m_audioFramesDropped++;
			return frames;
		}

		//proportional control: +deviation when empty, -deviation when at 2x target
		float fillError = (float)(AUDIO_TARGET_QUEUED_SAMPLES - curSamplesInBuffer) / (float)AUDIO_TARGET_QUEUED_SAMPLES;
		fillError = FMath::Clamp(fillError, -1.0f, 1.0f);
		const double ratio = 1.0 + fillError * AUDIO_RATE_MAX_DEVIATION; //output samples per input sample
		const double step = 1.0 / ratio;

		//linear resample, mono (left channel). s_prevSample/s_phase carry across calls so chunk
		//boundaries stay continuous.
		static float s_prevSample = 0.0f;
		static double s_phase = 0.0; //position in extended input space where index 0 is s_prevSample and index k+1 is data[k]

		const float scale = 1.0f / (32768.0f * 2);
		const int maxOut = (int)((double)frames * (1.0 + AUDIO_RATE_MAX_DEVIATION)) + 2;

		RTSampleChunk chunk;
		chunk.pSampleData = new float[maxOut];

		int outCount = 0;
		while (s_phase < (double)frames && outCount < maxOut)
		{
			const int idx = (int)s_phase;
			const float frac = (float)(s_phase - (double)idx);
			const float a = (idx == 0) ? s_prevSample : (float)data[(idx - 1) * 2] * scale;
			const float b = (float)data[idx * 2] * scale; //idx <= frames-1 here
			chunk.pSampleData[outCount++] = a + (b - a) * frac;
			s_phase += step;
		}
		s_phase -= (double)frames;
		s_prevSample = (float)data[(frames - 1) * 2] * scale;

		chunk.validSamples = outCount;
		pGen->AddChunkSchedule(chunk);
	}

		//LogMsg("Got batch audio callback with %d frames", frames);
	return frames;
}

//we're not using this I guess, although maybe we should
void retro_input_poll_callback(void)
{
	return;
}

int16_t retro_input_state_callback(unsigned port, unsigned device, unsigned index, unsigned id)
{
	//LogMsg("State callback called: port %d, dev %d, index %d, id %d", port, device, index, id);
	//OPTIMIZE: basic way for now, should switch to bitmasked input.  But not sure that would matter much in the grand scheme
	//of things as far as optimization
	
	if (port == 0 && device == RETRO_DEVICE_JOYPAD)
	{
		if (id < C_MAX_JOYPAD_BUTTONS)
		{
			//3DS: harness `press up/left/...` holds feed the circle pad (analog fallback below),
			//NOT the digital d-pad - SM3DL and friends bind the d-pad to camera control, so
			//folding them in here would move Mario AND spin the camera at once
			bool bAutoHeld = g_pLibretroManager->m_autoButtonHoldFrames[id] > 0;
			if (g_pLibretroManager->m_emulatorType == EMULATOR_3DS && bAutoHeld &&
				(id == RETRO_DEVICE_ID_JOYPAD_UP || id == RETRO_DEVICE_ID_JOYPAD_DOWN ||
				 id == RETRO_DEVICE_ID_JOYPAD_LEFT || id == RETRO_DEVICE_ID_JOYPAD_RIGHT))
			{
				bAutoHeld = false;
			}
			//LogMsg("Scanning button %d which is %d", id, (int)g_pLibretroManager->m_joyPad.m_button[id]);
			return g_pLibretroManager->m_joyPad.m_button[id] || bAutoHeld;
		}
		else
		{
			LogMsg("Unhandled button: %d", id);
		}
	}

	//Analog sticks (the 3DS core reads its circle pad and C-stick this way).  The axes are
	//fed by MoveX/MoveY/RMoveX/RMoveY, so keyboard and gamepad dpad give full deflection
	//while a real stick is properly analog.  The harness dpad holds are folded in too so
	//`press right` walks Mario in analog-only games.  libretro convention: +X right, +Y down.
	if (port == 0 && device == RETRO_DEVICE_ANALOG)
	{
		JoyPadButtonStates& pad = g_pLibretroManager->m_joyPad;
		auto axisToRetro = [](float v) -> int16_t { return (int16_t)FMath::Clamp((int)(v * 32767.0f), -32767, 32767); };

		if (index == RETRO_DEVICE_INDEX_ANALOG_LEFT)
		{
			float x = pad.m_axisLX;
			float y = pad.m_axisLY;
			if (g_pLibretroManager->m_autoButtonHoldFrames[RETRO_DEVICE_ID_JOYPAD_LEFT] > 0) x = -1;
			if (g_pLibretroManager->m_autoButtonHoldFrames[RETRO_DEVICE_ID_JOYPAD_RIGHT] > 0) x = 1;
			if (g_pLibretroManager->m_autoButtonHoldFrames[RETRO_DEVICE_ID_JOYPAD_UP] > 0) y = -1;
			if (g_pLibretroManager->m_autoButtonHoldFrames[RETRO_DEVICE_ID_JOYPAD_DOWN] > 0) y = 1;
			return axisToRetro(id == RETRO_DEVICE_ID_ANALOG_X ? x : y);
		}
		if (index == RETRO_DEVICE_INDEX_ANALOG_RIGHT)
		{
			//the right stick drives OUR touch cursor (PlayerPawn::Tick), not the 3DS C-stick
			return 0;
		}
	}

	//Touchscreen: the 3DS core polls an absolute pointer over its whole 400x480 layout and
	//maps positions inside the bottom-screen rect (40,240..360,480 at 1x) to touch.  Our
	//virtual cursor lives in bottom-screen pixels; convert here.  Left click = pressed.
	if (port == 0 && device == RETRO_DEVICE_POINTER)
	{
		const float absX = 40.0f + g_pLibretroManager->GetTouchPointX();
		const float absY = 240.0f + g_pLibretroManager->GetTouchPointY();
		if (id == RETRO_DEVICE_ID_POINTER_X) return (int16_t)FMath::Clamp((int)((absX / 400.0f * 2.0f - 1.0f) * 32767.0f), -32767, 32767);
		if (id == RETRO_DEVICE_ID_POINTER_Y) return (int16_t)FMath::Clamp((int)((absY / 480.0f * 2.0f - 1.0f) * 32767.0f), -32767, 32767);
		if (id == RETRO_DEVICE_ID_POINTER_PRESSED) return g_pLibretroManager->m_touchDown ? 1 : 0;
	}
	if (port == 0 && device == RETRO_DEVICE_MOUSE && id == RETRO_DEVICE_ID_MOUSE_LEFT)
	{
		return g_pLibretroManager->m_touchDown ? 1 : 0;
	}
	return 0;
}
 

void LibretroManager::SetEmulatorData(eEmulatorType emu)
{
	m_emulatorType = emu;
	string rom = "unset";
	
	//defaults for all emulators
	
	/*
	//original values
	m_pLibretroManagedActor->m_layerCount = 5;
	m_pLibretroManagedActor->m_total3dDepth = 150;
	m_pLibretroManagedActor->SetTextureSmoothingToUse(false);
	m_pLibretroManagedActor->m_layerWidth = 256;
	m_pLibretroManagedActor->m_layerHeight = 256;
	m_pLibretroManagedActor->m_depthOffsetForAllLayers = 0;
	m_pLibretroManagedActor->m_coreLayerScale= FVector2D(4.46, 2.965);
	m_pLibretroManagedActor->m_corePosition = FVector2D(0,0);
	m_pLibretroManagedActor->m_curLightingMode = LIGHTING_MODE_NORMAL;
	*/

	m_pLibretroManagedActor->m_layerCount = 5;
	m_pLibretroManagedActor->m_total3dDepth = 10;
	m_pLibretroManagedActor->SetTextureSmoothingToUse(false);
	m_pLibretroManagedActor->m_layerWidth = 256;
	m_pLibretroManagedActor->m_layerHeight = 256;
	m_pLibretroManagedActor->m_depthOffsetForAllLayers = 0;
	m_pLibretroManagedActor->m_coreLayerScale = FVector2D(0.41f, 0.41f);
	m_pLibretroManagedActor->m_corePosition  = FVector2D(0, 18.4);
	m_pLibretroManagedActor->m_curLightingMode = LIGHTING_MODE_NORMAL;


	m_pLibretroManagedActor->m_bg_color = FVector(0, 0, 0);
	m_pLibretroManagedActor->m_bg_color_strength = 1;
	m_pLibretroManagedActor->m_bgAllowShadows = true;
	//per-system depth default (3DS overrides below); a user adjustment sticks for the session
	if (!m_pLibretroManagedActor->m_bUserDepthScaleTouched) m_pLibretroManagedActor->m_userDepthScale = 1.0f;
	//3DS debug views (Shift+number) don't apply to the other systems; clear when switching away
	if (emu != EMULATOR_3DS)
	{
		m_pLibretroManagedActor->m_holoVizFlags = 0;
		m_pLibretroManagedActor->m_cutaway01 = 0.0f;
	}
	m_bGamePaused = false;
	m_targetFPS = 60;
	
	switch (emu)
	{

	case EMULATOR_ATARI:
		m_coreName = "stella_libretro.dll";
		m_surfaceSourceType = SURFACE_SOURCE_RGBA_32;
		m_romDir = "atari2600";
		m_romFileExtension1 = ".a26";
		m_romFileExtension2 = ".bin";
		m_pLibretroManagedActor->m_coreLayerScale = FVector2D(4.45, 3.5);
		m_pLibretroManagedActor->m_corePosition = FVector2D(69, 0);
		//m_pLibretroManagedActor->SetTextureSmoothingToUse(true);
		m_pLibretroManagedActor->m_total3dDepth = 100; //more compressed

		//m_pLibretroManagedActor->SetSampleRate(48000); 

		break;

	case EMULATOR_NES:
		m_coreName = "fceumm_libretro.dll";
		m_surfaceSourceType = SURFACE_SOURCE_RGB_565_16;
		m_romDir = "nes";
		m_romFileExtension1 = ".nes";
		m_romFileExtension2 = ".unusedcrap";
		
		
		//m_pLibretroManagedActor->m_coreLayerScale = FVector2D(2.8, 2.8);
		//m_pLibretroManagedActor->m_corePosition = FVector2D(0, 0);
		//m_pLibretroManagedActor->m_total3dDepth = 100; //more compressed
		m_pLibretroManagedActor->SetSampleRate(48000); //the nes core I'm using doesn't self report this for some reason
		break;
	
	case EMULATOR_VB:
		m_coreName = "beetle-vb-libretro.dll";
		m_surfaceSourceType = SURFACE_SOURCE_RGBA_32;
		m_romDir = "vb";
		m_romFileExtension1 = ".vb";
		m_romFileExtension2 = ".vboy";
		m_pLibretroManagedActor->m_layerWidth = 384;
		m_pLibretroManagedActor->m_layerHeight = 256;
		m_pLibretroManagedActor->m_layerCount = 16; //give it more to play with, it has more 3d to do
		m_pLibretroManagedActor->m_total3dDepth = 170;
		m_pLibretroManagedActor->m_depthOffsetForAllLayers = -35; //move it forward a bit, need stuff to be in focus for VB, even in the back
		m_pLibretroManagedActor->m_coreLayerScale = FVector2D(3.1, 2.8); //width hangs off screen, but looks better
		m_pLibretroManagedActor->m_corePosition = FVector2D(0, 0);
		
		//it's faster with lighting enabled.  Looks better disabled though, so push 8 to toggle it
		//m_pLibretroManagedActor->m_curLightingMode = LIGHTING_MODE_NONE;
		m_pLibretroManagedActor->m_bgAllowShadows = false;
		m_targetFPS = 50;
		break;

	case EMULATOR_3DS:
		//Azahar (Citra successor) core from the fork at f:\Unreal\azahar. The patched "holo"
		//build delivers the TOP screen as depth-sliced 400x240 layers via the v2 layer
		//callback (docs/3ds_azahar.md); a stock core falls back to the software renderer and
		//the profile blits the top-screen crop of its 400x480 composite flat onto one layer.
		m_coreName = "azahar_libretro.dll";
		m_surfaceSourceType = SURFACE_SOURCE_RGBA_32;
		m_romDir = "3ds";
		m_romFileExtension1 = ".3ds";
		m_romFileExtension2 = ".cci";
		m_pLibretroManagedActor->m_layerWidth = 400;
		m_pLibretroManagedActor->m_layerHeight = 240;
		m_pLibretroManagedActor->m_layerCount = 24; //negotiated to the core via holo_3d_layer_count (core clamps at 32; 30 was tried and dropped fps)
		m_pLibretroManagedActor->m_total3dDepth = 170;
		m_pLibretroManagedActor->m_depthOffsetForAllLayers = -35;
		//the layer quad mesh is (nearly) square, so the scale pair must carry the full
		//400:240 content aspect or everything displays horizontally squished (mesh aspect
		//measured ~1.05: 3.5*1.05/2.2 = 1.67 = 400/240).  The bottom screen quad derives
		//its own scale from this, so it stays correct automatically.
		m_pLibretroManagedActor->m_coreLayerScale = FVector2D(3.5f, 2.2f);
		m_pLibretroManagedActor->m_corePosition = FVector2D(0, 0);
		m_pLibretroManagedActor->m_bgAllowShadows = false;
		//Seth's preferred 3DS depth (Aug 27 2026: 90% -> 155% -> 175% as multiview got
		//dialed in); direct assign (not SetUserDepthScale) so no status text fires and
		//InitLayers just picks it up
		if (!m_pLibretroManagedActor->m_bUserDepthScaleTouched)
		{
			m_pLibretroManagedActor->m_userDepthScale = 1.75f;
			LogMsg("3DS: depth scale defaulting to 175%%");
		}
		m_targetFPS = 59.8331; //real 3DS refresh; audio rate comes from retro_get_system_av_info
		m_touchCursorShownOnce = false; //re-arm the "show the cursor briefly at boot" hint
		break;

	default:
		LogMsg("Error, unknown emulator type");
		return;
		break;
	}

}

void LibretroManager::DisableAllBlitPasses()
{
	for (int i = 0; i < C_MAX_BLITPASS_COUNT; i++)
	{
		DisableBlitPass(i); //just make sure nothing will actually render
	}

}
void LibretroManager::SetGamePaused(bool bNew)
{
	//a silently wrong pause state makes ALL emulator input look dead, so every transition gets logged
	if (m_bGamePaused != bNew) LogMsg("SetGamePaused: %d -> %d (helpVisible %d)", (int)m_bGamePaused, (int)bNew, (int)m_helpScreen.IsVisible());
	//an unpause from anywhere else (P key, rom switch/reset, harness) closes the help screen;
	//the help's own Hide() clears visibility before calling here, so this can't recurse
	if (!bNew && m_helpScreen.IsVisible())
	{
		m_helpScreen.NotifyExternallyUnpaused();
	}
	m_bGamePaused = bNew;
}

bool LibretroManager::SwitchRomByPartialName(string name)
{
	if (!SetRomToLoadByPartialFileName(name)) return false;
	InitEmulator(); //same reload path the ,/. rom-cycle keys use
	return true;
}

bool LibretroManager::SetRomToLoadByPartialFileName(string name)
{
	//rom = "Super Mario Bros. (World).nes";
	//rom = "Castlevania (USA) (Rev A).nes";
	//rom = "Pitfall! (1982) (Activision) [!].a26";

	name = ToUpperCaseString(name);

	for (int i = 0; i < m_romNameFileList.Num(); i++)
	{
		if (IsInString(ToUpperCaseString( toString(m_romNameFileList[i]) ), name.c_str() ))
		{
			m_activeRomIndex = i;
			LogMsg("Loading %s by partial match to the word %s", toString(m_romNameFileList[i]).c_str(), name.c_str());
			return true;
		}

	}

	LogMsg("Couldn't find any rom with the word %s in it", name.c_str());

	//didn't find it
	return false;
}

void LibretroManager::InitEmulator()
{
	m_profManager.Init(this);

	if (m_romNameFileList.Num() == 0)
	{
	
#if PLATFORM_WINDOWS
		MessageBox(NULL, (LPCWSTR)L"No game roms found.\nPut some in the atari2600 or nes dir first!\nCheck readme for which games are supported.",
			(LPCWSTR)L"Add game roms!",
			MB_ICONWARNING | MB_OK | MB_DEFAULT_DESKTOP_ONLY);

		return;
#endif
	}

	// The default ROM index and startup filename fragment are only preferences. A release may
	// contain fewer ROMs, or the user may rename a ROM and rely on checksum-based recognition.
	// Always establish a valid fallback before attempting the optional filename match.
	if (!m_romNameFileList.IsValidIndex(m_activeRomIndex) ||
		!m_emulatorIDList.IsValidIndex(m_activeRomIndex))
	{
		m_activeRomIndex = 0;
	}

	//-rom=partialname on the command line overrides the hardcoded startup rom
	FString romOverride;
	if (FParse::Value(FCommandLine::Get(), TEXT("rom="), romOverride) && !romOverride.IsEmpty())
	{
		g_partialRomNameToLoadOnStartup = toString(romOverride);
	}

	if (g_loadStateOnFirstLoad && !g_partialRomNameToLoadOnStartup.empty())
	{
		if (!SetRomToLoadByPartialFileName(g_partialRomNameToLoadOnStartup))
		{
			LogMsg("Startup ROM preference was not found; loading %s instead",
				toString(m_romNameFileList[m_activeRomIndex]).c_str());
		}

		g_loadStateOnFirstLoad = false;
		g_partialRomNameToLoadOnStartup = "";
	}

	SetEmulatorData((eEmulatorType) m_emulatorIDList[m_activeRomIndex]);
	
	if (m_romNameFileList.Num() > 0)
	{
		m_curRomName = toString(m_romNameFileList[m_activeRomIndex]);
	}
	else
	{
		LogMsg("Error, no roms found");

		ShowStatusMessage("No roms found!", 100);

		return;
	}
	strcpy(m_coreRenderFlags, "1111111");

	if (!LoadCore(m_coreName.c_str()))
	{
		string msg = string("ERROR: Can't find core ") + m_coreName;
		LogMsg(msg.c_str());
		ShowStatusMessage(msg.c_str(), 100);
		return;
	}
	else
	{
		LogMsg("libretro core %s loaded.", m_coreName.c_str());
	}

	int apiVer = m_core.retro_api_version();
	LogMsg("API of course is %d", apiVer);

	memset(&m_game_system_info, 0, sizeof(m_game_system_info));
	m_core.retro_get_system_info(&m_game_system_info);

	LogMsg("Core: %s\nVersion: %s\nNeed Full path: %s\nExtensions: %s", m_game_system_info.library_name, m_game_system_info.library_version, m_game_system_info.need_fullpath ? "true" : "false", m_game_system_info.valid_extensions);

	m_core.retro_set_environment(retro_environment_callback);
	m_core.retro_set_video_refresh(retro_video_refresh_callback);

	if (m_core.retro_set_video_refresh_ex) //this is nonstandard, I added it to the VB core
		m_core.retro_set_video_refresh_ex(retro_video_refresh_callback_ex);

	if (m_core.retro_set_video_refresh_holo) //nonstandard too, the patched Azahar (3DS) core.
		m_core.retro_set_video_refresh_holo(retro_video_refresh_callback_holo); //registering opts the core into holo mode

	m_lastQuiltPackSeq = -1; //fresh core load: the first delivered quilt always copies

	m_core.retro_set_audio_sample(retro_audio_sample_callback);

	m_core.retro_set_audio_sample_batch(retro_audio_sample_batch_callback);
	m_core.retro_set_input_poll(retro_input_poll_callback);
	m_core.retro_set_input_state(retro_input_state_callback);

	m_core.retro_init();

	m_romPath = m_rootPath + m_romDir + "/";

	if (!LoadRom(m_romPath + m_curRomName))
	{
		string romFullPath = m_romPath + m_curRomName;
		if (!m_lastCoreMessage.empty())
		{
			//the core already explained the failure (e.g. "really is encrypted"); re-show it
			//sticky rather than stomping it - its own 10 second display would fade to nothing
			ShowStatusMessage(m_lastCoreMessage, 100);
			LogMsg("Core refused rom %s: %s", romFullPath.c_str(), m_lastCoreMessage.c_str());
		}
		else if (FPaths::FileExists(FString(romFullPath.c_str())))
		{
			string msg = "ERROR: Core could not load " + m_curRomName + " (see log)";
			ShowStatusMessage(msg.c_str(), 100);
			LogMsg(msg.c_str());
		}
		else
		{
			string msg = "ERROR: Place rom (";
			msg += m_romFileExtension1 + ") in " + m_romPath + " dir!";
			ShowStatusMessage(msg.c_str(), 100);
			LogMsg(msg.c_str());
		}
		return;
	}
	m_nesHacker.Reset();
	SetFrameSkip(0);

	ShowStatusMessage(G_VERSION_STRING + " Loaded " + m_curRomName, 4);

	m_maxSaveStateSize = m_core.retro_serialize_size();
	if (m_emulatorType == EMULATOR_3DS)
	{
		//the Azahar core's core-info claims savestates and retro_serialize_size can return
		//nonzero, but retro_serialize always fails ("Core does not support save states").
		//Force the no-savestate path so the hotkeys report "not supported" instead of
		//writing garbage .sav0 files (verified live Aug 2026: without this, "Error saving
		//state 2" followed by a bogus save file).
		m_maxSaveStateSize = 0;
	}
	if (m_maxSaveStateSize > 0)
	{
		if (m_emulatorType == EMULATOR_ATARI)
			m_maxSaveStateSize += 2048; //it will fail because it's a couple bytes
		//off sometimes, a libretro_stella bug?  whatever, I'll give it some extra

		LogMsg("Preparing save state buffers for this emulator, each is %d bytes", m_maxSaveStateSize);
		for (int i = 0; i < C_SAVE_STATE_COUNT; i++)
		{
			SAFE_DELETE_ARRAY(m_pSaveStateBuffer[i]);
			m_pSaveStateBuffer[i] = new uint8[m_maxSaveStateSize];
		}
	}
	else if (m_emulatorType == EMULATOR_3DS)
	{
		//the Azahar libretro core genuinely has no savestate support (retro_serialize_size()
		//returns 0, verified Aug 2026) - run without the multi-pass/savestate machinery
		LogMsg("Core has no savestate support; savestates and rewind tricks disabled for this system");
		for (int i = 0; i < C_SAVE_STATE_COUNT; i++)
		{
			SAFE_DELETE_ARRAY(m_pSaveStateBuffer[i]);
		}
	}
	else
	{
		LogMsg("Serious error with savestate reporting, can't continue");
		return;
	}

	m_core.m_bActive = true;
	
	DisableAllBlitPasses();

	m_profManager.ApplyStartingGameSpecificSetup();

	m_pLibretroManagedActor->InitLayers();

	if (g_loadStateOnFirstLoad)
	{
		g_loadStateOnFirstLoad = false;
		//skip on systems without savestates (3DS) so boot doesn't show the
		//"doesn't support save states" message unprompted
		if (m_maxSaveStateSize > 0) LoadStateFromFile();
	}

	m_core.retro_run();
	SaveState(0);
}

void LibretroManager::ClearAllLayers()
{
	for (int i = 0; i < m_pLibretroManagedActor->m_layerInfo.size(); i++)
	{
		LayerInfo* pDestLayer = g_pLibretroManager->m_pLibretroManagedActor->GetLayer(i);
		if (!pDestLayer->GetPixelBuffer()) continue;
		uint8* pDst = pDestLayer->GetPixelBuffer();
		
		memset(pDst, 0, pDestLayer->m_texWidth * pDestLayer->m_texHeight * 4);
	}
}

void LibretroManager::ResetRom()
{
	SetGamePaused(false);

	m_nesHacker.Reset();
	LoadState(0);
	m_core.retro_reset();
	m_core.retro_run();
	SaveState(0);

	//re-assert the user's view settings after the core reboot: the reset path never used to
	//re-push depth/convergence (or re-place the 3DS bottom screen), so R looked like it reset
	//the 3D settings whenever the core came back with its own defaults
	if (m_pLibretroManagedActor)
	{
		m_pLibretroManagedActor->ApplyLayerDepth();
		m_pLibretroManagedActor->ApplyHoloViz();
	}
}

void LibretroManager::SetRomByIndex(int index)
{
	m_activeRomIndex = index;
}

void LibretroManager::Init(ALibretroManagerActor * pLibretroManagedActor)
{
	g_pLibretroManager = this;
	m_pLibretroManagedActor = pLibretroManagedActor;
	m_pPlayerPawn = (APlayerPawn*)GetActorByTag(m_pLibretroManagedActor->GetWorld(), "PlayerPawn");
	m_autoHarness.Init(this);

	LogMsg("Let's init the emu core we want from its dll!");
	
	//TODO stuff
	/*

	HMONITOR primaryHandle = MonitorFromWindow(GetActiveWindow(), MONITOR_DEFAULTTONEAREST);
	UINT dpiX, dpiY;
	HRESULT temp2 = GetDpiForMonitor(primaryHandle, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
	double scalingFactor = dpiY / 96.0;
	if (scalingFactor != 1.0f)
	{
		MessageBox(NULL, (LPCWSTR)L"Uh oh, your Looking Glass has a windows scaling\nfactor set!  Change to 100% scaling to fix visual glitches.\n(open Display Settings, then click the Looking Glass monitor)",
			(LPCWSTR)L"Monitor scaling detected!",
			MB_ICONWARNING | MB_OK | MB_DEFAULT_DESKTOP_ONLY);
	}
	*/

	LoadRomList();
	SetRomByIndex(C_DEFAULT_ROM_ID);
	InitEmulator();
	
}

bool LibretroManager::SaveState(int index)
{
	if (m_maxSaveStateSize <= 0 || !m_pSaveStateBuffer[index]) return false; //core has no savestates (3DS)

	if (!m_core.retro_serialize(m_pSaveStateBuffer[index], m_maxSaveStateSize))
	{
		LogMsg("Error saving state %d", index);
		return false;
	}

	return true;
}

bool LibretroManager::CopyState(int fromState, int toState)
{

	assert(fromState < C_SAVE_STATE_COUNT);
	assert(toState < C_SAVE_STATE_COUNT);

	memcpy(m_pSaveStateBuffer[toState], m_pSaveStateBuffer[fromState], m_maxSaveStateSize);

	return true;
}


bool LibretroManager::LoadState(int index)
{
	if (m_maxSaveStateSize <= 0 || !m_pSaveStateBuffer[index]) return false; //core has no savestates (3DS)

	if (!m_core.retro_unserialize(m_pSaveStateBuffer[index], m_maxSaveStateSize))
	{
		LogMsg("Error loading state %d", index);
		return false;
	}

	return true;
}

uint32 ARGB1555toARGB8888(unsigned short c)
{
	const uint32 a = c & 0x8000, r = c & 0x7C00, g = c & 0x03E0, b = c & 0x1F;
	const uint32 rgb = (r << 9) | (g << 6) | (b << 3);
	return (a * 0x1FE00) | rgb | ((rgb >> 5) & 0x070707);
}

//SETH Blit core to unreal layer
void BlitCoreLayerToUnrealLayer(Layer3DSlice* pSrcLayer, LayerInfo* pDestLayer)
{
	uint8* pDst = pDestLayer->GetPixelBuffer();
	if (!pDst)
	{
		LogMsg("BlitCoreLayerToUnrealLayer: pDst text is null!");
		return;
	}
	
	
	//fast way
	if (pDestLayer->m_bUsedThisFrame)
	{
		//uh oh, we're overwriting an existing image.  Let's do fancy processing, can't use a memcpy
	
		uint32* pPixels32 = (uint32*)pDst;
		uint32* pSrcPixels32 = (uint32*)pSrcLayer->m_image;

		for (int y = 0; y < pSrcLayer->m_height; y++)
		{
			for (int x = 0; x < pSrcLayer->m_width; x++)
			{
				if (pSrcPixels32[y * pSrcLayer->m_width + x] != 0)
				{
					pPixels32[y * pSrcLayer->m_width + x] = pSrcPixels32[y * pSrcLayer->m_width + x];
				}
			}

		}
	}
	else
	{
		//fast way
		assert(pDestLayer->m_texPitchBytes == pSrcLayer->m_pitchBytes);
		memcpy(pDst, pSrcLayer->m_image, pSrcLayer->m_height * pSrcLayer->m_pitchBytes);
	}
	
}

void retro_video_refresh_callback_ex(const void* data, unsigned width, unsigned height, size_t pitch, const void* extraData)
{
	//first the main image

	retro_video_refresh_callback(data, width, height, pitch);
	Layer3DInfo* pLayerInfo = (Layer3DInfo*)extraData;
	
	for (int i = 0; i < g_pLibretroManager->m_pLibretroManagedActor->GetLayerCount(); i++) //yes, we're using layercount instead of C_MAX_3D_LAYERS on purpose
	{
		if (i > pLayerInfo->m_layerCount) continue;  //true later size hasn't been initted yet probably
		if (i >= g_pLibretroManager->m_pLibretroManagedActor->m_layerInfo.size()) continue;

		BlitCoreLayerToUnrealLayer(&pLayerInfo->m_pLayers[i], g_pLibretroManager->m_pLibretroManagedActor->GetLayer(i));
		g_pLibretroManager->m_pLibretroManagedActor->GetLayer(i)->m_bUsedThisFrame = true;
	}

}

//v2 of the callback above: the patched Azahar (3DS) core's depth-sliced layers.  Slice 0 is
//the NEAREST band while HoloVCS layer 0 is the DEEPEST plane, so the order flips here.  The
//blit is pitch-safe (slice dims can differ from the layer texture; rows are clipped).
void retro_video_refresh_callback_holo(const void* data, unsigned width, unsigned height, size_t pitch, const HoloLayerInfo* info)
{
	if (g_pLibretroManager->m_bDiscardVideoFrame) return; //frameskip junk frame: nobody will see it

	if (!info || info->abiVersion != HOLO_LAYER_ABI_VERSION)
	{
		static bool warned = false;
		if (!warned) { warned = true; LogMsg("holo callback: ABI mismatch (core %u, we want %d)", info ? info->abiVersion : 0, HOLO_LAYER_ABI_VERSION); }
		return;
	}

	ALibretroManagerActor* pActor = g_pLibretroManager->m_pLibretroManagedActor;
	const int unrealLayerCount = pActor->GetLayerCount();

	for (int slice = 0; slice < info->layerCount; slice++)
	{
		const int unrealLayer = (unrealLayerCount - 1) - slice; //near slice -> high (near) layer index
		if (unrealLayer < 0 || unrealLayer >= (int)pActor->m_layerInfo.size()) continue;

		const HoloLayerSlice& src = info->layers[slice];
		LayerInfo* pDestLayer = pActor->GetLayer(unrealLayer);
		uint8* pDstBase = pDestLayer->GetPixelBuffer();
		if (!pDstBase || !src.pixels) continue;

		//honor the core's per-slice `used` flag: an unused slice skips the copy (and its
		//GPU upload via m_bDirty), with a one-shot clear on the used->unused transition
		//so the texture doesn't keep showing stale content
		if (!src.used)
		{
			if (pDestLayer->m_bHoloContent)
			{
				memset(pDstBase, 0, pDestLayer->mDataSize);
				pDestLayer->m_bHoloContent = false;
				pDestLayer->m_bDirty = true;
			}
			continue;
		}

		const int rows = FMath::Min<int>((int)src.height, (int)pDestLayer->m_texHeight);
		const int rowBytes = FMath::Min<int>((int)src.pitchBytes, (int)pDestLayer->m_texPitchBytes);
		for (int y = 0; y < rows; y++)
		{
			memcpy(pDstBase + y * pDestLayer->m_texPitchBytes, src.pixels + (size_t)y * src.pitchBytes, rowBytes);
		}
		pDestLayer->m_bUsedThisFrame = true;
		pDestLayer->m_bHoloContent = true;
		pDestLayer->m_bDirty = true;
	}

	//the BOTTOM screen goes to the dedicated quad past the depth slices (see InitLayers)
	if (info->bottom.used && info->bottom.pixels && (int)pActor->m_layerInfo.size() > unrealLayerCount)
	{
		LayerInfo* pBottomLayer = pActor->GetLayer(unrealLayerCount);
		uint8* pDstBase = pBottomLayer->GetPixelBuffer();
		if (pDstBase)
		{
			const int rows = FMath::Min<int>((int)info->bottom.height, (int)pBottomLayer->m_texHeight);
			const int rowBytes = FMath::Min<int>((int)info->bottom.pitchBytes, (int)pBottomLayer->m_texPitchBytes);
			for (int y = 0; y < rows; y++)
			{
				memcpy(pDstBase + y * pBottomLayer->m_texPitchBytes, info->bottom.pixels + (size_t)y * info->bottom.pitchBytes, rowBytes);
			}
			if (!g_pLibretroManager->m_touchCursorShownOnce)
			{
				//show the touch cursor for a few seconds on the first real frame so the feature is discoverable
				g_pLibretroManager->m_touchCursorShownOnce = true;
				g_pLibretroManager->m_touchLastActiveTime = FPlatformTime::Seconds() + 3;
			}
			DrawTouchCursor(pDstBase, pBottomLayer->m_texWidth, pBottomLayer->m_texHeight, pBottomLayer->m_texPitchBytes);
			pBottomLayer->m_bUsedThisFrame = true;
			pBottomLayer->m_bHoloContent = true;
			pBottomLayer->m_bDirty = true;
		}
	}

	//ABI v4 quilt (multiview mode 2): the packed per-view quilt goes to the carrier quad
	//the LKG sprite path blits per-tile (see EnsureQuiltCarrier).  packSeq gates the 18MB
	//copy so cadence gaps and paused frames cost nothing.
	const HoloQuiltInfo& quilt = info->quilt;
	if (quilt.used && quilt.pixels && quilt.viewCount >= 2)
	{
		if (quilt.packSeq != g_pLibretroManager->m_lastQuiltPackSeq)
		{
			LayerInfo* pQuiltLayer = pActor->EnsureQuiltCarrier(quilt.width, quilt.height,
				quilt.viewCount, quilt.cols, quilt.rows);
			if (pQuiltLayer && pQuiltLayer->GetPixelBuffer())
			{
				const int rows = FMath::Min<int>((int)quilt.height, (int)pQuiltLayer->m_texHeight);
				const int rowBytes = FMath::Min<int>((int)quilt.pitchBytes, (int)pQuiltLayer->m_texPitchBytes);
				for (int y = 0; y < rows; y++)
				{
					memcpy(pQuiltLayer->GetPixelBuffer() + (size_t)y * pQuiltLayer->m_texPitchBytes,
						quilt.pixels + (size_t)y * quilt.pitchBytes, rowBytes);
				}
				pQuiltLayer->m_bUsedThisFrame = true;
				pQuiltLayer->m_bHoloContent = true;
				pQuiltLayer->m_bDirty = true;
				g_pLibretroManager->m_lastQuiltPackSeq = quilt.packSeq;
			}
		}
	}
	else
	{
		//quilt dormant (2D screen flat fallback / mode 1): tell the sprite path to skip
		//the quilt draw so the flat middle-band composite shows instead
		pActor->SetQuiltCarrierActive(false);
		g_pLibretroManager->m_lastQuiltPackSeq = -1;
	}
}

//Big obvious mouse pointer stamped onto the 3DS bottom-screen texture at the virtual touch
//cursor position (the hologram has no OS cursor, so we ARE the cursor).  White fill, black
//outline, 2x scale (24x36 px on the 320x240 screen).  Auto-hides after a few idle seconds.
void DrawTouchCursor(uint8* pDstBase, int texWidth, int texHeight, int texPitchBytes)
{
	const double now = FPlatformTime::Seconds();
	if (!g_pLibretroManager->m_touchDown && now - g_pLibretroManager->m_touchLastActiveTime > 4.0) return;

	//classic pointer proportions (about half as wide as tall) or it reads squished on the panel
	static const char* arrow[20] = {
		"X         ",
		"XX        ",
		"X.X       ",
		"X..X      ",
		"X...X     ",
		"X....X    ",
		"X.....X   ",
		"X......X  ",
		"X.......X ",
		"X........X",
		"X....XXXXX",
		"X..X.X    ",
		"X.X X.X   ",
		"XX  X.X   ",
		"X    X.X  ",
		"     X.X  ",
		"      X.X ",
		"      X.X ",
		"       X  ",
		"          ",
	};
	const int scale = 2;
	//draw at the latched point while pressing so the arrow marks EXACTLY where the tap registers
	const int cx = (int)g_pLibretroManager->GetTouchPointX();
	const int cy = (int)g_pLibretroManager->GetTouchPointY();
	const uint32 colFill = g_pLibretroManager->m_touchDown ? 0xFF60C0FFu : 0xFFFFFFFFu; //fill flashes blue while touching
	const uint32 colOutline = 0xFF000000u;

	for (int ay = 0; ay < 20; ay++)
	{
		for (int ax = 0; ax < 10; ax++)
		{
			const char c = arrow[ay][ax];
			if (c == ' ') continue;
			const uint32 col = (c == 'X') ? colOutline : colFill;
			for (int sy = 0; sy < scale; sy++)
			{
				for (int sx = 0; sx < scale; sx++)
				{
					const int px = cx + ax * scale + sx;
					const int py = cy + ay * scale + sy;
					if (px < 0 || px >= texWidth || py < 0 || py >= texHeight) continue;
					*(uint32*)(pDstBase + py * texPitchBytes + px * 4) = col;
				}
			}
		}
	}
}

void retro_video_refresh_callback(const void* data, unsigned width, unsigned height, size_t pitch)
{
	//LogMsg("Updating frame: %d, %d pitch %d", width, height, (int)pitch);
	const uint8* pNesTileIds = g_pLibretroManager->m_core.retro_get_holo_bg_tile_ids
		? g_pLibretroManager->m_core.retro_get_holo_bg_tile_ids()
		: nullptr;

	for (int pass = 0; pass < C_MAX_BLITPASS_COUNT; pass++)
	{
		BlitPass* pBlitPass = &g_pLibretroManager->m_blitPass[pass];
		if (!pBlitPass->m_bActive) break; //stop here - passes after a gap are IGNORED, so
		                                  //profiles must set up pass indices contiguously
		if (pBlitPass->m_bUseNesTileMask && !pNesTileIds) continue;

		uint8* pSrc = (uint8*)data;
		LayerInfo* pDestLayer = g_pLibretroManager->m_pLibretroManagedActor->GetLayer(pBlitPass->m_activeLayerIndex);
		if (!pDestLayer) return;
		pDestLayer->m_bUsedThisFrame = true;
		uint8* pDst = pDestLayer->GetPixelBuffer();

		if (!pDst)
		{
			LogMsg("retro_video_refresh_callback: pDst text is null!");
			return;
		}

		check(height <= pDestLayer->m_texHeight);

#if UE_BUILD_DEBUG
		//hack to show the pixel color of the bottom middle pixel of the area of a selected pass, so I know what to use for a colorkey
		/*if (pass == BLIT_PASS0)
		{
			pSrc = (uint8*)data + (pBlitPass->m_blitSrcRect.Min.Y * pitch) + (pBlitPass->m_blitSrcRect.Min.X + ((pBlitPass->m_blitSrcRect.Max.X - pBlitPass->m_blitSrcRect.Min.X) / 2));
			LogMsg("Center color is %d, %d, %d", pSrc[0], pSrc[1], pSrc[2]);
		}*/
#endif
		if (g_pLibretroManager->m_surfaceSourceType == SURFACE_SOURCE_RGBA_32_UNREAL)
		{
			memcpy(pDst, pSrc, height * pitch);
		} else
		if (g_pLibretroManager->m_surfaceSourceType == SURFACE_SOURCE_RGBA_32)
		{
			for (int y = pBlitPass->m_blitSrcRect.Min.Y; y < pBlitPass->m_blitSrcRect.Max.Y; y++)
			{
				pDst = pDestLayer->GetPixelBuffer() + (y * pDestLayer->m_texPitchBytes);
				pSrc = (uint8*)data + (y * pitch);

				//skip ahead a bit
				pSrc += pBlitPass->m_blitSrcRect.Min.X * 4;
				pDst += pBlitPass->m_blitSrcRect.Min.X * 4;

				//OPTIMIZE:  These copies could be sped up a lot, but considering this isn't where my slowdown is I'm not
				//really caring now
				for (int x = pBlitPass->m_blitSrcRect.Min.X; x < pBlitPass->m_blitSrcRect.Max.X; x++)
				{
					if (pBlitPass->m_bUseNesTileMask)
					{
						const uint8 tile = pNesTileIds[y * 256 + x];
						if (!(pBlitPass->m_nesTileMask[tile >> 3] & (1U << (tile & 7))))
						{
							pDst += 4;
							pSrc += 4;
							continue;
						}
					}

					pDst[0] = pSrc[0]; //red
					pDst[1] = pSrc[1]; //green
					pDst[2] = pSrc[2]; //blue

					switch (pBlitPass->m_blitColorKeyStyle)
					{

					case COLOR_KEY_STYLE_BLACK:

						if (pSrc[0] == 0 && pSrc[1] == 0 && pSrc[2] == 0)
						{
							pDst[3] = 0; //transparent
						}
						else
						{
							pDst[3] = 255; //alpha
						}
						break;

					case COLOR_KEY_STYLE_1COLOR:

						if (pSrc[0] == pBlitPass->m_blitColorKey.R && pSrc[1] == pBlitPass->m_blitColorKey.G
							&& pSrc[2] == pBlitPass->m_blitColorKey.B)
						{
							pDst[3] = 0; //transparent
						}
						else
						{
							pDst[3] = 255; //alpha
						}
						break;

					default:
						pDst[3] = 255; //alpha

						break;
					}

					pDst += 4;
					pSrc += 4;
				}
			}
		} else

		if (g_pLibretroManager->m_surfaceSourceType == SURFACE_SOURCE_RGB_565_16)
		{
			for (int y = pBlitPass->m_blitSrcRect.Min.Y; y < pBlitPass->m_blitSrcRect.Max.Y; y++)
			{
				pDst = pDestLayer->GetPixelBuffer() + (y * pDestLayer->m_texPitchBytes);
				pSrc = (uint8*)data + (y * pitch);

				//skip ahead a bit?
				pSrc += pBlitPass->m_blitSrcRect.Min.X * 2;
				pDst += pBlitPass->m_blitSrcRect.Min.X * 4;

				//OPTIMIZE:  These copies could be sped up a lot, but considering this isn't where my slowdown is I'm not
				//really caring now
				uint8_t r, g, b;

				for (int x = pBlitPass->m_blitSrcRect.Min.X; x < pBlitPass->m_blitSrcRect.Max.X; x++)
				{
					if (pBlitPass->m_bUseNesTileMask)
					{
						const uint8 tile = pNesTileIds[y * 256 + x];
						if (!(pBlitPass->m_nesTileMask[tile >> 3] & (1U << (tile & 7))))
						{
							pDst += 4;
							pSrc += 2;
							continue;
						}
					}

					uint16 color = *((uint16*)&pSrc[0]);

					r = ((((color >> 11) & 0x1F) * 527) + 23) >> 6;
					g = ((((color >> 5) & 0x3F) * 259) + 33) >> 6;
					b = (((color & 0x1F) * 527) + 23) >> 6;

				
					switch (pBlitPass->m_blitColorKeyStyle)
					{

					case COLOR_KEY_STYLE_BLACK:

						if (r == 0 && g == 0 && b == 0)
						{
							//pDst[3] = 0; //transparent
						}
						else
						{
							pDst[2] = r;
							pDst[1] = g;
							pDst[0] = b;

							pDst[3] = 255; //alpha
						}
						break;

					case COLOR_KEY_STYLE_1COLOR:

						if (r == pBlitPass->m_blitColorKey.R && g == pBlitPass->m_blitColorKey.G
							&& b == pBlitPass->m_blitColorKey.B)
						{
							//pDst[3] = 0; //transparent
						}
						else
						{
							pDst[2] = r;
							pDst[1] = g;
							pDst[0] = b;

							pDst[3] = 255; //alpha
						}
						break;

					case COLOR_KEY_STYLE_FILL:

							pDst[2] = pBlitPass->m_blitColorKey.R;
							pDst[1] = pBlitPass->m_blitColorKey.G;
							pDst[0] = pBlitPass->m_blitColorKey.B;
							pDst[3] = pBlitPass->m_blitColorKey.A;

						break;

					case COLOR_KEY_STYLE_2COLOR:

						if (r == pBlitPass->m_blitColorKey.R && g == pBlitPass->m_blitColorKey.G
							&& b == pBlitPass->m_blitColorKey.B
							||
							r == pBlitPass->m_blitColorKey2.R && g == pBlitPass->m_blitColorKey2.G
							&& b == pBlitPass->m_blitColorKey2.B)
						{
							//pDst[3] = 0; //transparent
						}
						else
						{
							pDst[2] = r;
							pDst[1] = g;
							pDst[0] = b;

							pDst[3] = 255; //alpha
						}
						break;

					default:
						pDst[2] = r;
						pDst[1] = g;
						pDst[0] = b;

						pDst[3] = 255; //alpha

						break;
					}

					pDst += 4;
					pSrc += 2;
				}
			}
		}

	}
}

void ClearLayers()
{
	for (int i = 0; i < g_pLibretroManager->m_pLibretroManagedActor->m_layerInfo.size(); i++)
	{
		LayerInfo* pDestLayer = g_pLibretroManager->m_pLibretroManagedActor->GetLayer(i);
		uint8* pDst = pDestLayer->GetPixelBuffer();
		memset(pDst, 0, pDestLayer->mDataSize);
	}

	g_pLibretroManager->ResetBlitInformation();
}

void LibretroManager::ResetBlitInformation()
{
	for (int i = 0; i < m_pLibretroManagedActor->m_layerInfo.size(); i++)
	{
		LayerInfo* pDestLayer = g_pLibretroManager->m_pLibretroManagedActor->GetLayer(i);
		pDestLayer->m_bUsedThisFrame = false;
		pDestLayer->m_distanceMod = 0.0f;
	}
}

void LibretroManager::RenderFrame(const char* pRenderFlags)
{
	//A filter is scoped to one explicit filtered render.  Clearing it here also makes ROM
	//switches and early-returning profile paths safe if a previous visual pass used one.
	if (m_core.retro_set_holo_bg_tile_filter)
	{
		m_core.retro_set_holo_bg_tile_filter(nullptr, 0);
	}
	strcpy(m_coreRenderFlags, pRenderFlags);
	m_core.retro_run(); 
}

//NOTE: a RefreshPausedFrame mechanism lived here briefly (Aug 27 2026): it re-rendered
//the frozen 3DS screen after a paused viz/depth change via a core-side savestate pin.
//Seth cut it - even with a fast render path plus a debounced rewind, the state
//round-trips were seconds-long hitches and felt like bad UI.  Paused changes that would
//need a core re-render are now refused outright with a status message instead
//(ALibretroManagerActor::RefusePausedHoloChange).

bool LibretroManager::RenderFrameWithNesBackgroundTileFilter(const char* pRenderFlags, const byte* pKeepList, int keepListSize, byte replacementTile)
{
	if (!m_core.retro_set_holo_bg_tile_filter || !pKeepList || keepListSize <= 0)
	{
		return false;
	}

	uint8 allowedMask[32] = {};
	for (int i = 0; i < keepListSize; i++)
	{
		const uint8 tile = pKeepList[i];
		allowedMask[tile >> 3] |= (uint8)(1U << (tile & 7));
	}

	strcpy(m_coreRenderFlags, pRenderFlags);
	m_core.retro_set_holo_bg_tile_filter(allowedMask, replacementTile);
	m_core.retro_run();
	m_core.retro_set_holo_bg_tile_filter(nullptr, 0);
	return true;
}

void LibretroManager::SetFrameSkip(int frameSkip)
{
	m_frameSkip = frameSkip;
	ShowStatusMessage(string("Frameskip: " + toString(m_frameSkip)));
}

string LibretroManager::GetSaveStateDir()
{
	return m_rootPath + "saves/" + m_romDir + "/";
}

string LibretroManager::GetSaveStatePath()
{
	return GetSaveStateDir() + GetFileNameWithoutExtension(m_curRomName) + ".sav0";
}

void LibretroManager::SaveStateToFile()
{

	if (!m_core.m_bActive)
	{
		ShowStatusMessage("No rom loaded");
		return;
	}

	if (m_emulatorType == EMULATOR_3DS)
	{
		//the azahar core's serialize round-trip works (the paused-refresh pin runs on
		//it); only the fixed-slot machinery is unsuitable (variable state size), so 3DS
		//gets its own fresh-serialize file path
		Save3DSStateToFile();
		return;
	}

	if (m_maxSaveStateSize <= 0)
	{
		//without this guard we'd write a 0-byte .sav0 and claim "Saved state."
		ShowStatusMessage("This system doesn't support save states");
		return;
	}

	SaveState(C_SAVE_STATE_USER_SLOT);

	//well, we have the data but now we need to save it to disk

	TArray<uint8> data(m_pSaveStateBuffer[C_SAVE_STATE_USER_SLOT], m_maxSaveStateSize);
	IFileManager::Get().MakeDirectory(ANSI_TO_TCHAR(GetSaveStateDir().c_str()), true);
	string fileName = GetSaveStatePath();
	LogMsg("Saving state to %s", fileName.c_str());
	FFileHelper::SaveArrayToFile(data, ANSI_TO_TCHAR( fileName.c_str()));
	ShowStatusMessage("Saved state.");
}

void LibretroManager::LoadStateFromFile()
{
	if (!m_core.m_bActive)
	{
		ShowStatusMessage("No rom loaded");
		return;
	}

	if (m_emulatorType == EMULATOR_3DS)
	{
		Load3DSStateFromFile();
		return;
	}

	if (m_maxSaveStateSize <= 0)
	{
		ShowStatusMessage("This system doesn't support save states");
		return;
	}

	//well, we have a place in memory to put it..

	//yes, we're copying data in to it for no reason
	TArray<uint8> data(m_pSaveStateBuffer[C_SAVE_STATE_USER_SLOT], m_maxSaveStateSize);

	string fileName = GetSaveStatePath();

	if (!FPaths::FileExists(FString(fileName.c_str())))
	{
		//older builds wrote the .sav0 next to the top-level exe; migrate it into saves/ so it keeps
		//working and the top folder cleans itself up
		string legacyFileName = m_rootPath + GetFileNameWithoutExtension(m_curRomName) + ".sav0";
		if (FPaths::FileExists(FString(legacyFileName.c_str())))
		{
			IFileManager::Get().MakeDirectory(ANSI_TO_TCHAR(GetSaveStateDir().c_str()), true);
			if (IFileManager::Get().Move(ANSI_TO_TCHAR(fileName.c_str()), ANSI_TO_TCHAR(legacyFileName.c_str())))
			{
				LogMsg("Migrated legacy save state %s to %s", legacyFileName.c_str(), fileName.c_str());
			}
			else
			{
				fileName = legacyFileName; //move failed (locked/read-only?), just load it where it is
			}
		}
		else
		{
			ShowStatusMessage("No state save exists yet for this rom");
			return;
		}
	}

	LogMsg("Loading state from %s", fileName.c_str());
	FFileHelper::LoadFileToArray(data, ANSI_TO_TCHAR(fileName.c_str()));

	ShowStatusMessage("Loaded state.");
	memcpy(m_pSaveStateBuffer[C_SAVE_STATE_USER_SLOT], data.GetData(), data.Num());
	m_nesHacker.Reset();
	LoadState(C_SAVE_STATE_USER_SLOT);
	SaveState(0); //we load from state 0 every frame to reset the gamelogic we've broken due to multiple renderings for the layers
}

//3DS file savestates (Aug 27 2026, Seth request): the azahar libretro layer implements a
//real serialize round-trip (System::SaveStateBuffer behind retro_serialize, with async
//draining) - the old "always fails" verdict came from a boot-time probe that failed
//before the game ran.  The state is a variable-size zstd blob (tens of MB), so it flows
//through a fresh temporary buffer per save instead of the fixed slot buffers;
//m_maxSaveStateSize stays 0 for 3DS so the per-frame rewind machinery keeps no-oping.
void LibretroManager::Save3DSStateToFile()
{
	const size_t stateSize = m_core.retro_serialize_size();
	if (stateSize == 0)
	{
		ShowStatusMessage("Save state failed (core busy, try again)");
		return;
	}
	TArray<uint8> data;
	data.SetNumUninitialized((int64)stateSize);
	if (!m_core.retro_serialize(data.GetData(), stateSize))
	{
		ShowStatusMessage("Save state failed");
		return;
	}
	IFileManager::Get().MakeDirectory(ANSI_TO_TCHAR(GetSaveStateDir().c_str()), true);
	string fileName = GetSaveStatePath();
	LogMsg("Saving 3DS state to %s (%d KB)", fileName.c_str(), (int)(stateSize / 1024));
	if (FFileHelper::SaveArrayToFile(data, ANSI_TO_TCHAR(fileName.c_str())))
	{
		ShowStatusMessage("Saved state.");
	}
	else
	{
		ShowStatusMessage("Save state failed (couldn't write file)");
	}
}

void LibretroManager::Load3DSStateFromFile()
{
	string fileName = GetSaveStatePath();
	if (!FPaths::FileExists(FString(fileName.c_str())))
	{
		ShowStatusMessage("No state save exists yet for this rom");
		return;
	}
	TArray<uint8> data;
	if (!FFileHelper::LoadFileToArray(data, ANSI_TO_TCHAR(fileName.c_str())) || data.Num() == 0)
	{
		ShowStatusMessage("Load state failed (couldn't read file)");
		return;
	}
	LogMsg("Loading 3DS state from %s (%d KB)", fileName.c_str(), data.Num() / 1024);
	if (m_core.retro_unserialize(data.GetData(), (size_t)data.Num()))
	{
		LogMsg("3DS state load OK");
		ShowStatusMessage("Loaded state.");
		//while paused, run a few muted frames so the frozen screen shows the LOADED
		//state instead of the stale pre-load frame (no savestate tricks here - you just
		//loaded, so a 4/60s position shift is meaningless)
		if (m_bGamePaused)
		{
			const bool bAudioWas = m_useAudio;
			m_useAudio = false;
			for (int i = 0; i < 4; i++)
			{
				RenderFrame("11");
			}
			m_useAudio = bAudioWas;
		}
	}
	else
	{
		//covers the legacy bogus 0-byte-era .sav0 files and truly incompatible states
		LogMsg("3DS state load REJECTED by the core");
		ShowStatusMessage("Load state failed (state from another game or core version?)");
	}
}

void LibretroManager::Update()
{
	m_autoHarness.Update(); //before the early-outs so pause/unpause and rom switching work anytime

	if (!m_core.m_bActive) { m_timeOfLastFrame = 0; return; }

	if (m_bGamePaused) { m_timeOfLastFrame = 0; return; }

	m_helpScreen.TickAutoShow(); //after the early-outs so the diorama has frames behind the panel

	//Emulation is wall-clock driven, not render-driven. The audio stream only advances when the
	//core runs, so if we ran it exactly once per rendered frame, every dropped/late frame (a
	//missed vblank, a Bridge stall, a GC spike) punched a 17ms hole in the audio. Instead we keep
	//a phase-locked frame deadline and run however many emulator frames are owed (capped) so a
	//late tick runs the core twice and the audio stays continuous; the diorama just skips a frame.
	//This wait is also what caps the whole app at m_targetFPS - the 0 hotkey sets m_bUncapFPS to
	//bypass it so the fps counter can show true throughput. Sleep for the bulk of the wait and
	//only spin the last ~1.5ms: a pure busy-wait burned a full CPU core every frame.
	//NOTE: the deadline advances by whole intervals (m_timeOfLastFrame += n*interval), NOT to
	//"now": the old "now" version accumulated sleep overshoot every frame, which beat against
	//vsync and dropped a vblank every few seconds (the 57-58 fps seconds in log.txt) - that was
	//the source of the periodic audio clicks.
	int framesToRun = 1;
	m_lastPaceWaitSeconds = 0;
	if (m_frameSkip == 0 && m_targetFPS != 0 && !m_bUncapFPS)
	{
		const double waitStart = FPlatformTime::Seconds();
		const double desiredInterval = 1.0 / m_targetFPS;
		//a 3DS frame can cost 10ms+; running 3 in one tick to catch up would hitch, so no catch-up there
		const int maxCatchUpFrames = (m_emulatorType == EMULATOR_3DS) ? 1 : 3;
		double now = FPlatformTime::Seconds();

		if (m_timeOfLastFrame == 0 || now - m_timeOfLastFrame > desiredInterval * (maxCatchUpFrames + 1))
		{
			//first frame, resumed from pause, or a real stall: resync rather than fast-forward
			m_timeOfLastFrame = now - desiredInterval;
		}

		while (1)
		{
			double remaining = (m_timeOfLastFrame + desiredInterval) - FPlatformTime::Seconds();
			if (remaining <= 0) break;
			if (remaining > 0.002)
			{
				FPlatformProcess::SleepNoStats((float)(remaining - 0.0015));
			}
			//else: spin the final stretch for frame-accurate pacing
		}

		now = FPlatformTime::Seconds();
		m_lastPaceWaitSeconds = now - waitStart;
		framesToRun = (int)((now - m_timeOfLastFrame) / desiredInterval);
		framesToRun = FMath::Clamp(framesToRun, 1, maxCatchUpFrames);
		m_timeOfLastFrame += framesToRun * desiredInterval;
		m_catchUpFrames += framesToRun - 1;
	}

	const double emuStart = FPlatformTime::Seconds();
	for (int i = 0; i < framesToRun; i++)
	{
		m_useAudio = true;
		m_profManager.Update();
	}
	m_lastEmuUpdateSeconds = FPlatformTime::Seconds() - emuStart;

	//harness-injected button holds tick down once per visible frame (not per rewound render pass)
	for (int i = 0; i < C_MAX_JOYPAD_BUTTONS; i++)
	{
		if (m_autoButtonHoldFrames[i] > 0) m_autoButtonHoldFrames[i]--;
	}
}
