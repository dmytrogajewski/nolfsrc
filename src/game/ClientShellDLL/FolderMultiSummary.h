// FolderMultiSummary.h: interface for the CFolderMultiSummary class.
//
//////////////////////////////////////////////////////////////////////

#ifndef _FOLDER_MP_SUMMARY_H_
#define _FOLDER_MP_SUMMARY_H_

#pragma once

#include "BaseFolder.h"

class CFolderMultiSummary : public CBaseFolder
{
public:
	CFolderMultiSummary();
	virtual ~CFolderMultiSummary();

	// Build the folder
    LTBOOL   Build();
	void	Escape();

	LTBOOL	OnEnter() {Escape(); return LTTRUE;}
	LTBOOL	OnLButtonUp(int x, int y) {Escape(); return LTTRUE;}

    void    OnFocus(LTBOOL bFocus);
    uint32  OnCommand(uint32 dwCommand, uint32 dwParam1, uint32 dwParam2);

	virtual LTBOOL	Render(HSURFACE hDestSurf);


protected:
//	void	BuildObjectivesList();

	LTBOOL 	m_bEscaped;


};

#endif // _FOLDER_MP_SUMMARY_H_