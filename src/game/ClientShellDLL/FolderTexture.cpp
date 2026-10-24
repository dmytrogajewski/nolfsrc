// FolderTexture.cpp: implementation of the CFolderTexture class.
//
//////////////////////////////////////////////////////////////////////
#include "Stdafx.h"
#include "FolderTexture.h"
#include "FolderMgr.h"
#include "FolderCommands.h"
#include "ClientRes.h"

#include "GameClientShell.h"
#include "GameSettings.h"
extern CGameClientShell* g_pGameClientShell;
#ifdef NOLFSRC_MODERN
// M-P1-26: a fourth preset, Ultra
#include "cshell/p1_modern.h"
#define TEX_LEVELS			4
#define TEX_POTENTIAL		{LTTRUE,LTTRUE,LTTRUE,LTTRUE}
#else
#define TEX_LEVELS			3
#define TEX_POTENTIAL		{LTTRUE,LTTRUE,LTTRUE}
#endif

namespace
{
	int kHeaderWidth = 300;
	int kSliderWidth = 200;
	int kSpacerWidth = 25;
	int kTotalWidth  = kHeaderWidth + kSpacerWidth;

	int nInitVal[6];

#ifdef NOLFSRC_MODERN
	// M-P1-26: Ultra puts every slider at its top, an offset of -2. It
	// cancels the 1 that some textures' own files add (357 of them, among
	// them the character heads and the skyboxes), which High leaves at half
	// size; no file adds more.
	int kDefaultValues[6][TEX_LEVELS] =
	{
		{1,0,0,-2},
		{1,1,0,-2},
		{1,1,0,-2},
		{1,1,0,-2},
		{2,1,0,-2},
		{2,1,0,-2},
	};
#else
	int kDefaultValues[6][3] =
	{
		{1,0,0},
		{1,1,0},
		{1,1,0},
		{1,1,0},
		{2,1,0},
		{2,1,0},
	};
#endif
}


//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CFolderTexture::CFolderTexture()
{
	m_nOverall = 0;

	for (int i = 0; i < 6; i++)
	{
		m_nSliderVal[i] = 0;
		m_pSlider[i] = LTNULL;
	}

}

CFolderTexture::~CFolderTexture()
{
	Term();
}

// Build the folder
LTBOOL CFolderTexture::Build()
{

	if (g_pLayoutMgr->HasCustomValue(FOLDER_ID_TEXTURE,"ColumnWidth"))
	{
		kTotalWidth = g_pLayoutMgr->GetFolderCustomInt(FOLDER_ID_TEXTURE,"ColumnWidth");
		kHeaderWidth = kTotalWidth - kSpacerWidth;
	}
	if (g_pLayoutMgr->HasCustomValue(FOLDER_ID_TEXTURE,"SliderWidth"))
		kSliderWidth = g_pLayoutMgr->GetFolderCustomInt(FOLDER_ID_TEXTURE,"SliderWidth");
#ifdef NOLFSRC_MODERN
	// M-P3-03: the columns scale with the menu page
	kTotalWidth = P3_MenuLen(kTotalWidth);
	kHeaderWidth = kTotalWidth - kSpacerWidth;
	kSliderWidth = P3_MenuLen(kSliderWidth);
#endif

	CreateTitle(IDS_TITLE_TEXTURE);

	m_pOverallCtrl = AddCycleItem(IDS_DETAILLEVEL,IDS_HELP_DETAILLEVEL,kHeaderWidth,kSpacerWidth,&m_nOverall);
	m_pOverallCtrl->AddString(IDS_LOW);
	m_pOverallCtrl->AddString(IDS_MEDIUM);
	m_pOverallCtrl->AddString(IDS_HIGH);
#ifdef NOLFSRC_MODERN
	m_pOverallCtrl->AddString(IDS_P1_TEX_ULTRA);
#endif
	m_pOverallCtrl->AddString(IDS_CUSTOMIZED);

	m_pSlider[0]=AddSlider(IDS_TEXTURE_WORLD, IDS_HELP_TEXTURE_WORLD, kTotalWidth, kSliderWidth, &m_nSliderVal[0]);
	m_pSlider[1]=AddSlider(IDS_TEXTURE_WEAPONS, IDS_HELP_TEXTURE_WEAPONS, kTotalWidth, kSliderWidth, &m_nSliderVal[1]);
	m_pSlider[2]=AddSlider(IDS_TEXTURE_CHARS, IDS_HELP_TEXTURE_CHARS, kTotalWidth, kSliderWidth, &m_nSliderVal[2]);
	m_pSlider[3]=AddSlider(IDS_TEXTURE_PROPS, IDS_HELP_TEXTURE_PROPS, kTotalWidth, kSliderWidth, &m_nSliderVal[3]);
	m_pSlider[4]=AddSlider(IDS_TEXTURE_SFX, IDS_HELP_TEXTURE_SFX, kTotalWidth, kSliderWidth, &m_nSliderVal[4]);
	m_pSlider[5]=AddSlider(IDS_TEXTURE_SKY, IDS_HELP_TEXTURE_SKY, kTotalWidth, kSliderWidth, &m_nSliderVal[5]);

	for (int i = 0; i < 6; i++)
	{
		m_pSlider[i]->SetSliderRange(0,5);
		m_pSlider[i]->SetSliderIncrement(1);
	}
 	// Make sure to call the base class
	if (!CBaseFolder::Build()) return LTFALSE;

	UseBack(LTTRUE,LTTRUE);

	return LTTRUE;

}

