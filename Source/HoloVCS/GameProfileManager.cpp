#include "GameProfileManager.h"
#include "LibretroManager.h"
#include "PlayerPawn.h"
#include "LibretroManagerActor.h"

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

	//real render

	pL->SetupBlitPass(BLIT_PASS0, 2, FIntRect(0, 0, 256, 240), COLOR_KEY_STYLE_NONE, FLinearColor(54, 147, 99, 0));
	pL->DisableBlitPass(BLIT_PASS1);
	pL->RenderFrame("11");
	pL->SaveState(0); //the real one we're going to save for the next frame

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

	/*

	DisableBlitPass(BLIT_PASS1);
	RenderFrame("10");

	SetupBlitPass(BLIT_PASS0, 2, FIntRect(0, 49, 256, 240), COLOR_KEY_STYLE_1COLOR, FLinearColor(107, 109, 107, 0));
	DisableBlitPass(BLIT_PASS1);
	RenderFrame("10");
	*/


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
	//pL->SetupBlitPass(BLIT_PASS0, 0, FIntRect(0, 0, 256, 240), COLOR_KEY_STYLE_FILL, FLinearColor(148, 146, 255, 0));
	//pL->SetupBlitPass(BLIT_PASS0, 3, FIntRect(0, 33, 256, 240), COLOR_KEY_STYLE_1COLOR, FLinearColor(148, 146, 255, 0));
	//pL->SetupBlitPass(BLIT_PASS1, 3, FIntRect(0, 33, 256, 240), COLOR_KEY_STYLE_1COLOR, FLinearColor(148, 146, 255, 0));
	//pL->SetupBlitPass(BLIT_PASS2, 4, FIntRect(0, 33, 256, 240), COLOR_KEY_STYLE_1COLOR, FLinearColor(148, 146, 255, 0));

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

GameProfileManager::GameProfileManager()
{
	//setup the profile data.  DON'T CHANGE THE NAMES without also changing anything in ApplyStartingGameSpecificSetup
	m_profileVec.push_back(GameProfile("Default", "", NULL));
	m_profileVec.push_back(GameProfile("Castlevania", "728e05f245ab8b7fe61083f6919dc485", UpdateCastlevania));
	m_profileVec.push_back(GameProfile("Super Mario Bros", "8e3630186e35d477231bf8fd50e54cdd", UpdateSuperMarioBros));
	m_profileVec.push_back(GameProfile("Pitfall", "3e90cf23106f2e08b2781e41299de556", UpdatePitfall));
	m_profileVec.push_back(GameProfile("Wario Land VB", "fb4dc9f4ebd506702eb49e99a62bd803", UpdateDefaultVB));
	m_profileVec.push_back(GameProfile("Jack Bros. VB", "ee873c9969c15e92ca9a0f689c4ce5ea", UpdateDefaultVB));
}

void GameProfileManager::ApplyStartingGameSpecificSetup()
{

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


