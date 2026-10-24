#include "servermsg.h"
#include "servermgr.h"
#include "console.h"
#include "de_error.h"
#include "engine/portable/string.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>

// ---- the server's console ------------------------------------------------ //

// 0x10028f52 — the server's own console. Everything the console reaches the
// outside world through is filled in here, which is what lets the same
// console code serve a server and a client: the printer, the allocator, and
// the two notifications the server hangs off variable creation and change.
//
// The common console owns its records; reinitialization releases them before
// installing the server callbacks and built-in command table.
void cc_InitState(CServerMgr* pMgr, ConsoleState* pState)
{
	con_Term(pState);

	pState->m_pSavers = LTNULL;
	pState->m_nSavers = 0;

	pState->m_pBindings = dsi_GetConsoleBindings();
	pState->m_nBindings = dsi_GetNumConsoleBindings();

	pState->m_pBuiltins = g_ConsoleCommands;
	pState->m_nBuiltins = 8;

	pState->m_pPrint = dsi_ServerError;

	pState->m_pOnCreate = cc_OnVarCreated;
	pState->m_pOnChange = cc_OnVarChanged;

	pState->m_pServerMgr = pMgr;

	con_Init(pState);
}

void cc_TermState(ConsoleState* pState)
{
	con_Term(pState);
}


// ---- the eight built-in console commands -------------------------------- //
//
// servermgr.h declares 0x10034521 as `void sm_BroadcastPrint(char* pMsg)`
// with one fixed argument; it is variadic, and smgr.cpp already carries the
// corrected declaration under the name below.
void sm_BroadcastPrintf(char* pFormat, ...);
//
// The names are not ours: g_ConsoleCommands at 0x10053d80 is eight
// {name, fn, user} triples and reading it names every handler outright --
// "Portal" 0x10028ad9, "ShowGameVars" 0x10028b47, "ShowUsedFiles" 0x10028b00,
// "world" 0x10028b9d, "objectinfo" 0x10028be3, "DisableWMPhysics" 0x10028d7e,
// "ExhaustMemory" 0x10036d21 and "SpawnObject" 0x10028da3. Every handler is
// cdecl (int argc, char** argv).
//
// "ExhaustMemory" is worth a note: its entry points at 0x10036d21, the image's
// one shared empty function, so the command parses and does nothing. It is a
// debug command that was compiled out of the release build without being
// taken off the table.

// 0x10028ad9 — "Portal <name> <0|1>": open or close one of the world's
// portals (lithtech twin 0x00466870). The guard is `argc < 2` with the whole
// body under it; both images test the count before touching argv and there
// is no usage line. The name is argv[0] and the flag argv[1] -- argv does not
// carry the command name, the same convention cc_World is written to.
void cc_Portal(int argc, char** argv)
{
	if (argc < 2)
		return;

	sm_SetPortalFlags(g_pServerMgr, argv[0], atoi(argv[1]));
}

// 0x10028b00 — "ShowUsedFiles": every filename the file manager has a record
// for, broadcast one line each. The names come back as the HASH KEYS rather
// than out of the records, which is what dhash_GetKey is for.
void cc_ShowUsedFiles(int, char**) {
    if (!g_pServerMgr) return;
    for (const auto* file : g_pServerMgr->m_FileMgr.files())
        sm_BroadcastPrintf("Used file: %s", file->m_Name.c_str());
}

