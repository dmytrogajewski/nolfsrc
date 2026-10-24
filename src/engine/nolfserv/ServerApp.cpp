// *********************************************************************** //
//
//	MODULE:		ServerApp (.CPP)
//
//	PURPOSE:	The application object of NolfServ.exe, the stand-alone
//				server: an MFC application whose one window is the server
//				dialog (ServerDlg.cpp), run modally from Run().
//
//				Retail NolfServ.exe: 0x403830 - 0x40398f. The layout is
//				AppWizard's dialog application's, as AvP2's AVP2Serv (a
//				descendant of this server, in Monolith's AvP2 source
//				release) still has it.
//
// *********************************************************************** //

#include "stdafx.h"
#include "ServerApp.h"
#include "ServerDlg.h"
#include "Resource.h"
#ifdef NOLFSRC_DEBUG
#include "nolfsrc_crash.h"     // debug build only: crash backtraces (src/nolfsrc/)
#endif
#include "nolfserv/p6_server.h"	// M-P6-11 (src/modern/nolfserv/)

// Globals...

HINSTANCE		g_hInst = NULL;			// 0x4597b8


/////////////////////////////////////////////////////////////////////////////
// CServerApp

BEGIN_MESSAGE_MAP(CServerApp, CWinApp)
	//{{AFX_MSG_MAP(CServerApp)
		// NOTE - the ClassWizard will add and remove mapping macros here.
		//    DO NOT EDIT what you see in these blocks of generated code!
	//}}AFX_MSG
	ON_COMMAND(ID_HELP, CWinApp::OnHelp)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CServerApp construction  [0x403840]

CServerApp::CServerApp(LPCTSTR lpszAppName) : CWinApp(lpszAppName)
{
	// Place all significant initialization in InitInstance
}

/////////////////////////////////////////////////////////////////////////////
// The one and only CServerApp object (0x4596f8). Its name is the
// registry key MFC keeps the application's profile under.

CServerApp theApp("NolfServer");

// [0x4038d0]
CServerApp* GetTheApp()
{
	return(&theApp);
}


/////////////////////////////////////////////////////////////////////////////
// CServerApp initialization  [0x4038e0]

BOOL CServerApp::InitInstance()
{
#ifdef NOLFSRC_DEBUG
	// a backtrace in nolfsrc.log for any crash in the process: this program,
	// server.dll, the game's server DLL
	NOLFSRC_INSTALL_CRASH_HANDLER();
#endif
	// M-P6-11: the server logs to modern-server.log
	P6_InitServerLog();
	AfxEnableControlContainer();

	// Standard initialization

#ifdef _AFXDLL
	Enable3dControls();			// Call this when using MFC in a shared DLL
#else
	Enable3dControlsStatic();	// Call this when linking to MFC statically
#endif

	// Get our instance handle...
	g_hInst = AfxGetInstanceHandle();

	return TRUE;
}

// [0x403910]: the server dialog, modal, is the whole application.

int CServerApp::Run()
{
	CServerDlg dlg;
	m_pMainWnd = &dlg;

	int nResponse = dlg.DoModal();

	// Since the dialog has been closed, return FALSE so that we exit the
	//  application, rather than start the application's message pump.

	return nResponse;
}
