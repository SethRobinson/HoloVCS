//  ***************************************************************
//  GameProfileManager - Creation date: 1/7/2022 5:16:11 PM
//  -------------------------------------------------------------
//  License: Uh, check for license.txt or license.md for that?
//
//  ***************************************************************
//  Programmer(s):  Seth A. Robinson (seth@rtsoft.com)
//  ***************************************************************

#pragma once
#include "Shared/UnrealMisc.h"
#include <vector>

using std::string;
using std::vector;

class LayerSetupInfo
{
public:

	bool m_bIgnoreShadows = false;
	//Force the unlit material on this layer regardless of the global lighting mode (both the
	//flat scene and the LKG sprite path key off the material, so this kills lighting AND
	//shadow stamps on it in one place).  Used for the 3DS backdrop band: it holds the game's
	//sky/backdrop, and tree silhouettes shadow-stamped onto a sky read as glitch blobs.
	bool m_bUnlit = false;
};

#define C_MAX_LAYERS 40 //must exceed the largest m_layerCount plus one (the 3DS bottom screen quad sits at index GetLayerCount())

//Hardcoded the path to this interface I made when I modified the VB core
#include "HoloVB.h"

class GameProfile
{
public:

	GameProfile(string name, string hash, void(*update)(void*))
	{
		m_name = name;
		m_hash = hash;
		m_hashInt = HashString(hash.c_str(), hash.length());
		m_update = update;
	}

	string m_name;
	string m_hash;
	uint32 m_hashInt;
	void (*m_update)(void*);
};



class LibretroManager;

class GameProfileManager
{
public:
	GameProfileManager();
	void ApplyStartingGameSpecificSetup();
	virtual ~GameProfileManager();
	void InitGame(string hash);
	void Init(LibretroManager* pManager);

	void UpdateNES();

	void UpdateVB();

	void UpdateAtari();

	void Update3DS();

	void Update();



	LibretroManager* m_pLibretroManager = NULL;
	vector<GameProfile> m_profileVec;
	uint32 m_curGameProfileIndex = 0;
	LayerSetupInfo m_layerSetupInfo[C_MAX_LAYERS];
};

void UpdateDefaultVB(void* pProfileManager);
void UpdateDefault3DS(void* pProfileManager);
