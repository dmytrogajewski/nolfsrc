// ----------------------------------------------------------------------- //
//
// MODULE  : p6_server.cpp
//
// PURPOSE : The modern build's dedicated server, NolfServ.exe: see
//           p6_server.h (MODERN.md, P6)
//
// ----------------------------------------------------------------------- //

#include "stdafx.h"
#include "../../engine/nolfserv/ServerDlg.h"
#include "nolfserv/p6_server.h"

// ModernLog() (M-P2-07): the game DLLs' log, with the same "first line of
// the run empties it" rule, shared with the Object.lto this server loads;
// P6_InitServerLog() points both at modern-server.log.
#include "cshell/p2_log_body.h"

// The master server a game with no MasterServer setting reports to
// (src/lithshared/gamespy/, M-P6-02).
#define P6_DEFAULT_MASTER	"master.openspy.net"


void P6_InitServerLog()
{
	nolf::platform::set_log_file(P6_SERVER_LOG_FILE);
}


void P6_LogConsole(const char* pLine)
{
	char szLine[512];
	int  nLen;

	if (!pLine) return;

	strncpy(szLine, pLine, sizeof(szLine) - 1);
	szLine[sizeof(szLine) - 1] = '\0';

	// the console's own line ends
	nLen = strlen(szLine);
	while (nLen > 0 && (szLine[nLen - 1] == '\r' || szLine[nLen - 1] == '\n'))
		szLine[--nLen] = '\0';

	if (!nLen) return;

	ModernLog("server: %s", szLine);
}


BOOL P6_LevelOption(CServerDlg* pDlg, const char* szArg, const char* szValue,
                    int& nLevels, int& nCALevels)
{
	const char* szCount;
	const char* szFormat;
	int*        pCount;
	CString     sKey;

	if (!pDlg || !szArg || !szValue) return FALSE;

	if (!stricmp(szArg, "-NetLevel"))
	{
		szFormat = "NetLevel%i";
		szCount  = "NetNumLevels";
		pCount   = &nLevels;
	}
	else if (!stricmp(szArg, "-NetCALevel"))
	{
		szFormat = "NetCALevel%i";
		szCount  = "NetCANumLevels";
		pCount   = &nCALevels;
	}
	else
	{
		return FALSE;
	}

	// at most what the wizard's worlds page allows
	if (*pCount >= 50)
		return TRUE;

	sKey.Format(szFormat, *pCount);
	pDlg->SetSettingString(sKey, szValue);
	(*pCount)++;
	pDlg->SetSettingFloat(szCount, (float)*pCount);

	return TRUE;
}


void P6_ShowMasterServer(CServerDlg* pDlg)
{
	if (!pDlg) return;

	// where players find the game: its address and port, and its size
	ServerInterface* pServerMgr = pDlg->GetServerMgr();
	if (pServerMgr)
	{
		char           sAddress[128];
		unsigned short nPort = 0;

		sAddress[0] = '\0';
		EnterCriticalSection(pDlg->GetCS());
		pServerMgr->GetTcpIpAddress(sAddress, sizeof(sAddress), nPort);
		LeaveCriticalSection(pDlg->GetCS());
		sAddress[sizeof(sAddress) - 1] = '\0';

		pDlg->WriteConsoleString("Hosting \"%s\" on %s, UDP port %u, for up to %d players.",
			(LPCTSTR)pDlg->m_sServerName, sAddress[0] ? sAddress : "this machine", nPort,
			(int)pDlg->GetServerSettingFloat("NetMaxPlayers"));
	}

	CString sMaster = pDlg->GetSettingString("MasterServer", "");
	sMaster.TrimLeft();
	sMaster.TrimRight();

	if (!sMaster.CompareNoCase("none"))
	{
		pDlg->WriteConsoleString("%s", "Master server: none (the game is not listed).");
	}
	else
	{
		pDlg->WriteConsoleString("Master server: %s", sMaster.IsEmpty() ? P6_DEFAULT_MASTER : (LPCTSTR)sMaster);
	}
}
