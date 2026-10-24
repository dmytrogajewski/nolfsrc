/****************************************************************************
;
;	 MODULE:		GAMESPYPLAYER (.H)
;
;	PURPOSE:		Game Spy Player Encapsulation Class
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



// Classes...

class CGameSpyPlayer
{
	// Member functions...

public:
	CGameSpyPlayer() { Clear(); }
	~CGameSpyPlayer() { Term(); }

	LTBOOL				Init(GServer pGServer, int nIndex);
	void				Term();

	LTBOOL				IsInitialized() { return(m_bInit); }

	char*				GetName();
	int					GetPing();
	int					GetFrags();
	void*				GetHandle() { return(reinterpret_cast<void*>(static_cast<std::uintptr_t>(m_nIndex))); }

	char*				GetStringValue(char* sKey, char* sDefault = "");
	int					GetIntValue(char* sKey, int nDefault = 0);
	float				GetFloatValue(char* sKey, float fDefault = 0);

protected:
	void				Clear();


	// Member variables...

private:
	GServer				m_pGServer;
	int					m_nIndex;
	LTBOOL				m_bInit;
};


