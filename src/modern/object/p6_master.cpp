// ----------------------------------------------------------------------- //
//
// MODULE  : p6_master.cpp
//
// PURPOSE : M-P6-02 on the server: which master server a multiplayer
//           server reports to (package P6, MODERN.md). The heartbeat
//           itself is in src/lithshared/gamespy/GameSpyMgr.cpp.
//
// ----------------------------------------------------------------------- //

#include "Stdafx.h"
#include "object/p6_master.h"
#include "GameSpyMgr.h"
#include "ServerUtilities.h"

void P6_SetServerMaster(CGameSpyMgr* pGameSpyMgr)
{
	if (!pGameSpyMgr) return;

	char sMaster[512];
	GetConsoleString("MasterServer", sMaster, "");
	sMaster[sizeof(sMaster) - 1] = '\0';

	pGameSpyMgr->SetGameSpyAddr(sMaster);
}
