// FolderHostLevels.h: interface for the CFolderHostLevels class.
//
//////////////////////////////////////////////////////////////////////

#ifndef _FOLDER_HOST_LEVELS_H_
#define _FOLDER_HOST_LEVELS_H_

#pragma once

#include "BaseFolder.h"


class CFolderHostLevels : public CBaseFolder
{
public:
	CFolderHostLevels();
	virtual ~CFolderHostLevels();

	// Build the folder
    LTBOOL   Build();

    void    OnFocus(LTBOOL bFocus);
    LTBOOL   Render(HSURFACE hDestSurf);

    LTBOOL   OnLButtonDown(int x, int y);
    LTBOOL   OnRButtonDown(int x, int y);
    LTBOOL   OnLButtonUp(int x, int y);
    LTBOOL   OnRButtonUp(int x, int y);
    LTBOOL   OnUp();
    LTBOOL   OnDown();
    LTBOOL   OnLeft();
    LTBOOL   OnRight();
    LTBOOL   OnEnter();
	LTBOOL   OnLButtonDblClick(int x, int y);
	LTBOOL   OnMouseMove(int x, int y);


protected:
    uint32  OnCommand(uint32 dwCommand, uint32 dwParam1, uint32 dwParam2);
    LTBOOL   FillLevelList(CListCtrl *pList, char* sDir);
	void	LoadLevelList(CListCtrl *pList);
	void	SaveLevelList(CListCtrl *pList);
	void	AddLevelToList(CListCtrl *pList, char* sGameLevel);
	void	UpdateButtons();
    void    DrawFrame(HSURFACE hDestSurf, LTRect *rect, LTBOOL bSel = LTFALSE);
	// GOTY: reconstructed from the Game of the Year CShell.dll
	LTBOOL	IsUserLevel(char* sLevel, char** pList);

	int					m_nGameType;

	CListCtrl*			m_pAvailLevels;
	CListCtrl*			m_pSelLevels;
	CStaticTextCtrl*	m_pAddLevel;
	CStaticTextCtrl*	m_pRemoveLevel;
	CStaticTextCtrl*	m_pAddAll;
	CStaticTextCtrl*	m_pRemoveAll;

	// GOTY: reconstructed from the Game of the Year CShell.dll -- the note
	// under the level list about the user-designed maps
	HSTRING				m_hUserLevelNote;
	HSTRING				m_hGOTYNote1;
	HSTRING				m_hGOTYNote2;
};

#endif // _FOLDER_HOST_LEVELS_H_