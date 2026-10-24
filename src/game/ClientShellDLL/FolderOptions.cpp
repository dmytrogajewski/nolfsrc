// FolderOptions.cpp: implementation of the CFolderOptions class.
//
//////////////////////////////////////////////////////////////////////
#include "Stdafx.h"
#include "FolderOptions.h"
#include "FolderMgr.h"
#include "FolderCommands.h"
#include "ClientRes.h"
// M-P3-13: the Interface options folder
#include "cres/p3.h"

#include "GameClientShell.h"
extern CGameClientShell* g_pGameClientShell;
// M-P4-02: jukebox
#include "cres/p4.h"


//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CFolderOptions::CFolderOptions()
{
}

CFolderOptions::~CFolderOptions()
{

}

// Build the folder
LTBOOL CFolderOptions::Build()
{

	CreateTitle(IDS_TITLE_OPTIONS);

	AddTextItem(IDS_SCREEN,			FOLDER_CMD_DISPLAY,		IDS_HELP_DISPLAY);
	AddTextItem(IDS_SOUND,			FOLDER_CMD_AUDIO,		IDS_HELP_SOUND);
	AddTextItem(IDS_CONTROLS,		FOLDER_CMD_CONTROLS,	IDS_HELP_CONTROLS);
	AddTextItem(IDS_GAME_OPTIONS,	FOLDER_CMD_GAME,		IDS_HELP_GAME_OPTIONS);
	AddTextItem(IDS_PERFORMANCE,	FOLDER_CMD_PERFORMANCE,	IDS_HELP_PERFORMANCE);
	// M-P3-13 (P3), then P4's: the modern build's options folders
	AddTextItem(IDS_P3_INTERFACE,	FOLDER_CMD_INTERFACE,	IDS_P3_HELP_INTERFACE);
	// M-P4-02: jukebox
	AddTextItem(IDS_P4_JUKEBOX,		FOLDER_CMD_JUKEBOX,		IDS_P4_HELP_JUKEBOX);

	// Make sure to call the base class
	if (! CBaseFolder::Build()) return LTFALSE;

	UseBack(LTTRUE,LTTRUE);
	return LTTRUE;
}

uint32 CFolderOptions::OnCommand(uint32 dwCommand, uint32 dwParam1, uint32 dwParam2)
{
	switch(dwCommand)
	{
	case FOLDER_CMD_DISPLAY:
		{
			m_pFolderMgr->SetCurrentFolder(FOLDER_ID_DISPLAY);
			break;
		}
	case FOLDER_CMD_AUDIO:
		{
			m_pFolderMgr->SetCurrentFolder(FOLDER_ID_AUDIO);
			break;
		}
	case FOLDER_CMD_GAME:
		{
			m_pFolderMgr->SetCurrentFolder(FOLDER_ID_GAME);
			break;
		}
	case FOLDER_CMD_PERFORMANCE:
		{
			m_pFolderMgr->SetCurrentFolder(FOLDER_ID_PERFORMANCE);
			break;
		}
	case FOLDER_CMD_CONTROLS:
		{
			m_pFolderMgr->SetCurrentFolder(FOLDER_ID_CONTROLS);
			break;
		}
	// M-P3-13 (P3), then P4's: the modern build's options folders
	case FOLDER_CMD_INTERFACE:
		{
			m_pFolderMgr->SetCurrentFolder(FOLDER_ID_INTERFACE);
			break;
		}
	// M-P4-02: jukebox
	case FOLDER_CMD_JUKEBOX:
		{
			m_pFolderMgr->SetCurrentFolder(FOLDER_ID_JUKEBOX);
			break;
		}
	default:
		return CBaseFolder::OnCommand(dwCommand,dwParam1,dwParam2);
	}
	return 1;
};


// Change in focus
void    CFolderOptions::OnFocus(LTBOOL bFocus)
{
	if (bFocus)
	{
        UpdateData(LTFALSE);
	}
	else
	{
		UpdateData();
	}
	CBaseFolder::OnFocus(bFocus);
}

