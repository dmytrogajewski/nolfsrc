// *********************************************************************** //
//
//	MODULE:		ServerDlg (.CPP)
//
//	PURPOSE:	The server's one window: the dialog that loads server.dll,
//				runs the setup wizard (WizardDlg.cpp) unless the command
//				line says not to, hosts the game and updates it from a
//				thread of its own, and shows the players, the levels and
//				the console.
//
//				Retail NolfServ.exe: 0x403990 - 0x408d1f.
//
//				AvP2's AVP2Serv (Monolith's AvP2 source release) is a
//				descendant of this file: most of its functions are the
//				same code, which is where the names come from. NOLF's
//				differs in the settings (NetHost.txt, kept in a CButeMgr,
//				and edited by the wizard), the server options
//				(Attributes\ServerOptions.txt), CRes.dll for the game's
//				strings, and the command line.
//
// *********************************************************************** //

#include "stdafx.h"
#include "ServerApp.h"
#include "ServerDlg.h"
#include "WizardDlg.h"
#include <stdio.h>
#include <process.h>
#include "mmsystem.h"
#include "nolfserv/p6_server.h"	// M-P6-11, M-P6-12, M-P6-13 (src/modern/nolfserv/)
#ifdef NOLFSRC_DEBUG
#include "nolfsrc_debug.h"
#endif

#define MIN_SERVER_UPDATE 33 // At least 33 ms between server updates = cap at 30fps

#define DEFAULT_PORT	27888

#define SETTINGS_TAG	"Host"
#define SETTINGS_FILE	"NetHost.txt"

enum
{
	// Maximum number of rez files to use when launching game.
	eMaxRezFiles = 32
};

// Column identifiers for player list control.
enum PlayerColumns
{
	ePlayerColumnsName,
	ePlayerColumnsPing,
	ePlayerColumnsKills,
	ePlayerColumnsTime,
};


CServerDlg*			g_pDialog = NULL;		// 0x4597bc


// [0x4039a0]
LTRESULT CGameServerAppHandler::ShellMessageFn( char* pInfo, uint32 nLen )
{
	if (g_pDialog)
	{
		g_pDialog->OnShellMessage( pInfo, nLen );
	}

	return(LT_OK);
}

// [0x4039c0]
LTRESULT CGameServerAppHandler::ConsoleOutputFn(char* pInfo)
{
	if (g_pDialog)
	{
		g_pDialog->OnShellConsoleOutput(pInfo);
	}

	return(LT_OK);
}

// [0x4039e0]
LTRESULT CGameServerAppHandler::OutOfMemory()
{
	if (g_pDialog)
	{
		g_pDialog->OnShellFatalError( IDS_ERROR_MEMORY );
	}

	return(LT_OK);
}

// [0x403a00]
LTRESULT CGameServerAppHandler::ProcessPacket(char *pData, uint32 dataLen, uint8 senderAddr[4], uint16 senderPort)
{
	if(g_pDialog)
	{
		g_pDialog->OnShellProcessNetPacket(pData, dataLen, senderAddr, senderPort);
	}

	return LT_OK;
}


/////////////////////////////////////////////////////////////////////////////
// CServerDlg dialog

// [0x403a30]
CServerDlg::CServerDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CServerDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CServerDlg)
	m_sServerName = _T("");
	m_sServerTime = _T("");
	m_sTimeInLevel = _T("");
	m_nPeakPlayers = 0;
	m_nTotalPlayers = 0;
	m_nAveragePing = 0;
	m_nGamePlayers = 0;
	//}}AFX_DATA_INIT

	m_bCSInitted = FALSE;
	memset(&m_CS, 0, sizeof(m_CS));

	m_nCurLevel_Shell = -1;

	m_hThread = NULL;
	m_bConfirmExit = TRUE;
	m_pServerMgr = NULL;
	m_hResModule = NULL;

	m_bFirstShow = TRUE;
	g_pDialog = this;

	m_hStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
}


// [0x403e90]
CServerDlg::~CServerDlg()
{
	StopServer();
	CloseHandle(m_hStopEvent);
}

// [0x4040e0]
void CServerDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CServerDlg)
	DDX_Control(pDX, IDC_PLAYER_BOOT, m_PlayerBoot);
	DDX_Control(pDX, IDC_PLAYERS, m_Players);
	DDX_Control(pDX, IDC_LEVELS, m_Levels);
	DDX_Control(pDX, IDC_CONSOLE_WINDOW, m_edConsole);
	DDX_Text(pDX, IDC_SERVER_NAME, m_sServerName);
	DDX_Text(pDX, IDC_SERVER_TIME, m_sServerTime);
	DDX_Text(pDX, IDC_TIMEINLEVEL, m_sTimeInLevel);
	DDX_Text(pDX, IDC_PEAKPLAYERS, m_nPeakPlayers);
	DDX_Text(pDX, IDC_TOTALPLAYERS, m_nTotalPlayers);
	DDX_Text(pDX, IDC_AVERAGEPING, m_nAveragePing);
	DDX_Text(pDX, IDC_GAME_PLAYERS, m_nGamePlayers);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CServerDlg, CDialog)
	//{{AFX_MSG_MAP(CServerDlg)
	ON_BN_CLICKED(IDC_CONSOLE_SEND, OnConsoleSend)
	ON_BN_CLICKED(IDC_CONSOLE_CLEAR, OnConsoleClear)
	ON_BN_CLICKED(IDC_COMMANDS_NEXTLEVEL, OnCommandsNextLevel)
	ON_BN_CLICKED(IDC_COMMANDS_SELLEVEL, OnCommandsSelectLevel)
	ON_BN_CLICKED(IDC_PLAYER_BOOT, OnPlayersBoot)
	ON_WM_DESTROY()
	ON_WM_TIMER()
	ON_NOTIFY(NM_DBLCLK, IDC_LEVELS, OnDblclkLevels)
	ON_WM_SHOWWINDOW()
	ON_WM_CLOSE()
	ON_BN_CLICKED(IDC_STOPSERVER, OnStopserver)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

// [0x4041c0]
BOOL CServerDlg::LoadServer()
{
	CWaitCursor wc;
	CString sWorlds, sWorldsKey, sWorld;
	SI_CREATESTATUS status;

	// Start fresh.
	StopServer( );

	WriteConsoleString(IDS_CONSOLE_INITSERVER);

	// Initialzie our critical section for the server thread.
	if( !m_bCSInitted )
	{
		InitializeCriticalSection(&m_CS);
		m_bCSInitted = TRUE;
	}

	// Create the server.
	status = CreateServer( SI_VERSION, NOLFGUID, &m_pServerMgr );
	if( status != 0 || !m_pServerMgr )
	{
		AfxMessageBox( IDS_NETERR_GENERIC );
		return(FALSE);
	}

	// Set our app handler.
	if( m_pServerMgr->SetAppHandler( &m_AppHandler ) != LT_OK )
	{
		AfxMessageBox( IDS_NETERR_GENERIC );
		StopServer( );
		return FALSE;
	}

	// Add in the resources.  (Three tests, each with the same failure:
	// VC6 keeps one copy, the last, and the first two jump to it.)
	if( !AddResources( ))
	{
		AfxMessageBox( IDS_ERROR_LOADREZ );
		StopServer( );
		return FALSE;
	}

	// The game's strings.
	if( !LoadResourceModule( ))
	{
		AfxMessageBox( IDS_ERROR_LOADREZ );
		StopServer( );
		return FALSE;
	}

	// The server options.
	if( !LoadServerOptions( ))
	{
		AfxMessageBox( IDS_ERROR_LOADREZ );
		StopServer( );
		return FALSE;
	}

	// Clear some shell variables.
	m_lstShellMsgs_Shell.RemoveAll( );
	m_nCurLevel_Shell = -1;
	m_bStopServer_Shell = FALSE;
	m_nCurLevel = -1;
	m_bPlayerUpdate_Shell = FALSE;
	m_bChangingLevels_Shell = FALSE;
	m_bChangedLevel_Shell = FALSE;

	WriteConsoleString( IDS_CONSOLE_SERVERINITED );

	return(TRUE);
}

