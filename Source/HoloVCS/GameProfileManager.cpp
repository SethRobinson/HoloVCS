#include "GameProfileManager.h"
#include "LibretroManager.h"
#include "PlayerPawn.h"
#include "LibretroManagerActor.h"

#include <string>
#include <vector>

using std::string;
using std::vector;

void UpdatePitfall(void* pProfileManager)
{
	GameProfileManager* pProf = (GameProfileManager*)pProfileManager;
	LibretroManager* pL = pProf->m_pLibretroManager;
	
	
	pL->LoadState(0);

	pL->DisableBlitPass(BLIT_PASS0); //a junk render to speed up time
	for (int i = 0; i < pL->m_frameSkip; i++)
	{
		pL->RenderFrame("1111111"); //m0 m1 p0 p1 b pl bg
	}

	//advance another tick
	pL->SaveState(1); //a copy for the visual tricks we're going to do later

	FIntRect rectPartial = FIntRect(0, 0, 160, 77); //77
	FIntRect rectFull = FIntRect(0, 0, 160, pL->m_game_av_info.geometry.base_height);

	///*** Note, these blits below are setup for Pitfall, but it would be easy to customize for other roms.
	//The SetupBlitPass tells it what parts of the screen to copy to which Unreal dynamical textures when
	//RenderFrame is called. Most use only one pass.  The parms to render frame tell our modified version of Stella
	//which VCS hardware sprites to render for that pass.

	//to play in 2d normally, we could just blit the full screen instead			  

	pL->SetupBlitPass(BLIT_PASS0, 4, FIntRect(0, 0, 160, 77), COLOR_KEY_STYLE_1COLOR, FLinearColor(54, 147, 99, 0));
	pL->SetupBlitPass(BLIT_PASS1, 4, FIntRect(0, 200, 80, 228), COLOR_KEY_STYLE_BLACK, FLinearColor(54, 147, 99, 0));


	pL->RenderFrame("1111111"); //m0 m1 p0 p1 b pl bg
	pL->DisableBlitPass(BLIT_PASS1); //don't need the second render pass anymore

	pL->SaveState(0); //the real one we're going to save for the next frame
	pL->m_useAudio = false;  //don't process any more audio

	//LogMsg("Plat time: %f, which should be more than %f", FPlatformTime::Seconds(), m_frameStartTime);

	//b = vines, ladder, stairs
	//p0 = player
	//p1 = enemies, logs, some walls

	//backdrop colors

	pL->LoadState(1);
	pL->SetupBlitPass(BLIT_PASS0, 0, FIntRect(0, 0, 160, 160), COLOR_KEY_STYLE_NONE, FLinearColor(0, 0, 0, 0));
	pL->RenderFrame("0000001"); //m0 m1 p0 p1 b pl bg

	//main bg
	pL->LoadState(1);
	pL->SetupBlitPass(BLIT_PASS0, 1, FIntRect(0, 70, 160, 200), COLOR_KEY_STYLE_BLACK, FLinearColor(0, 0, 0, 0));
	pL->RenderFrame("0000010"); //m0 m1 p0 p1 b pl bg

	//ladder/vine
	pL->LoadState(1);
	pL->SetupBlitPass(BLIT_PASS0, 2, FIntRect(0, 70, 160, 200), COLOR_KEY_STYLE_BLACK, FLinearColor(0, 0, 0, 0));
	pL->RenderFrame("0000100"); //m0 m1 p0 p1 b pl bg

	//characters
	pL->LoadState(1);
	pL->SetupBlitPass(BLIT_PASS0, 3, FIntRect(0, 80, 160, 200), COLOR_KEY_STYLE_BLACK, FLinearColor(0, 0, 0, 0));
	pL->RenderFrame("0011000"); //m0 m1 p0 p1 b pl bg

}

//normal 2d blit when we don't know what to do
void UpdateDefaultAtari(void* pProfileManager)
{
	GameProfileManager* pProf = (GameProfileManager*)pProfileManager;
	LibretroManager* pL = pProf->m_pLibretroManager;

	//disable pic layer
	pL->m_pPlayerPawn->SetTintBG(FVector(0, 0, 0), 1.0f, false);

	pL->LoadState(0);

	pL->DisableBlitPass(BLIT_PASS0); //a junk render to speed up time
	for (int i = 0; i < pL->m_frameSkip; i++)
	{
		pL->RenderFrame("1111111"); //m0 m1 p0 p1 b pl bg
	}

	FIntRect rectPartial = FIntRect(0, 0, 160, 77); 
	FIntRect rectFull = FIntRect(0, 0, 160, pL->m_game_av_info.geometry.base_height);

	///*** Note, these blits below are setup for Pitfall, but it would be easy to customize for other roms.
	//The SetupBlitPass tells it what parts of the screen to copy to which Unreal dynamical textures when
	//RenderFrame is called. Most use only one pass.  The parms to render frame tell our modified version of Stella
	//which VCS hardware sprites to render for that pass.

	//to play in 2d normally, we could just blit the full screen instead			  

	pL->SetupBlitPass(BLIT_PASS0, 2, rectFull, COLOR_KEY_STYLE_NONE, FLinearColor(54, 147, 99, 0));
	pL->DisableBlitPass(BLIT_PASS1); //don't need the second render pass anymore
	pL->RenderFrame("1111111"); //m0 m1 p0 p1 b pl bg
	pL->SaveState(0); //the real one we're going to save for the next frame
	pL->m_useAudio = false;  //don't process any more audio
}