// 0x10028b47 — "ShowGameVars": a banner, then every console variable through
// con_PrintVar. The list comes off the console state's own name hash, and the
// records out of the elements rather than the keys -- the mirror of
// ShowUsedFiles above, which wants the keys and not the records.
//
// The banner goes through dsi_ServerError, not the console's own m_pPrint:
// the whole point of these commands is that their output reaches the host
// application's log.
//
// lithtech.exe carries this body (0x004668f0) AND the client console's own
// cc_ShowGameVars (clientconsole.cpp, 0x0041e840), each reached only through
// its own TU's command table -- so in the client build this one is file-local
// (static), which is what lets both link.  Likewise cc_World below
// (0x00466950 against the client's 0x0041d840).  server.dll is unchanged.
#ifndef LT_SERVER_BUILD
static
#endif
void cc_ShowGameVars(int argc, char** argv)
{
	ConsoleState* pState;
	void*         hIter;
	void*         hElement;

	if (!g_pServerMgr)
		return;

	pState = &g_pServerMgr->m_ConsoleState;

	dsi_ServerError("Game vars --------------------");

	hIter = dhash_GetFirst(pState->m_hVarHash);

	if (!hIter)
		return;

	do
	{
		hElement = dhash_GetNext(&hIter);

		if (hElement)
			con_PrintVar(pState, (LTCommandVar*)dhash_GetData(hElement));
	}
	while (hIter);
}

// 0x10028be2 is `ret` -- see below.
//
// 0x10028b9d — "world <name> [mode]": start a world from the console.
//
// argv does NOT carry the command name: DoStartWorld is handed argv[0] as the
// world name, so the parser has already stripped it, and the `argc < 1` guard
// is "was there an argument at all".
//
// The mode defaults to 3 and drops to 2 when a second argument is present and
// atoi()s to zero -- so `world foo 0` and `world foo bar` both mean 2, and
// anything else leaves 3. Reading it the other way round (a nonzero second
// argument selecting 2) is what the `jne` forbids.
//
// The answer goes into a global rather than being returned: the handler is
// void, and 0x100587a8 is where a console-started world's result is left for
// whoever asks later.
#ifndef LT_SERVER_BUILD
static   // see cc_ShowGameVars above
#endif
void cc_World(int argc, char** argv)
{
	uint32 nMode;

	if (argc < 1)
		return;

	nMode = 3;

	if (argc >= 2 && atoi(argv[1]) == 0)
		nMode = 2;

	g_pServerMgr->m_ConsoleState.m_Result =
		g_pServerMgr->DoStartWorld(argv[0], nMode, dsi_GetTime());
}

// 0x10028be3 / 0x004669a0 — "objectinfo": every live object, tallied by
// class name. Moved here out of src/engine/lithtech/ccobj.cpp's holding pen,
// which is a lithtech-only TU: parked there the body could only ever reach
// lithtech.exe's copy, and server.dll's 0x10028be3 had no source at all.
// console.cpp compiles into BOTH images, so this is where the pair lives.
//
// THE lithtech.exe ROW THE FUNCTION ENUMERATION REPORTS (0x004669aa, 370 bytes) IS
// NOT A FUNCTION.  0x004669a0 is `mov $0x7924,%eax ; call 0x00492c60` -- the
// __chkstk probe a >4KB frame needs -- and 0x004669aa is the instruction
// after it.  The enumeration split them because the probe is reached by a
// data reference (the command table) and the body only by fall-through.
// The real extent is 0x004669a0..0x00466b1c, 380 bytes, one `ret`, no
// branch target outside it.  g_ConsoleCommands' "objectinfo" slot is
// 0x10028be3 in server.dll; 0x004ad0d8 in lithtech.exe holds
// { "objectinfo", 0x004669a0, 0 }.

// strncpy (0x00492b60 / 0x1003f050) and strcmp (inlined / 0x1003e1f0) both
// come from <string.h>, included at the top.  VC6 has strcmp on its
// intrinsic list and strncpy NOT, which is exactly the split the shipped /O2
// body shows: the strcmp in the helper below is expanded and the strncpy
// here is still a call.

// The tally reads the class name and object size off the object's SDK class
// definition (the original read +0x00 and +0x20 of its class record).

// The manager's two fields this reads: the live-object list at 0x8dc
// (servermgr.h's m_List8dc) and nothing else.  Written through the global
// rather than a parameter -- a console handler gets only (argc, argv).

#define OBJINFO_MAXCLASSES 500
#define OBJINFO_NAMELEN     50

