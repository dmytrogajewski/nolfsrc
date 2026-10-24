// server.dll — the ILTServer message starters (reconstruction). Build /O1.
#include <string.h>
#include <stdio.h>
#include <limits>
#include <vector>
#include <cstring>
#include "iltserver.h"
#include "servermsg.h"
#include "engine/portable/message_format.h"
#include "engine/portable/allocation.h"
#include "commonphysics.h"
#include "sharedcommon.h"
#include "commonmodel.h"
#include "servermodel.h"
#include "servermgr.h"
#include "de_error.h"
#include "packet.h"

// FormatString's callees (names ours): format a message-table string from
// the class module, wrap the result in a string handle, release the raw.
void*   str_FormatMessage(const nolf::ResourceModule* hModule, int messageCode, va_list* pMarker,
                          uint32* pLen);   // 0x10035caa
void    str_FreeMessage(void* pRaw);       // 0x10035d39

// 0x10034fb2 — one dereference, cdecl, seven bytes: an accessor VC6 left out
// of line. WriteFormattedString hands it CClassMgr::m_pUnknown08 and passes
// the result to str_FormatMessage, which dereferences again -- so the 0x814
// field points at a record whose first word is the module handle, one
// indirection further out than the m_hModule at 0x810 that FormatString uses
// (name ours).
// 0x10034fb2 / 0x00477f30, seven bytes and no relocation, byte-identical
// between the two images. Declared here since this file was written and never
// defined, which is the third of the three ways a function hides from an
// address scan: the address lives beside a declaration, so a bare-name grep
// finds this file and an address grep finds nothing. Three callers in
// lithtech.exe -- 0x0040edf4, 0x0041560a, 0x0047be4a.
//
//      8b 44 24 04   mov 0x4(%esp),%eax
//      8b 00         mov (%eax),%eax
//      c3            ret
void* clm_GetRecordModule(void* pRecord)
{
	return *(void**)pRecord;
}

// The engine's string-handle helpers (all cdecl, defined elsewhere).
HSTRING sb_CopyString(HSTRING hString);                        // 0x10035e3e
HSTRING sb_CreateString(char* pString);                        // 0x10035dca
void    sb_FreeString(HSTRING hString);                        // 0x1001372f
LTBOOL  sb_CompareStrings(HSTRING h1, HSTRING h2);             // 0x10035e76
LTBOOL  sb_CompareStringsUpper(HSTRING h1, HSTRING h2);        // 0x10035e9e
char*   sb_GetStringData(HSTRING hString);                     // 0x10035ec6
// (the identifier's name is its hash key: see dhash_GetKey, 0x1000ddfb)

// Where GetObjectName looks: the object's engine-side data at +0x188 carries
// an identifier handle at +0xc; objects without one share this "" in .data
// (0x10056ff4, directly after g_DebugLevel).
// Empty object names use the shared native string-bank storage.

// CMessageRef::Init now lives in servermsg.h, `inline`, where the original
// had it -- the local copy this file used to carry was a workaround for its
// absence and would now be a duplicate definition.

// ---- class reflection (the TU's opening block, 0x1002e074-0x1002e34b) ---- //
//
// HCLASS is a CClassInfo*, whose slot table at +0x24 starts with the SDK
// ClassDef. Both property methods misname their error site
// "CLTServer::GetClassDef" — one pooled literal, two static name pointers.

LTRESULT CLTServer::GetNumClassProps(const HCLASS hClass, uint32 &count)
{
    count = 0;
    const auto* record = m_pServerMgr ? m_pServerMgr->m_ClassMgr.record(hClass) : nullptr;
    if (!record) return LT_INVALIDPARAMS;
    std::uint64_t total = 0;
    for (auto* definition = record->m_pClassDef; definition; definition = definition->m_ParentClass)
        total += definition->m_nProps;
    if (total > std::numeric_limits<uint32>::max()) return LT_ERROR;
    count = uint32(total);
    return LT_OK;
}

LTRESULT CLTServer::GetClassProp(const HCLASS hClass, const uint32 iProp, ClassPropInfo &info)
{
    info = {};
    const auto* record = m_pServerMgr ? m_pServerMgr->m_ClassMgr.record(hClass) : nullptr;
    if (!record) return LT_INVALIDPARAMS;
    std::vector<ClassDef*> lineage;
    for (auto* definition = record->m_pClassDef; definition; definition = definition->m_ParentClass)
        lineage.push_back(definition);
    std::uint64_t base = 0;
    for (auto it = lineage.rbegin(); it != lineage.rend(); ++it) {
        const auto* definition = *it;
        if (iProp >= base && iProp - base < unsigned(definition->m_nProps)) {
            const auto& property = definition->m_Props[iProp - base];
            std::strncpy(info.m_PropName,property.m_PropName,MAX_CLASSPROPINFONAME_LEN-1);
            info.m_PropType = property.m_PropType;
            return LT_OK;
        }
        base += definition->m_nProps;
    }
    return LT_NOTFOUND;
}

LTRESULT CLTServer::GetClassName(const HCLASS hClass, char *pName, uint32 maxNameBytes)
{
    if (maxNameBytes && !pName) return LT_INVALIDPARAMS;
    if (maxNameBytes) pName[0] = 0;
    const auto* record = m_pServerMgr ? m_pServerMgr->m_ClassMgr.record(hClass) : nullptr;
    if (!record) return LT_INVALIDPARAMS;
    if (maxNameBytes) {
        std::strncpy(pName,record->m_pClassDef->m_ClassName,maxNameBytes-1);
        pName[maxNameBytes-1] = 0;
    }
    return LT_OK;
}

LTRESULT CLTServer::GetClientPing(HCLIENT handle, float& ping) {
    ping = 0;
    if (!m_pServerMgr) return LT_NOTINITIALIZED;
    auto* client = client_Find(m_pServerMgr->m_ClientList,handle);
    if (!client) return LT_INVALIDPARAMS;
    if (!client->m_pConnection || client->m_Removing) return LT_NOTINITIALIZED;
    ping = client->m_pConnection->m_Ping;
    return LT_OK;
}

LTRESULT CLTServer::GetNetFlags(HOBJECT hObj, uint32 &flags)
{
	if (!hObj)
		RETURN_ERROR(2, "GetNetFlags", LT_INVALIDPARAMS);

	flags = hObj->m_pSData->m_NetFlags;
	return LT_OK;
}

LTRESULT CLTServer::SetNetFlags(HOBJECT hObj, uint32 flags)
{
	if (!hObj)
		RETURN_ERROR(2, "SetNetFlags", LT_INVALIDPARAMS);

	// Unguaranteed animation only means something on a model.
	if (hObj->m_ObjectType != OT_MODEL)
		flags &= ~NETFLAG_ANIMUNGUARANTEED;

	hObj->m_pSData->m_NetFlags = (uint16)flags;
	return LT_OK;
}