//normal 2d blit when we don't know what to do
void UpdateDefaultNES(void* pProfileManager)
{
	GameProfileManager* pProf = (GameProfileManager*)pProfileManager;
	LibretroManager* pL = pProf->m_pLibretroManager;

	pL->m_pPlayerPawn->SetBGPic();
	ClearLayers();
	FIntRect rectFull = FIntRect(0, 0, pL->m_game_av_info.geometry.base_width, pL->m_game_av_info.geometry.base_height);

	static byte keepList[128];

	pL->DisableBlitPass(BLIT_PASS0); //a junk render to speed up time
	for (int i = 0; i < pL->m_frameSkip; i++)
	{
		pL->RenderFrame("00"); //bg, sprites and optional |<num> to specify the active palette offset to use for bg when not rendering bg
	}
	pL->SaveState(1); //a copy for the visual tricks we're going to do later

	//real render single pass

	/*
	pL->SetupBlitPass(BLIT_PASS0, 2, FIntRect(0, 0, 256, 240), COLOR_KEY_STYLE_NONE, FLinearColor(54, 147, 99, 0));
	pL->DisableBlitPass(BLIT_PASS1);
	pL->RenderFrame("11");
	pL->SaveState(0); //the real one we're going to save for the next frame
	pL->m_useAudio = false;  //don't process any more audio
	*/

	//well, let's do the sprites on a different layer just for fun, even though we know nothing about this game

	//real render

	pL->SetupBlitPass(BLIT_PASS0, 2, FIntRect(0, 0, 256, 240), COLOR_KEY_STYLE_NONE, FLinearColor(54, 147, 99, 0));
	pL->DisableBlitPass(BLIT_PASS1);
	pL->RenderFrame("10");
	pL->SaveState(0); //the real one we're going to save for the next frame
	pL->m_useAudio = false;  //don't process any more audio

	//render the sprites now.  We have to reload the frame because everytime we render, we're actually advancing the game...

	pL->LoadState(1);
	pL->SetupBlitPass(BLIT_PASS0, 3, FIntRect(0, 0, 256, 240), COLOR_KEY_STYLE_BLACK, FLinearColor(54, 147, 99, 0));
	pL->DisableBlitPass(BLIT_PASS1);
	pL->RenderFrame("01");

}


GameProfileManager::~GameProfileManager()
{
}

void GameProfileManager::InitGame(string hash)
{
	uint32 hashInt = HashString(hash.c_str());

	for (unsigned int i = 0; i < m_profileVec.size(); i++)
	{
		if (hashInt == m_profileVec[i].m_hashInt)
		{
			LogMsg("Recognized game %s from hash.", m_profileVec[i].m_name.c_str());
			m_curGameProfileIndex = i;
			return;
		}
    }

	LogMsg("Didn't recognize game rom, using default settings.");
	m_curGameProfileIndex = 0;
}

void GameProfileManager::Init(LibretroManager* pManager)
{
	m_pLibretroManager = pManager;
	
}

void GameProfileManager::UpdateNES()
{
	if (m_pLibretroManager->m_bNesDumpRequested)
	{
		m_pLibretroManager->m_bNesDumpRequested = false;
		//grab a fresh state into scratch slot 1 (safe: every NES profile overwrites slot 1 before reading it)
		m_pLibretroManager->SaveState(1);
		m_pLibretroManager->m_nesHacker.DumpToFile(m_pLibretroManager->m_pSaveStateBuffer[1], m_pLibretroManager->m_maxSaveStateSize);
	}

	if (m_curGameProfileIndex != 0)
	{
		m_profileVec[m_curGameProfileIndex].m_update(this);
	}
	else
	{
		//default NES
		UpdateDefaultNES(this);
	}
}

void GameProfileManager::UpdateVB()
{
		UpdateDefaultVB(this);
}

void GameProfileManager::UpdateAtari()
{
	if (m_curGameProfileIndex != 0)
	{
		m_profileVec[m_curGameProfileIndex].m_update(this);
	}
	else
	{
		//default NES
		UpdateDefaultAtari(this);
	}
}