// 0x10028d47 / 0x00466b20 — which row of the name table holds this class
// name, if any.  A file-static helper: nothing else in either image calls it.
//
// The /O1 image CALLS strcmp (0x1003e1f0) and the /O2 image EXPANDS it --
// the `sbb %eax,%eax ; sbb $0xffffffff,%eax` pair at 0x00466b5c is VC6's
// inline strcmp returning the sign, which is the intrinsic and not a
// hand-written compare.  So the source says `strcmp(...) == 0`.
//
// The table row is 50 bytes: `add $0x32,%edi` in both images.
//
// The answer goes out through a pointer and the return value is a flag --
// `mov %esi,(%eax) ; mov $0x1,%eax` at 0x00466b7a -- not a -1/index return.
static int oi_FindClass(char* pszName, char (*pNames)[OBJINFO_NAMELEN],
                            int nCount, int* pIndex)
{
	int i;

	for (i = 0; i < nCount; i++)
	{
		if (strcmp(pszName, pNames[i]) == 0)
		{
			*pIndex = i;
			return 1;
		}
	}

	return 0;
}

// 0x10028be3 (355 bytes, /O1) / 0x004669a0 (380 bytes, /O2).
//
// Three parallel 500-entry dword tables and a 500x50 name table -- 0x7924
// bytes of frame, which is why the client body opens with __chkstk and the
// server one with 0x1003e740.
//
// THE THREE CLEARS ARE THREE STATEMENTS AND THEIR ORDER IS THE SOURCE'S.
// server.dll makes three `memset(p, 0, 0x7d0)` calls in the order
// counts / active / bytes (0x10028c06, 0x10028c14, 0x10028c22) and
// lithtech.exe emits three `rep stosl` of 0x1f4 dwords in the same order
// (0x004669be, 0x004669cc, 0x004669d7).  The name table is NOT cleared.
// Which table lands where on the stack differs between the images and is
// allocation; the order of the three statements does not.
//
// The count guard is `>= 500` and it is checked ONLY on the miss path
// (0x00466a2a / 0x10028c7a): a class already in the table is still tallied
// after the table is full, and a 501st distinct class is dropped whole --
// the `jge` skips the tally as well as the insert.
//
// The active test is `!(pObject->m_ChangeFlags & 0x38)` -- one byte-wide
// `test $0x38` on LTObject+0x120 in both images.
//
// The per-class line's byte total is accumulated BEFORE the print, not
// after: `add %eax,%ebx` sits at 0x00466ad0 with the loads and ahead of
// every push, and at 0x10028d07 between them; neither image has it after
// the call, which a print-then-accumulate source would force.
//
// BYTE-IDENTICAL IN BOTH IMAGES once three more source facts were measured,
// and each one was worth exactly the bytes it looks worth:
//
//  * THE LIST HEAD IS A NAMED LOCAL. Written straight off the global in both
//    the init and the test (`pCur != &g_pServerMgr->m_List8dc.m_Head`) VC6
//    reloads 0x10058bf0 and recomputes the `lea 0x8e0` on every iteration --
//    it does not CSE a global across the calls in the body -- and the back
//    edge degenerates into `jmp` to a shared top test. With pHead in a local
//    the head sits in a callee-saved register for the whole loop, exactly
//    the shipped ebx. This also moved the three 500-dword arrays onto the
//    shipped frame slots (-0x7e0 counts / -0xfb0 bytes / -0x1780 actives,
//    names at -0x7928) and the frame from 0x7924 to the shipped 0x7928.
//
//  * THERE IS NO pszNext CURSOR: the source indexes szNames[nClasses] and
//    VC6 strength-reduces it. The derived pointer's initialisation is what
//    the shipped code puts in the loop's PREHEADER (`lea -0x7928(%ebp),%eax
//    ; mov %eax,-0x8(%ebp)` at 0x10028c46, below the empty-list `je`), and
//    a preheader is what forces the loop to rotate and carry its own bottom
//    test at 0x10028ce0. Written with an explicit cursor the store lands
//    above the test, the test is shared, and the function is 353 of 356 --
//    the three bytes being `jmp` rel32 against `cmp`+`jne`. The second loop
//    already showed the pattern: its szNames[i] is strength-reduced into edi
//    with the `lea` in its own preheader at 0x10028cf1. Rewriting loop one
//    as a `while` and as a guarded `do`/`while` changes NOTHING -- VC6
//    normalises all three forms to the same blocks; only the preheader does.
//
//  * `nClasses = 0` COMES BEFORE THE THREE memsets. Last three bytes: with
//    it after them both zero stores land together after the third call;
//    ahead of them the store schedules into the first memset's argument
//    run, which is the shipped 0x10028c03. (`nActive = 0` stays after.)
void cc_ObjectInfo(int argc, char** argv)
{
	uint32 nCounts[OBJINFO_MAXCLASSES];
	uint32 nActives[OBJINFO_MAXCLASSES];
	uint32 nBytes[OBJINFO_MAXCLASSES];
	char   szNames[OBJINFO_MAXCLASSES][OBJINFO_NAMELEN];

	LTLink* pHead;
	LTLink* pCur;
	int     nClasses;
	int     nActive;
	int     nTotalBytes;
	int     nIndex;
	int     i;

	nClasses = 0;

	memset(nCounts, 0, sizeof(nCounts));
	memset(nActives, 0, sizeof(nActives));
	memset(nBytes, 0, sizeof(nBytes));

	nActive = 0;

	pHead = &g_pServerMgr->m_List8dc.m_Head;

	for (pCur = pHead->m_pNext; pCur != pHead; pCur = pCur->m_pNext)
	{
		LTObject* pObject = (LTObject*)pCur->m_pData;

		if (!oi_FindClass(
			pObject->m_pSData->m_pClass->m_ClassName,
			szNames, nClasses, &nIndex))
		{
			if (nClasses >= OBJINFO_MAXCLASSES)
				continue;

			nIndex = nClasses;

			strncpy(szNames[nClasses],
				pObject->m_pSData->m_pClass->m_ClassName,
				OBJINFO_NAMELEN - 1);

			nClasses++;
		}

		nCounts[nIndex]++;

		nBytes[nIndex] +=
			pObject->m_pSData->m_pClass->m_ClassObjectSize;

		if (!(pObject->m_ChangeFlags & 0x38))
		{
			nActives[nIndex]++;
			nActive++;
		}
	}

	nTotalBytes = 0;

	for (i = 0; i < nClasses; i++)
	{
		nTotalBytes += nBytes[i];

		dsi_ServerError("%d (%d) instance(s) of '%s' (%d bytes)",
			nCounts[i], nActives[i], szNames[i], nBytes[i]);
	}

	dsi_ServerError("--- Total objects: %d (Active: %d) (Bytes: %d) ---",
		g_pServerMgr->m_List8dc.m_nElements, nActive, nTotalBytes);
}