LTRESULT CLTServer::GetClientData(HCLIENT handle, void*& data, uint32& length) {
    data = nullptr;
    length = 0;
    if (!m_pServerMgr) return LT_NOTINITIALIZED;
    auto* client = client_Find(m_pServerMgr->m_ClientList,handle);
    if (!client) return LT_INVALIDPARAMS;
    data = client->Data().empty() ? nullptr : client->Data().data();
    length = static_cast<uint32>(client->Data().size());
    return LT_OK;
}

// ---- the threaded-loader halves (0x1002ec9a-0x1002ef14) ------------------ //

// 0x1002eceb — tell every client about one file, and mark it `inline`.
//
// lithtech.exe has NO out-of-line copy of this function: at /O2 it is expanded
// into all four of the loader halves below (and, through them, into
// ThreadLoadFile and UnloadFile), which is why its call to sm_SendToClient and
// its msg_AllocPacket appear only inside those two in the shipped image. VC6's
// inliner is forward-only, so it has to sit above ThreadLoadModel; at 199
// bytes it is far past what /Os will take, so server.dll's /O1 build still
// calls it out of line and keeps its own byte-identical copy.
inline void CLTServer::BroadcastFileMessage(uint32 nFileType, uint32 nMsgID, uint16 nFileID, int bToAll)
{
	CMessageRef msg;
	LTLink* pListHead;
	LTLink* pCur;

	pListHead = &m_pServerMgr->m_ClientList.m_Head;
	for (pCur = pListHead->m_pNext; pCur != pListHead; pCur = pCur->m_pNext)
	{
		Client* pClient = (Client*)pCur->m_pData;

		if (!bToAll && (pClient->m_ClientFlags & 2))
			continue;

		{
			CAbstractIO* pIO;
			msg.Init(msg_AllocPacket(&pIO, 0x1000, 0x1000));
			if (pIO)
				pIO->Release();
		}

		msg.m_pMsg->WriteByte((uint8)nFileType);
		msg.m_pMsg->WriteWord((uint16)nFileID);

		sm_SendToClient(m_pServerMgr, pClient, nMsgID, msg.m_pMsg, 0, 0x80);
	}
}

// The four loader halves are `inline` for the same reason: none of them has
// an out-of-line body in lithtech.exe either -- ThreadLoadFile (732 bytes at
// 0x0046dd50) and UnloadFile (744 at 0x0046e030) are the two of them expanded
// end to end, BroadcastFileMessage and all. At 84-150 bytes apiece /Os leaves
// them alone, so server.dll's /O1 halves are untouched.
inline LTRESULT CLTServer::ThreadLoadModel(char *pFilename)
{
	Model* pModel;
	CFileIdentifier* pFile;
	LTRESULT dResult;

	dResult = sm_LoadModel(m_pServerMgr, pFilename, &pModel, &pFile, 1, 0);
	if (pFile && (dResult == LT_OK || dResult == LT_INPROGRESS))
		BroadcastFileMessage(FT_MODEL, 0x17, pFile->m_nFileID, 0);

	return dResult;
}

inline LTRESULT CLTServer::ThreadLoadTexture(char *pFilename)
{
	CFileIdentifier* pFile;

	if (!fs_GetFileIdentifier(&m_pServerMgr->m_FileMgr, pFilename, 0, (void**)&pFile))
		RETURN_ERROR_INFO(1, "ThreadLoadTexture", LT_MISSINGFILE, pFilename);

	BroadcastFileMessage(FT_TEXTURE, 0x17, pFile->m_nFileID, 1);
	return LT_OK;
}

// Both unload halves report their missing-file error as "ThreadLoadTexture"
// — Monolith's copy-paste, and what the shipped literals say.

inline LTRESULT CLTServer::UnloadModel(char *pFilename)
{
	CFileIdentifier* pFile;

	if (!fs_GetFileIdentifier(&m_pServerMgr->m_FileMgr, pFilename, 0, (void**)&pFile))
		RETURN_ERROR_INFO(1, "UnloadModel", LT_MISSINGFILE, pFilename);

	LTRESULT dResult = sm_UnloadModel(m_pServerMgr, pFilename, pFile);
	if (dResult != LT_OK)
		return dResult;

	BroadcastFileMessage(FT_MODEL, 0x18, pFile->m_nFileID, 0);
	return LT_OK;
}

inline LTRESULT CLTServer::UnloadTexture(char *pFilename)
{
	CFileIdentifier* pFile;

	if (!fs_GetFileIdentifier(&m_pServerMgr->m_FileMgr, pFilename, 0, (void**)&pFile))
		RETURN_ERROR_INFO(1, "UnloadTexture", LT_MISSINGFILE, pFilename);

	BroadcastFileMessage(FT_TEXTURE, 0x18, pFile->m_nFileID, 1);
	return LT_OK;
}

// 0x1002ef12 (23 bytes) / lithtech 0x0046eb30 — ILTServer::SetModelFilenames,
// the one iltserver.h marks "OBSOLETE: use ILTCommon::SetObjectFilenames".
// It is slot 117 of the server interface's vtable (ILTCSBase's 71 slots plus
// ILTServer index 46), and its whole body forwards all three arguments,
// unchanged, to the NEXT slot: `call *0x1d8(%eax)` is 0x1d8 / 4 = 118,
// SetObjectFilenames (below). Placed here because it sits between UnloadTexture
// (ends 0x1002ef0f) and SetObjectFilenames (0x1002ef29) in the image.
//
// servermsg.h's CLTServer has no vtable of its own, so the virtual call is
// written against the SDK interface it implements, through a view that adds
// nothing to it (name ours). The two images encode the three pushes
// differently from this one source: `push 0xc(%esp)` at /O1, `mov
// 0xc(%esp),%edx; push %edx` at /O2.


LTRESULT CLTServer::SetModelFilenames(HOBJECT hObj, char *pFilename, char *pSkinName)
{
	return SetObjectFilenames(hObj, pFilename, pSkinName);
}

