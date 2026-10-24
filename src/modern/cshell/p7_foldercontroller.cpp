// p7_foldercontroller.cpp -- the Controller options folder (MODERN.md,
// M-P7-06): the right stick's look speed and direction, both sticks' dead
// zones (cshell/p7_pad.cpp reads them), and the controller's default
// buttons back (defctrls.cfg's ##gamepad lines; the keyboard's and the
// mouse's are left alone). The buttons themselves are bound in Controls'
// Customize, like any key.

#include "Stdafx.h"
#include "FolderMgr.h"
#include "FolderCommands.h"
#include "ClientRes.h"
#include "InterfaceMgr.h"
#include "cshell/p3_ui.h"
#include "cshell/p7_pad.h"
#include "cshell/p7_foldercontroller.h"

namespace
{
	int kGap = 250;
	int kWidth = 160;

	void ConfirmCallBack(LTBOOL bReturn, void* pData)
	{
		CFolderController* pFolder = (CFolderController*)pData;
		if (pFolder)
			pFolder->ConfirmRestore(bReturn);
	}

	int Percent(const char* pVar, float fDefault)
	{
		return (int)(100.0f * GetConsoleFloat((char*)pVar, fDefault) + 0.5f);
	}
}

CFolderController::CFolderController()
{
	m_nLookSpeed = 100;
	m_bInvertLook = LTFALSE;
	m_nLookDeadZone = 15;
	m_nMoveDeadZone = 20;
}

CFolderController::~CFolderController()
{
}

LTBOOL CFolderController::Build()
{
	CreateTitle(IDS_P7_TITLE_CONTROLLER);

	if (g_pLayoutMgr->HasCustomValue(FOLDER_ID_CONTROLLER,"ColumnWidth"))
		kGap = g_pLayoutMgr->GetFolderCustomInt(FOLDER_ID_CONTROLLER,"ColumnWidth");
	if (g_pLayoutMgr->HasCustomValue(FOLDER_ID_CONTROLLER,"SliderWidth"))
		kWidth = g_pLayoutMgr->GetFolderCustomInt(FOLDER_ID_CONTROLLER,"SliderWidth");
	kGap = P3_MenuLen(kGap);
	kWidth = P3_MenuLen(kWidth);

	CSliderCtrl *pSlider = AddSlider(IDS_P7_LOOK_SPEED, IDS_P7_HELP_LOOK_SPEED, kGap, kWidth, &m_nLookSpeed);
	pSlider->SetSliderRange(25, 300);
	pSlider->SetSliderIncrement(25);
	pSlider->SetNumericDisplay(LTTRUE);

	CToggleCtrl *pToggle = AddToggle(IDS_P7_INVERT_LOOK, IDS_P7_HELP_INVERT_LOOK, kGap, &m_bInvertLook);
	pToggle->SetOnString(IDS_YES);
	pToggle->SetOffString(IDS_NO);

	pSlider = AddSlider(IDS_P7_LOOK_DEADZONE, IDS_P7_HELP_LOOK_DEADZONE, kGap, kWidth, &m_nLookDeadZone);
	pSlider->SetSliderRange(0, 50);
	pSlider->SetSliderIncrement(5);
	pSlider->SetNumericDisplay(LTTRUE);

	pSlider = AddSlider(IDS_P7_MOVE_DEADZONE, IDS_P7_HELP_MOVE_DEADZONE, kGap, kWidth, &m_nMoveDeadZone);
	pSlider->SetSliderRange(0, 50);
	pSlider->SetSliderIncrement(5);
	pSlider->SetNumericDisplay(LTTRUE);

	AddTextItem(IDS_P7_RESTORE, FOLDER_CMD_RESET_DEFAULTS, IDS_P7_HELP_RESTORE);

	// Make sure to call the base class
	if (!CBaseFolder::Build()) return LTFALSE;

	UseBack(LTTRUE, LTTRUE);
	return LTTRUE;
}

uint32 CFolderController::OnCommand(uint32 dwCommand, uint32 dwParam1, uint32 dwParam2)
{
	if (dwCommand == FOLDER_CMD_RESET_DEFAULTS)
	{
		HSTRING hString = g_pLTClient->FormatString(IDS_P7_CONFIRM_RESTORE);
		g_pInterfaceMgr->ShowMessageBox(hString, LTMB_YESNO, ConfirmCallBack, this, LTFALSE);
		g_pLTClient->FreeString(hString);
		return 1;
	}

	return CBaseFolder::OnCommand(dwCommand, dwParam1, dwParam2);
}

void CFolderController::ConfirmRestore(LTBOOL bYes)
{
	if (!bYes)
		return;

	P7Pad_RestoreDefaultBindings();
	g_pLTClient->WriteConfigFile("autoexec.cfg");
}

void CFolderController::OnFocus(LTBOOL bFocus)
{
	if (bFocus)
	{
		m_nLookSpeed = Percent("PadLookSpeed", 1.0f);
		m_bInvertLook = (GetConsoleInt("PadInvertY", 0) != 0);
		m_nLookDeadZone = Percent("PadLookDeadZone", 0.15f);
		m_nMoveDeadZone = Percent("PadMoveDeadZone", 0.2f);

		UpdateData(LTFALSE);
	}
	else
	{
		UpdateData();

		WriteConsoleFloat("PadLookSpeed", (LTFLOAT)m_nLookSpeed / 100.0f);
		WriteConsoleInt("PadInvertY", m_bInvertLook ? 1 : 0);
		WriteConsoleFloat("PadLookDeadZone", (LTFLOAT)m_nLookDeadZone / 100.0f);
		WriteConsoleFloat("PadMoveDeadZone", (LTFLOAT)m_nMoveDeadZone / 100.0f);

		g_pLTClient->WriteConfigFile("autoexec.cfg");
	}
	CBaseFolder::OnFocus(bFocus);
}
