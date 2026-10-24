// FolderJoinLAN.h: interface for the CFolderJoinLAN class.
//
//////////////////////////////////////////////////////////////////////

#ifndef _FOLDER_JOIN_LAN_H_
#define _FOLDER_JOIN_LAN_H_

#pragma once

#include "BaseFolder.h"

class CFolderJoinLAN : public CBaseFolder
{
public:
	CFolderJoinLAN();
	virtual ~CFolderJoinLAN();

	// Build the folder
    LTBOOL   Build();

	LTBOOL	 Render(HSURFACE hDestSurf);

    LTBOOL   OnLButtonUp(int x, int y);
    LTBOOL   OnRButtonUp(int x, int y);

    void    OnFocus(LTBOOL bFocus);
	LTBOOL	HandleKeyDown(int key, int rep);
	void	Escape();

	void	JoinCurGame();

protected:
    uint32  OnCommand(uint32 dwCommand, uint32 dwParam1, uint32 dwParam2);

	LTBOOL	InitSessions();
	void	FindServers();
	LTBOOL	SetService();
	LTBOOL	IsCurrentGame(CString sAddress);
	LTBOOL	DoJoinGame(CString sAddress);

	LTBOOL	IsValidPort(uint16 nPort);

	int		m_nFrameDelay;

	char m_szPort[8];

	CGroupCtrl			*m_pPortGroup;
	CLTGUIEditCtrl		*m_pPortEdit;
	CLTGUITextItemCtrl	*m_pPortLabel;
#ifdef NOLFSRC_MODERN
	// M-P6-10: the password for a locked server, which checks it
	char				m_szP6Password[MAX_PASSWORD];
	CGroupCtrl			*m_pP6PassGroup;
	CLTGUIEditCtrl		*m_pP6PassEdit;
	CLTGUITextItemCtrl	*m_pP6PassLabel;
#endif

	CMoArray<CString>	m_lstSessions;

};

inline LTBOOL CFolderJoinLAN::IsValidPort(uint16 nPort)
{
	return LTTRUE;
}


#endif // _FOLDER_MULTI_H_