void GameProfileManager::Update()
{
	if (m_pLibretroManager->m_emulatorType == EMULATOR_ATARI)
		UpdateAtari();

	if (m_pLibretroManager->m_emulatorType == EMULATOR_NES)
		UpdateNES();

	if (m_pLibretroManager->m_emulatorType == EMULATOR_VB)
		UpdateVB();

}

void UpdateCastlevania(void* pProfileManager)
{
	GameProfileManager* pProf = (GameProfileManager*)pProfileManager;
	LibretroManager* pL = pProf->m_pLibretroManager;

	pL->m_pPlayerPawn->SetBGPic();
	ClearLayers();
	FIntRect rectFull = FIntRect(0, 0, pL->m_game_av_info.geometry.base_width, pL->m_game_av_info.geometry.base_height);

	static byte keepList[128];

	pL->DisableBlitPass(BLIT_PASS0); //a junk render to speed up time
	for (int i = 0; i < pL->m_frameSkip; i++)
	{
		pL->RenderFrame("00"); //bg, sprites and optional |<num> to specify the active palette offset to use for bg when not rendering bg
	}

	pL->SaveState(1); //a copy for the visual tricks we're going to do later
	pL->m_nesHacker.SetupMemoryMappingIfNeeded(pL->m_pSaveStateBuffer[1], pL->m_maxSaveStateSize);

	//0, 36, 148 = dark blue, 0x1
	//107, 109, 107 = dark grey 0x2

	pL->SetupBlitPass(BLIT_PASS0, 4, FIntRect(0, 0, 256, 48), COLOR_KEY_STYLE_NONE, FLinearColor(54, 147, 99, 0));
	pL->SetupBlitPass(BLIT_PASS1, 1, FIntRect(0, 49, 256, 240), COLOR_KEY_STYLE_1COLOR, FLinearColor(0, 36, 148, 255), FLinearColor(0, 0, 0, 255));
	//SetupBlitPass(BLIT_PASS2, 0, FIntRect(0, 0, 256, 240), COLOR_KEY_STYLE_FILL, FLinearColor(0, 36, 148, 0));
	pL->DisableBlitPass(BLIT_PASS2);

	pL->RenderFrame("10");
	pL->SaveState(0); //the real one we're going to save for the next frame
	pL->m_useAudio = false;  //don't process any more audio


	keepList[0] = 0x68;
	keepList[1] = 0x60;
	keepList[2] = 0x6a;
	keepList[3] = 0x6b;
	keepList[4] = 0x66;
	keepList[5] = 0x69;
	keepList[6] = 0x0e;

	pL->m_nesHacker.DeleteNametableTilesNotInList(pL->m_pSaveStateBuffer[1], pL->m_maxSaveStateSize, keepList, 7, 0);
	//m_nesHacker.SetTileColorIndex(0, 0x0); //dark grey
	pL->LoadState(1);
	pL->SetupBlitPass(BLIT_PASS0, 4, FIntRect(0, 49, 256, 240), COLOR_KEY_STYLE_BLACK, FLinearColor(107, 109, 107, 0));
	pL->SetupBlitPass(BLIT_PASS1, 3, FIntRect(0, 49, 256, 240), COLOR_KEY_STYLE_BLACK, FLinearColor(107, 109, 107, 0));
	pL->SetupBlitPass(BLIT_PASS2, 2, FIntRect(0, 49, 256, 240), COLOR_KEY_STYLE_BLACK, FLinearColor(107, 109, 107, 0));
	pL->RenderFrame("10");

	pL->m_nesHacker.SetTileColorIndex(0, 0x0); //dark grey
	pL->LoadState(1);
	pL->SetupBlitPass(BLIT_PASS0, 4, FIntRect(0, 49, 256, 240), COLOR_KEY_STYLE_1COLOR, FLinearColor(107, 109, 107, 0));
	pL->DisableBlitPass(BLIT_PASS1);
	pL->RenderFrame("01");
	//put it back
	pL->m_nesHacker.SetTileColorIndex(0, 0xf); //back to default
	pL->LoadState(0);
}


