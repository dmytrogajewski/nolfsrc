/****************************************************************************
;
;	 MODULE:		GAMESPYSERVER (.H)
;
;	PURPOSE:		Game Spy Server Encapsulation Class
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

#include "../cengine/goaceng.h"
#include "GameSpyPlayer.h"



// Defines...

#define	GSS_MAX_PLAYERS		64

#define GSS_SORT_NAME		0
#define GSS_SORT_PING		1
#define GSS_SORT_FRAGS		2


// Classes...

class CGameSpyServer
{
	// Member functions...

public:
	CGameSpyServer() { Clear(); }
	~CGameSpyServer() { Term(); }

	LTBOOL				Init(GServer pServer);
	void				Term();

	LTBOOL				IsInitialized() { return(m_bInit); }

	char*				GetName();
	char*				GetGameType();
	int					GetPing();
	int					GetNumPlayers();
	int					GetMaxPlayers();
	char*				GetMap();
	char*				GetAddress();
	void*				GetHandle() { return(m_pGServer); }

	char*				GetStringValue(char* sKey, char* sDefault = "");
	int					GetIntValue(char* sKey, int nDefault = 0);
	float				GetFloatValue(char* sKey, float fDefault = 0);

	int					UpdatePlayers();

	CGameSpyPlayer*		GetPlayer(int nIndex);
	CGameSpyPlayer*		GetFirstPlayer();
	CGameSpyPlayer*		GetNextPlayer();

	void				SortPlayersByName(LTBOOL bAscending = LTTRUE) { SortPlayers(bAscending, GSS_SORT_NAME); }
	void				SortPlayersByPing(LTBOOL bAscending = LTTRUE) { SortPlayers(bAscending, GSS_SORT_PING); }
	void				SortPlayersByFrags(LTBOOL bAscending = LTFALSE) { SortPlayers(bAscending, GSS_SORT_FRAGS); }

protected:
	void				Clear();
	void				SortPlayers(LTBOOL bAscending, int nKey);


	// Member variables...

private:
	GServer				m_pGServer;
	LTBOOL				m_bInit;
	CGameSpyPlayer		m_aPlayers[GSS_MAX_PLAYERS];
	int					m_nNumPlayers;
	int					m_nPlayerIndex;
};



