// *********************************************************************** //
//
//  MODULE:   SERVERMGR (.CPP)
//
//  PURPOSE:  MyServerInterface — server.dll's concrete ServerInterface.
//            The console / config-file / shell-message half is reconstructed
//            (0x1002c021-0x1002c372); the rest is still blank stubs with the
//            exact shipped signatures. Reconstruct one at a time and verify each.
//
//            Every method opens the same way: the manager pointer is checked
//            and a missing one is LT_NOTINITIALIZED at debug level 1, not the
//            LT_INVALIDPARAMS/level-2 pair the ILTCSBase block uses.
//
//  SERVER.DLL ONLY. This TU contributes NOTHING to the shipped lithtech.exe,
//  so the 29-row "servermgr" /O2 gap is a false positive and there
//  is no /O2 shape to chase here. Three independent proofs:
//
//    * lithtech.exe contains ZERO occurrences of the literal "ServerInterface::"
//      (server.dll has nine). Eleven of these methods push such a literal into
//      dsi_ServerError, so none of them is in the image.
//    * a byte match of src/engine/lithtech/Release/servermgr.obj against
//      the retail lithtech.exe matches one symbol, FreeFileList -- and that is a
//      14-byte `jmp fs_FreeFileList` thunk that matches ambiguously (0x407a70+),
//      i.e. it matched somebody else's identical tail call.
//    * server.cpp, which defines the g_ServerInterface that would anchor the
//      vtable, is equally absent: the retail lithtech.exe has NO export directory,
//      and server.cpp's CreateServer/DeleteServer are __declspec(dllexport).
//
//  The vtable is one link unit: if it were emitted every virtual below would
//  be kept, so the eleven string-bearing methods being absent proves all 31
//  are. A "present on both sides" filter that checks only OUR object, not
//  the shipped image, cannot see this.
//
// *********************************************************************** //

#include <stdio.h>
#include <string.h>
#include "servermgr.h"
#include "de_error.h"
#include "server_access.h"


// The whole TU is server.dll's (see above), so lithtech.exe's compile of
// it is empty: the vtable is one link unit and nothing in the client
// references it (server.cpp's two exports, the only users, are guarded the
// same way).
#ifdef LT_SERVER_BUILD

// 0x10058bb0 -- the one server interface (servermgr.h). Its dynamic
// initializer group (0x1002bfe7 head, 0x1002bff1 constructor, 0x1002c00a
// atexit registrar, 0x1002c016 destructor) opens this TU's code, ahead of
// RunConsoleString at 0x1002c021.
MyServerInterface g_ServerInterface;


LTRESULT MyServerInterface::SetAppHandler(ServerAppHandler *pHandler)
{
    ServerInterfaceCall activity;
    auto* pMgr = activity.manager();
    if (!activity.available()) { return LT_NOTINITIALIZED; }

	if (!pMgr)
		RETURN_ERROR(1, "ServerInterface::SetAppHandler", LT_NOTINITIALIZED);

	pMgr->m_pAppHandler = pHandler;
	return LT_OK;
}

LTRESULT MyServerInterface::RunConsoleString(char *pStr)
{
    ServerInterfaceCall activity;
    auto* pMgr = activity.manager();
    if (!activity.available()) { return LT_NOTINITIALIZED; }

	if (!pMgr)
		RETURN_ERROR(1, "ServerInterface::RunConsoleString", LT_NOTINITIALIZED);

	return c_CommandHandler(&pMgr->m_ConsoleState, pStr);
}