// The obsolete flat form (lithtech 0x0046eb50): build a create struct around
// the two names and hand it to ILTCommon. (The declaration's ctor already
// runs Clear; the explicit Clear is Monolith's, and the linker folds the two
// identical COMDATs onto one address.)
//
// Written against ILTServer (through the view above) because the reach for
// the common interface is ILTCSBase's inline Common(), and the two copies
// are LTStrCpy expansions (what SAFE_STRCPY becomes for a 128-byte buffer).
// /O1 folds all three to the same bytes as a raw member read and strncpy +
// terminator; /O2 does not, because they spend lithtech's per-function
// inline budget: shipped leaves LTVector::Init (0x0041ab50) x2 and
// LTRotation::Init (0x00402ea0) as calls in the constructor's Clear and
// LTRotation::Init as a call in the explicit one. strncpy + m_pCommonLT
// expands everything (301 bytes), LTStrCpy alone runs one unit short (380);
// with Common() it is 409, identical. (servermsg.h still declares a
// CLTServer::SetObjectFilenames; this is its one definition.)
LTRESULT CLTServer::SetObjectFilenames(HOBJECT hObj, char *pFilename, char *pSkinName)
{
	ObjectCreateStruct ocs;

	ocs.Clear();
	LTStrCpy(ocs.m_Filename, pFilename, sizeof(ocs.m_Filename));
	LTStrCpy(ocs.m_SkinName, pSkinName, sizeof(ocs.m_SkinName));

	return Common()->SetObjectFilenames(hObj, &ocs);
}

// ---- the old flat model-state block and friends (0x1002e8a0-0x1002eb36) -- //

LTRESULT CLTServer::CopyFile(const char *pszSourceFile, const char *pszDestFile)
{
	return fs_CopyFile(&m_pServerMgr->m_FileMgr, pszSourceFile, pszDestFile);
}

void CLTServer::SetModelAnimation(HOBJECT hObj, HMODELANIM hAnim)
{
	ModelInstance* pInst = (ModelInstance*)hObj;

	if (hObj->m_ObjectType != OT_MODEL)
		return;

	LTAnimTracker* pTracker = &pInst->m_AnimTracker;
	if (hAnim == pTracker->m_TimeRef.m_Cur.m_iAnim)
		return;

	// bit 7 = FLAG_ANIMTRANSITION (the literal shift is what the bytes say)
	at_SetCurAnim(pTracker, hAnim, (hObj->m_Flags >> 7) & 1);
	sm_SetObjectChangeFlag(g_pServerMgr, hObj, CF_MODELINFO);
}

HMODELANIM CLTServer::GetModelAnimation(HOBJECT hObj)
{
	if (hObj->m_ObjectType == OT_MODEL)
		return ((ModelInstance*)hObj)->m_AnimTracker.m_TimeRef.m_Cur.m_iAnim;

	return (HMODELANIM)-1;
}

void CLTServer::SetModelLooping(HOBJECT hObj, LTBOOL bLoop)
{
	ModelInstance* pInst = (ModelInstance*)hObj;

	if (hObj->m_ObjectType != OT_MODEL)
		return;

	// Direct field read-modify-write: the compiler loads m_Flags into ax
	// once, keeps the arithmetic 16-bit, and stores once. Through a tracker
	// pointer, for the test AND the update: lithtech.exe (0x0046e582) keeps
	// the inlined change-flag's `mov 0x120(%esi),%cl` BELOW the 16-bit store,
	// which it only does when the store is not off the object's own base;
	// spelled `pInst->m_AnimTracker.m_Flags` the load is hoisted above it.
	LTAnimTracker* pTracker = &pInst->m_AnimTracker;
	if (((pTracker->m_Flags >> 1) & 1) != (bLoop != 0))
	{
		if (bLoop)
			pTracker->m_Flags |= AT_LOOPING;
		else
			pTracker->m_Flags &= ~AT_LOOPING;

		sm_SetObjectChangeFlag(g_pServerMgr, hObj, CF_MODELINFO);
	}
}

LTBOOL CLTServer::GetModelLooping(HOBJECT hObj)
{
	if (hObj->m_ObjectType == OT_MODEL)
		return (((ModelInstance*)hObj)->m_AnimTracker.m_Flags >> 1) & 1;

	return LTFALSE;
}

LTRESULT CLTServer::ResetModelAnimation(HOBJECT hObj)
{
	if (hObj && hObj->m_ObjectType == OT_MODEL)
	{
		at_ResetAnim(&((ModelInstance*)hObj)->m_AnimTracker);
		sm_SetObjectChangeFlag(g_pServerMgr, hObj, CF_MODELINFO);
		return LT_OK;
	}

	RETURN_ERROR(1, "ILTServer::ResetModelAnimation", LT_ERROR);
}

uint32 CLTServer::GetModelPlaybackState(HOBJECT hObj)
{
	if (hObj->m_ObjectType == OT_MODEL && at_IsDone(&((ModelInstance*)hObj)->m_AnimTracker))
		return MS_PLAYDONE;

	return 0;
}

HMODELANIM CLTServer::GetAnimIndex(HOBJECT hObj, char *pAnimName)
{
	return ci_GetAnimIndex(hObj, pAnimName);
}

void CLTServer::CPrint(char *pMsg, ...)
{
	char msg[500];
	va_list marker;

	va_start(marker, pMsg);
	vsnprintf(msg, sizeof(msg), pMsg, marker);
	va_end(marker);
	dsi_ServerError(msg);
}

uint32 CLTServer::GetPointContainers(LTVector *pPoint, HOBJECT *pList, uint32 maxListSize)
{
    if (!g_pServerMgr || !pPoint || (!pList && maxListSize)) return 0;
	PointContainerGather gather;

	gather.m_nElements = 0;
	gather.m_pList = pList;
	gather.m_MaxListSize = maxListSize;
	gather.m_Position = *pPoint;

	((IObjectQuery*)&g_pServerMgr->m_Unknown210)->FindPointContainers(
		pPoint, sm_GatherContainersCB, &gather, 0);

	return gather.m_nElements;
}

LTBOOL CLTServer::GetContainerCode(HOBJECT hObj, uint16 *pCode)
{
	// Two separate returns: the compiler tail-merges the identical bodies
	// into the inline block both checks reach (an `||` jumps the other way).
	if (!hObj)
		return LTFALSE;
	if (hObj->m_ObjectType != OT_CONTAINER)
		return LTFALSE;

	*pCode = ((ContainerInstance*)hObj)->m_ContainerCode;
	return LTTRUE;
}