void UpdateSuperMarioBros(void* pProfileManager)
{
	GameProfileManager* pProf = (GameProfileManager*)pProfileManager;
	LibretroManager* pL = pProf->m_pLibretroManager;
	static byte keepList[128];

	//NOTE: the old build passed bAllowShadows=true here (sky wall receives shadows); the port
	//had flipped it to false, which is why the 2D view lost its wall shadows in Mario
	pL->m_pPlayerPawn->SetTintBG(FVector(146.0/255.0, 146.0 / 255.0, 1.0f), 1.0f, true);

	ClearLayers();
	FIntRect rectFull = FIntRect(0, 0, pL->m_game_av_info.geometry.base_width, pL->m_game_av_info.geometry.base_height);

	pL->DisableBlitPass(BLIT_PASS0); //a junk render to speed up time
	for (int i = 0; i < pL->m_frameSkip; i++)
	{
		pL->RenderFrame("00"); //bg, sprites and optional |<num> to specify the active palette offset to use for bg when not rendering bg
	}
	pL->SaveState(1); //a copy for the visual tricks we're going to do later
	pL->CopyState(1, 2);
	//pL->m_nesHacker.SetupMemoryMappingIfNeeded(pL->m_pSaveStateBuffer[1], pL->m_maxSaveStateSize);

	
	//real render
	pL->DisableAllBlitPasses();
	pL->SetupBlitPass(BLIT_PASS0, 4, FIntRect(0, 0, 256, 32), COLOR_KEY_STYLE_1COLOR, FLinearColor(148, 146, 255, 0));
	pL->RenderFrame("10");
	pL->SaveState(0); //the real one we're going to save for the next frame
	pL->m_useAudio = false;  //don't process any more audio

	//draw clouds

	keepList[0] = 0x39;
	keepList[1] = 0x3a;
	keepList[2] = 0x3b;
	keepList[3] = 0x3c;
	keepList[4] = 0x25;
	keepList[5] = 0x38;
	keepList[6] = 0x36;
	keepList[7] = 0x37;
	keepList[8] = 0x35;
	keepList[9] = 0x2e; //the coin overlay thingie, if I kill it, the scroller breaks?

	pL->m_nesHacker.DeleteNametableTilesNotInList(pL->m_pSaveStateBuffer[1], pL->m_maxSaveStateSize, keepList, 10, 0x24);
	pL->LoadState(1);
	pL->DisableAllBlitPasses();
	//clouds
	pL->SetupBlitPass(BLIT_PASS0, 4, FIntRect(0, 32, 256, 179), COLOR_KEY_STYLE_1COLOR, FLinearColor(148, 146, 255, 0)); 
	//bushes
	pL->SetupBlitPass(BLIT_PASS1, 4, FIntRect(0, 180, 256, 240), COLOR_KEY_STYLE_1COLOR, FLinearColor(148, 146, 255, 0));
	pL->RenderFrame("10");


	//draw sprites
	pL->LoadState(2);
	pL->DisableAllBlitPasses();
	pL->SetupBlitPass(BLIT_PASS0, 3, FIntRect(0, 0, 256, 240), COLOR_KEY_STYLE_1COLOR, FLinearColor(148, 146, 255, 0));
	pL->RenderFrame("01");

	//draw everything that isn't clouds or hills

	keepList[0] = 0x39;
	keepList[1] = 0x3a;
	keepList[2] = 0x3b;
	keepList[3] = 0x3c;
	keepList[4] = 0x25;
	keepList[5] = 0x38;
	keepList[6] = 0x36;
	keepList[7] = 0x37;
	keepList[8] = 0x35;

	keepList[9] = 0x31;
	keepList[10] = 0x32;
	keepList[11] = 0x30;
	keepList[12] = 0x26;
	keepList[13] = 0x34;
	keepList[14] = 0x33;
	
	pL->CopyState(2, 1); //copy 2 over 1
	pL->m_nesHacker.ReplaceHorizontalPairInNametable(pL->m_pSaveStateBuffer[1], pL->m_maxSaveStateSize, 0x26, 0x6a, 0x0, 0x6a); //replace this as 0x26 is used in the pipe which I want to keep
	pL->m_nesHacker.DeleteNametableTilesInList(pL->m_pSaveStateBuffer[1], pL->m_maxSaveStateSize, keepList, 15, 0x24);
	//put back the pipe piece we previously changed to avoid the delete
	pL->m_nesHacker.ReplaceHorizontalPairInNametable(pL->m_pSaveStateBuffer[1], pL->m_maxSaveStateSize, 0x0, 0x6a, 0x26, 0x6a);

	pL->LoadState(1);
	pL->DisableAllBlitPasses();
	pL->SetupBlitPass(BLIT_PASS0, 2, FIntRect(0, 32, 256, 240), COLOR_KEY_STYLE_1COLOR, FLinearColor(148, 146, 255, 0));
	pL->SetupBlitPass(BLIT_PASS1, 3, FIntRect(0, 207, 256, 240), COLOR_KEY_STYLE_1COLOR, FLinearColor(148, 146, 255, 0));
	pL->RenderFrame("10");

	//draw the hills
	keepList[0] = 0x31;
	keepList[1] = 0x32;
	keepList[2] = 0x30;
	keepList[3] = 0x26;
	keepList[4] = 0x34;
	keepList[5] = 0x33;
	keepList[6] = 0x2e; //the coin overlay thingie, if I kill it, the scroller breaks?

	pL->CopyState(2, 1); //copy 2 over 1
	pL->m_nesHacker.DeleteNametableTilesNotInList(pL->m_pSaveStateBuffer[1], pL->m_maxSaveStateSize, keepList, 7, 0x24);
	pL->LoadState(1);
	pL->DisableAllBlitPasses();
	//clouds
	//pL->SetupBlitPass(BLIT_PASS0, 4, FIntRect(0, 32, 256, 179), COLOR_KEY_STYLE_1COLOR, FLinearColor(148, 146, 255, 0));
	//bushes
	pL->SetupBlitPass(BLIT_PASS0, 1, FIntRect(0, 140, 256, 240), COLOR_KEY_STYLE_1COLOR, FLinearColor(148, 146, 255, 0));
	pL->RenderFrame("10");
	pL->LoadState(0);
}


