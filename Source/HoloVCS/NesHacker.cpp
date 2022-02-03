#include "NesHacker.h"


NesHacker::NesHacker()
{
	Reset();
}

void NesHacker::SetupMemoryMappingIfNeeded(byte* pSaveState, int saveStateSize)
{
	if (m_saveSize == saveStateSize && pSaveState == m_lastSaveStateAddress) return; //already did it
	m_saveSize = saveStateSize;
	m_lastSaveStateAddress = pSaveState;

	GetNesMemAddresses(pSaveState);
}

void NesHacker::ScanType(unsigned char* pState, unsigned int chunkSize)
{
	byte* pStartingState = pState;

	//run through every chunk, save the pointer to things we might care about

	unsigned int size;
	char name[5];
	name[4] = 0;

	//if you get compile errors here, make sure you're compiling as Win64 and not Win32!
	while (pState - pStartingState < chunkSize)
	{
		memcpy(&name, pState, 4); pState += 4;
		memcpy(&size, pState, 4); pState += 4;
		
		//LogMsg("Found %s (%d bytes)", name, size);
		
		if (strcmp(name, "RAM") == 0)
		{ 
			m_pNESRAM = pState;
	
		}
	
		if (strcmp(name, "PRAM") == 0)
		{
			m_pTilePals = pState;
			m_pSpriPals = pState + 16;

		}

		if (strcmp(name, "NTAR") == 0)
		{
			m_pNametable[0] = pState;
			m_pNametable[1] = pState + 1024;

		}
		pState += size;
	}

}

void NesHacker::GetNesMemAddresses(byte* pState)
{
	byte* pStartingState = pState;

	pState += 16; //skip header
	
	//The FCE emulator's save state format is kind of tricky, it's 5 or so main chunks, and then each chunk itself has sub-chunks to scan.

	byte type;
	unsigned int size;
	char name[5];
	name[4] = 0;

	while (pState - pStartingState < m_saveSize)
	{
		//get main chunk
		memcpy(&type, pState, 1); pState += 1;
		memcpy(&size, pState, 4); pState += 4;

		ScanType(pState, size); //we'll find the sub-chunks of this chunk with this
		pState += size;
	}

}


void NesHacker::ChangeIfNotOnKeepList(byte* pTileToSet, byte* pKeepList, int keepListSize, byte fillTile)
{

	/*
	if (*pTileToSet > 0x29  && *pTileToSet < 0x30)
	{
		LogMsg("(%x) Ignoring %x", pTileToSet, *pTileToSet);
		return;
	}

	*/

	for (int i = 0; i < keepListSize; i++)
	{
		if (*pTileToSet == pKeepList[i]) return;
	}

	*pTileToSet = fillTile;
}


void NesHacker::ChangeIfOnList(byte* pTileToSet, byte* pList, int listSize, byte fillTile)
{

	for (int i = 0; i < listSize; i++)
	{
		if (*pTileToSet == pList[i])
		{
			*pTileToSet = fillTile;
		}
	}

}

//pKillList is an array of bytes,
void NesHacker::DeleteNametableTilesNotInList(byte *pSave, int saveSize, byte *pKeepList, int keepListSize, byte fillTile)
{
	SetupMemoryMappingIfNeeded(pSave, saveSize);

	for (int i = 0; i < 30 * 32; i++)
	{

		ChangeIfNotOnKeepList(&m_pNametable[0][i], pKeepList, keepListSize, fillTile);
		
		ChangeIfNotOnKeepList(&m_pNametable[1][i], pKeepList, keepListSize, fillTile);
		
	}
}

//pKillList is an array of bytes,
void NesHacker::DeleteNametableTilesInList(byte* pSave, int saveSize, byte* pList, int listSize, byte fillTile)
{
	SetupMemoryMappingIfNeeded(pSave, saveSize);

	for (int i = 0; i < 30 * 32; i++)
	{

		ChangeIfOnList(&m_pNametable[0][i], pList, listSize, fillTile);

		ChangeIfOnList(&m_pNametable[1][i], pList, listSize, fillTile);

	}
}

void NesHacker::ReplaceHorizontalPairInNametable(byte* pSave, int saveSize, byte left, byte right, byte newLeft, byte newRight)
{
	SetupMemoryMappingIfNeeded(pSave, saveSize);
	
	for (int i = 0; i < 30 * 32; i+=2)
	{

		if ( *(&m_pNametable[0][i]) == left && *(&m_pNametable[0][i + 1]) == right )
		{
			//LogMsg("Found match");
			m_pNametable[0][i] = newLeft;
			m_pNametable[0][i+1] = newRight;
		}

		if (*(&m_pNametable[1][i]) == left && *(&m_pNametable[1][i + 1]) == right)
		{
			//LogMsg("Found match");
			m_pNametable[1][i] = newLeft;
			m_pNametable[1][i + 1] = newRight;
		}

	}
}

void NesHacker::CheckIfMapped()
{
		check(m_pTilePals && "Uh oh");
	
}

void NesHacker::SetTileColorIndex(int indexSlot, byte newColorIndex)
{
	CheckIfMapped();
	m_pTilePals[indexSlot] = newColorIndex;
}

void NesHacker::Reset()
{
	m_pNESRAM = NULL;
	m_pTilePals = NULL;
	m_pSpriPals = NULL;
	m_pNametable[0] = NULL;
	m_pNametable[1] = NULL;
	m_saveSize = 0;
}