LTRESULT MyServerInterface::GetConsoleVar(char *pName, HCONSOLEVAR *hVar, char *pDefaultVal)
{
    ServerInterfaceCall activity;
    auto* pMgr = activity.manager();
    if (!activity.available()) { return LT_NOTINITIALIZED; }

	if (!hVar || !pName || !*pName)
		RETURN_ERROR(1, "ServerInterface::GetConsoleVar", LT_INVALIDPARAMS);

	*hVar = LTNULL;
	if (!pMgr)
		RETURN_ERROR(1, "ServerInterface::GetConsoleVar", LT_NOTINITIALIZED);

	// The two success arms end in the same `*hVar = hVarFound; return <code>;`
	// and VC6 merges them. The shipped build hosts the merged copy in the
	// FIRST arm (0x1002c108) and the second arm jumps back into it
	// (0x1002c160). That is the if/else-chain form: every test an explicit
	// `else`, every arm ending in its own return. As guarded early returns,
	// or with LT_NOTFOUND as a trailing statement, VC6 hosts it at the LAST
	// arm instead -- same size, one block out.
	HCONSOLEVAR hVarFound = cc_FindConsoleVar(&pMgr->m_ConsoleState, pName);
	if (hVarFound)
	{
		*hVar = hVarFound;
		return LT_OK;
	}
	else if (pDefaultVal)
	{
		con_SetVar(&pMgr->m_ConsoleState, pName, pDefaultVal);

		hVarFound = cc_FindConsoleVar(&pMgr->m_ConsoleState, pName);
		if (hVarFound)
		{
			*hVar = hVarFound;
			return LT_FINISHED;
		}
		else
		{
			return LT_ERROR;
		}
	}
	else
	{
		return LT_NOTFOUND;
	}
}

LTRESULT MyServerInterface::GetVarValueFloat(HCONSOLEVAR hVar, float *val)
{
    ServerInterfaceCall activity;
    auto* pMgr = activity.manager();
    if (!activity.available()) { return LT_NOTINITIALIZED; }

	if (!hVar || !val || !pMgr->m_ConsoleState.m_Variables.Contains(reinterpret_cast<LTCommandVar*>(hVar)))
		RETURN_ERROR(1, "ServerInterface::GetVarValueFloat", LT_INVALIDPARAMS);

	*val = ((LTCommandVar*)hVar)->m_FloatVal;
	return LT_OK;
}

LTRESULT MyServerInterface::GetVarValueString(HCONSOLEVAR hVar, char *pStr, uint32 maxLen)
{
    ServerInterfaceCall activity;
    auto* pMgr = activity.manager();
    if (!activity.available()) { return LT_NOTINITIALIZED; }

	if (!hVar || !pStr || !maxLen || !pMgr->m_ConsoleState.m_Variables.Contains(reinterpret_cast<LTCommandVar*>(hVar)))
		RETURN_ERROR(1, "ServerInterface::GetVarValueString", LT_INVALIDPARAMS);

	const auto* value = ((LTCommandVar*)hVar)->m_pStringVal;
    strncpy(pStr, value ? value : "", maxLen - 1);
	pStr[maxLen - 1] = 0;
	return LT_OK;
}

LTRESULT MyServerInterface::LoadConfigFile(char *pFilename)
{
    ServerInterfaceCall activity;
    auto* pMgr = activity.manager();
    if (!activity.available()) { return LT_NOTINITIALIZED; }

	if (!pMgr)
		RETURN_ERROR(1, "ServerInterface::LoadConfigFile", LT_NOTINITIALIZED);

	return !cc_LoadConfigFile(&pMgr->m_ConsoleState, pFilename, LTFALSE, LTTRUE);
}

LTRESULT MyServerInterface::SaveConfigFile(char *pFilename)
{
    ServerInterfaceCall activity;
    auto* pMgr = activity.manager();
    if (!activity.available()) { return LT_NOTINITIALIZED; }

	if (!pMgr)
		RETURN_ERROR(1, "ServerInterface::SaveConfigFile", LT_NOTINITIALIZED);

	return !cc_SaveConfigFile(&pMgr->m_ConsoleState, pFilename);
}

LTRESULT MyServerInterface::SendToServerShell(char *pInfo, int nLen)
{
    ServerInterfaceCall activity;
    auto* pMgr = activity.manager();
    if (!activity.available()) { return LT_NOTINITIALIZED; }

	if (!pMgr)
		RETURN_ERROR(1, "ServerInterface::SendToServerShell", LT_NOTINITIALIZED);

	IServerShell* pShell = pMgr->m_ClassMgr.m_pServerShell;
	if (pShell)
		return pMgr->m_ClassMgr.shell([&](IServerShell& shell) { return shell.ServerAppMessageFn(pInfo,nLen); });

	return LT_NOTFOUND;
}

LTBOOL MyServerInterface::AddResources(char **pResources, uint32 nResources)
{
    ServerInterfaceCall activity;
    auto* pMgr = activity.manager();
    if (!activity.available()) { return LTFALSE; }

	if (!m_pServerMgr)
		return LTFALSE;

	return pMgr->AddResources(pResources, nResources);
}

