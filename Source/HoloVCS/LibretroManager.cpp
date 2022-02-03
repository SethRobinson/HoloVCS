#include "LibretroManager.h"
#include "LibretroManagerActor.h"
#include "PlayerPawn.h"

#include "StatusDisplayActor.h" //so we can show messages on screen

//SETH:  If I don't set these, we can't get SetProcessDpiAwareness

#if UE_BUILD_DEBUG
const int C_DEFAULT_ROM_ID = 1;
bool g_loadStateOnFirstLoad = true;
string g_partialRomNameToLoadOnStartup = "mario";
#else
const int C_DEFAULT_ROM_ID = 0;
bool g_loadStateOnFirstLoad = false;
string g_partialRomNameToLoadOnStartup = "";
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

#pragma warning(disable:4191)

const unsigned short ASYNC_BUTTON_DOWN_MSB = 0x8000;

string G_VERSION_STRING = "HoloVCS V0.6";

LibretroManager* g_pLibretroManager = NULL; //I don't want to fool with caring how to get Unreal globals correctly
void retro_video_refresh_callback(const void* data, unsigned width, unsigned height, size_t pitch);
#include <thread>

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
}

LibretroManager::LibretroManager()
{
	for (int i = 0; i < C_SAVE_STATE_COUNT; i++)
	{
		m_pSaveStateBuffer[i] = NULL;
	}
}

LibretroManager::~LibretroManager()
{
	Kill();
	g_pLibretroManager = NULL;

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
	m_blitPass[blitPassIndex].m_activeLayerIndex = layer;
	m_blitPass[blitPassIndex].m_blitColorKeyStyle = colorKeyStyle;
	m_blitPass[blitPassIndex].m_blitSrcRect = srcRect;
	m_blitPass[blitPassIndex].m_blitColorKey = colorKey;
	m_blitPass[blitPassIndex].m_blitColorKey2 = colorKey2;
}

#define GET_VARIABLE_NAME(Variable) (#Variable)

FARPROC MapFunction(HINSTANCE m_dllHandle, char* varName)
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

void LibretroManager::FreeEmulatorIfNeeded()
{
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
}
 
bool LibretroManager::LoadCore(string fileName)
{
	FreeEmulatorIfNeeded();

	m_dllHandle = LoadLibraryA(fileName.c_str());

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
	m_core.retro_set_audio_sample = (decltype(m_core.retro_set_audio_sample))MapFunction(m_dllHandle, GET_VARIABLE_NAME(m_core.retro_set_audio_sample));
	m_core.retro_set_audio_sample_batch = (decltype(m_core.retro_set_audio_sample_batch))MapFunction(m_dllHandle, GET_VARIABLE_NAME(m_core.retro_set_audio_sample_batch));
	m_core.retro_set_input_poll = (decltype(m_core.retro_set_input_poll))MapFunction(m_dllHandle, GET_VARIABLE_NAME(m_core.retro_set_input_poll));
	m_core.retro_set_input_state = (decltype(m_core.retro_set_input_state))MapFunction(m_dllHandle, GET_VARIABLE_NAME(m_core.retro_set_input_state));
	m_core.retro_load_game = (decltype(m_core.retro_load_game))MapFunction(m_dllHandle, GET_VARIABLE_NAME(m_core.retro_load_game));
	m_core.retro_get_system_av_info = (decltype(m_core.retro_get_system_av_info))MapFunction(m_dllHandle, GET_VARIABLE_NAME(m_core.retro_get_system_av_info));
	m_core.retro_run = (decltype(m_core.retro_run))MapFunction(m_dllHandle, GET_VARIABLE_NAME(m_core.retro_run));
	m_core.retro_unload_game = (decltype(m_core.retro_unload_game))MapFunction(m_dllHandle, GET_VARIABLE_NAME(m_core.retro_unload_game));
	m_core.retro_serialize_size = (decltype(m_core.retro_serialize_size))MapFunction(m_dllHandle, GET_VARIABLE_NAME(m_core.retro_serialize_size));
	m_core.retro_serialize = (decltype(m_core.retro_serialize))MapFunction(m_dllHandle, GET_VARIABLE_NAME(m_core.retro_serialize));
	m_core.retro_unserialize = (decltype(m_core.retro_unserialize))MapFunction(m_dllHandle, GET_VARIABLE_NAME(m_core.retro_unserialize));

	return true;
}