// *********************************************************************** //
//  0x10035caa — str_FormatMessage, the body behind CLTServer::FormatString.
//
//  Two Win32 calls in series: LoadString pulls the format out of the class
//  module's string table, FormatMessage expands the caller's arguments into
//  a buffer it allocates itself (FORMAT_MESSAGE_ALLOCATE_BUFFER, which is
//  why lpBuffer is the *address* of the pointer and why the caller has to
//  hand the result to str_FreeMessage).
//
//  The first parameter is not the HINSTANCE: the shipped code loads it and
//  then dereferences it (`mov 0x8(%ebp),%eax` / `push (%eax)`), so
//  CClassMgr::m_hModule is a pointer to a module record whose first field is
//  the instance handle. FormatString pushes the field's value, so the extra
//  indirection is here and not at the call site.
//
//  `*pLen` is FormatMessage's own return scaled by three plus one — the
//  shipped `lea 0x1(%eax,%eax,2)`. Nothing in this function reads it back.
//
//  The two failure arms are one tail-merged block: the second pushes its
//  literal and jumps backwards into the first's call. That direction comes
//  from the single-return shape below -- one result pointer set on every
//  path, the FormatMessage call inside the LoadString guard's `else`. With
//  two early `return LTNULL`s cl 8447 kept the second site's tail and jumped
//  the first forward (143/143, two blocks); the nested form with early
//  returns and a `goto` were both worse. The literal order is unchanged by
//  the single-return shape, and it is a check on any other: the LoadString
//  guard has to come first to put the two literals into .data in the shipped order
//  (VC6 emits them in reverse of their .text use, so "FormatMessage error on
//  string %d" at 0x10055298 before "Couldn't get string %d" at 0x100552bc).
// *********************************************************************** //

// Both Win32 entry points come in through windows.h, which lthread.h (and so
// servermgr.h) already pulls into this TU; the A-suffixed names are spelled
// out so the header's UNICODE macros cannot pick the other one. Both are
// dllimport there, which is the `call *[__imp__...]` the shipped code makes.

// 0x10058500 — the console variable "DebugStrings". Its name and its slot
// are in the console-variable record at 0x10052034, whose value pointer is
// this address; nothing else in the image reads it. Integer, not float: the
// shipped test is `cmp %edi,0x10058500` against a zeroed register.
extern int g_CV_DebugStrings;

void* str_FormatMessage(const nolf::ResourceModule* module, int code, va_list* arguments, uint32* length) {
    if (!length) return nullptr;
    *length = 0;
    if (!module) return nullptr;
    try {
        const auto text = nolf::format_resource(*module, uint32(code), arguments);
        auto* result = static_cast<char*>(dalloc(uint32(text.size() + 1)));
        if (!result) return nullptr;
        std::memcpy(result, text.c_str(), text.size() + 1);
        *length = uint32(text.size() + 1);
        return result;
    } catch (const std::exception& error) {
        dsi_ServerError("Resource %d: %s", code, error.what());
        return nullptr;
    }
}
void str_FreeMessage(void* text) { dfree(text); }


HSTRING CLTServer::FormatString(int messageCode, ...)
{
	va_list marker;
	uint32 nLen;

	if (m_pServerMgr->m_ClassMgr.m_Resources.resources)
	{
		va_start(marker, messageCode);
		void* pRaw = str_FormatMessage(&m_pServerMgr->m_ClassMgr.m_Resources,
		                               messageCode, &marker, &nLen);
		va_end(marker);

		if (pRaw)
		{
			// servermsg.h's declaration, not a local one: this file used
			// to carry a second `void*` overload of the same address,
			// which a call-target check reports as a merge and a whole-DLL link
			// would reject as an unresolved external.
			HSTRING hString = sb_CreateStringRaw((char*)pRaw);
			str_FreeMessage(pRaw);
			return hString;
		}
	}

	return LTNULL;
}

// The buffered name reader (0x1002f1b3) and two ILTCommon forwards.

LTRESULT CLTServer::GetObjectName(HOBJECT hObject, char *pName, uint32 nameBufSize)
{
	if (hObject && pName && nameBufSize > 0)
	{
		void* hIdent = hObject->m_pSData->m_hIdentifier;
		if (hIdent)
		{
			// The temp keeps the length computing after the call, so the two
			// cdecl cleanups batch into one add esp.
			char* pString = dhash_GetKey(hIdent, 0);
			strncpy(pName, pString, nameBufSize - 1);
			pName[nameBufSize - 1] = 0;
		}
		else
		{
			pName[0] = 0;
		}

		return LT_OK;
	}

	RETURN_ERROR_NAMED(2, "ILTPhysics::GetObjectName", LT_INVALIDPARAMS);
}

LTRESULT CLTServer::GetPolyTextureFlags(HPOLY hPoly, uint32 *pFlags)
{
	return m_pCommonLT->GetPolyTextureFlags(hPoly, pFlags);
}

LTRESULT CLTServer::GetModelAnimUserDims(HOBJECT hObject, LTVector *pDims, HMODELANIM hAnim)
{
	return m_pCommonLT->GetModelAnimUserDims(hObject, pDims, hAnim);
}

// ---- the object finders (0x1002e349-0x1002e4c1) -------------------------- //

// An HPOLY is (world-model index << 16) | poly index; the index selects the
// record in the 0x330 table, and the object whose m_pWorldBsp is that
// record's +0x04 pointer is the world model it came from.
LTRESULT CLTServer::GetHPolyObject(const HPOLY hPoly, HOBJECT &hObject)
{
	static char* pErrFnName = "CLTServer::GetHPolyObject";
	LTLink* pListHead;
	LTLink* pLink;
	LTObject* pObj;
	CFileTree* pWorld;

	uint32 iWorld = hPoly >> 16;
	CServerMgr* pMgr = m_pServerMgr;

	// Two separate identical not-found sites: the compiler tail-merges them
	// into the one inline error block both checks reach (an `||` writes the
	// same test pair but sinks the merged block past the walk).
	if (iWorld >= pMgr->m_nFileTrees)
		RETURN_ERROR(5, "CLTServer::GetHPolyObject", LT_NOTFOUND);

	pWorld = pMgr->m_pFileTrees[iWorld];
	if (!pWorld)
		RETURN_ERROR(5, "CLTServer::GetHPolyObject", LT_NOTFOUND);

	// pListHead first: the lea binds before the cursor load (sm_UpdateClients).
	pListHead = pMgr->WorldModelListHead();
	for (pLink = pListHead->m_pNext; pLink != pListHead; pLink = pLink->m_pNext)
	{
		pObj = (LTObject*)pLink->m_pData;
		if (pObj->m_pWorldBsp == (IWorldBsp*)pWorld->m_pWorldBsp)
		{
			hObject = pObj;
			return LT_OK;
		}
	}

	RETURN_ERROR(2, pErrFnName, LT_INVALIDPARAMS);
}

