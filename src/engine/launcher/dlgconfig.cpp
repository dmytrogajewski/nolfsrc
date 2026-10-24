// *********************************************************************** //
//
//	MODULE:		DLGCONFIG (.CPP)
//
//	PURPOSE:	The configuration sub-dialogs reachable from the main
//				launcher window - performance/detail, advanced options -
//				plus the persisted command-line settings.
//
// *********************************************************************** //

#include "stdafx.h"
#include "regmgr.h"
#include "launcher.h"


// Globals...

int		g_bSaveCommands;		// 0x304


// Prototypes...

BOOL	CALLBACK DetailDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
BOOL	CALLBACK AdvancedDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
BOOL	CALLBACK CustomizeDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
BOOL	CALLBACK AdvHelpDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
BOOL	CALLBACK Adv3DDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void	InitCommandControls(HWND hWnd);
void	SaveCommandControls(HWND hWnd);
extern int g_bRestoreDefaults;


// *********************************************************************** //
//
//	ROUTINE:	DoDetailDialog
//
//	PURPOSE:	Runs the performance/detail settings dialog
//
// *********************************************************************** //

int DoDetailDialog(HWND hWnd)
{
	return(DialogBoxParam(g_hInst, "DETAIL", hWnd, (DLGPROC)DetailDlgProc, 0));
}


// *********************************************************************** //
//
//	ROUTINE:	DoAdvancedDialog
//
//	PURPOSE:	Runs the advanced options dialog, warning the user the
//				first time
//
// *********************************************************************** //

int DoAdvancedDialog(HWND hWnd)
{
	// The modern build opens the dialog without this warning; its Help
	// button still explains each option.

	int nResult = DialogBoxParam(g_hInst, "ADVANCED", hWnd, (DLGPROC)AdvancedDlgProc, 0);

	g_pRegMgr->Set("AdvWarning", 1);

	return(nResult);
}


// *********************************************************************** //
//
//	ROUTINE:	LoadCommands
//
//	PURPOSE:	Loads the persisted extra command-line string
//
// *********************************************************************** //

void LoadCommands()
{
	if (!g_pRegMgr) return;

	g_bSaveCommands = g_pRegMgr->Get("Save Commands", 1);
	if (!g_bSaveCommands) return;

	char sBuf[254];
	UINT dwSize = 254;
	sBuf[0] = '\0';

	g_pRegMgr->Get("Commands", sBuf, dwSize, NULL);

	g_csCommands = sBuf;
}


// *********************************************************************** //
//
//	ROUTINE:	InitCommandControls   [0x406000]
//
//	PURPOSE:	Seeds the advanced dialog's command-line controls from the
//				registry
//
// *********************************************************************** //

void InitCommandControls(HWND hWnd)
{
	if (!hWnd) return;
	if (!g_pRegMgr) return;

	g_bSaveCommands = g_pRegMgr->Get("Save Commands", 1);

	CheckDlgButton(hWnd, 1043, g_bSaveCommands != 0);

	if (!g_bSaveCommands) return;

	char sBuf[254];
	UINT dwSize = 254;
	sBuf[0] = '\0';

	g_pRegMgr->Get("Commands", sBuf, dwSize, NULL);

	g_csCommands = sBuf;
}


// *********************************************************************** //
//
//	ROUTINE:	SaveCommandControls   [0x406090]
//
//	PURPOSE:	Persists the advanced dialog's command-line settings
//
// *********************************************************************** //

void SaveCommandControls(HWND hWnd)
{
	if (!hWnd) return;
	if (!g_pRegMgr) return;

	g_bSaveCommands = (IsDlgButtonChecked(hWnd, 1043) == BST_CHECKED);
	g_pRegMgr->Set("Save Commands", g_bSaveCommands);

	if (!g_bSaveCommands) return;

	char sBuf[256];
	strcpy(sBuf, g_csCommands);

	g_pRegMgr->Set("Commands", sBuf);
}


// *********************************************************************** //
//
//	ROUTINE:	DetailDlgProc
//
//	PURPOSE:	Dialog procedure for the performance/detail settings dialog
//
// *********************************************************************** //

BOOL CALLBACK DetailDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
		case WM_INITDIALOG:
		{
			HICON hIcon = LoadIcon(g_hInst, "ICON");
			if (hIcon)
			{
				SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
			}
			return(TRUE);
		}

		case WM_COMMAND:
		{
			if (wParam == 1045)
			{
				g_bPerformanceSet = 1;
				g_nPerformanceLevel = 0;
				EndDialog(hWnd, 1);
				return(TRUE);
			}
			else if (wParam == 1047)
			{
				g_bPerformanceSet = 1;
				g_nPerformanceLevel = 1;
				EndDialog(hWnd, 1);
				return(TRUE);
			}
			else if (wParam == 1048)
			{
				g_bPerformanceSet = 1;
				g_nPerformanceLevel = 2;
				EndDialog(hWnd, 1);
				return(TRUE);
			}
			else if (wParam == 2)
			{
				EndDialog(hWnd, 0);
				return(TRUE);
			}

			return(TRUE);
		}
	}

	return(FALSE);
}


// *********************************************************************** //
//
//	ROUTINE:	AdvancedDlgProc
//
//	PURPOSE:	Dialog procedure for the advanced options dialog
//
// *********************************************************************** //