void UpdateTetrisNES(void* pProfileManager)
{
	GameProfileManager* pProf = (GameProfileManager*)pProfileManager;
	LibretroManager* pL = pProf->m_pLibretroManager;

	pL->m_pPlayerPawn->SetBGPic();
	ClearLayers();
	FIntRect rectFull = FIntRect(0, 0, pL->m_game_av_info.geometry.base_width, pL->m_game_av_info.geometry.base_height);

	static byte keepList[128];

	pL->DisableBlitPass(BLIT_PASS0); //a junk render to speed up time
	for (int i = 0; i < pL->m_frameSkip; i++)
	{
		pL->RenderFrame("00"); //bg, sprites and optional |<num> to specify the active palette offset to use for bg when not rendering bg
	}
	pL->SaveState(1); //a copy for the visual tricks we're going to do later
	//real render

	pL->SetupBlitPass(BLIT_PASS0, 2, FIntRect(0, 0, 256, 240), COLOR_KEY_STYLE_NONE, FLinearColor(54, 147, 99, 0));
	pL->DisableBlitPass(BLIT_PASS1);
	pL->RenderFrame("10");
	pL->SaveState(0); //the real one we're going to save for the next frame
	pL->m_useAudio = false;  //don't process any more audio


	//render the sprites now.  We have to reload the frame because everytime we render, we're actually advancing the game...

	pL->LoadState(1);
	pL->SetupBlitPass(BLIT_PASS0, 3, FIntRect(0, 0, 256, 240), COLOR_KEY_STYLE_BLACK, FLinearColor(54, 147, 99, 0));
	pL->DisableBlitPass(BLIT_PASS1);
	pL->RenderFrame("01");
}