// 0x10028d7e — "DisableWMPhysics": put every live object on box physics,
// which is what turns world-model collision off (lithtech twin 0x00466b90).
// FLAG_BOXPHYSICS is 1<<14; server.dll ORs the byte at LTObject+0x89 with
// 0x40 and lithtech.exe the whole dword at +0x88 with 0x4000 -- same source,
// /O1 narrowed it. Neither argument is read; the list is the manager's 0x8dc.
void cc_DisableWMPhysics(int argc, char** argv)
{
	LTLink* pHead = &g_pServerMgr->m_List8dc.m_Head;
	LTLink* pCur;

	for (pCur = pHead->m_pNext; pCur != pHead; pCur = pCur->m_pNext)
	{
		LTObject* pObject = (LTObject*)pCur->m_pData;

		pObject->m_Flags |= FLAG_BOXPHYSICS;
	}
}

// The create struct is the SDK's own ObjectCreateStruct (ltbasedefs.h), and
// its in-class Clear() is what the two images disagree about: lithtech.exe
// (/O2) EXPANDS both Clears into cc_SpawnObject (0x00466bdb..0x00466c6d for
// the constructor's, 0x00466ce2..0x00466d8a for the explicit one, every
// store at the SDK struct's offsets), while server.dll's /Os declines them
// and calls the out-of-line COMDAT (0x10001bd7). One spelling gives both.
// (The local view with an out-of-line Clear that stood here matched
// server.dll only.)

