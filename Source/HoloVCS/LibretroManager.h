//  ***************************************************************
//  LibretroManager - Creation date: 08/07/2021
//  -------------------------------------------------------------
//  Robinson Technologies Copyright (C) 2021 - All Rights Reserved
//
//  ***************************************************************
//  Programmer(s):  Seth A. Robinson (seth@rtsoft.com)
//  ***************************************************************

#ifndef LibretroManager_h__
#define LibretroManager_h__
#include "libretro.h"
#include "Shared/UnrealMisc.h"
using namespace std;
const int C_SAVE_STATE_COUNT = 3;
const int C_SAVE_STATE_USER_SLOT = 2;
enum eColorKeyStyle
{
	COLOR_KEY_STYLE_NONE,
	COLOR_KEY_STYLE_BLACK,
	COLOR_KEY_STYLE_1COLOR
};

const int C_MAX_JOYPAD_BUTTONS = 16;

class JoyPadButtonStates
{
public:

	JoyPadButtonStates()
	{
		Clear();
	}
	void Clear();
	
	bool m_button[C_MAX_JOYPAD_BUTTONS];

};


class ALibretroManagerActor;
//typedef int(__stdcall* f_funci)();
class CoreInterface
{
public:

	unsigned (*retro_api_version)(void);
	void (*retro_get_system_info)(struct retro_system_info* info);
	void (*retro_init)(void);
	void (*retro_deinit)(void);
	void (*retro_set_environment)(retro_environment_t);
	void (*retro_set_video_refresh)(retro_video_refresh_t);
	void (*retro_set_audio_sample)(retro_audio_sample_t);
	void (*retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
	void (*retro_set_input_poll)(retro_input_poll_t);
	void (*retro_set_input_state)(retro_input_state_t);
	bool (*retro_load_game)(const struct retro_game_info*);
	//bool (*retro_load_game_special)(unsigned, const struct retro_game_info*, size_t);
	void (*retro_get_system_av_info)(struct retro_system_av_info*);
	void (*retro_run)(void);
	void (*retro_unload_game)(void);
	size_t(*retro_serialize_size)(void);
	bool (*retro_serialize)(void* data, size_t size);
	bool (*retro_unserialize)(const void* data, size_t size);

	bool m_bActive = false;

};

class BlitPass
{
public:

	bool m_bActive = false;

	FIntRect m_blitSrcRect;
	eColorKeyStyle m_blitColorKeyStyle;
	FLinearColor m_blitColorKey = FLinearColor(0, 0, 0, 0);
	int m_activeLayerIndex = 0;

};

const int C_MAX_BLITPASS_COUNT=3;

const int BLIT_PASS0 = 0;
const int BLIT_PASS1 = 1;
const int BLIT_PASS2 = 2;

class LibretroManager
{
public:
	LibretroManager();
	virtual ~LibretroManager();

	bool LoadCore(string fileName);
	bool LoadRom(string fileName);
	void Init(ALibretroManagerActor* pLibretroManagedActor);
	bool SaveState(int index);
	bool LoadState(int index);
	void RenderFrame(const char* pRenderFlags);
	void SetFrameSkip(int frameSkip);
	void Update();
	void Kill();

	void DisableBlitPass(int blitPassIndex);
	bool IsCoreLoaded() { return m_core.m_bActive; }
	ALibretroManagerActor* m_pLibretroManagedActor = NULL;
	CoreInterface m_core;
	retro_system_av_info m_game_av_info;
	retro_system_info m_game_system_info;
	HINSTANCE m_dllHandle = NULL;
	char m_stellaRenderFlags[12];
	int m_maxSaveStateSize = 0;
	void SetupBlitPass(int blitPassIndex, int layer, FIntRect srcRect, eColorKeyStyle colorKeyStyle, FLinearColor colorKey);
	BlitPass m_blitPass[C_MAX_BLITPASS_COUNT];
	bool m_useAudio = true;
	int m_frameSkip = 1; //default to 1 skip, good for the portrait screen

	JoyPadButtonStates m_joyPad;
	void UpdateAudioStatistics(int framesWritten);
	float m_audioStatisticsTimer = 0;
	void SetSampleRate();
	int m_framesWrittenInPeriod;
	void SaveStateToFile();
	void LoadStateFromFile();
	string m_rootPath;
	string m_romPath;
	string m_curRomName;

protected:

	uint8* m_pSaveStateBuffer[C_SAVE_STATE_COUNT];
		int m_framesWrittenInLastPeriod;
	
private:
};

//Yes, I cheat and use globals.  Shhh
extern LibretroManager* g_pLibretroManager;

#endif // LibretroManager_h__