// [0x4043b0]
BOOL CServerDlg::RunServer()
{
	CWaitCursor wc;

	if( !m_pServerMgr )
		return FALSE;

	// Set our connection service.
	if( !SetService( ))
	{
		AfxMessageBox( IDS_NETERR_SETNETSERVICE );
		StopServer( );
		return FALSE;
	}

	// Host the game.
	if( !DoHost( ))
	{
		AfxMessageBox( IDS_CONSOLE_UNABLETOHOST );
		StopServer( );
		return FALSE;
	}

	// Update the full ip address
	char sAddress[128];
	unsigned short nPort;
	m_pServerMgr->GetTcpIpAddress( sAddress, 128, nPort );

	m_sFullTcpIpAddress = sAddress;
	if( nPort > 0 && nPort != DEFAULT_PORT )
	{
		char sTemp[32];
		wsprintf( sTemp, ":%i", nPort );
		m_sFullTcpIpAddress += sTemp;
	}

	// Add in the servershell
	if( !m_pServerMgr->LoadBinaries( ))
	{
		AfxMessageBox( IDS_ERROR_BINARIES );
		StopServer( );
		return FALSE;
	}

	// Make sure the stop event is reset
	ResetEvent(m_hStopEvent);

	// Create the thread to do our updating.
	m_hThread = ( HANDLE )_beginthread( ThreadUpdate, 0, this );
	if(( int )m_hThread == -1 )
	{
		StopServer( );
		return FALSE;
	}

	// Give us a timer update every 100 ms.
	SetTimer( 0, 100, NULL );

	// Init some stuff now that we're running...
	m_serverStartTime = CTime::GetCurrentTime( );

	// Tell server shell to be initialized.
	char szBuf[32];
	ostrstream memStream( szBuf, sizeof( szBuf ));
	memStream << (( BYTE )SERVERSHELL_INIT ) << endl;

	m_pServerMgr->SendToServerShell(szBuf, sizeof(szBuf));

	// Start with the first level.
	SelectLevel( 0 );

	// All done...
	WriteConsoleString(IDS_CONSOLE_SERVERRUNNING);

	return(TRUE);
}

// [0x404680]
BOOL CServerDlg::StopServer( )
{
	CPlayerInfo *pPlayerInfo;
	WORD nKey;
	POSITION pos;

	// Kill the thread.
	if( m_hThread )
	{
		SetEvent(m_hStopEvent);

		DWORD dwRet = WaitForSingleObject( m_hThread, 5000 );
		if( dwRet == WAIT_TIMEOUT)
		{
			// Kill thread forcefully.
			TerminateThread( m_hThread, 0 );
			CloseHandle( m_hThread );
		}

		// Reset the stop event since it's successfully stopped
		ResetEvent(m_hStopEvent);

		m_hThread = NULL;

		WriteConsoleString( IDS_CONSOLE_SERVERSTOPPED );
	}

	// Free the game's strings.
	if( m_hResModule )
	{
		FreeLibrary( m_hResModule );
		m_hResModule = NULL;
	}

	if( !m_sResModuleFile.IsEmpty( ))
	{
		DeleteFile( m_sResModuleFile );
		m_sResModuleFile.Empty( );
	}

	// Delete the server interface.
	if( m_pServerMgr )
	{
		m_pServerMgr->SetAppHandler( NULL );
		DeleteServer();
		m_pServerMgr = NULL;
	}

	// Clear out variables.
	m_lstShellMsgs_Shell.RemoveAll( );
	m_nCurLevel_Shell = 0;
	m_bStopServer_Shell = FALSE;

	// Clear out the player info list.
	pos = m_mapPlayerInfo_Shell.GetStartPosition( );
	while( pos )
	{
		m_mapPlayerInfo_Shell.GetNextAssoc( pos, nKey, ( void *& )pPlayerInfo );
		delete pPlayerInfo;
	}
	m_mapPlayerInfo_Shell.RemoveAll( );

	// Clear out the server options.
	pos = m_lstServerOptions.GetHeadPosition( );
	while( pos )
	{
		delete m_lstServerOptions.GetNext( pos );
	}
	m_lstServerOptions.RemoveAll( );

	// Get rid of critical section.
	if( m_bCSInitted )
	{
		DeleteCriticalSection(&m_CS);
		m_bCSInitted = FALSE;
	}

	return TRUE;
}


// [0x404810]
BOOL CServerDlg::SetService( )
{
	NetService *pCur, *pListHead;
	HNETSERVICE hNetService;

	// Make sure we have server interface.
	if( !m_pServerMgr )
		return FALSE;

	pCur      = NULL;
	pListHead = NULL;
	hNetService = NULL;

	// Initialize the networking.
	if( !m_pServerMgr->InitNetworking( NULL, 0 ))
		return FALSE;

	// Get the comm services the server is supporting.
	if( !m_pServerMgr->GetServiceList( pListHead ) || !pListHead )
		return FALSE;

	// Find the TCPIP service.
	pCur = pListHead;
	while( pCur )
	{
		if( pCur->m_dwFlags & NETSERVICE_TCPIP )
		{
			hNetService = pCur->m_handle;
			break;
		}

		pCur = pCur->m_pNext;
	}

	// Free the service list.
	m_pServerMgr->FreeServiceList( pListHead );

	// Check if tcp not found.
	if( !hNetService )
		return FALSE;

	// Select it.
	if( !m_pServerMgr->SelectService( hNetService ))
		return FALSE;

	return TRUE;
}

// [0x4048a0]: the game's settings, for the server shell, and the host's.

BOOL CServerDlg::DoHost( )
{
	CWaitCursor wc;
	NetHost netHost;
	NetGame netGame;
	CString sLevelFmt, sLevelKey, sLevel;

	// Make sure we have server interface.
	if( !m_pServerMgr )
		return FALSE;

	memset( &netGame, 0, sizeof( netGame ));

	// The server options, in ServerOptions.txt's order.
	int nOption = 0;
	POSITION pos = m_lstServerOptions.GetHeadPosition( );
	while( pos )
	{
		ServerOption* pOption = m_lstServerOptions.GetNext( pos );
		if( !pOption )
			return FALSE;
		// NetGame has room for MAX_GAME_OPTIONS (a ServerOptions.txt with
		// more wrote past it, over the levels)
		if( nOption >= MAX_GAME_OPTIONS )
			break;

		netGame.m_fOptions[nOption++] = GetServerSettingFloat( pOption->szVariable );
	}
	netGame.m_byNumOptions = ( uint8 )nOption;

	// The levels of the game type.
	int nLevels;
	switch(( int )g_pDialog->GetSettingFloat( "NetGameType", 2.0f ))
	{
		case COOPERATIVE_ASSAULT:
			sLevelFmt = "NetCALevel%i";
			nLevels = ( int )GetSettingFloat( "NetCANumLevels", 0.0f );
			break;

		case DEATHMATCH:
			sLevelFmt = "NetLevel%i";
			nLevels = ( int )GetSettingFloat( "NetNumLevels", 0.0f );
			break;

		default:
			return FALSE;
	}

	if( !nLevels )
		return FALSE;

	netGame.m_byNumLevels = ( uint8 )nLevels;
	for( int nLevel = 0; nLevel < nLevels; nLevel++ )
	{
		sLevelKey.Format( sLevelFmt, nLevel );
		sLevel = GetSettingString( sLevelKey, "" );
		strncpy( netGame.m_sLevels[nLevel], sLevel, NML_LEVEL );
	}

	CString sSession;
	sSession.LoadString( IDS_GOODVSEVIL );
	sSession = GetSettingString( "NetSessionName", sSession );
	strncpy( netGame.m_sSession, sSession, MAX_SESSION_NAME );
	netGame.m_sSession[MAX_SESSION_NAME - 1] = '\0';

	netGame.m_byType = ( uint8 )( int )GetServerSettingFloat( "NetGameType" );
	netGame.m_bUsePassword = ( int )GetSettingFloat( "NetUsePassword", 0.0f );

	CString sPassword;
	sPassword = GetSettingString( "NetPassword", "password" );
	strncpy( netGame.m_sPassword, sPassword, MAX_PASSWORD );
	netGame.m_sPassword[MAX_PASSWORD - 1] = '\0';

	// Tell server about the game info.
	m_pServerMgr->SetGameInfo( &netGame, sizeof( netGame ));

	// Setup the host info.
	netHost.m_Port = ( int )GetSettingFloat( "NetPort", ( float )DEFAULT_PORT );
	netHost.m_dwMaxPlayers = ( int )GetServerSettingFloat( "NetMaxPlayers" );
	strncpy( netHost.m_sName, m_sServerName, sizeof( netHost.m_sName ));
	netHost.m_sName[sizeof( netHost.m_sName ) - 1] = '\0';

	// Start hosting.
	if( !m_pServerMgr->HostGame( netHost ))
		return FALSE;

	return TRUE;
}