LTRESULT CLTServer::FindNamedObjects(char *pName, BaseObjArray<HOBJECT> &objArray, uint32 *nTotalFound)
{
	uint32 nTotal;
	uint32 nLen;
	void* hElement;

	nTotal = 0;
	if (!pName)
		RETURN_ERROR_NAMED(2, "CLTServer::FindNamedObjects", LT_INVALIDPARAMS);

	objArray.Reset();
	nLen = strlen(pName) + 1;

	hElement = dhash_FindElement(g_pServerMgr->m_hHash8c0, pName, nLen);
	while (hElement)
	{
		LTObject* pObj = (LTObject*)dhash_GetData(hElement);

		// Objects carrying change-flag 1 are invisible to the finder, but
		// everything else counts toward the total even once the array fills.
		if (!(pObj->m_ChangeFlags & 1))
		{
			objArray.AddObject((HOBJECT)pObj);
			nTotal++;
		}

		hElement = dhash_FindNextElement(g_pServerMgr->m_hHash8c0, hElement, pName, nLen);
	}

	if (nTotalFound)
		*nTotalFound = nTotal;

	return LT_OK;
}

// Move the world model to the candidate transform, gather everything its
// bounding sphere touches, test each candidate's box against the transformed
// BSP, then put the world model back where it was.
LTRESULT CLTServer::FindWorldModelObjectIntersections(HOBJECT hWorldModel, LTVector vNewPos,
                                                      LTRotation rNewRot, BaseObjArray<HOBJECT> &objArray)
{
	static char* pErrFnName = "CLTServer::FindObjectIntersections";
	LTVector vOldPos;
	LTRotation rOldRot;
	ObjectList* pList;
	ObjectLink* pLink;
	float       fRadius;

	if (!hWorldModel)
		RETURN_ERROR_NAMED(2, "CLTServer::FindObjectIntersections", LT_INVALIDPARAMS);

	if (hWorldModel->m_ObjectType != OT_WORLDMODEL && hWorldModel->m_ObjectType != OT_CONTAINER)
		RETURN_ERROR(1, "ILTPhysics::FindWorldModelObjectIntersections", LT_ERROR);

	vOldPos = hWorldModel->m_Pos;
	rOldRot = hWorldModel->m_Rotation;

	hWorldModel->SetPos(vNewPos);
	hWorldModel->m_Rotation = rNewRot;
	obj_RetransformWorldModel(hWorldModel);

	// The radius goes through a named local: the shipped /O2 body stores it
	// to the frame (`mov %edx,0x34(%esp)`, into the slot pList takes two
	// instructions later) and pushes a register copy, which is the local's
	// dead initialisation surviving into a slot the optimiser then reuses.
	// Those two instructions are the whole 544-vs-538 difference.
	fRadius = hWorldModel->m_Radius;
	pList = sm_FindObjectsTouchingSphere(&vNewPos, fRadius);
	if (pList)
	{
		for (pLink = pList->m_pFirstLink; pLink; pLink = pLink->m_pNext)
		{
			LTObject* pObj = (LTObject*)pLink->m_hObject;
			if (!pObj || pObj == hWorldModel)
				continue;

			if (ws_GetBoxStatus(hWorldModel->m_pWMIface->GetRoot(),
			                     &pObj->m_BBoxMin, &pObj->m_BBoxMax)
				&& !(pObj->m_ChangeFlags & 1))
			{
				objArray.AddObject(pLink->m_hObject);
			}
		}

		sm_RelinquishList(pList);
	}

	hWorldModel->SetPos(vOldPos);
	hWorldModel->m_Rotation = rOldRot;
	obj_RetransformWorldModel(hWorldModel);

	return LT_OK;
}

LTRESULT CLTServer::GetWorldBox(LTVector &min, LTVector &max)
{
	if (!m_pServerMgr->m_pWorldFile)
		RETURN_ERROR(2, "ILTPhysics::GetWorldBox", LT_NOTINITIALIZED);

	min = m_pServerMgr->m_WorldBoxMin;
	max = m_pServerMgr->m_WorldBoxMax;
	return LT_OK;
}

// ---- the threaded file loader (0x1002e6e6-0x1002e8a0) -------------------- //

LTRESULT CLTServer::ThreadLoadFile(char *pFilename, uint32 type)
{
	if (!pFilename)
		RETURN_ERROR(2, "ILTPhysics::ThreadLoadFile", LT_INVALIDPARAMS);

	if (type == FT_MODEL)
		return ThreadLoadModel(pFilename);
	else if (type == FT_TEXTURE)
		return ThreadLoadTexture(pFilename);

	RETURN_ERROR(1, "ILTPhysics::ThreadLoadFile", LT_UNSUPPORTED);
}

LTRESULT CLTServer::UnloadFile(char *pFilename, uint32 type)
{
	if (!pFilename)
		RETURN_ERROR(2, "ILTPhysics::UnloadFile", LT_INVALIDPARAMS);

	if (type == FT_MODEL)
		return UnloadModel(pFilename);
	else if (type == FT_TEXTURE)
		return UnloadTexture(pFilename);

	RETURN_ERROR(1, "ILTPhysics::UnloadFile", LT_UNSUPPORTED);
}

LTRESULT CLTServer::OpenFile(char *pFilename, ILTStream **pStream)
{
	// One name variable, two macros: the parameter check reports the code
	// symbolically, the missing-file site reports it expanded ("22").
	static char* pErrFnName = "ILTPhysics::OpenFile";

	if (!pFilename || !pStream)
		RETURN_ERROR_SYM(2, pErrFnName, LT_INVALIDPARAMS);

	*pStream = fs_OpenFile(&m_pServerMgr->m_FileMgr, pFilename);
	if (*pStream)
		return LT_OK;

	RETURN_ERROR(2, pErrFnName, LT_MISSINGFILE);
}

// The four that hand back a raw HMESSAGEWRITE report a failure by returning
// LTNULL, and pass that same LTNULL to the error macro as the "code" — which
// is why the shipped data has a string "0" alongside the usual "60" and "73".

HMESSAGEWRITE CLTServer::StartMessageToObject(LPBASECLASS pSender, HOBJECT hSendTo, uint32 messageID)
{
	if (!hSendTo)
		RETURN_ERROR(2, "ILTPhysics::StartMessageToObject", LTNULL);

	LMessageImpl* pWrite = &m_pServerMgr->StartMessage()->m_Write;

	pWrite->m_Type = MT_TOOBJECT;
	pWrite->m_hSender = pSender ? pSender->m_hObject : LTNULL;
	pWrite->m_hSendTo = hSendTo;
	pWrite->m_MessageID = messageID;

	return (HMESSAGEWRITE)pWrite;
}

LTRESULT CLTServer::StartMessageToServer(LPBASECLASS pSender, uint32 messageID, HMESSAGEWRITE* hWrite)
{
	if (!pSender || !hWrite)
		RETURN_ERROR(2, "ILTPhysics::StartMessageToServer", LT_INVALIDPARAMS);

	LMessageImpl* pWrite = &m_pServerMgr->StartMessage()->m_Write;

	pWrite->m_Type = MT_TOSERVER;
	pWrite->m_MessageID = messageID;
	pWrite->m_hSender = pSender->m_hObject;

	*hWrite = (HMESSAGEWRITE)pWrite;
	return LT_OK;
}