void CFolderTexture::Term()
{
	// Make sure to call the base class
	CBaseFolder::Term();
}

uint32 CFolderTexture::OnCommand(uint32 dwCommand, uint32 dwParam1, uint32 dwParam2)
{
	return CBaseFolder::OnCommand(dwCommand,dwParam1,dwParam2);
};


void CFolderTexture::OnFocus(LTBOOL bFocus)
{

	
	if (bFocus)
	{
		LTBOOL bPotential[TEX_LEVELS] = TEX_POTENTIAL;
		for (int i = 0; i < 6; i++)
		{
			char szKey[16];
			sprintf(szKey,"GroupOffset%d",i+1);
			int offset = GetConsoleInt(szKey,0);
			m_nSliderVal[i] = 3 - offset;
			nInitVal[i] = m_nSliderVal[i];
			for (int j = 0; j < TEX_LEVELS; j++)
			{
				if (offset != kDefaultValues[i][j])
					bPotential[j] = LTFALSE;
			}
		}

		m_nOverall = 0;
		while (m_nOverall < TEX_LEVELS && !bPotential[m_nOverall])
			m_nOverall++;

		UpdateData(LTFALSE);
	}
	else
	{
		LTBOOL bRebind = LTFALSE;
        UpdateData(LTTRUE);
		for (int i = 0; i < 6; i++)
		{
			char szKey[16];
			sprintf(szKey,"GroupOffset%d",i+1);
			int offset = 3 - m_nSliderVal[i];
			if (nInitVal[i] != m_nSliderVal[i])
			{
				bRebind = LTTRUE;
				WriteConsoleInt(szKey,offset);
			}
		}
		if (bRebind)
		{
            g_pLTClient->Start3D();
            g_pLTClient->StartOptimized2D();

			g_pInterfaceResMgr->DrawMessage(GetSmallFont(),IDS_REBINDING_TEXTURES);

            g_pLTClient->EndOptimized2D();
            g_pLTClient->End3D();
            g_pLTClient->FlipScreen(0);

			g_pLTClient->RunConsoleString("RebindTextures");
		}

	}
	CBaseFolder::OnFocus(bFocus);
}


