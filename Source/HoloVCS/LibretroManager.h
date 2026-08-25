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
#include "holo_layer_abi.h" //v2 layer ABI shared with the patched Azahar (3DS) core
#include "Shared/UnrealMisc.h"
#include "NesHacker.h"
#include "AutomationHarness.h"
#include "HelpScreen.h"
#include "GameProfileManager.h"
#include "Windows/WindowsHWrapper.h" //HINSTANCE/LoadLibrary for the core dll loading

//using namespace std;

const int C_SAVE_STATE_COUNT = 3;
const int C_SAVE_STATE_USER_SLOT = 2;
enum eColorKeyStyle
{
	COLOR_KEY_STYLE_NONE,
	COLOR_KEY_STYLE_BLACK,
	COLOR_KEY_STYLE_1COLOR,
	COLOR_KEY_STYLE_2COLOR,
	COLOR_KEY_STYLE_FILL //ignores everything and just fills the whole thing with the color
};

enum eEmulatorType
{
	EMULATOR_ATARI,
	EMULATOR_NES,
	EMULATOR_VB,
	EMULATOR_3DS,

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

	//analog sticks, -1..1 (fed by the same MoveX/MoveY axes as the digital dpad bits, so
	//keyboard/dpad give full deflection and a real stick gives true analog).  Served to
	//cores that ask for RETRO_DEVICE_ANALOG - the 3DS circle pad and C-stick.
	float m_axisLX = 0, m_axisLY = 0;
	float m_axisRX = 0, m_axisRY = 0;
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
	//Nonstandard optional extension of the patched Azahar core: depth-sliced layer delivery
	//(v2 of the VB scheme above).  Presence of the export doubles as "holo mode available".
	void (*retro_set_video_refresh_holo)(retro_video_refresh_holo_t) = nullptr;
	void (*retro_set_audio_sample)(retro_audio_sample_t);
	void (*retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
	void (*retro_set_input_poll)(retro_input_poll_t);
	void (*retro_set_input_state)(retro_input_state_t);
	//Older nonstandard FCEUmm extension retained for stationary compatibility replays.
	//The core copies the 32-byte tile allow mask immediately; nullptr disables filtering.
	void (*retro_set_holo_bg_tile_filter)(const uint8* allowedMask, uint8 replacementTile) = nullptr;
	//Nonstandard optional FCEUmm extension.  The returned 256x240 byte map identifies the
	//background tile that produced each pixel of the most recently rendered frame.
	const uint8* (*retro_get_holo_bg_tile_ids)(void) = nullptr;
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
	bool m_bUseNesTileMask = false;
	uint8 m_nesTileMask[32] = {};

};

enum eBlitPass
{
	BLIT_PASS0,
	BLIT_PASS1,
	BLIT_PASS2,
	BLIT_PASS3,

	C_MAX_BLITPASS_COUNT = 12
};

/*
typedef struct {
	void* v;
	uint32 s;
	char desc[5];
} SFORMAT;

*/

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
	bool RenderFrameWithNesBackgroundTileFilter(const char* pRenderFlags, const byte* pKeepList, int keepListSize, byte replacementTile);
	bool HasNesBackgroundTileFilter() const { return m_core.retro_set_holo_bg_tile_filter != nullptr; }
	bool HasNesBackgroundTileIds() const { return m_core.retro_get_holo_bg_tile_ids != nullptr; }
	void SetFrameSkip(int frameSkip);
	void UpdateAtari();
	void Update();
	void Kill();
	void ModEmulatorType(int mod);
	void ModRom(int mod);

	void DisableBlitPass(int blitPassIndex);
	bool IsCoreLoaded() { return m_core.m_bActive; }
	void SetupBlitPass(int blitPassIndex, int layer, FIntRect srcRect, eColorKeyStyle colorKeyStyle, FLinearColor colorKey, FLinearColor colorKey2 = FLinearColor(0, 0, 0, 0));
	void SetupNesTileFilteredBlitPass(int blitPassIndex, int layer, FIntRect srcRect, eColorKeyStyle colorKeyStyle,
		FLinearColor colorKey, FLinearColor colorKey2, const byte* pKeepList, int keepListSize);
	void SaveStateToFile();
	void LoadStateFromFile();
	string GetSaveStateDir(); //saves/<romdir>/, keeps the top-level folder clean and avoids cross-system name collisions
	string GetSaveStatePath(); //full path of the current rom's .sav0 in GetSaveStateDir()
	void ResetRom();
	void DisableAllBlitPasses();
	bool GetGamePaused() { return m_bGamePaused; }
	void SetGamePaused(bool bNew);
	bool SwitchRomByPartialName(string name); //live rom switch, used by the automation harness

