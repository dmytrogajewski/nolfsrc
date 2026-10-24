// *********************************************************************** //
//
//	MODULE:		CDCHECK (.CPP)
//
//	PURPOSE:	CD-ROM detection for the NOLF launcher.  The play disc is
//				identified by \DATA\NOLF.REZ; a disc with \DATA\NOLF2.REZ
//				present is the other disc (prompting a swap message).
//
// *********************************************************************** //

#include "stdafx.h"
#include "regmgr.h"
#include "RezFind.h"
#include "launcher.h"


// Globals...

char	g_chCdRom;					// 0x4160 - cached CD-ROM drive letter


// Prototypes...

BOOL	FindCd(BOOL* pbOtherCd);
BOOL	ExistFile(const char* sFile);
char	CheckDriveForCd(char chDrive, BOOL* pbOtherCd);
char	CheckDriveForOtherCd(char chDrive);


// *********************************************************************** //
//
//	ROUTINE:	CheckCd
//
//	PURPOSE:	Makes sure the play disc is in the drive, prompting the
//				user until it is (or until they cancel)
//
// *********************************************************************** //

BOOL CheckCd(HWND hWnd)
{
	BOOL bOtherCd = FALSE;
	// M-P1-15: no disc check. The game's data is installed beside the
	// launcher (FindRezFiles found it in the game directory at startup):
	// the disc is only needed when the data is on it.
	{
		char* sGameRez = GetGameRezFile();
		if (sGameRez && sGameRez[0] != '\0' && sGameRez[1] != ':')
			return(TRUE);
	}

	{
		CWaitCursor wait;
		if (FindCd(&bOtherCd)) return(TRUE);
	}

	while (1)
	{
		char sMsg[256];

		int nID = 8;
		if (bOtherCd) nID = 14;

		if (!LoadString(g_hInst, nID, sMsg, 250))
		{
			if (bOtherCd)
			{
				strcpy(sMsg, "Please remove the NOLF disc 1 CD-ROM and insert the NOLF disc 2 CD-ROM into your CD-ROM drive.");
			}
			else
			{
				strcpy(sMsg, "Please insert the NOLF disc 2 CD-ROM into your CD-ROM drive.");
			}
		}

		if (MessageBox(hWnd, sMsg, g_csCaption, MB_OKCANCEL | MB_ICONEXCLAMATION) != IDOK)
		{
			break;
		}

		CWaitCursor wait;

		if (FindCd(&bOtherCd)) return(TRUE);

		Sleep(1000);

		if (FindCd(&bOtherCd)) return(TRUE);
	}

	return(FALSE);
}


// *********************************************************************** //
//
//	ROUTINE:	FindCd
//
//	PURPOSE:	Looks for the play disc, refreshing the rez info if needed
//
// *********************************************************************** //

BOOL FindCd(BOOL* pbOtherCd)
{
	char chCdRom = FindCdRom(pbOtherCd);
	if (!chCdRom) return(FALSE);

	char* sGameRez = GetGameRezFile();
	if (!sGameRez || sGameRez[0] == '\0')
	{
		FindRezFiles(chCdRom);
	}

	return(TRUE);
}


// *********************************************************************** //
//
//	ROUTINE:	ExistFile
//
//	PURPOSE:	Determines if the given file exists
//
// *********************************************************************** //

BOOL ExistFile(const char* sFile)
{
	if (!sFile) return(FALSE);
	if (sFile[0] == '\0') return(FALSE);

	OFSTRUCT ofs;

	return(OpenFile(sFile, &ofs, OF_EXIST) != HFILE_ERROR);
}


// *********************************************************************** //
//
//	ROUTINE:	FindCdRom
//
//	PURPOSE:	Finds the CD-ROM drive containing the play disc.  Fills in
//				sDrive if a drive containing the other disc was found.
//
// *********************************************************************** //

char FindCdRom(BOOL* pbOtherCd)
{
	// Use the cached value if we have one...

	if (g_chCdRom) return(g_chCdRom);

	*pbOtherCd = FALSE;


	// Check the drive stored in the registry first...

	if (g_pRegMgr)
	{
		char sBuf[30];
		UINT dwSize = 30;
		sBuf[0] = '\0';

		if (g_pRegMgr->Get("CdRom Drive", sBuf, dwSize, NULL))
		{
			if (sBuf[0] > 20)
			{
				char chDrive = CheckDriveForCd(sBuf[0], pbOtherCd);
				if (chDrive) return(chDrive);
			}
		}
	}


	// Check all of the drives...

	for (char ch = 'A'; ch <= 'Z'; ch++)
	{
		char chDrive = CheckDriveForCd(ch, pbOtherCd);
		if (chDrive) return(chDrive);
	}

	return(0);
}