//The Legend of Zelda.  Screen layout: rows 0-63 are the HUD (minimap/rupees/items), rows 64-239 are the
//11x16 tile play area.  The diorama: full playfield as the deep ground plane, solid/obstacle tiles
//(rocks, trees, dungeon walls etc) extruded forward Castlevania-style, sprites in front of those, HUD in front.
//The HUD's item icons and the minimap blip are SPRITES, so the sprite pass also blits its HUD strip to the
//HUD layer or they'd hide behind the opaque HUD.
void UpdateZelda(void* pProfileManager)
{
	GameProfileManager* pProf = (GameProfileManager*)pProfileManager;
	LibretroManager* pL = pProf->m_pLibretroManager;

	pL->m_pPlayerPawn->SetBGPic();
	ClearLayers();

	pL->DisableBlitPass(BLIT_PASS0); //a junk render to speed up time
	for (int i = 0; i < pL->m_frameSkip; i++)
	{
		pL->RenderFrame("00"); //bg, sprites and optional |<num> to specify the active palette offset to use for bg when not rendering bg
	}

	pL->SaveState(1); //a copy for the visual tricks we're going to do later
	pL->m_nesHacker.SetupMemoryMappingIfNeeded(pL->m_pSaveStateBuffer[1], pL->m_maxSaveStateSize);

	//Zelda's game mode byte at $12 (values dumped empirically): 0 = title/attract, 1 = file select
	//menus, 5 = gameplay (subscreen included), 4/6/7 = screen-scroll transition, 8 = game over.
	byte gameMode = pL->m_nesHacker.m_pNESRAM ? pL->m_nesHacker.m_pNESRAM[0x12] : 5;

	if (gameMode == 0 || gameMode == 1 || gameMode == 8)
	{
		//title/menus/game over: the gameplay layer split looks wrong on these, so render simple:
		//whole BG on one layer, sprites floating in front of it
		pL->DisableAllBlitPasses();
		pL->SetupBlitPass(BLIT_PASS0, 2, FIntRect(0, 0, 256, 240), COLOR_KEY_STYLE_NONE, FLinearColor(0, 0, 0, 0));
		pL->RenderFrame("10");
		pL->SaveState(0);
		pL->m_useAudio = false;

		pL->m_nesHacker.SetTileColorIndex(0, 0x00); //known grey fill for the sprite pass colorkey
		pL->LoadState(1);
		pL->DisableAllBlitPasses();
		pL->SetupBlitPass(BLIT_PASS0, 3, FIntRect(0, 0, 256, 240), COLOR_KEY_STYLE_1COLOR, FLinearColor(107, 109, 107, 0));
		pL->RenderFrame("01");
		pL->LoadState(0);
		return;
	}

	//Screen-scroll transition detection is only needed for the old-core fail-safe below.  The
	//patched FCEUmm core filters tiles at PPU fetch time, so Zelda's mid-frame nametable writes
	//are filtered too and the obstacle layers can follow the native scroll in 3D.
	bool bScrolling = (gameMode == 4 || gameMode == 6 || gameMode == 7);

	//Item subscreen (Start): $e1 = 0 closed, 7 sliding open, 8 open at rest, 9 sliding closed.
	//While active, $fc holds the PPU vertical scroll, so the HUD strip's top OUTPUT row is
	//(240 - $fc) % 240 on every frame of the slide (175 at rest-open, 0 when closed).  All
	//values dumped empirically (Aug 2026).  One split handles closed/sliding/open: the HUD
	//strip -> layer 4 wherever it is, everything else (item screen above it, play area below
	//it) -> layer 1, and the item screen's sprites (selection cursor etc) pop at layer 3 like
	//play-area sprites do.
	int hudTop = 0;
	byte subscreenState = (gameMode == 5 && pL->m_nesHacker.m_pNESRAM) ? pL->m_nesHacker.m_pNESRAM[0xe1] : 0;
	if (subscreenState != 0)
	{
		int scrollY = pL->m_nesHacker.m_pNESRAM[0xfc];
		hudTop = FMath::Clamp((240 - scrollY) % 240, 0, 240 - 64);
	}

	//real render: HUD strip -> layer 4 (front), everything else -> layer 1 (the deep ground plane).
	//GOTCHA: pass indices MUST be contiguous - the video refresh callback BREAKS at the first
	//inactive pass, so a gap silently drops every pass after it (that bug shipped once: closed
	//subscreen skipped PASS1 and the ground plane + sprites on PASS2 never blitted).
	int pass = BLIT_PASS0;
	pL->DisableAllBlitPasses();
	pL->SetupBlitPass(pass++, 4, FIntRect(0, hudTop, 256, hudTop + 64), COLOR_KEY_STYLE_NONE, FLinearColor(0, 0, 0, 0));
	if (hudTop > 0)
	{
		pL->SetupBlitPass(pass++, 1, FIntRect(0, 0, 256, hudTop), COLOR_KEY_STYLE_NONE, FLinearColor(0, 0, 0, 0));
	}
	if (hudTop + 64 < 240)
	{
		pL->SetupBlitPass(pass++, 1, FIntRect(0, hudTop + 64, 256, 240), COLOR_KEY_STYLE_NONE, FLinearColor(0, 0, 0, 0));
	}
	pL->RenderFrame("10");
	pL->SaveState(0); //the real one we're going to save for the next frame
	pL->m_useAudio = false;  //don't process any more audio

	//obstacle pass: keep only the solid/raised tiles, everything else becomes the fill tile which renders
	//as the backdrop color.  Zelda's backdrop (PALRAM[0]) is BLACK on the overworld and in dungeons, so we
	//key the masked BG pass on black.  NOTE: forcing PALRAM[0] via SetTileColorIndex does NOT survive a
	//BG-on render here (Zelda rewrites the palette from its RAM shadow every frame), but it DOES hold for
	//the sprites-only pass fill, so the grey trick below is sprite-pass only.  Proven via dumplayers.

	//Obstacle extrusion, TWO masked re-renders (tile IDs from the N-key dumps):
	//- wall trees (d8-db) keyed on black only: their art uses the ground color as a highlight
	//  (24% of tree pixels, measured), so keying the ground color here would moth-eat them.
	//- ground-keyed set, keyed on black AND the ground color so the ground-painted parts stay
	//  flat and only the object shape extrudes: forest-edge diagonals (cc-d7, dc-df), standalone
	//  rocks (c8-cb), standalone trees (c4-c7).
	//The normal saved-state tile edits cannot handle screen scrolling because Zelda rewrites
	//nametable rows during the re-render.  The live core filter performs the same substitution
	//when each tile is fetched, after those writes, without mutating emulator state.  If an old
	//core DLL lacks the extension, stationary frames keep the legacy path and scrolling frames
	//retain the safe 2D fallback rather than showing unfiltered stripes.

	static byte keepWallTrees[] = { 0xd8, 0xd9, 0xda, 0xdb };
	static byte keepGroundKeyed[] = { 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xcb,
		0xcc, 0xcd, 0xce, 0xcf, 0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xdc, 0xdd, 0xde, 0xdf };
	byte fillTile = 0x24; //blank tile, renders as the backdrop color (PALRAM[0], black)
	FLinearColor blackKey(0, 0, 0, 0);
	FLinearColor groundKey(255, 255, 74, 0); //overworld ground ($37 in our rgb palette), sampled from the layer buffer

	const bool bHasLiveTileFilter = pL->HasNesBackgroundTileFilter();
	const bool bCanExtrude = subscreenState == 0 && (bHasLiveTileFilter || !bScrolling);
	if (bCanExtrude) //subscreen tiles aren't in the keep-lists, so extrusion still sits that out
	{
		if (!bHasLiveTileFilter)
		{
			pL->CopyState(1, 2); //legacy fallback: each masked pass mutates the nametables from clean
		}

		//wall trees -> layers 2, 3 AND 4: the extrusion tops out one layer above the sprites
		//(layer 3), so trees read taller than the player/creatures on the device.  Layer 4 is
		//also the HUD layer, but the HUD only occupies rows 0-63 and these blits are rows
		//64-240 of the same texture - no overlap (and the NES blit path skips keyed pixels, so
		//stacked passes never erase each other).
		if (bHasLiveTileFilter)
		{
			pL->LoadState(1);
		}
		else
		{
			pL->m_nesHacker.DeleteNametableTilesNotInList(pL->m_pSaveStateBuffer[1], pL->m_maxSaveStateSize, keepWallTrees, sizeof(keepWallTrees), fillTile);
			pL->LoadState(1);
		}
		pL->DisableAllBlitPasses();
		pL->SetupBlitPass(BLIT_PASS0, 2, FIntRect(0, 64, 256, 240), COLOR_KEY_STYLE_BLACK, blackKey);
		pL->SetupBlitPass(BLIT_PASS1, 3, FIntRect(0, 64, 256, 240), COLOR_KEY_STYLE_BLACK, blackKey);
		pL->SetupBlitPass(BLIT_PASS2, 4, FIntRect(0, 64, 256, 240), COLOR_KEY_STYLE_BLACK, blackKey);
		if (bHasLiveTileFilter)
		{
			pL->RenderFrameWithNesBackgroundTileFilter("10", keepWallTrees, sizeof(keepWallTrees), fillTile);
		}
		else
		{
			pL->RenderFrame("10");
		}

		//edge diagonals, standalone rocks and trees -> layers 2, 3 and 4, ground-colored parts keyed away
		if (bHasLiveTileFilter)
		{
			pL->LoadState(1);
		}
		else
		{
			pL->CopyState(2, 1);
			pL->m_nesHacker.DeleteNametableTilesNotInList(pL->m_pSaveStateBuffer[1], pL->m_maxSaveStateSize, keepGroundKeyed, sizeof(keepGroundKeyed), fillTile);
			pL->LoadState(1);
		}
		pL->DisableAllBlitPasses();
		pL->SetupBlitPass(BLIT_PASS0, 2, FIntRect(0, 64, 256, 240), COLOR_KEY_STYLE_2COLOR, blackKey, groundKey);
		pL->SetupBlitPass(BLIT_PASS1, 3, FIntRect(0, 64, 256, 240), COLOR_KEY_STYLE_2COLOR, blackKey, groundKey);
		pL->SetupBlitPass(BLIT_PASS2, 4, FIntRect(0, 64, 256, 240), COLOR_KEY_STYLE_2COLOR, blackKey, groundKey);
		if (bHasLiveTileFilter)
		{
			pL->RenderFrameWithNesBackgroundTileFilter("10", keepGroundKeyed, sizeof(keepGroundKeyed), fillTile);
		}
		else
		{
			pL->RenderFrame("10");
		}
	}
	else if (bScrolling && subscreenState == 0 && !bHasLiveTileFilter)
	{
		static bool bLoggedMissingLiveTileFilter = false;
		if (!bLoggedMissingLiveTileFilter)
		{
			LogMsg("Zelda 3D scrolling unavailable: fceumm core lacks retro_set_holo_bg_tile_filter; using 2D transition fallback");
			bLoggedMissingLiveTileFilter = true;
		}
	}

	//sprite pass: with BG off the scanlines fill with PALRAM[0], which we force to NES $00
	//(107,109,107 grey in the rgb palette) so black sprite pixels survive the key.
	pL->m_nesHacker.SetTileColorIndex(0, 0x00);
	//Play-area/item-screen sprites -> layer 3 (in front of the extruded obstacles; the
	//subscreen's cursor pops the same way), HUD-strip sprites -> layer 4, tracking the split.
	//Same contiguous-pass rule as the BG render above.
	pL->LoadState(1);
	pL->DisableAllBlitPasses();
	pass = BLIT_PASS0;
	pL->SetupBlitPass(pass++, 4, FIntRect(0, hudTop, 256, hudTop + 64), COLOR_KEY_STYLE_1COLOR, FLinearColor(107, 109, 107, 0));
	if (hudTop > 0)
	{
		pL->SetupBlitPass(pass++, 3, FIntRect(0, 0, 256, hudTop), COLOR_KEY_STYLE_1COLOR, FLinearColor(107, 109, 107, 0));
	}
	if (hudTop + 64 < 240)
	{
		pL->SetupBlitPass(pass++, 3, FIntRect(0, hudTop + 64, 256, 240), COLOR_KEY_STYLE_1COLOR, FLinearColor(107, 109, 107, 0));
	}
	pL->RenderFrame("01");

	pL->LoadState(0);
}