// [0x404cc0]. Callers pass text from outside as the format -- the shell's
// console messages (UpdateUI) and the console command typed (OnConsoleSend)
// -- so a '%' in them is read as a conversion; and a message of 499
// characters or more is left without its terminating zero by _vsnprintf
// (RETAIL_BUGS.md).
void CServerDlg::WriteConsoleString(LPCTSTR pMsg, ...)
{
	TCHAR		str[500];
	va_list		marker;
	int			nLen;

	static int nMax = 250;

	if(m_edConsole.GetLineCount() > nMax)
	{
		// Nuke the oldest 75%.
		int iLine = (m_edConsole.GetLineCount()*75) / 100;
		int iChar = m_edConsole.LineIndex(iLine);

		m_edConsole.SetRedraw(FALSE);
		m_edConsole.SetSel(0, iChar);
		m_edConsole.ReplaceSel("", FALSE);
		m_edConsole.SetRedraw(TRUE);
	}

	va_start(marker, pMsg);
	_vsnprintf( str, sizeof( str ) - 1, pMsg, marker );
	va_end(marker);
	// a line of 499 characters or more comes back unterminated
	str[sizeof( str ) - 3] = '\0';	// and room for the "\r\n" below

	// M-P6-11: the console, in the modern log too
	P6_LogConsole( str );
#ifdef NOLFSRC_DEBUG
	NOLFSRC_LOG(( "console: %s", str ));
#endif

	int len = strlen(str);
	if (len > 0)
	{
		if (len > 0 && str[len-1] < 32) str[len-1] = '\0';

		strcat(str, "\r\n");

		nLen = m_edConsole.SendMessage(EM_GETLIMITTEXT, 0, 0);
		m_edConsole.SetSel(nLen, nLen);
		m_edConsole.ReplaceSel(str);
	}
}

// [0x404e60]
void CServerDlg::WriteConsoleString(int nStringID)
{
	CString sMsg;

	if( !sMsg.LoadString( nStringID ))
		return;

	WriteConsoleString( sMsg );
}

/////////////////////////////////////////////////////////////////////////////
// CServerDlg message handlers

// [0x404ed0]
BOOL CServerDlg::OnInitDialog()
{
	CTimeSpan timeSpan( 0 );

	CDialog::OnInitDialog();

	// The command line, in upper case -- and then not looked at again
	// (ParseCommandLine reads __argv instead).
	CString sCmdLine( GetTheApp( )->m_lpCmdLine );
	sCmdLine.MakeUpper( );

	WriteConsoleString( IDS_CONSOLE_SERVERRUNNING );

	// Set the server up time.
	m_sServerTime = FormatTime( timeSpan );

	// Init the game progress.
	m_nGamePlayers = 0;

	m_sTimeInLevel = FormatTime( timeSpan );

	// Setup player list.
	CString sColumnHeader;
	sColumnHeader.LoadString( IDS_COLUMNHEADER_NAME );
	m_Players.InsertColumn( ePlayerColumnsName, sColumnHeader );
	sColumnHeader.LoadString( IDS_COLUMNHEADER_PING );
	m_Players.InsertColumn( ePlayerColumnsPing, sColumnHeader );
	sColumnHeader.LoadString( IDS_COLUMNHEADER_KILLS );
	m_Players.InsertColumn( ePlayerColumnsKills, sColumnHeader );
	sColumnHeader.LoadString( IDS_COLUMNHEADER_TIME );
	m_Players.InsertColumn( ePlayerColumnsTime, sColumnHeader );

	CRect rect;
	m_Players.GetWindowRect( &rect );

	m_Players.SetColumnWidth( ePlayerColumnsName,	(int)(rect.Width() * 0.4));
	m_Players.SetColumnWidth( ePlayerColumnsPing,	(int)(rect.Width() * 0.2));
	m_Players.SetColumnWidth( ePlayerColumnsKills,	(int)(rect.Width() * 0.15));
	m_Players.SetColumnWidth( ePlayerColumnsTime,	LVSCW_AUTOSIZE_USEHEADER);

	// Disable the boot button.
	m_PlayerBoot.EnableWindow( FALSE );

	// Setup the world listctrl.
	CImageList* pImageList = new CImageList;
	pImageList->Create( 16, 16, TRUE, 1, 1 );
	pImageList->Add( AfxGetApp( )->LoadIcon( IDI_SELECTED ));
	m_Levels.SetImageList( pImageList, LVSIL_STATE );

	sColumnHeader.LoadString( IDS_COLUMNHEADER_WORLD );
	m_Levels.InsertColumn( 0, sColumnHeader );
	m_Levels.SetColumnWidth( 0, LVSCW_AUTOSIZE_USEHEADER );

	// Set the icon.
	HICON hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
	SetIcon(hIcon, TRUE);
	SetIcon(hIcon, FALSE);

	// All done...
	return(TRUE);
}

// [0x405220]: called by the free running thread.
void CServerDlg::ServerUpdate()
{
	// Make sure the server is still running.
	if( !m_pServerMgr || m_bStopServer_Shell )
		return;

	// Update everything if we're running...
	if (IsRunning())
	{
		// Update the server manager...
		if ( !m_pServerMgr->Update(0))
		{
			char szError[256];

			// Something went wrong.  Put the error on the console.
			m_pServerMgr->GetErrorString( szError, sizeof( szError ) - 1 );
			m_lstShellMsgs_Shell.AddTail( CString( szError ));
			m_bStopServer_Shell = TRUE;
		}
	}
}

// [0x4052e0]
void CServerDlg::OnTimer(UINT nIDEvent)
{
	UpdateUI( );

	CDialog::OnTimer(nIDEvent);
}

