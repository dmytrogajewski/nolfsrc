#include "engine/portable/string.h"
#include "engine/portable/text.h"
#include <cstdio>
#include <cstring>
// ----------------------------------------------------------------------- //
//
// MODULE  : p6_multiplayer.cpp
//
// PURPOSE : The modern build's multiplayer changes on the client (package
//           P6, MODERN.md). See p6_multiplayer.h.
//
// ----------------------------------------------------------------------- //

#include "Stdafx.h"
#include "cshell/p6_multiplayer.h"
#include "GameClientShell.h"
#include "InterfaceMgr.h"
#include "VarTrack.h"
#include "CommonUtilities.h"

extern CGameClientShell* g_pGameClientShell;

// In GameSpyClientMgr.lib (src/lithshared/gamespy/gsmaster.h).
void GameSpyClient_SetMasterServer(const char* sAddr);


// ----------------------------------------------------------------------- //
//  M-P6-02: the browser's master server
// ----------------------------------------------------------------------- //

#define P6_DEFAULT_MASTER	"master.openspy.net"

static VarTrack s_vtMasterServer;

void P6_SetBrowserMaster()
{
	if (!s_vtMasterServer.IsInitted())
	{
		s_vtMasterServer.Init(g_pLTClient, "MasterServer", P6_DEFAULT_MASTER, 0.0f);
	}

	GameSpyClient_SetMasterServer(s_vtMasterServer.GetStr(P6_DEFAULT_MASTER));
}


// ----------------------------------------------------------------------- //
//  M-P6-01: names from the network
// ----------------------------------------------------------------------- //

HSTRING P6_FilterNetString(HSTRING hStr)
{
	if (!hStr) return(hStr);

	char* sText = g_pLTClient->GetStringData(hStr);
	if (!sText || !strchr(sText, '%')) return(hStr);

	char sFiltered[256];
	SAFE_STRCPY(sFiltered, sText);
	FilterFormatChars(sFiltered);

	HSTRING hFiltered = g_pLTClient->CreateString(sFiltered);
	if (!hFiltered)
	{
		// keep the name, but not something that formats
		hFiltered = g_pLTClient->CreateString("");
		if (!hFiltered) return(hStr);
	}

	g_pLTClient->FreeString(hStr);
	return(hFiltered);
}


// ----------------------------------------------------------------------- //
//  M-P6-05: rejoin after a timeout between levels
//
//  Clients sometimes time out the moment the server changes level. When a
//  multiplayer connection ends in a timeout, the same server is joined
//  again, at most P6_MAX_REJOINS times until a world is entered. The join
//  cannot start from inside the disconnect event, so it waits for the next
//  client update.
// ----------------------------------------------------------------------- //

#define P6_MAX_REJOINS	2

static int  s_nRejoinsLeft = P6_MAX_REJOINS;
static char s_sRejoinAddress[MAX_SGR_STRINGLEN] = "";

LTBOOL P6_RejoinAfterTimeout(const char* sAddress, int nPort)
{
	if (!g_pGameClientShell || !g_pInterfaceMgr) return(LTFALSE);
	if (g_pGameClientShell->GetDisconnectCode() != LT_DISCON_TIMEOUT) return(LTFALSE);
	if (!sAddress || !sAddress[0] || nPort <= 0) return(LTFALSE);
	if (s_nRejoinsLeft <= 0) return(LTFALSE);

	s_nRejoinsLeft--;

	std::snprintf(s_sRejoinAddress, sizeof(s_sRejoinAddress) - 1, "%s:%d", sAddress, nPort);
	s_sRejoinAddress[sizeof(s_sRejoinAddress) - 1] = '\0';

	g_pGameClientShell->ClearDisconnectCode();
	g_pInterfaceMgr->ClearAllScreenBuffers();
	g_pInterfaceMgr->StartingNewGame();

	g_pLTClient->CPrint("Connection to %s timed out; joining again (%d of %d)",
	                    s_sRejoinAddress, P6_MAX_REJOINS - s_nRejoinsLeft, P6_MAX_REJOINS);
	return(LTTRUE);
}

void P6_ResetRejoin()
{
	s_nRejoinsLeft = P6_MAX_REJOINS;
}

void P6_UpdateRejoin()
{
	if (!s_sRejoinAddress[0] || !g_pGameClientShell) return;

	char sAddress[MAX_SGR_STRINGLEN];
	SAFE_STRCPY(sAddress, s_sRejoinAddress);
	s_sRejoinAddress[0] = '\0';

	if (!g_pGameClientShell->DoJoinGame(sAddress))
	{
		g_pInterfaceMgr->LoadFailed();
		g_pInterfaceMgr->ConnectionFailed(LT_ERROR);
	}
}


// ----------------------------------------------------------------------- //
//  M-P6-06: join log
// ----------------------------------------------------------------------- //

void P6_LogJoinAttempt(const char* sAddress, int nAttempt, int nAttempts)
{
	g_pLTClient->CPrint("Joining %s (attempt %d of %d)", sAddress ? sAddress : "?", nAttempt, nAttempts);
}

void P6_LogJoinResult(const char* sAddress, LTBOOL bJoined)
{
	if (bJoined)
		g_pLTClient->CPrint("Joined %s", sAddress ? sAddress : "?");
	else
		g_pLTClient->CPrint("Could not join %s", sAddress ? sAddress : "?");
}


// ----------------------------------------------------------------------- //
//  M-P6-10: the join password
// ----------------------------------------------------------------------- //

static char s_szJoinPassword[MAX_PASSWORD];

void P6_SetJoinPassword(const char* sPassword)
{
	SAFE_STRCPY(s_szJoinPassword, sPassword ? sPassword : "");
}

const char* P6_GetJoinPassword()
{
	return(s_szJoinPassword);
}