GameProfileManager::GameProfileManager()
{
	//setup the profile data.  DON'T CHANGE THE NAMES without also changing anything in ApplyStartingGameSpecificSetup
	m_profileVec.push_back(GameProfile("Default", "", NULL));
	m_profileVec.push_back(GameProfile("Castlevania", "728e05f245ab8b7fe61083f6919dc485", UpdateCastlevania));
	m_profileVec.push_back(GameProfile("Super Mario Bros", "8e3630186e35d477231bf8fd50e54cdd", UpdateSuperMarioBros));
	m_profileVec.push_back(GameProfile("Pitfall", "3e90cf23106f2e08b2781e41299de556", UpdatePitfall));
	m_profileVec.push_back(GameProfile("Wario Land VB", "fb4dc9f4ebd506702eb49e99a62bd803", UpdateDefaultVB));
	m_profileVec.push_back(GameProfile("Jack Bros. VB", "ee873c9969c15e92ca9a0f689c4ce5ea", UpdateDefaultVB));
	m_profileVec.push_back(GameProfile("Tetris NES", "5b0e571558c8c796937b96af469561c6", UpdateTetrisNES));
	m_profileVec.push_back(GameProfile("Legend of Zelda", "d3f453931146e95b04a31647de80fdab", UpdateZelda)); //PRG 1

	

}