	ALibretroManagerActor* m_pLibretroManagedActor = NULL;
	APlayerPawn* m_pPlayerPawn = NULL;
	CoreInterface m_core;
	retro_system_av_info m_game_av_info;
	retro_system_info m_game_system_info;
	
	//only include this if not static
#ifndef RT_STATIC_CORE
	HINSTANCE m_dllHandle = NULL;
#endif
	
	char m_coreRenderFlags[12];
	int m_maxSaveStateSize = 0;
	BlitPass m_blitPass[C_MAX_BLITPASS_COUNT];
	bool m_useAudio = true;
	int m_frameSkip = 0; //0 means no skipping, normal
	double m_targetFPS = 0; //0 means no limit
	bool m_bUncapFPS = false; //true = skip the pacing busy-wait entirely (0 hotkey, for fps measurement)
	double m_mainTimer = 0;
	double m_timeOfLastFrame = 0;
	double m_lastEmuUpdateSeconds = 0; //diagnostics: wall time the last Update spent running the core(s)
	double m_lastPaceWaitSeconds = 0; //diagnostics: wall time the last Update spent in the pacing wait
	int m_catchUpFrames = 0; //diagnostics: extra emulator frames run to keep up with wall-clock (reset by the per-second log)
	int m_audioFramesDropped = 0; //diagnostics: audio chunks discarded by the hard-drop safety valve (reset by the per-second log)
	JoyPadButtonStates m_joyPad;
	bool m_bGamePaused = false;
	bool m_bNesDumpRequested = false; //set by the N hotkey; UpdateNES writes Saved/nes_state_dump.txt next frame

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

	//virtual touch cursor for the 3DS bottom screen, in bottom-screen pixels.  Mouse deltas
	//move it (PlayerPawn), it never leaves the screen, and the holo blit draws a big pointer
	//at its position so it's obvious on the hologram.  Left click = touch.
	float m_touchX = 160.0f;
	float m_touchY = 120.0f;
	bool m_touchDown = false;
	double m_touchLastActiveTime = 0; //FPlatformTime seconds; drives cursor auto-hide
	bool m_touchCursorShownOnce = false; //primes a few visible seconds on the first delivered frame

	//Press-position latch: the panel shows the cursor a few frames late and the act of
	//clicking (stick-click wobble, trigger squeeze, mouse nudge) moves it right as the press
	//lands, so a tap latches to where the cursor was ~100ms ago and stays there until the
	//cursor clearly drags away.  All touch press/release goes through SetTouchDown.
	static const int C_TOUCH_HISTORY = 16;
	float m_touchHistX[C_TOUCH_HISTORY];
	float m_touchHistY[C_TOUCH_HISTORY];
	int m_touchHistPos = 0;
	bool m_touchLatched = false;
	float m_touchLatchX = 160.0f;
	float m_touchLatchY = 120.0f;
	void SetTouchDown(bool bDown);
	void RecordTouchHistory(); //once per game-thread tick while the 3DS is active
	float GetTouchPointX(); //latched during a fresh press, live otherwise
	float GetTouchPointY();

	NesHacker m_nesHacker;
	AutomationHarness m_autoHarness;
	HelpScreen m_helpScreen;
	int m_autoButtonHoldFrames[C_MAX_JOYPAD_BUTTONS] = {}; //harness-injected holds, OR'd into the input callback, decremented per visible frame
	GameProfileManager m_profManager;
	uint8* m_pSaveStateBuffer[C_SAVE_STATE_COUNT];
	
protected:

	void SetEmulatorData(eEmulatorType emu);


	bool SetRomToLoadByPartialFileName(string name);

	void InitEmulator();
	void ClearAllLayers();
	void LoadRomList();

private:
};


void ClearLayers();

//Yes, I cheat and use globals.  Shhh
extern LibretroManager* g_pLibretroManager;

#endif // LibretroManager_h__