// [0x405300]
void CServerDlg::UpdateUI()
{
	int nPlayer;
	int nCurLevel;
	CStringList lstShellMsgs;
	POSITION pos;
	BOOL bPlayerUpdate;
	BOOL bLevelChanged = FALSE;
	CString sVal;
	CTimeSpan serverRunTime, levelRunTime;
	CTimeSpan playerOnServerTime;
	int nCurSel, nPlayerId;
	WORD nKey;
	CArray< CPlayerInfo, CPlayerInfo & > playerInfo;
	CPlayerInfo *pShellPlayerInfo;

	// Make sure server is running.
	if( !IsRunning())
		return;

	// See if shell wants to stop itself.
	if( m_bStopServer_Shell )
	{
		StopServer( );
	}

	// Check if we're in the middle of changing levels.
	if( m_bChangingLevels_Shell )
		return;

	// Block the shell thread while we make some copies of its data.
	EnterCriticalSection( &m_CS );

		bPlayerUpdate = m_bPlayerUpdate_Shell;
		if( bPlayerUpdate )
			m_bPlayerUpdate_Shell = FALSE;
		nCurLevel = m_nCurLevel_Shell;
		lstShellMsgs.AddTail( &m_lstShellMsgs_Shell );
		m_lstShellMsgs_Shell.RemoveAll( );
		bLevelChanged = m_bChangedLevel_Shell;
		m_bChangedLevel_Shell = FALSE;

	// Unblock the shell thread.
	LeaveCriticalSection( &m_CS );

	// Update the server time.
	serverRunTime = CTime::GetCurrentTime( ) - m_serverStartTime;
	if( serverRunTime != m_serverRunTime )
	{
		// Set new time.
		m_serverRunTime = serverRunTime;

		// Choose format for time.
		m_sServerTime = FormatTime( m_serverRunTime );
	}

	// Check if we just changed levels.
	if( bLevelChanged )
	{
		m_levelStartTime = CTime::GetCurrentTime( );
		m_sTimeInLevel = FormatTime( CTimeSpan( 0 ));
	}

	// Update the level time.
	levelRunTime = CTime::GetCurrentTime( ) - m_levelStartTime;
	if( levelRunTime != m_levelRunTime )
	{
		m_levelRunTime = levelRunTime;

		// Choose format for time.
		m_sTimeInLevel = FormatTime( m_levelRunTime );
	}

	// Get currently selected player id.
	nCurSel = m_Players.GetNextItem( -1, LVNI_SELECTED );
	if( nCurSel != -1 )
		nPlayerId = m_Players.GetItemData( nCurSel );
	else
		nPlayerId = -1;

	// Block the server shell thread while we copy the playerinfo data.
	EnterCriticalSection( &m_CS );

		// Loop over each player in shell's list.
		pos = m_mapPlayerInfo_Shell.GetStartPosition( );
		while( pos )
		{
			// Get the next player info from the shell list.
			m_mapPlayerInfo_Shell.GetNextAssoc( pos, nKey, ( void *& )pShellPlayerInfo );

			if( !pShellPlayerInfo )
			{
				continue;
			}

			// Make a local copy.
			playerInfo.Add( *pShellPlayerInfo );
		}

	// Unblock the server shell thread.
	LeaveCriticalSection( &m_CS );

	// Make sure there are the right number of items in the list control.
	while( m_Players.GetItemCount( ) < playerInfo.GetSize( ))
	{
		if( m_Players.InsertItem( m_Players.GetItemCount( ), "" ) == -1 )
			break;
	}
	while( m_Players.GetItemCount( ) > playerInfo.GetSize( ))
	{
		if( m_Players.DeleteItem( m_Players.GetItemCount( ) - 1 ) == -1 )
			break;
	}

	// Clear out the average ping value so we can add to it as we
	// loop over the players.
	m_nAveragePing = 0;

	// Loop over each player in the playerinfo array.  Set the listcontrol
	// items in a index to index mapping.
	for( nPlayer = 0; nPlayer < m_Players.GetItemCount( ); nPlayer++ )
	{
		// Update any shell information for this slot.
		if( bPlayerUpdate )
		{
			// Set the user data for the list control.
			m_Players.SetItemData( nPlayer, playerInfo[nPlayer].m_nClientId );

			// If this is the player we were pointing at before, update our
			// selection.
			if( playerInfo[nPlayer].m_nClientId == nPlayerId )
			{
				m_Players.SetSelectionMark( nPlayer );
			}

			// Update kills.
			sVal.Format( "%d", playerInfo[nPlayer].m_nFrags );
			if( m_Players.GetItemText( nPlayer, ePlayerColumnsKills ).Compare( sVal ))
			{
				m_Players.SetItemText( nPlayer, ePlayerColumnsKills, sVal );
			}

			// Update the player name.
			sVal = playerInfo[nPlayer].m_szPlayerHandle;
			if( m_Players.GetItemText( nPlayer, ePlayerColumnsName ).Compare( sVal ))
			{
				m_Players.SetItemText( nPlayer, ePlayerColumnsName, sVal );
			}
		}

		// Always update the ping regardless of shell update.
		float fPing = 0.0f;
		DWORD nPing;
		m_pServerMgr->GetClientPing( m_Players.GetItemData( nPlayer ), fPing );
		nPing = ( DWORD )( fPing * 1000.0f );
		m_nAveragePing += nPing;
		sVal.Format( "%d", nPing );
		if( m_Players.GetItemText( nPlayer, ePlayerColumnsPing ).Compare( sVal ))
		{
			m_Players.SetItemText( nPlayer, ePlayerColumnsPing, sVal );
		}

		// Update the player time.
		playerOnServerTime = CTime::GetCurrentTime( ) - playerInfo[nPlayer].m_timeOnServer;
		sVal = FormatTime( playerOnServerTime );
		if( m_Players.GetItemText( nPlayer, ePlayerColumnsTime ).Compare( sVal ))
		{
			m_Players.SetItemText( nPlayer, ePlayerColumnsTime, sVal );
		}
	}

	// Set the number of players.
	if( m_Players.GetItemCount( ) != ( int )m_nGamePlayers )
	{
		m_nGamePlayers = m_Players.GetItemCount( );

		// Check if this is the most players we've had at one time.
		if( m_nGamePlayers > m_nPeakPlayers )
		{
			m_nPeakPlayers = m_nGamePlayers;
		}
	}

	// Finish calculation of average ping.
	if( m_nGamePlayers > 0 )
	{
		m_nAveragePing = ( DWORD )(( float )m_nAveragePing / m_nGamePlayers + 0.5f );
	}

	// The boot button only while there is someone to boot.
	m_PlayerBoot.EnableWindow( m_Players.GetItemCount( ) > 0 );

	// Change selected level.
	if( nCurLevel != m_nCurLevel )
	{
		// Deselect previous level.
		if( 0 <= m_nCurLevel && m_nCurLevel < m_Levels.GetItemCount( ))
		{
			m_Levels.SetItemState( m_nCurLevel, 0, LVIS_STATEIMAGEMASK );
			m_Levels.RedrawItems( m_nCurLevel, m_nCurLevel );
			m_nCurLevel = -1;
		}

		// Update the current level.
		if( 0 <= nCurLevel && nCurLevel < m_Levels.GetItemCount( ))
		{
			m_Levels.SetItemState( nCurLevel, INDEXTOSTATEIMAGEMASK( 1 ), LVIS_STATEIMAGEMASK );
			m_Levels.RedrawItems( nCurLevel, nCurLevel );

			m_nCurLevel = nCurLevel;
		}
	}

	// Update any console strings from the shell.
	pos = lstShellMsgs.GetHeadPosition( );
	while( pos )
	{
		// the shell's console lines are text: a '%' in one (a player's
		// name, say) is not a conversion
		WriteConsoleString( "%s", ( LPCTSTR )lstShellMsgs.GetNext( pos ));
	}

	// Send data to dialog.
	UpdateData( FALSE );
}


// [0x405c20]
void CServerDlg::OnShellMessage( char *pMsg, uint32 nLen )
{
	// Check inputs.
	if( !pMsg )
		return;

	// Put input message into a stream.
	istrstream msg( pMsg, nLen );

	BYTE nMsgId;
	msg >> nMsgId;
	switch( nMsgId )
	{
		case SERVERAPP_ADDCLIENT:
			OnShellAddClient( msg );
			break;
		case SERVERAPP_REMOVECLIENT:
			OnShellRemoveClient( msg );
			break;
		case SERVERAPP_SHELLUPDATE:
			OnShellUpdate( msg );
			break;
		case SERVERAPP_PRELOADWORLD:
			OnShellPreLoadWorld( msg );
			break;
		case SERVERAPP_POSTLOADWORLD:
			OnShellPostLoadWorld( msg );
			break;
		case SERVERAPP_CONSOLEMESSAGE:
		{
			// (Reads one word, unbounded: the message is cut at its first
			// space, and a longer word overruns the buffer.)
			char szMsg[1024];

			// the rest of the message, at most the buffer
			msg >> ws;
			msg.getline( szMsg, sizeof( szMsg ));
			m_lstShellMsgs_Shell.AddTail( szMsg );
			break;
		}
	}
}

