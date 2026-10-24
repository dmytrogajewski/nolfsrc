// FolderAdvDisplay.h: interface for the CFolderAdvDisplay class.
//
//////////////////////////////////////////////////////////////////////

#ifndef _FOLDER_ADV_DISPLAY_H_
#define _FOLDER_ADV_DISPLAY_H_

#pragma once

#include "BaseFolder.h"

class CFolderAdvDisplay : public CBaseFolder
{
public:
	CFolderAdvDisplay();
	virtual ~CFolderAdvDisplay();

	// Build the folder
    LTBOOL   Build();
	void	Term();

    void    OnFocus(LTBOOL bFocus);

	void	ConfirmSetting(LTBOOL bConfirm);

	LTBOOL	OnLeft();
	LTBOOL	OnRight();
	LTBOOL	OnLButtonUp(int x, int y);
	LTBOOL	OnRButtonUp(int x, int y);

protected:
    uint32  OnCommand(uint32 dwCommand, uint32 dwParam1, uint32 dwParam2);

	LTBOOL	m_bLightMap;
	LTBOOL	m_bMirrors;
	int		m_nShadows;
	LTBOOL	m_bDetailTextures;
	LTBOOL	m_bEnvMapWorld;
	LTBOOL	m_bEnvMapEnable;
	LTBOOL	m_bTripleBuffer;
	LTBOOL	m_bFixSparkleys;
	LTBOOL	m_bTrilinear;

	uint32	m_nSettingToConfirm;

	CCycleCtrl*		m_pShadows;
//	CToggleCtrl*	m_pOverdraw;
	CToggleCtrl*	m_pLightMap;
	CToggleCtrl*	m_pMirrors;
#ifdef NOLFSRC_MODERN
	// M-P1-09: texture filtering (none, bilinear, trilinear, anisotropic)
	int		m_nP1Filter;		// the cycle's index
	int		m_nP1FilterShown;	// the index the folder opened with
	// M-P1-18: antialiasing (off, 2x, 4x, 8x)
	int		m_nP1AA;
	int		m_nP1AAShown;
#endif
};

#endif // _FOLDER_ADV_DISPLAY_H_