// *********************************************************************** //
//
//	ROUTINE:	CheckDriveForCd
//
//	PURPOSE:	Determines if the given drive is a CD-ROM drive with the
//				play disc in it
//
// *********************************************************************** //

char CheckDriveForCd(char chDrive, BOOL* pbOtherCd)
{
	char sRoot[256];
	sprintf(sRoot, "%c:\\", chDrive);

	if (GetDriveType(sRoot) == DRIVE_CDROM)
	{
		char sFile[256];
		sprintf(sFile, "%c:%s", chDrive, (const char*)g_csRez1);

		if (ExistFile(sFile))
		{
			g_chCdRom = chDrive;
			return(chDrive);
		}
	}

	if (CheckDriveForOtherCd(chDrive))
	{
		*pbOtherCd = TRUE;
	}

	return(0);
}


// *********************************************************************** //
//
//	ROUTINE:	CheckDriveForOtherCd
//
//	PURPOSE:	Determines if the given drive is a CD-ROM drive with the
//				other (non-play) disc in it
//
// *********************************************************************** //

char CheckDriveForOtherCd(char chDrive)
{
	char sRoot[256];
	sprintf(sRoot, "%c:\\", chDrive);

	if (GetDriveType(sRoot) == DRIVE_CDROM)
	{
		char sFile[256];
		sprintf(sFile, "%c:%s", chDrive, (const char*)g_csRez2);

		if (ExistFile(sFile))
		{
			return(chDrive);
		}
	}

	return(0);
}


// *********************************************************************** //
//
//	ROUTINE:	ExistFileInDir   [0x404b30]
//
//	PURPOSE:	Does sFile exist inside sDir?  An empty directory means
//				"look wherever ExistFile looks"
//
// *********************************************************************** //

BOOL ExistFileInDir(const char* sDir, const char* sFile)
{
	if (!sDir) return(FALSE);
	if (!sFile) return(FALSE);
	if (!sFile[0]) return(FALSE);

	if (!sDir[0])
	{
		return(ExistFile(sFile));
	}

	char sPath[256];
	strcpy(sPath, sDir);

	int nLen = strlen(sPath);
	if (nLen > 0 && sPath[nLen - 1] == '\\')
	{
		sPath[nLen - 1] = '\0';
	}

	strcat(sPath, "\\");
	strcat(sPath, sFile);

	OFSTRUCT of;
	return(OpenFile(sPath, &of, OF_EXIST) != HFILE_ERROR);
}


// *********************************************************************** //
//
//	ROUTINE:	RunProgram   [0x4043f0]
//
//	PURPOSE:	Launches sFile out of sDir, with sDir as the working
//				directory when there is one
//
// *********************************************************************** //

BOOL RunProgram(const char* sDir, const char* sFile)
{
	char				sCmd[256];
	STARTUPINFO			si;
	PROCESS_INFORMATION	pi;

	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);

	if (sDir[0])
	{
		int nLen = strlen(sDir);
		if (nLen <= 0 || sDir[nLen - 1] == '\\')
		{
			wsprintf(sCmd, "%s%s", sDir, sFile);
		}
		else
		{
			wsprintf(sCmd, "%s\\%s", sDir, sFile);
		}
	}
	else
	{
		wsprintf(sCmd, "%s", sFile);
	}

	if (!sDir[0])
	{
		sDir = NULL;
	}

	return(CreateProcess(NULL, sCmd, NULL, NULL, FALSE, 0, NULL, sDir, &si, &pi));
}


// *********************************************************************** //
//
//	ROUTINE:	RunSetup   [0x404b00]
//
// *********************************************************************** //

BOOL RunSetup()
{
	if (!ExistFileInDir(g_sCdRomPath, g_csSetupExe))
	{
		return(FALSE);
	}

	return(RunProgram(g_sCdRomPath, g_csSetupExe));
}


// EOF...