// [0x405d60]
void CServerDlg::OnShellAddClient( istrstream &msg )
{
	CPlayerInfo *pPlayerInfo;
	int nClientId = 0;
	WORD nKey;
	CString sVal;

	// Get the client id for the player.
	msg >> nClientId;

	// Make a key.
	nKey = ( WORD )nClientId;

	// Remove any old players that might be in the player list.
	if( m_mapPlayerInfo_Shell.Lookup( nKey, ( void *& )pPlayerInfo ))
	{
		m_mapPlayerInfo_Shell.RemoveKey( nKey );
		delete pPlayerInfo;
		pPlayerInfo = NULL;
	}

	// Create a new player.
	pPlayerInfo = new CPlayerInfo;
	if( !pPlayerInfo )
		return;

	// Read in the player info.
	pPlayerInfo->m_nClientId = nClientId;

	// Load the temporary player name.
	sVal.LoadString( IDS_NEWPLAYER );
	strcpy( pPlayerInfo->m_szPlayerHandle, sVal );

	// Set our current time.
	pPlayerInfo->m_timeOnServer = CTime::GetCurrentTime( );

	// Put the player in the list.
	m_mapPlayerInfo_Shell.SetAt( nKey, pPlayerInfo );

	// Signal to updateui that the player info changed.
	m_bPlayerUpdate_Shell = TRUE;

	// Add this player to the total number of visitors.
	m_nTotalPlayers++;
}

// [0x405ec0]
void CServerDlg::OnShellRemoveClient( istrstream &msg )
{
	int nClientId = 0;
	CPlayerInfo *pPlayerInfo;
	WORD nKey;

	// Get the client id for the player.
	msg >> nClientId;

	// Make a key.
	nKey = ( WORD )nClientId;

	// Remove the player from the list.
	if( m_mapPlayerInfo_Shell.Lookup( nKey, ( void *& )pPlayerInfo ))
	{
		m_mapPlayerInfo_Shell.RemoveKey( nKey );
		delete pPlayerInfo;
	}

	// Signal to updateui that the player info changed.
	m_bPlayerUpdate_Shell = TRUE;
}


// [0x405f20]: handler for update message sent by game servershell.
void CServerDlg::OnShellUpdate( istrstream &msg )
{
	int nClientId;
	CPlayerInfo *pPlayerInfo;
	WORD nKey;
	char dummy;

	// Signal to updateui that the player info changed.
	m_bPlayerUpdate_Shell = TRUE;

	// Keep reading player updates until invalid clientid given.
	while( 1 )
	{
		msg >> nClientId;
		if( nClientId == -1 )
			break;

		// Make a key.
		nKey = ( WORD )nClientId;

		// Lookup clientid in known clients.
		if( !m_mapPlayerInfo_Shell.Lookup( nKey, ( void *& )pPlayerInfo ))
		{
			// We don't have this client.  Something's skrewy.
			break;
		}

		// Update the playerinfo.
		msg.get( dummy );
		msg.getline( pPlayerInfo->m_szPlayerHandle, sizeof( pPlayerInfo->m_szPlayerHandle ));
		pPlayerInfo->m_szPlayerHandle[sizeof( pPlayerInfo->m_szPlayerHandle ) - 1] = 0;
		msg >> pPlayerInfo->m_nFrags;
	}
}

// [0x405ff0]
void CServerDlg::OnShellPreLoadWorld( istrstream &msg )
{
	// We're about to change levels.
	m_bChangingLevels_Shell = TRUE;
}

// [0x406000]
void CServerDlg::OnShellPostLoadWorld( istrstream &msg )
{
	// Level change is complete.
	m_bChangingLevels_Shell = FALSE;
	m_bChangedLevel_Shell = TRUE;
	msg >> m_nCurLevel_Shell;
}

// [0x406030]
void CServerDlg::OnShellConsoleOutput(char* pMsg)
{
	// Record shell messages for UI to grab.
	m_lstShellMsgs_Shell.AddTail( CString( pMsg ));
}

// [0x406090]
void CServerDlg::OnConsoleSend()
{
	if( !m_pServerMgr )
		return;

	// Get the string from control.
	char sCmd[128];
	sCmd[0] = '\0';
	if( GetDlgItemText( IDC_CONSOLE_COMMAND, sCmd, 120 ) == 0 )
		return;

	// Update the UI with string.
	WriteConsoleString( "%s", sCmd );	// text, not a format

	// Clear out the string from the control.
	SetDlgItemText(IDC_CONSOLE_COMMAND, "");

	// Run it in the server's console.
	EnterCriticalSection( &m_CS );
		m_pServerMgr->RunConsoleString( sCmd );
	LeaveCriticalSection( &m_CS );
}

// [0x406110]
void CServerDlg::OnConsoleClear()
{
	// Clear the console.
	SetDlgItemText(IDC_CONSOLE_WINDOW, "");
}

// [0x406120]
BOOL CServerDlg::DestroyWindow()
{
	// We're shutting down.
	StopServer( );
	return(CDialog::DestroyWindow());
}

// [0x406140]
void CServerDlg::OnShellFatalError( int nStringId )
{
	AfxMessageBox( nStringId );
	m_bConfirmExit = FALSE;
	SendMessage(WM_COMMAND, IDCANCEL);
	exit(0);
}

// [0x406180]
void CServerDlg::OnCommandsNextLevel()
{
	char sMsg[128];

	if( !m_pServerMgr )
		return;

	// Send next level message to server.
	ostrstream msg( sMsg, 128 );
	msg << (( BYTE )SERVERSHELL_NEXTWORLD ) << endl;
	EnterCriticalSection( &m_CS );
		m_pServerMgr->SendToServerShell( sMsg, sizeof( sMsg ));
	LeaveCriticalSection( &m_CS );
}


// [0x406250]
void CServerDlg::OnCommandsSelectLevel()
{
	int nWorldIndex;
	POSITION pos;

	if( !IsRunning( ))
		return;

	// Get the selected item.
	pos = m_Levels.GetFirstSelectedItemPosition( );
	if( !pos )
		return;

	// Go to next item.
	nWorldIndex = m_Levels.GetNextSelectedItem( pos );
	SelectLevel( nWorldIndex );
}


// [0x4062b0]
BOOL CServerDlg::SelectLevel( int nLevelIndex )
{
	char sMsg[128];

	// Check if we're running.
	if( !IsRunning( ))
		return FALSE;

	// Create a message.
	ostrstream msg( sMsg, 128 );
	msg << (( BYTE )SERVERSHELL_SETWORLD ) << endl;
	msg << (( DWORD )nLevelIndex ) << endl;

	// Send message to the server.
	EnterCriticalSection( &m_CS );
		m_pServerMgr->SendToServerShell( sMsg, sizeof( sMsg ));
	LeaveCriticalSection( &m_CS );

	return TRUE;
}


// [0x4063c0]
void CServerDlg::OnPlayersBoot()
{
	if( !m_pServerMgr )
		return;

	// Get the player selected.
	int nCurSel = m_Players.GetNextItem( -1, LVNI_SELECTED );
	if( nCurSel == -1 )
		return;

	// Get the player's id.
	DWORD dwID = m_Players.GetItemData( nCurSel );

	// Boot that player.
	EnterCriticalSection( &m_CS );
		m_pServerMgr->BootClient(dwID);
	LeaveCriticalSection( &m_CS );
}

