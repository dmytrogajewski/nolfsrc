/****************************************************************************
;
;	 MODULE:		GAMESPYCLIENTMGR (.H)
;
;	PURPOSE:		Game Spy Client Manager
;
;	HISTORY:		08/01/00  [blg]  This file was created
;
;	COMMENT:		Copyright (c) 2000, LithTech, Inc.
;
****************************************************************************/

#pragma once
#include "ltbasetypes.h"
#include <cstdint>


// Includes...

#ifdef GSCM_LOCAL_BUILD
	#include "../gamespy/cengine/goaceng.h"
	#include "../gamespy/gamespyclientmgr/GameSpyServer.h"
#else
	#include "../gamespy/cengine/goaceng.h"
	#include "../gamespy/gamespyclientmgr/GameSpyServer.h"
#endif


// Libraries...

#pragma comment(lib, "wsock32.lib")


// Defines...

#define	GSCM_MAX_STRING		128
#define	GSCM_MAX_UPDATES	12


// Classes...

class CGameSpyClientMgr
{
	// Member functions...

public:
	CGameSpyClientMgr() { Clear(); }
	~CGameSpyClientMgr() { Term(); }

	LTBOOL				Init(const char* sGameName, const char* sSecretKey);
	void				Term();

	LTBOOL				IsInitialized() { return(m_bInit); }

	int					GetNumServers();
	int					GetState();
	int					GetProgress() { return(m_nProgress); }

	void				ClearServers();
	LTBOOL				RefreshServers(LTBOOL bAsync = LTTRUE);

	CGameSpyServer*		GetServer(int nIndex);
	CGameSpyServer*		GetServerFromHandle(void* pHandle);
	CGameSpyServer*		GetFirstServer();
	CGameSpyServer*		GetNextServer();

	LTBOOL				ExistNewServer();

	void				SortServersByName(LTBOOL bAscending = LTTRUE);
	void				SortServersByPing(LTBOOL bAscending = LTTRUE);
	void				SortServersByPlayers(LTBOOL bAscending = LTFALSE);
	void				SortServersByMap(LTBOOL bAscending = LTTRUE);
	void				SortServersByGameType(LTBOOL bAscending = LTTRUE);
	void				SortServers(LTBOOL bAscending, char* sSortKey, GCompareMode eCompareMode);

	void				Update();

	void				HandleCallback(GServerList pServerList, int nMsg, void* pParam1, void* pParam2);


protected:
	void				Clear();

	LTBOOL				CreateServerList();
	void				FreeServerList();

	CGameSpyServer*		CreateGameSpyServer(GServer pGServer);


	// Member variables...

private:
	LTBOOL				m_bInit;
	GServerList			m_pServerList;
	char				m_sGameName[GSCM_MAX_STRING];
	char				m_sSecretKey[GSCM_MAX_STRING];
	int					m_nGetServerIndex;
	int					m_nProgress;
	LTBOOL				m_bNewServer;
	CGameSpyServer		m_CurServer;
};






