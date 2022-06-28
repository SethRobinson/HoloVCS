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
#include "NesHacker.h"
#include "GameProfileManager.h"

using namespace std;
const int C_SAVE_STATE_COUNT = 3;
const int C_SAVE_STATE_USER_SLOT = 2;
enum eColorKeyStyle
{
	COLOR_KEY_STYLE_NONE,
	COLOR_KEY_STYLE_BLACK,
	COLOR_KEY_STYLE_1COLOR,
	COLOR_KEY_STYLE_2COLOR,
	COLOR_KEY_STYLE_FILL
};

enum eEmulatorType
{
	EMULATOR_ATARI,
	EMULATOR_NES,
	EMULATOR_VB,

	//add more above here
	EMULATOR_COUNT
};

enum eSurfaceSourceType
{
	SURFACE_SOURCE_RGBA_32, //ARGB
	SURFACE_SOURCE_ARGB_1555_16,
	SURFACE_SOURCE_RGB_565_16,
	SURFACE_SOURCE_RGBA_32_UNREAL //RGBA already formatted like our unreal layer so a memcpy across the whole thing can be used
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
class APlayerPawn;
class GameProfileManager;


class CoreInterface
{
public:

	unsigned (*retro_api_version)(void);
	void (*retro_get_system_info)(struct retro_system_info* info);
	void (*retro_init)(void);
	void (*retro_deinit)(void);
	void (*retro_reset)(void);
	void (*retro_set_environment)(retro_environment_t);
	void (*retro_set_video_refresh)(retro_video_refresh_t);
	void (*retro_set_video_refresh_ex)(retro_video_refresh_ex_t);
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
	FLinearColor m_blitColorKey2 = FLinearColor(0, 0, 0, 0);
	int m_activeLayerIndex = 0;

};

enum eBlitPass
{
	BLIT_PASS0,
	BLIT_PASS1,
	BLIT_PASS2,
	BLIT_PASS3,

	
	C_MAX_BLITPASS_COUNT
};

typedef struct {
	void* v;
	uint32 s;
	char desc[5];
} SFORMAT;

class LibretroManager
{
public:
	LibretroManager();
	virtual ~LibretroManager();

	void FreeEmulatorIfNeeded();

	bool LoadCore(string fileName);
	bool LoadRom(string fileName);
	void SetRomByIndex(int index);
	void Init(ALibretroManagerActor* pLibretroManagedActor);
	bool SaveState(int index);
	bool CopyState(int fromState, int toState);
	bool LoadState(int index);
	void ResetBlitInformation();
	void RenderFrame(const char* pRenderFlags);
	void SetFrameSkip(int frameSkip);
	void UpdateAtari();
	void Update();
	void Kill();
	void ModEmulatorType(int mod);
	void ModRom(int mod);

	void DisableBlitPass(int blitPassIndex);
	bool IsCoreLoaded() { return m_core.m_bActive; }
	void SetupBlitPass(int blitPassIndex, int layer, FIntRect srcRect, eColorKeyStyle colorKeyStyle, FLinearColor colorKey, FLinearColor colorKey2 = FLinearColor(0, 0, 0, 0));
	void UpdateAudioStatistics(int framesWritten);
	void SetSampleRate();
	void SaveStateToFile();
	void LoadStateFromFile();
	void ResetRom();
	void DisableAllBlitPasses();
	bool GetGamePaused() { return m_bGamePaused; }
	void SetGamePaused(bool bNew);

	ALibretroManagerActor* m_pLibretroManagedActor = NULL;
	APlayerPawn* m_pPlayerPawn = NULL;
	CoreInterface m_core;
	retro_system_av_info m_game_av_info;
	retro_system_info m_game_system_info;
	HINSTANCE m_dllHandle = NULL;
	char m_coreRenderFlags[12];
	int m_maxSaveStateSize = 0;
	BlitPass m_blitPass[C_MAX_BLITPASS_COUNT];
	bool m_useAudio = true;
	int m_frameSkip = 0; //0 means no skipping, normal
	double m_targetFPS = 0; //0 means no limit
	double m_mainTimer = 0;
	double m_timeOfLastFrame = 0;
	JoyPadButtonStates m_joyPad;
	float m_audioStatisticsTimer = 0;
	int m_framesWrittenInPeriod;
	bool m_bGamePaused = false;

	string m_rootPath;
	string m_romPath;
	string m_curRomName;
	string m_romFileExtension1;
	string m_romFileExtension2;
	string m_romDir;
	string m_coreName;
	string m_romHash;
	
	eEmulatorType m_emulatorType = EMULATOR_ATARI;
	eSurfaceSourceType m_surfaceSourceType = SURFACE_SOURCE_RGBA_32;
	TArray<uint8> m_romDataArray;
	TArray< FString > m_romNameFileList;
	TArray< int > m_emulatorIDList;
	int m_activeRomIndex = 0;

	NesHacker m_nesHacker;
	GameProfileManager m_profManager;
	uint8* m_pSaveStateBuffer[C_SAVE_STATE_COUNT];
	
protected:

	void SetEmulatorData(eEmulatorType emu);


	bool SetRomToLoadByPartialFileName(string name);

	void InitEmulator();
	void ClearAllLayers();
	void LoadRomList();

		int m_framesWrittenInLastPeriod;
	
private:
};


void ClearLayers();

//Yes, I cheat and use globals.  Shhh
extern LibretroManager* g_pLibretroManager;

#endif // LibretroManager_h__