// [0x406420]
void CServerDlg::ThreadUpdate( void *pParam )
{
	CServerDlg *pServerDlg;

	pServerDlg = ( CServerDlg * )pParam;

	uint32 nLastTime = timeGetTime();

	HANDLE hStopEvent = pServerDlg->m_hStopEvent;
	CRITICAL_SECTION *pCS = pServerDlg->GetCS();

	// Call the update until we are done...
	while(WaitForSingleObject(hStopEvent, 0) == WAIT_TIMEOUT)
	{
		EnterCriticalSection(pCS);
			pServerDlg->ServerUpdate();
		LeaveCriticalSection(pCS);

		// Restrict the server's framerate
		uint32 nCurTime = timeGetTime();
		uint32 nDelay = 0;
		if (((nLastTime + MIN_SERVER_UPDATE) > nCurTime) && (nCurTime > nLastTime))
			nDelay = (nLastTime + MIN_SERVER_UPDATE) - nCurTime;
		// If we're running too fast, slow it down..
		if (nDelay)
		{
			// Allow our sleeping to be interrupted by a terminate request
			WaitForSingleObject(hStopEvent, nDelay);
		}

		// Get the time for the next loop
		nLastTime = timeGetTime();
		// Adjust the time by however much we overslept
		if (((nLastTime - nCurTime) > nDelay) && (nDelay > 0))
		{
			nLastTime -= (nLastTime - (nCurTime + nDelay));
		}
	}
}

// [0x4064d0]
void CServerDlg::OnCancel()
{
	// Ask the user to confirm stopping server.
	if( m_bConfirmExit )
	{
		if( !ConfirmStop( ))
			return;
	}

	// User really wants to stop the server.
	StopServer( );

	CDialog::OnCancel();
}

// [0x406500]
void CServerDlg::OnDestroy()
{
	StopServer( );

	// The level list's image list is ours (OnInitDialog).
	CImageList* pImageList = m_Levels.GetImageList( LVSIL_STATE );
	if( pImageList )
	{
		pImageList->DeleteImageList( );
		delete pImageList;
	}

	CDialog::OnDestroy();
}

// [0x406550]: the .rez files startup.txt lists ("Rez" tag, Rez0 ... Rez31),
// or the retail game's three when it lists none.
BOOL CServerDlg::AddResources( )
{
	CWaitCursor wc;
	char *apszRezFiles[eMaxRezFiles];
	CButeMgr buteRez;
	CStringList lstRez;
	CString sRezKey;
	CString sRez;
	int nRezIndex;
	POSITION pos;

	// Make sure we have server interface.
	if( !m_pServerMgr )
		return FALSE;

	// Check if there's a startup file specifying rez files.
	if( buteRez.Parse( "startup.txt" ))
	{
		// Find all the rezfiles.  Add them in order.  The first
		// missed rezkey means there are no more to add.
		for( nRezIndex = 0; nRezIndex < eMaxRezFiles; nRezIndex++ )
		{
			sRezKey.Format( "Rez%d", nRezIndex );
			sRez = buteRez.GetString( "Rez", sRezKey );

			// Check if no more rez files.
			if( !buteRez.Success( ))
			{
				break;
			}

			// Add it to the list.
			lstRez.AddTail( sRez );
		}
	}

	// Add the stock rez files.
	if( lstRez.IsEmpty( ))
	{
		lstRez.AddTail( "nolf" );
		lstRez.AddTail( "nolf.rez" );
		lstRez.AddTail( "nolf2.rez" );
	}

	// Now add them to the array to pass to the server.
	pos = lstRez.GetHeadPosition( );
	nRezIndex = 0;
	while( pos && nRezIndex < eMaxRezFiles )
	{
		apszRezFiles[nRezIndex] = ( char* )( char const* )lstRez.GetNext( pos );
		nRezIndex++;
	}

	// Add the resources.
	return m_pServerMgr->AddResources(( char ** )apszRezFiles, nRezIndex );
}

// [0x406b90]
void CServerDlg::OnShellProcessNetPacket(char* sData, DWORD nLen, BYTE senderAddr[4], DWORD senderPort)
{
}

// [0x406ba0]
BOOL CServerDlg::ConfirmStop( )
{
	CString str, title;

	// Make sure server is really running.
	if( !IsLoaded( ))
		return TRUE;

	// Ask the user to confirm the server stop.
	str.LoadString(IDS_CHECKEXIT);
	title.LoadString(IDS_APPNAME);
	return ( MessageBox(str, title, MB_YESNO | MB_ICONQUESTION) == IDYES );
}

// [0x406c80]
CString CServerDlg::GetSettingString(const char* szVar, const char* szDefault)
{
	CString sDefault;
	if( szDefault )
		sDefault = szDefault;

	if( !szVar || !m_pServerMgr )
		return sDefault;

	return m_ButeMgr.GetString( SETTINGS_TAG, szVar, sDefault );
}

// [0x406d40]
float CServerDlg::GetSettingFloat(const char* szVar, float fDefault)
{
	if( !szVar || !m_pServerMgr )
		return fDefault;

	return m_ButeMgr.GetFloat( SETTINGS_TAG, szVar, fDefault );
}

// [0x406d80]
void CServerDlg::SetSettingString(const char* szVar, const char* szValue)
{
	if( !szVar || !szValue )
		return;

	CString sValue = szValue;
	m_ButeMgr.SetString( SETTINGS_TAG, szVar, sValue );
}

// [0x406e00]
void CServerDlg::SetSettingFloat(const char* szVar, float fValue)
{
	if( !szVar )
		return;

	m_ButeMgr.SetFloat( SETTINGS_TAG, szVar, fValue );
}

// [0x406e30]: the worlds (.dat) in a directory of the server's resources,
// without their extension.
BOOL CServerDlg::FillWorldList(CStringList* pList, const char* szDir, BOOL bFullPath)
{
	CWaitCursor wc;

	if( !szDir )
		return FALSE;

	FileEntry* pFiles = m_pServerMgr->GetFileList(( char* )szDir );
	if( pFiles )
	{
		FileEntry* pFile = pFiles;
		while( pFile )
		{
			if( pFile->m_Type == TYPE_FILE )
			{
				char szExt[_MAX_EXT*2];
				_splitpath( pFile->m_pBaseFilename, NULL, NULL, NULL, szExt );
				if( !stricmp( szExt, ".dat" ))
				{
					char szWorld[_MAX_PATH*2];
					if( !bFullPath )
						strcpy( szWorld, pFile->m_pBaseFilename );
					else
						strcpy( szWorld, pFile->m_pFullFilename );

					int nLen = strlen( szWorld );
					if( nLen > 4 )
						szWorld[nLen - 4] = '\0';

					pList->AddTail( szWorld );
				}
			}

			pFile = pFile->m_pNext;
		}

		m_pServerMgr->FreeFileList( pFiles );
	}

	return TRUE;
}

