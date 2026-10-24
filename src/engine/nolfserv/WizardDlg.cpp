// *********************************************************************** //
//
//	MODULE:		WizardDlg (.CPP)
//
//	PURPOSE:	The server setup wizard: a property sheet in wizard mode
//				holding the six pages -- customization (only when the
//				Custom folder has a .rez file), session, options, the
//				deathmatch or H.A.R.M. vs UNITY options, and the worlds.
//				The server dialog runs it before it starts the server.
//
//				Retail NolfServ.exe: 0x409860 - 0x409b3f.
//
// *********************************************************************** //

#include "stdafx.h"
#include <io.h>
#include <direct.h>
#include "WizardDlg.h"


// Globals...

bool	g_bCustomRez = false;		// 0x4597c4


/////////////////////////////////////////////////////////////////////////////
// CWizardDlg

IMPLEMENT_DYNAMIC(CWizardDlg, CPropertySheet)

// [0x409870]

CWizardDlg::CWizardDlg() : CPropertySheet(IDS_WIZARD_TITLE)
{
	m_psh.dwFlags &= ~PSH_HASHELP;

	// Anything in the Custom folder?

	if (_chdir("Custom") == 0)
	{
		struct _finddata_t fileinfo;
		// the search is closed again (retail leaves its handle open)
		long hFind = _findfirst("*.rez", &fileinfo);
		if (hFind != -1)
		{
			g_bCustomRez = true;
			_findclose(hFind);
		}

		_chdir("..");
	}

	if (g_bCustomRez)
	{
		AddPage(&m_CustomDlg);
	}

	AddPage(&m_SessionDlg);
	AddPage(&m_OptionsDlg);
	AddPage(&m_DeathmatchDlg);
	AddPage(&m_AssaultDlg);
	AddPage(&m_WorldsDlg);

	m_psh.dwFlags |= PSH_WIZARD;
}

// [0x409a00]

CWizardDlg::~CWizardDlg()
{
}

// [0x409ab0]: Next, or Finish on the last page; Back past the first page
// (the customization page, when there is one, cannot be gone back to).

void CWizardDlg::SetButtons()
{
	int nLast = GetPageCount() - 1;
	DWORD dwButtons = (GetActiveIndex() < nLast) ? PSWIZB_NEXT : PSWIZB_FINISH;

	BOOL bBack;
	if (g_bCustomRez)
	{
		bBack = GetActiveIndex() > 1;
	}
	else
	{
		bBack = GetActiveIndex() > 0;
	}

	SetWizardButtons(bBack | dwButtons);
}


BEGIN_MESSAGE_MAP(CWizardDlg, CPropertySheet)
	//{{AFX_MSG_MAP(CWizardDlg)
		// NOTE - the ClassWizard will add and remove mapping macros here.
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CWizardDlg message handlers

// [0x409b30]

BOOL CWizardDlg::OnInitDialog()
{
	CPropertySheet::OnInitDialog();

	return TRUE;
}