void GameProfileManager::ApplyStartingGameSpecificSetup()
{
	//set all m_layerSetupInfo to default
	for (int i = 0; i < C_MAX_LAYERS; i++)
	{
		m_layerSetupInfo[i] = LayerSetupInfo();
	}

	if (m_curGameProfileIndex == 0)
	{
		LogMsg("Applying game specific setup... unknown rom, using defaults.");
		return; //don't recognize this rom
	}

	GameProfile *pProfile = &m_profileVec[m_curGameProfileIndex];
	
	if (pProfile->m_name == "Wario Land VB")
	{
		m_pLibretroManager->m_pLibretroManagedActor->m_total3dDepth = 410;
		m_pLibretroManager->m_pLibretroManagedActor->m_depthOffsetForAllLayers = 20;
	}
	else if(pProfile->m_name == "Jack Bros. VB")
	{
		m_pLibretroManager->m_pLibretroManagedActor->m_total3dDepth = 410;
		m_pLibretroManager->m_pLibretroManagedActor->m_depthOffsetForAllLayers = 20;
	}
	else if (pProfile->m_name == "Castlevania")
	{
		//turn off shadow receiving for specific layers
		LogMsg("Castlevania settings...");
		
		/*
		m_layerSetupInfo[5].m_bIgnoreShadows = true;
		m_layerSetupInfo[4].m_bIgnoreShadows = true;
		m_layerSetupInfo[3].m_bIgnoreShadows = true;
		m_layerSetupInfo[2].m_bIgnoreShadows = true;
		*/

	}
	else
	{
		LogMsg("Applying game specific setup for recognized game %s - non specified, using defaults.", pProfile->m_name.c_str());
		return;
	}

	LogMsg("Applying game specific setup for %s.", pProfile->m_name.c_str());

}


void UpdateDefaultVB(void* pProfileManager)
{
	GameProfileManager* pProf = (GameProfileManager*)pProfileManager;
	LibretroManager* pL = pProf->m_pLibretroManager;

	pL->m_pPlayerPawn->SetBGPic();
	ClearLayers();
	FIntRect rectFull = FIntRect(0, 0, pL->m_game_av_info.geometry.base_width, pL->m_game_av_info.geometry.base_height);

	static byte keepList[128];

	pL->DisableBlitPass(BLIT_PASS0); //a junk render to speed up time
	for (int i = 0; i < pL->m_frameSkip; i++)
	{
		pL->RenderFrame("00"); //bg, sprites and optional |<num> to specify the active palette offset to use for bg when not rendering bg
	}

	pL->ResetBlitInformation();

	//real render

	pL->SetupBlitPass(BLIT_PASS0, 0, FIntRect(0, 0, 384, 224), COLOR_KEY_STYLE_NONE, FLinearColor(54, 147, 99, 0));
	pL->DisableBlitPass(BLIT_PASS1);
	pL->RenderFrame("11");
	pL->SaveState(0); //the real one we're going to save for the next frame

}