// [0x406fb0]: the game type's levels, from the settings, into the list.
// A level's item data says whether it is a custom one, outside the game's
// Multi\AssaultMap and Multi\DeathMatch directories -- meant to, rather:
// the test asks whether the name STARTS with either (Find() == 0), which
// none does, so no level is ever "custom", and every name loses its path.
// Nothing reads the item data, so nothing shows it.
BOOL CServerDlg::FillSelectedWorldList(CListCtrl* pList)
{
	CString sLevelFmt, sLevelKey, sLevel, sUpper;
	int nLevels;

	switch(( int )GetSettingFloat( "NetGameType", 2.0f ))
	{
		case COOPERATIVE_ASSAULT:
			nLevels = ( int )GetSettingFloat( "NetCANumLevels", 0.0f );
			sLevelFmt = "NetCALevel%i";
			break;

		case DEATHMATCH:
			nLevels = ( int )GetSettingFloat( "NetNumLevels", 0.0f );
			sLevelFmt = "NetLevel%i";
			break;

		default:
			return FALSE;
	}

	pList->DeleteAllItems( );

	for( int nLevel = 0; nLevel < nLevels; nLevel++ )
	{
		sLevelKey.Format( sLevelFmt, nLevel );
		sLevel = GetSettingString( sLevelKey, "" );

		sUpper = sLevel;
		sUpper.MakeUpper( );

		BOOL bCustom;
		if( !sUpper.Find( "\\MULTI\\ASSAULTMAP\\" ) && !sUpper.Find( "\\MULTI\\DEATHMATCH\\" ))
		{
			bCustom = TRUE;
		}
		else
		{
			bCustom = FALSE;

			// Show the name without its path.
			int nLen = sLevel.GetLength( );
			if( nLen > 2 )
			{
				int i;
				for( i = nLen - 1; i > 0 && sLevel[i] != '\\'; i-- );
				if( i < nLen - 1 )
				{
					if( sLevel[i] == '\\' )
						i++;
					sLevel = sLevel.Right( nLen - i );
				}
			}
		}

		if( !sLevel.IsEmpty( ))
		{
			int nItem = pList->InsertItem( LVIF_TEXT, pList->GetItemCount( ), sLevel, 0, 0, 0, 0 );
			pList->SetItem( nItem, 0, LVIF_PARAM, NULL, 0, 0, 0, bCustom );
		}
	}

	return TRUE;
}

// [0x407260]: the game's strings are in CRes.dll, inside the .rez files:
// copied out to a temporary file, and loaded from there.
BOOL CServerDlg::LoadResourceModule( )
{
	CWaitCursor wc;
	char szTempPath[MAX_PATH*2];
	char szTempFile[MAX_PATH*2];

	if( !GetTempPath( sizeof( szTempPath ), szTempPath ))
		return FALSE;

	if( !GetTempFileName( szTempPath, "NOLF", 0, szTempFile ))
		return FALSE;

	if( m_pServerMgr->CopyFile( "cres.dll", szTempFile ) != LT_OK )
		return FALSE;

	m_sResModuleFile = szTempFile;
	m_hResModule = LoadLibrary( m_sResModuleFile );
	if( !m_hResModule )
	{
		DeleteFile( m_sResModuleFile );
		m_sResModuleFile.Empty( );
		return FALSE;
	}

	return TRUE;
}

// [0x407380]: the server options (Attributes\ServerOptions.txt, in the
// .rez files): Option0, Option1, ... until one is missing. nLen and pData
// are declared (and zeroed) up front: without that, VC6 stops expanding
// ~CButeMgr inline one return early and calls it out of line (0x403c10).
BOOL CServerDlg::LoadServerOptions( )
{
	ILTStream* pStream = NULL;
	unsigned long nLen = 0;
	char* pData = NULL;
	CWaitCursor wc;
	CButeMgr buteMgr;

	if( m_pServerMgr->OpenFile( "Attributes\\ServerOptions.txt", &pStream ) != LT_OK || !pStream )
		return FALSE;

	nLen = pStream->GetLen( );

	pData = new char[nLen];
	if( !pData )
	{
		pStream->Release( );
		pStream = NULL;
		return FALSE;
	}

	if( pStream->Read( pData, nLen ) != LT_OK )
	{
		delete [] pData;
		pStream->Release( );
		pStream = NULL;
		return FALSE;
	}

	BOOL bParsed = buteMgr.Parse( pData, nLen );

	delete [] pData;
	pStream->Release( );
	pStream = NULL;

	if( !bParsed )
		return FALSE;

	CString sTag, sUnused;

	m_lstServerOptions.RemoveAll( );

	int nOption = 0;
	sTag.Format( "Option%d", nOption );
	while( buteMgr.Exist( sTag ))
	{
		ServerOption* pOption = new ServerOption;
		if( !pOption )
			return FALSE;

		if( !pOption->InitializeFromBute( buteMgr, sTag ))
			delete pOption;
		else
			m_lstServerOptions.AddTail( pOption );

		nOption++;
		sTag.Format( "Option%d", nOption );
	}

	return TRUE;
}

// [0x4079e0]
ServerOption* CServerDlg::GetServerOption(const char* szVar)
{
	POSITION pos = m_lstServerOptions.GetHeadPosition( );
	while( pos )
	{
		ServerOption* pOption = m_lstServerOptions.GetNext( pos );
		if( pOption && !stricmp( pOption->szVariable, szVar ))
			return pOption;
	}

	return NULL;
}

// [0x407a30]
float CServerDlg::GetServerOptionDefault(const char* szVar)
{
	ServerOption* pOption = GetServerOption( szVar );
	if( !pOption )
		return 0.0f;

	return pOption->fDefault;
}

// [0x407a50]
float CServerDlg::GetServerSettingFloat(const char* szVar)
{
	return GetSettingFloat( szVar, GetServerOptionDefault( szVar ));
}

// [0x407a70]
CString CServerDlg::LoadResString(UINT nStringID)
{
	CString sString;

	HINSTANCE hResModule = m_hResModule;
	if( !hResModule )
		return sString;

	LoadString( hResModule, nStringID, sString.GetBuffer( 260 ), 260 );
	sString.ReleaseBuffer( );

	return sString;
}

// [0x407b30]: the weapons a player can start with (Attributes\Weapons.txt:
// those in the player's weapon order, CanBeDefault), by their names in
// CRes.dll, with their weapon order index (+ 20) as the item data.
// dResult and bCanBeDefault keep a zero in ebx for the whole function (the
// weapon counter then lives in memory), as in the shipped code.
BOOL CServerDlg::FillWeaponList(CComboBox* pComboBox)
{
	CButeMgr buteMgr;
	CWaitCursor wc;
	ILTStream* pStream;

	if( !m_hResModule )
		return FALSE;

	LTRESULT dResult = m_pServerMgr->OpenFile( "Attributes\\Weapons.txt", &pStream );
	if( dResult != LT_OK )
		return FALSE;

	unsigned long nLen = pStream->GetLen( );

	char* pData = new char[nLen];
	if( !pData )
	{
		pStream->Release( );
		return FALSE;
	}

	pStream->Read( pData, nLen );

	BOOL bParsed = buteMgr.Parse( pData, nLen );

	pStream->Release( );
	delete [] pData;

	if( !bParsed )
		return FALSE;

	CMapStringToPtr mapWeapons;
	CString sKey, sName;
	void* pValue;

	int nFirst = buteMgr.GetInt( "WeaponOrder", "FirstPlayerWeapon", 0 );
	int nLast = buteMgr.GetInt( "WeaponOrder", "LastPlayerWeapon", 0 );
	for( int i = nFirst; i <= nLast; i++ )
	{
		sKey.Format( "Weapon%d", i );
		sName = buteMgr.GetString( "WeaponOrder", sKey );
		if( !buteMgr.Success( ))
			break;

		pValue = ( void* )( i + 20 );
		mapWeapons.SetAt( sName, pValue );
	}

	CString sTag, sDisplay;
	int nWeapon = 0;
	sTag.Format( "Weapon%d", nWeapon );
	sName = buteMgr.GetString( sTag, "Name" );
	while( buteMgr.Success( ))
	{
		BOOL bCanBeDefault = buteMgr.GetInt( sTag, "CanBeDefault", 0 );
		if( bCanBeDefault )
		{
			int nNameId = buteMgr.GetInt( sTag, "NameId" );
			if( buteMgr.Success( ) && mapWeapons.Lookup( sName, pValue ))
			{
				sDisplay = LoadResString( nNameId );
				int nIndex = pComboBox->AddString( sDisplay );
				if( nIndex >= 0 )
					pComboBox->SetItemData( nIndex, ( DWORD )pValue );
			}
		}

		nWeapon++;
		sTag.Format( "Weapon%d", nWeapon );
		sName = buteMgr.GetString( sTag, "Name" );
	}

	return TRUE;
}

