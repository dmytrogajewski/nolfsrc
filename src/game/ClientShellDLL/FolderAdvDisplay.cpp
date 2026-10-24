// FolderAdvDisplay.cpp: implementation of the CFolderAdvDisplay class.
//
//////////////////////////////////////////////////////////////////////
#include "Stdafx.h"
#include "FolderAdvDisplay.h"
#include "FolderMgr.h"
#include "FolderCommands.h"
#include "ClientRes.h"

#include "GameClientShell.h"
#include "GameSettings.h"
extern CGameClientShell* g_pGameClientShell;
// M-P1-09: texture filtering; M-P1-18: antialiasing
#include "cshell/p1_modern.h"

namespace
{
	// The texture-filtering cycle's choices, and what each one writes to
	// the d3d.ren variables Bilinear, Trilinear and Anisotropic.
	struct P1Filter
	{
		int nStringId;
		int nBilinear;
		int nTrilinear;
		int nAnisotropic;
	};

	const P1Filter s_aP1Filters[] =
	{
		{ IDS_P1_TEXFILTER_NONE,		0, 0, 0 },
		{ IDS_P1_TEXFILTER_BILINEAR,	1, 0, 0 },
		{ IDS_P1_TEXFILTER_TRILINEAR,	1, 1, 0 },
		{ IDS_P1_TEXFILTER_ANISO2,		1, 1, 2 },
		{ IDS_P1_TEXFILTER_ANISO4,		1, 1, 4 },
		{ IDS_P1_TEXFILTER_ANISO8,		1, 1, 8 },
		{ IDS_P1_TEXFILTER_ANISO16,		1, 1, 16 },
	};
	const int kP1NumFilters = sizeof(s_aP1Filters) / sizeof(s_aP1Filters[0]);

	// The choice the current variables come closest to.
	int P1CurrentFilter()
	{
		int nAniso = GetConsoleInt("Anisotropic",0);
		if (nAniso > 0)
		{
			int i;
			for (i = kP1NumFilters - 1; i > 3; i--)
			{
				if (nAniso >= s_aP1Filters[i].nAnisotropic)
					break;
			}
			return i;
		}
		if (GetConsoleInt("Trilinear",0) > 0)
			return 2;
		if (GetConsoleInt("Bilinear",1) > 0)
			return 1;
		return 0;
	}

	// The antialiasing cycle's choices: the renderers' Antialiasing
	// variable (samples per pixel; 0 is off).
	const int s_aP1AASamples[] = { 0, 2, 4, 8 };
	const int kP1NumAA = sizeof(s_aP1AASamples) / sizeof(s_aP1AASamples[0]);

	int P1CurrentAA()
	{
		int nSamples = GetConsoleInt("Antialiasing",0);
		int i;
		for (i = kP1NumAA - 1; i > 0; i--)
		{
			if (nSamples >= s_aP1AASamples[i])
				break;
		}
		return i;
	}
}

namespace
{
	enum eLocalCommands
	{
		CMD_TEXTURES = FOLDER_CMD_CUSTOM+1,
	};
	int kHeaderWidth = 300;
	int kSliderWidth = 200;
	int kSpacerWidth = 25;
	int kTotalWidth  = kHeaderWidth + kSpacerWidth;

	void AreYouSureCallBack(LTBOOL bReturn, void *pData)
	{
		CFolderAdvDisplay *pThisFolder = (CFolderAdvDisplay *)pData;
		if (pThisFolder)
		{
			pThisFolder->ConfirmSetting(bReturn);
		}
	}

	LTBOOL g_bFocus = LTFALSE;
}


//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CFolderAdvDisplay::CFolderAdvDisplay()
{
	m_bLightMap = LTFALSE;
	m_bMirrors = LTFALSE;
	m_nShadows = 0;
	m_bDetailTextures = LTFALSE;
	m_bEnvMapWorld = LTFALSE;
	m_bEnvMapEnable = LTFALSE;
	m_bTripleBuffer = LTFALSE;
	m_bFixSparkleys = LTFALSE;
	m_bTrilinear = LTFALSE;
	// M-P1-09: texture filtering
	m_nP1Filter = 0;
	m_nP1FilterShown = 0;
	m_nP1AA = 0;
	m_nP1AAShown = 0;

	m_pShadows = LTNULL;
//	m_pOverdraw = LTNULL;
	m_pLightMap = LTNULL;
	m_pMirrors = LTNULL;
}