// 0x10028da3 — "SpawnObject <class name> \"spawn args\"": make one object of a
// named class next to whoever is playing.
//
// The spawn point is the FIRST client's m_Position with 200 added to its Z --
// the client list's m_nElements is what the fourth guard tests, and the origin
// comes off m_Head.m_pNext->m_pData. (LTList declares m_nElements BEFORE
// m_Head, which is what makes +0x7e4 the count and +0x7ec the first link.)
//
// CServerMgr+0x928 is a CLTServer, and GetClass and CreateObjectProps are
// FUNCTION-POINTER MEMBERS of it rather than virtuals -- the shipped calls are
// `call *0x24(%ecx)` and `call *0x38(%eax)` straight through the object with
// one pushed argument and a cdecl cleanup, where a virtual would load a vtable
// and pass `this` in ecx. servermsg.h already lays that table out.
//
// The create struct is Cleared TWICE, once on construction and again after
// the class lookup succeeds. Both calls are in the shipped code; the second
// is redundant and is what the source says.
//
// The usage line is shared by all four guards -- no arguments, no manager, no
// factory, no clients -- so a console `SpawnObject` with nobody connected
// prints the usage rather than an error about the client list.
void cc_SpawnObject(int argc, char** argv)
{
	ObjectCreateStruct  ocs;
	Client*             pClient;
	char*               pszProps;
	HCLASS              hClass;

	if (argc == 0 || !g_pServerMgr ||
	    !g_pServerMgr->m_pUnknown928 ||
	    !g_pServerMgr->m_ClientList.m_nElements)
	{
		dsi_ServerError("SpawnObject <class name> \"spawn args\"");
		return;
	}

	if (argc >= 2)
		pszProps = argv[1];
	else
		pszProps = "";

	hClass = g_pServerMgr->m_pUnknown928->GetClass(argv[0]);

	if (!hClass)
	{
		dsi_ServerError("Can't find class %s", argv[0]);
		return;
	}

	ocs.Clear();

	pClient = (Client*)g_pServerMgr->m_ClientList.m_Head.m_pNext->m_pData;

	ocs.m_Pos = pClient->m_Position;
	ocs.m_Pos.z += 200.0f;

	if (g_pServerMgr->m_pUnknown928->CreateObjectProps(
		hClass, &ocs, pszProps))
		dsi_ServerError("%s spawned successfully", argv[0]);
	else
		dsi_ServerError("Error in CreateObjectProps");
}


// "ExhaustMemory" -- a debug command the release build compiled to nothing:
// its table slot points at a bare `ret` (server.dll 0x10036d21, which other
// empty functions share; lithtech.exe's twin slot is the image's folded empty
// function 0x00490200). Written empty because the shipped body is (name ours).
void cc_ExhaustMemory(int argc, char** argv)
{
}


// 0x10053d80 -- the eight commands the console starts with: {name, handler,
// user} triples in this order, every user word zero (lithtech.exe 0x004ad0a8
// holds the same table). cc_InitState hands it to the console with a count of
// 8 (name ours).
ConsoleCommandDef g_ConsoleCommands[8] =
{
	{ "Portal",           cc_Portal,           LTNULL },
	{ "ShowGameVars",     cc_ShowGameVars,     LTNULL },
	{ "ShowUsedFiles",    cc_ShowUsedFiles,    LTNULL },
	{ "world",            cc_World,            LTNULL },
	{ "objectinfo",       cc_ObjectInfo,       LTNULL },
	{ "DisableWMPhysics", cc_DisableWMPhysics, LTNULL },
	{ "ExhaustMemory",    cc_ExhaustMemory,    LTNULL },
	{ "SpawnObject",      cc_SpawnObject,      LTNULL }
};