// [0x408330]
void CServerDlg::OnDblclkLevels(NMHDR* pNMHDR, LRESULT* pResult)
{
	POSITION pos;
	int nWorldIndex;

	pos = m_Levels.GetFirstSelectedItemPosition( );
	if( !pos )
		return;

	nWorldIndex = m_Levels.GetNextSelectedItem( pos );
	SelectLevel( nWorldIndex );

	*pResult = 0;
}


// The command line's settings: -<setting> <value> for each of these, which
// sets the setting <setting> in NetHost.txt (a number or a string). Note
// that the levels can only be set as "NetLevel" and "NetCALevel", not
// NetLevel0, NetLevel1, ...: the server reads none of those two.
struct CmdLineOption
{
	char	m_szName[256];
	BOOL	m_bFloat;
};

static CmdLineOption g_aCmdLineOptions[] =		// 0x449550
{
	{ "-NetSessionName",		FALSE },
	{ "-NetUsePassword",		TRUE },
	{ "-NetPassword",			FALSE },
	{ "-NetMaxPlayers",			TRUE },
	{ "-NetPort",				TRUE },
	{ "-NetCAGameEnd",			TRUE },
	{ "-NetEndTime",			TRUE },
	{ "-NetIntelScore",			TRUE },
	{ "-NetEndScore",			TRUE },
	{ "-NetFragScore",			TRUE },
	{ "-NetFriendlyFire",		TRUE },
	{ "-NetDMGameEnd",			TRUE },
	{ "-NetEndFrags",			TRUE },
	{ "-NetEndTime",			TRUE },
	{ "-NetDefaultWeapon",		TRUE },
	{ "-NetHitLocation",		TRUE },
	{ "-NetWeaponsStay",		TRUE },
	{ "-NetAudioTaunts",		TRUE },
	{ "-NetRunSpeed",			TRUE },
	{ "-NetRespawnScale",		TRUE },
	{ "-NetFallDamageScale",	TRUE },
	{ "-NetArmorHealthPercent",	TRUE },
	{ "-NetGameType",			TRUE },
	{ "-NetNumLevels",			TRUE },
	{ "-NetCANumLevels",		TRUE },
	{ "-NetLevel",				FALSE },
	{ "-NetCALevel",			FALSE },
	// M-P6-13: the master server the game reports to (the modern game
	// DLL reads it from NetHost.txt; MODERN.md M-P6-02)
	{ "-MasterServer",			FALSE },
};

#define NUM_CMDLINE_OPTIONS		(int)(sizeof(g_aCmdLineOptions) / sizeof(g_aCmdLineOptions[0]))

// [0x408380]: -nowiz skips the wizard; returns whether the command line
// had anything for the server at all. Every option is taken to have a
// value after it, -nowiz included: as the last argument, -nowiz is not
// seen (RETAIL_BUGS.md).
BOOL CServerDlg::ParseCommandLine( BOOL* pbWizard )
{
	CString sArg;
	BOOL bFound = FALSE;

	*pbWizard = TRUE;
	// M-P6-12: -NetLevel / -NetCALevel each add a level, in order
	int nLevels = 0, nCALevels = 0;

	for( int nArg = 1; nArg < __argc; nArg++ )
	{
		// -nowiz has no value: it counts wherever it is, the last
		// argument too
		sArg = __argv[nArg];

		if( !stricmp( sArg, "-nowiz" ))
		{
			bFound = TRUE;
			*pbWizard = FALSE;
			continue;
		}

		// every other option has one
		if( nArg + 1 >= __argc )
			break;

		if( P6_LevelOption( this, sArg, __argv[nArg + 1], nLevels, nCALevels ))
		{
			bFound = TRUE;
			nArg++;
			continue;
		}

		int nOption;
		for( nOption = 0; nOption < NUM_CMDLINE_OPTIONS; nOption++ )
		{
			if( !stricmp( sArg, g_aCmdLineOptions[nOption].m_szName ))
				break;
		}

		if( nOption == NUM_CMDLINE_OPTIONS )
			continue;

		bFound = TRUE;

		if( g_aCmdLineOptions[nOption].m_bFloat )
			SetSettingFloat( &g_aCmdLineOptions[nOption].m_szName[1], ( float )atof( __argv[nArg + 1] ));
		else
			SetSettingString( &g_aCmdLineOptions[nOption].m_szName[1], __argv[nArg + 1] );

		// Skip the value.
		nArg++;
	}

	return bFound;
}


// [0x4084e0]: on the first show, the settings, the server and the wizard.
void CServerDlg::OnShowWindow(BOOL bShow, UINT nStatus)
{
	CDialog::OnShowWindow(bShow, nStatus);

	if( !bShow || !m_bFirstShow )
		return;

	// Only count the first show.
	m_bFirstShow = FALSE;

	// The settings.
	m_ButeMgr.Parse( SETTINGS_FILE );

	// Load up the server.
	if( !LoadServer( ))
	{
		OnShellFatalError( IDS_ERROR_CANTSTARTSERVER );
		PostMessage( WM_COMMAND, IDCANCEL );
		return;
	}

	// The wizard, unless the command line says -nowiz.
	{
		BOOL bWizard = TRUE;
		if( !ParseCommandLine( &bWizard ) || bWizard )
		{
			CWizardDlg dlg;
			int nRet = dlg.DoModal( );
			if( nRet != IDOK && nRet != ID_WIZFINISH )
			{
				StopServer( );
				PostMessage( WM_COMMAND, IDCANCEL );
				return;
			}
		}
	}

	// Keep the settings for the next time.
	if( !m_ButeMgr.Save( SETTINGS_FILE ))
	{
		AfxMessageBox( IDS_ERROR_CANTSAVEOPTIONS );
	}

	m_sServerName.LoadString( IDS_GOODVSEVIL );
	m_sServerName = GetSettingString( "NetSessionName", m_sServerName );

	if( !FillSelectedWorldList( &m_Levels ) || !m_Levels.GetItemCount( ))
	{
		OnShellFatalError( IDS_ERROR_CANTSTARTSERVER );
		PostMessage( WM_COMMAND, IDCANCEL );
		return;
	}

	// Run the server.
	if( !RunServer( ))
	{
		OnShellFatalError( IDS_ERROR_CANTSTARTSERVER );
		PostMessage( WM_COMMAND, IDCANCEL );
		return;
	}

	// M-P6-13: where the game is listed
	P6_ShowMasterServer( this );

	// Update our controls.
	UpdateData( FALSE );
}

// [0x4086f0]
void CServerDlg::OnStopserver()
{
	OnCancel( );
}

// [0x408700]: a slider for a server option: the option's range, and the
// setting's value (in the option's scale).
BOOL CServerDlg::InitSlider(CSliderCtrl* pSlider, const char* szVar)
{
	if( !szVar )
		return FALSE;

	ServerOption* pOption = GetServerOption( szVar );
	if( !pOption )
		return FALSE;

	pSlider->SetRange( pOption->nSliderMin, pOption->nSliderMax );

	float fValue = GetSettingFloat( szVar, pOption->fDefault );
	if( pOption->fSliderScale > 0.0f )
		fValue /= pOption->fSliderScale;

	pSlider->SetPos(( int )( fValue + 0.5f ));

	return TRUE;
}


// [0x408790]
CString CServerDlg::FormatTime( CTimeSpan &timeSpan )
{
	CString sTime;

	if( timeSpan.GetDays( ) > 0 )
		sTime = timeSpan.Format( "%Dd:%Hh:%Mm:%Ss" );
	else if( timeSpan.GetHours( ) > 0 )
		sTime = timeSpan.Format( "%Hh:%Mm:%Ss" );
	else
		sTime = timeSpan.Format( "%Mm:%Ss" );

	return sTime;
}
