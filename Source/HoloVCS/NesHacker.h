#pragma once
#include "Shared/UnrealMisc.h"

class NesHacker
{
public:

	NesHacker();

	void SetupMemoryMappingIfNeeded(byte *pSaveState, int saveStateSize);
	void ScanType(unsigned char* pState, unsigned int chunkSize);
	void GetNesMemAddresses(byte* pState);
	void DeleteNametableTilesNotInList(byte* pSave, int saveSize, byte* pKeepList, int keepListSize, byte fillTile);
	void DeleteNametableTilesInList(byte* pSave, int saveSize, byte* pList, int listSize, byte fillTile);
	void CheckIfMapped();
	void Reset();
	void ChangeIfNotOnKeepList(byte* pTileToSet, byte* pKeepList, int keepListSize, byte fillTile);
	void ChangeIfOnList(byte* pTileToSet, byte* pList, int listSize, byte fillTile);
	void SetTileColorIndex(int indexSlot, byte newColorIndex);
	void ReplaceHorizontalPairInNametable(byte* pSave, int saveSize, byte left, byte right, byte newLeft, byte newRight);

	byte* m_pNESRAM;
	byte* m_pTilePals;
	byte* m_pSpriPals;
	byte* m_pNametable[2];
	int m_saveSize = 0;
	byte* m_lastSaveStateAddress = NULL;
};