BOOL CALLBACK AdvancedDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
		case WM_INITDIALOG:
		{
			HICON hIcon = LoadIcon(g_hInst, "ICON");
			if (hIcon)
			{
				SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
			}

			InitCommandControls(hWnd);

			CheckDlgButton(hWnd, 1051, g_bDisableMusic != 0);
			CheckDlgButton(hWnd, 1050, g_bDisableSound != 0);
			CheckDlgButton(hWnd, 1055, g_bDisableMovies != 0);
			CheckDlgButton(hWnd, 1057, g_bDisableFog != 0);
			CheckDlgButton(hWnd, 1067, g_bDisableJoystick != 0);
			CheckDlgButton(hWnd, 1054, g_bRestoreDefaults != 0);
			CheckDlgButton(hWnd, 1058, g_bDisableTripBuf != 0);
			CheckDlgButton(hWnd, 1071, g_bDisableCursor != 0);
			CheckDlgButton(hWnd, 1073, g_bDisableLoadscreen != 0);
#ifdef NOLFSRC_MODERN
			// M-P1-29
			CheckDlgButton(hWnd, 1090, g_bDisableLauncherSound != 0);
#endif

			SetDlgItemText(hWnd, 1042, g_csCommands);

			return(TRUE);
		}

		case WM_COMMAND:
		{
			if (wParam == 1)
			{
				char sBuf[255];
				GetDlgItemText(hWnd, 1042, sBuf, 255);
				g_csCommands = sBuf;

				SaveCommandControls(hWnd);

				g_bDisableMusic      = IsDlgButtonChecked(hWnd, 1051) == 1;
				g_bDisableSound      = IsDlgButtonChecked(hWnd, 1050) == 1;
				g_bDisableMovies     = IsDlgButtonChecked(hWnd, 1055) == 1;
				g_bDisableFog        = IsDlgButtonChecked(hWnd, 1057) == 1;
				g_bDisableJoystick   = IsDlgButtonChecked(hWnd, 1067) == 1;
				g_bRestoreDefaults   = IsDlgButtonChecked(hWnd, 1054) == 1;
				g_bDisableTripBuf    = IsDlgButtonChecked(hWnd, 1058) == 1;
				g_bDisableCursor     = IsDlgButtonChecked(hWnd, 1071) == 1;
				g_bDisableLoadscreen = IsDlgButtonChecked(hWnd, 1073) == 1;
#ifdef NOLFSRC_MODERN
				g_bDisableLauncherSound = IsDlgButtonChecked(hWnd, 1090) == 1;
#endif

				EndDialog(hWnd, 1);
				return(TRUE);
			}
			else if (wParam == 2)
			{
				EndDialog(hWnd, 0);
				return(TRUE);
			}
			else if (wParam == 1070)
			{
				DialogBoxParam(g_hInst, "ADV_CUSTOM", hWnd, (DLGPROC)CustomizeDlgProc, 0);
				return(TRUE);
			}
			else if (wParam == 1069)
			{
				DialogBoxParam(g_hInst, "ADV_HELP", hWnd, (DLGPROC)AdvHelpDlgProc, 0);
				return(TRUE);
			}
			else if (wParam == 1066)
			{
				DialogBoxParam(g_hInst, "ADV_3D", hWnd, (DLGPROC)Adv3DDlgProc, 0);
				return(TRUE);
			}
			else if (wParam == 1068)
			{
				Do3DSoundSetup(g_hInst, hWnd, 1);
				return(TRUE);
			}

			return(TRUE);
		}
	}

	return(FALSE);
}


// *********************************************************************** //
//
//	ROUTINE:	GetAdvancedCommands   [0x406660]
//
//	PURPOSE:	Builds the console-variable command line implied by the
//				advanced options check boxes
//
// *********************************************************************** //

CString GetAdvancedCommands()
{
	CString csCommands;

	if (g_bDisableMusic)
		csCommands += " +DisableMusic 1";
	else
		csCommands += " +DisableMusic 0";

	if (g_bDisableSound)
		csCommands += " +DisableSound 1";
	else
		csCommands += " +DisableSound 0";

	if (g_bDisableMovies)
		csCommands += " +DisableMovies 1";

	if (g_bDisableFog)
		csCommands += " +DisableFog 1";

	if (g_bDisableJoystick)
		csCommands += " +DisableJoystick 1";

	if (g_bDisableTripBuf)
		csCommands += " +DisableTripBuf 1";
	else
		csCommands += " +DisableTripBuf 0";

	if (g_bDisableCursor)
		csCommands += " +DisableHardwareCursor 1";
	else
		csCommands += " +DisableHardwareCursor 0";

	if (g_bDisableLoadscreen)
		csCommands += " +DynamicLoadScreen 0";

	if (g_bRestoreDefaults)
		csCommands += " -config defaults.cfg";

	return csCommands;
}


// EOF...


// *********************************************************************** //
//
//	ROUTINE:	DoDisplayDialog   [0x404c80]
//
//	PURPOSE:	Warns once that enumeration is slow, builds the mode list
//				on first use, then runs the DISPLAY dialog
//
// *********************************************************************** //

BOOL CALLBACK DisplayDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

BOOL DoDisplayDialog(HWND hWnd)
{
	// The modern build opens the dialog without this warning: listing the
	// display modes takes no noticeable time on a machine of today.

	if (!g_pDisplayModes)
	{
		g_pDisplayModes = new CDisplayModes;

		if (!g_pDisplayModes->Init("", "autoexec.cfg"))
		{
			return(FALSE);
		}
	}

	if (g_pDisplayModes->m_renderers.GetCount() <= 0)
	{
		ErrorMsg(hWnd, 51, g_csCaption, MB_ICONEXCLAMATION);
		return(FALSE);
	}

	g_bDisplaySet = DialogBoxParam(g_hInst, "DISPLAY", hWnd, (DLGPROC)DisplayDlgProc, 0);

	return(g_bDisplaySet);
}


// EOF...
