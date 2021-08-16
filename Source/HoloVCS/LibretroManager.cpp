#include "LibretroManager.h"
#include "LibretroManagerActor.h"
#include "StatusDisplayActor.h" //so we can show messages on screen

const unsigned short ASYNC_BUTTON_DOWN_MSB = 0x8000;

string G_VERSION_STRING = "HoloVCS V0.5";

LibretroManager* g_pLibretroManager = NULL; //I don't want to fool with caring how to get Unreal globals correctly
void retro_video_refresh_callback(const void* data, unsigned width, unsigned height, size_t pitch);
#include <thread>

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
}

void LibretroManager::Kill()
{

	if (m_core.m_bActive && m_dllHandle != NULL)
	{
		m_core.retro_unload_game();
		m_core.retro_deinit();
		m_core.m_bActive = false;
	}

	if (m_dllHandle != NULL)
	{
		LogMsg("Unloading dll");

		if (!FreeLibrary(m_dllHandle))
		{
			LogMsg("Error unloading core dll");
		}

		m_dllHandle = NULL;
	}

	for (int i = 0; i < C_SAVE_STATE_COUNT; i++)
	{
		SAFE_DELETE_ARRAY(m_pSaveStateBuffer[i]);
	}

}

void LibretroManager::DisableBlitPass(int blitPassIndex)
{
	m_blitPass[blitPassIndex].m_bActive = false;
}

void LibretroManager::SetupBlitPass(int blitPassIndex, int layer, FIntRect srcRect, eColorKeyStyle colorKeyStyle, FLinearColor colorKey)
{
	m_blitPass[blitPassIndex].m_bActive = true;
	m_blitPass[blitPassIndex].m_activeLayerIndex = layer;
	m_blitPass[blitPassIndex].m_blitColorKeyStyle = colorKeyStyle;
	m_blitPass[blitPassIndex].m_blitSrcRect = srcRect;
	m_blitPass[blitPassIndex].m_blitColorKey = colorKey;
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

bool LibretroManager::LoadCore(string fileName)
{
	check(!m_dllHandle);

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

	LogMsg("%d - %s", buffer);
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
			pVar->value = g_pLibretroManager->m_stellaRenderFlags;
			return true;
		} 
		
		return false;
	}
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

	//string finalPath = GetBaseAppPath() + fileName; 

	ginfo.path = fileName.c_str();
	LogMsg("Loading rom %s", ginfo.path);
	if (!m_core.retro_load_game(&ginfo))
	{
		LogMsg("Error loading rom");
		return false;
	}

	m_joyPad.Clear();

	memset(&m_game_av_info, 0, sizeof(m_game_av_info));
	m_core.retro_get_system_av_info(&m_game_av_info);
	LogMsg("Core says screen is %d, %d, but max is %d, %d.", m_game_av_info.geometry.base_width, m_game_av_info.geometry.base_height,
		m_game_av_info.geometry.max_width, m_game_av_info.geometry.max_height);
	
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
		sprintf(st, "Try again in %.2f seconds, we need more time to measure audio speed", SECONDS_REQUIRED - timeTaken);
		ShowStatusMessage(st);
		return;
	}

	m_framesWrittenInLastPeriod = m_framesWrittenInPeriod / (FPlatformTime::Seconds() - m_audioStatisticsTimer);
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
			for (int i = 0; i < frames; i++)
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

