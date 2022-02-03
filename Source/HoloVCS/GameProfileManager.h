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
	virtual ~GameProfileManager();
	void InitGame(string hash);
	void Init(LibretroManager* pManager);

	void UpdateNES();

	void UpdateAtari();

	void Update();



	LibretroManager* m_pLibretroManager = NULL;
	vector<GameProfile> m_profileVec;
	uint32 m_curGameProfileIndex = 0;

};