CFolderAdvDisplay::~CFolderAdvDisplay()
{
	Term();
}

// Build the folder
LTBOOL CFolderAdvDisplay::Build()
{

	if (g_pLayoutMgr->HasCustomValue(FOLDER_ID_ADVDISPLAY,"ColumnWidth"))
	{
		kTotalWidth = g_pLayoutMgr->GetFolderCustomInt(FOLDER_ID_ADVDISPLAY,"ColumnWidth");
		kHeaderWidth = kTotalWidth - kSpacerWidth;
	}
	if (g_pLayoutMgr->HasCustomValue(FOLDER_ID_ADVDISPLAY,"SliderWidth"))
		kSliderWidth = g_pLayoutMgr->GetFolderCustomInt(FOLDER_ID_ADVDISPLAY,"SliderWidth");
	// M-P3-03: the columns scale with the menu page
	kTotalWidth = P3_MenuLen(kTotalWidth);
	kHeaderWidth = kTotalWidth - kSpacerWidth;
	kSliderWidth = P3_MenuLen(kSliderWidth);

	CreateTitle(IDS_TITLE_ADVDISPLAY);

	AddTextItem(IDS_TEXTURE_RES, CMD_TEXTURES,	IDS_HELP_TEXTURE_RES);

	m_pLightMap = AddToggle(IDS_LIGHTMAP, IDS_HELP_LIGHTMAP, kTotalWidth, &m_bLightMap );
	m_pLightMap->SetOnString(IDS_ON);
	m_pLightMap->SetOffString(IDS_OFF);

	m_pMirrors = AddToggle(IDS_MIRRORS, IDS_HELP_MIRRORS, kTotalWidth, &m_bMirrors );
	m_pMirrors->SetOnString(IDS_ON);
	m_pMirrors->SetOffString(IDS_OFF);

	m_pShadows = AddCycleItem(IDS_SHADOWS,IDS_HELP_SHADOWS,kHeaderWidth,kSpacerWidth,&m_nShadows);
	m_pShadows->AddString(IDS_NONE);
	m_pShadows->AddString(IDS_CIRCULAR);
//	m_pShadows->AddString(IDS_PROJECTED);
	
	CToggleCtrl* pToggle = AddToggle(IDS_DETAILTEXTURES, IDS_HELP_DETAILTEXTURES, kTotalWidth, &m_bDetailTextures );
	pToggle->SetOnString(IDS_ON);
	pToggle->SetOffString(IDS_OFF);

	pToggle = AddToggle(IDS_ENVIRONMENT_MAP, IDS_HELP_ENVIRONMENT_MAP, kTotalWidth, &m_bEnvMapWorld );
	pToggle->SetOnString(IDS_ON);
	pToggle->SetOffString(IDS_OFF);

	pToggle = AddToggle(IDS_CHROME, IDS_HELP_CHROME, kTotalWidth, &m_bEnvMapEnable );
	pToggle->SetOnString(IDS_ON);
	pToggle->SetOffString(IDS_OFF);

//	m_pOverdraw = AddToggle(IDS_OVERDRAW, IDS_HELP_OVERDRAW, kTotalWidth, &m_bFixSparkleys );
//	m_pOverdraw->SetOnString(IDS_ON);
//	m_pOverdraw->SetOffString(IDS_OFF);

	// M-P1-09: texture filtering replaces the trilinear toggle
	CCycleCtrl* pP1Filter = AddCycleItem(IDS_P1_TEXFILTER,IDS_P1_HELP_TEXFILTER,kHeaderWidth,kSpacerWidth,&m_nP1Filter);
	for (int nP1 = 0; nP1 < kP1NumFilters; nP1++)
	{
		pP1Filter->AddString(s_aP1Filters[nP1].nStringId);
	}

	// M-P1-18: antialiasing
	CCycleCtrl* pP1AA = AddCycleItem(IDS_P1_ANTIALIAS,IDS_P1_HELP_ANTIALIAS,kHeaderWidth,kSpacerWidth,&m_nP1AA);
	pP1AA->AddString(IDS_OFF);
	pP1AA->AddString(IDS_P1_ANTIALIAS_2X);
	pP1AA->AddString(IDS_P1_ANTIALIAS_4X);
	pP1AA->AddString(IDS_P1_ANTIALIAS_8X);

	pToggle = AddToggle(IDS_TRIPLE_BUFF, IDS_HELP_TRIPLE_BUFF, kTotalWidth, &m_bTripleBuffer );
	pToggle->SetOnString(IDS_ON);
	pToggle->SetOffString(IDS_OFF);    
	
	uint32 dwAdvancedOptions = g_pInterfaceMgr->GetAdvancedOptions();
	pToggle->Enable( (dwAdvancedOptions & AO_TRIPLEBUFFER) );


 	// Make sure to call the base class
	if (!CBaseFolder::Build()) return LTFALSE;

	UseBack(LTTRUE,LTTRUE);

	return LTTRUE;

}