LTBOOL MyServerInterface::LoadBinaries()
{
    ServerInterfaceCall activity;
    auto* pMgr = activity.manager();
    if (!activity.available()) { return LTFALSE; }

	if (!m_pServerMgr)
		return LTFALSE;

	return pMgr->LoadBinaries();
}

FileEntry* MyServerInterface::GetFileList(char *pDirName)
{
    ServerInterfaceCall activity;
    auto* pMgr = activity.manager();
    if (!activity.available()) { return nullptr; }

	if (!m_pServerMgr)
		return LTNULL;

	return fs_GetFileList(&pMgr->m_FileMgr, pDirName);
}

void MyServerInterface::FreeFileList(FileEntry *pList)
{
	fs_FreeFileList(pList);
}

LTRESULT MyServerInterface::OpenFile(char *pFilename, ILTStream **pStream)
{
    ServerInterfaceCall activity;
    auto* pMgr = activity.manager();
    if (!activity.available()) { if (pStream) *pStream = nullptr; return LT_NOTINITIALIZED; }

	static char* pFnName = "MyServerInterface::OpenFile";

	if (!pFilename || !pStream)
		RETURN_ERROR_SYM(2, pFnName, LT_INVALIDPARAMS);

	*pStream = fs_OpenFile(&pMgr->m_FileMgr, pFilename);
	if (*pStream)
		return LT_OK;

	RETURN_ERROR(2, pFnName, LT_MISSINGFILE);
}

LTRESULT MyServerInterface::CopyFile(const char *pszSourceFile, const char *pszDestFile)
{
    ServerInterfaceCall activity;
    auto* pMgr = activity.manager();
    if (!activity.available()) { return LT_NOTINITIALIZED; }

	return fs_CopyFile(&pMgr->m_FileMgr, pszSourceFile, pszDestFile);
}

LTBOOL MyServerInterface::SetGameInfo(void *pGameInfo, uint32 nGameInfoLen)
{
    ServerInterfaceCall activity;
    auto* pMgr = activity.manager();
    if (!activity.available()) { return LTFALSE; }

	return pMgr->SetGameInfo(pGameInfo, nGameInfoLen);
}

int MyServerInterface::GetNumClients()
{
    ServerInterfaceCall activity;
    auto* pMgr = activity.manager();
    if (!activity.available()) { return 0; }

	if (!m_pServerMgr)
		return 0;

	return pMgr->GetNumClients();
}

LTBOOL MyServerInterface::GetClientName(int index, char *pName, int maxChars)
{
    ServerInterfaceCall activity;
    auto* pMgr = activity.manager();
    if (!activity.available()) { return LTFALSE; }

	if (!m_pServerMgr)
		return LTFALSE;

	return pMgr->GetClientName(index, pName, maxChars);
}

LTBOOL MyServerInterface::GetClientInfo(int index, ClientInfo* pInfo)
{
    ServerInterfaceCall activity;
    auto* pMgr = activity.manager();
    if (!activity.available()) { return LTFALSE; }

	if (!m_pServerMgr)
		return LTFALSE;

	return pMgr->GetClientInfo(index, pInfo);
}

LTBOOL MyServerInterface::BootClient(uint32 dwClientID)
{
    ServerInterfaceCall activity;
    auto* pMgr = activity.manager();
    if (!activity.available()) { return LTFALSE; }

	if (!m_pServerMgr)
		return LTFALSE;

	return pMgr->BootClient(dwClientID);
}

LTRESULT MyServerInterface::GetClientPing(uint32 id, float& ping) {
    ServerInterfaceCall activity;
    auto* pMgr = activity.manager();
    if (!activity.available()) { ping = 0; return LT_NOTINITIALIZED; }

    ping = 0;
    if (!m_pServerMgr) return LT_NOTINITIALIZED;
    auto* client = pMgr->FindClient(id);
    if (!client || !client->m_pConnection || client->m_Removing) return LT_NOTINITIALIZED;
    ping = client->m_pConnection->m_Ping;
    return LT_OK;
}

int MyServerInterface::GetErrorCode()
{
    ServerInterfaceCall activity;
    auto* pMgr = activity.manager();
    if (!activity.available()) { return -1; }

	if (!m_pServerMgr)
		return -1;

	return pMgr->GetErrorCode();
}