HMESSAGEWRITE CLTServer::StartMessage(HCLIENT hSendTo, uint8 messageID)
{
	LMessageImpl* pWrite = &m_pServerMgr->StartMessage()->m_Write;

	// m_Type first: lithtech.exe (0x46f12c) stores it first; server.dll's
	// /O1 build moves the constant store below m_hClient either way.
	pWrite->m_Type = MT_TOCLIENT;
	pWrite->m_hClient = hSendTo;
	pWrite->m_MessageID = messageID;

	return (HMESSAGEWRITE)pWrite;
}

HMESSAGEWRITE CLTServer::StartInstantSpecialEffectMessage(LTVector* pPos)
{
	if (!pPos)
		RETURN_ERROR(2, "ILTPhysics::StartInstantSpecialEffectMessage", LTNULL);

	LMessageImpl* pWrite = &m_pServerMgr->StartMessage()->m_Write;

	pWrite->m_Type = MT_INSTANTSFX;
	pWrite->m_vPos = *pPos;

	return (HMESSAGEWRITE)pWrite;
}

HMESSAGEWRITE CLTServer::StartSpecialEffectMessage(LPBASECLASS pObject)
{
	if (!pObject)
		RETURN_ERROR(2, "ILTPhysics::StartSpecialEffectMessage", LTNULL);

	LMessageImpl* pWrite = &m_pServerMgr->StartMessage()->m_Write;

	pWrite->m_Type = MT_SFX;
	pWrite->m_hSendTo = pObject->m_hObject;

	return (HMESSAGEWRITE)pWrite;
}

// 0x1002f416 — ILTCSBase::StartHMessageWrite, the sixth message starter and
// the only one servermsg.h's "starters (0x1002f25f-0x1002f415)" block does
// not cover: it sits immediately after them and immediately before
// EndMessage2. It is the SDK's `virtual HMESSAGEWRITE StartHMessageWrite()`
// (iltcsbase.h), and it is the only starter that sizes the packet itself —
// 0x2000 bytes with a 0x1000 grow step, where the five above take whatever
// CServerMgr::StartMessage hands them.
//
// Read off the bytes: no null check on the manager's message (the shipped
// code goes straight from the call into `Init`), and the return is
// `lea 0x8(%esi),%eax` — the embedded LMessageImpl, exactly as the other
// five return it.
//
// servermsg.h's CLTServer does not declare this method and that header is
// not this reconstruction's to change, so it is written against a view of
// the same layout; the padding to 0x224 is servermsg.h's own offset for
// m_pServerMgr. WHAT SHOULD REPLACE THIS: one line in servermsg.h's
// CLTServer, `HMESSAGEWRITE StartHMessageWrite();`, next to the other
// starters, and this view can go.
HMESSAGEWRITE CLTServer::StartHMessageWrite() {
    ServerMessage* msg = m_pServerMgr->StartMessage();
    if (!msg) return nullptr;
    auto* io = msg->m_Write.m_pObjectIO;
    msg->Init(0x2000, 0x1000);
    msg->m_Write.m_pObjectIO = io;
    return &msg->m_Write;
}

// ILTServer's global-force accessors. Same names as the ILTPhysics pair in
// commonphysics.cpp, but this header takes pointers, so these validate and
// forward — and the vtable slots they call (0x3c/0x40) are exactly where
// SPhysicsLT's own force methods sit, which is how m_pPhysicsLT was pinned.

LTRESULT CLTServer::GetGlobalForce(LTVector* pVec)
{
	if (!pVec)
		RETURN_ERROR(2, "ILTPhysics::GetGlobalForce", LT_INVALIDPARAMS);

	return m_pPhysicsLT->GetGlobalForce(*pVec);
}

LTRESULT CLTServer::SetGlobalForce(LTVector* pVec)
{
	if (!pVec)
		RETURN_ERROR(2, "ILTPhysics::SetGlobalForce", LT_INVALIDPARAMS);

	return m_pPhysicsLT->SetGlobalForce(*pVec);
}


// ---- the old flat API ---------------------------------------------------- //
// ILTServer predates the split interfaces, so CLTServer keeps every original
// method as a wrapper over whichever interface superseded it.

LTRESULT CLTServer::SetFrictionCoefficient(HOBJECT hObj, float coeff)
{
	return m_pPhysicsLT->SetFrictionCoefficient(hObj, coeff);
}

// The out-parameter is faithful to the header and faithfully ignored: the
// shipped code fills a zeroed local and returns that instead.
float CLTServer::GetForceIgnoreLimit(HOBJECT hObj, float &limit)
{
	float val = 0.0f;
	m_pPhysicsLT->GetForceIgnoreLimit(hObj, val);
	return val;
}

void CLTServer::SetForceIgnoreLimit(HOBJECT hObj, float limit)
{
	m_pPhysicsLT->SetForceIgnoreLimit(hObj, limit);
}

LTRESULT CLTServer::GetVelocity(HOBJECT hObj, LTVector *pVel)
{
	return m_pPhysicsLT->GetVelocity(hObj, pVel);
}

LTRESULT CLTServer::SetVelocity(HOBJECT hObj, LTVector *pVel)
{
	return m_pPhysicsLT->SetVelocity(hObj, pVel);
}

LTRESULT CLTServer::GetAcceleration(HOBJECT hObj, LTVector *pAccel)
{
	return m_pPhysicsLT->GetAcceleration(hObj, pAccel);
}

LTRESULT CLTServer::SetAcceleration(HOBJECT hObj, LTVector *pAccel)
{
	return m_pPhysicsLT->SetAcceleration(hObj, pAccel);
}

float CLTServer::GetObjectMass(HOBJECT hObj)
{
	float mass;

	m_pPhysicsLT->GetObjectMass(hObj, mass);
	return mass;
}

void CLTServer::SetObjectMass(HOBJECT hObj, float mass)
{
	m_pPhysicsLT->SetObjectMass(hObj, mass);
}

void CLTServer::GetObjectDims(HOBJECT hObj, LTVector *pNewDims)
{
	m_pPhysicsLT->GetObjectDims(hObj, pNewDims);
}

LTRESULT CLTServer::SetObjectDims(HOBJECT hObj, LTVector *pNewDims)
{
	return m_pPhysicsLT->SetObjectDims(hObj, pNewDims, 0);
}

LTRESULT CLTServer::SetObjectDims2(HOBJECT hObj, LTVector *pNewDims)
{
	return m_pPhysicsLT->SetObjectDims(hObj, pNewDims, SETDIMS_PUSHOBJECTS);
}