void libretro_log(enum retro_log_level level, const char* traceStr, ...)
{
	va_list argsVA;
	const int logSize = 1024 * 10;
	char buffer[logSize];
	memset((void*)buffer, 0, logSize);
	va_start(argsVA, traceStr);
	vsnprintf_s(buffer, logSize, logSize, traceStr, argsVA);
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
		

		    //here we change the palette to pure RGB, easier to setup colorkeys as I can also set
		    //mesen to "RGB (Nestopia)" and the palettes will exactly match
			if (strcmp(pVar->key, "fceumm_palette") == 0)
			{
				pVar->value = "rgb";
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
		bool* updated = (bool*)data;
		*updated = true;
	}
	break;

	case RETRO_ENVIRONMENT_SET_GEOMETRY:
	{
		retro_system_av_info* pInfo = (retro_system_av_info*)data;

		LogMsg("Update from core says screen is %d, %d, but max is %d, %d.", pInfo->geometry.base_width, pInfo->geometry.base_height,
			pInfo->geometry.max_width, pInfo->geometry.max_height);
		LogMsg("FPS %f, sample rate: %f", (float)pInfo->timing.fps, (float)pInfo->timing.sample_rate);

		g_pLibretroManager->m_pLibretroManagedActor->SetSampleRate(pInfo->timing.sample_rate);
		g_pLibretroManager->m_audioStatisticsTimer = FPlatformTime::Seconds();
		g_pLibretroManager->m_framesWrittenInPeriod = 0;
		break;
	}

	default:

		LogMsg("Got unhandled cmd %u", cmd);
		return false;
		break;
	}

	return true;
}

bool LibretroManager::LoadRom(string fileName)
{
	retro_game_info ginfo;
	memset(&ginfo, 0, sizeof(ginfo));
	
	m_romDataArray.Empty();

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
	
	m_profManager.InitGame(m_romHash);

	return true;
}

void retro_audio_sample_callback(int16_t left, int16_t right)
{
	LogMsg("Got sample callback");
	return;
}

void LibretroManager::SetSampleRate()
{
	//time to computer stuff
	float timeTaken = (FPlatformTime::Seconds() - m_audioStatisticsTimer);
	const float SECONDS_REQUIRED = 10;

	if (timeTaken < SECONDS_REQUIRED)
	{
		//Sorry, I refuse to use the fstring stuff
		char st[256];
		sprintf_s(st, "Try again in %.2f seconds, we need more time to measure audio speed", SECONDS_REQUIRED - timeTaken);
		ShowStatusMessage(st);
		return;
	}

	m_framesWrittenInLastPeriod = (double)m_framesWrittenInPeriod / (FPlatformTime::Seconds() - m_audioStatisticsTimer);
	ShowStatusMessage(string("Samplerate: ") + toString(m_framesWrittenInLastPeriod));
	m_framesWrittenInPeriod = 0;
	
	if (m_framesWrittenInLastPeriod < 3)
	{
		ShowStatusMessage(string("Invalid sample rate, ignoring - ") + toString(m_framesWrittenInLastPeriod));
	}
	else
	{
		m_pLibretroManagedActor->SetSampleRate(m_framesWrittenInLastPeriod);
		m_audioStatisticsTimer = FPlatformTime::Seconds();
	}
}

void LibretroManager::UpdateAudioStatistics(int framesWritten)
{
	m_framesWrittenInPeriod += framesWritten;
}

size_t retro_audio_sample_batch_callback(const int16_t* data, size_t frames)
{
	
	if (!g_pLibretroManager->m_useAudio) return frames; //this audio can be trashed, it's probably because I couldn't figure out how to turn it off when doing
														//some extra visual renders
	auto* pAudioBufferComp = g_pLibretroManager->m_pLibretroManagedActor->m_pRTAudioBufferComponent;

	g_pLibretroManager->UpdateAudioStatistics(frames);

	if (pAudioBufferComp->GetBufferGenerator())
	{
		int curSamplesInBuffer = pAudioBufferComp->GetBufferGenerator()->m_samplesInBuffer;

		if (curSamplesInBuffer < 4096)
		{
			RTSampleChunk chunk;

			int sampleSize = frames;

			chunk.pSampleData = new float[sampleSize];
			chunk.validSamples = sampleSize;

			//copy it into the buffer
			for (unsigned int i = 0; i < frames; i++)
			{
				//LogMsg(" %d - %.2f", (int)data[i * 2], (float)fFrame);
				chunk.pSampleData[i] = ((float)data[i * 2]) / (32768.0f * 2);
			}

			pAudioBufferComp->GetBufferGenerator()->AddChunkSchedule(chunk);
		}
		else
		{
			//LogMsg("(too much data)");
		}
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
			return g_pLibretroManager->m_joyPad.m_button[id];
		}
		else
		{
			LogMsg("Unhandled button: %d", id);
		}
	}
	return 0;
}
 