void MyServerInterface::GetErrorString(char *pString, int maxLen)
{
    if (!pString || maxLen <= 0) return;
    ServerInterfaceCall activity;
    auto* pMgr = activity.manager();
    if (!activity.available()) { if (pString && maxLen > 0) pString[0] = 0; return; }

	if (!m_pServerMgr)
	{
		pString[0] = 0;
		return;
	}

	pMgr->GetErrorString(pString, maxLen);
}

LTBOOL MyServerInterface::Update(long flags)
{
    ServerInterfaceCall activity;
    auto* pMgr = activity.manager();
    if (!activity.available()) { return LTFALSE; }

	if (!m_pServerMgr)
		return LTFALSE;

	// The engine can tell it is being reentered from its own update.
	if (m_bInUpdate) return LTFALSE;
	m_bInUpdate = LTTRUE;
    struct Reset { LTBOOL& flag; ~Reset() { flag = LTFALSE; } } reset{m_bInUpdate};
	return pMgr->Update(flags, dsi_GetTime());
}

LTBOOL MyServerInterface::InitNetworking(char *pDriver, uint32 dwFlags)
{
    ServerInterfaceCall activity;
    auto* pMgr = activity.manager();
    if (!activity.available()) { return LTFALSE; }

	// Both arguments are accepted and ignored; the manager's own network
	// manager is simply started.
	if (!m_pServerMgr)
		return LTFALSE;

	return pMgr->m_NetMgr.Init() == LT_OK;
}

LTBOOL MyServerInterface::GetServiceList(NetService *&pListHead)
{
    ServerInterfaceCall activity;
    auto* pMgr = activity.manager();
    if (!activity.available()) { pListHead = nullptr; return LTFALSE; }

	return !pMgr->m_NetMgr.GetServiceList(pListHead);
}

LTBOOL MyServerInterface::FreeServiceList(NetService *pListHead)
{
    ServerInterfaceCall activity;
    auto* pMgr = activity.manager();
    if (!activity.available()) { return LTFALSE; }

	return !pMgr->m_NetMgr.FreeServiceList(pListHead);
}

LTBOOL MyServerInterface::SelectService(HNETSERVICE hNetService)
{
    ServerInterfaceCall activity;
    auto* pMgr = activity.manager();
    if (!activity.available()) { return LTFALSE; }

	return !pMgr->m_NetMgr.SelectService(hNetService);
}

LTBOOL MyServerInterface::UpdateSessionName(const char* sName)
{
    ServerInterfaceCall activity;
    auto* pMgr = activity.manager();
    if (!activity.available()) { return LTFALSE; }

	return !pMgr->m_NetMgr.UpdateSessionName(sName);
}

LTBOOL MyServerInterface::HostGame(NetHost &hostInfo)
{
    ServerInterfaceCall activity;
    auto* pMgr = activity.manager();
    if (!activity.available()) { return LTFALSE; }

	if (!pMgr)
		return LTFALSE;
	else
	{
		CBaseDriver* pDriver = pMgr->m_NetMgr.m_pMainDriver;

		if (!pDriver)
			return LTFALSE;
		else
			return !pDriver->HostSession(&hostInfo);
	}
}

LTBOOL MyServerInterface::GetTcpIpAddress(char* sAddress, uint32 dwBufferSize, unsigned short &hostPort)
{
    ServerInterfaceCall activity;
    auto* pMgr = activity.manager();
    if (!activity.available()) { hostPort = 0; if (sAddress && dwBufferSize) *sAddress = 0; return LTFALSE; }

	if (!m_pServerMgr)
		return LTFALSE;

	return !pMgr->m_NetMgr.GetTcpIpAddress(sAddress, dwBufferSize, hostPort);
}

LTRESULT MyServerInterface::SendTo(const void *pData, uint32 len, const char *sAddr, uint32 port)
{
    ServerInterfaceCall activity;
    auto* pMgr = activity.manager();
    if (!activity.available()) { return LT_NOTINITIALIZED; }

	for (uint32 i = 0; i < pMgr->m_NetMgr.m_Drivers.m_nElements; i++)
	{
		CBaseDriver* pDriver = pMgr->m_NetMgr.m_Drivers.m_pArray[i];
		if (pDriver->m_DriverFlags & NETDRIVER_TCPIP)
			return pDriver->SendTo(pData, len, sAddr, port);
	}

	return LT_NOTINITIALIZED;
}

#endif // LT_SERVER_BUILD