LTRESULT CLTServer::MoveObject(HOBJECT hObj, LTVector *pNewPos)
{
	return m_pPhysicsLT->MoveObject(hObj, pNewPos, 0);
}

LTRESULT CLTServer::GetStandingOn(HOBJECT hObj, CollisionInfo *pInfo)
{
	return m_pPhysicsLT->GetStandingOn(hObj, pInfo);
}

uint32 CLTServer::GetObjectFlags(HOBJECT hObj)
{
	uint32 flags = 0;

	m_pCommonLT->GetObjectFlags(hObj, OFT_Flags, flags);
	return flags;
}

void CLTServer::SetObjectFlags(HOBJECT hObj, uint32 flags)
{
	m_pCommonLT->SetObjectFlags(hObj, OFT_Flags, flags);
}

char* CLTServer::GetObjectName(HOBJECT hObject)
{
	if (hObject && hObject->m_pSData->m_hIdentifier)
		return dhash_GetKey(hObject->m_pSData->m_hIdentifier, 0);

	return g_EmptyString;
}

// ---- the senders (0x1002f43a-0x1002f8b0) ---------------------------------- //

// Case order is the lithtech.exe block order: /O2 lays the cases out in
// SOURCE order, and 0x46f320 has TOCLIENT, TOOBJECT, SFX, INSTANTSFX,
// TOSERVER. server.dll's /O1 layout does not follow the source (it comes out
// TOSERVER, INSTANTSFX, SFX, TOCLIENT, TOOBJECT either way), but its tail
// merging does: with TOSERVER last, a `return LT_OK` inside that case gets
// cross-jumped into INSTANTSFX's dResult tail (`xor %edi,%edi`, 15 bytes
// short). TOSERVER `break`s to one `return LT_OK` after the switch instead,
// which keeps its own `xor %eax,%eax` tail at 0x1002f4c9 as shipped.
LTRESULT CLTServer::EndMessage2(HMESSAGEWRITE hMessage, uint32 flags)
{
	LTRESULT dResult;
	LMessageImpl* pMsg = (LMessageImpl*)hMessage;

	if (!hMessage || pMsg->Verify() != LT_OK)
		RETURN_ERROR_NAMED(2, "CLTServer::EndMessage2", LT_INVALIDPARAMS);

	switch (pMsg->m_Type)
	{
		default:
			RETURN_ERROR(1, "EndMessage", LT_ERROR);

		case MT_TOCLIENT:
			dResult = this->SendToClient(*(IMessageWrite*)hMessage,
				(uint8)pMsg->m_MessageID, pMsg->m_hClient, flags);
			hMessage->Release();
			return dResult;

		case MT_TOOBJECT:
			dResult = this->SendToObject(*(IMessageWrite*)hMessage,
				pMsg->m_MessageID, pMsg->m_hSender, pMsg->m_hSendTo, flags);
			hMessage->Release();
			return dResult;

		case MT_SFX:
			dResult = this->SetObjectSFXMessage(pMsg->m_hSendTo,
				*(IMessageWrite*)hMessage);
			hMessage->Release();
			return dResult;

		case MT_INSTANTSFX:
			this->SendSFXMessage(*(IMessageWrite*)hMessage,
				pMsg->m_vPos, flags);
			dResult = LT_OK;
			hMessage->Release();
			return dResult;

		case MT_TOSERVER:
			this->SendToServer(*(IMessageWrite*)hMessage,
				pMsg->m_MessageID, pMsg->m_hSender, flags);
			hMessage->Release();
			break;
	}

	return LT_OK;
}

LTRESULT CLTServer::EndMessage(HMESSAGEWRITE hMessage)
{
	return EndMessage2(hMessage, MESSAGE_GUARANTEED);
}

LTRESULT CLTServer::SendToClient(ILTMessage& message, uint8 id, HCLIENT target, uint32 flags) {
    if (!m_pServerMgr) return LT_NOTINITIALIZED;
    auto* client = target ? client_Find(m_pServerMgr->m_ClientList,target) : nullptr;
    if (target && !client) return LT_INVALIDPARAMS;
    auto* engine_message = dynamic_cast<LMessageImpl*>(&message);
    if (!engine_message || engine_message->Verify() != LT_OK) return LT_INVALIDPARAMS;
    auto* packet = engine_message->m_pOwner;
    packet->WriteByte(id);
    if (packet->m_Flags & PACKETFLAG_WRITEOVERFLOW) return LT_ERROR;
    if (client) return sm_QueueMessageToClient(m_pServerMgr,client,0xd,packet,flags);
    auto* head = &m_pServerMgr->m_ClientList.m_Head;
    for (auto* link = head->m_pNext; link != head; link = link->m_pNext) {
        auto* destination = static_cast<Client*>(link->m_pData);
        const auto result = sm_QueueMessageToClient(m_pServerMgr,destination,0xd,packet,flags);
        if (result != LT_OK) return result;
    }
    return LT_OK;
}

LTRESULT CLTServer::SendToObject(ILTMessage &msg, uint32 msgID, HOBJECT hSender,
                                 HOBJECT hSendTo, uint32 flags)
{
	LMessageImpl* pMsg = (LMessageImpl*)&msg;

	if (!hSendTo || pMsg->Verify() != LT_OK)
		RETURN_ERROR(2, "ILTPhysics::SendToObject", LT_INVALIDPARAMS);

	pMsg->m_pOwner->m_Unknown38 = 1;
	pMsg->m_MessageID = msgID;

	m_pServerMgr->m_ClassMgr.object_message(hSendTo->m_pSData->m_pBaseClass,hSender,msgID,(HMESSAGEREAD)&msg);
	return LT_OK;
}

LTRESULT CLTServer::SetObjectSFXMessage(HOBJECT hObject, ILTMessage &msg)
{
	LMessageImpl* pMsg = (LMessageImpl*)&msg;

	if (pMsg->Verify() != LT_OK)
		RETURN_ERROR_NAMED(2, "CLTServer::SetObjectSFXMessage", LT_INVALIDPARAMS);

	sm_SetObjectSFXMessage(m_pServerMgr, hObject, pMsg->m_pOwner);
	return LT_OK;
}

LTRESULT CLTServer::SendSFXMessage(ILTMessage &msg, LTVector &pos, uint32 flags)
{
	LMessageImpl* pMsg = (LMessageImpl*)&msg;

	if (pMsg->Verify() != LT_OK)
		RETURN_ERROR_NAMED(2, "CLTServer::SendSFXMessage", LT_INVALIDPARAMS);

	sm_SendSFXMessage(m_pServerMgr, 0x12, pMsg->m_pOwner, LTNULL, &pos, flags);
	return LT_OK;
}

