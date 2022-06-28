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
using namespace std;
#include <vector>
//Hardcoded the path to this interface I made when I modified the VB core
#include "D:\projects\libretro\beetle-vb-libretro\mednafen\vb\HoloVB.h"

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

	void Update();



	LibretroManager* m_pLibretroManager = NULL;
	vector<GameProfile> m_profileVec;
	uint32 m_curGameProfileIndex = 0;

};

void UpdateDefaultVB(void* pProfileManager);