void LibretroManager::SetEmulatorData(eEmulatorType emu)
{
	m_emulatorType = emu;
	string rom = "unset";
	
	switch (emu)
	{

	case EMULATOR_ATARI:
		m_coreName = "stella_libretro.dll";
		m_surfaceSourceType = SURFACE_SOURCE_RGBA_32;
		m_romDir = "atari2600";
		m_romFileExtension1 = ".a26";
		m_romFileExtension2 = ".bin";
		m_coreLayerScale = FVector2D(4.45, 3.5);
		m_corePosition = FVector2D(103.0, 213);
		break;

	case EMULATOR_NES:
		m_coreName = "fceumm_libretro.dll";
		m_surfaceSourceType = SURFACE_SOURCE_RGB_565_16;
		m_romDir = "nes";
		m_romFileExtension1 = ".nes";
		m_romFileExtension2 = ".unusedcrap";
		m_coreLayerScale = FVector2D(2.8, 2.8);
		m_corePosition = FVector2D(23.0, 213);
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
		MessageBox(NULL, (LPCWSTR)L"No game roms found.\nPut some in the atari2600 or nes dir first!\nCheck readme for which games are supported.",
			(LPCWSTR)L"Add game roms!",
			MB_ICONWARNING | MB_OK | MB_DEFAULT_DESKTOP_ONLY);
		
		return;
	}

	if (g_loadStateOnFirstLoad && !g_partialRomNameToLoadOnStartup.empty())
	{
		SetRomToLoadByPartialFileName(g_partialRomNameToLoadOnStartup);
	}

	SetEmulatorData((eEmulatorType) m_emulatorIDList[m_activeRomIndex]);
	
	if (m_romNameFileList.Num() > 0)
	{
		m_curRomName = toString(m_romNameFileList[m_activeRomIndex]);
	}
	strcpy_s(m_coreRenderFlags, "1111111");

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
	m_core.retro_set_audio_sample(retro_audio_sample_callback);

	m_core.retro_set_audio_sample_batch(retro_audio_sample_batch_callback);
	m_core.retro_set_input_poll(retro_input_poll_callback);
	m_core.retro_set_input_state(retro_input_state_callback);

	m_core.retro_init();

	m_romPath = m_rootPath + m_romDir + "/";

	if (!LoadRom(m_romPath + m_curRomName))
	{
		string msg = "ERROR: Place rom (";
		msg += m_romFileExtension1 + ") in " + m_romPath + " dir!";
		ShowStatusMessage(msg.c_str(), 100);
		LogMsg(msg.c_str());
		return;
	}
	m_nesHacker.Reset();

	ShowStatusMessage(G_VERSION_STRING + " Loaded " + m_curRomName, 4);

	m_maxSaveStateSize = m_core.retro_serialize_size();
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
	else
	{
		LogMsg("Serious error with savestate reporting, can't continue");
		return;
	}

	m_core.m_bActive = true;
	
	DisableAllBlitPasses();

	//setup timing based on what the core tells us
	m_audioStatisticsTimer = FPlatformTime::Seconds();
	m_framesWrittenInPeriod = 0;




	if (g_loadStateOnFirstLoad)
	{
		g_loadStateOnFirstLoad = false;
		LoadStateFromFile();
	}

	if (m_emulatorType == EMULATOR_ATARI)
	{
		SetFrameSkip(1);
	}
	 
	if (m_emulatorType == EMULATOR_NES)
	{
		SetFrameSkip(0);
		m_pLibretroManagedActor->SetSampleRate(44100);
		m_audioStatisticsTimer = FPlatformTime::Seconds();
		m_framesWrittenInPeriod = 0;
	}

	m_core.retro_run();
	SaveState(0);
	ClearAllLayers(); //don't want leftovers from previous games/renders showing up
	
	m_pLibretroManagedActor->SetScaleLayersXY(m_coreLayerScale.X, m_coreLayerScale.Y);
	m_pLibretroManagedActor->SetLayersPosXY(m_corePosition.X, m_corePosition.Y);
}

void LibretroManager::ClearAllLayers()
{
	for (int i = 0; i < C_LAYER_COUNT; i++)
	{
		LayerInfo* pDestLayer = g_pLibretroManager->m_pLibretroManagedActor->GetLayer(i);
		if (!pDestLayer->GetPixelBuffer()) continue;
		uint8* pDst = pDestLayer->GetPixelBuffer();
		
		memset(pDst, 0, pDestLayer->m_texWidth * pDestLayer->m_texHeight * 4);
	}
}

void LibretroManager::ResetRom()
{
	m_nesHacker.Reset();
	LoadState(0);
	m_core.retro_reset();
	m_core.retro_run();
	SaveState(0);
}

