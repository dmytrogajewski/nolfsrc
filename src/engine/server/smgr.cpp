#include "file_transfer.h"
#include "engine/portable/sky.h"
// server.dll — CServerMgr's interface-facing half (reconstruction). Build /O1.
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <new>
#include "servermgr.h"
#include "saveio.h"
#include "object_lifetime.h"
#include "engine/portable/string.h"
#include "platform/system.h"
#include <cstdint>
#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>
#include "serverengine.h"
#include "servermsg.h"
#include "commonmodel.h"
#include "servermodel.h"
#ifdef NOLFSRC_DEBUG
#include "nolfsrc_debug.h"
#endif
#include "commonphysics.h"
#include "packet.h"
#include "de_error.h"
#include "ltengineobjects.h"

// Three of servermgr.h's declarations for functions defined in this file
// have the wrong return type -- each body ends `xor %eax,%eax` on its only
// exit, which is LT_OK, where the header says void -- so they are defined
// under a 2 suffix (the convention sm_ObjectRemoved and
// sm_UpdateClientState2 already set) and declared here so their callers in
// this file reach the definitions rather than the header's declarations.
LTRESULT sm_ClientEnterWorld2(CServerMgr* pMgr, class Client* pClient);   // 0x1002759a
LTRESULT sm_SendObjectInfo2(CServerMgr* pMgr, class Client* pClient);     // 0x10026b4d

// struct_bank.h's allocate, inline as the header had it. servermgr.h declares
// it extern because server.dll calls 0x10001c86 at every site -- but the same
// source compiled /O2 into lithtech.exe carries the body expanded at those
// same sites (si_CreateObjectList is 69 bytes there against a 33-byte call,
// and sm_FindObjectsTouchingSphere opens with the free-list head, the grow and
// the unlink). An extern function cannot be expanded, so the declaration the
// original saw was inline and it is /Os that declines it: exactly the /O1-vs-
// /O2 split. Defined under a local name so servermgr.h's declaration -- and
// structbank.cpp's out-of-line copy, which server.dll still calls -- stand.
void sb_Grow(StructBank* pBank, uint32 nCount);

inline void* sm_sb_Allocate(StructBank* pBank)
{
	void* pStruct = pBank->m_pFreeList;

	if (!pStruct)
	{
		sb_Grow(pBank, pBank->m_Unknown08);

		pStruct = pBank->m_pFreeList;
		if (!pStruct)
			return LTNULL;
	}

	pBank->m_pFreeList = *(void**)pStruct;
	return pStruct;
}

// 0x10028933 / 0x004665xx -- structbank.cpp's CLEARED allocate, the same
// story as sm_sb_Allocate above: server.dll CALLS it (0x100288f3 in
// snd_FindSentState below) and lithtech.exe expands it at that same site
// (0x00466582..0x004665c5, no call but sb_Grow), which is the /Os-declines /
// /Ot-takes split, so the original declaration was `inline`.
//
// The fill is the CRT `memset`, not a named engine routine: the shipped call
// at 0x10028960 is 0x1003e280, whose normal-path `mov 0x8(%esp),%eax` at
// 0x1003e2cd is taken with %edi already pushed, i.e. it answers its
// DESTINATION, which is memset's contract. /O1 has no /Oi so it stays a
// call; /O2 does, which is why lithtech.exe has `rep stosl ; rep stosb`
// there and no call at all.
inline void* sm_sb_AllocateCleared(StructBank* pBank)
{
	void* pStruct = pBank->m_pFreeList;

	if (!pStruct)
	{
		sb_Grow(pBank, pBank->m_Unknown08);

		pStruct = pBank->m_pFreeList;
		if (!pStruct)
			return LTNULL;
	}

	pBank->m_pFreeList = *(void**)pStruct;

	memset(pStruct, 0, pBank->m_StructSize);

	return pStruct;
}

// CMessageRef::Init now lives in servermsg.h, `inline`, where the original
// had it -- the local copy this file used to carry was a workaround for its
// absence and would now be a duplicate definition.

// 0x10023e86 — servermsg.h's write cursor takes the message directly. Same
// story as the reference above: lithtech.exe's sm_WriteClientUpdate opens
// each of its two packet allocations with NO calls between msg_AllocPacket
// and the pIO test -- both the reference's Init and this one expanded, the
// freshly-constructed nulls folding both Release guards away -- where
// server.dll calls 0x1000ebab and 0x10023e86. servermessage.cpp owns the
// out-of-line definition and keeps it, so the body is reached through a view
// of the same layout; at /O1 the only difference from the shipped call is its
// masked target.
// ---- this file's globals ------------------------------------------------ //
//
// The server manager's own .bss run, 0x100587a0 .. 0x10058bf4 (every
// reference to these is from CServerMgr code in this file; g_ServerInterface
// at 0x10058bb0 is servermgr.cpp's and the bank at 0x10058bc0 svnew.cpp's).
// All zero in the shipped image. Names are servermgr.h's.
uint32          g_Unknown587a0;          // 0x100587a0  gather-tick accumulator
ClientIDList*   g_pFillIDList;           // 0x100587a4  the list being refilled
// 0x100587ac: 256 slots, not the 64 the dispatch can reach (it masks the id
// with 0x3f) -- sm_ResetProfileCounters' memset clears 0x400 bytes, and the
// next global (g_ServerInterface) is at 0x10058bb0 = 0x100587ac + 0x400 + 4.
PacketHandlerFn g_PacketHandlers[256];
int             g_FindSphereCount;       // 0x10058be4
int             g_FindSphereTicks;       // 0x10058be8
uint32          g_Unknown58bec;          // 0x10058bec
// 0x10058bf0 — the manager the old-API wrappers reach through
// (servermgr.h); SetupGlobals below refreshes it on every API entry. It
// follows this unit's own counters (0x10058be4..0x10058bec) in .bss, zeroed.
CServerMgr* g_pServerMgr;
CClassMgr*      g_pClassMgr;             // 0x10058bf4

// 0x10058bc0 — the bank the queued-object records come out of. Its Allocate
// is 0x1000ed6f, the same three-instruction forward onto the embedded struct
// bank that CHashElementBank::Allocate is (one body, folded by /OPT:ICF), so
// only the shape is established and not which class it belongs to.
//
// Defined in-class rather than declared: 0x1000ed6f IS the /Os compile of
// this body -- `add $0x4,%ecx; push %ecx; call sb_Allocate; pop %ecx; ret`,
// the forward with sm_sb_Allocate DECLINED inside it -- while lithtech.exe's
// sm_RemoveObject (0x0046a2a3) carries the whole two-level expansion, free
// list head, the grow with the bank at 0x004b17ac, and the unlink. An extern
// declaration can be neither, so the original saw a body here; /Ot accepts
// both levels and /Os declines both, which is why server.dll still calls the
// forward. Same argument, and the same local-COMDAT/ICF arrangement, as
// sm_sb_Allocate at the top of this file. One call site in this TU.


// 0x10058bc0. The object is packet.cpp's g_LinkNodeBank, defined once in
// svnew.cpp; link/aliases.rsp carries this spelling onto that one.


// 0x10058bdc, the shared LTLink free pool Term and sm_DrainObjectQueue thread
// spent links onto through their m_pPrev, is not a global of its own: it is
// this bank's free-list head (m_Bank.m_pFreeList, +0x1c). servermgr.h's
// `extern LTLink* g_FreeLinks` named that address as if it were one.
#define SM_FREE_LINKS (*(LTLink**)&g_ObjQueueBank.m_Bank.m_pFreeList)

void CServerMgr::SetupGlobals()
{
	g_pServerMgr = this;
	g_pClassMgr = &m_ClassMgr;
}

// SetupError RETURNS dsi_SetupMessage's result: that is what keeps eax busy
// and pushes the reload of nError for the store into ecx (0x10034660;
// lithtech 0x47638d has the same ecx reload, and the client's twin at
// lithtech 0x41fc60 returns the result outright). Declared void, the store
// reloads through eax and the function is two bytes off.
LTRESULT CServerMgr::SetupError(int nError, ...)
{
	va_list marker;
	LTRESULT dResult;

	va_start(marker, nError);
	dResult = dsi_SetupMessage(m_ErrorString, sizeof(m_ErrorString), nError, marker);
	va_end(marker);
	m_ErrorCode = nError;
	return dResult;
}

LTBOOL CServerMgr::AddResources(char** pResources, uint32 nResources)
{
	int nTrees;


	SetupGlobals();

	if (nResources == 0)
	{
		SetupError(57);
		return LTFALSE;
	}

	fs_AddResources(&m_FileMgr, pResources, nResources, nullptr, &nTrees);
	if (nTrees == 0)
	{
		SetupError(27, pResources[0]);
		return LTFALSE;
	}

	return LTTRUE;
}

LTBOOL CServerMgr::LoadBinaries()
{
	ServerActivity activity(m_Lifetime);
	if (!activity.available()) return LTFALSE;
	ServerCallbackGuard operation(m_Lifetime);
	SetupGlobals();
    const auto result = cb_LoadModule(&m_ClassMgr);
	return operation.unchanged() && result == LT_OK;
}

LTBOOL CServerMgr::SetGameInfo(void* data, uint32 size) {
    if (size > 16u*1024*1024 || (size && !data)) return LTFALSE;
    try {
        std::vector<std::byte> staged(size);
        if (size) std::memcpy(staged.data(),data,size);
        m_GameInformation.swap(staged);
        return LTTRUE;
    } catch (const std::bad_alloc&) { return LTFALSE; }
}

int CServerMgr::GetNumClients()
{
	return m_ClientList.m_nElements;
}

LTBOOL CServerMgr::GetClientName(int index, char* pName, int maxChars)
{
	SetupGlobals();

	pName[0] = 0;

	if (index < (int)m_ClientList.m_nElements)
	{
		LTLink* pCur = m_ClientList.m_Head.m_pNext;
		while (index > 0)
		{
			pCur = pCur->m_pNext;
			index--;
		}

		GetClientName((Client*)pCur->m_pData, pName, maxChars);
		return LTTRUE;
	}

	return LTFALSE;
}

LTBOOL CServerMgr::GetClientInfo(int index, ClientInfo* pInfo)
{
	if (!pInfo)
		return LTFALSE;

	SetupGlobals();

	if (index < (int)m_ClientList.m_nElements)
	{
		LTLink* pCur = m_ClientList.m_Head.m_pNext;
		while (index > 0)
		{
			pCur = pCur->m_pNext;
			index--;
		}

		Client* pClient = (Client*)pCur->m_pData;
		GetClientName(pClient, pInfo->m_sName, sizeof(pInfo->m_sName));
		pInfo->m_ClientID = pClient->m_ClientID;

		pInfo->m_Ping = -1.0f;
		if (pClient->m_pConnection)
			pInfo->m_Ping = pClient->m_pConnection->m_Ping;

		return LTTRUE;
	}

	return LTFALSE;
}

LTBOOL CServerMgr::BootClient(uint32 dwClientID)
{
	Client* pClient = FindClient(dwClientID);
	if (!pClient)
		return LTFALSE;

	m_NetMgr.Disconnect(pClient->m_pConnection, 6, 0);
	return LTTRUE;
}

Client* CServerMgr::FindClient(uint32 nClientId)
{
	LTLink* pCur = m_ClientList.m_Head.m_pNext;
	while (pCur != &m_ClientList.m_Head)
	{
		Client* pClient = (Client*)pCur->m_pData;
		if (pClient->m_ClientID == nClientId)
			return pClient;

		pCur = pCur->m_pNext;
	}

	return LTNULL;
}

void CServerMgr::GetErrorString(char* pString, int maxLen)
{
	strncpy(pString, m_ErrorString, maxLen);
}

int CServerMgr::GetErrorCode()
{
	return m_ErrorCode;
}

// 0x1003318d / 0x00474660 — the two images differ by ONE call, and it is the
// same mechanism serverengine.cpp's se_RegisterModel is written against:
// shared server source that reaches into the CLIENT when a client is in the
// process.
//
//   * lithtech.exe fills m_pUserData from `call 0x00414920` (0x004746aa) and
//     server.dll stores a zero there (`mov %edi,-0x10(%ebp)` at 0x100331d7).
//   * 0x00414920 is three instructions — `mov 0x004b17e0,%eax ; mov
//     0x11d8(%eax),%eax ; ret` — off the CLIENT manager global, and
//     src/engine/lithtech/clientmgr2.cpp already reconstructs it as
//     cm_GetUnknown11d8. Its ONLY caller in the whole image is this one site.
//
// Everything else about the two bodies lines up: the constructor's four
// stores hoist above m_World.Term() in both, and the memset is a real call at
// /O1 and four inline dword stores at /O2 (that is /Oi, not source).
#ifndef LT_SERVER_BUILD
uint32 cm_GetUnknown11d8();               // 0x00414920 (client only)
void   cs_Sub4149e0(int n);               // 0x004149e0 (client only)

// lithtech.exe 0x00474650, 9 bytes, directly in front of LoadWorld -- the
// load hook the client build hands the world loader, where server.dll hands
// dsi_OnReturnError (its empty release hook).  0x004746ba stores it
// (`movl $0x474650,0x1c(%esp)`), not lithtech's dsi_OnReturnError at
// 0x0042d760.  The body is `push $0; call 0x004149e0; pop %ecx; ret`: the
// argument is never read, and the call is the client manager poke CacheFile
// makes too (name ours).
void sm_ClientLoadHook(int nUnused)
{
	cs_Sub4149e0(0);
}
#endif

LTRESULT CServerMgr::LoadWorld(ILTStream* pStream, char* pWorldName)
{
	WorldLoadRequest request;

	m_World.Term();
	pStream->SeekTo(0);

	memset(&request, 0, sizeof(request));
	request.m_pStream = pStream;
#ifdef LT_SERVER_BUILD
	request.m_pUserData = LTNULL;
#else
	request.m_pUserData = (void*)cm_GetUnknown11d8();
#endif
#ifdef LT_SERVER_BUILD
	request.m_LoadHook = dsi_OnReturnError;
#else
	request.m_LoadHook = sm_ClientLoadHook;
#endif

	if (m_World.Load(&request) == 0 && m_World.LoadObjects(pStream) == 0)
	{
		m_bWorldLoaded = 1;
		return LT_OK;
	}

	m_World.Term();
	SetupError(42, pWorldName);
	RETURN_ERROR_INFO(1, "CServerMgr::LoadWorld", LT_INVALIDWORLDFILE, pWorldName);
}

LTRESULT CServerMgr::DoRunWorld() {
    ServerActivity activity(m_Lifetime);
    if (!activity.available()) return LT_NOTINITIALIZED;
    if (!m_Initialized || m_Terminating || m_EndingWorld || !m_Lifetime->alive || !m_ClassMgr.m_pServerShell) return LT_NOTINITIALIZED;
    if (!m_pWorldFile || !m_bWorldLoaded) return LT_NOTINWORLD;
    if (m_RunningWorldStart) return LT_ERROR;
    if (m_Unknown198 == 0) return LT_OK;
    ServerCallbackGuard operation(m_Lifetime);
    struct Scope {
        CServerMgr* manager; ServerCallbackGuard guard;
        ~Scope() { if (guard.alive()) manager->m_RunningWorldStart = false; }
    } scope{this,operation};
    m_RunningWorldStart = true;
    try {
        auto result = sm_CacheObjectFiles(this);
        if (!operation.unchanged()) return LT_ERROR;
        if (result != LT_OK) return result;
        result = sm_CacheFiles(this);
        if (!operation.unchanged()) return LT_ERROR;
        if (result != LT_OK) return result;
        m_Unknown198 = 0; m_UpdatesPerSec = g_UpdatesPerSec;
        m_ClassMgr.shell([](IServerShell& shell) { shell.PostStartWorld(); });
        if (!operation.unchanged()) return LT_ERROR;
        sm_FlushObjectQueue(this);
        if (!operation.unchanged()) return LT_ERROR;
        dsi_ServerError("Running world");
        return LT_OK;
    } catch (const std::exception& error) {
        if (operation.unchanged()) m_Unknown198 = 1;
        nolf::platform::log("world startup",error.what());
        return LT_ERROR;
    }
}

// 0x1002c929 — the server handed back. Term() is the whole body; everything
// after it is the compiler destroying the nine sub-objects that have
// destructors, in reverse declaration order, and then restoring CBaseMgr's
// vftable.
//
// server.dll only. lithtech.exe has neither out of line: clientshell.cpp's
// CreateServerMgr (0x00413590) expands the constructor in place and the
// deleting destructor at 0x00413740 expands this one, both through
// clientshell.cpp's own CServerMgrView; nothing in the image calls a
// standalone CServerMgr constructor or destructor.
#ifdef LT_SERVER_BUILD
CServerMgr::~CServerMgr()
{
    m_Lifetime->alive = false;
	Term();
}

CServerMgr::CServerMgr()
{
    m_ErrorCode = LT_OK; std::fill(std::begin(m_ErrorString),std::end(m_ErrorString),0);
    m_Unknown198 = 1; m_ServerFlags = m_Unknown1a0 = 0;
    m_bWorldLoaded = 0; m_hWorldFile = nullptr;
    m_pUnknown6a8 = nullptr; m_pCollisionInfo = nullptr;
    m_FrameTime = m_UpdatesPerSec = m_Unknown6b8 = m_Unknown6bc = m_Unknown6c0 = m_Unknown6c8 = m_Time = 0;
    m_Unknown6c4 = m_Unknown6dc = 0; m_Unknown6d0 = 0; m_Unknown6d4 = m_Unknown6d8 = 0;
    m_Unknown7f4 = m_Unknown7f8 = m_Unknown7fc = m_Unknown800 = 0;
    m_Unknown804 = nullptr; m_Link83c.Init2(nullptr);
    m_Unknown8b8 = 200; m_Unknown8bc = 0; m_hHash8c0 = nullptr;
    m_Link8c4.Init2(nullptr); m_Link8d0.Init2(nullptr);
    m_pChangedObjects = nullptr; m_pChangedSounds = nullptr;
    m_pUnknown928 = nullptr;
    m_SkyDef.m_Min.Init(-10,-10,-10); m_SkyDef.m_Max.Init(10,10,10);
    m_SkyDef.m_ViewMin.Init(); m_SkyDef.m_ViewMax.Init();
    std::fill(std::begin(m_SkyObjects),std::end(m_SkyObjects),uint16(0xffff));
    m_hGlobalLightObject = nullptr;
    m_Properties.reset(); m_pAppHandler = nullptr;
    m_ClientServerType = ServerType;
}
#endif

ServerMessage* CServerMgr::StartMessage()
{
	CMessageRef msg;
	CAbstractIO* pIO;

	msg.Init(msg_AllocPacket(&pIO, 0x1000, 0x1000));
	if (pIO)
		pIO->Release();

	msg.m_pMsg->m_Write.m_pObjectIO = m_Unknown804;

	// The reference the caller gets: the AddRef here is what the scoped
	// reference's destructor balances on the way out.
	msg.m_pMsg->AddRef();
	msg.m_pMsg->m_MsgFlags |= 1;

	return msg.m_pMsg;
}

// 0x1003318d's mechanism again, and this time the client's half is a whole
// loop. lithtech.exe's FreeUnusedModels (0x00475560..0x00475663) is ours plus
// ONE call — `call 0x00414a00` at 0x0047558d, between the object walk and the
// first dhash pass, return value discarded. 0x00414a00 runs the SAME walk over
// the CLIENT manager's object list (global 0x004b17e0, list head at +0x420)
// and sets the same bit 0 on the same +0x1dc model;
// src/engine/lithtech/csvis.cpp already reconstructs it as cs_DirtyAllModels
// and its note names 0x0047558d as its only call site. So the shipped source
// marks both managers' models and server.dll is the build where the client
// half is compiled out.
#ifndef LT_SERVER_BUILD
void cs_DirtyAllModels();                 // 0x00414a00 (client only)
#endif


// Retire borrowed hash entries before releasing unused owned models.
LTRESULT CServerMgr::FreeUnusedModels() {
    m_Models.CollectUnused([this](std::string_view name) {
        if (auto* element = dhash_FindElement(m_hModelHash,name.data(),static_cast<uint32>(name.size())))
            dhash_Remove(m_hModelHash,element);
    });
    return LT_OK;
}

// 0x1000d020 — CConnList's buffer release, reached through a view so that a
// copy of the body lives in this translation unit. lithtech.exe's Term does
// not call it: the object map's SetSize expands here and the release expands
// inside that, leaving the allocator's virtual Free reached directly and two
// `if (m_pItems)` tests on the same pointer still present: SetSize's, and
// BaseDelete's `if (!ptr) return;` (sm_BaseDeleteItems above) -- this one's
// own folds into SetSize's. The count BaseDelete's empty destruct loop takes
// is what lithtech parks in the frame. With the BaseDelete layer, server.dll
// also emits this body (0x1000d020) and SetSize folds with SetSize2 (0x1000be59). servermgr.h's declaration and netmgr.cpp's out-of-line
// definition -- which server.dll still calls -- are untouched; the call this
// emits at /O1 differs from the shipped one only in its (masked) target.
// Stand-in for collection.h's BaseDelete<T> on a flat view (an element type
// with no destructor): the null test and the dead count loop survive the
// /O2 expansion in lithtech Term (0x473623..0x473644: the count parked in
// 0x10(%esp), the second `cmp %ebp,%eax; je`).
inline void sm_BaseDeleteItems(CMoAllocator* pAlloc, void* ptr, uint32 nElements)
{
	if (!ptr)
		return;

	while (nElements)
	{
		--nElements;
	}

	pAlloc->Free(ptr);
}

class CConnListFreeView
{
public:
	uint8  m_pad_00[0x04];                   // the vptr
	void*  m_pItems;                         // +0x04
	uint32 m_nItems;                         // +0x08
	uint32 m_nSlack;                         // +0x0c
	uint32 m_nGrowBy;                        // +0x10

	inline void FreeItems(CMoAllocator* pAlloc, uint32 nItems)
	{
		if (m_pItems)
		{
			sm_BaseDeleteItems(pAlloc, m_pItems, nItems);
			m_pItems = LTNULL;
		}

		m_nSlack = 0;
	}
};

// 0x1002ccee — the object map's resize is CMoArray<CObjectMapEntry>::SetSize2
// (servermgr.h): lithtech.exe's Term carries it expanded, server.dll calls it.

void CServerMgr::Term()
{
    ServerActivity activity(m_Lifetime);
    if (m_Terminating) return;
    m_Terminating = true;
    ++m_Lifetime->epoch;
	SetupGlobals();
    m_Models.CancelAll();

	m_Thread.Term(LTTRUE);
	ProcessThreadMessages();

	EndWorld(0);

    m_GameInformation.clear();

    while (m_ClientList.m_Head.m_pNext != &m_ClientList.m_Head) {
        sm_RemoveClient(this,static_cast<Client*>(m_ClientList.m_Head.m_pNext->m_pData));
    }

	dl_InitList(&m_ClientList);

	m_NetMgr.Term();

	sm_RemoveObjects(this, LTTRUE);
	sm_FreeList44(this);
    m_InterObjectLinks.Clear();

	// Give the spare links back to the shared pool, threading them on
	// through their prev pointers.
	{
		LTLink* pCur = m_Link8c4.m_pNext;
		while (pCur != &m_Link8c4)
		{
			LTLink* pNext = pCur->m_pNext;
			pCur->m_pPrev = SM_FREE_LINKS;
			SM_FREE_LINKS = pCur;
			pCur = pNext;
		}
	}
	m_Link8c4.TieOff();
	m_Unknown8bc = 0;

	m_Unknown8fc.SetSize2(0, (LAlloc*)&g_DefAlloc);
	m_World.Term();
	sm_TermModels(this);

	dhash_Free(m_hModelHash);
	m_hModelHash = LTNULL;

	m_ClassMgr.Term();

	cc_TermState(&m_ConsoleState);
	sb_EnumStrings([](char* text, void*) { sm_ErrorHook(text); }, nullptr);

	fs_Term(&m_FileMgr);

	sb_Term(&m_ObjectListBank);
	sb_Term(&m_ObjectLinkBank);
	m_SoundInstances.Term();
	m_MessageBank.Term();

	dhash_Free(m_hHash8c0);
	m_hHash8c0 = LTNULL;

    m_CachedFiles.clear();

	un390_Term(&m_Unknown390);

	if (m_pUnknown6a8)
	{
		delete m_pUnknown6a8;
		m_pUnknown6a8 = LTNULL;
	}
    m_pCollisionInfo = nullptr;

	if (m_pUnknown928)
	{
		delete m_pUnknown928;
		m_pUnknown928 = LTNULL;
	}

	if (m_pPacketTrace)
	{
		m_pPacketTrace->Release();
		m_pPacketTrace = LTNULL;
	}

	if (m_Unknown804)
	{
		delete m_Unknown804;
		m_Unknown804 = LTNULL;
	}

	if (m_pPlaceholderModel)
	{
		m_PlaceholderModelOwner.reset();
		m_pPlaceholderModel = LTNULL;
	}

	m_pAppHandler = LTNULL;
	g_pServerMgr = LTNULL;
	g_pClassMgr = LTNULL;
    m_Initialized = false;
    m_Terminating = false;
}

// The packet-slot rewind is INDEXED, not walked. Written with a
// `ClientPacketSet* pSet = &pClient->m_PacketSets[0][0];` cursor and `pSet++`
// VC6 destroys the client pointer with `add $0x134,%ecx` and the function
// comes out a byte short; written as `pClient->m_PacketSets[i][j]` it
// strength-reduces to the same walking pointer but forms it beside the client
// with `lea 0x134(%ecx),%edx`, which is what shipped. (Loops want
// the array indexed, not walked — and this was the last byte of a function
// previously written off as the SpriteControl::SetCurPos SIB near class.)
void CServerMgr::EndWorld(int keep_data) {
    ServerActivity activity(m_Lifetime);
    if (m_EndingWorld) return;
    m_EndingWorld = true; ++m_Lifetime->epoch;
    struct Scope {
        CServerMgr& manager; ServerCallbackGuard guard;
        ~Scope() { if (guard.alive()) manager.m_EndingWorld = false; }
    } scope{*this,ServerCallbackGuard(m_Lifetime)};
    m_bWorldLoaded = 0;
    m_Properties.reset();
    client_Visit(m_ClientList,[](void*,Client& client) {
        for (auto& pair : client.m_PacketSets) for (auto& set : pair) if (auto* packet = set.Packet()) packet->m_Pos = packet->m_DataLen = 1;
    },nullptr);
    sm_EndWorldA(this);
    if (!scope.guard.unchanged()) return;
    sm_EndWorldB(this);
    struct Context { CServerMgr* manager; ServerCallbackGuard guard; } context{this,scope.guard};
    client_Visit(m_ClientList,[](void* pointer,Client& client) {
        auto& context = *static_cast<Context*>(pointer);
        if (context.guard.unchanged()) sm_SetClientState(context.manager,&client,1);
    },&context);
    if (!scope.guard.unchanged()) return;
    sm_EndWorldC(this); sm_RemoveObjects(this,LTFALSE);
    if (!scope.guard.unchanged()) return;
    sm_EndWorldD(this);
    m_World.TermObjects();
    if (!keep_data) m_World.Term();
    m_Unknown198 = 1;
    dsi_ServerError("World ended");
}

// Detach every client's tracked object (name ours).
void sm_EndWorldC(CServerMgr* pMgr)
{
	// A for loop off pHead->m_pNext: the while form with pCur read from the
	// list member swapped esi/edi in lithtech (0x00475524); server.dll is
	// identical either way.
	LTLink* pHead = &pMgr->m_ClientList.m_Head;
	for (LTLink* pCur = pHead->m_pNext; pCur != pHead; pCur = pCur->m_pNext)
	{
		Client* pClient = (Client*)pCur->m_pData;
		LTObject* pObj = pClient->m_pUnknown3AC;
		if (pObj)
		{
			pObj->m_pSData->m_pOwningClient = LTNULL;
			pClient->m_pUnknown3AC = LTNULL;
		}
	}
}

// ltbasedefs.h's ObjectCreateStruct is used here as the header has it. The
// local view this file used to carry redeclared it with Clear() out of line,
// on the theory that /Ob1 would otherwise expand the SDK's inline body into
// every caller and lose the calls the shipped code makes. The shipped bytes
// say the opposite, and say it in both images:
//
//   * server.dll's PrecacheModels (0x10033e76, which we already match) opens
//     `call 0x10001bd7` and calls it twice more inside the loops -- so /Os
//     DECLINED the inline body. 0x10001bd7 is Clear itself, and it is the
//     header's body exactly: m_CreateFlags=1, the two uint16 zeroes, the two
//     flag words, m_Pos.Init(), m_Scale.Init(1,1,1), m_Rotation.Init(), then
//     the four string heads with MAX_MODEL_TEXTURES=2 rolled as a loop and
//     the two trailing floats as `fldz/fstps` at 0x27c and 0x280 -- which is
//     what pinned the tail as m_NextUpdate and m_fDeactivationTime rather
//     than the eight bytes of pad the view called it.
//   * lithtech.exe's (0x00475670) has that body EXPANDED at the two explicit
//     oc.Clear() sites and CALLED once at the top, because the constructor
//     -- `{ Clear(); }`, small enough for /Ot -- is what got expanded there
//     and the inliner does not recurse into an expansion it just made.
//
// One spelling, both shapes: exactly the /O1-vs-/O2 split. Declaring Clear
// out of line bought server.dll nothing and cost lithtech.exe 292 bytes of
// this function.

// ---- the world's model precache ------------------------------------------ //

// The parts of a resource tree the precache reads. servermgr.h models the
// tree as a pad and one list pointer and the list as three named fields, and
// neither covers what is needed here, so both are reached through views
// (names by offset). The flag word at the list's +0x48 decides which of the
// two shapes the tree holds: bit 3 an indexed table of names, bits 4-5 a
// single name hanging off the tree's own +0x0c.
using CPrecacheTreeView = CServerWorldRec;

// 0x10007e44 — the i'th name out of the table at the list's +0x04, copied
// into the caller's buffer; nonzero if there was one (name ours).
int fs_GetTableName(void* pTable, uint32 iEntry, char* pName, uint32 nMaxLen);

// 0x10033e76 — every file the world's resource trees name gets an object
// made for it, so whatever loading it needs happens now rather than mid
// frame. LTBOOL, not the void servermgr.h declares (`push $1; pop %eax` on
// the good path, `xor %eax,%eax` on every refusal), so it is defined through
// a view of the manager and DoStartWorld calls it through the same view.
#ifndef LT_SERVER_BUILD
void cs_PreLoadWorld(char* pWorldName);     // 0x004148d0 (client only)
#endif

LTRESULT CServerMgr::DoStartWorld(char* requested, uint32 flags, float time) {
    ServerActivity activity(m_Lifetime);
    if (!activity.available()) return LT_NOTINITIALIZED;
    if (!requested || !*requested || (flags & ~uint32(7)) || !std::isfinite(time)) return LT_INVALIDPARAMS;
    if (!m_Initialized || !m_Lifetime->alive || m_Terminating || m_EndingWorld || !m_ClassMgr.m_pServerShell) return LT_NOTINITIALIZED;
    if (m_StartingWorld || m_RunningWorldStart) return LT_ERROR;
    const auto life = m_Lifetime;
    ServerCallbackGuard operation(life);
    struct Transition {
        CServerMgr* manager; ServerCallbackGuard guard;
        bool changed = false, committed = false;
        ~Transition() {
            if (!guard.alive()) return;
            if (changed && !committed && !manager->m_Terminating && !manager->m_EndingWorld) manager->EndWorld(0);
            if (guard.alive()) manager->m_StartingWorld = false;
        }
    } transition{this,operation};
    m_StartingWorld = true;
    try {
        auto name = nolf::platform::asset_key(requested);
        if (name.ends_with(".dat")) name.resize(name.size()-4);
        if (name.empty() || name.size()+4 >= LT_MAX_PATH) return LT_INVALIDPARAMS;
        const auto filename = name + ".dat";
        struct ReleaseStream { void operator()(ILTStream* stream) const { if (stream) stream->Release(); } };
        std::unique_ptr<ILTStream,ReleaseStream> stream(fs_OpenFile(&m_FileMgr,const_cast<char*>(filename.c_str())));
        if (!stream) { SetupError(41,name.c_str()); return LT_MISSINGWORLDFILE; }
        void* file = nullptr;
        if (!fs_GetFileIdentifier(&m_FileMgr,const_cast<char*>(filename.c_str()),1,&file)) {
            SetupError(41,name.c_str()); return LT_MISSINGWORLDFILE;
        }
        bool same = false;
        if ((flags & 4) && m_hWorldFile && m_World.m_bLoaded) {
            const auto* current = fs_GetFilename(&m_FileMgr,m_hWorldFile);
            same = current && nolf::platform::asset_key(current) == filename;
        }
        const auto ticks = nolf::platform::monotonic_nanoseconds();
        const auto seed = static_cast<uint32>(ticks ^ (ticks >> 32));
        srand(seed);
        m_ClassMgr.shell([&](IServerShell& shell) { shell.SRand(seed); });
        if (!operation.unchanged()) return LT_ERROR;
        const bool was_running = m_Unknown198 == 0;
        transition.changed = true;
        m_ClassMgr.shell([&](IServerShell& shell) { shell.PreStartWorld(was_running ? LTTRUE : LTFALSE); });
        if (!operation.unchanged()) return LT_ERROR;
        // Loaded-but-paused worlds own actors too; retire them before replacing
        // their geometry, even if RunWorld has not yet been called.
        if (m_bWorldLoaded || was_running) {
            const auto generation = life->epoch;
            EndWorld(same ? 1 : 0);
            if (!life->alive || life->epoch != generation+1 || !m_Initialized || m_Terminating) return LT_ERROR;
            operation = ServerCallbackGuard(life);
        }
        std::fill(std::begin(m_SkyObjects),std::end(m_SkyObjects),uint16(0xffff));
        m_Time = 0; m_pChangedObjects = nullptr; m_pChangedSounds = nullptr; m_Link83c.TieOff();
#ifndef LT_SERVER_BUILD
        cs_PreLoadWorld(name.data());
        if (!operation.unchanged()) return LT_ERROR;
#endif
        if (same) m_bWorldLoaded = 1;
        else {
            const auto result = LoadWorld(stream.get(),name.data());
            if (!operation.unchanged()) return LT_ERROR;
            if (result != LT_OK) return result;
        }
        m_hWorldFile = file;
        auto result = sm_StartWorldA(this,1);
        if (result != LT_OK) return result;
        if (!PrecacheModels()) return LT_ERROR;
        if (!operation.unchanged()) return LT_ERROR;
        result = sm_LoadWorldObjects(this,stream.get(),name.data(),flags & 1);
        if (!operation.unchanged()) return LT_ERROR;
        if (result != LT_OK) return result;
        m_pWorldStream = stream.release(); m_Unknown324 |= 1;
        m_Unknown6c4 = 0; m_Unknown6bc = time; m_Unknown6c0 = 0;
        m_Unknown6d0 = 0; m_Unknown6c8 = 0; m_Time = 0; m_Unknown6b8 = 0;
        m_Unknown6d8 = time; m_FrameTime = 0;
        if (flags & 2) {
            result = DoRunWorld();
            if (!operation.unchanged()) return LT_ERROR;
            if (result != LT_OK) return result;
        }
        transition.committed = true;
        dsi_ServerError("Loaded world: %s",name.c_str());
        return LT_OK;
    } catch (const std::exception& error) {
        nolf::platform::log("world startup",error.what());
        return LT_ERROR;
    }
}

LTBOOL CServerMgr::Init() {
    if (m_Initialized) return LTTRUE;
    if (!m_Lifetime->alive || m_Terminating) return LTFALSE;
    SetupGlobals();
    try {
        if (un390_Init(&m_Unknown390,0) != 0) throw std::runtime_error("native object manager initialization failed");
        m_Unknown804 = new CMessageObjectHelper(this);
        m_PlaceholderModelOwner = std::make_shared<Model>(&g_DefAlloc,&g_DefAlloc);
        m_pPlaceholderModel = m_PlaceholderModelOwner.get();
        m_pUnknown6a8 = new ServerObjectMover(this);
        m_Unknown210.Init(&m_Unknown390);
        m_NetMgr.SetNetName("SERVER_PLAYER"); m_NetMgr.m_pHandler = this;
        m_Link83c.Init2(nullptr); m_Link8c4.Init2(nullptr); m_Link8d0.Init2(nullptr);
        m_Unknown8fc.SetCacheSize(100);
        m_Unknown8b8 = 200; m_Unknown8bc = 0;
        m_Unknown198 = 1; m_ServerFlags = m_Unknown1a0 = 0;
        m_Unknown7f4 = m_Unknown7f8 = m_Unknown7fc = m_Unknown800 = 0;
        m_CachedFiles.clear();
        m_pChangedObjects = nullptr; m_pChangedSounds = nullptr;
        m_pAppHandler = nullptr; m_Properties.reset(); m_ErrorCode = LT_OK; m_ErrorString[0] = 0;
        m_FrameTime = m_UpdatesPerSec = m_Unknown6b8 = m_Unknown6bc = m_Unknown6c0 = m_Unknown6c8 = m_Time = 0;
        m_Unknown6c4 = m_Unknown6dc = 0; m_Unknown6d0 = 0; m_Unknown6d4 = m_Unknown6d8 = 0;
        m_bWorldLoaded = 0; m_hWorldFile = nullptr; m_pCollisionInfo = nullptr;
        std::fill(std::begin(m_SkyObjects),std::end(m_SkyObjects),uint16(0xffff)); m_hGlobalLightObject = nullptr;
        if (!sb_Init2(&m_ObjectListBank,sizeof(ObjectList),16,4) ||
            !sb_Init2(&m_ObjectLinkBank,sizeof(ObjectLink),128,64)) throw std::bad_alloc();
        m_MessageBank.m_Bank.m_CacheSize = 64;
        m_hHash8c0 = dhash_Create(500,DHASHKEY_STRING); m_hModelHash = dhash_Create(100,DHASHKEY_FILENAME);
        if (!m_hHash8c0 || !m_hModelHash) throw std::bad_alloc();
        fs_Init(&m_FileMgr,this); m_FileMgr.set_notification(se_NotifyFileRegistered);
        cc_InitState(this,&m_ConsoleState);
        if (!m_ConsoleState.m_Initialized) throw std::bad_alloc();
        cc_LoadConfigFile(&m_ConsoleState,const_cast<char*>("s_autoexec.cfg"),2,0);
        if (!m_ClassMgr.Init(this)) throw std::runtime_error("native class manager initialization failed");
        m_pUnknown928 = sm_CreateUnknown928(this);
        if (!m_pUnknown928) throw std::bad_alloc();
        sm_ResetProfileCounters();
        // Publication and callbacks use fully initialized native services.
        if (!m_Thread.Init(this) || m_Thread.Start(3) != LT_OK) throw std::runtime_error("native server worker initialization failed");
        m_Initialized = true; return LTTRUE;
    } catch (const std::exception& error) {
        nolf::platform::log("server initialization",error.what()); Term(); return LTFALSE;
    }
}

LTRESULT CServerMgr::PlaySound(PlaySoundInfo* pPlaySoundInfo, HLTSOUND& hResult)
{
    hResult = LTNULL;
	SoundInstance* pSound = LTNULL;
	std::shared_ptr<const SoundTrack> pTrack;
	char* pSoundName;
	void* hFile;

	if (!pPlaySoundInfo)
		return LT_INVALIDPARAMS;
    pPlaySoundInfo->m_hSound = LTNULL;
    const auto validRequest = sound_NormalizeRequest(*pPlaySoundInfo);
    if (validRequest != LT_OK) return validRequest;
    if ((pPlaySoundInfo->m_dwFlags & (PLAYSOUND_ATTACHED | PLAYSOUND_CLIENTLOCAL)) &&
        (!pPlaySoundInfo->m_hObject || !m_Unknown390.m_Allocated.contains(pPlaySoundInfo->m_hObject))) return LT_INVALIDPARAMS;

	// A world must be up, and an attached sound must name its object.
	if (!m_pWorldFile)
		return LT_ERROR;

	if ((pPlaySoundInfo->m_dwFlags & PLAYSOUND_ATTACHED) && !pPlaySoundInfo->m_hObject)
		return LT_ERROR;

	pSoundName = pPlaySoundInfo->m_szSoundName;
	if (!fs_GetFileIdentifier(&m_FileMgr, pSoundName, 0, &hFile))
	{
		if (g_DebugLevel >= 2)
			dsi_ServerError("Missing sound file %s", pSoundName);

		return LT_MISSINGFILE;
	}


	if (pPlaySoundInfo->m_dwFlags & (PLAYSOUND_TIMESYNC | PLAYSOUND_TIME |
	    PLAYSOUND_GETHANDLE | PLAYSOUND_ATTACHED | PLAYSOUND_LOOP))
	{
		// The server has to keep this one.
		if (pPlaySoundInfo->m_dwFlags & PLAYSOUND_ATTACHED)
			pPlaySoundInfo->m_dwFlags &= ~PLAYSOUND_CLIENTLOCAL;

		// A kept, non-looping timed sound needs its length data up front.
		if ((pPlaySoundInfo->m_dwFlags & (PLAYSOUND_TIMESYNC | PLAYSOUND_TIME | PLAYSOUND_ATTACHED)) &&
		    !(pPlaySoundInfo->m_dwFlags & PLAYSOUND_LOOP) &&
		    !(pTrack = AddCachedFile(static_cast<CFileRef*>(hFile))))
		{
			return LT_ERROR;
		}

		pSound = m_SoundInstances.Allocate();
		if (!pSound)
			return LT_ERROR;

		if (pPlaySoundInfo->m_dwFlags & PLAYSOUND_GETHANDLE)
			pPlaySoundInfo->m_hSound = (HLTSOUND)pSound;
		else
			pPlaySoundInfo->m_hSound = LTNULL;

		if (!pSound->Init(*this, pPlaySoundInfo, m_Time, static_cast<CFileRef*>(hFile), pTrack))
		{
			pPlaySoundInfo->m_hSound = LTNULL;

			pSound->Term();
			m_SoundInstances.Free(pSound);
			return LT_ERROR;
		}

		dl_AddHead(&m_List54, &pSound->m_Link, pSound);
	}
	else
	{
		// Fire and forget: queue the play for the clients and be done.
        try {
            auto record = std::make_shared<SoundRecord>();
            record->file_name = static_cast<CFileRef*>(hFile)->m_Name;
            record->file_id = static_cast<CFileRef*>(hFile)->m_nFileID;
            record->m_Info = *pPlaySoundInfo;
            if (record->m_Info.m_dwFlags & PLAYSOUND_CLIENTLOCAL)
                record->m_Info.m_vPosition = record->m_Info.m_hObject->m_Pos;
            if (!client_QueueSound(m_ClientList,std::move(record))) return LT_OUTOFMEMORY;
        } catch (const std::bad_alloc&) { return LT_OUTOFMEMORY; }
	}

	// pSound is still LTNULL on the fire-and-forget path.
	hResult = (HLTSOUND)pSound;
	return LT_OK;
}

LTBOOL CServerMgr::Update(long flags, float requested_time) {
    ServerActivity activity(m_Lifetime);
    const float rate = g_UpdatesPerSec;
    if (!activity.available() || !m_Initialized || m_Terminating || m_EndingWorld || m_StartingWorld || m_Updating ||
        !std::isfinite(requested_time) || !std::isfinite(rate) || rate <= 0 || rate > 10000 ||
        !std::isfinite(m_Time) || !std::isfinite(m_Unknown6d8) || !std::isfinite(m_Unknown6bc) || !std::isfinite(m_Unknown6c0)) return LTFALSE;
    const double adjusted = static_cast<double>(requested_time)+m_Unknown6b8;
    if (!std::isfinite(adjusted) || std::fabs(adjusted) > std::numeric_limits<float>::max()) return LTFALSE;
    const float time = static_cast<float>(adjusted);
    ServerCallbackGuard operation(m_Lifetime);
    auto interrupted = [&]() -> LTBOOL {
        return operation.alive() && activity.available() && m_Initialized && m_bWorldLoaded && !m_Terminating ? LTTRUE : LTFALSE;
    };
    struct Updating {
        CServerMgr* manager; ServerCallbackGuard guard;
        ~Updating() { if (guard.alive()) manager->m_Updating = false; }
    } updating{this,operation};
    m_Updating = true;
    try {
        SetupGlobals();
        ProcessThreadMessages();
        if (!operation.unchanged()) return interrupted();
        const double elapsed = static_cast<double>(time)-m_Unknown6d8;
        if (!std::isfinite(elapsed)) return LTFALSE;
        // Preserve the modern game's 10ms minimum tick interval. Network
        // processing continues on every call, including paused/demo waits.
        const bool paused = (flags & 1) || (m_ServerFlags & 1);
        const bool demo_wait = (m_Unknown1a0 & 4) && !(m_Unknown1a0 & 8) && !m_Unknown6dc;
        const bool tick = m_Unknown198 == 0 && !paused && !demo_wait && (elapsed >= .01 || elapsed < 0);
        if (m_Unknown198 != 0 || paused || tick) m_Unknown6d8 = time;
        m_Unknown6d4 = static_cast<float>(std::clamp(elapsed,-double(std::numeric_limits<float>::max()),double(std::numeric_limits<float>::max())));
        m_NetMgr.Update(const_cast<char*>("Server: "),time,1);
        if (!operation.unchanged()) return interrupted();
        g_Unknown585a0 = 0; g_Unknown5872c = g_Unknown58728 = 0;
        g_nIsectQueries = g_nIsectTime = g_FindSphereTicks = g_FindSphereCount = 0;
        const auto packets = sm_ProcessPackets(this);
        if (!operation.unchanged()) return interrupted();
        if (packets) return LTFALSE;
        if (m_Unknown198 == 0) {
            if (tick) {
                if (rate != m_UpdatesPerSec) {
                    m_Unknown6c4 = 0; m_Unknown6c8 = m_Time;
                    m_Unknown6bc = m_Unknown6c0; m_Unknown6d0 = 0;
                }
                m_UpdatesPerSec = rate;
                const double inverse = 1.0/double(m_UpdatesPerSec);
                const double previous = double(m_Unknown6bc)+double(m_Unknown6c4)*inverse;
                const double updates = std::max(0.0,(double(time)-previous)/inverse);
                // Preserve serialized counter widths without float conversion
                // overflow or a multi-year counter rollover corrupting time.
                if (updates > double(UINT32_MAX-m_Unknown6c4)) {
                    m_Unknown6c4 = 0; m_Unknown6bc = time; m_Unknown6c0 = time;
                } else {
                    m_Unknown6c4 += static_cast<uint32>(updates);
                    m_Unknown6c0 = static_cast<float>(double(m_Unknown6bc)+double(m_Unknown6c4)*inverse);
                }
                m_FrameTime = static_cast<float>(std::clamp(elapsed,.01,.2));
                const double next_time = double(m_Time)+m_FrameTime;
                if (!std::isfinite(next_time) || std::fabs(next_time) > std::numeric_limits<float>::max()) return LTFALSE;
                m_Time = static_cast<float>(next_time);
                if ((m_Unknown1a0 & 4) && !(m_Unknown1a0 & 8)) --m_Unknown6dc;
                UpdateSounds(m_FrameTime);
                if (!operation.unchanged()) return interrupted();
                if (m_ClassMgr.m_pServerShell) m_ClassMgr.shell([&](IServerShell& shell) { shell.Update(m_FrameTime); });
                if (!operation.unchanged()) return interrupted();
                UpdateObjects();
                if (!operation.unchanged()) return interrupted();
                m_Unknown6d0 = std::bit_cast<int32>(std::bit_cast<uint32>(m_Unknown6d0)+uint32(1));
                m_Unknown6d4 = 0;
            }
            sm_FlushObjectQueue(this);
            if (!operation.unchanged()) return interrupted();
            RemoveDeadSounds();
            if (!operation.unchanged()) return interrupted();
        } else {
            // File downloads and client state still progress between worlds.
            sm_UpdateClients(this);
            if (!operation.unchanged()) return interrupted();
        }
        if (g_ShowGameTime) dsi_ServerError("Game time: %.2f",m_Time);
        if (g_ShowTickCounts) {
            dsi_ServerError("ILTServer::FindObjectsTouchingSphere ticks: %u",g_FindSphereTicks);
            dsi_ServerError("ILTServer::FindObjectsTouchingSphere count: %u",g_FindSphereCount);
        }
        return LTTRUE;
    } catch (const std::exception& error) {
        nolf::platform::log("server update",error.what());
        return LTFALSE;
    }
}

// The seven packet handlers 0x10029ee7 installs. Four are already written
// under names of their own, with LTRESULT returns or an HCLIENT third
// argument, so the table stores cast them to the table's signature (the
// cast is free: each store is `movl $fn,slot`):
//   7  0x10029fb6  sm_ReadClientCommands (below)
//   8  0x10029f42  sm_Unknown29f42 (below)
//   10 0x1002a0f2  sub_1002a0f2 (below)
//   11 0x1002a11e  sm_GiveMessageToShell (below; Client* passed as HCLIENT)
int sub_1002a193(CServerMgr* pMgr, ServerMessage* pMsg, Client* pClient);
int sub_1002a0d0(CServerMgr* pMgr, ServerMessage* pMsg, Client* pClient);
LTRESULT sm_ReadClientCommands(CServerMgr* pMgr, ServerMessage* pMsg,
                               Client* pClient);
LTRESULT sm_Unknown29f42(CServerMgr* pMgr, ServerMessage* pMsg,
                         Client* pClient);
int sub_1002a167(CServerMgr* pMgr, ServerMessage* pMsg, Client* pClient);
int sub_1002a0f2(CServerMgr* pMgr, ServerMessage* pMsg, Client* pClient);
LTRESULT sm_GiveMessageToShell(CServerMgr* pMgr, ServerMessage* pMsg,
                               HCLIENT hClient);

// 0x10029ee7 — clear the packet-handler table and install the seven handlers
// the server has. Another declaration in servermgr.h whose name does not
// match what the body does (there are no profile counters here); kept.
//
// Two things read off the bytes. The memset covers 0x400 bytes, so the table
// is 256 entries wide and not the 64 servermgr.h declares — the dispatch
// masks the id with 0x3f, so only the first 64 are ever reached, but the
// array is four times that. And the store order is 6, 7, 8, 10, 11, 9, 5,
// which is source order: VC6 keeps independent stores where it found them.
//
// IT SITS HERE, above UpdateObjects, for the memset alone. Neither build
// names an intrinsic setting for it: /O1 has no /Oi so the memset is the
// library call server.dll makes (0x10029ef3), /O2 has /Oi so it is the
// `rep stos` lithtech.exe has (0x00468f2d). Below UpdateObjects it would be
// inside the `#pragma intrinsic(memset)` region that function needs, and
// the `#pragma function(memset)` that closes that region for /O1's sake
// also closes it for /O2's, which is what cost this function there.
void sm_ResetProfileCounters()
{
	std::fill(std::begin(g_PacketHandlers),std::end(g_PacketHandlers),nullptr);

	g_PacketHandlers[6]  = sub_1002a0d0;
	g_PacketHandlers[7]  = [](CServerMgr* manager, ServerMessage* packet, Client* client) { return static_cast<int>(sm_ReadClientCommands(manager,packet,client)); };
	g_PacketHandlers[8]  = [](CServerMgr* manager, ServerMessage* packet, Client* client) { return static_cast<int>(sm_Unknown29f42(manager,packet,client)); };
	g_PacketHandlers[10] = sub_1002a0f2;
	g_PacketHandlers[11] = [](CServerMgr* manager, ServerMessage* packet, Client* client) { return static_cast<int>(sm_GiveMessageToShell(manager,packet,client ? client->Handle() : nullptr)); };
	g_PacketHandlers[9]  = sub_1002a167;
	g_PacketHandlers[5]  = sub_1002a193;
}

// The shipped UpdateObjects pads the class name with an inlined rep-stos
// memset while PlaySound's PLAYSOUNDINFO_COPY stays a memcpy call, so the
// pragma names memset alone.
#pragma intrinsic(memset)

// NEAR MATCH (874 of 868 bytes, 251 of 250 instructions, 3 mnemonic blocks).
// The never-ticked retire block now sits out of line after the footer
// prints, behind the long je at 0x10032a57 with its `jmp` back at
// 0x10032b65, as shipped: the j-loop body is ONE nested positive guard
// (`if (!printed) { if (ticks) {...} else retire; }`) instead of two
// `continue`s. The only residual is strlen's result: shipped keeps it in eax
// into the intrinsic-memset padding (0x10032abd), ours copies it to edx.
// That extra `mov %eax,%edx` is the whole 1-instruction delta, and its two
// bytes push the `if (pBest)` skip (short `je` at 0x10032a91) past rel8
// range, which is the other four bytes. Probed without effect: nLen at
// function scope / top of the i-loop block, int with unsigned compare,
// sizeof bound, named pad count and/or pointer, assignment in the test,
// `sName + nLen`, reusing nLen (or nTicks) for the average. lithtech.exe
// shows the same eax-vs-edx choice (0x00473e4a). Allocation.
//
// lithtech.exe 0x00473bc0: 947 of 947, size-exact, 2 blocks, and the only
// residual is that same strlen register (shipped eax, ours edx). What made it
// size-exact: no tick counter at all in that build (see below), the census
// written through pStats (0x00473cfc loads the class chain before the
// index), and m_Ticks read at each use in the averages (0x00473d48 loads it
// after the m_nFrames store). Also probed for lithtech without effect: int
// nLen, `sName + nLen`, a named pad count, `30 > nLen`; strlen written out
// at each use is not CSE'd (969 bytes).
void CServerMgr::UpdateObjects() {
    ServerActivity activity(m_Lifetime);
    if (!activity.available() || !m_Initialized || m_Terminating || m_EndingWorld) return;
    struct Context { CServerMgr* manager; ServerCallbackGuard guard; } context{this,ServerCallbackGuard(m_Lifetime)};
    for (int index = 0; index < m_ClassMgr.m_nClasses; ++index) {
        auto& stats = m_ClassMgr.m_pClassStats[index];
        stats.m_Ticks = stats.m_nUpdated = stats.m_nObjects = stats.m_bPrinted = 0;
        if (stats.m_nFrames >= 600) stats.m_nFrames = stats.m_TotalTicks = stats.m_MaxTicks = 0;
    }
    if (!object_Visit(m_List8dc,[](void* pointer,LTObject& object) {
        auto& context = *static_cast<Context*>(pointer);
        if (!context.guard.unchanged()) return false;
        auto& manager = *context.manager;
        if (object.m_ChangeFlags & 0x38) return false;
        if (!(object.m_ChangeFlags & 4) || !object.m_pSData) return true;
        ObjectCallbackGuard actor(object);
        if (g_bAutoDeactivate && object.m_pSData->m_UpdateTimer > 0) {
            object.m_pSData->m_UpdateTimer -= manager.m_FrameTime;
            if (object.m_pSData->m_UpdateTimer <= 0) object.m_ChangeFlags |= 0x200;
        }
        auto* definition = object.m_pSData->m_pClass;
        CBaseCounter timer(0); timer.Start();
        sm_UpdateObject(&manager,&object);
        const auto ticks = timer.ReadRaw();
        if (!context.guard.unchanged()) return false;
        if (actor.unchanged() && definition && object.m_pSData->m_pClass == definition) {
            auto* stats = static_cast<CClassStats*>(definition->m_pInternal[manager.m_ClassMgr.m_nIndex]);
            if (stats) { stats->m_Ticks += ticks; ++stats->m_nUpdated; }
        }
        return true;
    },&context) || !context.guard.unchanged()) return;
    uint32 total_ticks = 0;
    if (!g_ShowClassTicks) {
        for (int index = 0; index < m_ClassMgr.m_nClasses; ++index) {
            auto& stats = m_ClassMgr.m_pClassStats[index];
            total_ticks += stats.m_Ticks; stats.m_nFrames = stats.m_TotalTicks = 0;
        }
        g_TotalTicks = std::bit_cast<int32>(total_ticks);
        return;
    }
    std::size_t objects = 0;
    auto* head = &m_List8dc.m_Head;
    for (auto* link = head->m_pNext; link != head; link = link->m_pNext) {
        auto* object = static_cast<LTObject*>(link->m_pData);
        if (!object->m_pSData || !object->m_pSData->m_pClass) continue;
        auto* stats = static_cast<CClassStats*>(object->m_pSData->m_pClass->m_pInternal[m_ClassMgr.m_nIndex]);
        if (stats) { ++stats->m_nObjects; ++objects; }
    }
    struct Row { std::string name; uint32 objects, updated, average, maximum, ticks; };
    std::vector<Row> rows;
    uint32 updated = 0;
    for (int index = 0; index < m_ClassMgr.m_nClasses; ++index) {
        auto& stats = m_ClassMgr.m_pClassStats[index];
        ++stats.m_nFrames; stats.m_TotalTicks += stats.m_Ticks;
        stats.m_MaxTicks = std::max(stats.m_MaxTicks,stats.m_Ticks);
        if (!stats.m_TotalTicks) continue;
        rows.push_back({stats.m_pClassDef->m_ClassName,stats.m_nObjects,stats.m_nUpdated,
            stats.m_TotalTicks/stats.m_nFrames,stats.m_MaxTicks,stats.m_Ticks});
        total_ticks += stats.m_Ticks; updated += stats.m_nUpdated;
    }
    g_TotalTicks = std::bit_cast<int32>(total_ticks);
    std::stable_sort(rows.begin(),rows.end(),[](const Row& first,const Row& second) { return first.average < second.average; });
    // No registry pointers survive into diagnostic callbacks.
    dsi_PrintString("Class                (OBJECTS) Updated   Average ticks   Max   Frame");
    for (const auto& row : rows)
        dsi_ServerError("%-30.30s| %3u | %3u | %6u | %6u | %6u",row.name.c_str(),row.objects,row.updated,row.average,row.maximum,row.ticks);
    dsi_ServerError("FRAME TOTALS: objects %zu, updated %u, ticks %u",objects,updated,total_ticks);
}

void CServerMgr::UpdateSounds(float fFrameTime)
{
	LTLink* pCur = m_List54.m_Head.m_pNext;
	while (pCur != &m_List54.m_Head)
	{
		SoundInstance* pSound = (SoundInstance*)pCur->m_pData;
		if (!pSound)
			break;

		LTLink* pNext = pCur->m_pNext;

		// A killed sound stops timing unless something still owns it.
		if (!pSound->m_bKill || pSound->m_ListenerCount)
			pSound->UpdateTimer(fFrameTime);

		pCur = pNext;
	}
}

void sm_UpdateClients(CServerMgr* manager) {
    struct Context { CServerMgr* manager; ServerCallbackGuard guard; } context{manager,ServerCallbackGuard(manager->m_Lifetime)};
    client_Visit(manager->m_ClientList,[](void* context, Client& client) {
        auto& state = *static_cast<Context*>(context);
        if (state.guard.unchanged()) sm_UpdateClient(state.manager,&client);
    },&context);
}

void CServerMgr::RemoveDeadSounds()
{
	LTLink* pCur = m_List54.m_Head.m_pNext;
	while (pCur != &m_List54.m_Head)
	{
		SoundInstance* pSound = (SoundInstance*)pCur->m_pData;
		if (!pSound)
			break;

		LTLink* pNext = pCur->m_pNext;

		if (pSound->m_bKill && pSound->m_Unknown2c == 0 && !pSound->m_ListenerCount)
		{
			// An attached sound tells its object it is going away.
			if (pSound->m_PlaySoundFlags & PLAYSOUND_ATTACHED)
			{
				if (pSound->GetObject())
				{
					LTObject* pObject = pSound->GetObject();
					if (pObject)
						sm_DetachSound(this, pObject, pSound, 1);
				}
			}

			dl_RemoveAt(&m_List54, &pSound->m_Link);

			pSound->Term();

			m_SoundInstances.Free(pSound);
		}

		pCur = pNext;
	}
}

// Return a queue link before running game code: destructors may drain or
// append to this same queue, and may retire its other object identities.
static void sm_UnqueueObject(CServerMgr* manager, LTObject* object) {
    auto* head = &manager->m_Link83c;
    for (auto* link = head->m_pNext; link != head;) {
        auto* next = link->m_pNext;
        if (link->m_pData == object) {
            dl_Remove(link); link->m_pPrev = SM_FREE_LINKS; SM_FREE_LINKS = link;
        }
        link = next;
    }
}
void sm_DrainObjectQueue(CServerMgr* manager)
{
    ServerCallbackGuard server(manager->m_Lifetime);
    while (manager->m_Link83c.m_pNext != &manager->m_Link83c) {
        auto* link = manager->m_Link83c.m_pNext;
        auto* object = static_cast<LTObject*>(link->m_pData);
        sm_UnqueueObject(manager,object);
        if (manager->m_Unknown390.m_Allocated.contains(object) && object->m_pSData && object->m_pSData->m_pBaseClass)
            sm_RemoveObjectFromWorld(manager,object->m_pSData->m_pBaseClass);
        if (!server.unchanged()) return;
    }
}

void sm_ClearChangedObjects(CServerMgr* pMgr)
{
	LTObject* pObject = pMgr->m_pChangedObjects;
	while (pObject)
	{
		pObject->m_pSData->m_ClientChangeFlags = 0;
		pObject = pObject->m_pSData->m_pNextChanged;
	}

	pMgr->m_pChangedObjects = LTNULL;
}

void sm_ClearChangedSounds(CServerMgr* pMgr)
{
	SoundInstance* pSound = pMgr->m_pChangedSounds;
	while (pSound)
	{
		pSound->m_Unknown2c = 0;
		pSound = pSound->m_pNextChanged;
	}

	pMgr->m_pChangedSounds = LTNULL;
}

// The walk is a `for` whose cursor is initialised THROUGH a named head, and
// both halves of that matter. All three callee-saved registers are spoken for
// (pMgr, the cursor, and the inner loops' scratch), so the head cannot live in
// one and VC6 re-materialises `lea 0x7e8(%edi),%eax` in the preheader and
// again in the latch — the six-byte-bigger `for` shape of the rule
// "`for` rematerialises a loop invariant into both predecessors" — and
// initialising the cursor off the head instead of off the list rotates the
// pair the shipped way (pMgr in edi, the cursor in esi). Written as
// `pCur = pMgr->m_ClientList.m_Head.m_pNext; while (pCur != &...m_Head)` the
// instructions are identical and pMgr/pCur are swapped in every one of them.
void sm_ApplyChangeFlags(CServerMgr* pMgr)
{
	LTLink* pCur;
	LTLink* pHead;
	Client* pClient;
	LTObject* pObject;
	SoundInstance* pSound;

	pHead = &pMgr->m_ClientList.m_Head;

	for (pCur = pHead->m_pNext; pCur != pHead; pCur = pCur->m_pNext)
	{
		pClient = (Client*)pCur->m_pData;
		if (pClient->m_State == 3)
		{
			pObject = pMgr->m_pChangedObjects;
			while (pObject)
			{
				pClient->m_ObjInfos[pObject->m_ObjectID].m_ChangeFlags |=
					pObject->m_pSData->m_ClientChangeFlags;
				pObject = pObject->m_pSData->m_pNextChanged;
			}

			// The sound's index is the same uint32 at +8 that
			// sm_SetSoundTrackChangeFlags reads, copied into a uint32 local
			// and narrowed with a (uint16) cast at the subscript. /O1 folds
			// that into one `movzwl` off memory (0x10032cb9); /O2 keeps the
			// full dword load and zero-extends the register with
			// `and $0xffff` (0x004740ae). Measured alternatives: a bare
			// `& 0xffff` mask gives mov/and on BOTH images (server near);
			// a `(uint16)` cast on the field itself, or a uint16 local,
			// makes /O2 narrow the load to `mov 8(%ecx),%cx` (lithtech near).
			pSound = pMgr->m_pChangedSounds;
			while (pSound)
			{
				uint32 nIndex = pSound->m_pUnknown0c->m_nIndex;
				pClient->m_ObjInfos[(uint16)nIndex].m_ChangeFlags |= pSound->m_Unknown2c;
				pSound = pSound->m_pNextChanged;
			}
		}
	}

	sm_ClearChangedObjects(pMgr);
	sm_ClearChangedSounds(pMgr);
}

void sm_SendClientUpdates(CServerMgr* pMgr)
{
    struct Context { CServerMgr* manager; ServerCallbackGuard guard; } context{pMgr,ServerCallbackGuard(pMgr->m_Lifetime)};
    client_Visit(pMgr->m_ClientList,[](void* opaque, Client& client) {
        auto& state = *static_cast<Context*>(opaque);
        if (state.guard.unchanged()) sm_WriteClientUpdate(state.manager,&client);
    },&context);
    if (!context.guard.unchanged()) return;

	pMgr->m_Unknown7fc = 0;
	pMgr->m_Unknown800 = 0;
}

void sm_FlushObjectQueue(CServerMgr* pMgr)
{
	ServerCallbackGuard operation(pMgr->m_Lifetime);
	sm_UpdateClients(pMgr);
	if (!operation.unchanged()) return;
	sm_ApplyChangeFlags(pMgr);

	// Processing a queued object can queue more, so drain until dry.
	while (pMgr->m_Link83c.m_pNext != &pMgr->m_Link83c) {
		sm_DrainObjectQueue(pMgr);
		if (!operation.unchanged()) return;
	}

	sm_SendClientUpdates(pMgr);
}

void sm_UpdateClient(CServerMgr* manager, Client* client) {
    if (!client || client->m_Removing) return;
    const auto lifetime = client->m_Lifetime;
    ServerCallbackGuard server(manager->m_Lifetime);
    if (client->m_State == 1) sm_SetClientState(manager,client,3);
    else if (client->m_State == 2) sm_UpdateClientState2(manager,client);
    if (server.unchanged() && *lifetime && !client->m_Removing) sm_FlushClientPackets(manager,client);
}

int sm_ProcessPackets(CServerMgr* manager) {
    ServerCallbackGuard operation(manager->m_Lifetime);
    CMessageRef message;
    CAbstractIO* io = nullptr;
    message.Init(msg_AllocPacket(&io,0x1000,0x1000));
    if (io) io->Release();
    if (!message.m_pMsg) return 1;
    manager->m_NetMgr.StartGettingPackets();
    struct Finish {
        CServerMgr* manager; ServerCallbackGuard guard;
        ~Finish() { if (guard.alive()) manager->m_NetMgr.EndGettingPackets(); }
    } finish{manager,operation};
    while (manager->m_NetMgr.GetPacket(message.m_pMsg)) {
        if (!operation.unchanged()) return 1;
        if (!message.m_pMsg->m_DataLen) continue;
        const auto packet_id = message.m_pMsg->m_pData[0] & 0x3f;
        auto* client = message.m_pMsg->m_pConnection ? sm_FindClientByConnection(manager,message.m_pMsg->m_pConnection) : nullptr;
        if (auto handler = g_PacketHandlers[packet_id]) {
            const auto result = handler(manager,message.m_pMsg,client);
            if (!operation.unchanged()) return 1;
            if (result) return result;
        } else if (client) {
            xfer_HandleMessage(client->m_FileTransfer.get(),message.m_pMsg);
            if (!operation.unchanged()) return 1;
        }
    }
    return operation.unchanged() ? 0 : 1;
}

Client* sm_FindClientByConnection(CServerMgr* pMgr, NetConnection* pConn)
{
	LTLink* pCur = pMgr->m_ClientList.m_Head.m_pNext;
	while (pCur != &pMgr->m_ClientList.m_Head)
	{
		Client* pClient = (Client*)pCur->m_pData;
		if (pClient->m_pConnection == pConn)
			return pClient;

		pCur = pCur->m_pNext;
	}

	return LTNULL;
}

int sm_SetClientState(CServerMgr* manager, Client* client, int state) {
    if (!manager || !client) return 0;
    if (client->m_State == state) return 1;
    const auto lifetime = client->m_Lifetime;
    const auto previous = client->m_State;
    if (state == 3) {
        if (client->m_Removing || !sm_PrepClientEnterWorld(manager,client)) return 0;
        const auto result = sm_ClientEnterWorld2(manager,client);
        return *lifetime && result == LT_OK;
    }
    if (previous == 3) sm_ClientExitWorld(manager,client);
    if (!*lifetime) return 0;
    if (client->m_State == previous) client->m_State = state;
    return 1;
}

// Both defined at the foot of this file, under the signatures the shipped
// bodies actually have rather than the ones servermgr.h declares.
LTBOOL sm_FinishUpdatePacket(CUpdateContext* pCtx, class ServerMessage* pMsg,
                             int nA, int nThreshold);                       // 0x100278ad

// 0x100285de — whether this client's update can be skipped this frame: a
// backed-up queue either way, or the net layer saying it is saturated.
//
// Defined here rather than after its caller, and `inline`: lithtech.exe's
// sm_WriteClientUpdate opens with this body expanded (the connection test,
// both queue depths against g_MaxQueueSize with `ja`, then the ack-timeout
// call) where server.dll calls 0x100285de. VC6's inliner only looks forward,
// so the original had it above the caller and marked inline; /Os declines the
// expansion -- which is why server.dll still has both the call and the
// out-of-line copy -- and /Ot takes it.
inline int sm_SkipClientUpdate(Client* pClient)
{
	NetConnection* pConn = pClient->m_pConnection;

	if (pConn)
	{
		if (pConn->m_GuaranteedQueue.m_nMessages > g_MaxQueueSize)
			return 1;

		if (pConn->m_SendQueue.m_nMessages > g_MaxQueueSize)
			return 1;

		if (pConn->CheckAckTimeout())
			return 1;
	}

	return 0;
}

// 0x10024cac and 0x10024cad — what a visibility query does by default: a
// one-byte `ret`, and a predicate that answers yes. Declared here and
// DEFINED further down, beside the gather that is their reason to exist;
// only their addresses are needed above.
static void vq_Ignore(void* pUser, void* pObject);
static void vq_IgnoreToo(void* pUser, void* pObject);
static int  vq_Accept(void* pUser, void* pObject);

// 0x100283c6 — a query centred nowhere with no radius, every slot defaulted.
// The caller fills in the two callbacks and the context afterwards; what the
// constructor leaves is the shape those go into.
//
// Defined here rather than beside the callbacks, and `inline`, for exactly
// the reason sm_SkipClientUpdate above is: all THREE of its users --
// sm_WriteClientUpdate (0x00465639), sm_SendSFXMessage (0x00476929) and
// sm_PingObjects -- carry the body EXPANDED in lithtech.exe, the two
// function-pointer immediates 0x00491b40 and 0x00432030 and all, while
// server.dll calls 0x100283c6 at every one of them (sm_SendSFXMessage's
// `lea -0x5c(%ebp),%ecx; call` at 0x10034a73). A constructor defined
// out-of-class is the one shape /Ob1 never expands, and VC6's inliner only
// looks forward, so the original had it above its first caller and marked
// inline; /Os declines a 57-byte body and /Ot takes it.
//
// The position is zeroed by `LTVector::Init()` (all three parameters
// default to 0.0f), not by three component assignments, and that is the
// whole difference between this and the near it used to be: **an inlined
// member function's body is a scheduling barrier**. Written as three
// separate `= 0.0f` statements, VC6 treats all four float stores as one
// group and pulls two of the following integer stores into each `fldz`'s
// shadow (two fillers per pair, evenly spread). With Init() the three
// position stores are a unit no integer store may enter, so they come out
// back-to-back exactly as shipped and only the radius store gets fillers.
// Statement order is *not* the lever -- moving the radius assignment
// anywhere in the list produces identical bytes either way.
#ifndef LT_SERVER_BUILD
// sm_WriteObjectUpdate's body, `inline` for lithtech.exe (see the note over
// sub_10027918 further down). Defined HERE, above both of its callers,
// because VC6's inliner is forward-only: sm_WriteClientUpdate's forced-update
// loop carries this body expanded too (0x00465b1d..0x00465c4c -- the id-list
// grow through dalloc/dfree at 0x00465b5a/0x00465b8c, the size estimate,
// then 0x00464840, 0x00467540, 0x00464da0 and the 0x1000 store), where
// server.dll calls sm_WriteObjectUpdate (0x100279d9).
int sub_100291c7(CServerMgr* pMgr, Client* pClient, LTObject* pObject,
                  CObjInfo* pInfo, class ServerMessage* pMsg);      // 0x100291c7
void sub_10027ad0(CUpdateContext* pCtx, LTObject* pObject,
                  CObjInfo* pInfo);                                 // 0x10027ad0

inline void sm_WriteObjectUpdateInline(CUpdateContext* pCtx, LTObject* pObject,
                                       CObjInfo* pInfo)
{
	uint16 nSize;
	uint16 nFlags;

	if (pInfo->m_ChangeFlags & 0x1000)
		return;

	if (!g_pFillIDList->Append(pObject->m_ObjectID)) return;

	nSize = 100;

	nFlags = pInfo->m_ChangeFlags;

	if ((nFlags & 1) && pObject->m_pSData->m_SFXMessage.m_pMsg)
	{
		nSize = ((ServerMessage*)pObject->m_pSData->m_SFXMessage.m_pMsg)->m_Unknown34
			+ 100;
	}

	if (nFlags & 0x100)
	{
		Attachment* pAttach = pObject->m_pAttachments;

		while (pAttach)
		{
			pAttach = pAttach->m_pNext;

			nSize += 25;
		}
	}

	sm_FinishUpdatePacket(pCtx, pCtx->m_Msg1.m_pMsg, 8, nSize);

	sub_100291c7(pCtx->m_pMgr, pCtx->m_pClient, pObject, pInfo,
		pCtx->m_Msg1.m_pMsg);

	sub_10027ad0(pCtx, pObject, pInfo);

	pInfo->m_ChangeFlags = 0x1000;
}
#endif // !LT_SERVER_BUILD

// 0x10027e8b / 0x00465610 (2,028 bytes; the row ends at 0x00465dfc -- the
// next start, 0x00465e00, is the visibility callback stored at 0x00465a07).
// lithtech.exe has no tick counters here (no cnt_Start/EndCounter call and
// no add into 0x587a0's twin), expands the context's constructor and
// destructor, and expands sm_WriteObjectUpdate in the forced-update loop.
// RESIDUAL (lithtech, 2,109 vs 2,028 bytes, near): at the three early
// returns shipped CALLS the member release 0x004666f0 (0x0046569f,
// 0x004656c0, 0x004656fe, then the shared 0x0046570a/0x00465713 tail) and
// expands it only at the final exit; we expand it at all four. That is the
// /O2 inline budget (spent from the end backwards): wrapping the
// two flag tests at the foot in inline helpers already turns the early
// cursor releases into calls (measured, 2,056 bytes), so the original spent
// more budget below them than this source does. What it spent it on is not
// visible, and invented wrappers are not kept.
void sm_WriteClientUpdate(CServerMgr* pMgr, Client* pClient)
{
	CUpdateContext ctx;
#ifdef LT_SERVER_BUILD
	CBaseCounter counter1(0);
	CBaseCounter counter2(0);
#endif
	CVisQuery query;
	LTObject* pObjects[MAX_FORCEUPDATE_OBJECTS];
	ForceUpdate forceUpdate;
	LTObject* pViewed;
	LTLink* pCur;
	uint32 i;

	if (sm_SkipClientUpdate(pClient))
		return;

	if (pClient->m_State != 3)
		return;

	if (!(pClient->m_ClientFlags & 0x12))
	{
		// Not time yet?
		if (!pClient->m_RateControl.Update())
			return;

		// How much of last frame's output made it out.
		float fRate;
		if (pMgr->m_Unknown7fc)
			fRate = 1.0f - (float)pMgr->m_Unknown800 / (float)pMgr->m_Unknown7fc;
		else
			fRate = 1.0f;

		if (g_DebugLevel > 0)
		{
			if (fRate < 0.999f)
				dsi_ServerError("Overflowing at %0.0f percent", (1.0f - fRate) * 100.0f);
		}

		// Scale down further when the whole server is over its bandwidth.
		if (g_NetBandwidth > 0)
		{
			float* pBandwidth = &pMgr->m_NetMgr.m_RateE0.m_Value;
			float fBandwidth = (float)g_NetBandwidth;
			float fUsage = pBandwidth[0] / pBandwidth[1] / fBandwidth;
			if (fUsage > 0.75f)
			{
				fUsage += 0.25f;
				if (fUsage >= 2.0f)
					fUsage = 2.0f;

				float fScale = 2.0f - fUsage;
				fRate *= fScale * fScale;
			}
		}

		fRate *= pClient->m_Unknown128;
		if (2.0f > fRate)
			fRate = 2.0f;

		// Ease the governor toward the new rate.
		float fCur = pClient->m_RateControl.GetRate();
		float fNew;
		if (fRate > fCur)
			fNew = fCur + (fRate - fCur) * 0.01f;
		else
			fNew = (fCur + fRate) * 0.5f;

		if (g_DebugLevel > 0)
		{
			if ((float)fabs(fNew - fRate) > 0.1f)
				dsi_ServerError("Connection %d: choked update rate %0.0f", pClient->m_ClientID, fNew);
		}

		pClient->m_RateControl.SetRate(fNew);
	}

	pClient->m_ClientFlags &= ~0x10;

	ctx.m_pMgr = pMgr;
	ctx.m_pClient = pClient;
    ctx.m_ClientLifetime = pClient->m_Lifetime;

	{
		CAbstractIO* pIO;
		ctx.m_Cursors[1].Init(ctx.m_Msg1.Init(msg_AllocPacket(&pIO, 0x1000, 0x1000)));
		if (pIO)
			pIO->Release();
	}
	{
		CAbstractIO* pIO;
		ctx.m_Cursors[0].Init(ctx.m_Msg2.Init(msg_AllocPacket(&pIO, 0x1000, 0x1000)));
		if (pIO)
			pIO->Release();
	}

	ctx.m_Unknown18 = 0;
	ctx.m_bLocal = (pClient->m_ClientFlags >> 9) & 1;

	sm_WriteUpdateHeader(pMgr, &ctx, pClient);
#ifdef LT_SERVER_BUILD
	cnt_StartCounter(&counter1);
#endif
	sm_Unknown26755(pMgr, pClient);

	// Swap the sent-id lists: clear last frame's sent bits, refill the other.
	ClientIDList* pOldList = &pClient->m_IDLists[pClient->m_CurIDList];
	ClientIDList* pNewList = &pClient->m_IDLists[pClient->m_CurIDList == 0];
	g_pFillIDList = pNewList;
	for (i = 0; i < pOldList->m_IDs.size(); i++)
		pClient->m_ObjInfos[pOldList->m_IDs[i]].m_ChangeFlags &= ~0x1000;
	pNewList->Clear();

#ifdef LT_SERVER_BUILD
	cnt_StartCounter(&counter2);
#endif

	// Gather what this client can see.
	query.m_Position = pClient->m_Position;
	query.m_Radius = 10000.0f;
	query.m_Fn1 = sm_VisCallbackA;
	query.m_pUser = &ctx;
	((IObjectQuery*)&pMgr->m_Unknown210)->FindObjects(&query);

	sm_WriteChangedObjects(pMgr, &pMgr->m_Unknown390, &ctx);

#ifdef LT_SERVER_BUILD
	g_Unknown587a0 += cnt_EndCounter(&counter2);
#endif

	if (g_Unknown5845c)
		g_Unknown5845c = 0;

	// The forced set: the viewed object, the sky, and the game's own picks.
	forceUpdate.m_Objects = (HOBJECT*)pObjects;
	forceUpdate.m_nObjects = 0;

	if (pClient->m_pViewClient)
		pViewed = pClient->m_pViewClient->m_pUnknown3AC;
	else
		pViewed = pClient->m_pUnknown3AC;

	if (pViewed)
	{
		forceUpdate.m_Objects[0] = (HOBJECT)pViewed;
		forceUpdate.m_nObjects = 1;
	}

	for (i = 0; i < 30; i++)
	{
		if (pMgr->m_SkyObjects[i] != 0xffff)
		{
			LTObject* pObject = sm_FindObjectByID(pMgr, pMgr->m_SkyObjects[i]);
			if (pObject)
				forceUpdate.m_Objects[forceUpdate.m_nObjects++] = (HOBJECT)pObject;
		}
	}

	if (pViewed)
		pMgr->m_ClassMgr.message(pViewed->m_pSData->m_pBaseClass,MID_GETFORCEUPDATEOBJECTS, &forceUpdate, 0.0f);

	for (i = 0; i < forceUpdate.m_nObjects; i++)
	{
		LTObject* pObject = (LTObject*)forceUpdate.m_Objects[i];
		if (pObject)
		{
			CObjInfo* pInfo = &pClient->m_ObjInfos[pObject->m_ObjectID];
			if (!(pInfo->m_ChangeFlags & 0x1000))
#ifdef LT_SERVER_BUILD
				sm_WriteObjectUpdate(&ctx, pObject, pInfo);
#else
				sm_WriteObjectUpdateInline(&ctx, pObject, pInfo);
#endif
		}
	}

    // Keep the batch alive across sends and retain unsent records for retry.
    std::vector<std::shared_ptr<const SoundRecord>> queued;
    try { queued = pClient->m_QueuedSounds; }
    catch (const std::bad_alloc&) { return; }
    const auto client_lifetime = pClient->m_Lifetime;
    for (auto it = queued.rbegin(); it != queued.rend(); ++it) {
        if (!*client_lifetime || pClient->m_Removing || pClient->m_State != 3) return;
        if (std::find(pClient->m_QueuedSounds.begin(),pClient->m_QueuedSounds.end(),*it) == pClient->m_QueuedSounds.end()) continue;
        sm_FinishUpdatePacket(&ctx,ctx.m_Msg1.m_pMsg,8,0x23);
        if (!*client_lifetime || pClient->m_Removing || pClient->m_State != 3) return;
        if (sound_WriteQueued(**it,*pClient,*ctx.m_Msg1.m_pMsg,pMgr->m_World.PositionReference()) != LT_OK) return;
        const auto sent = std::find(pClient->m_QueuedSounds.begin(),pClient->m_QueuedSounds.end(),*it);
        if (sent != pClient->m_QueuedSounds.end()) pClient->m_QueuedSounds.erase(sent);
    }

	sm_WriteRemovedObjects(&ctx, ctx.m_Msg1.m_pMsg);
	sm_FinishUpdatePacket(&ctx, ctx.m_Msg1.m_pMsg, 8, 0x10);
	sm_WriteIDList(&ctx, pOldList);
	sm_Unknown27c71(pMgr, pClient, ctx.m_Msg2.m_pMsg);

	ctx.m_Msg1.m_pMsg->m_MsgFlags |= 8;
	ctx.m_Msg2.m_pMsg->m_MsgFlags |= 0x10;

	if (ctx.m_Msg1.m_pMsg->m_Unknown34 - 1 > 1)
		sm_FinishUpdatePacket(&ctx, ctx.m_Msg1.m_pMsg, 8, -1);

	sm_FinishUpdatePacket(&ctx, ctx.m_Msg2.m_pMsg, 0xa, -1);

	pClient->m_CurIDList = (pClient->m_CurIDList == 0);

	if (pClient->m_ClientFlags & 0x20)
	{
		pClient->m_ClientFlags &= ~0x20;
		sm_SendSkyInfo(pMgr, pClient);
	}

	if (pClient->m_ClientFlags & 0x400)
	{
		pClient->m_ClientFlags &= ~0x400;
		sm_Unknown2873d(pMgr, pClient);
	}
}

void sm_MarkSkyChanged(CServerMgr* pMgr)
{
	LTLink* pCur = pMgr->m_ClientList.m_Head.m_pNext;
	while (pCur != &pMgr->m_ClientList.m_Head)
	{
		((Client*)pCur->m_pData)->m_ClientFlags |= 0x20;
		pCur = pCur->m_pNext;
	}
}

// The lea this used to carry was the single tail-hosted exit, and nothing
// else: with the body wrapped in one `if` and a `break` out of the search,
// every revision materialized &m_ChangeFlags and stored through it (104
// bytes against 102). Written with the returns INLINE -- a guard clause for
// the flag, a `return` where the `break` was, and the trailing one -- VC6
// runs its address CSE before it tail-merges the three, so the store keeps
// its displacement and the merge still produces the shipped single exit.
// Byte-identical on both images.
LTRESULT sm_RemoveObjectFromSky(CServerMgr* pMgr, LTObject* pObject)
{
	uint32 i;
	uint32 nFlags;
	uint16 nID;

	nFlags = pObject->m_ChangeFlags;
	if (~nFlags & 0x100)
		return LT_OK;

	pObject->m_ChangeFlags = nFlags & ~0x100;
	sm_MarkSkyChanged(pMgr);

	nID = pObject->m_ObjectID;
	for (i = 0; i < 30; i++)
	{
		if (g_pServerMgr->m_SkyObjects[i] == nID)
		{
			pMgr->m_SkyObjects[i] = 0xffff;
			sm_MarkSkyChanged(g_pServerMgr);
			return LT_OK;
		}
	}

	return LT_OK;
}

LTRESULT sm_RemoveObjectFromWorld(CServerMgr* pMgr, BaseClass* pClass)
{
    if (!pMgr || !pClass) return LT_INVALIDPARAMS;
	LTObject* pObject = (LTObject*)pClass->m_hObject;
	if (!pObject)
		RETURN_ERROR(1, "sm_RemoveObjectFromWorld", LT_ERROR);

    if (!pMgr->m_Unknown390.m_Allocated.contains(pObject)) return LT_INVALIDPARAMS;
    ServerCallbackGuard server(pMgr->m_Lifetime);
    ObjectCallbackGuard lifetime(*pObject);
    sm_UnqueueObject(pMgr,pObject);
	if (pObject->m_pSData)
	{
        if (pObject->m_pSData->m_Removing) return LT_OK;
        if (pObject->m_pSData->m_pOwningClient) return LT_TRIEDTOREMOVECLIENTOBJECT;
        const auto previous_flags = pObject->m_ChangeFlags;
        pObject->m_pSData->m_Removing = true;
		pObject->m_ChangeFlags |= 1;

		// Unless the world is going down whole, attached children get told.
		if (~pMgr->m_Unknown1a0 & 1)
		{
            if (!am_VisitChildren(&pMgr->m_Unknown390,pObject,[](void* context,LTObject& child) {
                if (child.m_pSData && child.m_pSData->m_pBaseClass)
                    static_cast<CServerMgr*>(context)->m_ClassMgr.message(child.m_pSData->m_pBaseClass,MID_PARENTATTACHMENTREMOVED,nullptr,0);
                return true;
            },pMgr)) {
                pObject->m_pSData->m_Removing = false;
                pObject->m_ChangeFlags = previous_flags;
                return LT_OUTOFMEMORY;
            }
            if (!server.unchanged() || !lifetime.same_data()) return LT_OK;
		}

		if (pObject->m_ChangeFlags & 0x100)
		{
			sm_MarkSkyChanged(pMgr);
			sm_RemoveObjectFromSky(pMgr, pObject);
			pObject->m_ChangeFlags &= ~0x100;
		}

		// A client's own object refuses to go.
		if (pObject->m_pSData->m_pOwningClient) {
            pObject->m_pSData->m_Removing = false;
            pObject->m_ChangeFlags = (pObject->m_ChangeFlags & ~1u) | (previous_flags & 1u);
            return LT_TRIEDTOREMOVECLIENTOBJECT;
        }

		sm_BreakAttachments(pMgr, pObject, 0, 1);
        if (!server.unchanged() || !lifetime.same_data()) return LT_OK;

		if (pObject->m_pSData->m_Unknown28)
		{
			sm_Free28(pMgr, pObject->m_pSData->m_Unknown28);

			// Free the object's id.
			pMgr->m_Unknown8fc.m_pArray[pObject->m_ObjectID].m_nFlags = 0;
			pMgr->m_Unknown8fc.m_pArray[pObject->m_ObjectID].m_pRecord = LTNULL;
		}

		obj_UnlinkTrackersA(pObject);
		obj_UnlinkTrackersB(pObject);
		obj_ReleaseMemory(pObject);
		// Trackers the game added live in the game object destroyed next.
		if (pObject->m_ObjectType == OT_MODEL)
			static_cast<ModelInstance*>(pObject)->DetachExternalTrackers();
		sm_ObjectRemoved(pMgr, pObject);
        if (!server.unchanged() || !lifetime.alive()) return LT_OK;

		pObject->m_pSData = LTNULL;
	}

	sm_RemoveFromWorldTree(pMgr, pObject);
	g_Unknown58bec -= sizeof(LTObject);
	sm_RemoveObjectAttachments(pMgr,pObject);

	return un390_RemoveObject(&pMgr->m_Unknown390, pObject);
}

LTRESULT sm_UpdateClientState2(CServerMgr* pMgr, Client* pClient)
{
    if (!pMgr || !pClient || pClient->m_Removing || pClient->m_State != 2) return LT_INVALIDPARAMS;
    ServerActivity activity(pMgr->m_Lifetime);
    if (!activity.available() || pMgr->m_Terminating || pMgr->m_EndingWorld) return LT_NOTINITIALIZED;
    ServerCallbackGuard operation(pMgr->m_Lifetime);
    const auto client_lifetime = pClient->m_Lifetime;
    const auto unchanged = [&] { return operation.unchanged() && *client_lifetime && !pClient->m_Removing && pClient->m_State == 2; };
	CMessageRef msg;
	LTLink* pListHead;
	LTLink* pCur;
	CObjInfo* pInfo;

	// Stage one: tell the client which files it needs.
	if (pClient->m_Unknown3BC == 1 || (pClient->m_ClientFlags & 2))
	{
		uint32 i, j;

		{
			CAbstractIO* pIO;
			msg.Init(msg_AllocPacket(&pIO, 0x1000, 0x1000));
			if (pIO)
				pIO->Release();
		}
		if (!msg.m_pMsg) return LT_OUTOFMEMORY;

		for (i = 0; i < pMgr->m_nFileTrees; i++)
		{
			CFileList* pList = pMgr->m_pFileTrees[i]->m_pWorldBsp;
			for (j = 0; j < pList->m_nChunks; j++)
			{
				if (pList->m_pChunks[j].m_Flags)
				{
					{
						ServerMessage* pMsg = msg.m_pMsg;
						pMsg->m_Unknown38 = 1;
						pMsg->m_Unknown34 = 1;
					}
					msg.m_pMsg->WriteWord((uint16)i);
					msg.m_pMsg->WriteWord((uint16)j);
					msg.m_pMsg->WriteByte((uint8)pList->m_pChunks[j].m_Flags);
					sm_SendToClient(pMgr, pClient, 0x13, msg.m_pMsg, 0, 0x80);
					if (!unchanged()) return LT_ERROR;
				}
			}
		}

		sm_SendWorldInfo(pMgr, pClient);
		if (!unchanged()) return LT_ERROR;
		pClient->m_nLightChanges = 0;
		sm_SendObjectInfo2(pMgr, pClient);
		if (!unchanged()) return LT_ERROR;
		sm_SendSkyInfo(pMgr, pClient);
		if (!unchanged()) return LT_ERROR;

		pClient->m_Unknown3BC = 2;
	}

	// Stage two: put the client's object into the world.
	if (pClient->m_Unknown3BC == 3 || (pClient->m_ClientFlags & 2))
	{
		{
			CAbstractIO* pIO;
			msg.Init(msg_AllocPacket(&pIO, 0x1000, 0x1000));
			if (pIO)
				pIO->Release();
		}
		if (!msg.m_pMsg) return LT_OUTOFMEMORY;

        if (pClient->m_EnteringWorld || pClient->m_ExitingWorld || !pMgr->m_ClassMgr.m_pServerShell) return LT_ERROR;
        const auto lifetime = pClient->m_Lifetime;
        const auto state = pClient->m_State;
        const auto data = pClient->RetainData();
        pClient->m_EnteringWorld = true;
        struct Entry {
            Client* client; std::shared_ptr<bool> lifetime;
            ~Entry() { if (*lifetime) client->m_EnteringWorld = false; }
        } entry{pClient,lifetime};
        BaseClass* pClass = pMgr->m_ClassMgr.shell([&](IServerShell& shell) { return shell.OnClientEnterWorld(
            pClient->Handle(), data->empty() ? nullptr : data->data(), static_cast<uint32>(data->size())); });
        if (!operation.unchanged() || !*lifetime) return LT_ERROR;
        if (pClient->m_Removing || pClient->m_State != state || !pClass || !pMgr->m_ClassMgr.owns(pClass)) return LT_ERROR;
        auto* object = pClass->m_hObject;
        if (!pMgr->m_Unknown390.m_Allocated.contains(object) || !object->m_pSData ||
            (object->m_pSData->m_pOwningClient && object->m_pSData->m_pOwningClient != pClient)) return LT_ERROR;
        pClient->m_pUnknown3AC = object;
        object->m_pSData->m_pOwningClient = pClient;

        pMgr->m_ClientReferences.RemoveFor(pClient->m_pUnknown3AC);

		if (pClient->m_pViewClient == LTNULL)
		{
			{
				ServerMessage* pMsg = msg.m_pMsg;
				pMsg->m_Unknown38 = 1;
				pMsg->m_Unknown34 = 1;
			}
			// WriteType<uint16>, expanded here and ONLY here in this
			// function: lithtech.exe calls the instantiation three times in
			// the file loop above (0x0046319a, 0x004631a2 word, 0x004631b8
			// byte) and expands this one (0x00463370..0x0046341e). The
			// inliner's budget is per call site, so the three stay spelled
			// as the wrapper.
			((CPacket*)msg.m_pMsg)->WriteType(pClient->m_pUnknown3AC->m_ObjectID);
			sm_SendToClient(pMgr, pClient, 7, msg.m_pMsg, 1, 0x80);
			if (!unchanged()) return LT_ERROR;
		}

		pClient->m_IDLists[1].Clear();
		pClient->m_IDLists[0].Clear();

		// Every updating object starts with its fresh change flags...
		pListHead = &pMgr->m_List8dc.m_Head;
		pCur = pListHead->m_pNext;
		while (pCur != pListHead)
		{
			LTObject* pObject = (LTObject*)pCur->m_pData;
			pInfo = &pClient->m_ObjInfos[pObject->m_ObjectID];
			pInfo->m_ChangeFlags = sm_GetObjectChangeFlags(pMgr, pObject);
			pCur = pCur->m_pNext;
		}

		// ...and every live sound plays from the top.
		pCur = pMgr->m_List54.m_Head.m_pNext;
		while (pCur != &pMgr->m_List54.m_Head)
		{
			SoundInstance* pSound = (SoundInstance*)pCur->m_pData;
			pInfo = &pClient->m_ObjInfos[pSound->m_pUnknown0c->m_nIndex];
			// Change flags first: /O1 normalises the pair to (byte, word)
			// -- server.dll stores 0x2 with `andb $0` BEFORE the word --
			// while /O2 keeps source order, and lithtech.exe has the word
			// first. Everything else in this 978-byte function is already
			// byte-identical in both images, so the order can only come
			// from the source.
			pInfo->m_ChangeFlags = 3;
			pInfo->m_Unknown02 = 0;
			pCur = pCur->m_pNext;
		}

		pClient->m_State = 3;
	}

	return LT_OK;
}



// 0x10032617 — what the string bank reports for anything still alive at
// shutdown.
void sm_ErrorHook(char* pString)
{
	if (g_DebugLevel >= 1)
		dsi_ServerError("Unfreed (server) string: %s", pString);
}

// 0x10031f2f — take a link out of wherever it is and put it on the manager's
// spare list.
void sm_Free28(CServerMgr* pMgr, void* record)
{
	auto* pLink = static_cast<CObjectIDRec*>(record);
	LTLink* pHead;

	pLink->m_pPrev->m_pNext = pLink->m_pNext;
	pLink->m_pNext->m_pPrev = pLink->m_pPrev;
	pLink->TieOff();

	pHead = &pMgr->m_Link8c4;
	pLink->m_pPrev = pHead;
	pLink->m_pNext = pHead->m_pNext;
	pLink->m_pNext->m_pPrev = pLink;
	pLink->m_pPrev->m_pNext = pLink;
}

// 0x1002acd3 — an object by its id. The map is indexed straight by id and a
// slot only counts when its flag says the object is live.
LTObject* sm_FindObjectByID(CServerMgr* pMgr, uint16 nID)
{
	CObjectMapEntry* pEntry;

	if (nID >= pMgr->m_Unknown8fc.m_nElements)
		return LTNULL;

	pEntry = &pMgr->m_Unknown8fc.m_pArray[nID];
	if (pEntry->m_nFlags == 1)
		return (LTObject*)pEntry->m_pRecord;

	return LTNULL;
}

// ---- the world, session and interface block (0x1002fb47-0x1003030a) ----- //

// The squared length of the world's box diagonal. The subtraction inlines and
// MagSqr does not, which is the shape the engine's own ltvector.h gives:
// operator- carries its body in the class, Mag/MagSqr only their declarations
// (the same split SPhysicsLT::SetVelocity shows for Dist/DistSqr).


// Save and restore both open the stream here and hand it to the object
// serializer; the interface half is only the open, the check and the close.
LTRESULT si_SaveObjects(char* filename, ObjectList* list, uint32 parameter, uint32 flags) {
    return sav_SaveFile(g_pServerMgr,filename,list,parameter,flags);
}
LTRESULT si_RestoreObjects(char* filename, uint32 parameter, uint32 flags) {
    return sav_RestoreFile(g_pServerMgr,filename,parameter,flags);
}

// The world is started at the manager's own idea of the time, not the
// caller's: the manager pointer is fetched once and read for both.
LTRESULT si_LoadWorld(char* pszWorldFileName, uint32 flags)
{
	CServerMgr* pMgr = g_pServerMgr;

	return pMgr->DoStartWorld(pszWorldFileName, flags, pMgr->m_Unknown6d8);
}

LTRESULT si_RunWorld()
{
	return g_pServerMgr->DoRunWorld();
}

LTRESULT si_UpdateSessionName(const char* sName)
{
	return g_pServerMgr->m_NetMgr.UpdateSessionName(sName);
}

LTRESULT si_GetSessionName(char* sName, uint32 dwBufferSize)
{
	if (!sName)
		RETURN_ERROR(1, "ILTPhysics::GetSessionName", LT_INVALIDPARAMS);

	return g_pServerMgr->m_NetMgr.GetSessionName(sName, dwBufferSize);
}

LTRESULT si_GetTcpIpAddress(char* sAddress, uint32 dwBufferSize, uint16& hostPort)
{
	if (!sAddress)
		RETURN_ERROR(1, "ILTPhysics::GetTcpIpAddress", LT_INVALIDPARAMS);

	return g_pServerMgr->m_NetMgr.GetTcpIpAddress(sAddress, dwBufferSize, hostPort);
}

// The one interface entry that talks back out of the engine: with no host
// application attached there is nothing to send to.
LTRESULT si_SendToServerApp(char* pMsg, uint32 nLen)
{
	ServerAppHandler* pHandler = g_pServerMgr->m_pAppHandler;
	if (pHandler)
		return pHandler->ShellMessageFn(pMsg, nLen);

	return LT_NOTFOUND;
}

FileEntry* si_GetFileList(char* pDirName)
{
	if (!pDirName)
		return LTNULL;

	return fs_GetFileList(&g_pServerMgr->m_FileMgr, pDirName);
}


// ---- the interface table (0x1002fd9b) ------------------------------------ //
//
// Everything ILTServer exposes that is not virtual is a function pointer on
// the interface object, and this is where all 125 of them are set. It is the
// map from the SDK's header to the shipped code: each store's offset names the
// field (iltserver.h's declaration order fixes the offsets) and its operand
// names the implementation, so the table below is the index of the whole
// server interface — including the parts we have not reconstructed yet.
//
// Entries whose code lives outside this file are declared here as they are
// used. Two prefixes appear: si_ for a server-interface entry that sits in
// another server TU, ci_ for one the client interface shares (the counter and
// model-node entries forward to the same common-module helpers both tables
// point at). Names for the unreconstructed ones are ours, taken from the
// field they fill; the addresses are what the stores actually write.
//
// Four fields are never assigned and stay null: GetPointAreas and
// GetBoxIntersecters (0x090, 0x094) and the model-node hide pair (0x1dc,
// 0x1e0) — the SDK still declares them, but the shipped engine no longer
// implements them. GetPropVector and GetPropColor share one implementation,
// which is why its address is loaded into a register and stored twice.

LTRESULT si_GetGameInfo(void **ppData, uint32 *pLen);                                                // 0x020
HCLASS si_GetClass(char *pName);                                                                     // 0x024
LTRESULT si_GetStaticObject(HCLASS hClass, HOBJECT *obj);                                            // 0x028
HCLASS si_GetObjectClass(HOBJECT hObject);                                                           // 0x02c
LTBOOL si_IsKindOf(HCLASS hClass, HCLASS hTest);                                                     // 0x030
LPBASECLASS si_CreateObject(HCLASS hClass, ObjectCreateStruct *pStruct);                             // 0x034
LPBASECLASS si_CreateObjectProps(HCLASS hClass, ObjectCreateStruct *pStruct, char *pszProps );       // 0x038
uint32 si_GetServerFlags();                                                                          // 0x03c
uint32 si_SetServerFlags(uint32 flags);                                                              // 0x040
LTRESULT si_CacheFile(uint32 fileType, char *pFilename);                                             // 0x044
LTRESULT si_GetNextPoly(HPOLY *hPoly);                                                               // 0x048
void ci_StartCounter(LTCounter *pCounter);                                                           // 0x04c
uint32 ci_EndCounter(LTCounter *pCounter);                                                           // 0x050
LTFLOAT ci_Random(LTFLOAT min, LTFLOAT max);                                                         // 0x054
int si_IntRandom(int min, int max);                                                                  // 0x058
LTFLOAT si_RandomScale(LTFLOAT scale);                                                               // 0x05c
void si_BPrint(char *pMsg, ...);                                                                     // 0x060
LTRESULT si_GetSkyDef(SkyDef *pDef);                                                                 // 0x068
LTRESULT si_SetSkyDef(SkyDef *pDef);                                                                 // 0x06c
LTRESULT si_AddObjectToSky(HOBJECT hObj, uint32 index);                                              // 0x070
LTRESULT si_RemoveObjectFromSky(HOBJECT hObj);                                                       // 0x074
LTRESULT si_GetGlobalLightObject(HOBJECT *hObj);                                                     // 0x078
LTRESULT si_SetGlobalLightObject(HOBJECT hObj);                                                      // 0x07c
LTRESULT si_GetPortalFlags(char *pPortalName, uint32 *pFlags);                                       // 0x080
LTRESULT si_SetPortalFlags(char *pPortalName, uint32 flags);                                         // 0x084
LTBOOL si_CastRay(IntersectQuery *pQuery, IntersectInfo *pInfo);                                     // 0x08c
ObjectList* si_CreateObjectList();                                                                   // 0x0a4
ObjectLink* si_AddObjectToList(ObjectList *pList, HOBJECT hObj);                                     // 0x0a8
void si_RemoveObjectFromList(ObjectList *pList, HOBJECT hObj);                                       // 0x0ac
LTRESULT si_AttachClient(HCLIENT hParent, HCLIENT hChild);                                           // 0x0b0
LTRESULT si_DetachClient(HCLIENT hClient);                                                           // 0x0b4
HCLIENT si_GetNextClient(HCLIENT hPrev);                                                             // 0x0b8
HCLIENTREF si_GetNextClientRef(HCLIENTREF hRef);                                                     // 0x0bc
uint32 si_GetClientRefInfoFlags(HCLIENTREF hClient);                                                 // 0x0c0
LTBOOL si_GetClientRefName(HCLIENTREF hClient, char *pName, int maxLen);                             // 0x0c4
HOBJECT si_GetClientRefObject(HCLIENTREF hClient);                                                   // 0x0c8
uint32 si_GetClientID(HCLIENT hClient);                                                              // 0x0cc
LTBOOL si_GetClientName(HCLIENT hClient, char *pName, int maxLen);                                   // 0x0d0
void si_SetClientInfoFlags(HCLIENT hClient, uint32 dwClientFlags );                                  // 0x0d4
uint32 si_GetClientInfoFlags(HCLIENT hClient);                                                       // 0x0d8
void si_SetClientUserData(HCLIENT hClient, void *pData);                                             // 0x0dc
void* si_GetClientUserData(HCLIENT hClient);                                                         // 0x0e0
void si_KickClient(HCLIENT hClient, uint32 nReasonCode);                                             // 0x0e4
LTRESULT si_SetClientViewPos(HCLIENT hClient, LTVector *pPos);                                       // 0x0e8
void si_RunGameConString(char *pString);                                                             // 0x0ec
void si_SetGameConVar(char *pName, char *pVal);                                                      // 0x0f0
HCONVAR si_GetGameConVar(char *pName);                                                               // 0x0f4
LTRESULT si_GetPropString(char *pPropName, char *pRet, int maxLen);                                  // 0x0f8
LTRESULT si_GetPropVector(char *pPropName, LTVector *pRet);                                          // 0x0fc
LTRESULT si_GetPropReal(char *pPropName, float *pRet);                                               // 0x104
LTRESULT si_GetPropFlags(char *pPropName, uint32 *pRet);                                             // 0x108
LTRESULT si_GetPropBool(char *pPropName, LTBOOL *pRet);                                              // 0x10c
LTRESULT si_GetPropLongInt(char *pPropName, long *pRet);                                             // 0x110
LTRESULT si_GetPropRotation(char *pPropName, LTRotation *pRet);                                      // 0x114
LTRESULT si_GetPropRotationEuler(char *pPropName, LTVector *pAngles);                                // 0x118
LTRESULT si_DoesPropExist(char *pPropName, int *pPropType);                                          // 0x120
HOBJECT si_ObjectToHandle(LPBASECLASS pObject);                                                      // 0x124
LPBASECLASS si_HandleToObject(HOBJECT hObject);                                                      // 0x128
LTBOOL si_IsCommandOn(HCLIENT hClient, int command);                                                 // 0x12c
LTBOOL ci_UpperStrcmp(char *pStr1, char *pStr2);                                                     // 0x130
int si_Parse(char *pCommand, char **pNewCommandPos, char *argBuffer, char **argPointers, int *nArgs); // 0x134
LTRESULT si_PlaySound( PlaySoundInfo *pPlaySoundInfo );                                              // 0x138
LTRESULT si_GetSoundDuration( HLTSOUND hSound, LTFLOAT *fDuration );                                 // 0x13c
LTRESULT si_IsSoundDone( HLTSOUND hSound, LTBOOL *bDone );                                           // 0x140
LTRESULT si_KillSound( HLTSOUND hSound );                                                            // 0x144
LTRESULT si_KillSoundLoop( HLTSOUND hSound );                                                        // 0x148
LTRESULT si_CreateInterObjectLink(HOBJECT hOwner, HOBJECT hLinked);                                  // 0x14c
void si_BreakInterObjectLink(HOBJECT hOwner, HOBJECT hLinked);                                       // 0x150
LTRESULT si_CreateAttachment(HOBJECT hParent, HOBJECT hChild, char *pSocketName, LTVector *pOffset, LTRotation *pRotationOffset, HATTACHMENT *pAttachment); // 0x154
LTRESULT si_RemoveAttachment(HATTACHMENT hAttachment);                                               // 0x158
LTRESULT si_FindAttachment(HOBJECT hParent, HOBJECT hChild, HATTACHMENT *hAttachment);               // 0x15c
LTBOOL si_GetObjectColor(HOBJECT hObject, float *r, float *g, float *b, float *a);                   // 0x160
LTBOOL si_SetObjectColor(HOBJECT hObject, float r, float g, float b, float a);                       // 0x164
uint32 si_GetObjectUserFlags(HOBJECT hObj);                                                          // 0x168
LTRESULT si_SetObjectUserFlags(HOBJECT hObj, uint32 flags);                                          // 0x16c
HOBJECT si_GetNextObject(HOBJECT hObj);                                                              // 0x170
HOBJECT si_GetNextInactiveObject(HOBJECT hObj);                                                      // 0x174
void si_SetNextUpdate(HOBJECT hObj, LTFLOAT nextUpdate);                                             // 0x178
void si_SetDeactivationTime(HOBJECT hObj, LTFLOAT fDeactivationTime);                                // 0x17c
void si_PingObjects(HOBJECT hObj);                                                                   // 0x180
void si_GetObjectPos(HOBJECT hObj, LTVector *pos);                                                   // 0x184
void si_SetObjectPos(HOBJECT hObj, LTVector *pos);                                                   // 0x188
void si_ScaleObject(HOBJECT hObj, LTVector *pNewScale);                                              // 0x18c
LTRESULT si_GetObjectScale(HOBJECT hObj, LTVector *pScale);                                          // 0x190
LTRESULT si_TeleportObject(HOBJECT hObj, LTVector *pNewPos);                                         // 0x194
// Every object whose box overlaps the sphere's box gets handed to the
// callback, which does the round test and adds what survives. The counter
// around it feeds the two stats the console reports.
void sm_SphereGather(LTObject* pObj, void* pUser);                 // 0x1002f861

// lithtech.exe's copy (0x0046faa0, 293 bytes) has NO tick counter: no
// CBaseCounter, no cnt_Start/EndCounter calls and no add into the ticks
// global -- only the count (0x004b48bc) survives.  Guarded here so the one
// source serves both images (identical on both).
ObjectList* sm_FindObjectsTouchingSphere(LTVector* pPosition, float radius)
{
#ifdef LT_SERVER_BUILD
	CBaseCounter counter(0);
#endif
	SphereQuery  query;
	LTVector     vMin;
	LTVector     vMax;

	// The list lives in the query record and nowhere else — every use reads
	// it back out, which is what keeps the frame at 0x40.
	query.m_pList = (ObjectList*)sm_sb_Allocate(&g_pServerMgr->m_ObjectListBank);
	if (!query.m_pList)
		return LTNULL;

	query.m_pList->m_nInList = 0;
	query.m_pList->m_pFirstLink = LTNULL;

	query.m_pPosition = pPosition;
	query.m_Radius = radius;
	query.m_RadiusSqr = radius * radius;
	query.m_Diameter = radius + radius;

#ifdef LT_SERVER_BUILD
	cnt_StartCounter(&counter);
#endif

	vMin = *pPosition - radius;
	vMax = *pPosition + radius;

	((IObjectQuery*)&g_pServerMgr->m_Unknown210)->FindObjectsInBox(
		&vMin, &vMax, [](LTObject* object, void* user) { sm_SphereGather(object, user); return 1; }, &query, 0);

#ifdef LT_SERVER_BUILD
	g_FindSphereTicks += cnt_EndCounter(&counter);
#endif
	g_FindSphereCount += query.m_pList->m_nInList;
	return query.m_pList;
}

// Swing a direction onto a plane without changing its length: the axis is the
// cross of the plane normal with it, and the result is that axis crossed back
// with the normal. A direction already along the normal has no axis to swing
// about, which is what the magnitude test catches.
void si_TiltToPlane(LTObject* hObj, LTVector* pNormal)
{
	LTVector vAxis;
	LTVector vNew;

	vAxis = pNormal->Cross(hObj->m_Acceleration);

	if (vAxis.MagSqr() > 0.001f)
	{
		vAxis.Norm();
		vNew = vAxis.Cross(*pNormal);
		hObj->m_Acceleration = vNew * hObj->m_Acceleration.Mag();
	}
}

// Hand a list back to the manager: every link first, then the list itself.
// The manager pointer is re-read on each turn of the loop because the free
// writes through it.
void sm_RelinquishList(ObjectList* pList)
{
	ObjectLink* pLink;
	ObjectLink* pNext;

	if (!pList)
		return;

	pLink = pList->m_pFirstLink;
	while (pLink)
	{
		pNext = pLink->m_pNext;
		sb_Free(&g_pServerMgr->m_ObjectLinkBank, pLink);
		pLink = pNext;
	}

	sb_Free(&g_pServerMgr->m_ObjectListBank, pList);
}

// ---- object creation and the container links ---------------------------- //

LPBASECLASS si_CreateObject(HCLASS hClass, ObjectCreateStruct* pStruct)
{
	if (!hClass || !pStruct)
		return LTNULL;

	return g_pServerMgr->CreateObject(hClass, pStruct);
}

// The two entries whose work is done elsewhere: both are the thin server-side
// half of a world query, with the manager's own state handed in.

LTBOOL si_IntersectSegment(IntersectQuery* pQuery, IntersectInfo* pInfo)
{
	if (!g_pServerMgr)
		return LTFALSE;

	return w_IntersectSegment(pQuery, pInfo, &g_pServerMgr->m_Unknown210, 1);
}

// The lookup answers in bytes and the caller wants floats, and the channels
// come back the other way round.
LTBOOL si_GetPointShade(LTVector* pPoint, LTVector* pColor)
{
	uint8 color[4];

	w_GetPointShade(&g_pServerMgr->m_ShadeState, pPoint, color);

	pColor->x = color[2];
	pColor->y = color[1];
	pColor->z = color[0];
	return LTTRUE;
}

// sm_FreeObjectInline and sm_AllocateObjectInline -- 0x1002abf8's and
// 0x10001c54's bodies again, `inline` under local names -- used to be
// defined here; they are in servermodel.h now, because servermodel.cpp's
// sm_LoadWorldObjects expands both in lithtech.exe too (0x0046a048,
// 0x0046a14c). lithtech.exe has NO standalone copy of either and expands
// them at every site in this file -- si_CreateObjectProps (0x00470c8d,
// 0x00470efb), CServerMgr::CreateObject (0x004745a0, 0x00474631) and
// sub_100334cc (0x00474a86) -- while server.dll calls 0x10001c54 /
// 0x1002abf8 at all of them.
//
// THE do/while(0) IS WHAT SEPARATES THE TWO IMAGES. Written straight, /Os
// expands the free too and server.dll loses all three callers (measured:
// CreateObject, si_CreateObjectProps and sub_100334cc all break). Any
// statement-level control flow in the body -- the `do { } while (0)`, or an
// `if (1)` -- makes /Os decline the expansion while /Ot still takes it, and
// the flow folds away so the emitted code is unchanged. The same rule
// explains SoundInstance::Term (its `if`) and sm_AllocateObjectInline (the
// `if` inside sb_Allocate): /Os-declined, /Ot-expanded, both with a branch
// in the body.

// Creating an object from a property string: parse the string into the
// manager's property list, let the object read it back through the PT_
// readers during its precreate, then throw the list away again. The list is
// the same one si_FindProp walks, which is why it hangs off the manager
// rather than being passed anywhere.
LPBASECLASS si_CreateObjectProps(HCLASS hClass, ObjectCreateStruct* pStruct, char* pszProps) {
    auto* manager = g_pServerMgr;
    if (!manager || !pStruct) return nullptr;
    try {
        auto properties = std::make_shared<ObjectProperties>(std::string_view(pszProps ? pszProps : ""));
        return manager->CreateObjectWithProperties(hClass, pStruct, std::move(properties), PRECREATE_STRINGPROP);
    } catch (const std::exception& error) {
        nolf::platform::log("object properties", error.what());
        return nullptr;
    }
}

// Both container walks run the same list — the inter-object links hanging off
// the object's own data — and differ only in which end of each link they are
// looking at. The flag list is written in step with the object list and left
// zeroed; the SDK asks for it to be the same size for that reason.
uint32 si_GetObjectContainers(LTObject* hObj, HOBJECT* pContainerList,
                              uint32* pFlagList, uint32 maxListSize)
{
	LTLink* pHead;
	LTLink* pCur;
	uint32  nFound;

	if (!hObj)
		return 0;

	pHead = &hObj->m_pSData->m_Links;
	nFound = 0;

	for (pCur = pHead->m_pNext; pCur != pHead; pCur = pCur->m_pNext)
	{
		InterObjectLink* pLink = (InterObjectLink*)pCur->m_pData;

		if (pLink->m_Type != INTERLINK_CONTAINER)
			continue;

		if (hObj == pLink->m_pOwner)
			continue;

		if (nFound >= maxListSize)
			return nFound;

		pContainerList[nFound] = pLink->m_pOwner;
		pFlagList[nFound] = 0;
		nFound++;
	}

	return nFound;
}

uint32 si_GetContainedObjects(LTObject* hContainer, HOBJECT* pObjectList,
                              uint32* pFlagList, uint32 maxListSize)
{
	LTLink* pHead;
	LTLink* pCur;
	uint32  nFound;

	if (!hContainer)
		return 0;

	pHead = &hContainer->m_pSData->m_Links;
	nFound = 0;

	for (pCur = pHead->m_pNext; pCur != pHead; pCur = pCur->m_pNext)
	{
		InterObjectLink* pLink = (InterObjectLink*)pCur->m_pData;

		if (pLink->m_Type != INTERLINK_CONTAINER)
			continue;

		if (hContainer != pLink->m_pOwner)
			continue;

		if (nFound >= maxListSize)
			return nFound;

		pObjectList[nFound] = pLink->m_pLinked;
		pFlagList[nFound] = 0;
		nFound++;
	}

	return nFound;
}

// ---- lights, the model play flag and the model filenames ---------------- //

// The light accessors forward to the object-colour pair, which is where a
// light's colour actually lives; the alpha channel a light has no use for is
// dropped on the way in and scaled back out of its byte on the way out.
void si_GetLightColor(LTObject* hObj, float* r, float* g, float* b)
{
	if (hObj)
		si_GetObjectColor(hObj, r, g, b, LTNULL);
}

void si_SetLightColor(LTObject* hObj, float r, float g, float b)
{
	if (!hObj)
		return;

	si_SetObjectColor(hObj, r, g, b, hObj->m_ColorA * (1.0f / 255.0f));
}

// Neither radius accessor checks for null — a light's radius overlays the
// world pointer, so both go straight at the type byte.
float si_GetLightRadius(LTObject* hObj)
{
	if (hObj->m_ObjectType == OT_LIGHT)
		return hObj->m_LightRadius;

	return 1.0f;
}

void si_SetLightRadius(LTObject* hObj, float radius)
{
	if (hObj->m_ObjectType != OT_LIGHT)
		return;

	hObj->m_LightRadius = radius;
	sm_SetObjectChangeFlag(g_pServerMgr, hObj, CF_COLORINFO);
}

// Playing is a bit on the model's own tracker, and only a real change is
// broadcast — which is what the compare against the caller's flag is doing.
//
// The bit is set and cleared on the member itself (`m_Flags |=`/`&=`), not
// through a uint16 local stored back once: the local made VC6 widen the
// clear to `and eax, imm32` where the shipped build has the 16-bit
// `and ax, 0fffeh` (server 0x10031bab). The tracker is reached through its
// own pointer: on lithtech.exe that keeps the flag store (0x00472d00) above
// the inlined change-flag's read of the object's internal flags.
void si_SetModelPlaying(LTObject* hObj, LTBOOL bPlaying)
{
	ModelInstance* pInst = (ModelInstance*)hObj;

	if (pInst->m_ObjectType != OT_MODEL)
		return;

	LTAnimTracker* pTracker = &pInst->m_AnimTracker;
	if ((pTracker->m_Flags & AT_PLAYING) == (bPlaying != 0))
		return;

	if (bPlaying)
		pTracker->m_Flags |= AT_PLAYING;
	else
		pTracker->m_Flags &= ~AT_PLAYING;

	sm_SetObjectChangeFlag(g_pServerMgr, pInst, CF_MODELINFO);
}

LTBOOL si_GetModelPlaying(LTObject* hObj)
{
	ModelInstance* pInst = (ModelInstance*)hObj;

	if (pInst->m_ObjectType == OT_MODEL)
		return pInst->m_AnimTracker.m_Flags & AT_PLAYING;

	return LTFALSE;
}

// The object keeps its model and skin as file identifiers, not strings, so
// both come back through the file manager. An unset one leaves an empty
// string rather than failing.
LTBOOL si_GetModelFilenames(LTObject* hObj, char* pFilename, int fileBufLen,
                            char* pSkinName, int skinBufLen)
{
	if (!hObj)
		return LTFALSE;

	if (hObj->m_ObjectType != OT_MODEL)
		return LTFALSE;

	if (hObj->m_pSData->m_pModelFile)
	{
		strncpy(pFilename,
		        fs_GetFileName(&g_pServerMgr->m_FileMgr, hObj->m_pSData->m_pModelFile),
		        fileBufLen - 1);
	}
	else
	{
		pFilename[0] = 0;
	}

	if (hObj->m_pSData->m_SkinFiles[0])
	{
		strncpy(pSkinName,
		        fs_GetFileName(&g_pServerMgr->m_FileMgr, hObj->m_pSData->m_SkinFiles[0]),
		        skinBufLen - 1);
	}
	else
	{
		pSkinName[0] = 0;
	}

	return LTTRUE;
}

int si_Parse(char* pCommand, char** pNewCommandPos, char* argBuffer,
             char** argPointers, int* nArgs)
{
	return parse_Command(pCommand, pNewCommandPos, argBuffer, argPointers, nArgs);
}

// ---- teleport, blocking priority and the sprite clip poly ---------------- //

// The sprite's clip poly sits past the end of LTObject (name ours).
using SpriteObject = CObjSprite;

LTRESULT si_TeleportObject(LTObject* hObj, LTVector* pNewPos)
{
	if (!hObj)
		RETURN_ERROR(1, "ILTPhysics::TeleportObject", LT_INVALIDPARAMS);

	si_SetObjectPos(hObj, pNewPos);
	return LT_OK;
}

uint8 si_GetBlockingPriority(LTObject* hObj)
{
	if (hObj)
		return hObj->m_BlockingPriority;

	return 0;
}

LTRESULT si_ClipSprite(LTObject* hObj, HPOLY hPoly)
{
	SpriteObject* pSprite = (SpriteObject*)hObj;

	// Two checks, not one `||`: the shipped code keeps the error inline where
	// the second one falls through instead of sinking it past the body.
	if (!pSprite)
		return LT_ERROR;

	if (pSprite->m_ObjectType != OT_SPRITE)
		return LT_ERROR;

	if (pSprite->m_ClipPoly != hPoly)
	{
		pSprite->m_ClipPoly = hPoly;
		sm_SetObjectChangeFlag(g_pServerMgr, pSprite, CF_MODELINFO);
	}

	return LT_OK;
}

void si_SetBlockingPriority(LTObject* hObj, uint8 pri)
{
	if (hObj)
		hObj->m_BlockingPriority = pri;
}

// ---- server state, the console and the object lists --------------------- //

// Two of the three SS_ bits live in the manager's other status word, so the
// reader assembles them rather than returning a field.
uint32 si_GetServerFlags()
{
	uint32      nFlags;

	nFlags = ((g_pServerMgr->m_Unknown1a0 >> 1) & SS_DEMOPLAYBACK) | g_pServerMgr->m_ServerFlags;

	if (g_pServerMgr->m_Unknown1a0 & 0x10)
		nFlags |= SS_CACHING;

	return nFlags;
}

// Only SS_PAUSED is settable, and the store is read back rather than reused:
// the global could be what was just written through.
uint32 si_SetServerFlags(uint32 flags)
{
	g_pServerMgr->m_ServerFlags = flags & SS_PAUSED;
	return g_pServerMgr->m_ServerFlags;
}

LTRESULT si_CacheFile(uint32 fileType, char* pFilename)
{
	return sm_CacheFile(g_pServerMgr, fileType, pFilename);
}

static Client* si_FindClient(HCLIENT handle) {
    return g_pServerMgr ? client_Find(g_pServerMgr->m_ClientList,handle) : nullptr;
}
void si_KickClient(HCLIENT handle, uint32 reason) {
    if (auto* client = si_FindClient(handle); client && !client->m_Removing)
        g_pServerMgr->m_NetMgr.Disconnect(client->m_pConnection,6,reason);
}

// The one check site in the file that reports at level 0, and the one whose
// name literal carries no class prefix.
LTRESULT si_SetClientViewPos(HCLIENT handle, LTVector* position) {
    auto* client = si_FindClient(handle);
    return client ? client->SetViewPosition(position) : LT_INVALIDPARAMS;
}

void si_RunGameConString(char* pString)
{
	con_RunString(&g_pServerMgr->m_ConsoleState, pString);
}

void si_SetGameConVar(char* pName, char* pVal)
{
	if (!pName || !pVal)
		return;

	con_SetVar(&g_pServerMgr->m_ConsoleState, pName, pVal);
}

HCONVAR si_GetGameConVar(char* pName)
{
	if (!pName)
		return LTNULL;

	return con_FindVar(&g_pServerMgr->m_ConsoleState, pName);
}

// The command states are double-buffered per client — one 0xff-byte page per
// buffer, the live one indexed by the counter after them — which is what the
// multiply by 0xff says.
LTBOOL si_IsCommandOn(HCLIENT handle, int command) {
    auto* client = si_FindClient(handle);
    return client && client->CommandOn(command);
}

ObjectList* si_CreateObjectList()
{
	ObjectList* pList = (ObjectList*)sm_sb_Allocate(&g_pServerMgr->m_ObjectListBank);

	pList->m_pFirstLink = LTNULL;
	pList->m_nInList = 0;
	return pList;
}

ObjectLink* si_AddObjectToList(ObjectList* pList, HOBJECT hObj)
{
	ObjectLink* pLink = (ObjectLink*)sm_sb_Allocate(&g_pServerMgr->m_ObjectLinkBank);

	pLink->m_pNext = pList->m_pFirstLink;
	pList->m_pFirstLink = pLink;
	pLink->m_hObject = hObj;
	pList->m_nInList++;
	return pLink;
}

void si_RemoveObjectFromList(ObjectList* pList, HOBJECT hObj)
{
	ObjectLink** ppPrev = &pList->m_pFirstLink;
	ObjectLink*  pCur   = pList->m_pFirstLink;

	while (pCur)
	{
		if (pCur->m_hObject == hObj)
		{
			*ppPrev = pCur->m_pNext;
			pList->m_nInList--;
			sb_Free(&g_pServerMgr->m_ObjectLinkBank, pCur);
			return;
		}

		ppPrev = &pCur->m_pNext;
		pCur = pCur->m_pNext;
	}
}

// Every message the manager hands out starts pointing at the same object, so
// the writer can reach it without going back through the manager.
void CServerMgr::InitMessageOwner(ServerMessage* pMsg)
{
	pMsg->m_Write.m_pObjectIO = m_Unknown804;
}

// Changing the global light is a change every client has to hear about, so it
// is marked on all of them rather than on an object.
void CServerMgr::SetGlobalLightObject(HOBJECT hObj)
{
	LTLink* pCur;

	m_hGlobalLightObject = hObj;

	for (pCur = m_ClientList.m_Head.m_pNext;
	     pCur != &m_ClientList.m_Head;
	     pCur = pCur->m_pNext)
	{
		((Client*)pCur->m_pData)->m_ClientFlags |= CLIENTFLAG_SKYCHANGED;
	}
}

// A ray is an intersect query with the far end computed for the caller: the
// direction is normalised to a fixed reach and added to the start. A
// direction too short to normalise is not a ray at all.
LTBOOL si_CastRay(IntersectQuery* pQuery, IntersectInfo* pInfo)
{
	float fMag = pQuery->m_Direction.Mag();

	if (fMag < 0.00001f)
		return LTFALSE;

	fMag = 10000.0f / fMag;

	pQuery->m_To = pQuery->m_Direction * fMag;
	pQuery->m_To += pQuery->m_From;

	return si_IntersectSegment(pQuery, pInfo);
}

// ---- the object property readers (0x1003047c-0x100306f0) ---------------- //
//
// Every one of these is the same three steps: find the named property on the
// object currently being created, check that its type is the one this reader
// speaks for, and hand back the value at +0x58. The type checks run straight
// down the PT_ list, which is what identifies each reader.

// 0x100304ac — the list walk the readers share (name ours).
const ObjectProperty* si_FindProp(char* name) {
    return name && g_pServerMgr && g_pServerMgr->m_Properties ? g_pServerMgr->m_Properties->find(name) : nullptr;
}
LTRESULT si_GetPropString(char* name, char* output, int capacity) {
    if (!output || capacity <= 0) return LT_INVALIDPARAMS;
    const auto* property = si_FindProp(name);
    if (!property || property->type != PT_STRING) return LT_NOTFOUND;
    std::snprintf(output, static_cast<std::size_t>(capacity), "%s", property->text.c_str());
    return LT_OK;
}
LTRESULT si_GetPropVector(char* name, LTVector* output) {
    if (!output) return LT_INVALIDPARAMS;
    const auto* property = si_FindProp(name);
    if (!property || (property->type != PT_VECTOR && property->type != PT_COLOR)) return LT_NOTFOUND;
    *output = property->value.m_Vec; return LT_OK;
}
LTRESULT si_GetPropReal(char* name, float* output) {
    if (!output) return LT_INVALIDPARAMS;
    const auto* property = si_FindProp(name);
    if (!property || property->type != PT_REAL) return LT_NOTFOUND;
    *output = property->value.m_Float; return LT_OK;
}
LTRESULT si_GetPropFlags(char* name, uint32* output) {
    if (!output) return LT_INVALIDPARAMS;
    const auto* property = si_FindProp(name);
    if (!property || property->type != PT_FLAGS) return LT_NOTFOUND;
    const double value = property->value.m_Float;
    if (value < 0 || value > static_cast<double>(UINT32_MAX)) return LT_INVALIDPARAMS;
    *output = static_cast<uint32>(value); return LT_OK;
}
LTRESULT si_GetPropBool(char* name, LTBOOL* output) {
    if (!output) return LT_INVALIDPARAMS;
    const auto* property = si_FindProp(name);
    if (!property || property->type != PT_BOOL) return LT_NOTFOUND;
    *output = property->value.m_Bool; return LT_OK;
}
LTRESULT si_GetPropLongInt(char* name, long* output) {
    if (!output) return LT_INVALIDPARAMS;
    const auto* property = si_FindProp(name);
    if (!property || property->type != PT_LONGINT) return LT_NOTFOUND;
    *output = property->value.m_Long; return LT_OK;
}
LTRESULT si_GetPropRotation(char* name, LTRotation* output) {
    if (!output) return LT_INVALIDPARAMS;
    const auto* property = si_FindProp(name);
    if (!property || property->type != PT_ROTATION) return LT_NOTFOUND;
    *output = property->value.m_Rotation; return LT_OK;
}
LTRESULT si_GetPropRotationEuler(char* name, LTVector* output) {
    if (!output) return LT_INVALIDPARAMS;
    const auto* property = si_FindProp(name);
    if (!property || property->type != PT_ROTATION) return LT_NOTFOUND;
    *output = property->value.m_Vec; return LT_OK;
}
LTRESULT si_GetPropGeneric(char* name, GenericProp* output) {
    if (!output) return LT_INVALIDPARAMS;
    const auto* property = si_FindProp(name);
    if (!property) return LT_NOTFOUND;
    *output = property->value; return LT_OK;
}
LTRESULT si_DoesPropExist(char* name, int* output) {
    if (!output) return LT_INVALIDPARAMS;
    const auto* property = si_FindProp(name);
    if (!property) return LT_NOTFOUND;
    *output = property->type; return LT_OK;
}

HOBJECT si_ObjectToHandle(LPBASECLASS pObject)
{
	if (!pObject)
		return LTNULL;

	return pObject->m_hObject;
}

LPBASECLASS si_HandleToObject(LTObject* hObject)
{
	if (!hObject)
		return LTNULL;

	return hObject->m_pSData->m_pBaseClass;
}

// ---- the client accessors (0x10030c50-0x10030def) ----------------------- //
//
// An HCLIENT is a Client*, an HCLIENTREF the pending-announce record the
// manager keeps for a client that has not finished joining; both open with
// the LTLink that ties them into their list, which is what lets the two
// walkers step from one handle to the next without touching the manager.

// The explicit `else` is load-bearing. Both arms end in `return
// pCur->m_pData`, so VC6 cross-jumps that tail to the end of the function
// either way; what the `else` decides is which of the two `return LTNULL`
// blocks survives. Written as two guarded early returns the second copy
// hosts the block and the first arm needs a `jmp` to reach it (two bytes
// more than shipped); written as an if/else chain the FIRST arm hosts it
// inline and the count check jumps backwards into it, which is what shipped.
// (The shared-exit host: which arm carries the common tail.)
HCLIENT si_GetNextClient(HCLIENT previous) {
    return g_pServerMgr ? client_Next(g_pServerMgr->m_ClientList,previous) : nullptr;
}

HCLIENTREF si_GetNextClientRef(HCLIENTREF previous) {
    return g_pServerMgr ? g_pServerMgr->m_ClientReferences.Next(previous) : nullptr;
}
uint32 si_GetClientRefInfoFlags(HCLIENTREF handle) {
    if (!g_pServerMgr) return 0;
    const auto* entry = g_pServerMgr->m_ClientReferences.Find(handle);
    return entry && (entry->flags & 2) ? CIF_LOCAL : 0;
}
LTBOOL si_GetClientRefName(HCLIENTREF handle, char* name, int capacity) {
    if (g_pServerMgr) return g_pServerMgr->m_ClientReferences.CopyName(handle,name,capacity);
    if (name && capacity > 0) name[0] = 0;
    return LTFALSE;
}
HOBJECT si_GetClientRefObject(HCLIENTREF handle) {
    if (!g_pServerMgr) return nullptr;
    const auto* entry = g_pServerMgr->m_ClientReferences.Find(handle);
    return entry ? entry->object : nullptr;
}

uint32 si_GetClientID(HCLIENT handle) {
    auto* client = si_FindClient(handle);
    return client ? client->m_ClientID : uint32(0xffffffff);
}

LTBOOL si_GetClientName(HCLIENT handle, char* name, int capacity) {
    auto* client = si_FindClient(handle);
    if (client) return client->CopyName(name,capacity);
    if (name && capacity > 0) name[0] = 0;
    return LTFALSE;
}

// The public CIF_ bits and the internal client flags agree on three values
// and disagree on the other three, so both directions are written out one
// bit at a time rather than masked wholesale.
void si_SetClientInfoFlags(HCLIENT handle, uint32 flags) {
    if (auto* client = si_FindClient(handle)) client->SetInfoFlags(flags);
}

uint32 si_GetClientInfoFlags(HCLIENT handle) {
    auto* client = si_FindClient(handle);
    return client ? client->InfoFlags() : 0;
}

void si_SetClientUserData(HCLIENT handle, void* data) {
    if (auto* client = si_FindClient(handle)) client->m_pUserData = data;
}

void* si_GetClientUserData(HCLIENT handle) {
    auto* client = si_FindClient(handle);
    return client ? client->m_pUserData : nullptr;
}

LTRESULT si_GetLastCollision(CollisionInfo *pInfo);                                                  // 0x198
LTRESULT si_GetObjectRotation(HOBJECT hObj, LTRotation *pRotation);                                  // 0x19c
LTRESULT si_SetObjectRotation(HOBJECT hObj, LTRotation *pRotation);                                  // 0x1a0
LTRESULT si_RotateObject(HOBJECT hObj, LTRotation *pRotation);                                       // 0x1a4
void si_TiltToPlane(HOBJECT hObj, LTVector *pNormal);                                                // 0x1a8
void si_SetObjectState(HOBJECT hObj, int state);                                                     // 0x1ac
int si_GetObjectState(HOBJECT hObj);                                                                 // 0x1b0
void si_SetBlockingPriority(HOBJECT hObj, uint8 pri);                                                // 0x1b4
uint8 si_GetBlockingPriority(HOBJECT hObj);                                                          // 0x1b8
LTRESULT si_ClipSprite(HOBJECT hObj, HPOLY hPoly);                                                   // 0x1bc
void si_GetLightColor(HOBJECT hObj, float *r, float *g, float *b);                                   // 0x1c0
void si_SetLightColor(HOBJECT hObj, float r, float g, float b);                                      // 0x1c4
float si_GetLightRadius(HOBJECT hObj);                                                               // 0x1c8
void si_SetLightRadius(HOBJECT hObj, float radius);                                                  // 0x1cc
LTRESULT ci_GetNextModelNode(HOBJECT hObject, HMODELNODE hNode, HMODELNODE *pNext);                  // 0x1d0
LTRESULT ci_GetModelNodeName(HOBJECT hObject, HMODELNODE hNode, char *pName, uint32 maxLen);         // 0x1d4
LTRESULT si_GetModelCommandString(HOBJECT hObj, char *pStr, uint32 maxLen);                          // 0x1d8
const char *ci_GetAnimName(HOBJECT hObject, HMODELANIM hAnim);                                       // 0x1e4
void si_SetModelPlaying(HOBJECT hObj, LTBOOL bPlaying);                                              // 0x1e8
LTBOOL si_GetModelPlaying(HOBJECT hObj);                                                             // 0x1ec
LTBOOL si_GetModelFilenames(HOBJECT hObj, char *pFilename, int fileBufLen, char *pSkinName, int skinBufLen); // 0x1f0
uint32 si_GetObjectContainers(HOBJECT hObj, HOBJECT *pContainerList, uint32 *pFlagList, uint32 maxListSize); // 0x1f4
uint32 si_GetContainedObjects(HOBJECT hContainer, HOBJECT *pObjectList, uint32 *pFlagList, uint32 maxListSize); // 0x1f8
LTRESULT si_SaveObjects(char *pszSaveFileName, ObjectList *pList, uint32 dwParam, uint32 flags);     // 0x1fc
LTRESULT si_RestoreObjects( char *pszRestoreFileName, uint32 dwParam, uint32 flags );                // 0x200
LTRESULT si_LoadWorld(char *pszWorldFileName, uint32 flags);                                         // 0x204
LTRESULT si_RunWorld();                                                                              // 0x208
LTRESULT si_UpdateSessionName(const char* sName);                                                    // 0x20c
LTRESULT si_GetSessionName(char* sName, uint32 dwBufferSize);                                        // 0x210
LTRESULT si_SendToServerApp( char *pMsg, uint32 nLen );                                              // 0x214
LTRESULT si_GetTcpIpAddress(char* sAddress, uint32 dwBufferSize, uint16 &hostPort);                  // 0x218
FileEntry* si_GetFileList(char *pDirName);                                                           // 0x21c
void fs_FreeFileList(FileEntry *pHead);                                                              // 0x220

void si_SetupInterface(CLTServer* pServer)
{
	pServer->GetGameInfo               = si_GetGameInfo;
	pServer->GetClass                  = si_GetClass;
	pServer->GetStaticObject           = si_GetStaticObject;
	pServer->GetObjectClass            = si_GetObjectClass;
	pServer->IsKindOf                  = si_IsKindOf;
	pServer->CreateObject              = si_CreateObject;
	pServer->CreateObjectProps         = si_CreateObjectProps;
	pServer->GetServerFlags            = si_GetServerFlags;
	pServer->SetServerFlags            = si_SetServerFlags;
	pServer->CacheFile                 = si_CacheFile;
	pServer->GetNextPoly               = si_GetNextPoly;
	pServer->StartCounter              = ci_StartCounter;
	pServer->EndCounter                = ci_EndCounter;
	pServer->Random                    = ci_Random;
	pServer->IntRandom                 = si_IntRandom;
	pServer->RandomScale               = si_RandomScale;
	pServer->BPrint                    = si_BPrint;
	pServer->DebugOut                  = con_Printf;
	pServer->GetSkyDef                 = si_GetSkyDef;
	pServer->SetSkyDef                 = si_SetSkyDef;
	pServer->AddObjectToSky            = si_AddObjectToSky;
	pServer->RemoveObjectFromSky       = si_RemoveObjectFromSky;
	pServer->GetGlobalLightObject      = si_GetGlobalLightObject;
	pServer->SetGlobalLightObject      = si_SetGlobalLightObject;
	pServer->GetPortalFlags            = si_GetPortalFlags;
	pServer->SetPortalFlags            = si_SetPortalFlags;
	pServer->IntersectSegment          = si_IntersectSegment;
	pServer->CastRay                   = si_CastRay;

	pServer->GetPointShade             = si_GetPointShade;
	pServer->FindObjectsTouchingSphere = sm_FindObjectsTouchingSphere;
	pServer->RelinquishList            = sm_RelinquishList;

	pServer->ObjectToHandle            = si_ObjectToHandle;
	pServer->HandleToObject            = si_HandleToObject;

	pServer->CreateObjectList          = si_CreateObjectList;
	pServer->AddObjectToList           = si_AddObjectToList;
	pServer->RemoveObjectFromList      = si_RemoveObjectFromList;

	pServer->GetPropString             = si_GetPropString;
	pServer->GetPropVector             = si_GetPropVector;
	pServer->GetPropColor              = si_GetPropVector;
	pServer->GetPropReal               = si_GetPropReal;
	pServer->GetPropFlags              = si_GetPropFlags;
	pServer->GetPropBool               = si_GetPropBool;
	pServer->GetPropLongInt            = si_GetPropLongInt;
	pServer->GetPropRotation           = si_GetPropRotation;
	pServer->GetPropRotationEuler      = si_GetPropRotationEuler;
	pServer->GetPropGeneric            = si_GetPropGeneric;
	pServer->DoesPropExist             = si_DoesPropExist;

	pServer->AttachClient              = si_AttachClient;
	pServer->DetachClient              = si_DetachClient;
	pServer->GetNextClient             = si_GetNextClient;
	pServer->GetNextClientRef          = si_GetNextClientRef;
	pServer->GetClientRefInfoFlags     = si_GetClientRefInfoFlags;
	pServer->GetClientRefName          = si_GetClientRefName;
	pServer->GetClientRefObject        = si_GetClientRefObject;
	pServer->GetClientID               = si_GetClientID;
	pServer->GetClientName             = si_GetClientName;
	pServer->SetClientInfoFlags        = si_SetClientInfoFlags;
	pServer->GetClientInfoFlags        = si_GetClientInfoFlags;
	pServer->SetClientUserData         = si_SetClientUserData;
	pServer->GetClientUserData         = si_GetClientUserData;
	pServer->KickClient                = si_KickClient;
	pServer->SetClientViewPos          = si_SetClientViewPos;
	pServer->RunGameConString          = si_RunGameConString;
	pServer->SetGameConVar             = si_SetGameConVar;
	pServer->GetGameConVar             = si_GetGameConVar;

	pServer->IsCommandOn               = si_IsCommandOn;
	pServer->UpperStrcmp               = ci_UpperStrcmp;

	pServer->GetLastCollision          = si_GetLastCollision;

	pServer->PlaySound                 = si_PlaySound;

	pServer->IsSoundDone               = si_IsSoundDone;

	pServer->GetSoundDuration          = si_GetSoundDuration;

	pServer->KillSound                 = si_KillSound;
	pServer->KillSoundLoop             = si_KillSoundLoop;
	pServer->CreateInterObjectLink     = si_CreateInterObjectLink;
	pServer->BreakInterObjectLink      = si_BreakInterObjectLink;
	pServer->CreateAttachment          = si_CreateAttachment;
	pServer->RemoveAttachment          = si_RemoveAttachment;
	pServer->FindAttachment            = si_FindAttachment;
	pServer->GetObjectColor            = si_GetObjectColor;
	pServer->SetObjectColor            = si_SetObjectColor;
	pServer->GetObjectUserFlags        = si_GetObjectUserFlags;
	pServer->SetObjectUserFlags        = si_SetObjectUserFlags;
	pServer->GetNextObject             = si_GetNextObject;
	pServer->GetNextInactiveObject     = si_GetNextInactiveObject;
	pServer->SetNextUpdate             = si_SetNextUpdate;
	pServer->SetDeactivationTime       = si_SetDeactivationTime;
	pServer->PingObjects               = si_PingObjects;
	pServer->GetObjectPos              = si_GetObjectPos;
	pServer->SetObjectPos              = si_SetObjectPos;
	pServer->ScaleObject               = si_ScaleObject;
	pServer->GetObjectScale            = si_GetObjectScale;
	pServer->TeleportObject            = si_TeleportObject;

	pServer->GetObjectRotation         = si_GetObjectRotation;
	pServer->SetObjectRotation         = si_SetObjectRotation;
	pServer->RotateObject              = si_RotateObject;

	pServer->GetBlockingPriority       = si_GetBlockingPriority;

	pServer->SetBlockingPriority       = si_SetBlockingPriority;

	pServer->ClipSprite                = si_ClipSprite;

	pServer->TiltToPlane               = si_TiltToPlane;

	pServer->GetObjectState            = si_GetObjectState;

	pServer->SetObjectState            = si_SetObjectState;

	pServer->GetLightColor             = si_GetLightColor;
	pServer->SetLightColor             = si_SetLightColor;
	pServer->GetLightRadius            = si_GetLightRadius;
	pServer->SetLightRadius            = si_SetLightRadius;
	pServer->GetNextModelNode          = ci_GetNextModelNode;
	pServer->GetModelNodeName          = ci_GetModelNodeName;
	pServer->GetModelCommandString     = si_GetModelCommandString;

	pServer->GetAnimName               = ci_GetAnimName;
	pServer->SetModelPlaying           = si_SetModelPlaying;
	pServer->GetModelPlaying           = si_GetModelPlaying;
	pServer->GetModelFilenames         = si_GetModelFilenames;

	pServer->Parse                     = si_Parse;

	pServer->GetObjectContainers       = si_GetObjectContainers;
	pServer->GetContainedObjects       = si_GetContainedObjects;
	pServer->SaveObjects               = si_SaveObjects;
	pServer->RestoreObjects            = si_RestoreObjects;
	pServer->LoadWorld                 = si_LoadWorld;
	pServer->RunWorld                  = si_RunWorld;
	pServer->UpdateSessionName         = si_UpdateSessionName;
	pServer->GetSessionName            = si_GetSessionName;
	pServer->SendToServerApp           = si_SendToServerApp;
	pServer->GetTcpIpAddress           = si_GetTcpIpAddress;
	pServer->GetFileList               = si_GetFileList;
	pServer->FreeFileList              = fs_FreeFileList;
}


// The three entries the table opens with. GetGameInfo re-reads the manager
// for the second field because the store through the first out-parameter
// could have written it.
LTRESULT si_GetGameInfo(void** ppData, uint32* pLen)
{
    if (!ppData || !pLen) return LT_INVALIDPARAMS;
    if (!g_pServerMgr) { *ppData = nullptr; *pLen = 0; return LT_NOTINITIALIZED; }
	*ppData = g_pServerMgr->m_GameInformation.empty() ? nullptr : g_pServerMgr->m_GameInformation.data();
	*pLen = static_cast<uint32>(g_pServerMgr->m_GameInformation.size());
	return LT_OK;
}

HCLASS si_GetClass(char* pName)
{
	return pName ? reinterpret_cast<HCLASS>(g_pServerMgr->m_ClassMgr.find(pName)) : LTNULL;
}

LTRESULT si_GetStaticObject(HCLASS hClass, HOBJECT* obj)
{
	if (!obj) return LT_INVALIDPARAMS;
	*obj = LTNULL;
	auto* record = g_pServerMgr->m_ClassMgr.record(hClass);
	if (!record)
		RETURN_ERROR(1, "ILTPhysics::GetStaticObject", LT_INVALIDPARAMS);

	LTObject* pObject = record->m_pStaticObject;
	if (pObject)
	{
		*obj = pObject;
		return LT_OK;
	}

	RETURN_ERROR(1, "ILTPhysics::GetStaticObject", LT_ERROR);
}

// ---- the flat ILTServer function-pointer entries (0x1003030b-0x10030c4f) - //
//
// Plain cdecl functions the interface table points at (si_ names ours; the
// error literals still say "ILTPhysics::"). What GetObjectClass hands back
// pins what an HCLASS really is: the CClassInfo slot-table entry at
// m_ClassMgr.m_nIndex — the per-module class record — not the CClassInfo.

HCLASS si_GetObjectClass(LTObject* pObj)
{
	if (!pObj || !g_pServerMgr->m_Unknown390.m_Allocated.contains(pObj) || !pObj->m_pSData ||
		!g_pServerMgr->m_ClassMgr.owns(pObj->m_pSData->m_pClass))
		RETURN_ERROR_VALUE_L(1, "ILTPhysics::GetObjectClass", LTNULL, LTNULL);

	return (HCLASS)pObj->m_pSData->m_pClass->m_pInternal[g_pServerMgr->m_ClassMgr.m_nIndex];
}

LTBOOL si_IsKindOf(HCLASS hClass, HCLASS hTest)
{
	const auto* record = g_pServerMgr->m_ClassMgr.record(hClass);
	const auto* test = g_pServerMgr->m_ClassMgr.record(hTest);
	if (!record || !test) return LTFALSE;
	for (auto* pDef = record->m_pClassDef; pDef; pDef = pDef->m_ParentClass)
	{
		if (pDef == test->m_pClassDef)
			return LTTRUE;
	}

	return LTFALSE;
}

LTRESULT si_GetPortalFlags(char* pPortalName, uint32* pFlags)
{
	WorldPortal* pPortal = w_FindPortal(&g_pServerMgr->m_World, pPortalName, 0, 0);
	if (!pPortal)
		RETURN_ERROR(1, "ILTPhysics::GetPortalFlags", LT_NOTFOUND);

	*pFlags = pPortal->m_Flags;
	return LT_OK;
}

LTRESULT si_SetPortalFlags(char* pPortalName, uint32 flags)
{
	return sm_SetPortalFlags(g_pServerMgr, pPortalName, flags);
}

LTRESULT si_GetNextPoly(HPOLY* hPoly) {
    return g_pServerMgr ? g_pServerMgr->m_World.NextPoly(hPoly) : LT_NOTINITIALIZED;
}

int si_IntRandom(int min, int max)
{
	return rand() % (max - min + 1) + min;
}

// One parameter, not two: this entry fills ILTServer::RandomScale (0x05c),
// while the two-argument Random (0x054) is a different function in the common
// module. Reading the table is what settled that — the body never touched a
// second argument, which read as Monolith ignoring it.
LTFLOAT si_RandomScale(LTFLOAT scale)
{
	return scale * (float(rand()) / float(RAND_MAX));
}

void sm_BroadcastPrintf(char* pMsg, ...);

void si_BPrint(char* pMsg, ...)
{
	char msg[500];
	va_list marker;

	va_start(marker, pMsg);
	vsnprintf(msg, sizeof(msg) - 1, pMsg, marker);
	sm_BroadcastPrintf(msg);
}

LTRESULT si_GetSkyDef(SkyDef* pDef)
{
	if (!pDef) return LT_INVALIDPARAMS;
	*pDef = g_pServerMgr->m_SkyDef;
	return LT_OK;
}

LTRESULT si_SetSkyDef(SkyDef* pDef)
{
	if (!pDef || !nolf::valid_sky(*pDef)) return LT_INVALIDPARAMS;
	g_pServerMgr->m_SkyDef = *pDef;
	sm_MarkSkyChanged(g_pServerMgr);
	return LT_OK;
}

LTRESULT si_AddObjectToSky(LTObject* pObj, uint32 index)
{
	if (!pObj)
		RETURN_ERROR_INFO(1, "ILTPhysics::AddObjectToSky", LT_ERROR, "object is LTNULL");
	if (index >= 30)
		RETURN_ERROR_INFO(1, "ILTPhysics::AddObjectToSky", LT_ERROR, "invalid index");

	if (!(pObj->m_ChangeFlags & 0x100))
	{
		pObj->m_ChangeFlags |= 0x100;
		g_pServerMgr->m_SkyObjects[index] = pObj->m_ObjectID;
		sm_MarkSkyChanged(g_pServerMgr);
	}

	return LT_OK;
}

LTRESULT si_RemoveObjectFromSky(LTObject* pObj)
{
	if (!pObj)
		RETURN_ERROR_INFO(1, "ILTPhysics::RemoveObjectFromSky", LT_ERROR, "object is LTNULL");

	return sm_RemoveObjectFromSky(g_pServerMgr, pObj);
}

LTRESULT si_GetGlobalLightObject(HOBJECT* hObj)
{
	if (!hObj)
		return LT_ERROR;

	*hObj = g_pServerMgr->m_hGlobalLightObject;
	return LT_OK;
}

LTRESULT si_SetGlobalLightObject(HOBJECT hObj)
{
	g_pServerMgr->SetGlobalLightObject(hObj);
	return LT_OK;
}

LTRESULT si_AttachClient(HCLIENT parent_handle, HCLIENT child_handle) {
    auto* parent = si_FindClient(parent_handle);
    auto* child = si_FindClient(child_handle);
    return parent && child ? sm_AttachClient(g_pServerMgr,parent,child) : LT_INVALIDPARAMS;
}

LTRESULT si_DetachClient(HCLIENT handle) {
    auto* client = si_FindClient(handle);
    return client ? sm_DetachClient(g_pServerMgr,client) : LT_INVALIDPARAMS;
}

LTRESULT si_GetLastCollision(CollisionInfo* pInfo)
{
	if (!pInfo)
		RETURN_ERROR(1, "ILTPhysics::GetLastCollision", LT_INVALIDPARAMS);

	if (!g_pServerMgr->m_pCollisionInfo)
		return LT_ERROR;

	// Statement-per-field, each re-walking the global chain — which is why
	// the shipped bytes reload g_pServerMgr four times.
	pInfo->m_Plane = g_pServerMgr->m_pCollisionInfo->m_Plane;
	pInfo->m_hObject = g_pServerMgr->m_pCollisionInfo->m_hObject;
	pInfo->m_hPoly = g_pServerMgr->m_pCollisionInfo->m_hPoly;
	pInfo->m_vStopVel = g_pServerMgr->m_pCollisionInfo->m_vStopVel;
	return LT_OK;
}

// The old flat sound API: CServerSoundMgr is CServerMgr's second base, so
// the calls below carry the +4 this-adjust the shipped bytes show.

LTRESULT si_PlaySound(PlaySoundInfo* pPlaySoundInfo)
{
    if (!g_pServerMgr) return LT_NOTINITIALIZED;
	HLTSOUND hResult;
	return g_pServerMgr->PlaySound(pPlaySoundInfo, hResult);
}

LTRESULT si_GetSoundDuration(HLTSOUND hSound, LTFLOAT* fDuration)
{
    if (!g_pServerMgr) return LT_NOTINITIALIZED;
    if (!fDuration) return LT_INVALIDPARAMS;
	return g_pServerMgr->GetSoundDuration(hSound, *fDuration);
}

LTRESULT si_IsSoundDone(HLTSOUND hSound, LTBOOL* bDone)
{
    if (!g_pServerMgr) return LT_NOTINITIALIZED;
    if (!bDone) return LT_INVALIDPARAMS;
	return g_pServerMgr->IsSoundDone(hSound, *bDone);
}

LTRESULT si_KillSound(HLTSOUND hSound)
{
    if (!g_pServerMgr) return LT_NOTINITIALIZED;
	return g_pServerMgr->KillSound(hSound);
}

LTRESULT si_KillSoundLoop(HLTSOUND hSound)
{
    if (!g_pServerMgr) return LT_NOTINITIALIZED;
	return g_pServerMgr->KillSoundLoop(hSound);
}

LTRESULT si_CreateInterObjectLink(LTObject* pOwner, LTObject* pLinked)
{
	if (!pOwner || !pLinked)
		RETURN_ERROR(1, "ILTPhysics::CreateInterObjectLink", LT_INVALIDPARAMS);

	return sm_CreateInterObjectLink(g_pServerMgr, pOwner, pLinked, 0);
}

void si_BreakInterObjectLink(LTObject* pOwner, LTObject* pLinked)
{
	if (!pOwner || !pLinked)
		return;

	sm_BreakInterObjectLink(g_pServerMgr, pOwner, pLinked, 0);
}

// The color change-flag thunk both color setters share (name ours).
void sm_ObjectColorChanged(LTObject* pObj)
{
	sm_SetObjectChangeFlag(g_pServerMgr, pObj, 0x40);
}

LTBOOL si_GetObjectColor(LTObject* hObject, float* r, float* g, float* b, float* a)
{
	if (!hObject)
		return LTFALSE;

	float fConv = 1.0f / 255.0f;
	if (r)
		*r = hObject->m_ColorR * fConv;
	if (g)
		*g = hObject->m_ColorG * fConv;
	if (b)
		*b = hObject->m_ColorB * fConv;
	if (a)
		*a = hObject->m_ColorA * fConv;

	return LTTRUE;
}

LTBOOL si_SetObjectColor(LTObject* hObject, float r, float g, float b, float a)
{
	if (!hObject)
		return LTFALSE;

	uint8 nColor[4];

	float fConv = 255.0f;
	nColor[0] = (uint8)(r * fConv);
	nColor[1] = (uint8)(g * fConv);
	nColor[2] = (uint8)(b * fConv);
	nColor[3] = (uint8)(a * fConv);

	if (hObject->m_ColorR != nColor[0] || hObject->m_ColorG != nColor[1] ||
	    hObject->m_ColorB != nColor[2] || hObject->m_ColorA != nColor[3])
	{
		hObject->m_ColorR = nColor[0];
		hObject->m_ColorG = nColor[1];
		hObject->m_ColorB = nColor[2];
		hObject->m_ColorA = nColor[3];
		sm_ObjectColorChanged(hObject);
	}

	return LTTRUE;
}

uint32 si_GetObjectUserFlags(LTObject* hObj)
{
	if (!hObj)
		return 0;

	return hObj->m_UserFlags;
}

LTRESULT si_SetObjectUserFlags(LTObject* hObj, uint32 flags)
{
	if (!hObj)
		RETURN_ERROR(1, "ILTPhysics::SetObjectUserFlags", LT_ERROR);

	if (hObj->m_UserFlags != flags)
	{
		hObj->m_UserFlags = flags;
		sm_SetObjectChangeFlag(g_pServerMgr, hObj, 8);
	}

	return LT_OK;
}

// The 0x8dc list holds every object: actives walked forward from the head,
// inactives (state bits 0x38) from the tail.
//
// Each arm carries its own end-of-list test (shipped lt 0x472110 keeps the
// two `cmp; jne; xor; ret` copies apart, srv 0x10031523 cross-jumps them).
// With one shared test after the if/else both images hoisted the
// g_pServerMgr load above the branch; the per-arm copy loads it in each arm
// as shipped (lt 0x4720ce/0x4720d9, srv 0x100314ea/0x100314f5). Identical on
// both images. GetObjectState (3 bytes) still keeps a movzbl our compilers
// prove away against the following mask.

LTObject* si_GetNextObject(LTObject* hObj)
{
	LTLink* pLink;

	if (hObj)
	{
		pLink = hObj->m_pSData->m_StateLink.m_pNext;
		if (pLink == &g_pServerMgr->m_List8dc.m_Head)
			return LTNULL;
	}
	else
	{
		pLink = g_pServerMgr->m_List8dc.m_Head.m_pNext;
		if (pLink == &g_pServerMgr->m_List8dc.m_Head)
			return LTNULL;
	}

	LTObject* pObj = (LTObject*)pLink->m_pData;
	return (pObj->m_ChangeFlags & 0x38) ? LTNULL : pObj;
}

LTObject* si_GetNextInactiveObject(LTObject* hObj)
{
	LTLink* pLink;

	if (hObj)
	{
		pLink = hObj->m_pSData->m_StateLink.m_pPrev;
		if (pLink == &g_pServerMgr->m_List8dc.m_Head)
			return LTNULL;
	}
	else
	{
		pLink = g_pServerMgr->m_List8dc.m_Head.m_pPrev;
		if (pLink == &g_pServerMgr->m_List8dc.m_Head)
			return LTNULL;
	}

	LTObject* pObj = (LTObject*)pLink->m_pData;
	return (pObj->m_ChangeFlags & 0x38) ? pObj : LTNULL;
}

void si_SetNextUpdate(LTObject* hObj, LTFLOAT nextUpdate)
{
	if (!hObj)
		return;

	hObj->m_pSData->m_NextUpdate = nextUpdate;
}

void si_SetDeactivationTime(LTObject* hObj, LTFLOAT fDeactivationTime)
{
	if (!hObj)
		return;

	hObj->m_pSData->m_DeactivationTime = fDeactivationTime;
	hObj->m_pSData->m_UpdateTimer = fDeactivationTime;

	if (hObj->m_ChangeFlags & 0x20)
		sm_SetObjectState(g_pServerMgr, hObj, hObj->m_ChangeFlags & 0x18);

	if ((g_bAutoDeactivate && !g_pServerMgr->m_Unknown210.Contains(hObj)) || !g_pServerMgr->m_pWorldFile)
		g_pServerMgr->m_Unknown210.BumpObject(hObj, 0);
}

void si_PingObjects(LTObject* hObj)
{
	if (!hObj)
		return;

	sm_PingObjects(g_pServerMgr, hObj);
}

void si_GetObjectPos(LTObject* hObj, LTVector* pos)
{
	if (!hObj)
	{
		pos->x = 0.0f;
		pos->y = 0.0f;
		pos->z = 0.0f;
		return;
	}

	*pos = hObj->m_Pos;
}

void si_SetObjectPos(LTObject* hObj, LTVector* pos)
{
	if (!hObj)
		return;

	sm_MoveObject(g_pServerMgr, hObj, pos, 0xf);
}

void si_SetObjectState(LTObject* hObj, int state)
{
	uint32 nBits;

	if (!hObj)
		return;

	switch (state)
	{
		default:
			nBits = 0;
			break;

		case OBJSTATE_AUTODEACTIVATE_NOW:
			if (!g_bAutoDeactivate)
				return;
			nBits = 0x20;
			break;

		case OBJSTATE_INACTIVE_TOUCH:
			nBits = 0x10;
			break;

		case OBJSTATE_INACTIVE:
			nBits = 8;
			break;
	}

	sm_SetObjectState(g_pServerMgr, hObj, nBits);
}

int si_GetObjectState(LTObject* hObj)
{
	uint32 nFlags;

	// Two separate returns tail-merge, keeping the inactive one inline; the
	// value temp keeps the flags in a register instead of an address temp.
	// The last two states are an if/else chain, not `(nFlags >> 3) & 2`:
	// VC6 derives the shift from the select and keeps the movzbl (server
	// 0x100316aa) / `and 0ffh` (lithtech 0x0047230e) that a written shift
	// loses. The else-if also puts the null refusal in the shipped place.
	if (!hObj)
		return OBJSTATE_INACTIVE;

	nFlags = hObj->m_ChangeFlags;
	if (nFlags & 8)
		return OBJSTATE_INACTIVE;
	else if (nFlags & 0x10)
		return OBJSTATE_INACTIVE_TOUCH;
	else
		return OBJSTATE_ACTIVE;
}

LTRESULT si_GetObjectRotation(LTObject* hObj, LTRotation* pRotation)
{
	if (!hObj)
	{
		pRotation->Init();
		RETURN_ERROR(1, "ILTPhysics::GetObjectRotation", LT_ERROR);
	}

	*pRotation = hObj->m_Rotation;
	return LT_OK;
}

// The six-field WMMoveState fill, as commonphysics.cpp's wm_InitMoveState
// (static there, so repeated here as a stand-in of the same shape; name
// ours). sm_SetObjectRotation below needs the helper, not six stores:
// its arguments are evaluated ahead of the body, which is what loads the
// 0x6a8 object before the container is formed and parks the priority in
// edx (lithtech 0x472478..0x4724a4). With the stores written out, lithtech
// was four blocks off and server.dll held the zero in edx instead of edi.
// The type test is likewise an inline helper (commonphysics.cpp's
// wm_IsWorldModelType shape): with a `char nType` local, lithtech laid the
// rotation-copy arm first and duplicated sm_SetObjectChangeFlag's error
// tail (523 bytes vs 500), and server.dll tested the type as a switch
// (`movsbl ; dec ; dec`).
inline LTBOOL sm_IsWorldModelType(LTObject* pObj)
{
	return pObj->m_ObjectType == OT_WORLDMODEL ||
	       pObj->m_ObjectType == OT_CONTAINER;
}

static inline void sm_InitMoveState(WMMoveState* pState, void* pContainer,
                                    void* pUnknown04, LTObject* pObj,
                                    uint32 nPriority)
{
	pState->m_pContainer = (CUnknown210*)pContainer;
	pState->m_pUnknown04 = static_cast<IObjectMover*>(pUnknown04);
	pState->m_pObj       = pObj;
	pState->m_nUnknown0C = nPriority;
	pState->m_pPushList = 0;
	pState->m_nPushCount = 0;
}

LTRESULT sm_SetObjectRotation(LTObject* hObj, LTRotation* pRotation, int bGuaranteed)
{
	WMMoveState state;

	state.m_pContainer = LTNULL;
	state.m_pUnknown04 = LTNULL;
	state.m_pObj = LTNULL;
	state.m_nUnknown64 = 0;

	if (!hObj)
		RETURN_ERROR(1, "ILTPhysics::SetObjectRotation", LT_ERROR);

	if (hObj->m_Rotation.m_Quat[0] == pRotation->m_Quat[0] &&
	    hObj->m_Rotation.m_Quat[1] == pRotation->m_Quat[1] &&
	    hObj->m_Rotation.m_Quat[2] == pRotation->m_Quat[2] &&
	    hObj->m_Rotation.m_Quat[3] == pRotation->m_Quat[3])
		return LT_OK;

	if (sm_IsWorldModelType(hObj))
	{
		sm_InitMoveState(&state, &g_pServerMgr->m_Unknown210,
		                 g_pServerMgr->m_pUnknown6a8, hObj, hObj->m_BlockingPriority);
		wm_RotateWorldModel(&state, pRotation, 1);
	}
	else
	{
		hObj->m_Rotation = *pRotation;
	}

	sm_SetObjectChangeFlag(g_pServerMgr, hObj, (bGuaranteed ? 0x400 : 0) | 4);
	return LT_OK;
}

LTRESULT si_SetObjectRotation(LTObject* hObj, LTRotation* pRotation)
{
	return sm_SetObjectRotation(hObj, pRotation, 1);
}

LTRESULT si_RotateObject(LTObject* hObj, LTRotation* pRotation)
{
	return sm_SetObjectRotation(hObj, pRotation, 0);
}

void si_ScaleObject(LTObject* hObj, LTVector* pScale)
{
	hObj->m_Scale = *pScale;
	sm_SetObjectChangeFlag(g_pServerMgr, hObj, 0x10);
}

LTRESULT si_GetObjectScale(LTObject* hObj, LTVector* pScale)
{
	if (!hObj || !pScale)
		RETURN_ERROR(1, "ILTPhysics::GetObjectScale", LT_ERROR);

	*pScale = hObj->m_Scale;
	return LT_OK;
}

LTRESULT si_GetModelCommandString(LTObject* hObj, char* pStr, uint32 maxLen)
{
	if (!hObj || !pStr || hObj->m_ObjectType != OT_MODEL)
		RETURN_ERROR(1, "ILTPhysics::GetModelCommandString", LT_INVALIDPARAMS);

	// The local names the MODEL and the command string is read at both uses:
	// naming the string instead is a one-register near in lithtech.exe
	// (0x00472c10, 170 of 169 bytes: the first error arm's format pointer
	// goes to %ecx where shipped reuses the dead %eax).
	Model* pModel = ((ModelInstance*)hObj)->m_AnimTracker.m_TimeRef.m_pModel;
	if (!pModel->m_pCommandString)
		RETURN_ERROR(1, "ILTPhysics::GetModelCommandString", LT_ERROR);

	// The full buffer length — no reserve for the terminator here.
	strncpy(pStr, pModel->m_pCommandString, maxLen);
	return LT_OK;
}

// am_RemoveAttachment (0x10025a1b / 0x0045bf10) is objbank.cpp's, the one
// definition; servermgr.h's declaration reaches it through link/aliases.rsp,
// as am_CreateAttachment's does. The second definition that stood here
// matched server.dll too but was two bytes off in lithtech.exe, and so was a
// function the client image has no counterpart for.

// The masked flag word is the `flags` PARAMETER, narrowed in place, not a
// local initialised from it — and that one word of source is what decides
// the callee-saved register assignment. With `uint32 nNew = flags & 1;` the
// local outranks the portal record and takes esi, leaving edi for the
// record; masking the parameter itself (`flags &= 1;`) starts its live range
// at function entry, which drops it below the record and produces the
// shipped esi=record / edi=flags pairing exactly. 232 of 232 bytes, and
// what had been written up as a register lottery was a source fact.
LTRESULT sm_SetPortalFlags(CServerMgr* pMgr, char* pPortalName, uint32 flags)
{
	CMessageRef msg;
	uint32 nWorld;
	uint32 nPortal;
	WorldPortal* pPortal;

	pPortal = w_FindPortal(&pMgr->m_World, pPortalName, &nWorld, &nPortal);
	if (!pPortal)
		RETURN_ERROR(1, "ILTServer::SetPortalFlags", LT_NOTFOUND);

	flags &= 1;
	if ((uint16)flags != pPortal->m_Flags)
	{
		{
			CAbstractIO* pIO;
			msg.Init(msg_AllocPacket(&pIO, 0x1000, 0x1000));
			if (pIO)
				pIO->Release();
		}

		msg.m_pMsg->WriteWord((uint16)nWorld);
		msg.m_pMsg->WriteWord((uint16)nPortal);

		// The BYTE goes through CPacket::WriteType<uint8> and the two words
		// do not, and lithtech.exe says so directly: at 0x00476790 the two
		// word writes are still calls (0x00476845, 0x00476851 -> 0x0042fa50)
		// and the byte write is EXPANDED at 0x00476856 -- `inc %edx` on
		// m_Pos, `inc %ecx` on m_DataLen, the SetAtGrow slow path at
		// 0x00476875 and the "CPacket::WriteType" / "ERROR: Packet write
		// overrun!" REPORT_ERROR at 0x004768b8, ending in
		// `or $0x4,0x40(%esi)` (PACKETFLAG_WRITEOVERFLOW). A member TEMPLATE
		// is an inline candidate at /Ob1 with or without the keyword;
		// ServerMessage::WriteWord is a non-template declared out of class,
		// which /Ob1 never expands. Same lever, same file, as
		// sm_DetachClient's WriteType<uint16> above.
		//
		// server.dll cannot move: ServerMessage::WriteByte IS
		// CPacket::WriteType<uint8> (both 0x1001393e -- netmgr.cpp says so),
		// so /O1 emits `call <same address>` for either spelling and
		// the byte comparison masks the relocated bytes.
		((CPacket*)msg.m_pMsg)->WriteType((uint8)flags);
		sm_BroadcastToClients(g_pServerMgr, 0x13, msg.m_pMsg);
	}

	pPortal->m_Flags = (uint16)flags;
	return LT_OK;
}

// What sm_SendSFXMessage hands its callbacks (names ours).
struct SFXSendInfo
{
	CServerMgr*    m_pMgr;
	ServerMessage* m_pMsg;
	LTObject*      m_pObject;
	uint8          m_nPacketID;
	uint32         m_Flags;
};

// The vis-query callbacks (names ours): the first sends to one client-owned
// object's owner, the second collects client-owned objects from a list.
void sm_SFXSendToOwner(LTObject* pObj, SFXSendInfo* pInfo)
{
	if (pObj->m_Unknown54 == 0 && pObj->m_pSData->m_pOwningClient)
	{
		sm_QueueMessageToClient(pInfo->m_pMgr, pObj->m_pSData->m_pOwningClient,
			pInfo->m_nPacketID, pInfo->m_pMsg, pInfo->m_Flags);
	}
}

LTRESULT sm_SendSFXMessage(CServerMgr* pMgr, int nPacketID, ServerMessage* pMsg,
                           LTObject* pObj, LTVector* pPos, uint32 flags)
{
	CVisQuery query;
	SFXSendInfo info;

	if (pMgr->m_ClientList.m_nElements == 0)
		return LT_OK;

	// One client, still connecting: send straight to it.
	if (pMgr->m_ClientList.m_nElements == 1)
	{
		Client* pClient = (Client*)pMgr->m_ClientList.m_Head.m_pNext->m_pData;
		if (pClient->m_ClientFlags & 2)
		{
			sm_QueueMessageToClient(pMgr, pClient, nPacketID, pMsg, flags);
			return LT_OK;
		}
	}

	info.m_pMgr = pMgr;
	info.m_pMsg = pMsg;
	info.m_pObject = pObj;
	info.m_nPacketID = (uint8)nPacketID;
	// Flags before the position copy: lithtech 0x4769c9 loads the flags
	// argument ahead of pPos (server.dll is identical either way).
	info.m_Flags = flags;
	query.m_Position = *pPos;
	query.m_pUser = &info;
	query.m_Radius = 10000.0f;
	query.m_Fn1 = [](LTObject* object, void* user) { sm_SFXSendToOwner(static_cast<LTObject*>(object), static_cast<SFXSendInfo*>(user)); };

	((IObjectQuery*)&pMgr->m_Unknown210)->FindObjects(&query);
	return LT_OK;
}

// The client-only statement inside sm_CacheFile's FT_MODEL arm, on the same
// LT_SERVER_BUILD discriminator serverengine.cpp's se_RegisterModel already
// uses (see the long note there; the macro's real name is not known).
// lithtech.exe's copy (0x00476ac0) has
//
//     push %ebx ; or %ebx,0x20(%eax) ; call 0x004149e0 ; add $0x4,%esp
//
// at 0x00476b56 where server.dll's copy (0x10034ca5) has only the `or`.
// 0x004149e0 is src/engine/lithtech/clientmgr2.cpp's cs_Sub4149e0 (already
// byte-identical there) -- reconstructed as no-argument, because its body
// never reads one, but this call site pushes a dword and cleans four bytes,
// so the prototype here takes one. Without the call the /O2 epilogue is
// scheduled ahead of the `|=` and the read-modify-write splits into
// load/or/store, which is the whole four-byte gap: with it both images are
// byte-exact.
#ifndef LT_SERVER_BUILD
void cs_Sub4149e0(int n);          // 0x004149e0 (client only)
#endif

// 0x10034b7a / 0x00476ac0 -- ILTServer::CacheFile's body. BYTE-IDENTICAL in
// both images. Four source facts, each one measured:
//
//  * IT IS A SWITCH, not an if/else chain. lithtech.exe settles it: /O2 emits
//    `cmp $0x3,%ebx ; ja default ; jmp *0x476cb0(,%ebx,4)` over a four-entry
//    jump table at 0x00476cb0. server.dll's /O1 spells the same switch as a
//    linear range test (`cmp %eax,%edi ; je case0 ; jbe default ; cmp $0x2
//    ; jbe case12 ; cmp $0x3 ; jne default`) -- the `jbe` after a compare
//    against a register known to hold zero is the giveaway: it is the
//    range tree's "below the first case" edge, not anything a hand-written
//    `fileType >= 1` would produce. Written as an if/else chain the function
//    came out 384 of 383 with fourteen differing mnemonic blocks; as a
//    switch, four. VC6 emits the case BODIES in reverse source order at /O1,
//    which is why the shipped layout runs case 3, case 1-2, case 0 while the
//    tests run 0, 1-2, 3.
//
//  * THE FT_SPRITE/FT_TEXTURE ARM HAS NO CACHE-RECORD BRANCH. Its shipped
//    block (0x10034c41) goes straight from the fs_GetFileIdentifier test to
//    `if (pFile) sm_BroadcastCacheFile(...)` with no `testb $0x10,0x1a0`
//    anywhere -- only FT_MODEL and FT_SOUND consult the flag. Worth 24 bytes.
//
//  * THE FT_MODEL ARM IS AN IF/ELSE, NOT A GUARD. All three arms raise the
//    same RETURN_ERROR_INFO and VC6 tail-merges them, but the FT_MODEL copy
//    can only share the suffix from the `jl` on (esi holds 1 there, so its
//    g_DebugLevel compare is `cmp %esi,mem` and the other two `cmpl $0x1,mem`).
//    Which copy hosts that suffix follows from which one is emitted LAST, and
//    writing the FT_MODEL test as `if (... == LT_OK) { ... } else { RETURN }`
//    sinks its error block past the case body to the end of the function --
//    exactly the shipped 0x10034cc8. As a guard (`if (... != LT_OK) RETURN;`)
//    the error block is the fall-through instead, the FT_SOUND/FT_SPRITE copy
//    ends up hosting the suffix, and the function is 397 of 383 -- same 134
//    instructions, fourteen bytes of near-vs-short jumps.
//
//  * pFile IS ONE FUNCTION-SCOPE LOCAL, not one per arm. Three block-scoped
//    copies put FT_MODEL's pFile in the pFilename parameter slot and pModel
//    in pMgr's; the shipped frame has pFile at 0x8(%ebp) in all three arms
//    and pModel at 0x10(%ebp). Last four bytes. (Swapping the two block-scoped
//    declarations instead changes nothing -- measured.)
LTRESULT sm_CacheFile(CServerMgr* pMgr, uint32 fileType, char* pFilename)
{
	CFileIdentifier* pFile;

	if (!g_bCachingEnabled)
		return LT_OK;

	if (!pFilename)
		RETURN_ERROR(1, "ILTServer::CacheFile", LT_ERROR);

	switch (fileType)
	{
		case FT_MODEL:
		{
			Model* pModel;

			if (sm_LoadModel(pMgr, pFilename, &pModel, &pFile, 1, 1) == LT_OK)
			{
				if (pMgr->m_Unknown1a0 & 0x10)
				{
					pModel->m_Flags |= 1;
#ifndef LT_SERVER_BUILD
					cs_Sub4149e0(1);
#endif
				}
				else if (pFile)
				{
					sm_BroadcastCacheFile(pMgr, 0, pFile->m_nFileID);
				}
			}
			else
			{
				RETURN_ERROR_INFO(1, "ILTServer::CacheFile", LT_NOTFOUND, pFilename);
			}
		}
		break;

		case FT_SPRITE:
		case FT_TEXTURE:
		{
			if (!fs_GetFileIdentifier(&pMgr->m_FileMgr, pFilename, 0, (void**)&pFile))
				RETURN_ERROR_INFO(1, "ILTServer::CacheFile", LT_NOTFOUND, pFilename);

			if (pFile)
			{
				sm_BroadcastCacheFile(pMgr, fileType, pFile->m_nFileID);
			}
		}
		break;

		case FT_SOUND:
		{
			if (!fs_GetFileIdentifier(&pMgr->m_FileMgr, pFilename, 0, (void**)&pFile))
				RETURN_ERROR_INFO(1, "ILTServer::CacheFile", LT_NOTFOUND, pFilename);

			if (pMgr->m_Unknown1a0 & 0x10)
			{
                try { pMgr->m_Sounds.MarkWanted(*pFile); }
                catch (const std::bad_alloc&) { return LT_OUTOFMEMORY; }
                catch (const std::exception&) { return LT_ERROR; }
			}
			else if (pFile)
			{
				sm_BroadcastCacheFile(pMgr, 3, pFile->m_nFileID);
			}
		}
		break;
	}

	return LT_OK;
}


// ---- the object map's array ---------------------------------------------- //

// 0x1000c4cf (Init) and 0x1002cbe3 (the constructor) are
// CMoArray<CObjectMapEntry>'s own; see servermgr.h.


// ---- the world teardown -------------------------------------------------- //

// 0x1002af34 — every object still in the world goes out through the ordinary
// removal, so the game hears about each one. Objects the flag does not mark
// are left for the caller (name ours).
//
// The head is named FIRST and the cursor initialised through it. Written the
// other way round (`pCur = pMgr->m_List8dc.m_Head.m_pNext; pHead =
// &pMgr->m_List8dc.m_Head;`) the instruction sequence is identical and all
// three callee-saved registers rotate — ebx/esi/edi hold the head, the next
// link and the manager instead of the manager, the head and the next link.
// Same lever as sub_1002af00 and sm_EndWorldB.
void sm_EndWorldA(CServerMgr* manager) {
    struct Context { CServerMgr* manager; ServerCallbackGuard guard; } context{manager,ServerCallbackGuard(manager->m_Lifetime)};
    object_Visit(manager->m_List8dc,[](void* pointer,LTObject& object) {
        auto& context = *static_cast<Context*>(pointer);
        if (!context.guard.unchanged()) return false;
        if ((object.m_ChangeFlags & 0x80) && object.m_pSData && object.m_pSData->m_pBaseClass)
            sm_RemoveObjectFromWorld(context.manager,object.m_pSData->m_pBaseClass);
        return context.guard.unchanged();
    },&context);
}

// 0x1002aec2 — the other list holds records the manager owns outright, so
// they go straight back to the allocator and the list is tied off empty
// (name ours).
//
// Head-first, cursor through the head, for the same reason sm_EndWorldA is.
void sm_EndWorldB(CServerMgr* manager) { manager->m_ClientReferences.Clear(); }

// 0x1002abf8 — an object goes back to its class's bank. The game's destructor
// runs first, through the class definition's own pointer, because the object
// belongs to the game module; the memory then goes onto the per-class free
// list rather than back to the allocator (name ours).
void sm_FreeObject(CServerMgr* pMgr, ClassDef* pClassDef, LPBASECLASS pObject)
{
	(void)pClassDef;
	pMgr->m_ClassMgr.destroy(pObject);
}


// 0x1003401a — a client's name into the caller's buffer. The manager is not
// touched: a client already knows its own name.
void CServerMgr::GetClientName(Client* client, char* name, int capacity) {
    if (client) client->CopyName(name,capacity);
    else if (name && capacity > 0) name[0] = 0;
}

// 0x100277c2 — everything the client still has queued goes out now, at
// whatever the manager's current time is (name ours).
void sm_FlushClientPackets(CServerMgr* pMgr, Client* pClient)
{
	// Simulation clears/rebases its delta independently of transport time.
	xfer_Update(pClient->m_FileTransfer.get(), pMgr->m_NetMgr.m_FrameTime);
}


// ---- the manager's own slots --------------------------------------------- //
//
// Every one of these publishes the manager through the globals first: the
// net layer can reach them at times when nothing else has.

LTBOOL CServerMgr::_v04(NetConnection* connection, uint32 flags) {
    SetupGlobals();
    return sm_AddClientForConnection(this,connection,static_cast<int>(flags)) != nullptr;
}

void CServerMgr::_v08(NetConnection* pConn)
{
	SetupGlobals();

	sm_DropConnection(this, pConn);
}

// A packet the net layer made nothing of goes to the application first and
// then to the game; either may be absent, and neither answer is looked at.
void CServerMgr::ProcessPacket(CPacket* pPacket, uint8* senderAddr, uint16 senderPort)
{
	ServerActivity activity(m_Lifetime);
	if (!activity.available() || !pPacket || !senderAddr || !pPacket->m_DataLen ||
        pPacket->m_DataLen > pPacket->m_Data.GetSize()) return;
    ServerCallbackGuard operation(m_Lifetime);
	if (m_pAppHandler)
	{
		m_pAppHandler->ProcessPacket((char*)pPacket->m_Data.m_pArray,
		                             pPacket->m_DataLen,
		                             senderAddr, senderPort);
	}
	if (!operation.unchanged()) return;

	if (m_ClassMgr.m_pServerShell)
	{
		m_ClassMgr.shell([&](IServerShell& shell) { return shell.ProcessPacket((char*)pPacket->m_Data.m_pArray,
		                                         pPacket->m_DataLen,
		                                         senderAddr, senderPort); });
	}
}

// SoundInstance::Term is DEFINED ABOVE CServerMgr::RemoveDeadSounds,
// `inline` -- see the note there.

// 0x100338b8 — the shell caches what it wants, and everything nothing is
// using afterwards is let go. The flag says the caching is over.
LTRESULT sm_CacheFiles(CServerMgr* pMgr)
{
	ServerCallbackGuard operation(pMgr->m_Lifetime);
	pMgr->m_ClassMgr.shell([&](IServerShell& shell) { return shell.CacheFiles(); });
	if (!operation.unchanged()) return LT_ERROR;

	pMgr->m_Unknown1a0 &= ~0x10;

	pMgr->FreeUnusedModels();
	sm_FreeUnusedSounds(pMgr);

	return LT_OK;
}


// 0x1002bab8 — take the object out of whatever container its type lives in.
// Types with nothing to do have no handler and the answer is nothing.
uint32 sm_RemoveFromWorldTree(CServerMgr* pMgr, LTObject* pObject)
{
	if (pObject->m_ObjectType >= NUM_OBJECTTYPES) return LT_INVALIDPARAMS;
	uint32 (*pFn)(CServerMgr*, LTObject*) =
		g_ObjTypeHandlers[pObject->m_ObjectType].m_pRemove;

	if (pFn)
		return pFn(pMgr, pObject);

	return 0;
}

// 0x1003471d — the queued-object list goes back to the manager's own bank,
// one node at a time, and the list is left empty (name ours).
void sm_FreeList44(CServerMgr* pMgr) {
    sm_FlushList44(pMgr);
    pMgr->m_Sounds.Clear();
}

// 0x1003512f — the worker thread's own construction; the base runs first and
// the manager is filled in later, by Init.
CServerThread::CServerThread()
{
	m_pServerMgr = LTNULL;
}

// 0x10035145 — which manager the thread works for.
LTBOOL CServerThread::Init(CServerMgr* pMgr)
{
	m_pServerMgr = pMgr;

	return LTTRUE;
}


// 0x10016971 — the notification slot's default is to ignore the event. Three
// bytes, `ret 0xc`: the arguments are accepted and none of them is read, and
// nothing is put in eax either, so a caller that looks at the result of this
// override reads whatever the last call left behind.



// 0x1002d34a — a one-line forward, and the only reason it exists is that the
// worker takes the manager by argument where the caller has it in `this`.
// The worker (0x1002d355) is what actually builds the object; nothing names
// either it or what it builds, so both keep their addresses.
CUnknown928Obj* sub_1002d355(CServerMgr* pMgr);

CUnknown928Obj* sm_CreateUnknown928(CServerMgr* pMgr)
{
	return sub_1002d355(pMgr);
}


// 0x10027135 — a connection going away only matters if a client was sitting
// on it. The lookup answers null for a connection that never got that far —
// one dropped during the handshake — and then there is nothing to remove.
void sm_DropConnection(CServerMgr* pMgr, NetConnection* pConn)
{
	Client* pClient = sm_FindClientByConnection(pMgr, pConn);

	if (pClient)
		sm_RemoveClient(pMgr, pClient);
}


// 0x10027be0 and 0x100348cc — whether this client still owes the object an
// update, and the queue it goes on when it does (names ours, from the shape
// of the call; nothing in the image names either).
//
// Both take the manager and the object, which settles sm_VisCallbackA's
// argument roles: its *second* parameter is the LTObject and the manager
// comes out of the first's +0x00.
// The model instance's own model pointer, which is serverengine.h's
// ModelInstanceObj::m_pModel; declared here so this file need not pull that
// header in.
// 0x10027be0 — does this object owe the clients an update at all? Change
// flag 4 has to be set, and unless FLAG_FORCECLIENTUPDATE overrides it the
// object also has to be one the clients can perceive: visible, or solid or
// ray-hittable, and an OT_NORMAL has to be carrying the engine-data flag at
// SObjData+0x2c. A model still holding the placeholder has nothing to send.
//
// `inline`, and defined above every caller: lithtech.exe (/O2) expands it at
// all four sites -- sm_VisCallbackA (0x00464ed0), sub_10027918 (0x00464a00,
// twice) and sm_VisGatherObjects (0x00465e00) -- and has no copy of its own,
// while server.dll's /Os declines every site and keeps 0x10027be0.
inline int sub_10027be0(CServerMgr* pMgr, LTObject* pObject)
{
    if (!pObject || !pObject->m_pSData || pObject->m_pSData->m_Removing) return 0;
	uint32 nFlags = pObject->m_Flags;

	if (!(nFlags & FLAG_FORCECLIENTUPDATE))
	{
		if (!(nFlags & FLAG_VISIBLE) && !(nFlags & (FLAG_RAYHIT | FLAG_SOLID)))
			return 0;

		if (pObject->m_ObjectType == OT_NORMAL &&
			pObject->m_pSData->m_SFXMessage.m_pMsg == 0)
			return 0;
	}

	if (!(pObject->m_ChangeFlags & 4))
		return 0;

	if (pObject->m_ObjectType == OT_MODEL &&
		static_cast<ModelInstance*>(pObject)->m_AnimTracker.m_TimeRef.m_pModel == pMgr->m_pPlaceholderModel)
		return 0;

	return 1;
}

// 0x100348cc — restart the object's update countdown and put it back in the
// state its change flags now ask for. The deactivation time is the interval;
// a zero or negative one is copied through unchanged, which is why one arm
// stores through the FPU and the other with an integer `mov`.
void sub_100348cc(CServerMgr* manager, LTObject* pObject)
{
	SObjData* pSData = pObject->m_pSData;

	if (pSData->m_DeactivationTime > 0.0f)
		pSData->m_UpdateTimer = manager->m_FrameTime + pSData->m_DeactivationTime;
	else
		pSData->m_UpdateTimer = pSData->m_DeactivationTime;

	if (pObject->m_ChangeFlags & 0x20)
		sm_SetObjectState(manager, pObject, pObject->m_ChangeFlags & 0x18);
}

// An object the sweep may deactivate is told so through its own vptr, slot 2.
// ltobject_internal.h models LTObject as a plain structure, which is right
// for every other use of it in this file, so the one virtual call reached
// from here goes through a view of the same object rather than making the
// whole class polymorphic (name ours; only the slot index is established).
// 0x10027c3c — one candidate object out of the visibility gather. Three tests
// have to pass before anything happens, and they are asked in the cheapest
// order: a field on the context, then the worker, then a second context field.
//
// The context is the first argument and the object the second, which is the
// reverse of what the parameter names suggest -- the fields read off the
// first argument (+0x54, and the flag byte at +0x121 it clears) belong to the
// thing being written to, and the second's +0x00 is what the worker is handed.
// Left named as declared; the argument roles are recorded here rather than
// renamed, because what either structure is has not been established.
void sm_VisCallbackA(LTObject* object, void* pointer) {
    auto& context = *static_cast<CUpdateContext*>(pointer);
    if (!context.ClientAlive() || object->m_Unknown54 || !sub_10027be0(context.m_pMgr,object)) return;
    ObjectCallbackGuard guard(*object);
    if (context.m_bLocal) {
        object->m_ChangeFlags &= ~0x200;
        sub_100348cc(context.m_pMgr,object);
    }
    if (context.ClientAlive() && guard.unchanged()) context.MarkVisible(*object);
}

// 0x1002a11e — hand one arrived message to the game's shell (name ours). The
// enumeration gives this 188 bytes; the `ret` is at 0x1002a166, so it is 73
// and the row runs into the function after it.
//
// The message id is the packet's LAST byte, not its first: ReadByteFromEnd
// takes it off the end and shortens the packet, so what the shell is handed
// through m_Write is the payload with the id already removed.
//
// That id lives in the pMgr parameter's slot -- pMgr is in %esi from the
// first instruction -- and it is stored as a byte and pushed as a dword, so
// the three bytes above it are whatever the pointer left there. Harmless,
// since the callee's parameter is a uint8, and it is the same byte-saving
// trick CBaseStream::ReadString's length uses.
//
// lithtech.exe 0x00469390 (120 bytes; the enumeration's 286 ran on into
// the next handler at 0x00469410) EXPANDS ReadByteFromEnd at
// 0x004693b6..0x004693e7 where server.dll calls 0x1002a1da. /O1 still calls
// it, so the header declaration stays out of line and /O2 gets this twin of
// servermsg.cpp's body (a stand-in: the shipped header presumably defined it
// inline, which server.dll's /O1 then declined to expand).
#ifndef LT_SERVER_BUILD
inline uint8 ReadByteFromEndInline(ServerMessage* pMsg)
{
	if ((uint32)(pMsg->m_Unknown34 - pMsg->m_Unknown38) >= 1)
	{
		uint8 nRet = pMsg->m_pData[pMsg->m_Unknown34 - 1];

		pMsg->m_Unknown34--;

		return nRet;
	}

	return 0;
}
#endif

// 0x1002a0d0 — packet handler 6 (name by address), lithtech.exe 0x00469330:
// the sender asks to go, so its connection is dropped with reason 5. Only a
// remote sender has a connection (ServerMessage::m_pConnection is null for
// local packets); the third argument is not read. Both images: `mov
// 0x3c(%eax),%eax ; test ; je`, then `push $0 ; push $5 ; push %eax` and a
// thiscall on pMgr+0x6e0 to CNetMgr::Disconnect (0x10021c18 / 0x00456320),
// and `xor %eax,%eax` at the one exit.
int sub_1002a0d0(CServerMgr* pMgr, ServerMessage* pMsg, Client* pClient)
{
	if (pMsg->m_pConnection)
		pMgr->m_NetMgr.Disconnect(pMsg->m_pConnection, 5, 0);

	return 0;
}

// 0x1002a0f2 — packet handler 10 (name by address): a console string from a
// client, run on the server's own console, but only for a client whose
// internal flags carry bit 1 (m_ClientFlags, +0x3c0); the answer is the
// console's (cc_RunConsoleString, sharedcommon.cpp). The string is read in
// place off the packet (servermsg.cpp's CMsgStringReader, 0x1002609f), and
// the read comes first: the second argument is evaluated before the first.
int sub_1002a0f2(CServerMgr* manager, ServerMessage* packet, Client* client) {
    if (!manager || !packet || !client || !(client->m_ClientFlags & CLIENTFLAG_LOCAL)) return LT_OK;
    auto* command = packet->ReadStringInPlace();
    return command ? cc_RunConsoleString(&manager->m_ConsoleState,command) : LT_INVALIDPARAMS;
}

LTRESULT sm_GiveMessageToShell(CServerMgr* pMgr, ServerMessage* pMsg,
                               HCLIENT hClient)
{
	uint8 nMsgId;

	// One positive condition around the body, not two guarded returns: the
	// shipped LT_OK sits at the very end with both tests jumping to it and
	// the body falling into it. As guards it goes inline in the middle
	// instead.
	if (hClient && pMsg && pMsg->CanRead(1) && pMgr->m_ClassMgr.m_pServerShell)
	{
		pMgr->InitMessageOwner(pMsg);

#ifdef LT_SERVER_BUILD
		nMsgId = pMsg->ReadByteFromEnd(0);
#else
		nMsgId = ReadByteFromEndInline(pMsg);
#endif

		pMgr->m_ClassMgr.shell([&](IServerShell& shell) { return shell.OnMessage(
			hClient, nMsgId, (HMESSAGEREAD)&pMsg->m_Write); });
	}

	return LT_OK;
}


// 0x1002a167 / lithtech.exe 0x00469410 — packet handler 9 (name by
// address): one byte off the packet, and the client's handshake stage
// (m_Unknown3BC) moves to 3 if it is set and to 1 if not. The read is
// CPacket::ReadType<uint8>: server.dll calls 0x100138d6, lithtech.exe
// expands it with its "CPacket::ReadType" overrun report (0x00469416..
// 0x0046947f). The lithtech row was the tail of the enumeration's 286-byte
// 0x00469390 row.
int sub_1002a167(CServerMgr* pMgr, ServerMessage* pMsg, Client* pClient)
{
    (void)pMgr;
    if (!pMsg || !pClient || pClient->m_State != 2 || !pMsg->CanRead(1) ||
        pMsg->m_DataLen-pMsg->m_Pos != 1) return LT_OK;
    const auto stage = pMsg->m_pData[pMsg->m_Pos];
    if (stage == 0 && pClient->m_Unknown3BC == 0) pClient->m_Unknown3BC = 1;
    else if (stage == 1 && pClient->m_Unknown3BC == 2) pClient->m_Unknown3BC = 3;
    else return LT_OK;
    ++pMsg->m_Pos;
    return LT_OK;
}

#ifdef LT_SERVER_BUILD
// 0x1002a193 / lithtech.exe 0x004694b0 — packet handler 5 (name by
// address): the client data the game hands over when it connects (a uint16
// length, then that many bytes), into a new buffer at Client+0x110 with
// its length at +0x114, which OnClientEnterWorld passes the server shell;
// then the flag word's 0x100 bit. CPacket::ReadType<uint16> is the call
// to 0x1000ebd0 (lithtech.exe expands it), the copy servermsg.cpp's
// ReadData (0x100260d9).
//
// Server-only: lithtech.exe's is lt2new1.cpp's sub_4694b0. Until it was
// written, server.dll's link/aliases.rsp stood LTMatrix::operator* in for
// it, so the first client to join a dedicated server (the one program that
// runs server.dll, NolfServ.exe) sent this packet into a matrix multiply,
// and the server stopped.
// servermsg.h types ReadData's length uint32; the call here loads it as
// sixteen bits (`mov (%esi),%cx`), as netmgr.cpp's view of the same function
// does (link/aliases.rsp joins the two).
int sub_1002a193(CServerMgr* manager, ServerMessage* packet, Client* client) {
    if (!manager || !packet || !client) return LT_OK;
    const auto result = client->ReceiveData(*packet);
    if (result != LT_OK && client->m_pConnection) manager->m_NetMgr.Disconnect(client->m_pConnection,6,result);
    return LT_OK;
}

#endif


// 0x1002a205 — after a flag change, put the object back in the containers
// under its new flags. An object the sweep is allowed to deactivate is
// re-seated in the world's own container; one it is not gets its second
// vtable slot called instead and never touches the container at all.
//
// Answers LT_OK on every path, including the one that does nothing.
LTRESULT sm_UpdateObjectBBox(CServerMgr* pMgr, LTObject* pObj)
{
	if (!sm_CanDeactivate(pObj))
	{
        pMgr->m_Unknown210.BumpObject(pObj,0);
	}
	else
	{
		pObj->_v08();
	}

	return LT_OK;
}


// 0x10024cac and 0x10024cad — what a visibility query does by default: a
// one-byte `ret`, and a predicate that answers yes. Both exist only so the
// gather can call through the slots without testing them, which is why the
// constructor installs them rather than leaving nulls (names ours).
static void vq_Ignore(void* pUser, void* pObject)
{
}

// Two of them, not one used twice: the shipped constructor stores the same
// address into both slots as an immediate each time, where one source
// function assigned twice makes VC6 hold the pointer in a register and store
// from that. Two symbols mean two relocations and no such sharing -- and
// with /Gy the linker then folds the identical bodies back onto one address,
// which is why only 0x10024cac survives in the image.
static void vq_IgnoreToo(void* pUser, void* pObject)
{
}

static int vq_Accept(void* pUser, void* pObject)
{
	return 1;
}


// Complete owned tickets on the owner thread, including queue-allocation fallback.
void CServerMgr::ProcessThreadMessages() {
    ServerCallbackGuard operation(m_Lifetime);
    m_FileMgr.dispatch_notifications();
    if (!operation.unchanged()) return;
    LThreadMessage message;
    while (m_Thread.m_OutgoingQueue.GetMessage(&message,0) == LT_OK) {
        m_FileMgr.dispatch_notifications();
        if (!operation.unchanged()) return;
        OnModelLoaded(&message);
        if (!operation.unchanged()) return;
    }
    for (const auto& [ticket,name] : m_Models.Ready()) {
        CFileRef* file = nullptr;
        m_FileMgr.identify(name,0,&file);
        if (!operation.unchanged()) return;
        LThreadMessage completion;
        completion.m_Data[3] = reinterpret_cast<uintptr_t>(file);
        completion.m_Data[5] = ticket;
        OnModelLoaded(&completion);
        if (!operation.unchanged()) return;
    }
    m_FileMgr.dispatch_notifications();
    if (!operation.unchanged()) return;
}



// 0x1002ba90 — hand the object's new file names to whatever knows about that
// object's type. A type with nothing to say has no handler.
LTRESULT w_ApplyObjectFilenames(CServerMgr* pMgr, LTObject* pObj,
                                ObjectCreateStruct* pStruct)
{
	if (pObj->m_ObjectType >= NUM_OBJECTTYPES) return LT_INVALIDPARAMS;
	uint32 (*pFn)(CServerMgr*, LTObject*, ObjectCreateStruct*) =
		g_ObjTypeHandlers[pObj->m_ObjectType].m_pApplyFilenames;

	if (pFn)
		return pFn(pMgr, pObj, pStruct);

	return 0;
}


// 0x1002cecf — the object map's allocate is CMoArray<CObjectMapEntry>::
// _AllocateTArray (servermgr.h).

// 0x1002a239 — may the sweep deactivate this object? Anything the engine
// still owes work on is left alone, and so is anything carrying one of the
// flags that mean "keep me live" — unless it also carries the one that
// overrides them (name ours).
// MOVED TO servermgr.h, `inline`. It has to be a header body for two
// independent reasons: /Ob1 never expands a file-scope function, and VC6's
// inliner is FORWARD-ONLY so the caller at line ~5152 above precedes this
// definition anyway. lithtech.exe has NO standalone copy -- the constant
// 0x21213001 occurs exactly twice in .text, at 0x0047c568 and 0x00469588,
// and both are expansions. server.dll keeps 0x1002a239 and CALLS it from
// both of its sites, which is the census that makes the move /O1-safe.


// ======================================================================== //
//  Reconstructions. Each carries the shipped address it
//  was read off; anything the headers do not model is declared here as a
//  local view and cast through, exactly as IObjectDeactivate above.
// ======================================================================== //


// 0x1003481c — every live sound instance is terminated and handed back to
// the bank at 0x28, and the list left empty.
//
// The name in servermgr.h is a misnomer this reconstruction cannot fix
// (the header is not ours to edit): the list walked here is m_List54, the
// SoundInstance list, not m_List44. sm_ReturnList44 (0x1003471d) calls this
// first and then does the same job for m_List44.
void sm_FlushList44(CServerMgr* pMgr)
{
	LTLink* pCur;
	LTLink* pHead;
	LTLink* pNext;

	pCur = pMgr->m_List54.m_Head.m_pNext;
	pHead = &pMgr->m_List54.m_Head;

	while (pCur != pHead)
	{
		SoundInstance* pSound = (SoundInstance*)pCur->m_pData;
		pNext = pCur->m_pNext;

		pSound->Term();
		pMgr->m_SoundInstances.Free(pSound);

		pCur = pNext;
	}

	dl_InitList(&pMgr->m_List54);
}


void sm_FreeUnusedSounds(CServerMgr* pMgr) { pMgr->m_Sounds.Collect(); }

// 0x10009d1e — the world's object store is shut down through its own vtable
// (slot 1) and then forgotten.
//
// The pointer lives at CServerWorld+0x184, which is well past the eight
// bytes servermgr.h gives the class; Term (0x10009be5) reaches +0x1d4. The
// class is therefore much bigger than the header models, so the member is
// reached through a view here rather than declared.
// 0x10028a35 is CPacket::WriteType instantiated for *float* — not for a
// generic four-byte value: the store is `flds`/`fstps`, which only the float
// specialisation emits, and `push $4; pop %ebx` is sizeof(float). packet.h
// carries the template; this is the translation unit its float instantiation
// belongs to, so the call sites reach it through a CPacket view of the
// message and the writes below are what emit it.

// 0x10027c71 — the unguaranteed stream is closed off with a sentinel object
// id and the server's current time. The client parameter is accepted and
// never read (the manager supplies the time and the message is the third
// argument), which is why the declared signature keeps it.
void sm_Unknown27c71(CServerMgr* pMgr, Client* pClient, ServerMessage* pMsg)
{
	// Both are CPacket::WriteType: lithtech.exe expands both (0x00464f67,
	// 0x0046501c) with Insert2 left a call; server.dll calls both.
	((CPacket*)pMsg)->WriteType((uint16)0xffff);
	((CPacket*)pMsg)->WriteType(pMgr->m_Time);
}


// 0x10021c55 — the net manager's queued send. servermgr.h's CNetMgr does not
// model it (SendPacket at 0x10022990 is a different function): this one
// masks the caller's flags to 0xc0, refuses a connection whose queues are
// over the limit, and answers 0/1. Reached through a view of the embedded
// manager; the name is ours, from the arguments.


// 0x100297b8 — stamp the packet id into the payload's first byte and hand
// the message to every client that is actually in the world. The id is
// written once, into the shared message, rather than per client.
void sm_BroadcastToClients(CServerMgr* pMgr, int nPacketID, ServerMessage* pMsg)
{
	LTLink* pHead;
	LTLink* pCur;

	pMsg->m_pData[0] = (uint8)nPacketID;

	pHead = &pMgr->m_ClientList.m_Head;
	pCur = pHead->m_pNext;

	while (pCur != pHead)
	{
		Client* pClient = (Client*)pCur->m_pData;

		if (pClient->m_pConnection && pClient->m_State == 3)
		{
			(&pMgr->m_NetMgr)->SendToConnection(
				pMsg, pClient->m_pConnection, 0x80);
		}

		pCur = pCur->m_pNext;
	}
}


// Ends the intrinsic region opened before UpdateObjects: every memset below
// this point is a call in server.dll. server.dll only: lithtech.exe's /O2
// expands the memsets below as `rep stos` (sm_AddSoundRecord 0x00468231,
// sm_AddClientForConnection 0x004637e2), so for it the region stays open.
#ifdef LT_SERVER_BUILD
#pragma function(memset)
#endif

// (CObjQueueBank and g_ObjQueueBank are declared near the top of the file,
// beside the free-list head Term and sm_DrainObjectQueue use.)

// 0x1002ac20 — take an object out of the world. Nothing happens while the
// manager is already sweeping objects (bit 0 of 0x1a0) or if this object is
// already queued (bit 0 of its change flags); otherwise it goes on the
// queued-object list at 0x83c, is marked queued, has its engine-side memory
// released, and loses the flag at bit 2.
//
// The list head at 0x83c is a bare LTLink with no element count, so the
// insert is dl_Insert and not dl_AddHead.
void sm_RemoveObject(CServerMgr* pServerMgr, HOBJECT hObj)
{
    if (!pServerMgr || !pServerMgr->m_Unknown390.m_Allocated.contains(hObj) || !hObj->m_pSData || hObj->m_pSData->m_Removing) return;
	if (~pServerMgr->m_Unknown1a0 & 1)
	{
		if (~hObj->m_ChangeFlags & 1)
		{
			LTLink* pLink = g_ObjQueueBank.Allocate();
            if (!pLink) { pServerMgr->m_ErrorCode = LT_OUTOFMEMORY; return; }

			pLink->m_pData = hObj;
			dl_Insert(&pServerMgr->m_Link83c, pLink);

			hObj->m_ChangeFlags |= 1;

			obj_ReleaseMemory(hObj);

			hObj->m_ChangeFlags &= ~4;
		}
	}
}


LTRESULT sm_StartWorldA(CServerMgr* pMgr, int bCaching)
{
	void* hElement;

	pMgr->m_CachedFiles.clear();

	pMgr->m_Sounds.BeginPass();

	hElement = dhash_GetFirst(pMgr->m_hModelHash);
	while (hElement)
	{
		auto* model = static_cast<Model*>(dhash_GetData(dhash_GetNext(&hElement)));
        model->m_Flags &= ~1u;
	}

	if (bCaching)
		pMgr->m_Unknown1a0 |= 0x10;
	else
		pMgr->m_Unknown1a0 &= ~0x10;

	return LT_OK;
}


int sm_PrepClientEnterWorld(CServerMgr* manager, Client* client) {
    return client && client->ReadyForWorld(manager->m_Unknown198 != 0);
}

// 0x1000e845 — how much room is left in the message: m_MaxDataLen (+0x36)
// minus the write position (+0x38) minus a seven-byte header reserve,
// clamped at zero. A thiscall (the splitter loads the message into ecx and
// pushes nothing), so it is a member ServerMessage does not declare;
// reached through a view.
//
// The name here used to be GetDataLen, which was the wrong way round: every
// comparison below reads as "does the message still fit". netmgr.cpp's
// declaration of the same address as GetSpaceLeft is the accurate one and
// is what the definition carries.
// 0x100277e2 — whatever the manager owes a packet before it goes out (two
// arguments, the manager and the message; named by address, nothing in the
// image says more).
extern uint32 g_bPacketTrace58474;
extern uint32 g_bTraceMarker51c64;
void sub_100277e2(CServerMgr* manager, ServerMessage* packet) {
    if (!g_bPacketTrace58474 || !manager || !packet) return;
    if (!manager->m_pPacketTrace) manager->m_pPacketTrace = streamsim_Open("packet.trc","wb");
    auto* stream = manager->m_pPacketTrace;
    if (!stream || stream->ErrorStatus() != LT_OK) return;
    if (stream->Write(packet->m_pData,packet->m_Unknown34) != LT_OK) return;
    if (g_bTraceMarker51c64) stream->Write("*END*",5);
}

// 0x100278ad — close the update packet off and start the next one, unless it
// is not full enough to bother: a threshold of -1 means "always", otherwise
// the packet has to be under it. The guaranteed stream (nA == 8) goes with
// flag 0x80 and the unguaranteed with none.
//
// LTBOOL, not the void servermgr.h declares (as sm_EndUpdatePacket): the
// body answers 0 on the "not yet" path and 1 when it sent, and VC6 could not
// have compiled a value-returning function that fell off the end. Renamed
// here for that reason; every call site discards the answer.
LTBOOL sm_FinishUpdatePacket(CUpdateContext* pCtx, ServerMessage* pMsg,
                             int nA, int nThreshold)
{
	if (nThreshold != -1 && (static_cast<ServerMessage*>(pMsg))->GetSpaceLeft() >= nThreshold)
		return LTFALSE;

	{
		uint32 nFlags = 0;

		if ((uint8)nA == 8)
			nFlags = 0x80;

		sub_100277e2(pCtx->m_pMgr, pMsg);

		sm_SendToClient(pCtx->m_pMgr, pCtx->m_pClient, nA, pMsg, 0, nFlags);

		pMsg->m_Unknown38 = 1;
		pMsg->m_Unknown34 = 1;
		// CPacket::WriteType<uint8>: server.dll's /O1 calls it (0x1001393e),
		// lithtech.exe's /O2 expands it (0x004648a6..0x004649ff, the `cmp $2`
		// folding the m_Pos just stored).
		((CPacket*)pMsg)->WriteType((uint8)0);

		pCtx->m_Unknown18++;
	}

	return LTTRUE;
}


// 0x1000e3f9 once more, through a two-string fixed prototype: the unfreed-
// sound warning batches its stack cleanup with fs_GetFilename's, which is
// what says this site was not compiled varargs (see dsi_PrintStat).

// 0x100347a4 — the world's sounds are taken down one by one, and anything
// still holding a file at debug level 3 or above is complained about first.
// Unlike sm_FlushList44 (0x1003481c) this one unlinks each instance as it
// goes and leaves no list re-init behind, because the list is empty by
// construction once the walk ends.
void sm_EndWorldD(CServerMgr* pMgr)
{
	LTLink* pCur;
	LTLink* pHead;

	pCur = pMgr->m_List54.m_Head.m_pNext;
	pHead = &pMgr->m_List54.m_Head;

	while (pCur != pHead)
	{
		SoundInstance* pSound = (SoundInstance*)pCur->m_pData;
		pCur = pCur->m_pNext;

		if ((pSound->m_PlaySoundFlags & 0x10) && g_DebugLevel >= 3 &&
			pSound->m_hFile)
		{
			dsi_ServerError("Unfreed sound file (server) %s",
				fs_GetFilename(&pMgr->m_FileMgr,
					pSound->m_hFile));
		}

		dl_RemoveAt(&pMgr->m_List54, &pSound->m_Link);

		pSound->Term();

		pMgr->m_SoundInstances.Free(pSound);
	}
}


LTRESULT sm_DetachClient(CServerMgr* manager, Client* client) {
    if (!manager || !client) return LT_INVALIDPARAMS;
    if (!client->Detach()) return LT_OK;
    if (client->m_pUnknown3AC) {
        ServerMessage message;
        message.WriteWord(client->m_pUnknown3AC->m_ObjectID);
        if (message.m_Flags & PACKETFLAG_WRITEOVERFLOW) return LT_OUTOFMEMORY;
        sm_SendToClient(manager, client, 7, &message, 1, 0x80);
    }
    return LT_OK;
}

void sub_100272d6(CServerMgr* manager, Client* client) {
    if (!client) return;
    while (client->m_AttachHead.m_pNext != &client->m_AttachHead)
        sm_DetachClient(manager, static_cast<Client*>(client->m_AttachHead.m_pNext->m_pData));
}

// 0x10033126 — make one object of a class: allocate the game-side instance,
// let it pre-create itself, then put it in the world. A refused world entry
// frees the instance again and answers null.
//
// The out-parameter for the LTObject lands in the dead `hClass` argument
// slot, which is why the function carries an ebp frame with no locals of its
// own; the class record at +0x24 is re-read at each of its three uses.
//
// Both helpers are the file's inline copies: lithtech.exe (0x004745a0, 173
// bytes, not the 185 its old enumerated extent said) expands the allocation and
// the free; server.dll calls 0x10001c54 and 0x1002abf8 (see the notes at
// sm_FreeObjectInline and sm_AllocateObjectInline).
LPBASECLASS CServerMgr::CreateObject(HCLASS hClass, ObjectCreateStruct* pStruct) {
    return CreateObjectWithProperties(hClass, pStruct, {}, PRECREATE_NORMAL);
}
LPBASECLASS CServerMgr::CreateObjectWithProperties(HCLASS hClass, ObjectCreateStruct* pStruct,
        std::shared_ptr<const ObjectProperties> properties, float reason)
{
    ServerActivity activity(m_Lifetime);
    if (!activity.available()) return nullptr;
    if (!pStruct || !m_Initialized || !m_bWorldLoaded || m_Terminating || m_EndingWorld || !m_Lifetime->alive) return nullptr;
    const auto* record = m_ClassMgr.record(hClass);
    if (!record || (record->m_pClassDef->m_ClassFlags & CF_NORUNTIME)) return nullptr;
    auto* definition = record->m_pClassDef;
    ObjectCreateStruct create = *pStruct;
    std::snprintf(create.m_ClassName,sizeof(create.m_ClassName),"%s",definition->m_ClassName);
    ServerCallbackGuard server(m_Lifetime);
    struct PropertyScope {
        CServerMgr* manager; ServerCallbackGuard guard;
        std::shared_ptr<const ObjectProperties> previous;
        ~PropertyScope() { if (guard.unchanged()) manager->m_Properties = std::move(previous); }
    } scope{this, server, std::exchange(m_Properties, std::move(properties))};
    auto* game = m_ClassMgr.allocate(definition);
    if (!server.unchanged() || !game) return nullptr;
    try {
        const auto accepted = m_ClassMgr.message(game,MID_PRECREATE,&create,reason);
        if (!server.unchanged()) return nullptr;
        if (accepted) {
            LTObject* object = nullptr;
            if (sm_CreateObjectInWorld(this,game,definition,&create,0xffff,reason,&object) == LT_OK && server.unchanged()) return game;
        }
    } catch (const std::exception& error) { nolf::platform::log("object creation",error.what()); }
    if (server.unchanged()) m_ClassMgr.destroy(game);
    return nullptr;
}


// 0x10034973 — what the ping walk does to each object it can see (name
// ours; it is only ever installed here).


// 0x1003491b — wake everything within ten thousand units of the object, so
// deactivated objects it might touch come back. Nothing happens while
// auto-deactivation is off or before a world is up, but the query is
// constructed either way: its declaration comes before both guards, so its
// constructor runs first.
//
// The gather takes no context, which is why m_pUser is left null here where
// sm_WriteClientUpdate fills it in — and it is the same zero VC6 already had
// in a register for the two guards.
void sm_PingObjects(CServerMgr* pMgr, LTObject* pObj)
{
	CVisQuery query;

	if (g_bAutoDeactivate == 0)
		return;

	if (pMgr->m_pWorldFile == 0)
		return;

	query.m_Position = pObj->m_Pos;
    query.m_pUser = pMgr;
    query.m_Radius = 10000.0f;
    query.m_Fn1 = [](LTObject* object,void* context) {
        if (!object->m_Unknown54 && object->m_pSData) sub_100348cc(static_cast<CServerMgr*>(context),object);
    };

	((IObjectQuery*)&pMgr->m_Unknown210)->FindObjects(&query);
}


// The class record's flag word, which servermgr.h leaves in CClassInfo's
// padding at +0x08. Bit 2 marks a class the world teardown leaves alone
// unless it is told to remove everything (name ours, from that one use).
void sm_RemoveObjects(CServerMgr* manager, int all) {
    ServerCallbackGuard lifetime(manager->m_Lifetime);
    manager->m_Unknown1a0 |= 1;
    sm_DrainObjectQueue(manager);
    if (!lifetime.unchanged()) return;
    struct Context { CServerMgr* manager; bool all; ServerCallbackGuard guard; } context{manager,all != 0,lifetime};
    object_Visit(manager->m_List8dc,[](void* pointer,LTObject& object) {
        auto& context = *static_cast<Context*>(pointer);
        if (!context.guard.unchanged()) return false;
        if (object.m_pSData && object.m_pSData->m_pBaseClass &&
            (context.all || !object.m_pSData->m_pClass || !(object.m_pSData->m_pClass->m_ClassFlags & CF_STATIC)))
            sm_RemoveObjectFromWorld(context.manager,object.m_pSData->m_pBaseClass);
        return context.guard.unchanged();
    },&context);
    if (lifetime.unchanged()) manager->m_Unknown1a0 &= ~1u;
}

// ltobject_internal.h declares LTObject's vtable slot 0x34 (index 13) as
// void; the write gate below tests its answer, so the object is reached
// through a view that gives the slot a return type. Only that slot is used
// (name ours, from what it decides).
#ifdef LT_SERVER_BUILD
// (lithtech.exe's /O2 copy, 0x00464a00, is further down, after
// sm_WriteObjectUpdate, so that both of its helpers can be expanded.)
// 0x10027918 — write one changed object into the update (two arguments, the
// object and the context; named by address). The object has to be one the
// client can be told about at all, and a world model whose BSP is busy is
// left alone; everything attached to it goes out with it, resolved by child
// id and gated the same way.
void sub_10027918(LTObject* pObject, CUpdateContext* pCtx)
{
	Attachment* pAttach;

	if (pObject->m_Unknown54)
		return;

	if (pObject->_v34())
		return;

	if (pObject->m_ObjectType == OT_WORLDMODEL &&
		pObject->m_pWorldBsp->IsBusy() == 1)
		return;

	if (!sub_10027be0(pCtx->m_pMgr, pObject))
		return;

	sm_WriteObjectUpdate(pCtx, pObject,
		&pCtx->m_pClient->m_ObjInfos[pObject->m_ObjectID]);

	pAttach = pObject->m_pAttachments;

	while (pAttach)
	{
		LTObject* pChild = sm_FindObjectByID(pCtx->m_pMgr, pAttach->m_nChildID);

		if (pChild && sub_10027be0(pCtx->m_pMgr, pChild))
		{
			sm_WriteObjectUpdate(pCtx, pChild,
				&pCtx->m_pClient->m_ObjInfos[pChild->m_ObjectID]);
		}

		pAttach = pAttach->m_pNext;
	}
}
#else
void sub_10027918(LTObject* pObject, CUpdateContext* pCtx);  // defined below
#endif // LT_SERVER_BUILD

// Eleven lists inside the object at 0x390, starting at +0x220. Only their
// heads are reached, and only from here (name ours).
#define NUM_CHANGEDOBJECT_LISTS 11

// 0x10027e26 — write every object whose state changed into this client's
// update, eleven buckets' worth. An object flagged 0x200 is moved between
// the active and inactive halves of the manager's list first, keeping only
// the two state bits it already had plus 0x20.
//
// The bucket counter lives in the dead p390 argument slot, because ebx, esi
// and edi are all spoken for by the two walks.
void sm_WriteChangedObjects(CServerMgr* manager, CUnknown390* objects, CUpdateContext* context) {
    struct Visit { CServerMgr* manager; CUpdateContext* context; } visit{manager,context};
    for (auto& list : objects->m_Lists) {
        if (!context->ClientAlive()) return;
        if (!object_Visit(list,[](void* pointer,LTObject& object) {
            auto& visit = *static_cast<Visit*>(pointer);
            if (!visit.context->ClientAlive()) return false;
            ObjectCallbackGuard guard(object);
            if (object.m_ChangeFlags & 0x200) {
                object.m_ChangeFlags &= ~0x200;
                sm_SetObjectState(visit.manager,&object,(object.m_ChangeFlags & 0x18) | 0x20);
            }
            if (!visit.context->ClientAlive()) return false;
            if (guard.unchanged() && (visit.context->IsVisible(object) || (object.m_Flags & FLAG_FORCECLIENTUPDATE)))
                sub_10027918(&object,visit.context);
            return visit.context->ClientAlive();
        },&visit)) return;
    }
}

// 0x1002759a — a client is let into the world: it is told the world has
// started (packet 5), then every console variable by name and value (packet
// 0x10, one message each), then the server clock and the world file's id
// (packet 6). A local client is taken straight on to the next state; a
// remote one waits for its own answer.
//
// LTRESULT, not the void servermgr.h declares: the single exit ends
// `xor %eax,%eax`. Renamed with a 2 suffix for that reason, the way
// sm_ObjectRemoved and sm_UpdateClientState2 are -- the header's
// declaration cannot be defined with this body.
//
// The console-variable pointer lives in the dead pMgr argument slot -- ebx,
// esi and edi are already holding the client, the manager and the send
// flags -- and the send flag itself is one materialised 0x80 shared by all
// three sends.
// 0x1002642c, defined further down; declared here because this is the
// first use.
LTRESULT sm_SendCacheEntry(CServerMgr* pMgr, Client* pClient, uint32 iEntry);

// The model reference the manager's hash at 0x918 holds: the cache pass
// clears bit 0 of its flags at +0x20, and the object-info send writes its
// file id out of +0x1c as a word (names by offset).


// 0x10026b4d — tell an entering client what it has to have loaded: the
// cache list first (section 0 and then whatever sm_SendCacheEntry adds),
// then every model reference by file id (section 2), then every wanted
// cache record's file id (section 5), then an empty section 1 to close.
// Each section is flushed whenever the message has less than 0x32 bytes of
// room left, and the section tag is written again after the flush.
//
// LTRESULT, not the void servermgr.h declares: the single exit ends
// `xor %eax,%eax`. Renamed with a 2 suffix for that reason.
//
// The cache-list cursor lives in the dead pMgr parameter slot -- the manager
// itself is in edi -- and the list head is re-`lea`d at both ends of the
// walk rather than named, which is the opposite of every other walk in this
// file and is what the shipped bytes have.
LTRESULT sm_SendObjectInfo2(CServerMgr* pMgr, Client* pClient)
{
	CMessageRef msg;
	void*       hIter;
	LTLink*     pCur;

	{
		CAbstractIO* pIO;
		msg.Init(msg_AllocPacket(&pIO, 0x1000, 0x1000));
		if (pIO)
			pIO->Release();
	}

	msg.m_pMsg->WriteByte(0);

	sm_SendToClient(pMgr, pClient, 0x15, msg.m_pMsg, 0, 0x80);

	{
		ServerMessage* pMsg = msg.m_pMsg;

		pMsg->m_Unknown38 = 1;
		pMsg->m_Unknown34 = 1;
	}

	sm_SendCacheEntry(pMgr, pClient, 0);

	msg.m_pMsg->WriteByte(2);

	hIter = dhash_GetFirst(pMgr->m_hModelHash);
	while (hIter)
	{
		auto* model = static_cast<Model*>(dhash_GetData(dhash_GetNext(&hIter)));

		msg.m_pMsg->WriteWord(static_cast<uint16>(model->m_FileFlags));

		if ((static_cast<ServerMessage*>(msg.m_pMsg))->GetSpaceLeft() < 0x32)
		{
			sm_SendToClient(pMgr, pClient, 0x15, msg.m_pMsg, 0, 0x80);

			{
				ServerMessage* pMsg = msg.m_pMsg;

				pMsg->m_Unknown38 = 1;
				pMsg->m_Unknown34 = 1;
			}

			msg.m_pMsg->WriteByte(2);
		}
	}

	sm_SendToClient(pMgr, pClient, 0x15, msg.m_pMsg, 0, 0x80);

	{
		ServerMessage* pMsg = msg.m_pMsg;

		pMsg->m_Unknown38 = 1;
		pMsg->m_Unknown34 = 1;
	}

	msg.m_pMsg->WriteByte(5);

    for (auto fileID : pMgr->m_Sounds.WantedFileIDs()) {
        msg.m_pMsg->WriteWord(fileID);
        if (msg.m_pMsg->GetSpaceLeft() < 0x32) {
            sm_SendToClient(pMgr,pClient,0x15,msg.m_pMsg,0,0x80);
            msg.m_pMsg->m_Unknown38 = 1;
            msg.m_pMsg->m_Unknown34 = 1;
            msg.m_pMsg->WriteByte(5);
        }
    }

	sm_SendToClient(pMgr, pClient, 0x15, msg.m_pMsg, 0, 0x80);

	{
		ServerMessage* pMsg = msg.m_pMsg;

		pMsg->m_Unknown38 = 1;
		pMsg->m_Unknown34 = 1;
	}

	// The last of this function's eight writes and the only one lithtech.exe
	// expands (0x00463680..0x004636fb); the other seven are calls, in the
	// order byte, byte, word, byte, byte, word, byte that our sites already
	// stand in.
	((CPacket*)msg.m_pMsg)->WriteType((uint8)1);

	sm_SendToClient(pMgr, pClient, 0x15, msg.m_pMsg, 0, 0x80);

	return LT_OK;
}


// 0x10026137 — append a length-prefixed string to a message's payload; the
// one writer servermsg.h does not declare (name ours). Declared here rather
// than beside its other user below, because this is the first use.


#ifndef LT_SERVER_BUILD
class CServerWorldQ2B;
extern CServerWorldQ2B* g_pServerWorldQ2B;  // 0x004b17e8 (client only)
#endif

LTRESULT sm_ClientEnterWorld2(CServerMgr* pMgr, Client* pClient)
{
	CMessageRef msg;
	void*       hIter;

	{
		CAbstractIO* pIO;
		msg.Init(msg_AllocPacket(&pIO, 0x1000, 0x1000));
		if (pIO)
			pIO->Release();
	}

	if (pMgr->m_Unknown198 == 0)
	{
		{
			ServerMessage* pMsg = msg.m_pMsg;

			pMsg->m_Unknown38 = 1;
			pMsg->m_Unknown34 = 1;
		}

		sm_SendToClient(pMgr, pClient, 5, msg.m_pMsg, 0, 0x80);

		// lithtech.exe only (0x004643fc..0x0046440f): a local client gets
		// the server's world published for the client loader to inherit
		// (startload.cpp's DoLoadWorld reads it at 0x00413eb8). Same symbol
		// startload.cpp declares; server.dll's copy has no such store.
#ifndef LT_SERVER_BUILD
		if (pClient->m_ClientFlags & CLIENTFLAG_LOCAL)
			g_pServerWorldQ2B = (CServerWorldQ2B*)&pMgr->m_World;
#endif

		hIter = dhash_GetFirst(pMgr->m_ConsoleState.m_hVarHash);
		while (hIter)
		{
			void* hElement = dhash_GetNext(&hIter);

			if (hElement)
			{
				LTCommandVar* pVar = (LTCommandVar*)dhash_GetData(hElement);

				{
					ServerMessage* pMsg = msg.m_pMsg;

					pMsg->m_Unknown38 = 1;
					pMsg->m_Unknown34 = 1;
				}

				(msg.m_pMsg)->WriteString(pVar->m_pName);
				(msg.m_pMsg)->WriteString(pVar->m_pStringVal);

				sm_SendToClient(pMgr, pClient, 0x10, msg.m_pMsg, 0, 0x80);
			}
		}

		{
			ServerMessage* pMsg = msg.m_pMsg;

			pMsg->m_Unknown38 = 1;
			pMsg->m_Unknown34 = 1;
		}

		((CPacket*)msg.m_pMsg)->WriteType(pMgr->m_Time);

		// WriteType<uint16>: lithtech.exe expands both writes here, the
		// float one at 0x004644ed and this one at 0x004645a6.
		((CPacket*)msg.m_pMsg)->WriteType(
			((CFileIdentifier*)pMgr->m_hWorldFile)->m_nFileID);

		sm_SendToClient(pMgr, pClient, 6, msg.m_pMsg, 0, 0x80);

		pClient->m_Unknown3BC = 0;
		pClient->m_State = 2;

		if (pClient->m_ClientFlags & CLIENTFLAG_LOCAL)
			sm_UpdateClientState2(pMgr, pClient);
	}

	return LT_OK;
}


// 0x100276eb — a client leaves the world: the shell is told, the object it
// was driving stops belonging to it, and the client is sent the exit packet.
// The message is allocated before any of that, which is why its reference
// lives in the highest frame slot.
#ifndef LT_SERVER_BUILD
void cs_InvalidateAndTerm();              // 0x00414900 (client only)
#endif

static void sm_ClearClientObject(CServerMgr* manager, Client* client) {
    auto* object = client->m_pUnknown3AC;
    client->m_pUnknown3AC = nullptr;
    if (object && manager->m_Unknown390.m_Allocated.contains(object) && object->m_pSData &&
        object->m_pSData->m_pOwningClient == client) object->m_pSData->m_pOwningClient = nullptr;
}

void sm_ClientExitWorld(CServerMgr* manager, Client* client) {
    if (!manager || !client || client->m_ExitingWorld) return;
    const auto lifetime = client->m_Lifetime;
    client->m_ExitingWorld = true;
    if (manager->m_ClassMgr.m_pServerShell) manager->m_ClassMgr.shell([&](IServerShell& shell) { return shell.OnClientExitWorld(client->Handle()); });
    if (!*lifetime) return;
    client->m_ExitingWorld = false;
    sm_ClearClientObject(manager,client);
    ServerMessage message;
    if (!(message.m_Flags & PACKETFLAG_WRITEOVERFLOW)) sm_SendToClient(manager,client,5,&message,0,0x80);
#ifndef LT_SERVER_BUILD
    if (client->m_ClientFlags & CLIENTFLAG_LOCAL) cs_InvalidateAndTerm();
#endif
}

// 0x100286a9 — hand the client the sky box and its thirty object slots.
// Sky definition and ordered IDs use the shared fixed-width wire codec.

void sm_SendSkyInfo(CServerMgr* pMgr, Client* pClient)
{
	CMessageRef msg;

	{
		CAbstractIO* pIO;
		msg.Init(msg_AllocPacket(&pIO, 0x1000, 0x1000));
		if (pIO)
			pIO->Release();
	}

	nolf::SkyInfo sky; sky.definition=pMgr->m_SkyDef;
	std::copy(std::begin(pMgr->m_SkyObjects),std::end(pMgr->m_SkyObjects),sky.objects.begin());
	const auto body=nolf::encode_sky(sky);
	msg.m_pMsg->WriteData(body.data(),static_cast<uint32>(body.size()));

	sm_SendToClient(pMgr, pClient, 0x11, msg.m_pMsg, 0, 0x80);
}


// The same 0x1002616a servermsg.h declares as WriteData, but with the byte
// count as the uint16 it really is: the callee widens its dword slot with
// `movzwl %di,%edi`, and the update header's call pushes a register whose
// upper half is left as garbage by a 16-bit `dec`. Only that call site can
// see the difference, so it goes through a view rather than being corrected
// in the header.


// 0x10029609 — the same broadcast sm_BroadcastToClients (0x100297b8) is,
// with the send flags handed in and no world-state test on the client: every
// connected client gets it. Name ours.
void sm_BroadcastToClientsEx(CServerMgr* pMgr, int nPacketID,
                             class ServerMessage* pMsg, uint32 flags);

// 0x10034521 — format a console line and send it to every client.
//
// servermgr.h declares this `void sm_BroadcastPrint(char* pMsg)`, one fixed
// argument; the shipped body takes the address of its *second* argument slot
// and hands it to _vsnprintf, so it is varargs. Renamed for that reason;
// si_BPrint's call site is unaffected either way, because cl
// 8447 merges a pending fixed-prototype cleanup into a varargs call's
// `add esp` and both spellings therefore give one `add $0x14,%esp`.
//
// The va_list never reaches a frame slot: it is not address-taken, so
// va_start folds into the `lea` of the argument and va_end's store dies.
void sm_BroadcastPrintf(char* pMsg, ...)
{
	CMessageRef msg;
	char        sBuffer[500];
	va_list     marker;

	if (!g_pServerMgr)
		return;

	va_start(marker, pMsg);
	vsnprintf(sBuffer, sizeof(sBuffer) - 1, pMsg, marker);

	{
		CAbstractIO* pIO;
		msg.Init(msg_AllocPacket(&pIO, 0x1000, 0x1000));
		if (pIO)
			pIO->Release();
	}

	// WriteType<uint8> -- 0x1001393e is that instantiation, the same body as
	// the word one a byte wide; lithtech.exe expands it at 0x00476074.
	((CPacket*)msg.m_pMsg)->WriteType((uint8)0);
	(msg.m_pMsg)->WriteString(sBuffer);

	sm_BroadcastToClientsEx(g_pServerMgr, 0xd, msg.m_pMsg, 0x80);
}


// Retain the effect payload without its packet ID for late-joining clients.
void sm_SetObjectSFXMessage(CServerMgr* server, LTObject* object, ServerMessage* source)
{
    if (!object || !object->m_pSData || !source) return;
    if (object->m_pSData->m_SFXMessage.CopyFrom(*source, 1) == LT_OK)
        sm_UpdateObjectBBox(server, object);
}


// 0x10027c97 — open this client's update with whatever the two guaranteed
// packet sets still owe. Each of the two cursors gets both of its client
// packet sets: a set small enough to fit is appended into the cursor's
// message, one too big goes out on its own, and either way the set is reset
// to empty afterwards. Both loop counters live in frame slots because ebx,
// esi and edi are all spoken for by the two walks and the set's message.
//
// Written as two indexed loops, `m_Cursors[i]` and `m_PacketSets[i][j]`. VC6
// strength-reduces both subscripts into its own pointers and turns the unread
// counters into the two frame down-counters (0x10027d1d, 0x10027d2f); the
// compiler's pointers are what put the cursor walk in edi and the set's
// message in ebx (0x10027cb2, 0x10027cc1). Hand-written `pSet++`/`pCursor++`
// pointers gave the same bytes with those two registers swapped.
void sm_WriteUpdateHeader(CServerMgr* pMgr, CUpdateContext* pCtx, Client* pClient)
{
	int i;
	int j;

	for (i = 0; i < 2; i++)
	{
		ServerMessage* pMsg = pCtx->m_Cursors[i].m_pMsg;

		for (j = 0; j < 2; j++)
		{
			ClientPacketSet* pSet = &pClient->m_PacketSets[i][j];
			ServerMessage* pSetMsg = (ServerMessage*)pSet->Packet();

			if (pSetMsg->m_Unknown34 - 1 > 0)
			{
				if (pSetMsg->m_Unknown34 - 1 <
					(static_cast<ServerMessage*>(pMsg))->GetSpaceLeft())
				{
					ServerMessage* pOut = (ServerMessage*)pSet->Packet();

					(pMsg)->WriteData(pOut->m_pData + 1,
						pOut->m_Unknown34 - 1);
				}
				else
				{
					sm_SendToClient(pMgr, pClient, 0xe,
						(ServerMessage*)pSet->Packet(), 1, pSet->m_SendFlags);
				}

				{
					ServerMessage* pReset = (ServerMessage*)pSet->Packet();

					pReset->m_Unknown38 = 1;
					pReset->m_Unknown34 = 1;
				}
			}
		}

		// lithtech.exe expands this, Insert2 and all (0x00465175..
		// 0x004652cf); server.dll calls 0x1001393e.
		((CPacket*)pMsg)->WriteType((uint8)0);
	}
}


// 0x10027155 — put one client's view on another's eyes. A client cannot
// watch itself; anything else detaches whatever it was watching first, files
// the child on the parent's attachment list, and tells the child which
// object it is now looking through.
//
// The message is allocated up front and released on both exits. The error
// path's release is NOT folded away even though the reference is provably
// null there, because the reference's address is taken later in the function
// and the two printer calls in between could alias it — which is exactly why
// sm_BroadcastPrintf's early return, taken before any call, does fold.
//
// The pIO out-parameter goes into the dead pParent argument slot, the parent
// having been cached in esi.
LTRESULT sm_AttachClient(CServerMgr* manager, Client* parent, Client* child) {
    if (!manager || !parent || !child || !child->AttachTo(*parent)) return LT_INVALIDPARAMS;
    if (parent->m_pUnknown3AC) {
        ServerMessage message;
        message.WriteWord(parent->m_pUnknown3AC->m_ObjectID);
        if (message.m_Flags & PACKETFLAG_WRITEOVERFLOW) return LT_OUTOFMEMORY;
        sm_SendToClient(manager,child,7,&message,1,0x80);
    }
    return LT_OK;
}


// 0x1000e3f9 again, through the three-argument fixed prototype the removal
// notice was compiled against (its cleanup batches with the two calls that
// follow it). Name ours.

// Three more of the client teardown, named by address: 0x100272d6 undoes
// whatever 0x10027155 did to the clients attached to this one, 0x100273bc
// takes one object out of a client's table, and 0x100273e9 releases the
// client record itself.
void sub_100272d6(CServerMgr* pMgr, Client* pClient);
void sub_100273e9(CServerMgr* pMgr, Client* pClient);

// 0x100273bc — one client stops hearing a sound: the client's own flag byte
// is marked, the instance's listener count comes down, and the timer is
// zeroed when the last listener goes. A thiscall on the SoundInstance with
// the flag byte as its one stack argument (`ret $4`), which is a member
// servermgr.h does not declare (name ours).
// The count at +0x28 is a count, not the `void*` servermgr.h declares: this
// function decrements it and compares it against zero unsigned (`jbe`), and
// zeroes m_fTimer when it reaches nothing.
//
// THE BODY IS IN THE CLASS, because lithtech.exe's /O2 build EXPANDS it into
// sm_RemoveClient. 0x004640fc..0x0046412c carries the whole thing with no
// call in it — `mov 0x34(%eax),%edx ; test $0x428,%edx ; je` (the three
// PLAYSOUND bits), `test $0x4,%dl ; jne` (the LOOP bit), `or %bl,0x2(%ecx)`
// with the 1 hoisted into %bl for the loop, `dec 0x28(%eax)` under a `jbe`,
// and `movl $0x0,0x20(%eax)`. A non-template member DEFINED OUT OF CLASS is
// the one construct /Ob1 never expands, which is why we were calling it.
//
// server.dll cannot move. The shipped /O1 build compiled this SAME source —
// the one that /O2 expands, so an inline candidate there too — and CALLS it
// at every one of its three sites (0x10027363, 0x10027dff, 0x10029f9f). /Os
// therefore declines it at this size with this source, and our body is
// byte-identical at /O1 (0x100273bc, 45 of 45), so neither this function nor
// its callers can change.
// 0x10027308 — remove one client. Its view attachment goes first, then the
// sounds it still owns are taken out of its object table, then the state is
// reset, the shell told, and the record unlinked and freed.
//
// The leaving count in the log line is the list count minus one — the client
// is still on the list when the message is printed.
//
// 180 of 180 at /O1. The flag-byte block that had been written up as a SIB
// base/index near was an ARTEFACT OF THE CALL: because we passed
// &pInfo->m_Unknown02 to an out-of-line DropListener we had to materialise
// the address (`lea (%edx,%eax,4),%eax ; mov 0x2(%eax),%dl ; add $0x2,%eax`),
// where lithtech.exe with DropListener EXPANDED folds it into one addressing
// mode (`lea (%edx,%ecx,4),%ecx ; test %bl,0x2(%ecx)`, 0x004640f4). The body
// move above is what closes it; the source below is unchanged.
void sm_RemoveClient(CServerMgr* pMgr, Client* pClient)
{
    if (!pMgr || !pClient || pClient->m_Removing) return;
    pClient->m_Removing = true;
	LTLink* pCur;
	LTLink* pHead;

	dsi_ServerError("Removing client, id %d, leaving %d.",
		pClient->m_ClientID, pMgr->m_ClientList.m_nElements - 1);

	sm_DetachClient(pMgr, pClient);
	sub_100272d6(pMgr, pClient);

	pCur = pMgr->m_List54.m_Head.m_pNext;
	pHead = &pMgr->m_List54.m_Head;

	while (pCur != pHead)
	{
		SoundInstance* pSound = (SoundInstance*)pCur->m_pData;
		CObjInfo* pInfo =
			&pClient->m_ObjInfos[pSound->m_pUnknown0c->m_nIndex];

		if (!(pInfo->m_Unknown02 & 1))
			pSound
				->DropListener(&pInfo->m_Unknown02);

		pCur = pCur->m_pNext;
	}

	if (pClient->m_ClientFlags & CLIENTFLAG_LOCAL)
		pMgr->m_Unknown1a0 &= ~2;

	sm_SetClientState(pMgr, pClient, 0);
    sm_ClearClientObject(pMgr,pClient);

	if (pMgr->m_ClassMgr.m_pServerShell) pMgr->m_ClassMgr.shell([&](IServerShell& shell) { return shell.OnRemoveClient(pClient->Handle()); });

	pClient->Unregister();

	sub_100273e9(pMgr, pClient);
}


// 0x100296d3 — queue one message on a client, or send it straight out.
//
// LTRESULT, not the void servermgr.h declares: the single exit ends
// `xor %eax,%eax`. And the manager argument is never read — every send here
// goes through the g_pServerMgr global instead, which is presumably why the
// parameter survived at all.
//
// The packet set is picked out of two bits of the caller's flags: bit 7
// chooses the row and bit 1 the column of Client::m_PacketSets. A local
// client, a message with neither queue bit set, or one longer than 255 bytes
// skips the queue entirely; so does one that will not fit in the set even
// after the set has been flushed, and that arm cross-jumps into the direct
// send's argument pushes (only the packet id differs).
//
// GetSpaceLeft's answer is compared against the message length plus one, and
// the flush happens when the message is the larger, so what 0x1000e845
// really reports is the room left rather than the bytes used; the view's
// name is kept as it was first written.
// STAND-IN: the full set's send and reset, as a helper. lithtech.exe spends
// /O2 inline budget on it, which keeps the Append loop's Insert2 a call
// (0x004680fa -> 0x00412f50) inside the expanded WriteType<uint8> below;
// written straight, Insert2 expands too (662 bytes against 459). server.dll's
// /Os declines a plain inline here and calls it (the queue function then
// loses its match, 196 against 229), so there it is forced: its code is the
// same as when the body is written out at the call site.
#ifdef LT_SERVER_BUILD
NOLF_FORCE_INLINE
#else
inline
#endif
void sm_FlushPacketSet(CServerMgr* manager, Client* pClient, ClientPacketSet* pSet)
{
	sm_SendToClient(manager, pClient, 0xe,
		(ServerMessage*)pSet->Packet(), 1, pSet->m_SendFlags);

	{
		ServerMessage* pReset = (ServerMessage*)pSet->Packet();

		pReset->m_Unknown38 = 1;
		pReset->m_Unknown34 = 1;
	}
}

LTRESULT sm_QueueMessageToClient(CServerMgr* pMgr, Client* pClient, uint8 nPacketID,
                                  ServerMessage* pMsg, uint32 flags)
{
    if (!pMgr || !pClient || !pMsg || (pMsg->m_Flags & PACKETFLAG_WRITEOVERFLOW)) return LT_INVALIDPARAMS;
	if (!(pClient->m_ClientFlags & CLIENTFLAG_LOCAL) && (flags & 3) &&
		pMsg->m_Unknown34 <= 0xff)
	{
		ClientPacketSet* pSet =
			&pClient->m_PacketSets[(flags >> 7) & 1][(flags >> 1) & 1];

		if (pMsg->m_Unknown34 + 1 >
			(pSet->Packet())->GetSpaceLeft())
		{
			sm_FlushPacketSet(pMgr, pClient, pSet);
		}

		if (pMsg->m_Unknown34 + 1 >
			(pSet->Packet())->GetSpaceLeft())
		{
			sm_SendToClient(pMgr, pClient, 0xe, pMsg, 1, flags);
		}
		else
		{
			pMsg->m_pData[0] = (uint8)nPacketID;

			// lithtech.exe 0x004680c0..0x00468160 expands this byte as
			// CPacket::WriteType<uint8> (the "CPacket::WriteType" overrun
			// report is inline); server.dll calls ServerMessage::WriteByte.
#ifdef LT_SERVER_BUILD
			((ServerMessage*)pSet->Packet())->WriteByte((uint8)pMsg->m_Unknown34);
#else
			((CPacket*)pSet->Packet())->WriteType((uint8)pMsg->m_Unknown34);
#endif

			(pSet->Packet())->WriteData(pMsg->m_pData,
				pMsg->m_Unknown34);
		}
	}
	else
	{
		sm_SendToClient(pMgr, pClient, nPacketID, pMsg, 1, flags);
	}

	return LT_OK;
}


// 0x1002acfa — the object map entry for an id, or null (name ours; it is
// sm_FindObjectByID's sibling and hands back the entry rather than the
// object behind it).
CObjectMapEntry* sm_FindObjectMapEntry(CServerMgr* pMgr, uint16 nID)
{
	if (nID < pMgr->m_Unknown8fc.m_nElements)
		return &pMgr->m_Unknown8fc.m_pArray[nID];

	return LTNULL;
}

uint32 sm_GetObjectChangeFlags(CServerMgr* pMgr, class LTObject* pObject);  // 0x1002a264

// 0x10027d39 — write the ids this client was sent last frame back to it, so
// it can drop what it will not hear about again. Every id that is still
// flagged 0x1000 is skipped; the rest go out in one run behind a two-byte
// header, and the packet is split whenever it fills up (a split resets the
// run, so the header goes out again).
//
// The run counter doubles as the "header written" flag and as the literal
// zero the first header byte carries. The trip count lives in the dead
// pList argument slot, and the `dec`/`inc` pair around the zero test is the
// post-decrement of `while (i--)`.
//
// The map entry's type decides what the id meant: type 1 is an object, whose
// change flags are recomputed, and anything else is a sound instance, which
// loses this client as a listener and is marked with the fixed flags 3.
void sm_WriteIDList(CUpdateContext* pCtx, ClientIDList* pList)
{
	uint32   i = static_cast<uint32>(pList->m_IDs.size());
	uint16*  pID = pList->m_IDs.data();
	uint32   nWritten = 0;

	while (i--)
	{
		if (!(pCtx->m_pClient->m_ObjInfos[*pID].m_ChangeFlags & 0x1000))
		{
			{
				ServerMessage* pMsg = pCtx->m_Msg1.m_pMsg;

				if (sm_FinishUpdatePacket(pCtx, pMsg, 8, 0x10))
					nWritten = 0;
			}

			if (nWritten == 0)
			{
				// lithtech.exe (0x00465300) expands the second byte write and
				// the word write below but CALLS the first (0x0046537c ->
				// 0x004159f0, CPacket::WriteType<uint8>): the /O2 inline budget
				// runs out at the earliest site. STAND-IN: WriteByte is
				// servermsg.h's name for that same out-of-line writer; three
				// WriteType spellings expand all three (820 bytes vs 670).
				// server.dll calls all three either way.
				pCtx->m_Msg1.m_pMsg->WriteByte((uint8)nWritten);
				((CPacket*)pCtx->m_Msg1.m_pMsg)->WriteType((uint8)3);
			}

			((CPacket*)pCtx->m_Msg1.m_pMsg)->WriteType(*pID);

			nWritten++;

			{
				CObjectMapEntry* pEntry =
					sm_FindObjectMapEntry(pCtx->m_pMgr, *pID);

				if (pEntry && pEntry->m_pRecord)
				{
					if (pEntry->m_nFlags == 1)
					{
						pCtx->m_pClient->m_ObjInfos[*pID].m_ChangeFlags =
							(uint16)sm_GetObjectChangeFlags(pCtx->m_pMgr,
								(LTObject*)pEntry->m_pRecord);
					}
					else
					{
						// The flag byte is spelled out at both uses, not held
						// in a `uint8* pFlags` local: with the local, /O2 keeps
						// the tested byte in %al and ORs the register, where
						// lithtech.exe tests memory and reloads for the OR
						// (0x00465537, 0x0046554c). server.dll is identical
						// either way.
						if (!(pCtx->m_pClient->m_ObjInfos[*pID].m_Unknown02 & 1))
						{
							static_cast<SoundInstance*>(pEntry->m_pRecord)
								->DropListener(&pCtx->m_pClient->m_ObjInfos[*pID].m_Unknown02);
						}

						pCtx->m_pClient->m_ObjInfos[*pID].m_ChangeFlags = 3;
					}
				}
			}
		}

		pID++;
	}
}


// 0x100264a5 — write every entry of one file type out of the cache list into
// the client's message, opening the message lazily through the out-parameter
// it shares with its caller. The fifth argument is the byte that introduces
// the run and the sixth the entry type it matches (name ours).
LTRESULT sub_100264a5(CServerMgr* pMgr, Client* pClient, uint32 iEntry,
                      CAbstractIO** ppIO, int nLeadByte, uint16 nType);

// 0x1002642c — tell one client about one entry of the cache list (name ours;
// the index is the entry just added). Four passes, one per file type, in the
// order 0,1,2,3 with lead bytes 2,4,3,5.
//
// LTRESULT, not the void first declared here: the single exit ends
// `xor %eax,%eax`. The message pointer msg_AllocPacket answers is discarded
// — only the CAbstractIO out-parameter is used, and it is what gets
// released at the end, so there is no CMessageRef in this one.
LTRESULT sm_SendCacheEntry(CServerMgr* pMgr, Client* pClient, uint32 iEntry)
{
    CAbstractIO* io = nullptr;
    msg_AllocPacket(&io, 0x1000, 0x1000);
    if (!io) return LT_ERROR;
    struct Release { CAbstractIO* io; ~Release() { io->Release(); } } release{io};
    for (const auto [lead, type] : {std::pair{2,0},std::pair{4,1},std::pair{3,2},std::pair{5,3}}) {
        const auto result = sub_100264a5(pMgr,pClient,iEntry,&io,lead,static_cast<uint16>(type));
        if (result != LT_OK) return result;
    }
    return LT_OK;
}

// 0x100264a5 / 0x004629d0 — the body of the prototype declared above and
// never defined, the fifth of those this sweep has turned up. Its own caller,
// sm_SendCacheEntry, sits just below it and has been reconstructed for ages;
// only this half was missing, and a bare-address grep could not see it
// because the declaration carries the server address.
//
// Two signature corrections, both read off the bytes rather than guessed:
//
//   * the return is LTRESULT, not the declared void — `xor %eax,%eax` at
//     0x1002657f / 0x00462b6f;
//   * the sixth parameter is uint16, not int — the compare at 0x100264dd /
//     0x00462a0b is sixteen bits wide, and an `int` would have promoted the
//     member and compared thirty-two.
//
// The lead byte goes out on the FIRST match and again after every flush, and
// the closing send happens only if at least one entry matched, which is what
// the flag initialised to one and cleared at the first write is for.
//
// The loop runs on its own counter, i, seeded from iEntry: shipped stores
// the seed straight back into iEntry's slot (`mov %eax,0x10(%ebp)` at
// 0x100264bf), reloads it at every use and bumps it with `incl 0x10(%ebp)`
// (0x10026544) -- a second variable coalesced into the parameter's home.
// Looping on iEntry itself left it enregistered for the first compare and
// the first index (214 bytes, `inc %eax`); with i, byte-identical at /O1.
//
// lithtech.exe 0x004629d0: IDENTICAL (422). Shipped CALLS the first two
// writes (0x00462a24 -> 0x004159f0, 0x00462a3f -> 0x0042fa50), expands
// GetSpaceLeft (the `js` at 0x00462a57), and EXPANDS only the re-opening
// byte after a flush (0x00462a88..0x00462b25), with Insert2 still a call
// (0x00462abd -> 0x00412f50). That last site is CPacket::WriteType; the
// first two keep the ServerMessage stand-ins, which /Ob1 cannot expand.
// With the flush written out at both sites the /O2 budget was left big
// enough to expand Insert2 as well (601 bytes); the flush as an inline
// helper, expanded twice, spends it -- the helper is ours, but it is also
// the only spelling measured that leaves Insert2 a call. server.dll is
// unchanged (223, identical): /O1 calls 0x1001393e for either spelling.
inline void sm_FlushCacheMsg(CServerMgr* pMgr, Client* pClient, CAbstractIO** ppIO)
{
	sm_SendToClient(pMgr, pClient, 0x15, (ServerMessage*)*ppIO, 0, 0x80);

	{
		// 0x10026531 then 0x10026537 — 0x38 BEFORE 0x34, in both
		// images (0x00462a80 / 0x00462a84).
		ServerMessage* pMsg = (ServerMessage*)*ppIO;

		pMsg->m_Unknown38 = 1;
		pMsg->m_Unknown34 = 1;
	}
}

LTRESULT sub_100264a5(CServerMgr* pMgr, Client* pClient, uint32 iEntry,
                      CAbstractIO** ppIO, int nLeadByte, uint16 nType)
{
	// 0x100264b2 / 0x004629ee — a DWORD, set to 1 and cleared with `and $0x0`
	// at /O1. "Nothing written yet", so the closing send is skipped when the
	// run is empty. Server keeps it in its own home slot and never spills a
	// second local; /O2 enregisters it and spills to a fresh slot at
	// 0x00462b36, which is why the lithtech frame carries two dwords.
    ServerCallbackGuard server(pMgr->m_Lifetime);
    const auto client_life = pClient->m_Lifetime;
    const auto entries = pMgr->m_CachedFiles;
	LTBOOL bFirst = LTTRUE;
	uint32 i;

	for (i = iEntry; i < entries.size(); i++)
	{
		if (entries[i].type == nType)
		{
			if (bFirst)
			{
				((ServerMessage*)*ppIO)->WriteByte((uint8)nLeadByte);

				bFirst = LTFALSE;
			}

			((ServerMessage*)*ppIO)->WriteWord(
				entries[i].file_id);

			// 0x1002650f call / 0x00462a44 expanded. 0x32 is the same
			// threshold smgr.cpp already uses for the model hash and the
			// cache-record list.
			if ((static_cast<ServerMessage*>(*ppIO))->GetSpaceLeft() < 0x32)
			{
				sm_FlushCacheMsg(pMgr, pClient, ppIO);
                if (!server.unchanged() || !*client_life) return LT_ERROR;

				// 0x1002653d — the run is re-opened, and bFirst is NOT put
				// back: the closing send still has to happen.
				static_cast<ServerMessage*>(*ppIO)->WriteByte(static_cast<uint8>(nLeadByte));
			}
		}
	}

	// 0x10026556 / 0x00462b40
	if (!bFirst)
	{
		sm_FlushCacheMsg(pMgr, pClient, ppIO);
		if (!server.unchanged() || !*client_life) return LT_ERROR;
	}

	return LT_OK;                                    // 0x1002657f / 0x00462b6f
}


// 0x10034cf9 — remember that a file has to be cached, and tell every client
// about it unless the shell is doing a caching pass of its own (bit 4 of
// 0x1a0). A file already on the list is not added twice.
//
// The list grows twenty entries at a time by hand — allocate, copy, free —
// rather than through any container.
void sm_BroadcastCacheFile(CServerMgr* manager, uint32 file_type, uint16 file_id) {
    for (const auto& entry : manager->m_CachedFiles) if (entry.file_id == file_id) return;
    manager->m_CachedFiles.push_back({static_cast<uint16>(file_type),file_id});
    if (manager->m_Unknown1a0 & 0x10) return;
    struct Context { CServerMgr* manager; ServerCallbackGuard server; uint32 index; } context{
        manager,ServerCallbackGuard(manager->m_Lifetime),static_cast<uint32>(manager->m_CachedFiles.size()-1)};
    client_Visit(manager->m_ClientList,[](void* opaque, Client& client) {
        auto& state = *static_cast<Context*>(opaque);
        if (state.server.unchanged()) sm_SendCacheEntry(state.manager,&client,state.index);
    },&context);
}

LTRESULT sm_CacheObjectFiles(CServerMgr* manager) {
    auto* head = manager->ObjectListHead();
    for (auto* link = head->m_pNext; link != head; link = link->m_pNext) {
        auto* object = static_cast<ModelInstance*>(link->m_pData);
        if (object->m_AnimTracker.GetModel() == manager->m_pPlaceholderModel || !object->m_pSData) continue;
        auto* data = object->m_pSData;
        if (data->m_pModelFile) sm_CacheFile(manager,0,fs_GetFilename(&manager->m_FileMgr,data->m_pModelFile));
        for (auto* skin : data->m_SkinFiles) if (skin) sm_CacheFile(manager,2,fs_GetFilename(&manager->m_FileMgr,skin));
    }
    head = &manager->m_Unknown390.m_Lists[OT_SPRITE].m_Head;
    for (auto* link = head->m_pNext; link != head; link = link->m_pNext) {
        auto* object = static_cast<LTObject*>(link->m_pData);
        if (object->m_pSData && object->m_pSData->m_pModelFile)
            sm_CacheFile(manager,1,fs_GetFilename(&manager->m_FileMgr,object->m_pSData->m_pModelFile));
    }
    return LT_OK;
}

// 0x1002a365 — shut every model down at server teardown. Three passes over
// the model hash: bump every cache count so nothing can go while the sweep
// runs, then bring each one back down and delete the ones that reach zero,
// then complain about whatever is left.
//
// The delete is a real `delete`: the null test and the `push $1` into vtable
// slot 0 are VC6's scalar deleting destructor, not something the source
// wrote.
void sm_TermModels(CServerMgr* manager) {
    void* iterator = dhash_GetFirst(manager->m_hModelHash);
    while (iterator) dhash_Remove(manager->m_hModelHash,dhash_GetNext(&iterator));
    manager->m_Models.Term();
}

// 0x1002ade4 — move an object between the two halves of the live-object list
// and tell the game object which way it went. Nothing happens when the state
// bits it is already carrying are the ones asked for.
//
// The manager argument is never read: the list this walks is the global
// manager's, reached through g_pServerMgr. Active objects go on the tail and
// inactive ones on the head, and the two arms cross-jump into one
// EngineMessageFn call, the message id being all that differs.
void sm_SetObjectState(CServerMgr* pMgr, LTObject* pObj, uint32 nStateBits)
{
	uint32 nFlags = pObj->m_ChangeFlags;

	if (nStateBits != (nFlags & 0x38))
	{
		pObj->m_ChangeFlags = (nFlags & ~0x38) | nStateBits;

		dl_RemoveAt(&pMgr->m_List8dc, &pObj->m_pSData->m_StateLink);

		if (nStateBits != 0)
		{
			dl_AddTail(&pMgr->m_List8dc, &pObj->m_pSData->m_StateLink,
				pObj);

			pMgr->m_ClassMgr.message(pObj->m_pSData->m_pBaseClass,MID_DEACTIVATING,
				LTNULL, 0.0f);
		}
		else
		{
			dl_AddHead(&pMgr->m_List8dc, &pObj->m_pSData->m_StateLink,
				pObj);

			pMgr->m_ClassMgr.message(pObj->m_pSData->m_pBaseClass,MID_ACTIVATING,
				LTNULL, 0.0f);
		}
	}
}


// 0x100334cc and 0x1002af00 — the first half of the removal bookkeeping, and
// the pending-notice record for an object if it has one (names by address).
// 0x100334cc — hand the game-side object back to its class and forget it.
// The engine data pointer is reloaded after the call (the free could have
// moved anything), which is why the store is not through a named local.
//
// ltobject_internal.h has SObjData+0x24 as a `CClassInfo*`; the dword there
// is pushed straight in as sm_FreeObject's `ClassDef*` with no second load,
// so it is a ClassDef* and the cast below carries the correction.
void sub_100334cc(CServerMgr* manager, LTObject* object)
{
    if (!object->m_pSData) return;
    auto* game = object->m_pSData->m_pBaseClass;
    object->m_pSData->m_pBaseClass = nullptr;
    manager->m_ClassMgr.destroy(game);
}

// 0x1002af00 — the late-join notice this object still has outstanding, or
// null. The record's LTLink is at its +0x00 and m_pData points back at the
// record, so the record address is what the caller unlinks and frees.
//
// The object id is loaded into si ahead of the loop, which is the hoist that
// rotates the walk: the head test at the top is the preheader's.
//
// The cursor is initialised THROUGH the head, and that is what decides the
// register roles: `for (pCur = pHead->m_pNext; ...)` loads the cursor first
// and then leas the head into a scratch (edx), which leaves the callee-saved
// esi for the object id the loop hoists into its preheader. Writing it the
// other way — `pCur = pMgr->m_List8ec.m_Head.m_pNext; pHead =
// &pMgr->m_List8ec.m_Head;` with a `while` — spends esi on the head and puts
// the id in dx instead, which is instruction-identical and two registers
// wrong. (Naming the list in an `LTList*` local and taking both off that
// reaches the shipped form too.)
// 0x1002ceac — let go of everything the engine-side object data owns; a
// thiscall on the SObjData with one argument, which ltobject_internal.h does
// not declare (name ours).
//
// Its shape is a scalar deleting destructor's — release what the object
// owns, `operator delete(this)` when bit 0 of the flag is set, return
// `this` — but it cannot be one: SObjData's +0x00 is its LTLink pair, so the
// class has no vptr and VC6 emits no `??_G` for it. So the flag, the
// `operator delete` (0x10028ace, the wrapper one call above dfree) and the
// returned `this` are all in the source, and the one call site pushes zero.
//
// What the object at +0x2c is is not determined here; only that it is
// released through slot 2 of its own vtable with nothing pushed.
// ---- putting a created object into the world ----------------------------- //

// The record sm_ClaimObjectID hands back; only the id at its +0x08 is read
// here, and it is a dword the object's uint16 id is truncated from (name by
// offset).


// 0x1002585f — take an LTObject of the type the create struct asks for out
// of the object manager's per-type bank, and 0x100332e8 — fill it in from
// the create struct and thread it into the world. 0x1001eae7 is what a
// world model or a container needs doing to it once its box is known. Names
// by address.

LTRESULT sub_100332e8(CServerMgr* pMgr, ObjectCreateStruct* pStruct,
                      struct ClassDef* pClassDef, LTObject* pLTObject,
                      LPBASECLASS pObject, class SObjData** ppSData);
void     obj_RetransformWorldModelDims(LTObject* pObject);

// 0x10031dfa — spritecontrol.cpp's name for the id-map claim, and 0x100333fc
// — sm_ObjectRemoved below, both declared here because this is the first
// use of either in the file.
int      sm_ClaimObjectID(CServerMgr* pMgr, void** ppRec, uint16 nID);
LTRESULT sm_ObjectRemoved(CServerMgr* pMgr, LTObject* pObject);

// 0x100334fe — the middle third of creating an object: the engine-side
// LTObject is allocated, filled in, given an id, activated and handed back.
// Anything that fails after the allocation unwinds it again.
//
// The function's own error literal names it "sm_AddObjectToWorld", which is
// the only naming evidence there is; servermgr.h's sm_CreateObjectInWorld is
// ours, so the name is kept and the discrepancy recorded.
//
// The LTObject the allocator answers lives in the dead pMgr parameter slot
// -- the manager itself is in edi from the first instruction -- and the
// manager's object member has its address named in a local, because both
// unwind paths reach it.
//
// The id the claim hands back has to go through a `uint32` of its own: read
// straight into the object's uint16 m_ObjectID, VC6 narrows the load to
// `mov 0x8(%eax),%ax` where the shipped code loads the whole dword and
// stores the low half.
//
// Each unwind path has its OWN `return dResult` -- three failure blocks,
// the filename and id-claim failures written separately. lithtech.exe
// (0x474b58, 0x474b94, 0x474bd6) keeps all three tails apart; server.dll
// (0x100335a3 jumping into 0x100335f8's `mov %ebx,%eax`) merges them
// itself. An earlier `goto` into one shared tail with the claim nested under
// the filename result matched server.dll but left lithtech 33 bytes short.
LTRESULT sm_CreateObjectInWorld(CServerMgr* manager, LPBASECLASS game,
                                ClassDef* definition, ObjectCreateStruct* input,
                                uint32 requested_id, float reason, LTObject** output)
{
    if (!output) return LT_INVALIDPARAMS;
    *output = nullptr;
    if (!manager || !game || !input || requested_id > 0xffff ||
        !manager->m_ClassMgr.owns(definition) || !manager->m_ClassMgr.owns(game)) return LT_INVALIDPARAMS;
    if (!manager->m_bWorldLoaded || manager->m_Terminating || manager->m_EndingWorld || !manager->m_Lifetime->alive) return LT_NOTINWORLD;
    // A callback may pass an SDK structure owned by another object. Keep it
    // independent of callback mutation and validate every bounded name.
    ObjectCreateStruct create = *input;
    if (!std::memchr(create.m_Name,0,sizeof(create.m_Name)) ||
        !std::memchr(create.m_Filename,0,sizeof(create.m_Filename))) return LT_INVALIDPARAMS;
    for (const auto& skin : create.m_SkinNames) if (!std::memchr(skin,0,sizeof(skin))) return LT_INVALIDPARAMS;
    ServerCallbackGuard server(manager->m_Lifetime);
    LTObject* object = nullptr;
    auto result = om_CreateObject(&manager->m_Unknown390,&create,&object);
    if (result != LT_OK) return result;
    game->m_hObject = object;
    result = sub_100332e8(manager,&create,definition,object,game,&object->m_pSData);
    if (result != LT_OK) { game->m_hObject = nullptr; un390_RemoveObject(&manager->m_Unknown390,object); return result; }
    g_Unknown58bec += sizeof(LTObject);
    ObjectCallbackGuard actor(*object);
    auto rollback = [&] {
        if (server.unchanged() && actor.same_data()) sm_RemoveObjectFromWorld(manager,game);
    };
    try {
        // Publish a complete native identity before invoking game code.
        void* record = nullptr;
        result = sm_ClaimObjectID(manager,&record,static_cast<uint16>(requested_id));
        if (result != LT_OK) { rollback(); return result; }
        object->m_pSData->m_Unknown28 = record;
        object->m_ObjectID = static_cast<uint16>(static_cast<CObjectIDRec*>(record)->m_nID);
        auto& entry = manager->m_Unknown8fc[object->m_ObjectID];
        entry.m_nFlags = 1; entry.m_pRecord = object;
        object->m_ChangeFlags |= 0x44;
        manager->m_ClassMgr.message(game,MID_ACTIVATING,nullptr,0);
        if (!server.unchanged()) return LT_ERROR;
        if (!actor.unchanged()) { rollback(); return LT_ERROR; }
        result = w_ApplyObjectFilenames(manager,object,&create);
        if (!server.unchanged()) return LT_ERROR;
        if (!actor.unchanged()) { rollback(); return LT_ERROR; }
        if (result != LT_OK) { rollback(); return result; }
        if (object->m_ObjectType == OT_WORLDMODEL || object->m_ObjectType == OT_CONTAINER) obj_RetransformWorldModelDims(object);
        sm_UpdateObjectBBox(manager,object);
        manager->m_ClassMgr.message(game,MID_INITIALUPDATE,&create,reason);
        if (!server.unchanged()) return LT_ERROR;
        if (!actor.unchanged()) { rollback(); return LT_ERROR; }
        sm_SetObjectChangeFlag(manager,object,sm_GetObjectChangeFlags(manager,object));
        *output = object;
        return LT_OK;
    } catch (...) { rollback(); throw; }
}


// 0x100333fc — the manager's own last rites for an object: drop its name
// from the identifier hash, drop any pending late-join notice, break all
// three kinds of attachment, unthread it from the live-object list, and give
// the engine-side data back.
//
// The data goes back to the *message* bank at 0x9f0 — SObjData and
// ServerMessage are both 0x54 bytes and share it.
//
// LTRESULT, not the void servermgr.h declares: the body ends `xor %eax,%eax`.
// Renamed for that reason.
LTRESULT sm_ObjectRemoved(CServerMgr* pMgr, LTObject* pObject)
{
    if (!pObject || !pObject->m_pSData) return LT_INVALIDPARAMS;
    ServerCallbackGuard server(pMgr->m_Lifetime);
    ObjectCallbackGuard lifetime(*pObject);
    pObject->m_pSData->m_Removing = true;

    auto** changed = &pMgr->m_pChangedObjects;
    while (*changed && *changed != pObject) changed = &(*changed)->m_pSData->m_pNextChanged;
    if (*changed) *changed = pObject->m_pSData->m_pNextChanged;
    pObject->m_pSData->m_pNextChanged = nullptr;

	sub_100334cc(pMgr, pObject);
    if (!server.unchanged() || !lifetime.same_data()) return LT_OK;

	if (pObject->m_pSData->m_hIdentifier)
		dhash_Remove(pMgr->m_hHash8c0, pObject->m_pSData->m_hIdentifier);

    pMgr->m_ClientReferences.RemoveFor(pObject);

	sm_BreakAttachments(pMgr, pObject, 0, 1);
    if (!server.unchanged() || !lifetime.same_data()) return LT_OK;
	sm_BreakAttachments(pMgr, pObject, 1, 0);
    if (!server.unchanged() || !lifetime.same_data()) return LT_OK;
	sm_BreakAttachments(pMgr, pObject, 2, 0);
    if (!server.unchanged() || !lifetime.same_data()) return LT_OK;

	dl_RemoveAt(&pMgr->m_List8dc, &pObject->m_pSData->m_StateLink);

	{
		SObjData* pSData = pObject->m_pSData;

        pObject->m_pSData = nullptr;
        pMgr->m_MessageBank.Free(pSData);
	}

	return LT_OK;
}


// 0x10026584 — write one light animation into a message (five arguments: the
// manager, the animation, its index, the message and a byte cap):
// serverlightanim.cpp's la_WriteChange, under its own signature.
void la_WriteChange(CServerMgr* pMgr, LightAnim* pAnim, uint32 hLightAnim,
                    class ServerMessage* pMsg, uint32 nFlags);

// 0x1002666c — tell an entering client about the world: every light
// animation, packed into as few messages as they fit in. A message that has
// filled up goes out mid-loop and is reset, and whatever is left over goes
// out at the end.
//
// The animation index lives in the dead pMgr argument slot and the byte
// offset VC6 strength-reduces the subscript into lives in pIO's, both
// because edi and esi are already holding the manager and the send flags.
//
// NEAR: 233 of 233 bytes, 82 of 82 instructions, and every differing byte is
// one of those two frame displacements — ours puts the index in pIO's slot
// and the strength-reduction offset in the parameter slot, the shipped build
// the other way round. Writing the byte offset out as a second local of my
// own makes it worse (three slots and a real 0xc frame), and the two stores
// are already in the shipped order relative to the loop guard. Frame-slot
// rotation. Also measured, all unchanged: `i + m_pLightAnims`; a
// `while` loop; `i` initialised at its declaration; `i` in its own block; a
// named element pointer at function scope; a `pServerMgr` copy; an extra
// `if (m_nLightAnims)` wrapper (238 B); an inline element accessor; the
// callee's index as uint16; pIO at function scope in all six declaration
// orders (234 B each); the allocation as an inline helper (not inlined).
//
// lithtech.exe (0x00462ee0, 239 bytes) is IDENTICAL, and the pServerMgr copy
// is what does it: walking the array through a local copy of the manager
// pointer makes /O2 form the element address as `mov 0x1ac(%edi),%eax;
// mov %ebp,%ecx; add %eax,%ecx` (offset + base) as shipped, where the
// parameter itself gives `mov 0x1ac(%edi),%ecx; add %ebp,%ecx` (237 bytes).
// server.dll is the same near either way (which pointer variable
// the uses go through decides the add's operand order).
void sm_SendWorldInfo(CServerMgr* pMgr, Client* pClient)
{
	CMessageRef msg;
	uint32      i;

	{
		CAbstractIO* pIO;
		msg.Init(msg_AllocPacket(&pIO, 0x1000, 0x1000));
		if (pIO)
			pIO->Release();
	}

	CServerMgr* pServerMgr = pMgr;

	for (i = 0; i < pServerMgr->m_nLightAnims; i++)
	{
		la_WriteChange(pMgr, &pServerMgr->m_pLightAnims[i], i, msg.m_pMsg, 0xff);

		if ((msg.m_pMsg)->GetSpaceLeft() <= 0x16)
		{
			sm_SendToClient(pMgr, pClient, 0x19, msg.m_pMsg, 0, 0x80);

			{
				ServerMessage* pMsg = msg.m_pMsg;

				pMsg->m_Unknown38 = 1;
				pMsg->m_Unknown34 = 1;
			}
		}
	}

	{
		ServerMessage* pMsg = msg.m_pMsg;

		if (pMsg->m_Unknown34 - 1 > 0)
		{
			sm_SendToClient(pMgr, pClient, 0x19, pMsg, 0, 0x80);

			{
				ServerMessage* pReset = msg.m_pMsg;

				pReset->m_Unknown38 = 1;
				pReset->m_Unknown34 = 1;
			}
		}
	}
}


// 0x1002a264 — what the client still has to be told about this object, as a
// bitmask. The manager argument is accepted and never read.
//
// The identity rotation and the unit scale are compared against 0.0f and
// 1.0f out of .rdata, and VC6 loads the 1.0f once and leaves it in st(0) for
// all four comparisons that need it — which is why the early-out arm has to
// `fstp %st(0)` to unwind the stack.
//
// The colour test differs by object type: type 3 (a sprite) is default at
// white and everything else at black, and both arms end in the same alpha
// comparison, which VC6 cross-jumps into one copy.
//
// uint32, not the uint16 servermgr.h declares: the answer leaves in eax
// whole (`mov %esi,%eax`), where a uint16 return truncates to `mov %si,%ax`.
// Renamed for that reason; its one caller stores the result into a uint16
// field either way.
uint32 sm_GetObjectChangeFlags(CServerMgr* pMgr, LTObject* pObject)
{
	uint32 nFlags = 0x201;

	if (pObject->m_Rotation.m_Quat[0] != 0.0f ||
		pObject->m_Rotation.m_Quat[1] != 0.0f ||
		pObject->m_Rotation.m_Quat[2] != 0.0f ||
		pObject->m_Rotation.m_Quat[3] != 1.0f)
	{
		nFlags = 0x601;
	}

	if ((pObject->m_Flags & 0xffff) != 0 || pObject->m_UserFlags != 0)
		nFlags |= 8;

	if (pObject->m_Scale.x != 1.0f || pObject->m_Scale.y != 1.0f ||
		pObject->m_Scale.z != 1.0f)
	{
		nFlags |= 0x10;
	}

	if (pObject->m_ObjectType == 3)
	{
		if (pObject->m_ColorR != 0xff || pObject->m_ColorG != 0xff ||
			pObject->m_ColorB != 0xff || pObject->m_ColorA != 0xff)
		{
			nFlags |= 0x40;
		}
	}
	else
	{
		if (pObject->m_ColorR != 0 || pObject->m_ColorG != 0 ||
			pObject->m_ColorB != 0 || pObject->m_ColorA != 0xff)
		{
			nFlags |= 0x40;
		}
	}

	if (pObject->m_ObjectType == 1)
		nFlags |= 0x20;

	if (pObject->m_pAttachments != 0 ||
		(pObject->m_ObjectType == 1 &&
			static_cast<ModelInstance*>(pObject)->m_HiddenPieces != 0))
	{
		nFlags |= 0x100;
	}

	return nFlags;
}


// Three small inline accessors, STAND-INS spending lithtech.exe's /O2
// inline budget (per function, spent from the end backwards).
// Shipped 0x00475670 expands the constructor but CALLS the Clear() inside it
// (0x00475680 -> 0x004147f0), then expands both explicit oc.Clear() sites in
// full, m_Rotation.Init() included (0x00475729..0x00475741). Written without
// them the constructor's Clear was expanded too and the rotation Init at
// both it and the else-arm Clear stayed calls (794 bytes against 664); each
// helper added walked the constructor one step further down (Rotation, then
// Scale, then Clear itself), and with all three it is identical. Names ours;
// the original presumably reached the trees and the class manager through
// accessors of its own. server.dll expands all three and is unchanged.
inline uint32 sm_PrecacheNumTrees(CServerMgr* pMgr)
{
	return pMgr->m_nFileTrees;
}

inline CPrecacheTreeView* sm_PrecacheTree(CServerMgr* pMgr, uint32 i)
{
	CPrecacheTreeView* pTree = (CPrecacheTreeView*)pMgr->m_pFileTrees[i];

	return pTree;
}

inline HCLASS sm_PrecacheFindClass(CServerMgr* pMgr)
{
	HCLASS hClass = reinterpret_cast<HCLASS>(pMgr->m_ClassMgr.find("BaseClass"));

	return hClass;
}

LTBOOL CServerMgr::PrecacheModels()
{
	ObjectCreateStruct oc;
	CPrecacheTreeView* pTree;
	HCLASS             hClass;
	uint32             i;
	uint32             j;

	hClass = sm_PrecacheFindClass((CServerMgr*)this);

	if (!hClass)
		return LTFALSE;

	for (i = 0; i < sm_PrecacheNumTrees((CServerMgr*)this); i++)
	{
		pTree = sm_PrecacheTree((CServerMgr*)this, i);

		if (pTree->m_pWorldBsp->m_nUnknown48 & 8)
		{
			for (j = 0; j < pTree->m_pWorldBsp->m_Array138.GetSize(); j++)
			{
				oc.Clear();

				oc.m_ObjectType = 2;
				oc.m_Flags = 0x3000;

				if (!fs_GetTableName(pTree->m_pWorldBsp->m_Name, j,
				                     oc.m_Filename, sizeof(oc.m_Filename)))
					return LTFALSE;

				if (!((CServerMgr*)this)->CreateObject(hClass, &oc))
					return LTFALSE;
			}
		}
		else if (pTree->m_pWorldBsp->m_nUnknown48 & 0x30)
		{
			oc.Clear();

			oc.m_ObjectType = 2;
			oc.m_Flags = 0x3000;

			strncpy(oc.m_Filename, pTree->m_pCurrent->m_Name,
				sizeof(oc.m_Filename) - 1);
			oc.m_Filename[sizeof(oc.m_Filename) - 1] = 0;

			if (!((CServerMgr*)this)->CreateObject(hClass, &oc))
				return LTFALSE;
		}
	}

	return LTTRUE;
}


std::shared_ptr<const SoundTrack> CServerMgr::AddCachedFile(CFileRef* file) {
    if (!file) return {};
    try { return m_Sounds.Load(*file); }
    catch (const std::exception& error) {
        if (g_DebugLevel >= 2) dsi_ServerError("Invalid sound file %s: %s", file->m_Name.c_str(), error.what());
        return {};
    }
}

LTBOOL CServerThread::IsRequestQueued(void* file) {
    return m_pServerMgr && file && m_pServerMgr->m_Models.HasPending(static_cast<CFileRef*>(file)->m_Name);
}

void CServerThread::ProcessMessage(LThreadMessage* message) {
    if (!m_pServerMgr || !message || message->m_Data[0] || message->m_Data[2]) return;
    auto* file = reinterpret_cast<CFileRef*>(message->m_Data[3]);
    if (!file) return;
    ModelService::Ticket ticket = message->m_Data[5];
    Model* model = nullptr;
    LThreadMessage reply;
    reply.m_Data[0] = se_LoadModelData(m_pServerMgr,file->m_Name.data(),file,&model,&ticket);
    reply.m_Data[2] = 0;
    reply.m_Data[3] = reinterpret_cast<uintptr_t>(file);
    reply.m_Data[5] = ticket;
    // The service retains Ready results even when this queue cannot allocate.
    m_OutgoingQueue.PostMessage(&reply);
}

int sub_100291c7(CServerMgr*, Client*, LTObject*, CObjInfo*, ServerMessage*);

// 0x10013585 — the full rotation, written as whatever its four floats decode
// to (it converts through 0x10015bcd and 0x1001210b first), and 0x100290d7 —
// the object's animation state, which reports by name when an object carries
// more than 0x100 trackers. Both cdecl; names by address.
void sub_10013585(class ServerMessage* pMsg, LTRotation* pRot);
void sub_100290d7(LTObject* pObject, class ServerMessage* pMsg);

// 0x100290d7 / 0x00467250 — the object's animation state into a client
// update: the tracker count, then per tracker the current animation index
// with two AT_ flag bits folded into the top of the word, the elapsed time as
// a 0..255 byte, and (for every tracker but the first) the weight set.
//
// Found by the productive lead rather than by an address scan: a prototype
// declared above and never defined. The 0x00467250 row spent this project's
// whole life on the unexamined list because the /O2 image gives it 740 bytes
// against the /O1 image's 240, and 480 of that 500-byte difference is
// CPacket::WriteType expanded inline at three of its five call sites.
//
// Read off the bytes:
//
//   * the count is SIXTEEN bits (`cmp $0x100,%ax` at 0x00467261) and so is
//     the loop index (`cmpw $0x0,0x10(%esp)` at 0x0046746f); both get dword
//     homes, the narrow read is what names the type.
//   * the >256 arm is a real early return with its own epilogue -- /O2
//     preserves it, and it carries no `pop %ebx` -- not a jump to the
//     common exit.
//   * 0x00467267 tests g_DebugLevel straight with no dsi_OnReturnError above
//     it, so the complaint is a bare guard, not a de_error.h macro.
//   * the two guards at 0x00467300 and 0x0046730b land on ONE continuation,
//     so it is a short-circuit `&&`; GetLength is written out twice
//     (0x00467306, 0x00467311) because VC6 does not CSE across the call.
//
// THE COIN-FLIP, stated plainly: which of the five packet writes is a call to
// ServerMessage::WriteByte and which is CPacket::WriteType expanded. The
// shipped /O2 image calls at two sites and expands at three, and those three
// expansions ARE most of the function, so this one decision dominates its
// byte count. Written the way matched la_WriteChange spells the same mixture,
// with the alternative spelling in a comment at every site. At /O1 the two
// spellings are identical, so server.dll cannot arbitrate; only a lithtech
// build can. Confidence: /O1 high, /O2 low-to-medium.
void sub_100290d7(LTObject* pObject, ServerMessage* pMsg)
{
	LTAnimTracker* pTracker;
	ModelAnim*     pAnim;
	uint16         nCount;
	uint16         nValue;
	uint16         nIndex;
	uint32         nPercent;

	nCount = (uint16)static_cast<ModelInstance*>(pObject)->GetTrackerCount();

	if (nCount > 0x100)
	{
		if (g_DebugLevel >= 1)
		{
			dsi_ServerError(
				"Model (%s) with > 256 AnimTrackers, ignoring animation.",
				static_cast<ModelInstance*>(pObject)->GetFilename());
		}

		// 0x0046728b -- OUT OF LINE in lithtech.exe.
		// expanded alternative: ((CPacket*)pMsg)->WriteType((uint8)0);
		pMsg->WriteByte(0);
		return;
	}

	// 0x0046729e -- OUT OF LINE in lithtech.exe.
	// expanded alternative: ((CPacket*)pMsg)->WriteType((uint8)nCount);
	pMsg->WriteByte((uint8)nCount);

	nIndex = 0;

	pTracker = static_cast<ModelInstance*>(pObject)->m_pTrackerList;

	while (pTracker)
	{
		nValue = pTracker->m_TimeRef.m_Cur.m_iAnim;

		if (pTracker->m_Flags & 2)
			nValue |= 0x8000;               // 0x004672cd `or $0x80,%dh`

		if (pTracker->m_Flags & 1)
			nValue |= 0x4000;               // 0x004672dd `or $0x40,%ch`

		nPercent = 0;

		pAnim = pTracker->m_TimeRef.m_pModel->m_AnimArray.m_pArray
		            [pTracker->m_TimeRef.m_Cur.m_iAnim].m_pAnim;

		if (pAnim && pAnim->GetLength())
		{
			nPercent = pTracker->m_TimeRef.m_Cur.m_Time * 255
			           / pAnim->GetLength();
		}

		// 0x0046732a -- EXPANDED, sizeof(T) == 2 (`mov $0x2,%ebx`).
		// out-of-line alternative: pMsg->WriteWord(nValue);
		((CPacket*)pMsg)->WriteType(nValue);

		// 0x004673d1 -- EXPANDED, sizeof(T) == 1 (`inc`, byte store).
		// out-of-line alternative: pMsg->WriteByte((uint8)nPercent);
		((CPacket*)pMsg)->WriteType((uint8)nPercent);

		if (nIndex)
		{
			// 0x0046747b -- EXPANDED, sizeof(T) == 1. +0x34 is
			// AnimTimeRef::m_iWeightSet, a byte load in both images.
			// out-of-line alternative:
			//   pMsg->WriteByte((uint8)pTracker->m_TimeRef.m_iWeightSet);
			((CPacket*)pMsg)->WriteType(
				(uint8)pTracker->m_TimeRef.m_iWeightSet);
		}

		pTracker = pTracker->GetNext();
		nIndex++;
	}
}


// The unguaranteed half of an object's update: position, rotation and
// animation, each sent only if the object's NETFLAG_*UNGUARANTEED bit asks
// for it, and all three flagged in the high bits of the object-id word that
// introduces them.
//
// The third parameter is accepted and never read.
//
// A shipped oddity worth recording: pObject is dereferenced at 0x10027ad8 to
// reach its SObjData and only null-tested at 0x10027b2a, on the animation
// path. The test is in the source (VC6 cannot invent one) and it is dead.
//
// nAnimFlag is SIXTEEN BITS, which the /O1 image states and the /O2 image
// confirms: server.dll stores it 32 bits wide into the dead pObject slot and
// ANDs the slot 32 bits wide, then reads it back TWICE with `cmpw`
// (0x10027b23), and lithtech.exe keeps it in %ebp with a 32-bit `mov`/`and`
// and reads it twice with `test %bp,%bp` (0x00464df9, 0x00464ead). Both
// images widen the write and narrow the read, which is what VC6 does with a
// uint16 local it has given a dword home. Written as a uint32 with the mask
// as a second statement -- the shape this function carried before -- the
// narrowing disappears and the `and`'s own flags serve the first test, which
// is one instruction and four bytes short of the shipped /O2 function.
//
// The remaining /O1 residual is where the local lives: server.dll leaves it
// in memory and we put it in a register. That is allocation, not shape; the
// /O2 build already agrees with us on the register.
//
// It is a straight two-into-one tie-break, and it is worth all six bytes.
// The function has NO frame (`push ebp ; mov esp,ebp ; push esi ; push edi`
// and no `sub esp`), so the only memory home available is a dead parameter
// slot, and only one variable can have it: whichever of nSendFlags and
// nAnimFlag loses the last callee-saved register. server.dll gives ebx to
// nSendFlags (`xor %ebx,%ebx`, `mov $0x4000,%ebx`, `or $0x80,%bh`,
// `test $0x40,%bh`) and homes nAnimFlag at 0xc(%ebp), computing it in place
// as `mov %ebx,0xc(%ebp)` + `and %eax,0xc(%ebp)` and re-reading it twice
// with `cmpw`. Ours does the exact opposite. Every byte of the difference is
// that swap: +1 on the clear, +2 on the 0x4000 store, +1 on each `orb`,
// +3 at the mask, +1 on the `or` into the id word, +1 on each of the three
// `testb`s, and -2 on each of the two `cmpw`-versus-`test` reads.
//
// Delete nAnimFlag and test `nSendFlags & 0x1000` at both sites and ours
// puts nSendFlags in ebx and the whole prologue through the mask goes
// byte-identical (266 bytes) -- so the ranking is not about nSendFlags at
// all, it is that nAnimFlag, whose live range runs to the very end of the
// function, outranks it in our build. Probed and negative, all 278 with
// identical bytes: declaring nAnimFlag before nSendFlags, nSendFlags as
// uint16, computing nAnimFlag in two statements (`= nSendFlags` then
// `&= 0x1000`, which is exactly the shape of the shipped pair and which VC6
// folds back), and moving `nSize = 14` after the mask.
//
// The tie DOES flip when nSize stops being set right before the test: with
// `uint16 nSize = 14` at the declaration (or `nSize = 14` anywhere up to the
// rotation block), or with the 14 on an `else` arm, ebx goes to nSendFlags
// as shipped. Neither is the shipped shape, though: the early 14 lands in
// edx and nAnimFlag is computed in ecx and stored (271 bytes, 4 blocks); the
// else arm adds a `jmp` (273 bytes, 3 blocks). Shipped sets the 14 into ecx
// between `mov %ebx,0xc(%ebp)` and `and %eax,0xc(%ebp)`, which is the
// original statement order -- and that order gives ebx to nAnimFlag. Left
// at the original, which also keeps the lithtech.exe twin byte-identical.
// Also negative (278, 13-15 blocks): nested `if`s for the pObject test,
// `!= 0` spellings, nSize as uint32, `nSendFlags | m_ObjectID` order.
void sub_10027ad0(CUpdateContext* pCtx, LTObject* pObject, CObjInfo* pInfo)
{
	uint16 nNetFlags = pObject->m_pSData->m_NetFlags;
	uint32 nSendFlags;
	uint16 nAnimFlag;
	uint16 nSize;

	if (!(nNetFlags & (NETFLAG_ANIMUNGUARANTEED | NETFLAG_POSUNGUARANTEED |
	                   NETFLAG_ROTUNGUARANTEED)))
		return;

	nSendFlags = 0;

	if (nNetFlags & NETFLAG_POSUNGUARANTEED)
		nSendFlags = 0x4000;

	if (nNetFlags & NETFLAG_ROTUNGUARANTEED)
	{
		if (pObject->m_Flags & FLAG_YROTATION)
			nSendFlags |= 0x8000;
		else
			nSendFlags |= 0x2000;
	}

	if (nNetFlags & NETFLAG_ANIMUNGUARANTEED)
		nSendFlags |= 0x1000;

	nSize = 14;

	nAnimFlag = (uint16)(nSendFlags & 0x1000);

	if (nAnimFlag && pObject)
	{
		nSize = (uint16)(static_cast<ModelInstance*>(pObject)->GetTrackerCount() * 4
			+ 15);
	}

	sm_FinishUpdatePacket(pCtx, pCtx->m_Msg2.m_pMsg, 10, nSize);

	pCtx->m_Msg2.m_pMsg->WriteWord((uint16)(pObject->m_ObjectID | nSendFlags));

	if (nSendFlags & 0x4000)
	{
		lmsg_WriteCompPos(&pCtx->m_Msg2.m_pMsg->m_Write, &pObject->m_Pos,
			pCtx->m_pMgr->m_World.PositionReference());

		((IMessageWrite*)&pCtx->m_Msg2.m_pMsg->m_Write)->WriteCompVector(
			&pObject->m_Velocity);
	}

	if (nSendFlags & 0x8000)
	{
		sub_10013585(pCtx->m_Msg2.m_pMsg, &pObject->m_Rotation);
	}
	else if (nSendFlags & 0x2000)
	{
		lmsg_WriteCompRotation(&pCtx->m_Msg2.m_pMsg->m_Write,
			&pObject->m_Rotation);
	}

	if (nAnimFlag)
		sub_100290d7(pObject, pCtx->m_Msg2.m_pMsg);
}

// 0x100279d9 — write one object into this client's update, first making sure
// the packet has room for it. The size estimate is a hundred bytes plus the
// object's stowed SFX message if it has one, plus twenty-five per
// attachment; it is a uint16, which is why the SFX case's addition is
// sixteen-bit where the per-attachment one is not.
//
// The object's id also goes on the list of ids the client is being told
// about this frame, which is grown two hundred entries at a time — allocate,
// copy, free, by hand. Every one of the seven uses re-reads the global.
void sm_WriteObjectUpdate(CUpdateContext* pCtx, LTObject* pObject,
                          CObjInfo* pInfo)
{
	uint16 nSize;
	uint16 nFlags;

	if (pInfo->m_ChangeFlags & 0x1000)
		return;

	if (!g_pFillIDList->Append(pObject->m_ObjectID)) return;

	nSize = 100;

	nFlags = pInfo->m_ChangeFlags;

	if ((nFlags & 1) && pObject->m_pSData->m_SFXMessage.m_pMsg)
	{
		nSize = ((ServerMessage*)pObject->m_pSData->m_SFXMessage.m_pMsg)->m_Unknown34
			+ 100;
	}

	if (nFlags & 0x100)
	{
		Attachment* pAttach = pObject->m_pAttachments;

		while (pAttach)
		{
			pAttach = pAttach->m_pNext;

			nSize += 25;
		}
	}

	sm_FinishUpdatePacket(pCtx, pCtx->m_Msg1.m_pMsg, 8, nSize);

	sub_100291c7(pCtx->m_pMgr, pCtx->m_pClient, pObject, pInfo,
		pCtx->m_Msg1.m_pMsg);

	sub_10027ad0(pCtx, pObject, pInfo);

	pInfo->m_ChangeFlags = 0x1000;
}

#ifndef LT_SERVER_BUILD
// lithtech.exe 0x00464a00 (926 bytes) — sub_10027918's /O2 copy. It EXPANDS
// both helpers server.dll calls: sub_10027be0's filter (0x00464a3f..
// 0x00464a9e, again for each child at 0x00464bfe) and the whole of
// sm_WriteObjectUpdate (0x00464aa4..0x00464bc6 and 0x00464c5e..0x00464d84).
// Neither has an out-of-line lithtech.exe copy (0x00464a00 is followed
// directly by sub_10027ad0 at 0x00464da0, and 0x00464ed0 sm_VisCallbackA
// expands the filter too), so both were `inline` in the original and /O1
// declined them. sub_10027be0 is (see above); sm_WriteObjectUpdateInline is
// still a stand-in twin for the other.
void sub_10027918(LTObject* pObject, CUpdateContext* pCtx)
{
	Attachment* pAttach;

	if (pObject->m_Unknown54)
		return;

	if (pObject->_v34())
		return;

	if (pObject->m_ObjectType == OT_WORLDMODEL &&
		pObject->m_pWorldBsp->IsBusy() == 1)
		return;

	if (!sub_10027be0(pCtx->m_pMgr, pObject))
		return;

	sm_WriteObjectUpdateInline(pCtx, pObject,
		&pCtx->m_pClient->m_ObjInfos[pObject->m_ObjectID]);

	pAttach = pObject->m_pAttachments;

	while (pAttach)
	{
		LTObject* pChild = sm_FindObjectByID(pCtx->m_pMgr, pAttach->m_nChildID);

		if (pChild && sub_10027be0(pCtx->m_pMgr, pChild))
		{
			sm_WriteObjectUpdateInline(pCtx, pChild,
				&pCtx->m_pClient->m_ObjInfos[pChild->m_ObjectID]);
		}

		pAttach = pAttach->m_pNext;
	}
}
#endif // !LT_SERVER_BUILD


// 0x100270d4 — a client, freshly built. Nothing here is body code: the four
// stores are all member initialisation and the shipped order proves it, since
// the four-trip packet-slot loop is emitted BETWEEN the rate governor's
// constructor at +0x118 and the message list's tie-off at +0x3cc, which is
// where declaration order puts ClientPacketSet's own constructor and nowhere a
// statement could be scheduled to.
//
// The list is tied off inline rather than called because ltlink.h's LTList
// constructor is an in-class one-liner; the rate governor's is a call because
// its body is out of line.
//
// `inline`, and that is the /O2 image's evidence: lithtech.exe has NO
// standalone Client constructor, it expands this body into its only caller
// (0x004637aa..0x004637d9, sm_AddClientForConnection), and it sits in
// server.dll at 0x100270d4 as a COMDAT emitted right after that caller,
// beside the out-of-line CheapLTLink::AddAfter (0x100270be) -- where /O1
// leaves inline bodies it declined, not where a defined function would go.
// Out of line, our lithtech build called it (1332 bytes against 1386, and a
// stray 72-byte ??0Client row); inline it is expanded (1380), server.dll
// unchanged.
// The sound instance's audible radius and world position, both inside the
// padding servermgr.h leaves after +0x3c (names ours, from the range test
// below).
// 0x10028454 — write the sounds this client still has to hear about.
//
// The declared name is servermgr.h's and it is a misnomer: the list walked
// is m_List54, the sound instances, and what the loop skips is a sound the
// client cannot hear. A killed sound is written only if the client's table
// still has the 0x20 bit for it; a playing one only while its timer has time
// left; and a sound with either of the low two play flags has to be within
// twice its radius (the test is against radius squared times four) of either
// the object the client is looking through or the client's own view
// position.
//
// The one four-byte frame slot serves two unrelated locals — the squared
// range while the tests run, and the reallocated id array afterwards.
//
// The id written onto the fill list goes through a BLOCK-SCOPED uint32 temp,
// which is what defeats VC6's load-narrowing peephole: the shipped build
// loads the full dword (`mov 0x8(%ecx),%ecx`) and stores a word, where the
// field read inline — or through a function-scope temp — narrows the load to
// `mov 0x8(%ecx),%cx`.
void sm_WriteRemovedObjects(CUpdateContext* pCtx, ServerMessage* pMsg)
{
	LTLink* pCur;

	for (pCur = pCtx->m_pMgr->m_List54.m_Head.m_pNext;
		pCur != &pCtx->m_pMgr->m_List54.m_Head;
		pCur = pCur->m_pNext)
	{
		SoundInstance* pSound = (SoundInstance*)pCur->m_pData;

		if (pSound->m_bKill)
		{
			if (!(pCtx->m_pClient->m_ObjInfos[
					pSound->m_pUnknown0c->m_nIndex].m_ChangeFlags & 0x20))
			{
				continue;
			}
		}

		if (pSound->m_Track)
		{
			if (pSound->m_fTimer <= 0.0f)
				continue;
		}

		if (pSound->m_PlaySoundFlags & 3)
		{
			float fRangeSqr = pSound->m_fOuterRadius *
				pSound->m_fOuterRadius * 4.0f;

			if (pSound->m_vPosition.DistSqr(
					(pCtx->m_pClient->m_pUnknown3AC ? pCtx->m_pClient->m_pUnknown3AC->m_Pos : pCtx->m_pClient->m_Position)) >= fRangeSqr &&
				pSound->m_vPosition.DistSqr(
					pCtx->m_pClient->m_Position) >= fRangeSqr)
			{
				continue;
			}
		}

		sm_FinishUpdatePacket(pCtx, pMsg, 8, 0x2d);

		{
			CObjInfo* pInfo = &pCtx->m_pClient->m_ObjInfos[
				pSound->m_pUnknown0c->m_nIndex];

            if (!g_pFillIDList->Append(static_cast<uint16>(pSound->m_pUnknown0c->m_nIndex))) continue;

			if (pInfo->m_ChangeFlags != 0 && !(pInfo->m_Unknown02 & 1))
			{
                if (sound_WriteInstance(*pSound,*pInfo,*pCtx->m_pClient,*pMsg,
                    pCtx->m_pMgr->m_World.PositionReference(),pCtx->m_pMgr->m_Time) != LT_OK) continue;
			}

			pInfo->m_ChangeFlags = 0x1000;
		}
	}
}


// 0x1000f878 — transform a point by a 4x4 (the matrix's translation row is
// at +0x30 and the homogeneous divisor comes back as the return value, which
// this caller discards; hence the bare `fstp %st(0)` before the next call).
// Name ours, from the arithmetic.
float MatVMul_H(LTVector* pOut, class LTMatrix* pMat, LTVector* pV);

// 0x10012c3a — is a point inside this world's geometry? Two flag bits at
// +0x16 of the argument short-circuit it. It is worldsim.cpp's
// bsp_PointInside (worldsim.h), lithtech.exe 0x00435540 (called from
// 0x0046feaf); declared here as that file declares it.

int bsp_PointInside(BSPNode* pRoot, LTVector* pPoint);

// 0x1002f861 — one candidate from the sphere query's box walk. The box test
// the container did is generous, so this does the round one: the object's
// bounding sphere against the query sphere, which expands to
// R*2r + R*R + r*r — exactly (R + r) squared, the query having pre-doubled
// and pre-squared its own radius.
//
// void, not the `int` servermgr.h declares as sm_SphereGatherCB: nothing is
// put in eax on any path. Renamed for that reason (the query slot it is
// installed in is cast at the call site).
//
// The bare `lea 0x1a4(%esi),%eax` whose result is never used is the residue
// of a pointer local for the radius: VC6 emits it for the third read of the
// same field whether the source names it or not (ours does too).
//
// The three terms are NOT factored, which is the whole difficulty: written
// as `R * D + R * R + RSqr` with the radius read straight from the object
// three times, VC6 factors it to `(D + R) * R + RSqr` and the function comes
// out twelve bytes short. Reading it once into a local and twice through the
// pointer blocks that and gives the shipped `fld %st(1)` / `fmul %st(2)`.
// WHICH read goes where is the last three bytes: the local in the product
// and the pointer in the square gives the shipped `flds 0x1a4(%esi); lea;
// fmuls 0x10(%ebx)` (server 0x1002f8bd). The other way round, VC6 loads the
// diameter and folds the radius into the fmul, whichever operand order the
// product is written in (all four orders measured identical).
void sm_SphereGather(LTObject* pObj, void* pUser)
{
	SphereQuery* pQuery = (SphereQuery*)pUser;

	if (pObj->m_Unknown54 != 0)
		return;

	{
		float  fDistSqr = (pObj->m_Pos - *pQuery->m_pPosition).MagSqr();
		float* pRadius = &pObj->m_Radius;
		float  fRadius = *pRadius;

		if (fDistSqr < pQuery->m_Diameter * fRadius +
			*pRadius * *pRadius + pQuery->m_RadiusSqr)
		{
			ObjectLink* pLink =
				(ObjectLink*)sm_sb_Allocate(&g_pServerMgr->m_ObjectLinkBank);

			if (pLink)
			{
				pLink->m_hObject = (HOBJECT)pObj;
				pLink->m_pNext = pQuery->m_pList->m_pFirstLink;
				pQuery->m_pList->m_pFirstLink = pLink;
				pQuery->m_pList->m_nInList++;
			}
		}
	}
}


// 0x10029653 — send one message to a client, and optionally to every client
// attached to it. The packet id is stamped into the payload's first byte
// once, for all the recipients.
//
// The two counters at 0x7fc and 0x800 are only bumped for the client itself:
// 0x7fc for every attempt and 0x800 for the ones the net manager refused.
// The attached clients' sends are not counted and their results not looked
// at.
void sm_SendToClient(CServerMgr* pMgr, Client* pClient, int nPacketID,
                     ServerMessage* pMsg, int nFlag, int nPriority)
{
	pMsg->m_pData[0] = (uint8)nPacketID;

	if (pClient->m_pConnection)
	{
		if ((&pMgr->m_NetMgr)->SendToConnection(pMsg,
				pClient->m_pConnection, nPriority) == 0)
		{
			pMgr->m_Unknown800++;
		}

		pMgr->m_Unknown7fc++;
	}

	if (nFlag)
	{
		LTLink* pCur = pClient->m_AttachHead.m_pNext;
		LTLink* pHead = &pClient->m_AttachHead;

		while (pCur != pHead)
		{
			Client* pChild = (Client*)pCur->m_pData;

			if (pChild->m_pConnection)
			{
				(&pMgr->m_NetMgr)->SendToConnection(pMsg,
					pChild->m_pConnection, nPriority);
			}

			pCur = pCur->m_pNext;
		}
	}
}


// What the global light object keeps that the sky update sends on: a colour
// at +0x10 and one more float at +0x28. It is a game-side class derived from
// BaseClass, so the offsets are all this side knows (names ours).
class CGlobalLightView
{
public:
	uint8    m_pad_00[0x10];
	LTVector m_vColor;                       // +0x10
	uint8    m_pad_1c[0x28 - 0x1c];
	float    m_fUnknown28;                   // +0x28
};

// 0x1002873d — tell one client where the global light is pointing and what
// colour it is. The direction is the forward basis vector of the object's
// rotation, which is why the rotation is turned into a matrix and the matrix
// asked for its three basis vectors even though only the last is sent.
//
// The object at CServerMgr+0x928 that servermgr.h calls CUnknown928Obj is a
// CLTServer: the two calls here go through its function-pointer members at
// +0x128 and +0x19c, which servermsg.h already names HandleToObject and
// GetObjectRotation, and the caller cleans their arguments up (they are
// plain indirect cdecl calls, not virtuals).
//
// The light handle for the FIRST call is read into its own local: shipped
// loads it into eax and pushes the register (0x10028775/0x10028787) while the
// second call pushes the member straight off memory (0x10028796). Without the
// local VC6 CSEs `&m_hGlobalLightObject` into a register, which put the
// manager in ebx, turned `lea 0x928` into an `add` and reloaded pMgr from its
// argument slot for the send (286 bytes).
//
// NEAR: 283 of 283, 101 of 101 instructions. The one residual is the
// epilogue: shipped pops edi/esi/ebx BEFORE the CMessageRef destructor's
// `test %ecx,%ecx` (0x1002884a); ours tests first, then pops. Elsewhere in
// this file shipped puts the pops first only where the destructor block is a
// join target (sm_SendWorldInfo 0x10026748); on a fall-through tail it tests
// first (sm_SendSkyInfo, sm_SendObjectInfo2), as ours does here. Tried
// without effect: the whole body flat at function scope; the send inside the
// inner block; the body in `do { } while (0)`. Also tried without effect: a
// trailing `return;`; `goto done; done: ;`; the body as `for (;;) { ...
// break; }`; pIO at function scope; msg in its own inner scope; a local copy
// of pMgr used for the body and the send; a second local with an empty
// inline destructor declared after msg. Read from the bytes: ours puts the
// pops between `test` and `je`, so the destructor's test shares the send's
// block. Shipped puts them before the test, which means the test starts a
// block of its own, and nothing tried here has produced that.
//
// lithtech.exe 0x00466320 (441 bytes; ours 1040): shipped CALLS the first six
// float writes (0x004663b3..0x004663fb -> WriteType<float> 0x00466700) and
// expands only the seventh; ours expands the first five. Measured and NOT
// kept: the six through ServerMessage::WriteFloat, each argument through
// a value-returning accessor `(&v.x)[i]` (a plain member, a `(float)` cast or
// LTVector::operator[] all push by `mov`, where shipped `flds/fstps`), gives
// 441 of 441 with one register swap left in the expanded write (edx/ecx at
// 0x00466403). It costs the file the WriteType<float> COMDAT (0x00466700,
// no out-of-line caller left), so it is a net loss while it stays a near.
//
// Measured further: the CALLS-FIRST / EXPANDS-LAST order is what VC6 does
// with one chained expression. Written as `p << vForward.x << ... <<
// m_fUnknown28` through an inline `CPacket& operator<<(CPacket&, float)`
// (stand-in, not kept), /O2 expands the outermost (LAST) sites first: the
// first three are calls and the last four expand, and the calls push through
// x87 exactly as shipped (flds 0x4c(%esp) straight off the matrix, y/z via the
// copied vector) with no accessor stand-in, and the WriteType<float> COMDAT
// stays. Statement form spends the budget forward instead (first five
// expand). Shipped has budget for ONE: ~3 more small inline sites before the
// chain reach 441 of 441 with the COMDAT kept, and what is left is the
// edx/ecx swap at 0x00466403 (allocation). Explicit instantiation
// `template void CPacket::WriteType(float);` emits nothing in VC6 (the
// `<float>` spelling is an ICE), and a defined WriteFloat wrapper is 349 bytes
// against 0x00466700's 356, so neither keeps the COMDAT another way.
void sm_Unknown2873d(CServerMgr* pMgr, Client* pClient)
{
	CMessageRef msg;
	LTVector    vForward;
	LTVector    vUp;
	LTVector    vRight;
	LTRotation  rRot;
	LTMatrix    mRot;

	{
		CAbstractIO* pIO;
		msg.Init(msg_AllocPacket(&pIO, 0x1000, 0x1000));
		if (pIO)
			pIO->Release();
	}

	{
		HOBJECT           hLight = pMgr->m_hGlobalLightObject;
		CLTServer**       ppServer = (CLTServer**)&pMgr->m_pUnknown928;
		CGlobalLightView* pLight = (CGlobalLightView*)
			(*ppServer)->HandleToObject(hLight);

		(*ppServer)->GetObjectRotation(pMgr->m_hGlobalLightObject, &rRot);

		quat_ConvertToMatrix(&rRot, &mRot);

		mRot.GetBasisVectors(&vRight, &vUp, &vForward);

		((CPacket*)msg.m_pMsg)->WriteType(vForward.x);
		((CPacket*)msg.m_pMsg)->WriteType(vForward.y);
		((CPacket*)msg.m_pMsg)->WriteType(vForward.z);
		((CPacket*)msg.m_pMsg)->WriteType(pLight->m_vColor.x);
		((CPacket*)msg.m_pMsg)->WriteType(pLight->m_vColor.y);
		((CPacket*)msg.m_pMsg)->WriteType(pLight->m_vColor.z);
		((CPacket*)msg.m_pMsg)->WriteType(pLight->m_fUnknown28);
	}

	sm_SendToClient(pMgr, pClient, 0x1a, msg.m_pMsg, 0, 0x80);
}


// 0x10029f42 — the tail of the client-command read below; three arguments and
// the only thing established about it is that a null third one answers zero
// (name ours).
// *********************************************************************** //
//  MOVED HERE from svlast.cpp: lithtech.exe 0x00468f80 is this body at /O2
//  (ReadType and DropListener expanded), and svlast.cpp is not in
//  lithtech.exe's build. smgr.cpp used to only DECLARE it, at the top of the
//  client-command reader that calls it:
//
//      LTRESULT sm_Unknown29f42(CServerMgr* pMgr, ServerMessage* pMsg,
//                               Client* pClient);
//
//  READ THIS FIRST: the enumerated row for this function is 0x10029f4f / 103
//  bytes, and 0x10029f4f IS NOT A FUNCTION. It is the fall-through of this
//  function's own null guard. The real function starts at 0x10029f42, which
//  the enumeration lists separately as 13 bytes because the guard's early
//  `ret` at 0x10029f4e looked like an end; 0x10029f42 + 13 = 0x10029f4f, so
//  the two rows are one 116-byte function, 0x10029f42..0x10029fb6. The one
//  call site (0x1002a0c1, in the client-command reader at 0x10029fb6)
//  reaches 0x10029f42 and nothing reaches 0x10029f4f. An extent check validates a
//  row's extent and never its start, which is exactly this failure mode.
//
//  What it is: the tail of the client-command packet. Whatever ids are left
//  unread in the message are sound instances this client is no longer to
//  hear, and each one is dropped the same way sm_WriteIDList drops them —
//  which is the model to read this against, because the two share the map
//  lookup, the CObjInfo flag byte and DropListener.
//
//  Read off the bytes:
//
//    * the arguments are (pMgr, pMsg, pClient) and only the THIRD is
//      checked: `mov 0x10(%esp),%ebx ; test ; jne` before anything else, and
//      the failing arm returns zero. That is the one fact smgr.cpp's
//      existing declaration comment already records.
//    * the loop bound is `movzwl 0x34 ; movzwl 0x38 ; sub ; je` — a
//      SUBTRACTION tested against zero, not a compare, and 0x34/0x38 load in
//      that order. Written `!=` it would be a `cmp`. The caller does the
//      same subtraction the other way round and tests it `jle`, which is why
//      the two look alike and are not.
//    * the read is 0x1000ebd0 = CPacket::ReadType<uint16> — `push $0`,
//      `mov %edi,%ecx`, no cleanup. The dummy pointer is the VC6-era way of
//      naming the template argument (packet.h documents it); smgr.cpp
//      already casts ServerMessage* to CPacket* for the WriteType side.
//    * the id is a uint16 LOCAL, not the dword the read left in eax: it is
//      pushed whole to sm_FindObjectMapEntry (whose parameter is uint16
//      already) but re-zero-extended with `movzwl %si,%ecx` for the array
//      index. A uint32 local would not have the movzwl.
//    * the entry test order is `pEntry != 0`, then `m_nFlags == 2`, then the
//      record — and the record is loaded ONCE, into the register that later
//      becomes `this` for DropListener. Hence the explicit local; a third
//      `&&` arm re-reading pEntry->m_pRecord is the same bytes only if VC6
//      chooses to CSE it across the pFlags address computation, and the
//      standing rule on this codebase is that a pointer held across other
//      work is held because the source held it.
//      COIN-FLIP, both spellings exact:
//          pRecord = pEntry->m_pRecord; if (pRecord) { ... }     // used here
//          if (pEntry && pEntry->m_nFlags == 2 && pEntry->m_pRecord) { ... }
//    * `m_nFlags == 2` and not `!= 1`: `cmpb $0x2,(%eax) ; jne`. Note
//      sm_WriteIDList treats type 1 as the object case and *anything else*
//      as the sound case; this one takes only type 2, so the two are not
//      the same predicate written twice.
//    * the flag byte is CObjInfo::m_Unknown02 — `testb $0x1,0x2(%edx,%ecx,4)`
//      against a four-byte record indexed by id off Client+0x190, which is
//      m_ObjInfos, and the address is then handed to DropListener as its
//      one stack argument. NOT through a `uint8* pFlags` local, though:
//      lithtech.exe 0x0046903c tests the byte in memory (`testb $0x1,
//      (%eax)`) and re-reads it for DropListener's `or` (0x00469051), which
//      is the element written out at both uses. Through a pointer local
//      /O2 loads the byte once into %al and keeps the address in %esi.
//      server.dll is the same either way.
//    * single `xor %eax,%eax` at each of the two exits, so LTRESULT/LT_OK.
//
//  CSoundListenerView is smgr.cpp's local view class (0x100273bc). It is
//  re-declared here with only the member this file calls, which is enough
//  for the mangled name and for `this` at offset zero; when this body moves
//  to smgr.cpp the re-declaration goes away and the real one is used.
// *********************************************************************** //

LTRESULT sm_Unknown29f42(CServerMgr* pMgr, ServerMessage* pMsg,
                         Client* pClient)
{
    if (!pMgr || !pMsg || !pClient || !pMsg->CanRead(0) ||
        ((pMsg->m_DataLen-pMsg->m_Pos) & 1)) return LT_OK;
    while (pMsg->CanRead(2)) {
        const auto id = pMsg->ReadWord(nullptr);
        if (id >= pClient->m_ObjInfos.size()) continue;
        const auto* entry = sm_FindObjectMapEntry(pMgr,id);
        if (entry && entry->m_nFlags == 2 && entry->m_pRecord && !(pClient->m_ObjInfos[id].m_Unknown02 & 1))
            static_cast<SoundInstance*>(entry->m_pRecord)->DropListener(&pClient->m_ObjInfos[id].m_Unknown02);
    }
    return LT_OK;
}


// 0x10029fb6 (282 bytes) — a client's command state off the wire, and
// IDENTICAL (name ours).
//
// THE ENUMERATED ROW OF 360 BYTES IS THREE FUNCTIONS, and an earlier
// version of this banner talked itself out of the right answer. The `ret` at
// 0x1002a0cf IS the end: 0x1002a0d0 and 0x1002a0f2 are two more packet
// handlers, both `__cdecl` off %esp with no frame at all, and all four of
// this handler, those two and sm_Unknown29f42 are reached only from the
// ten-byte-stride dispatch table at file offset 0x29f01 (their addresses
// sit at 0x29f01, 0x29f0b, 0x29f15 and 0x29f1f). A table reference is not
// a call, which is why the enumeration calls 0x10029fb6 a `gap` row and
// swallows its two neighbours whole. lithtech.exe says the same thing
// independently: the body there ends `ret` at 0x0046932b and 0x00469330 is
// the first of the pair again, four `nop` later. The score is taken on OUR
// length, not the row's, so 282 exact scores the row — measured, it does.
//
// The four things that were wrong, each fixed against a named address:
//
//   * THE RATE IS AN int, NOT A uint32. 0x1002a02d is `fildl` on the dead
//     pClient slot; a uint32 gives `fildll` with a zero high dword written
//     first, which is what we emitted. Reading it as `(float)ReadByte(0)`
//     — uint8 promoted to int — is the shape: the value never needs a
//     named local at all, because the converted float stays on the FPU
//     stack across both the compare and SetRate (0x1002a030 `fcoms`,
//     0x1002a037 `fsts`, 0x1002a040 `fstps`).
//   * THE COMMAND COUNT IS A uint16 TESTED `> 0`. 0x1002a056 is
//     `movzbw %al,%ax` and 0x1002a05a `test %ax,%ax / jbe` — a sixteen-bit
//     destination and an unsigned-greater test, where a uint32 and `!= 0`
//     give `movzbl` and `je`.
//   * THE LOOP COUNTER IS A SECOND, 32-BIT VARIABLE. The widening store
//     0x1002a05f..0x1002a062 (`movzwl %ax,%eax ; mov %eax,0x10(%ebp)`)
//     happens INSIDE the taken arm, after the `jbe`, and the loop then
//     decrements a dword (`decl`, 0x1002a06f). So the uint16 is copied to
//     a uint32 once the guard has passed; one variable of either width
//     gives a 16-bit `add $0xffff` / `cmpw` pair instead.
//   * THE DIFF INDEX IS A uint16 TOO: 0x1002a092 is `cmp $0xff,%ax`, a
//     sixteen-bit bound, and the change list is filled with `%al`.
//
// With those four the frame falls to 0x100 — exactly the 256-byte change
// list, no spill — the constant zero stops claiming %ebx, pLive lives in
// %ebx as it does in the shipped build, and the body is byte-identical.
// The old banner's "%ebx lottery" was a symptom of the uint32 rate: the
// 64-bit `fild` temp is what pushed the frame out and cost the register.
//
// The tail was a semantic fault as well and is fixed: the shipped `jle`
// sends the false arm to `xor %eax,%eax` while the true arm jumps PAST it
// (0x1002a0ba / 0x1002a0c9), so the call's value is returned, not LT_OK;
// and the subtraction is m_Unknown34 MINUS m_Unknown38 (0x1002a0b6
// `sub %ecx,%edx` with %edx holding +0x34), i.e. bytes REMAINING, not the
// other way round. lithtech.exe agrees at 0x00469304/0x00469309.
LTRESULT sm_ReadClientCommands(CServerMgr* pMgr, ServerMessage* pMsg,
                               Client* pClient)
{
    if (!pMgr || !pMsg || !pClient || pClient->m_Removing || pClient->m_nCurCommands > 1 || !pMsg->CanRead(2)) return LT_OK;
    const auto rate = pMsg->m_pData[pMsg->m_Pos];
    const auto count = pMsg->m_pData[pMsg->m_Pos+1];
    const auto available = pMsg->m_DataLen-pMsg->m_Pos-2;
    // Validate the complete message before publishing any command changes.
    // The optional sound-completion tail consists of whole object IDs.
    if (count > available || ((available-count) & 1)) return LT_OK;
    std::array<uint8,MAX_CLIENTCOMMANDS> next{}, changed{};
    for (unsigned i = 0; i < count; ++i) {
        const auto command = pMsg->m_pData[pMsg->m_Pos+2+i];
        if (command >= next.size()) return LT_OK;
        next[command] = 1;
    }
    const auto page = pClient->m_nCurCommands;
    unsigned changed_count = 0;
    for (unsigned i = 0; i < next.size(); ++i)
        if (next[i] != pClient->m_CommandStates[page][i]) changed[changed_count++] = static_cast<uint8>(i);
    const auto client_lifetime = pClient->m_Lifetime;
    ServerCallbackGuard operation(pMgr->m_Lifetime);
    pClient->m_nCurCommands = !page;
    std::copy(next.begin(),next.end(),pClient->m_CommandStates[!page]);
    if (rate != pClient->m_Unknown128) {
        pClient->m_Unknown128 = rate;
        pClient->m_RateControl.SetRate(rate);
    }
    pMsg->m_Pos += 2+count;
    pMgr->NotifyCommandChanges(pClient,changed.data(),changed_count);
    if (!operation.unchanged() || !*client_lifetime || pClient->m_Removing) return LT_OK;
    return sm_Unknown29f42(pMgr,pMsg,pClient);
}


// *********************************************************************** //
//  THREE SMALL ROWS FROM THE lithtech.exe UNEXAMINED SWEEP
//
//  All three are lithtech.exe addresses that sit inside smgr.obj's band and
//  have no server.dll twin recorded, so they carry their /O2 address. They
//  are here because an out-of-line member definition is emitted whether or
//  not the TU calls it, which is what lets a nine-byte constructor be
//  credited before the 1,300-byte function that constructs it is written.
// *********************************************************************** //

// 0x00463ce0, 9 bytes — a one-pointer constructor, run over an array of four.
//
//      8b c1                  mov %ecx,%eax
//      c7 00 00 00 00 00      movl $0x0,(%eax)
//      c3                     ret
//
// `mov %ecx,%eax` first, no arguments, plain `ret`, `this` back in %eax:
// __thiscall constructor. Its one caller, 0x004637c8, is a peeled array
// construction that gives the element size and count outright —
//
//      0x004637bb  lea 0x134(%esi),%edi     ; &m_Array[0]
//      0x004637c1  mov $0x4,%ebx            ; four elements
//      0x004637c6  mov %edi,%ecx
//      0x004637c8  call 0x00463ce0
//      0x004637cd  add $0xc,%edi            ; sizeof(element) == 0xc
//      0x004637d0  dec %ebx ; jne 0x004637c6
//
// — so the member is a four-element array at +0x134 of the object that
// caller builds, each element 12 bytes with only its head pointer set.
// Historical packet-set and reference-holder layout views are retired.
// ClientPacketSet and CMessageRef own the actual native message references.

// 0x00476770, 19 bytes — a lazy-init guard.
//
//      8b 44 24 04   mov 0x4(%esp),%eax
//      8b 48 54      mov 0x54(%eax),%ecx
//      85 c9         test %ecx,%ecx
//      75 07         jne 0x00476782
//      50            push %eax
//      e8 ..         call 0x00476660
//      59            pop %ecx
//      c3            ret
//
// cdecl, one argument. The `jne` goes FORWARD, past the only other
// statement, to the bare `ret` — so the guard is positive and its body is a
// trailing statement, i.e. the source tests the case that CONTINUES. The
// reloaded member at +0x54 is tested, not compared, so it is a pointer. The
// callee 0x00476660 is in the same module and reads both 0x188(%edx) and
// the file-scope pointer at 0x004b48c8.
// *********************************************************************** //
//  0x10026d23 / 0x00463750 — a connection becomes a client.
//
//  A prototype servermgr.h has carried since it was written and never
//  defined: the second of the three ways a finished function hides from an
//  address scan, and the second one this sweep found. Its declared return
//  type was LTRESULT and is really Client*, which the tail settles —
//  0x00463cb1 `mov eax,esi` puts the client in %eax on the way out.
//
//  Every offset, constant and branch below is cited by the lithtech address
//  it was read at; `S:` cites the server twin where that is the clearer
//  witness. The /O2 image expands Client's constructor, memset,
//  CMessageRef::Init and the CPacket::WriteType templates exactly where /O1
//  calls 0x100270d4, 0x1003e280, 0x1000ebab and 0x1000ec3d/0x1001393e — the
//  two-image oracle working as intended.
//
//  MEASURED: a NEAR on both images — 937 bytes against 917 at /O1 and 1356
//  against 1386 at /O2, in opposite directions, which is itself informative:
//  we are over on the /O1 side and under on the /O2 side, so the residual is
//  not one missing statement.
//
//  One block was diagnosed and fixed from the diff. The attach head is tied
//  off with TWO STORES WRITTEN OUT, not through dl_TieOff: /Ob1 cannot have
//  inlined that call, because dl_TieOff is out of line everywhere in the tree
//  and both images call it at every other site. Spelling it out took the
//  mnemonic diff from 24 blocks to 19.
//
//  What is left is the allocation tax this file keeps paying: our VC6
//  materialises the branchless-select constant into %eax and ANDs with the
//  register (`mov $X,%eax ; and %eax,%ebx`) where the shipped build ANDs with
//  the immediate. Same shape, more bytes, and nothing in the source reaches
//  it.
// *********************************************************************** //

// Native transfer adapter: the file manager owns mounted asset lookup;
// CClientFiles owns each opened stream for the duration of the transfer.
static ILTStream* open_client_transfer(void* context, const char* name) {
    return fs_OpenFile(static_cast<CFileMgr*>(context), const_cast<char*>(name));
}

// 0x00414930 — the client model's inherit-the-server's-file step, which
// csvis.cpp defines (lithtech.exe only) on its own view of the model.
class CInheritOwnerQ414;
void  cs_InheritFileFromServerQ414(CInheritOwnerQ414* pOwner);
void  dl_TieOff(LTLink* pHead);

extern int g_LocalNoFlag;                                    // 0x004b329c

// Prepare all native client resources before publishing the connection.
Client* sm_AddClientForConnection(CServerMgr* manager, NetConnection* connection, int local) {
    if (!manager || !connection) return nullptr;
    local = local && !g_LocalNoFlag;
    ServerMessage hello, identity;
    hello.WriteDWord(6);
    if (hello.m_Flags & PACKETFLAG_WRITEOVERFLOW) return nullptr;
    std::unique_ptr<Client> owned;
    try {
        owned = std::make_unique<Client>();
        if (!owned->Initialize(manager->m_Unknown8b8)) return nullptr;
        owned->m_pConnection = connection;
        if (local) owned->m_ClientFlags |= CLIENTFLAG_LOCAL;
        uint16 id = 0;
        for (; id < 30000; ++id) {
            bool taken = false;
            for (auto* link = manager->m_ClientList.m_Head.m_pNext; link != &manager->m_ClientList.m_Head; link = link->m_pNext)
                if (static_cast<Client*>(link->m_pData)->m_ClientID == id) { taken = true; break; }
            if (!taken) break;
        }
        if (id == 30000) return nullptr;
        owned->m_ClientID = id;
        identity.WriteWord(id);
        identity.WriteByte(static_cast<uint8>(local));
        if (identity.m_Flags & PACKETFLAG_WRITEOVERFLOW) return nullptr;
        CPacketHolderDesc descriptor{open_client_transfer, &manager->m_FileMgr, &manager->m_NetMgr, connection};
        owned->m_FileTransfer.reset(ph_CreateHolder(&descriptor, local ? 4 : 0));
        if (!owned->m_FileTransfer) return nullptr;
        for (const auto* file : manager->m_FileMgr.files())
            if (!owned->m_FileTransfer->Add(file->m_Name.c_str(), file->m_Unknown04, file->m_nFileID, uint16(file->m_Flags | 2)))
                return nullptr;
    } catch (const std::bad_alloc&) { return nullptr; }
    auto* client = owned.get();
    client->Register(manager->m_ClientList);
    if (local) manager->m_Unknown1a0 |= 2;
    const auto lifetime = client->m_Lifetime;
    owned.release();
    if (manager->m_ClassMgr.m_pServerShell) manager->m_ClassMgr.shell([&](IServerShell& shell) { return shell.OnAddClient(client->Handle()); });
    if (!*lifetime) return nullptr;
    sm_SendToClient(manager, client, 4, &hello, 0, 0x80);
    sm_SendToClient(manager, client, 0xc, &identity, 0, 0x80);
    if (!client->m_FileTransfer->Announce()) {
        manager->m_NetMgr.Disconnect(connection,6,0);
        return nullptr;
    }
    sm_SetClientState(manager, client, 1);
    return client;
}


// *********************************************************************** //
//  0x100291c7 — write one object's changed state into the update.
//
//  MOVED HERE from src/engine/lithtech/ccobj.cpp, which is a lithtech-only
//  translation unit -- so server.dll has never compiled this function at
//  all, and smgr.obj carried it as an undefined external while the
//  declaration above and sm_WriteObjectUpdate's call sat here waiting for
//  it. The /O1 side has no shape question: across
//  0x100291c7..0x10029609 every writer is an out-of-line call, which is
//  exactly what the body below spells.
//
//  The views come with it. They are the .cpp-local classes ccobj.cpp
//  gathered because a lithtech TU cannot reach three different server
//  .cpp-local ones; re-suffixed Q291 here so nothing collides with this
//  file's own. The one exception is sub_100290d7, which this file now
//  defines for real, so the local declaration of it is dropped.
//
//  THE ENUMERATED /O2 ROW IS WRONG IN LENGTH, per the note this block
//  carried: 0x00467540 is reported as 174 bytes, but its last instruction
//  branches to 0x004675ee, the next enumerated row. The two are one
//  function, 0x00467540..0x00467eec, three rets and no branch target
//  outside it -- the early `return 0` at 0x004675ed is a full epilogue, so
//  the byte after it looks like a start.
// *********************************************************************** //

// The six free writers under their real names (servercommon.cpp and
// servermsg.cpp define them; same signatures, so the calls are unchanged).
// lithtech.exe reaches the same definitions: 0x004670a0, 0x004358d0,
// 0x00435960, 0x00435ad0, 0x00435d00, 0x00435d60 (the call sequence over
// 0x00467540..0x00467eec).
void sm_WriteObjectFiles(LTObject* pObj, ServerMessage* pMsg);      // 0x10029087
void lmsg_CompressPos(uint8* pOut, LTVector* pVec, CPosRef* pRef);  // 0x10012ec5
void lmsg_WriteCompPosWords(IMessageWrite* pMsg, uint8* pData);     // 0x10012f49
void sub_1001307f(LTRotation* pRot, uint8* pOut);                   // 0x1001307f
void lmsg_WriteCompRotationBytes(IMessageWrite* pMsg, uint8* pData);// 0x10013287
void lmsg_WriteCompRotation(LMessageImpl* pMsg, LTRotation* pRot);  // 0x100132da

// The ServerMessage members servermsg.h does not carry.  Each already exists
// under exactly this shape elsewhere in the tree -- servermessage.cpp's
// ServerMessage (WriteDWord 0x1000ecd6, WriteString 0x10026137), smgr.cpp's
// ServerMessage (WriteFloat 0x10028a35) and CMsgLenView (GetSpaceLeft
// 0x1000e845) -- and they are gathered on one view here only because this
// file cannot reach three different .cpp-local classes.


// CF_MODELINFO sends a sprite's clip polygon, and the attachment block ends
// with a model's hidden pieces: the native members the original read at
// LTObject+0x1bc and +0x204 (0x10029381 / 0x100295f4). A container sends its
// code (+0x270).

// SObjData's two fields ltobject_internal.h does not name at these offsets:
// +0x2c is the stowed SFX ServerMessage (smgr.cpp's sm_SetObjectSFXMessage
// already treats it as one) and +0x4c is a flag word whose bit 0 sets 0x20
// in the info byte (0x100292d7 / 0x00467661).


// The file record the file id comes out of: smgr.cpp's CSoundIDRec is the
// same eight-bytes-then-a-word shape, and this is the file manager's.
class CFileIDRecQ291
{
public:
	uint8  m_pad_00[0x08];
	uint16 m_nID;                            // +0x08
};

// STAND-INS for sub_100291c7's lithtech.exe (/O2) shape, measured against
// 0x00467540..0x00467eec (2,476 bytes).  Neither changes server.dll.
//
// sm_FloatArgQ291: the SIX float writes that ship CALLS (0x00467800 ..
// 0x00467872, into WriteType<float> 0x00466700) load the member onto the
// x87 stack and `fstps (%esp)` it, where a plain member argument gives
// `mov/push` at /O2.  A value-returning inline accessor is what
// materialises a float in st(0) before a push; what the original accessor
// was is not visible.  (/O1 emits fld/fstp either way.)
inline float sm_FloatArgQ291(const float& f) { return f; }

#ifndef LT_SERVER_BUILD
// sm_WritePacketDWordQ291: CPacket::WriteType<uint32> with the grow reaching
// Insert2 DIRECTLY instead of through Append->Insert.  Only at that depth
// does /O2 expand BaseDelete inside the expanded Insert2 (the virtual
// Free at 0x00467e2d) at the function's LAST write, as shipped does.
// packet.h's two-level chain leaves BaseDelete a call there (measured).
//
// RESIDUAL (lithtech, 2,655 vs 2,476 bytes, 4 mnemonic blocks): the
// `WriteType((uint16)0xffff)` after the attachment loop still expands
// Insert2, where shipped CALLS it (0x00467cec) -- /O2's inline budget runs
// out one site earlier in shipped.  Not moved by an extra wrapper level on
// either site, by dropping sm_FloatArgQ291, or by the direct-Insert2 helper
// at the 0xffff site (then the LAST site loses BaseDelete instead).
// Everything from the attachment loop on (GetSpaceLeft, the seven writes)
// is expanded in shipped, so those sites are written as the inline
// CPacket::WriteType / CMsgLenView::GetSpaceLeft; every earlier write is a
// call in both images.
inline void sm_WritePacketDWordQ291(CPacket* p, uint32 val)
{
	if (p->m_Pos + sizeof(uint32) < p->m_MaxDataLen)
	{
		while (p->m_DataLen + sizeof(uint32) >= p->m_Data.GetSize())
			p->m_Data.Insert2(p->m_Data.m_nElements, 0, &g_DefAlloc);

		*(uint32*)&p->m_Data.m_pArray[p->m_Pos] = val;
		p->m_Pos += sizeof(uint32);
		p->m_DataLen += sizeof(uint32);
	}
	else
	{
		p->ReportWriteOverrun();
		p->m_Flags |= PACKETFLAG_WRITEOVERFLOW;
	}
}
#endif

// 0x100291c7 / 0x00467540.
//
// HOW THE FLAG WORD IS SPELLED.  It is a uint16 with a dword home -- both
// images store it 32 bits wide and read it back with `cmpw` / `test %bx,%bx`
// (0x1002925e, 0x004675e1), which smgr.cpp's sub_10027ad0 note
// records as what VC6 does with a uint16 local.  The masks are written
// POSITIVE: the shipped immediates are 0x0000fbfb, 0x0000fffd, 0x0000fffb,
// 0x0000ffdf, 0x0000ffbf and 0x0000fdff, and `nFlags &= ~0x0404` on a uint16
// would give 0xfffffbfb instead.  So the source has the sixteen-bit
// complements, not `~CF_`.
//
// The three-way clear at the top is ONE statement (`and` then `or`, one
// store, 0x10029236..0x10029240) where every other clear is its own.
//
// The header is two WriteBytes or one, with the SECOND call shared: the
// wide arm writes (low | 0x80) and falls into the common WriteByte with the
// high byte, the narrow arm jumps into the same call with the whole word.
// That is a tail merge, not two separate statements.
//
// Every member read is WRITTEN OUT at each use.  pObject->m_pSData is
// fetched four times, pObject->m_pSData->m_SFXMessage.m_pMsg three, and
// pObject->m_ObjectType six; the single load each straight-line region shows
// is VC6's CSE inside a block, and the re-read after every call is what says
// there is no local.
//
// The light radius is a comparison in the SOURCE, not a conversion artefact:
// `fcoms` against 0.0f (0x1004d208 / 0x004a14d8) with the `< 0` arm FALLING
// THROUGH to `push $0` and the `>= 0` arm jumping to __ftol, and the two
// WriteWord calls merged.  A plain `(uint16)fRadius` emits __ftol with no
// compare at all.
// The radius IS a local, the one exception to the written-out rule: ship
// keeps the loaded value on the x87 stack across the compare (`fcoms`, then
// `fstp %st(0)` on the `< 0` arm, 0x10029438..0x10029443), where reading the
// member twice gives `fcomps` + a `lea` + a reload.
int sub_100291c7(CServerMgr* pMgr, Client* pClient, LTObject* pObject,
                     CObjInfo* pInfo, ServerMessage* pMsg)
{
	uint16 nFlags;
	uint8  nInfo;
	// TWO buffers, not one: server.dll gives the position seven bytes at
	// -0x10(%ebp) and the rotation six at -0x8(%ebp), and both images size
	// the frame at 0x10 for exactly that pair.  The spellings are
	// servermsg.cpp's own (lmsg_WriteCompPos / lmsg_WriteCompRotation).
	uint8  posData[7];
	uint8  rotData[6];

	nFlags = pInfo->m_ChangeFlags;

	if (!(pClient->m_ClientFlags & CLIENTFLAG_SENDCOBJROTATION) &&
		pClient->m_pUnknown3AC == pObject)
	{
		nFlags &= 0xfbfb;                    // ~(CF_ROTATION | 0x400)
	}

	if ((pObject->m_pSData)->m_NetFlags & NETFLAG_POSUNGUARANTEED)
		nFlags &= 0xfffd;                    // ~CF_POSITION

	if ((pObject->m_pSData)->m_NetFlags & NETFLAG_ROTUNGUARANTEED)
		nFlags &= 0xfffb;                    // ~CF_ROTATION

	if (pObject->m_ObjectType == OT_MODEL &&
		((pObject->m_pSData)->m_NetFlags & NETFLAG_ANIMUNGUARANTEED))
	{
		nFlags &= 0xffdf;                    // ~CF_MODELINFO
	}

	if (nFlags & CF_NEWOBJECT)
		nFlags = (uint16)((nFlags & 0xfdff) | CF_POSITION);

	if ((nFlags & CF_COLORINFO) &&
		(pObject->m_ObjectType == OT_NORMAL ||
		 pObject->m_ObjectType == OT_CONTAINER ||
		 pObject->m_ObjectType == OT_CAMERA))
	{
		nFlags &= 0xffbf;                    // ~CF_COLORINFO
	}

	if (nFlags == 0)
		return 0;

	if (nFlags & 0x700)
	{
		pMsg->WriteByte((uint8)(nFlags | 0x80));
		pMsg->WriteByte((uint8)(nFlags >> 8));
	}
	else
	{
		pMsg->WriteByte((uint8)nFlags);
	}

	pMsg->WriteWord(pObject->m_ObjectID);

	if (nFlags & CF_NEWOBJECT)
	{
		nInfo = (uint8)pObject->m_ObjectType;

		if ((pObject->m_pSData)->m_SFXMessage.m_pMsg)
		{
			nInfo |= 0x40;

			if ((pObject->m_pSData)->m_SFXMessage.m_pMsg->m_Unknown34
				>= 0x100)
			{
				nInfo |= 0x80;
			}
		}

		if ((pObject->m_pSData)->m_CreateFlags & 1)
			nInfo |= 0x20;

		pMsg->WriteByte(nInfo);

		if ((pObject->m_pSData)->m_SFXMessage.m_pMsg)
		{
			if (nInfo & 0x80)
			{
				pMsg->WriteWord((pObject->m_pSData)
					->m_SFXMessage.m_pMsg->m_Unknown34);
			}
			else
			{
				pMsg->WriteByte((uint8)(pObject->m_pSData)
					->m_SFXMessage.m_pMsg->m_Unknown34);
			}

			// THROUGH ServerMessage, whose length parameter is uint16.
			// servermsg.h's WriteData takes uint32, which makes our build
			// zero-extend (`movzwl 0x34(%eax),%ecx`); the shipped call at
			// 0x10029320 is `mov cx,WORD PTR [eax+0x34]` and pushes the dword
			// with a stale top half, which is what a uint16 argument does.
			// That view already exists in this file for the same reason.
			(pMsg)->WriteData(
				(pObject->m_pSData)->m_SFXMessage.m_pMsg->m_pData,
				(pObject->m_pSData)->m_SFXMessage.m_pMsg->m_Unknown34);
		}

		if (pObject->m_ObjectType == OT_WORLDMODEL)
		{
			(pMsg)->WriteString(pObject->m_pWorldBsp->m_Name);
		}
		else if (pObject->m_ObjectType == OT_CONTAINER)
		{
			(pMsg)->WriteString(pObject->m_pWorldBsp->m_Name);

			pMsg->WriteWord(static_cast<CObjContainer*>(pObject)->m_ContainerCode);
		}
		else if (pObject->m_ObjectType == OT_MODEL)
		{
			sm_WriteObjectFiles(pObject, pMsg);
		}
		else if (pObject->m_ObjectType == OT_SPRITE)
		{
			pMsg->WriteWord(static_cast<CFileRef*>(pObject->m_pSData->m_pModelFile)->m_nFileID);
		}
	}

	if (nFlags & CF_MODELINFO)
	{
		if (pObject->m_ObjectType == OT_MODEL)
		{
			sub_100290d7(pObject, pMsg);
		}
		else if (pObject->m_ObjectType == OT_SPRITE)
		{
			(pMsg)->WriteDWord(
				static_cast<CObjSprite*>(pObject)->m_ClipPoly);
		}
	}

	if (nFlags & CF_FLAGS)
	{
		pMsg->WriteWord((uint16)pObject->m_Flags);
		pMsg->WriteByte((uint8)pObject->m_Flags2);
		(pMsg)->WriteDWord(pObject->m_UserFlags);
	}

	if (nFlags & CF_COLORINFO)
	{
		pMsg->WriteByte(pObject->m_ColorR);
		pMsg->WriteByte(pObject->m_ColorG);
		pMsg->WriteByte(pObject->m_ColorB);
		pMsg->WriteByte(pObject->m_ColorA);

		if (pObject->m_ObjectType == OT_LIGHT)
		{
			float fRadius = pObject->m_LightRadius;
			if (fRadius < 0.0f)
				pMsg->WriteWord(0);
			else
				pMsg->WriteWord((uint16)fRadius);
		}
	}

	if (nFlags & CF_SCALE)
	{
		(pMsg)->WriteFloat(sm_FloatArgQ291(pObject->m_Scale.x));
		(pMsg)->WriteFloat(sm_FloatArgQ291(pObject->m_Scale.y));

		if (pObject->m_ObjectType != OT_SPRITE)
			(pMsg)->WriteFloat(sm_FloatArgQ291(pObject->m_Scale.z));
	}

	if (nFlags & (CF_POSITION | 0x200))
	{
		if (pObject->m_Flags & FLAG_FULLPOSITIONRES)
		{
			(pMsg)->WriteFloat(sm_FloatArgQ291(pObject->m_Pos.x));
			(pMsg)->WriteFloat(sm_FloatArgQ291(pObject->m_Pos.y));
			(pMsg)->WriteFloat(sm_FloatArgQ291(pObject->m_Pos.z));

			((IMessageWrite*)&pMsg->m_Write)->WriteVector(&pObject->m_Velocity);
		}
		else
		{
			lmsg_CompressPos(posData, &pObject->m_Pos,
				pMgr->m_World.PositionReference());

			lmsg_WriteCompPosWords((IMessageWrite*)&pMsg->m_Write,
				posData);

			((IMessageWrite*)&pMsg->m_Write)->WriteCompVector(
				&pObject->m_Velocity);
		}
	}

	if (nFlags & (CF_ROTATION | 0x400))
	{
		if (pObject->m_Flags & FLAG_FULLPOSITIONRES)
		{
			((IMessageWrite*)&pMsg->m_Write)->WriteRotation(
				&pObject->m_Rotation);
		}
		else
		{
			sub_1001307f(&pObject->m_Rotation, rotData);

			lmsg_WriteCompRotationBytes((IMessageWrite*)&pMsg->m_Write,
				rotData);
		}
	}

	if (nFlags & CF_ATTACHMENTS)
	{
		Attachment* pAttach = pObject->m_pAttachments;

		while (pAttach)
		{
			if ((static_cast<ServerMessage*>(pMsg))->GetSpaceLeft() >= 30)
			{
				((CPacket*)pMsg)->WriteType((uint16)(pAttach->m_nChildID));

				((CPacket*)pMsg)->WriteType((uint32)(pAttach->m_iNode));

				((CPacket*)pMsg)->WriteType(pAttach->m_Offset.m_Pos.x);
				((CPacket*)pMsg)->WriteType(pAttach->m_Offset.m_Pos.y);
				((CPacket*)pMsg)->WriteType(pAttach->m_Offset.m_Pos.z);

				lmsg_WriteCompRotation(&pMsg->m_Write,
					&pAttach->m_Offset.m_Rot);
			}

			pAttach = pAttach->m_pNext;
		}

		((CPacket*)pMsg)->WriteType((uint16)0xffff);

		if (pObject->m_ObjectType == OT_MODEL)
		{
#ifndef LT_SERVER_BUILD
			sm_WritePacketDWordQ291((CPacket*)pMsg,
				static_cast<ModelInstance*>(pObject)->m_HiddenPieces);
#else
			((CPacket*)pMsg)->WriteType((uint32)
				static_cast<ModelInstance*>(pObject)->m_HiddenPieces);
#endif
		}
	}

	return 1;
}


// 0x100273e9 (211 bytes, /O1) / 0x004641a0 (251 bytes, /O2) — the last thing
// sm_RemoveClient does: everything the client record owns, then the record.
// Ported from the /O2-tuned copy in src/engine/lithtech/ccobj.cpp, which is
// byte-identical at 0x004641a0; that copy is kept in step rather than
// deleted, exactly as sub_100291c7's is.
//
// The manager parameter is ACCEPTED AND NEVER READ. Both images load only
// the second argument (`mov 0xc(%ebp),%esi` at 0x100273ee), so the pair in
// the declaration above is confirmed and the first one is dead.
//
// Client+0x18c IS THE FILE-TRANSFER STATE, which corrects servermgr.h's
// "unhandled packets go here": 0x100273f7 hands it to svnew.cpp's xfer_Term
// (0x1000e5c5), so it is a CClientFilesView*.
//
// THE FREES ARE THREE DIFFERENT FUNCTIONS and the split is the shape:
// 0x1000d398 is `dfree` (the four engine-allocated buffers), 0x10028ace is
// the one-call wrapper servermgr.h calls net_Free and that is also the
// image's `operator delete` (the client data block, and the record itself),
// and dhash_Free (0x1000dbe0) takes the hash.
//
// The server returns its pooled records before the client releases its owned state.
void sub_100273e9(CServerMgr*, Client* client) { delete client; }

// 0x10013585 (sub_10013585, the facing as one byte) is servermsg.cpp's.


// ---- light-animation changes to one client ------------------------------ //
//
// Here rather than in serverlightanim.cpp: both images put sm_Unknown26755
// inside this file's run (lithtech.exe 0x00462fd0 between 0x00462ee0 and
// 0x004630f0; server.dll 0x10026755 before 0x10026859).
#include "serverlightanim.h"

void la_WriteChange(CServerMgr* pMgr, LightAnim* pAnim, uint32 hLightAnim,
                    ServerMessage* pMsg, uint32 nChangeFlags);

// 0x1000e845 — how many more bytes the message will take: the declared
// maximum less the write cursor, less seven, floored at zero. servermsg.h
// does not carry it and is not this reconstruction's to change, so the
// message is cast to reach it (name ours).



// 0x10026755 — flush a client's queued light-animation changes. One packet is
// filled with as many changes as fit and sent whenever it runs low on room,
// then the remainder goes out at the end and the queue is emptied.
// servermgr.h names it sm_Unknown26755, which is the name both call sites
// already use.
//
// Read off the bytes:
//
//   * "runs low on room" is the space accessor answering 0x16 or less, and it
//     is asked *after* the change has been written, so the flush is a
//     look-ahead for the next one.
//   * a sent packet is reset by storing 1 into the payload length and the
//     write cursor, in that order (0x38 before 0x34) — the same pair of
//     statements every direct send in smgr.cpp opens with.
//   * the tail flush tests `m_Unknown34 - 1 > 0` rather than `> 1`: the
//     shipped code decrements and tests, so the subtraction is in the source.
//   * an animation index the world does not have is skipped without being
//     dequeued separately — the whole queue is cleared at the end regardless.
//   * the loop counter and the queue cursor are both memory-homed because the
//     manager, the client and the hoisted 0x80 priority take all three
//     callee-saved registers; the cursor lands in the dead pClient parameter
//     slot, and the element pointer is a body local, which is what puts the
//     induction variable on the record's first byte rather than on its
//     +2 field.
//   * the animation index is read off the change at each use rather than
//     through a named local: with a local, `&m_pLightAnims[h]` accumulated
//     into %ecx; read in place, the base is added straight into %eax as
//     shipped (0x100267c1), and lithtech.exe's twin (0x00462fd0) went from
//     six mnemonic blocks to three.
//   * pIO is at function scope: in a block of its own its slot is freed and
//     reused for `i`, which the shipped frame (8 bytes: the reference and
//     pIO) does not do.
//
// NEAR, size-exact on server.dll (260 of 260 bytes, 94 instructions), and
// lithtech.exe 290 vs 286 with three mnemonic blocks. The whole residual on
// both images is where `i` is homed: the shipped builds put it in the dead
// pMgr parameter slot (0x8(%ebp) here; 0x14(%esp) at 0x00463013), ours gives
// it a frame slot of its own. On server.dll that also lets VC6 park a zero
// in %ebx for the reference's constructor, the pIO test, `i = 0` and the
// loop guard (shipped: `andl $0x0` and `cmpl $0x0` each time), which moves
// the three callee-saved pushes from after the pIO release to the prologue.
// Dead-parameter-slot homing has no known source lever in this tree.
//
// Falsified against it: cl 8168, 8447 and 8966 (all three identical here);
// pIO in an inner block; `i` at function scope, first among the locals,
// after pLAClient, in the for-init, as an `int`, as a while loop, and
// initialized before the allocation; the queue walked by index, by a hoisted
// cursor, and by a body-local element pointer; the index as a uint32 or
// uint16 local; pLAClient dropped for casts at each use; the reset pairs
// written through msg.m_pMsg directly (265 bytes); `pIO != LTNULL`.
void sm_Unknown26755(CServerMgr* pMgr, Client* pClient)
{
	CMessageRef msg;
	Client* pLAClient = pClient;
	CAbstractIO* pIO;

	msg.Init(msg_AllocPacket(&pIO, 0x1000, 0x1000));
	if (pIO)
		pIO->Release();
    if (!msg.m_pMsg) return;

	for (uint32 i = 0; i < std::min<uint32>(pLAClient->m_nLightChanges,MAX_CLIENTLACHANGES); i++)
	{
		ClientLightChange* pChange = &pLAClient->m_LightChanges[i];

		if (pChange->m_hLightAnim < pMgr->m_nLightAnims)
		{
			la_WriteChange(pMgr, &pMgr->m_pLightAnims[pChange->m_hLightAnim],
			               pChange->m_hLightAnim, msg.m_pMsg,
			               pChange->m_ChangeFlags);

			if ((static_cast<ServerMessage*>(msg.m_pMsg))->GetSpaceLeft() <= 0x16)
			{
				sm_SendToClient(pMgr, pClient, 0x19, msg.m_pMsg, 0, 0x80);

				{
					ServerMessage* pMsg = msg.m_pMsg;

					pMsg->m_Unknown38 = 1;
					pMsg->m_Unknown34 = 1;
				}
			}
		}
	}

	if (msg.m_pMsg->m_Unknown34 - 1 > 0)
	{
		sm_SendToClient(pMgr, pClient, 0x19, msg.m_pMsg, 0, 0x80);

		{
			ServerMessage* pMsg = msg.m_pMsg;

			pMsg->m_Unknown38 = 1;
			pMsg->m_Unknown34 = 1;
		}
	}

	pLAClient->m_nLightChanges = 0;
}