LTBOOL CFolderTexture::OnLeft()
{
	if (GetSelectedControl() == m_pOverallCtrl)
	{
		int detailLevel = m_pOverallCtrl->GetSelIndex();
		--detailLevel;
		if (detailLevel < 0)
			detailLevel = TEX_LEVELS - 1;
		m_pOverallCtrl->SetSelIndex(detailLevel);
		SetSliders(detailLevel);
        return LTTRUE;
	}
	else if (IsSlider(GetSelectedControl()))
	{
		LTBOOL bSlid = GetSelectedControl()->OnLeft();
		if (bSlid)
		{
			GetSelectedControl()->UpdateData();
			CheckOverall();
		}
		return bSlid;
	}

	return CBaseFolder::OnLeft();
}

LTBOOL CFolderTexture::OnRight()
{
	if (GetSelectedControl() == m_pOverallCtrl)
	{
		int detailLevel = m_pOverallCtrl->GetSelIndex();
		++detailLevel;
		if (detailLevel > TEX_LEVELS - 1)
			detailLevel = 0;
		m_pOverallCtrl->SetSelIndex(detailLevel);
		SetSliders(detailLevel);
		return LTTRUE;
	}
	else if (IsSlider(GetSelectedControl()))
	{
		LTBOOL bSlid = GetSelectedControl()->OnRight();
		if (bSlid)
		{
			GetSelectedControl()->UpdateData();
			CheckOverall();
		}
		return bSlid;
	}
	return CBaseFolder::OnRight();
}


void CFolderTexture::SetSliders(int nLevel)
{
	if (nLevel < 0 || nLevel >= TEX_LEVELS) return;
	for (int i = 0; i < 6; i++)
	{
		m_nSliderVal[i] = 3-kDefaultValues[i][nLevel];
	}
	m_nOverall = nLevel;
	UpdateData(LTFALSE);
}

void CFolderTexture::CheckOverall()
{
	LTBOOL bPotential[TEX_LEVELS] = TEX_POTENTIAL;
	for (int i = 0; i < 6; i++)
	{
		int offset = 3 - m_nSliderVal[i];
		for (int j = 0; j < TEX_LEVELS; j++)
		{
			if (offset != kDefaultValues[i][j])
				bPotential[j] = LTFALSE;
		}
	}
	m_nOverall = 0;
	while (m_nOverall < TEX_LEVELS && !bPotential[m_nOverall])
		m_nOverall++;

	UpdateData(LTFALSE);
}

LTBOOL CFolderTexture::IsSlider(CLTGUICtrl* pCtrl)
{
	for (int i = 0; i < 6; i++)
	{
		if ((CLTGUICtrl*)m_pSlider[i] == pCtrl) return LTTRUE;
	}
	return LTFALSE;
}


LTBOOL CFolderTexture::OnLButtonUp(int x, int y)
{
	int nControlIndex=0;
	if (GetControlUnderPoint(x, y, &nControlIndex))
	{
		CLTGUICtrl* pCtrl = GetControl(nControlIndex);
		if (pCtrl == m_pOverallCtrl)
		{
			return OnRight();
		}
		else if (IsSlider(pCtrl))
		{
#ifdef NOLFSRC_MODERN
			// M-P3-01: the point in the slider's own frame
			P3_PointToCtrl(pCtrl, x, y);
#endif
			LTBOOL bSlid = pCtrl->OnLButtonUp(x, y);
			if (bSlid)
			{
				pCtrl->UpdateData();
				CheckOverall();
			}
			return bSlid;
		}
	}
	return CBaseFolder::OnLButtonUp(x, y);
}

LTBOOL CFolderTexture::OnRButtonUp(int x, int y)
{
	int nControlIndex=0;
	if (GetControlUnderPoint(x, y, &nControlIndex))
	{
		CLTGUICtrl* pCtrl = GetControl(nControlIndex);
		if (pCtrl == m_pOverallCtrl)
		{
			return OnLeft();
		}
	}
	return CBaseFolder::OnRButtonUp(x, y);
}