void LibretroManager::LoadRomList()
{

	string romFileName;

	FString FullPath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*FPaths::ProjectDir());
	m_rootPath = StringCast<ANSICHAR>(*FullPath).Get();
	FString testPath = FullPath + "atari2600";

	if (!FPaths::DirectoryExists(testPath))
	{
		m_rootPath += "../";
		LogMsg("Adding ../ to path due to detected release dir layout");
	}

	m_romNameFileList.Empty();
	m_emulatorIDList.Empty();

	for (int i = 0; i < EMULATOR_COUNT; i++)
	{
		SetEmulatorData((eEmulatorType)i);
		m_romPath = m_rootPath + m_romDir + "/";
		//get list of roms and play the first one
		int romCountBeforeAdding = m_romNameFileList.Num();
		
		IFileManager::Get().FindFiles(m_romNameFileList, ANSI_TO_TCHAR(m_romPath.c_str()), ANSI_TO_TCHAR(m_romFileExtension1.c_str()));
		IFileManager::Get().FindFiles(m_romNameFileList, ANSI_TO_TCHAR(m_romPath.c_str()), ANSI_TO_TCHAR(m_romFileExtension2.c_str()));

		int romsFound = m_romNameFileList.Num() - romCountBeforeAdding;

		LogMsg("Scanning %s dir (for %s), found %d roms (%d total)", m_romPath.c_str(), m_coreName.c_str(), romsFound, m_romNameFileList.Num());

		for (int j = 0; j < romsFound; j++)
		{
			m_emulatorIDList.Add(i);
		}
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

	LogMsg("Let's init the emu core we want from its dll!");
	
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
	LoadRomList();
	SetRomByIndex(C_DEFAULT_ROM_ID);
	InitEmulator();
	
}

bool LibretroManager::SaveState(int index)
{
	
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

void retro_video_refresh_callback(const void* data, unsigned width, unsigned height, size_t pitch)
{
	//LogMsg("Updating frame: %d, %d pitch %d", width, height, (int)pitch);
	
	for (int pass = 0; pass < C_MAX_BLITPASS_COUNT; pass++)
	{
		BlitPass* pBlitPass = &g_pLibretroManager->m_blitPass[pass];
		if (!pBlitPass->m_bActive) break; //stop here

		uint8* pSrc = (uint8*)data;
		LayerInfo* pDestLayer = g_pLibretroManager->m_pLibretroManagedActor->GetLayer(pBlitPass->m_activeLayerIndex);

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
		}

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
	for (int i = 0; i < C_LAYER_COUNT; i++)
	{
		LayerInfo* pDestLayer = g_pLibretroManager->m_pLibretroManagedActor->GetLayer(i);
		uint8* pDst = pDestLayer->GetPixelBuffer();
		memset(pDst, 0, pDestLayer->mDataSize);
	}
	
	
}

void LibretroManager::RenderFrame(const char* pRenderFlags)
{
	strcpy_s(m_coreRenderFlags, pRenderFlags);
	m_core.retro_run(); 
}

void LibretroManager::SetFrameSkip(int frameSkip)
{
	if (frameSkip != m_frameSkip)
	{
		m_audioStatisticsTimer = FPlatformTime::Seconds();
		m_framesWrittenInPeriod = 0;
	}

	m_frameSkip = frameSkip;
	ShowStatusMessage(string("Frameskip: " + toString(m_frameSkip)));
}

void LibretroManager::SaveStateToFile()
{

	if (!m_core.m_bActive)
	{
		ShowStatusMessage("No rom loaded");
		return;
	}
	
	SaveState(C_SAVE_STATE_USER_SLOT);

	//well, we have the data but now we need to save it to disk

	TArray<uint8> data(m_pSaveStateBuffer[C_SAVE_STATE_USER_SLOT], m_maxSaveStateSize);
	string fileName = m_rootPath + GetFileNameWithoutExtension(m_curRomName) + ".sav0";
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

	//well, we have a place in memory to put it..

	//yes, we're copying data in to it for no reason
	TArray<uint8> data(m_pSaveStateBuffer[C_SAVE_STATE_USER_SLOT], m_maxSaveStateSize);
	
	string fileName = m_rootPath + GetFileNameWithoutExtension(m_curRomName) + ".sav0";
	
	if (!FPaths::FileExists(FString(fileName.c_str())))
	{
		ShowStatusMessage("No state save exists yet for this rom");
  	    return;
	}

	LogMsg("Loading state from %s", fileName.c_str());
	FFileHelper::LoadFileToArray(data, ANSI_TO_TCHAR(fileName.c_str()));

	ShowStatusMessage("Loaded state.");
	memcpy(m_pSaveStateBuffer[C_SAVE_STATE_USER_SLOT], data.GetData(), data.Num());
	m_nesHacker.Reset();
	LoadState(C_SAVE_STATE_USER_SLOT);
	SaveState(0); //we load from state 0 every frame to reset the gamelogic we've broken due to multiple renderings for the layers
}



void LibretroManager::Update()
{
	if (!m_core.m_bActive) return;
	
	m_useAudio = true;

	m_profManager.Update();

}