void LibretroManager::Init(ALibretroManagerActor * pLibretroManagedActor)
{
	g_pLibretroManager = this;
	m_pLibretroManagedActor = pLibretroManagedActor;
	LogMsg("Let's init shit");
	strcpy(m_stellaRenderFlags, "1111111");

	if (m_core.m_bActive)
	{
		LogMsg("Huh, it's already initted?");
		return;
	}

	string coreName = "stella_libretro.dll";

	if (!LoadCore(coreName.c_str()))
	{
		string msg = string("ERROR: Can't find core ") + coreName;
		LogMsg(msg.c_str());
		ShowStatusMessage(msg.c_str(), 100);
		return;
	}
	else
	{
		LogMsg("libretro core %s loaded.", coreName.c_str());
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

	string romFileName;

	FString FullPath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*FPaths::ProjectDir());
	m_rootPath = StringCast<ANSICHAR>(*FullPath).Get();
	
#if WITH_EDITOR
	LogMsg("We're running in the editor");
#else
	m_rootPath += "../";
	LogMsg("We're running the standalone game, adding ../ because of the directory layout difference");
#endif

	m_romPath = m_rootPath+ "atari2600/";
	//get list of roms and play the first one
	TArray< FString > fileList;

	IFileManager::Get().FindFiles(fileList, ANSI_TO_TCHAR(m_romPath.c_str()), TEXT(".a26"));
	IFileManager::Get().FindFiles(fileList, ANSI_TO_TCHAR(m_romPath.c_str()), TEXT(".bin"));
	LogMsg("Found %d roms, grabbing first one", fileList.Num());

	
	if (fileList.Num() > 0)
	{
		m_curRomName = string(StringCast<ANSICHAR>(*fileList[0]).Get());
		
	}

	if (!LoadRom(m_romPath+m_curRomName))
	{
		char* pMsg = "ERROR: Place rom (.a26 or .bin) in atari2600 dir!";
		ShowStatusMessage(pMsg, 100);
		LogMsg(pMsg);
		return;
	}

	ShowStatusMessage(G_VERSION_STRING+ " Loaded "+ m_curRomName, 4);
	
	m_maxSaveStateSize = m_core.retro_serialize_size();
	if (m_maxSaveStateSize > 0)
	{
		m_maxSaveStateSize += 150000; //it will fail because it's a couple bytes
		//off sometimes, a libretro_stella bug?  whatever, I'll give it tons
		LogMsg("Preparing save state buffer of %d bytes", m_maxSaveStateSize);
		for (int i = 0; i < C_SAVE_STATE_COUNT; i++)
		{
			check(m_pSaveStateBuffer[i] == NULL);
			m_pSaveStateBuffer[i] = new uint8[m_maxSaveStateSize];
		}
	}
	else
	{
		LogMsg("Serious error with savestate reporting, can't continue");
		return;
	}

	m_core.m_bActive = true;
	DisableBlitPass(BLIT_PASS0); //just make sure nothing will actually render

	//setup timing based on what the core tells us
	m_audioStatisticsTimer = FPlatformTime::Seconds();
	m_framesWrittenInPeriod = 0;

	m_core.retro_run();
	SaveState(0);
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

bool LibretroManager::LoadState(int index)
{
	if (!m_core.retro_unserialize(m_pSaveStateBuffer[index], m_maxSaveStateSize))
	{
		LogMsg("Error loading state %d", index);
		return false;
	}

	return true;
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


#ifdef UE_BUILD_DEBUG
		//hack to show the pixel color of the bottomt middle pixel of the area of a selected pass, so I know what to use for a colorkey
		/*if (pass == BLIT_PASS0)
		{
			pSrc = (uint8*)data + (pBlitPass->m_blitSrcRect.Min.Y * pitch) + (pBlitPass->m_blitSrcRect.Min.X + ((pBlitPass->m_blitSrcRect.Max.X - pBlitPass->m_blitSrcRect.Min.X) / 2));
			LogMsg("Center color is %d, %d, %d", pSrc[0], pSrc[1], pSrc[2]);
		}*/
#endif

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
}

void LibretroManager::RenderFrame(const char* pRenderFlags)
{
	strcpy(m_stellaRenderFlags, pRenderFlags);
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
	LoadState(C_SAVE_STATE_USER_SLOT);
	SaveState(0); //we load from state 0 every frame to reset the gamelogic we've broken due to multiple renderings for the layers
}

void LibretroManager::Update()
{
	if (!m_core.m_bActive) return;
	
	LoadState(0);

	m_useAudio = true;
	FIntRect rectPartial = FIntRect(0, 0, 160, 77); //77
	FIntRect rectFull = FIntRect(0, 0, 160, m_game_av_info.geometry.base_height);

	DisableBlitPass(BLIT_PASS0); //a junk render to speed up time
	for (int i = 0; i < m_frameSkip; i++)
	{
		RenderFrame("1111111"); //m0 m1 p0 p1 b pl bg
	}
	
	//advance another tick
	SaveState(1); //a copy for the visual tricks we're going to do later
	
	///*** Note, these blits below are setup for Pitfall, but it would be easy to customize for other roms.
	//The SetupBlitPass tells it what parts of the screen to copy to which Unreal dynamical textures when
	//RenderFrame is called. Most use only one pass.  The parms to render frame tell our modified version of Stella
	//which VCS hardware sprites to render for that pass.

	//to play in 2d normally, we could just blit the full screen instead			  
  
	SetupBlitPass(BLIT_PASS0, 4, FIntRect(0, 0, 160, 77), COLOR_KEY_STYLE_1COLOR, FLinearColor(54, 147, 99, 0)); 
	SetupBlitPass(BLIT_PASS1, 4, FIntRect(0, 200, 80, 228), COLOR_KEY_STYLE_BLACK, FLinearColor(54, 147, 99, 0));
	
	
	RenderFrame("1111111"); //m0 m1 p0 p1 b pl bg
	DisableBlitPass(BLIT_PASS1); //don't need the second render pass anymore

	SaveState(0); //the real one we're going to save for the next frame
	m_useAudio = false;  //don't process any more audio

	//LogMsg("Plat time: %f, which should be more than %f", FPlatformTime::Seconds(), m_frameStartTime);

	//b = vines, ladder, stairs
	//p0 = player
	//p1 = enemies, logs, some walls

	//backdrop colors
	LoadState(1);
	SetupBlitPass(BLIT_PASS0, 0, FIntRect(0, 0, 160, 160), COLOR_KEY_STYLE_NONE, FLinearColor(0, 0, 0, 0));
	RenderFrame("0000011"); //m0 m1 p0 p1 b pl bg
	//main bg
	LoadState(1);
	SetupBlitPass(BLIT_PASS0, 1, FIntRect(0, 70, 160, 200), COLOR_KEY_STYLE_BLACK, FLinearColor(0, 0, 0, 0));
	RenderFrame("0000010"); //m0 m1 p0 p1 b pl bg
	
	//ladder/vine
	LoadState(1);
	SetupBlitPass(BLIT_PASS0, 2, FIntRect(0, 70, 160, 200), COLOR_KEY_STYLE_BLACK, FLinearColor(0, 0, 0, 0));
	RenderFrame("0000100"); //m0 m1 p0 p1 b pl bg

	//characters
	LoadState(1);
	SetupBlitPass(BLIT_PASS0, 3, FIntRect(0, 80, 160, 200), COLOR_KEY_STYLE_BLACK, FLinearColor(0, 0, 0, 0));
	RenderFrame("0011000"); //m0 m1 p0 p1 b pl bg

}