LTRESULT CLTServer::SendToServer(ILTMessage &msg, uint32 msgID, HOBJECT hSender, uint32 flags)
{
	LMessageImpl* pMsg = (LMessageImpl*)&msg;

	if (pMsg->Verify() != LT_OK)
		RETURN_ERROR_NAMED(2, "CLTServer::SendToServer", LT_INVALIDPARAMS);

	// Success first; the not-initialized error owns the lexically-last return.
	IServerShell* pShell = g_pServerMgr->m_ClassMgr.m_pServerShell;
	if (pShell)
	{
		// The message's own routing fields, not the parameters, feed the shell.
		pMsg->m_pOwner->m_Unknown38 = 1;
		g_pServerMgr->m_ClassMgr.shell([&](IServerShell& shell) { return shell.OnObjectMessage(pMsg->m_hSender->m_pSData->m_pBaseClass,
			pMsg->m_MessageID, (HMESSAGEREAD)&msg); });
		return LT_OK;
	}

	RETURN_ERROR(2, "ILTPhysics::SendToServer", LT_NOTINITIALIZED);
}

// ---- strings, console variables, time ------------------------------------ //

HSTRING CLTServer::CopyString(HSTRING hString)
{
	return sb_CopyString(hString);
}

HSTRING CLTServer::CreateString(char *pString)
{
	return sb_CreateString(pString);
}

void CLTServer::FreeString(HSTRING hString)
{
	sb_FreeString(hString);
}

LTBOOL CLTServer::CompareStrings(HSTRING hString1, HSTRING hString2)
{
	return sb_CompareStrings(hString1, hString2);
}

LTBOOL CLTServer::CompareStringsUpper(HSTRING hString1, HSTRING hString2)
{
	return sb_CompareStringsUpper(hString1, hString2);
}

char* CLTServer::GetStringData(HSTRING hString)
{
	return sb_GetStringData(hString);
}

float CLTServer::GetVarValueFloat(HCONSOLEVAR hVar)
{
	if (!hVar)
		return 0.0f;

	return ((LTCommandVar*)hVar)->m_FloatVal;
}

char* CLTServer::GetVarValueString(HCONSOLEVAR hVar)
{
	if (!hVar)
		return LTNULL;

	return ((LTCommandVar*)hVar)->m_pStringVal;
}

float CLTServer::GetTime()
{
	return g_pServerMgr->m_Time;
}

float CLTServer::GetFrameTime()
{
	return g_pServerMgr->m_FrameTime;
}

LTRESULT CLTServer::RemoveObject(HOBJECT hObj)
{
	if (!hObj)
		return LT_ERROR;

	sm_RemoveObject(g_pServerMgr, hObj);
	return LT_OK;
}

LTRESULT CLTServer::FreeUnusedModels()
{
	return m_pServerMgr->FreeUnusedModels();
}

// The interface's own copy of the driver scan — same loop as
// MyServerInterface::SendTo, but here the index is a uint16. m_pServerMgr is
// read at each use, not through a local: lithtech.exe (0x46ea70) then loads
// it back into %ecx and gives the counter %edx; a local took %edx.
LTRESULT CLTServer::SendTo(const void *pData, uint32 len, const char *sAddr, uint32 port)
{
	for (uint16 i = 0; i < m_pServerMgr->m_NetMgr.m_Drivers.m_nElements; i++)
	{
		CBaseDriver* pDriver = m_pServerMgr->m_NetMgr.m_Drivers.m_pArray[i];
		if (pDriver->m_DriverFlags & NETDRIVER_TCPIP)
			return pDriver->SendTo(pData, len, sAddr, port);
	}

	return LT_NOTINITIALIZED;
}


// ---- the counted references --------------------------------------------- //
//
// The cursor takes a reference on what arrives and drops the one it was
// holding, in that order, so assigning a reference to itself is safe, and
// hands back what it now holds. (CMessageRef::Init is the same body, hoisted
// to the head of this file so /O2 can expand it — see the note there.)

// 0x10023e86 — the cursor takes the message directly.
// 0x1003506e — put one object's identifier on a message, or 0xffff for none.
// Slot 0x04 of the helper's vftable; it never touches `this`.
//
// WHICH identifier depends on the message: Verify() answering 1 selects
// m_ObjectID2 at +0xba and anything else selects m_ObjectID at +0xb8, so the
// two fields are alternative ids for the same object and the message knows
// which side it is bound for. That is the only thing in the reconstruction
// that reaches +0xba, and it is what named it.
//
// One detail is forced. Each arm tests pObject POSITIVELY -- the shared 0xffff write is
// hosted after both arms, and writing the arms as `if (!pObject) ... else ...`
// hosts it at the first mention instead, two bytes short with the first branch
// inverted. Same fingerprint as the guarded-return run in 09-codegen-patterns:
// the shared block lands wherever the source first needs it.
//
// The null test really is inside both arms; VC6 merges the two 0xffff writes
// into one block, so there are three call sites in the source and two in the
// bytes (name ours).
LTRESULT CMessageObjectHelper::WriteObject(LMessageImpl* message, HOBJECT object) {
    return message->WriteWord(object ? object->m_ObjectID : uint16(0xffff));
}
LTRESULT CMessageObjectHelper::ReadObject(LMessageImpl* message, HOBJECT* object) {
    if (!object) return LT_INVALIDPARAMS;
    *object = nullptr;
    uint16 id = 0xffff;
    const auto status = message->ReadWordFL(id);
    if (status != LT_OK) return status;
    if (id == 0xffff || id >= m_pServerMgr->m_Unknown8fc.m_nElements) return LT_OK;
    auto& entry = m_pServerMgr->m_Unknown8fc.m_pArray[id];
    if (entry.m_nFlags == 1) *object = static_cast<LTObject*>(entry.m_pRecord);
    return LT_OK;
}
void CMessageObjectHelper::GetPosRef(CPosRef** reference) {
    if (!reference) return;
    *reference = nullptr;
    if (!m_pServerMgr->m_pWorldFile) return;
    *reference = m_pServerMgr->m_World.PositionReference();
}
LTRESULT CMessageObjectHelper::WriteFormattedHString(LMessageImpl* message, int code, va_list* arguments) {
    try {
        auto text = nolf::format_resource(m_pServerMgr->m_ClassMgr.m_Resources, uint32(code), arguments);
        return message->WriteStringAsHString(text.data());
    } catch (const std::exception& error) {
        dsi_ServerError("Resource %d: %s", code, error.what());
        return message->WriteWord(0);
    }
}
CMessageObjectHelper::CMessageObjectHelper(CServerMgr* manager) : m_pServerMgr(manager) {}