void CFolderAdvDisplay::Term()
{
	// Make sure to call the base class
	CBaseFolder::Term();
}

uint32 CFolderAdvDisplay::OnCommand(uint32 dwCommand, uint32 dwParam1, uint32 dwParam2)
{
	switch (dwCommand)
	{
	case CMD_TEXTURES:
	{
		m_pFolderMgr->SetCurrentFolder(FOLDER_ID_TEXTURE);
	} break;

	default:
		return CBaseFolder::OnCommand(dwCommand,dwParam1,dwParam2);
	};

	return 1;
};


void CFolderAdvDisplay::OnFocus(LTBOOL bFocus)
{
	uint32 dwAdvancedOptions = g_pInterfaceMgr->GetAdvancedOptions();
	g_bFocus = LTFALSE;
	LTBOOL bAllowMirrors = (GetConsoleInt("BitDepth",16) == 32);
	if (bFocus)
	{
		

		m_pMirrors->Enable(bAllowMirrors);

        m_bLightMap = (GetConsoleInt("LightMap",0) > 0);
		if (bAllowMirrors)
	        m_bMirrors = (GetConsoleInt("DrawPortals",0) > 0);
		else
			m_bMirrors = LTFALSE;
        m_bDetailTextures = (GetConsoleInt("DetailTextures",0) > 0);
        m_bEnvMapWorld = (GetConsoleInt("EnvMapWorld",0) > 0);
        m_bEnvMapEnable = (GetConsoleInt("EnvMapEnable",0) > 0);
        m_bTripleBuffer = (GetConsoleInt("TripleBuffer",0) > 0) && (dwAdvancedOptions & AO_TRIPLEBUFFER);
        m_bFixSparkleys = (GetConsoleInt("FixSparkleys",0) > 0);
        m_bTrilinear = (GetConsoleInt("Trilinear",0) > 0);
		// M-P1-09: texture filtering
		m_nP1Filter = m_nP1FilterShown = P1CurrentFilter();
		m_nP1AA = m_nP1AAShown = P1CurrentAA();

		m_nShadows = 0;
		if (GetConsoleInt("DrawShadows",0) > 0)
		{
			m_nShadows++;
			if (GetConsoleInt("ModelShadowProj",0) > 0) m_nShadows++;
		}

        UpdateData(LTFALSE);
		g_bFocus = LTTRUE;

	}
	else
	{
        UpdateData(LTTRUE);

		LTBOOL bOldTB = (GetConsoleInt("TripleBuffer",0) > 0);

		WriteConsoleInt("LightMap",m_bLightMap);
		if (bAllowMirrors)
			WriteConsoleInt("DrawPortals",m_bMirrors);
		WriteConsoleInt("DetailTextures",m_bDetailTextures);
		WriteConsoleInt("EnvMapWorld",m_bEnvMapWorld);
		WriteConsoleInt("EnvMapEnable",m_bEnvMapEnable);
		WriteConsoleInt("TripleBuffer",m_bTripleBuffer && (dwAdvancedOptions & AO_TRIPLEBUFFER));
		WriteConsoleInt("FixSparkleys",m_bFixSparkleys);
		// M-P1-09: texture filtering, written only when changed (so that
		// values the cycle has no choice for are kept)
		if (m_nP1Filter != m_nP1FilterShown && m_nP1Filter >= 0 && m_nP1Filter < kP1NumFilters)
		{
			WriteConsoleInt("Bilinear",s_aP1Filters[m_nP1Filter].nBilinear);
			WriteConsoleInt("Trilinear",s_aP1Filters[m_nP1Filter].nTrilinear);
			WriteConsoleInt("Anisotropic",s_aP1Filters[m_nP1Filter].nAnisotropic);
		}

		WriteConsoleInt("DrawShadows",(m_nShadows > 0) );
		WriteConsoleInt("MaxModelShadows",(m_nShadows > 0) );
		WriteConsoleInt("ModelShadowProj",(m_nShadows > 1) );

		// M-P1-18: antialiasing, which the renderer reads when it starts:
		// restart it (once, with a triple-buffer change below)
		if (m_nP1AA != m_nP1AAShown && m_nP1AA >= 0 && m_nP1AA < kP1NumAA)
		{
			WriteConsoleInt("Antialiasing",s_aP1AASamples[m_nP1AA]);
			if (bOldTB == m_bTripleBuffer)
			{
				g_pInterfaceMgr->SetSwitchingRenderModes(LTTRUE);
				g_pLTClient->RunConsoleString("RestartRender");
				g_pInterfaceMgr->SetSwitchingRenderModes(LTFALSE);
			}
		}

		if (bOldTB != m_bTripleBuffer)
		{
			// Set the renderer mode
			g_pInterfaceMgr->SetSwitchingRenderModes(LTTRUE);
			g_pLTClient->RunConsoleString("RestartRender");
			g_pInterfaceMgr->SetSwitchingRenderModes(LTFALSE);

		}

        g_pLTClient->WriteConfigFile("autoexec.cfg");

	}
	CBaseFolder::OnFocus(bFocus);
}



void CFolderAdvDisplay::ConfirmSetting(LTBOOL bConfirm)
{
	if (bConfirm) return;
	switch (m_nSettingToConfirm)
	{
	case IDS_CONFIRM_SHADOWS:
		m_nShadows = 1;
		UpdateData(LTFALSE);
		break;
//	case IDS_CONFIRM_OVERDRAW:
//		m_bFixSparkleys = LTFALSE;
//		UpdateData(LTFALSE);
//		break;
	case IDS_CONFIRM_LIGHTMAP:
		m_bLightMap = LTTRUE;
		UpdateData(LTFALSE);
		break;

	}

}


LTBOOL CFolderAdvDisplay::OnLeft()
{
	if (!CBaseFolder::OnLeft()) return LTFALSE;
	if (GetSelectedControl() == m_pLightMap)
	{
		if (m_pLightMap->GetSelIndex())
		{
			m_nSettingToConfirm = IDS_CONFIRM_LIGHTMAP;
			HSTRING hString = g_pLTClient->FormatString(IDS_CONFIRM_LIGHTMAP);
			g_pInterfaceMgr->ShowMessageBox(hString,LTMB_YESNO,AreYouSureCallBack,this,LTFALSE,LTFALSE);
			g_pLTClient->FreeString(hString);
		}
	}
	return LTTRUE;
}

LTBOOL CFolderAdvDisplay::OnRight()
{
	if (!CBaseFolder::OnRight()) return LTFALSE;
	if (GetSelectedControl() == m_pLightMap)
	{
		if (m_pLightMap->GetSelIndex())
		{
			m_nSettingToConfirm = IDS_CONFIRM_LIGHTMAP;
			HSTRING hString = g_pLTClient->FormatString(IDS_CONFIRM_LIGHTMAP);
			g_pInterfaceMgr->ShowMessageBox(hString,LTMB_YESNO,AreYouSureCallBack,this,LTFALSE,LTFALSE);
			g_pLTClient->FreeString(hString);
		}
	}
	return LTTRUE;
}


LTBOOL CFolderAdvDisplay::OnLButtonUp(int x, int y)
{
	int nControlIndex=0;
	if (GetControlUnderPoint(x, y, &nControlIndex))
	{
		CLTGUICtrl* pCtrl = GetControl(nControlIndex);
		if (pCtrl == m_pLightMap)
		{
			return OnRight();
		}
	}
	return CBaseFolder::OnLButtonUp(x, y);
}

LTBOOL CFolderAdvDisplay::OnRButtonUp(int x, int y)
{
	int nControlIndex=0;
	if (GetControlUnderPoint(x, y, &nControlIndex))
	{
		CLTGUICtrl* pCtrl = GetControl(nControlIndex);
		if (pCtrl == m_pLightMap)
		{
			return OnLeft();
		}
	}
	return CBaseFolder::OnRButtonUp(x, y);
}
