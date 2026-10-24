// lithtech.exe — the CLIENT console: its command history, the print
// forwarders, the input line's cursor, and a run of console command handlers
// (reconstruction).  Build cl 8447 /O2, include path /I ..\server.
//
// Nothing here belongs to src/engine/server/console.cpp.  That file IS
// compiled into lithtech.exe and seven functions of this address range are
// already its code — see the ALREADY SOURCED list at the bottom of this file.
// They are named there and deliberately NOT rewritten.
//
// What ties the range together is one global console object at 0x004b2af8,
// built by 0x0041c2a0 and torn down by 0x0041c390, plus the client's
// ConsoleState at 0x004b2f00 (clientmgr2.cpp already names that one) and the
// CClientMgr the console works through at 0x004b2f44.
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "servermgr.h"
#include "de_error.h"
#include "cltdib.h"


// ----------------------------------------------------------------------- //
//  What this run calls but does not define
// ----------------------------------------------------------------------- //

// The client's console state and the CClientMgr it works through, both
// already named in src/engine/lithtech/clientmgr2.cpp.  Both are this TU's
// .bss (the console's block runs 0x004b2af0..0x004b2f47) and are DEFINED
// below, in front of the command handlers.
class CClientMgrView;
extern ConsoleState    g_ClientConsoleState;                // 0x004b2f00
extern CClientMgrView* g_pConsoleClientMgr;                 // 0x004b2f44

// The console printer every command handler in this range reports through.
// It is variadic and cdecl; the format is its first argument.  (name ours)
void  con_ConsolePrint(char* pFormat, ...);                 // 0x0042e6f0

// src/engine/lithtech/stdlithalloc.cpp names both of these.
void* dalloc(unsigned long nSize);                          // 0x004290b0
void  dfree(void* pMem);                                    // 0x004290c0

// src/engine/lithtech/clientinterface.h names this one.
// Its body is written at the bottom of this file (see there for why).
LTRESULT ci_DeleteSurface(HSURFACE hSurface);               // 0x0040c4b0

// src/engine/lithtech/clientmgr2.cpp names this one.
void  cm_FreeList1110AndMore(CClientMgrView* pClientMgr);   // 0x00411a50

// src/engine/server/console.cpp is where these live; they are reached from
// the client's console command handlers.
//   con_PrintVar  0x0041b570   (server twin 0x10005d1e)
// dhash_* are src/engine/server/dhash.cpp's, at the client's addresses.
void  con_PrintVar(ConsoleState* pState, LTCommandVar* pVar);  // 0x0041b570
void* dhash_GetFirst(void* hHash);                          // 0x0042d0a0
void* dhash_GetNext(void** phIter);                          // 0x0042d0f0
void* dhash_GetData(void* hElement);                        // 0x0042d080

// The input-device table at CClientMgr+0x1508; src/engine/lithtech/clientmgr.h
// already lays it out (the entries are cdecl function pointers, and
// EnableDevice at +0x24 is handed the table as its own first argument).  This
// is the two-field local view of it, because this TU does not include that
// header.  (names clientmgr.h's)
class CClientInput
{
public:
	uint8  m_pad_00[0x24];
	LTBOOL (*EnableDevice)(CClientInput* pInput, char* strDeviceName);  // 0x24
};

// clientmgr.h's CClientConnection, the object at CClientMgr+0x150c: the
// game mode it reports and the console-string sender ccclient.cpp names.
class CClientConnectionView
{
public:
	void RunConsoleString(char* pCommand);             // 0x00413c60

	uint8  m_pad_00[0x84];
	int    m_nGameMode;                      // 0x084  GetGameMode
};

// clientmgr.h's CClientFileRef: a type, the name, and a word the lookup
// fills in; the constructor zeroes all three in that order.
class CClientFileRefView
{
public:
	CClientFileRefView() { m_nFileType = 0; m_pFilename = LTNULL; m_nLocalID = 0; }

	uint32 m_nFileType;                      // 0x00
	char*  m_pFilename;                      // 0x04
	uint16 m_nLocalID;                       // 0x08
};

// 0x00404680 -- the file manager's lookup (clientwin.cpp's clu_fm_GetFileRec,
// cchandler.cpp's fm_GetFileRec); cdecl, a byte flag last.
class CClientFileMgrView;
void* fm_GetFileRec(CClientFileMgrView* pMgr, CClientFileRefView* pRef,
                    uint8 nFlag);                           // 0x00404680

// The fields of CClientMgr this run reads, at clientmgr.h's offsets.
class CClientMgrView
{
public:
	LTRESULT StartGame(StartGameRequest* pRequest);    // 0x0040fd90

	uint8                  m_pad_0000[0x1508];
	CClientInput*          m_pInput;         // 0x1508
	CClientConnectionView* m_pConnection;    // 0x150c
	uint8                  m_pad_1510[0x1534 - 0x1510];
	CClientFileMgrView*    m_pFileMgr;       // 0x1534
};

// The world starter behind the console's "World" command; 450 bytes at
// 0x0041d870 building "worlds\\%s.dat" and running "world %s".  (name ours)
void cc_DoStartWorld(char* pWorldName, char* pRecordFilename); // 0x0041d870

// Two globals the command handlers write.  0x004a9f18 ships holding
// "Screenshot", which is what says it is a char array and not a pointer.
// (names ours)
//
// g_szScreenshotName is DEFINED here, once, for the whole link (clientwin.cpp
// reaches it under this name too).  Its shipped home is the dsi TU's .data
// run (src/engine/server/dsi.cpp): 0x004a9f18 sits between ConsoleEnable
// (0x004a9f14) and the next initialised word at 0x004a9f7c, a 100-byte
// buffer, with server.dll's twin at 0x10051cb0.  g_nUnknown4a9efc (1) is
// dsi.cpp's, lithtech-only.
char   g_szScreenshotName[100] = "Screenshot";               // 0x004a9f18
extern int    g_nUnknown4a9efc;                              // 0x004a9efc

// The two model-offset vectors the config-file writer prints.  (names ours,
// after the console commands the writer emits)
extern LTVector g_ModelAdd;                                  // 0x004b3228
extern LTVector g_ModelDirAdd;                               // 0x004b3218

// The third list the completion enumerator walks: twenty-byte records whose
// first field is the name, behind a count/array pair of accessors.  Neither
// the class nor the record is in this run.  (names ours)
class ConsoleExtraName
{
public:
	char* m_pName;                           // 0x00
	uint8 m_pad_04[0x14 - 0x04];
};
int               cn_GetNumExtraNames();                    // 0x0042f670
ConsoleExtraName* cn_GetExtraNames();                       // 0x0042f660


// ----------------------------------------------------------------------- //
//  The console's name lists
// ----------------------------------------------------------------------- //

// 0x004a1e38 is a TEN-slot vtable — the base of everything the console
// enumerates by name.  Its instance is the do-nothing list at 0x004b2af0
// (the constructor at 0x0041b940 is one vptr store and nothing else), which
// is what the console falls back to when it is handed no history; five of the
// ten slots point at the image's shared empty body 0x00432040.
//
// What numbers the slots is the four bodies below plus the two derived tables
// that reuse them (0x004a1e60, the ring history; 0x004a1ebc, the completion
// enumerator):
//
//    +0x00  First()          -- 0x004a1e60 overrides it with 0x0041c080
//    +0x04  Last()
//    +0x08  Next()
//    +0x0c  Prev()
//    +0x10  ~CConsoleNameList        (0x0041b950 in the console TU)
//    +0x14  FindFirst(char*)  0x0041b990  -> slots +0x00 and +0x1c
//    +0x18  FindLast(char*)   0x0041ba00  -> slots +0x04 and +0x20
//    +0x1c  FindNext(char*)   0x0041ba70  -> slot  +0x08 in a loop
//    +0x20  FindPrev(char*)   0x0041bad0  -> slot  +0x0c in a loop
//    +0x24  GetName()
//
// The pairing is what says which raw slot is which: FindFirst starts with
// +0x00 and then hands off to FindNext, which advances through +0x08; FindLast
// starts with +0x04 and hands off to FindPrev, which advances through +0x0c.
// So +0x00/+0x08 are the forward pair and +0x04/+0x0c the backward one, and
// the console's history needs exactly that (up-arrow walks backwards).
//
// The name match is a PREFIX match -- strncmp against strlen of the wanted
// prefix -- which is what these four are for: tab completion.
// (every name in this class is ours)
class CConsoleNameList
{
public:
	// The five slots the base leaves to the image's shared return-zero body
	// at 0x00432040 are declared, NOT made pure: 0x004b2af0 is an instance
	// of this class itself, so the class is concrete.  Their bodies are the
	// return-zero ones written below the class, folded onto 0x00432040.
	virtual LTBOOL First();                            // +0x00
	virtual LTBOOL Last();                             // +0x04
	virtual LTBOOL Next();                             // +0x08
	virtual LTBOOL Prev();                             // +0x0c
	virtual ~CConsoleNameList() {}                     // +0x10
	virtual LTBOOL FindFirst(char* pPrefix);           // +0x14
	virtual LTBOOL FindLast(char* pPrefix);            // +0x18
	virtual LTBOOL FindNext(char* pPrefix);            // +0x1c
	virtual LTBOOL FindPrev(char* pPrefix);            // +0x20
	virtual char*  GetName();                          // +0x24
};

// The five slots 0x004a1e38 points at 0x00432040, `xor %eax,%eax; ret`: the
// do-nothing list answers "nothing" to every step.  /OPT:ICF folds these
// onto the image's one return-zero body, which is why they share its
// address.
LTBOOL CConsoleNameList::First()
{
	return LTFALSE;
}

LTBOOL CConsoleNameList::Last()
{
	return LTFALSE;
}

LTBOOL CConsoleNameList::Next()
{
	return LTFALSE;
}

LTBOOL CConsoleNameList::Prev()
{
	return LTFALSE;
}

char* CConsoleNameList::GetName()
{
	return LTNULL;
}

// 0x0041b990 — the first entry whose name starts with the prefix.
//
// No prefix (null or empty) means "the first entry", full stop; the answer is
// then just whether there was one.  With a prefix, a first entry that does not
// match is not a failure -- FindNext is asked to walk on from it.
LTBOOL CConsoleNameList::FindFirst(char* pPrefix)
{
	char* pName;

	if (!First())
		return LTFALSE;

	if (pPrefix && pPrefix[0] != 0)
	{
		pName = GetName();

		if (!pName)
			return LTFALSE;

		if (strncmp(pPrefix, pName, strlen(pPrefix)) != 0)
			return FindNext(pPrefix);
	}

	return LTTRUE;
}

// 0x0041ba00 — the same walking backwards: Last() then FindPrev().
//
// /OPT:ICF is on in lithtech.exe and this body is NOT folded with FindFirst
// above, which is the proof the two are separate source: the slot numbers
// differ (+0x04/+0x20 against +0x00/+0x1c) and so do the bytes.
LTBOOL CConsoleNameList::FindLast(char* pPrefix)
{
	char* pName;

	if (!Last())
		return LTFALSE;

	if (pPrefix && pPrefix[0] != 0)
	{
		pName = GetName();

		if (!pName)
			return LTFALSE;

		if (strncmp(pPrefix, pName, strlen(pPrefix)) != 0)
			return FindPrev(pPrefix);
	}

	return LTTRUE;
}

// 0x0041ba70 — walk forward until a name starts with the prefix.
//
// The prefix length is hoisted out of the loop (one `repnz scasb` before it,
// %edi held across every iteration), which is what the local says.  The
// answer is the match flag itself, so running off the end returns it still
// clear -- there is no second exit.
LTBOOL CConsoleNameList::FindNext(char* pPrefix)
{
	uint32 nLen;
	LTBOOL bFound;
	char*  pName;

	if (!pPrefix)
		return Next();

	nLen = strlen(pPrefix);
	bFound = LTFALSE;

	// The test belongs at the TOP: as a do/while with two breaks VC6
	// duplicates the Next() call and the function comes out 110 bytes
	// against 96.
	while (!bFound)
	{
		if (!Next())
			break;

		pName = GetName();

		if (!pName)
			break;

		bFound = (strncmp(pPrefix, pName, nLen) == 0);
	}

	return bFound;
}

// 0x0041bad0 — the same backwards, through Prev().
LTBOOL CConsoleNameList::FindPrev(char* pPrefix)
{
	uint32 nLen;
	LTBOOL bFound;
	char*  pName;

	if (!pPrefix)
		return Prev();

	nLen = strlen(pPrefix);
	bFound = LTFALSE;

	// The test belongs at the TOP: as a do/while with two breaks VC6
	// duplicates the Prev() call and the function comes out 110 bytes
	// against 96.
	while (!bFound)
	{
		if (!Prev())
			break;

		pName = GetName();

		if (!pName)
			break;

		bFound = (strncmp(pPrefix, pName, nLen) == 0);
	}

	return bFound;
}


// ----------------------------------------------------------------------- //
//  The command history — a ring of 0x10c-byte records
// ----------------------------------------------------------------------- //

// One entry.  The size comes straight out of the index arithmetic every
// method here shares: %eax*67*4, i.e. 0x10c, with the text at +0x0c and
// 0x100 bytes long (the strncpy length).  What the first twelve bytes are for
// is not visible from this run.  (names ours)
class ConsoleHistoryEntry
{
public:
	uint8 m_pad_00[0x0c];
	char  m_szText[0x100];                   // 0x0c
};

// The ring, vtable 0x004a1e60.  One instance: the console's own at
// CConsole+0x3cc, i.e. g_Console.m_History at 0x004b2ec4, which is the one
// the history console commands work on.
//
// m_nNext is one past the newest entry and m_nFirst the oldest, so
// m_nFirst == m_nNext means empty; m_nCur is the enumeration cursor and is
// deliberately allowed to sit ON m_nNext, which is the "past the end" state
// GetName reports as null.  (names ours)
class CConsoleHistory : public CConsoleNameList
{
public:
	CConsoleHistory(int nSize);                        // 0x0041c010
	virtual ~CConsoleHistory();                        // 0x0041c060 (thunk
	                                                   //   0x0041c040)
	virtual LTBOOL First();                            // 0x0041c080
	virtual LTBOOL Last();                             // 0x0041c0a0
	virtual LTBOOL Next();                             // 0x0041c0b0
	virtual LTBOOL Prev();                             // 0x0041c0e0
	virtual char*  GetName();                          // 0x0041c270

	void   Add(char* pText);                           // 0x0041c110
	LTBOOL SetSize(int nSize);                         // 0x0041c180

	// The usable size: the ring keeps one slot empty.  0x0041c3e0 tests it
	// as `test; je; dec; test; jne` -- zero, or one less than m_nSize.
	int    GetMaxEntries() { return m_nSize ? m_nSize - 1 : 0; }  // (name ours)

	ConsoleHistoryEntry* m_pEntries;         // 0x04
	int                  m_nCur;             // 0x08
	int                  m_nSize;            // 0x0c  entries allocated
	int                  m_nFirst;           // 0x10
	int                  m_nNext;            // 0x14
};

// 0x0041c010 — the ring starts empty and then asks for its size, so a size
// that cannot be allocated leaves a usable empty history rather than a
// half-built one.
CConsoleHistory::CConsoleHistory(int nSize)
{
	m_pEntries = LTNULL;
	m_nCur = 0;
	m_nSize = 0;
	m_nFirst = 0;
	m_nNext = 0;

	SetSize(nSize);
}

// 0x0041c060 — size zero frees the storage, so the destructor is one call.
//
// The vptr store at the top is NOT dead here (SetSize dispatches through the
// vtable), which is why this destructor is a real function while the
// completion enumerator's at 0x0041e990 collapses to a single store: there
// the derived store is immediately overwritten by the base's and /O2 drops it.
CConsoleHistory::~CConsoleHistory()
{
	SetSize(0);
}

// 0x0041c080 — cursor to the oldest entry; the answer is "is there one".
LTBOOL CConsoleHistory::First()
{
	m_nCur = m_nFirst;

	return (m_nCur != m_nNext);
}

// 0x0041c0a0 — cursor one PAST the newest, then step back: Prev() is what
// reports the empty ring, so this is a tail call into it.
LTBOOL CConsoleHistory::Last()
{
	m_nCur = m_nNext;

	return Prev();
}

// 0x0041c0b0 — forward one, wrapping.  A cursor already at m_nNext has
// nowhere to go; landing on m_nNext after the step means the walk is over.
LTBOOL CConsoleHistory::Next()
{
	if (m_nCur == m_nNext)
		return LTFALSE;

	m_nCur++;

	if (m_nCur >= m_nSize)
		m_nCur = 0;

	return (m_nCur != m_nNext);
}

// 0x0041c0e0 — back one, wrapping.  The oldest entry has nothing before it;
// anything else always lands somewhere real, hence the unconditional true.
LTBOOL CConsoleHistory::Prev()
{
	if (m_nCur == m_nFirst)
		return LTFALSE;

	m_nCur--;

	if (m_nCur < 0)
		m_nCur = m_nSize - 1;

	return LTTRUE;
}

// 0x0041c270 — the text at the cursor, or nothing.  Three guards: below the
// array, past it, and sitting on the write position.
char* CConsoleHistory::GetName()
{
	if (m_nCur < 0 || m_nCur >= m_nSize || m_nCur == m_nNext)
		return LTNULL;

	return m_pEntries[m_nCur].m_szText;
}

// 0x0041c110 — file one command.  Overrunning the oldest entry pushes
// m_nFirst along, so the ring silently forgets rather than filling up; a
// history of size zero (no storage) accepts nothing.
void CConsoleHistory::Add(char* pText)
{
	if (m_nSize == 0)
		return;

	strncpy(m_pEntries[m_nNext].m_szText, pText, 0x100);

	m_nNext++;

	if (m_nNext >= m_nSize)
		m_nNext = 0;

	if (m_nFirst == m_nNext)
	{
		m_nFirst++;

		if (m_nFirst >= m_nSize)
			m_nFirst = 0;
	}
}

// 0x0041c180 — resize, carrying the existing commands over.
//
// The allocation is one entry MORE than asked for: m_nSize is compared
// against nSize + 1 for the "nothing to do" case, and a non-zero nSize is
// then INCREMENTED in place (0x0041c1a4 `mov %eax,%ebp` overwrites the
// parameter's register with nSize + 1, and the parameter's stack slot is
// reused for pNew) -- that is the ring's spare slot (m_nNext can equal
// m_nFirst only when empty, so one entry is never usable).  A zero nSize
// skips the increment, so SetSize(0) leaves m_nSize == 0, not 1
// (0x0041c24f stores %ebp, still 0 on that path).
//
// The copy runs BACKWARDS from the newest -- Last() then Prev() -- filling the
// new array from its top down, so when the old history is longer than the new
// array the OLDEST commands are the ones dropped.  m_nFirst then names
// wherever the copy stopped and m_nNext is zero, i.e. the new ring is full to
// the top.
LTBOOL CConsoleHistory::SetSize(int nSize)
{
	int                  i;
	ConsoleHistoryEntry* pNew;
	char*                pName;

	if (m_nSize == nSize + 1)
		return LTTRUE;

	i = 0;
	pNew = LTNULL;

	if (nSize != 0)
	{
		nSize++;

		pNew = (ConsoleHistoryEntry*)dalloc(nSize * sizeof(ConsoleHistoryEntry));

		if (!pNew)
			return LTFALSE;

		if (Last())
		{
			i = nSize - 1;

			while ((pName = GetName()) != LTNULL)
			{
				strncpy(pNew[i].m_szText, pName, 0x100);

				i--;

				if (i == 0)
					break;

				if (!Prev())
					break;
			}

			i++;

			m_nNext = 0;
			m_nFirst = i;

			i = 0;
		}
		else
		{
			m_nNext = i;
			m_nFirst = i;
		}
	}
	else
	{
		m_nNext = i;
		m_nFirst = i;
	}

	if (m_nSize != i)
		dfree(m_pEntries);

	m_nSize = nSize;
	m_pEntries = pNew;

	return LTTRUE;
}


// ----------------------------------------------------------------------- //
//  The completion enumerator
// ----------------------------------------------------------------------- //

// Vtable 0x004a1ebc, the second class over CConsoleNameList: it hands the
// four prefix-search bodies above THREE name sources in turn, which is what
// makes console tab completion see commands, variables and one further list
// as a single sequence.
//
// m_nStage says which source; the three cursors are independent because the
// walk moves between them.  Slots +0x04 (Last) and +0x0c (Prev) are the shared
// empty body 0x00432040 -- the completion list only runs forwards.
//
// The stage numbering is read off GetName below: 0 is the console state's
// built-in command table, 1 the console variable hash, 2 the twenty-byte
// records behind 0x0042f660/0x0042f670.  (names ours)
class CConsoleCompletion : public CConsoleNameList
{
public:
	CConsoleCompletion();                              // 0x0041e950
	virtual ~CConsoleCompletion();                     // 0x0041e990 (??_G
	                                                   //   0x0041e970)
	virtual LTBOOL First();                            // 0x0041e9a0
	virtual LTBOOL Next();                             // 0x0041e9b0
	virtual char*  GetName();                          // 0x0041eaa0

	int    m_nStage;                         // 0x04
	int    m_nCommand;                       // 0x08
	void*  m_hVarIter;                       // 0x0c
	void*  m_hVarElement;                    // 0x10
	int    m_nExtra;                         // 0x14
};

// 0x0041e950 — the enumerator starts on nothing.  Both halves are out of
// line in the image: the file-scope instance's initialiser (0x0041d610) is
// `mov $0x4b2ee8,%ecx; jmp 0x0041e950`, and its atexit thunk (0x0041d630)
// jumps to 0x0041e990.
CConsoleCompletion::CConsoleCompletion()
	: m_nStage(0), m_nCommand(0), m_hVarIter(LTNULL), m_hVarElement(LTNULL),
	  m_nExtra(0)
{
}

// 0x0041e990 — nothing to free; the body is only the base's vptr store.
CConsoleCompletion::~CConsoleCompletion()
{
}

// 0x0041e9a0 — back to the first command of the first source.  It cannot
// fail: the walk always has a valid position even when the table is empty,
// because GetName is what reports that.
LTBOOL CConsoleCompletion::First()
{
	m_nStage = 0;
	m_nCommand = 0;

	return LTTRUE;
}

// 0x0041e9b0 — step to the next name, moving on to the next source when one
// runs out.  Each source change resets the next source's cursor and then
// asks Next() again THROUGH THE VTABLE (`call *0x8(%edx)` at 0x0041ea46 /
// 0x0041ea7f), so an empty source is skipped by recursion.
//
// The answer is a "done" flag inverted on the way out (`test %ebx; sete`),
// and the recursive answer is folded into it inverted (`neg/sbb/inc`).
//
// Stage 2 tests m_nCommand, not m_nExtra, against the record count
// (`cmp %eax,0x8(%esi)` at 0x0041e9d0) -- as shipped.
LTBOOL CConsoleCompletion::Next()
{
	LTBOOL bDone = LTFALSE;

	switch (m_nStage)
	{
		case 0:
			m_nCommand++;

			if (m_nCommand >= g_ClientConsoleState.m_nBuiltins)
			{
				m_nStage = 1;
				m_hVarIter = LTNULL;

				bDone = !Next();
			}
			break;

		case 1:
			if (!m_hVarIter)
			{
				m_hVarIter = dhash_GetFirst(g_ClientConsoleState.m_hVarHash);
				m_hVarElement = LTNULL;
			}

			do
			{
				m_hVarElement = dhash_GetNext(&m_hVarIter);
			}
			while (m_hVarIter && !m_hVarElement);

			if (!m_hVarIter)
			{
				m_nStage = 2;
				m_nExtra = -1;

				bDone = !Next();
			}
			break;

		case 2:
			if (m_nCommand >= cn_GetNumExtraNames())
				bDone = LTTRUE;
			else
				m_nExtra++;
			break;
	}

	return !bDone;
}

// 0x0041eaa0 — the name at the current position, whichever source that is.
//
// One result local, NULL-initialised and assigned on each arm, returned
// once: shipped holds that NULL in %edi from the entry (`xor %edi,%edi`,
// then `sub %edi,%eax` for case 0 and `mov %edi,%eax` at the shared exit).
// Early `return`s per arm give `sub $0` / `xor %eax,%eax` and no %edi.
//
// Every arm bails to the same null, and the three arms are what name the
// three sources: ConsoleCommandDef's 0x0c stride and its m_pName at +0x00 for
// stage 0, LTCommandVar::m_pName at +0x10 behind dhash_GetData for stage 1,
// and a 0x14 stride with the name at +0x00 for stage 2.
char* CConsoleCompletion::GetName()
{
	char* pName = LTNULL;

	switch (m_nStage)
	{
		case 0:
			if (m_nCommand < g_ClientConsoleState.m_nBuiltins)
				pName = g_ClientConsoleState.m_pBuiltins[m_nCommand].m_pName;
			break;

		case 1:
			if (m_hVarElement)
				pName = ((LTCommandVar*)dhash_GetData(m_hVarElement))->m_pName;
			break;

		case 2:
			if (m_nExtra < cn_GetNumExtraNames())
				pName = cn_GetExtraNames()[m_nExtra].m_pName;
			break;
	}

	return pName;
}


// ----------------------------------------------------------------------- //
//  The console's input line
// ----------------------------------------------------------------------- //

// The sub-object at CConsole+0x10, constructed by 0x0041bb30.  The text is a
// 0x100-byte buffer at +0x08 with its length beside it at +0x04 and the
// caret index far below at +0x12c, and the three colours the constructor
// plants (0x00b4b4ff, 0x001e1e1e, 0x00ffffff) sit between them.  (names ours)
class CConsoleLine
{
public:
	CConsoleLine();                                    // 0x0041bb30
	void Clear();                                      // 0x0041bb90
	void SetText(char* pText);                         // 0x0041bba0
	void MoveCursor(int nDelta);                       // 0x0041bd30
	void HandleKey(int nKey);                          // 0x0041bea0 (conline.cpp)

	uint32 m_Unknown00;                      // 0x000
	int    m_nLen;                           // 0x004
	char   m_szText[0x100];                  // 0x008
	uint32 m_Unknown108;                     // 0x108
	uint32 m_ColorText;                      // 0x10c  0x00b4b4ff
	uint32 m_ColorBack;                      // 0x110  0x001e1e1e
	uint32 m_ColorCaret;                     // 0x114  0x00ffffff
	LTRect m_rcText;                         // 0x118  (ccline.cpp's name)
	int    m_nUnknown128;                    // 0x128  500
	int    m_nCursor;                        // 0x12c
};

// 0x0041bd30 — slide the caret, clamped to the text.
//
// The clamp is TWO macro expansions, not one comparison each way: the minimum
// is computed once for the test and then formed AGAIN for the value (the
// shipped code pops %esi and re-does `cmp %edx,%eax`), which is what a
// macro whose argument is itself a macro gives.  The outer test is
// `> 0`, not `0 >`: the shipped branch is `jle` to the zero.
void CConsoleLine::MoveCursor(int nDelta)
{
	m_nCursor = LTMAX(LTMIN(m_nCursor + nDelta, m_nLen), 0);
}

// 0x0041bb30 — the line's constructor.  The four zero stores that lead the
// shipped body are LTRect's defaulted constructor for m_rcText, run before
// the body as a member's must be; then the three colours, the first word,
// the 500, and Clear() to empty the text and park the caret.
CConsoleLine::CConsoleLine()
{
	m_ColorText   = 0x00b4b4ff;
	m_ColorBack   = 0x001e1e1e;
	m_ColorCaret  = 0x00ffffff;
	m_Unknown00   = 0;
	m_nUnknown128 = 500;

	Clear();
}

// 0x0041bb90 — empty the line.  The terminator goes down before the length,
// and MoveCursor(0) is what re-clamps the caret to the front.
void CConsoleLine::Clear()
{
	m_szText[0] = 0;
	m_nLen = 0;

	MoveCursor(0);
}

// 0x0041bba0 — replace the line's text.  Null means clear; anything else is
// copied in bounded, its length measured from the COPY (so a string longer
// than the buffer is measured after truncation), and the caret re-clamped.
void CConsoleLine::SetText(char* pText)
{
	if (!pText)
	{
		Clear();
		return;
	}

	strncpy(m_szText, pText, 0x100);

	m_nLen = strlen(m_szText);

	MoveCursor(m_nLen);
}


// ----------------------------------------------------------------------- //
//  The console object itself
// ----------------------------------------------------------------------- //

// The console object.  0x0041c2a0 is the constructor and 0x0041c390 the
// destructor; the object is the file-scope g_Console at 0x004b2af8, whose
// dynamic initialiser pair is 0x0041b8f0/0x0041b910 (0x0041b920 is the atexit
// thunk: `mov $0x4b2af8,%ecx; jmp 0x41c390`).
//
// The size is at least 0x3ec (the constructor's last store).  The members
// with constructors or destructors are exactly the four the pair of them
// call or expand, and the destructor tears them down in the reverse of this
// order (+0x3cc, +0x3b8, +0x250; the line at +0x10 has no destructor):
//
//   +0x010  CConsoleLine          ctor 0x0041bb30 (called)
//   +0x250  the critical section  ctor 0x00444150 / dtor 0x00444170 (called)
//   +0x3b8  the scrollback list   vptr 0x004a1e88, ctor/dtor expanded inline
//   +0x3cc  CConsoleHistory       ctor 0x0041c010(0) / dtor 0x0041c060 (called)
//
// ccline.cpp's CcnConsole is the same object seen from the methods at
// 0x0041c910..; its field names are used where it has them.  (names ours)

// 0x00444150 / 0x00444170 -- lthread.h's critical-section wrapper, a
// __thiscall pair around Initialize/DeleteCriticalSection (lt2new3.cpp's
// CCritSecBase).  Both are called, so both are out of line; the 0x18 bytes
// are the CRITICAL_SECTION.  (name ours)
class CConsoleCritSec
{
public:
	CConsoleCritSec();                                 // 0x00444150
	~CConsoleCritSec();                                // 0x00444170
	void Enter();                                      // 0x00444180
	void Leave();                                      // 0x00444190
	LTBOOL IsValid();                                  // 0x00490730, `mov $1,%eax; ret`

	uint8 m_CS[0x18];
};

// The scrollback ring's list head, vtable 0x004a1e88 (the twelve-slot
// collection block clientmgr2.cpp describes).  Its constructor and
// destructor are EXPANDED at both sites -- the vptr store and two zeroes --
// so both are in-class.  The constructor zeroes +0x04 then +0x08
// (0x0041c312/0x0041c318) and the destructor +0x08 then +0x04
// (0x0041c3bb/0x0041c3c5), the same mirror lt2new3.cpp found at 0x004a2648.
// The field names are ccline.cpp's.  (class name ours)
// A scrollback node (0x10c bytes; ccline.cpp's CcnTextLine, whose remove
// at 0x0041c9ad is what says which pointer is which).  (names ours)
struct CConsoleTextLine
{
	CConsoleTextLine* m_pPrev;               // 0x000
	CConsoleTextLine* m_pNext;               // 0x004
	uint32            m_nColor;              // 0x008
	char              m_szText[0x100];       // 0x00c
};

class CConsoleLineList
{
public:
	CConsoleLineList() { m_nNumLines = 0; m_pLines = LTNULL; }
	~CConsoleLineList() { m_pLines = LTNULL; m_nNumLines = 0; }

	// The first of the table's twelve slots, standing for all of them: the
	// table has no destructor slot, so the destructor above is NOT virtual.
	// The in-class accessor, not the bare field: read through it, VC6 keeps
	// Init's +0x3c8 store ABOVE the +0x3bc load as shipped (0x0041c457).
	int GetNumLines() { return m_nNumLines; }

	virtual void GetFirst();                           // +0x00  0x00439fc0

	// Slots 4, 5 and 7 (0x0041d550 add at the tail, 0x0041d5a0 unlink,
	// the count), which 0x0041d1c0 EXPANDS on the member.  Written as
	// plain in-class members with the shipped slot bodies (a stand-in for
	// the template's virtuals, called non-virtually on a member object).
	uint32 GetSize() { return m_nNumLines; }

	LTBOOL Add(CConsoleTextLine*& pElem)
	{
		CConsoleTextLine* pNew = pElem;
		CConsoleTextLine* pTail;

		if (m_pLines)
		{
			pTail = m_pLines->m_pPrev;
			pTail->m_pNext->m_pPrev = pNew;
			pNew->m_pNext = pTail->m_pNext;
			pNew->m_pPrev = pTail;
			pTail->m_pNext = pNew;
			m_nNumLines++;
		}
		else
		{
			pNew->m_pNext = pNew;
			pNew->m_pPrev = pNew;
			m_pLines = pNew;
			m_nNumLines++;
		}

		return LTTRUE;
	}

	void Remove(CConsoleTextLine* pLine)
	{
		pLine->m_pPrev->m_pNext = pLine->m_pNext;
		pLine->m_pNext->m_pPrev = pLine->m_pPrev;

		if (pLine == m_pLines)
			m_pLines = m_pLines->m_pNext;

		m_nNumLines--;

		if (m_nNumLines == 0)
			m_pLines = LTNULL;
	}

	int               m_nNumLines;           // +0x04  (console +0x3bc)
	CConsoleTextLine* m_pLines;              // +0x08  (console +0x3c0)
};

class CConsoleView
{
public:
	CConsoleView();                                    // 0x0041c2a0
	~CConsoleView();                                   // 0x0041c390
	void   Term(LTBOOL bFreeLines);                    // 0x0041c4e0 (dmgrow.cpp)
	void   VPrintf(uint32 nColor, int nLevel, char* pFormat, va_list ap);  // 0x0041d320
	LTBOOL Init(LTRect* pRect, void (*pfnCommand)(char* pText),
	            uint32 nUnknown268, CConsoleNameList* pHistory);  // 0x0041c3e0
	LTBOOL InitFont();                                 // 0x0041c5a0
	LTRESULT LoadBackground();                         // 0x0041cd50 (cltcursor.cpp)
	LTBOOL SetNumLines(uint32 nNumLines);              // 0x0041c910 (ccline.cpp)
	void   RefreshFromVars();                          // 0x0041ca60 (ccline.cpp)
	LTBOOL InitLines();                                // 0x0041c4c0
	void   Sub41d1c0(uint32 nColor, int nLevel, char* pText);  // 0x0041d1c0
	void   Sub41d3a0(int nKey);                        // 0x0041d3a0
	// ccline.cpp's CcnConsole methods, the ones the key handler reaches.
	void   OnEnter();                                  // 0x0041ca10
	void   SearchHistory(int nCount);                  // 0x0041cae0
	void   CompleteNext();                             // 0x0041cbc0
	void   CompletePrev();                             // 0x0041cc30
	void   ListMatches();                              // 0x0041cca0
	void   ClearMode();                                // 0x0041cd40
	void   Scroll(int nDelta);                         // 0x0041cee0
	void   SetHistory(CConsoleNameList* pHistory);     // 0x0041cba0
	void   FreeSurface();                              // 0x0041c570
	void   Free29c();                                  // 0x0041c8e0

	uint32             m_Unknown000;         // 0x000  1 in the ctor, 0 in the dtor
	uint32             m_Unknown004;         // 0x004
	uint8              m_pad_008[0x010 - 0x008];
	CConsoleLine       m_Line;               // 0x010 .. 0x140
	uint8              m_pad_140[0x144 - 0x140];
	int                m_nLevel;             // 0x144  VPrintf's threshold; 1
	uint8              m_pad_148[0x248 - 0x148];
	uint32             m_Unknown248;         // 0x248
	uint32             m_Color24c;           // 0x24c  0x00ffffff
	CConsoleCritSec    m_CS;                 // 0x250 .. 0x268
	uint32             m_Unknown268;         // 0x268
	LTRect             m_rcConsole;          // 0x26c  (ccline.cpp's name)
	LTRect             m_rc27c;              // 0x27c
	LTRect             m_rc28c;              // 0x28c
	uint32*            m_pCharBits;          // 0x29c  InitFont's row masks, one dword per cell row
	uint16             m_nFontHeight;        // 0x2a0  (ccline.cpp's name); 1
	uint16             m_nCharStride;        // 0x2a2  dwords per character
	uint16             m_CharWidths[128];    // 0x2a4
	void*              m_hSurface;           // 0x3a4
	float              m_fAlpha;             // 0x3a8  (ccline.cpp's name); 1.0f
	LTRESULT           m_nOptimized;         // 0x3ac  (ccline.cpp's name)
	uint32             m_Unknown3b0;         // 0x3b0
	void*              m_hMainWnd;           // 0x3b4  the window GetDC takes
	CConsoleLineList   m_Lines;              // 0x3b8 .. 0x3c4
	int                m_nNumVisible;        // 0x3c4  (ccline.cpp's name)
	int                m_nScrollPos;         // 0x3c8  (ccline.cpp's name)
	CConsoleHistory    m_History;            // 0x3cc .. 0x3e4
	void             (*m_pfnCommand)(char* pText);  // 0x3e4  (ccline.cpp's name)
	CConsoleNameList*  m_pHistory;           // 0x3e8
};

// The two file-scope objects the TU opens with, in the order their
// initialiser groups run: g_Console first (0x0041b8f0: construct 0x0041b900,
// atexit 0x0041b910 of 0x0041b920), then the do-nothing list (0x0041b930:
// the one-store constructor 0x0041b940, atexit of 0x0041b980).
//
// The history the console commands work on (0x004b2ec4) and the hook word
// con_Sub41b870 sets (0x004b2ea8) are NOT globals of their own: they are
// g_Console's m_History (+0x3cc) and m_Unknown3b0 (+0x3b0).  Nothing
// constructs 0x004b2ec4 separately; g_Console's constructor does.
CConsoleView     g_Console;                                  // 0x004b2af8
CConsoleNameList g_EmptyNameList;                            // 0x004b2af0

// 0x0041c2a0 — the console's constructor.  The members come first, in
// declaration order: the line (a call), the critical section (a call), the
// three LTRects' twelve zero stores, the list's vptr and two zeroes, and the
// history with size 0 (a call).  Then the body, in the shipped store order.
CConsoleView::CConsoleView() : m_History(0)
{
	m_pCharBits = LTNULL;
	m_hMainWnd    = LTNULL;
	m_pfnCommand  = LTNULL;
	m_nLevel      = 1;
	m_Unknown248  = 0;
	m_nFontHeight = 1;
	m_Unknown268  = 0;
	m_Unknown3b0  = 0;
	m_hSurface    = LTNULL;
	m_nOptimized  = 0;
	m_Unknown000  = 1;
	m_Unknown004  = 0;
	m_Color24c    = 0x00ffffff;
	m_fAlpha      = 1.0f;
	m_pHistory    = &g_EmptyNameList;
}

// 0x0041c390 — the console's destructor.  Term(LTTRUE) takes the scrollback
// and the history's storage with it; the one statement of the body after it
// is the store at 0x0041c3a0, which VC6 schedules between the next call's
// `lea` and the call.  The rest is the three member destructors.
CConsoleView::~CConsoleView()
{
	Term(LTTRUE);

	m_Unknown000 = 0;
}

// 0x0041c3e0 (250 bytes) — bring the console up.  thiscall, `ret $0x10`,
// answers 1 or 0.  The window handle is fetched FIRST (0x0042e740,
// binkvideo.cpp's dsi_GetMainWindow) and held in %edi across a re-init's
// Term(LTFALSE) and the font build (0x0041c5a0), and only filed at +0x3b4
// once the font has come up; a font failure tears everything down with
// Term(LTTRUE).  The rect is a struct copy into +0x27c.  The scrollback is
// sized to 200 lines only if it has none, the input line's first word is
// pointed at the console, and the history ring gets 20 entries if it has no
// usable room.  (name ours)
HWND dsi_GetMainWindow();                                   // 0x0042e740

LTBOOL CConsoleView::Init(LTRect* pRect, void (*pfnCommand)(char* pText),
                          uint32 nUnknown268, CConsoleNameList* pHistory)
{
	void* hWnd;

	hWnd = dsi_GetMainWindow();

	if (m_Unknown004)
		Term(LTFALSE);

	m_Unknown004 = 1;

	if (!InitFont())
	{
		Term(LTTRUE);
		return LTFALSE;
	}

	m_hMainWnd    = hWnd;
	m_rc27c       = *pRect;
	m_pfnCommand  = pfnCommand;
	m_Unknown268  = nUnknown268;
	m_nScrollPos  = 0;

	if (!m_Lines.GetNumLines())
		SetNumLines(200);

	m_Line.m_Unknown00 = (uint32)this;

	if (!m_History.GetMaxEntries())
		m_History.SetSize(20);

	if (pHistory)
		SetHistory(pHistory);

	RefreshFromVars();

	return LTTRUE;
}

// 0x0041c4c0 (26 bytes) — Init's scrollback step on its own: 200 lines if
// there are none, and always 1.  Nothing in the image calls it (no rel32
// call, no stored pointer), so it has no row in the start list; it is
// hidden in 0x0041c3e0's 250-byte row, whose real extent is 212 bytes
// (0x0041c3e0..0x0041c4b3, then twelve nops).  (name ours)
LTBOOL CConsoleView::InitLines()
{
	if (!m_Lines.GetNumLines())
		SetNumLines(200);

	return LTTRUE;
}

// 0x0041cba0 — point the console at a history, or at the do-nothing list at
// 0x004b2af0 when handed nothing.  The null arm is the FIRST block in the
// shipped code, which is what puts the test the way round it is written here.
void CConsoleView::SetHistory(CConsoleNameList* pHistory)
{
	if (!pHistory)
		m_pHistory = &g_EmptyNameList;
	else
		m_pHistory = pHistory;
}

// 0x0041c570 — drop the console's backdrop surface.  Its one caller
// (0x0041cd50) does this before loading console.pcx again.
void CConsoleView::FreeSurface()
{
	if (m_hSurface)
	{
		ci_DeleteSurface((HSURFACE)m_hSurface);
		m_hSurface = LTNULL;
	}
}

// ----------------------------------------------------------------------- //
//  0x0041c5a0 (830 bytes) -- build the console's bitmap font.
// ----------------------------------------------------------------------- //
//
// Between FreeSurface and Free29c in the shipped image, so this TU's; it
// builds cltdib.h's bitmap on its stack (0x478 frame: the 0x458-byte
// bitmap at +0x30 and eight dwords under it).  Its one caller is Init
// (0x0041c404).  It was first written in cltcursor.cpp, beside the bitmap
// class, where no shipped neighbour could place it.
//
// The font is a resource of type "#12345" (0x004a87f8) in the EXE's own
// module, its id the "ConsoleFont" console variable (con_FindVar 0x0041b3e0
// on the client's state at 0x004b2f00, then atoi 0x004937a7), else 0x8c --
// and 0x8c again if that id is not there.  The resource is an 8-bit PCX
// (the bitmap's format type must be 0): copied into a memory stream
// (0x00484170, chunk 0x100; Write +0x24, SeekTo +0x14), read by 0x0043df40,
// and cut into a 16 x 16 grid of cells.  Each cell row becomes one dword
// whose bit N is set when pixel N is non-zero; the dwords go to +0x29c,
// charH per character, and every character's width is the cell width.
// GetDC/ReleaseDC on the main window bracket the cut and nothing uses the
// DC.  (names ours)
//
// NEAR, 823 vs 830 bytes, 236 instructions against 238; the block layout,
// all six exits and their three destructor expansions, the rep-stos width
// fill and the four loops are shipped's.  The SizeofResource result is a
// named local: shipped calls it BEFORE loading the stream's vptr for Write
// (0x0041c6f7 / 0x0041c6fd); with the call nested in the argument list VC6
// loads the vptr first and spills it across the call (831 bytes, 25
// register-blind instruction differences; now 9).  What is left is a
// register permutation: shipped has this=%ebx, pData=%edi, pStream=%ebp
// (hence a 1-byte longer `mov 0x0(%ebp)` three times) and in the loops
// charH=%esi, charW=%ebx, pCell=%ebp, where charH's reload from its spill
// slot inside the row loop and pCell's `mov %eax,%ebp` are the two
// instructions ours does without.  Not moved by: local declaration order
// (fourteen shuffles, byte-identical output), charW/charH statement order,
// block-scoped row locals, an explicit ci counter (up or down x loop),
// pSrc[col] vs *pSrc++, pCell recomputed per cell or folded into pSrc, an
// int charW, the GetDC test folded into the assignment, the load test
// split in two, or cl 8168 / 8804 / 8966.
// Declared as their definitions are: clientstreams.cpp's growstream_Open
// (con_FindVar is servermgr.h's, dalloc is declared at the top).
ILTStream* growstream_Open(uint32 nWantedCache);                          // 0x00484170

LTBOOL CConsoleView::InitFont()
{
	CObj4a1ab4View bitmap;
	LTCommandVar* pVar;
	HRSRC hRes;
	HGLOBAL hGlobal;
	void* pData;
	ILTStream* pStream;
	HDC hDC;
	LTBOOL bRet;
	int nFontID;
	uint32 charW, charH;
	uint32 x, y, row, col, ci;
	uint8 *pCell, *pSrc;
	uint32 *pOut;
	uint32 bits, mask;
	DWORD nSize;

	if (!m_CS.IsValid())
		return LTFALSE;

	nFontID = 0x8c;
	pVar = (LTCommandVar*)con_FindVar(&g_ClientConsoleState, "ConsoleFont");
	if (pVar)
		nFontID = atoi(pVar->m_pStringVal);

	hRes = FindResourceA(NULL, MAKEINTRESOURCE(nFontID), "#12345");
	if (!hRes)
	{
		hRes = FindResourceA(NULL, MAKEINTRESOURCE(0x8c), "#12345");
		if (!hRes)
			return LTFALSE;
	}

	hGlobal = LoadResource(NULL, hRes);
	if (!hGlobal)
		return LTFALSE;

	pData = LockResource(hGlobal);
	if (!pData)
		return LTFALSE;

	pStream = growstream_Open(0x100);
	if (!pStream)
		return LTFALSE;

	nSize = SizeofResource(NULL, hRes);
	pStream->Write(pData, nSize);
	pStream->SeekTo(0);

	bRet = LTFALSE;

	if (dib_LoadQ41C(pStream, &bitmap) && bitmap.m_Format.m_nBitDepth == 0)
	{
		hDC = GetDC((HWND)m_hMainWnd);
		if (hDC)
		{
			charW = bitmap.m_Unknown438 >> 4;
			charH = bitmap.m_Unknown43c >> 4;

			m_pCharBits = (uint32*)dalloc(charH << 10);
			if (m_pCharBits)
			{
				m_nCharStride = (uint16)charH;
				m_nFontHeight = (uint16)charH;

				for (ci = 0; ci < 127; ci++)
					m_CharWidths[ci] = (uint16)charW;

				for (y = 0; y < 16; y++)
				{
					pCell = bitmap.m_Data.GetArray() + bitmap.m_Unknown440 * y * charH;

					for (x = 0; x < 16; x++)
					{
						pOut = &m_pCharBits[m_nCharStride * (y * 16 + x)];

						for (row = 0; row < charH; row++)
						{
							pSrc = pCell + row * bitmap.m_Unknown440;
							bits = 0;
							mask = 1;

							for (col = 0; col < charW; col++)
							{
								if (*pSrc)
									bits |= mask;

								mask <<= 1;
								pSrc++;
							}

							*pOut++ = bits;
						}

						pCell += charW;
					}
				}

				bRet = LTTRUE;
			}

			ReleaseDC((HWND)m_hMainWnd, hDC);
		}
	}

	pStream->Release();

	return bRet;
}

// 0x0041c8e0 — the same for the font's row masks; a plain dfree, and the
// clear is inside the test rather than after it.
void CConsoleView::Free29c()
{
	if (m_pCharBits)
	{
		dfree(m_pCharBits);
		m_pCharBits = LTNULL;
	}
}


// ----------------------------------------------------------------------- //
//  The print forwarders
// ----------------------------------------------------------------------- //

// Six free functions over g_Console, each `mov $0x4b2af8,%ecx` and a
// thiscall.  What pins the two printers' argument order is the callee itself:
// 0x0041d320 reads its SECOND argument as the level it compares against
// CConsole+0x144 and its third and fourth as the format and the va_list, and
// the one caller that passes a literal (0x0041db60, "Error enabling device")
// passes 0x00ffffff and 1.  (names ours)

// 0x0041b8a0
void con_PrintColored(uint32 nColor, int nLevel, char* pFormat, ...)
{
	va_list ap;

	va_start(ap, pFormat);

	g_Console.VPrintf(nColor, nLevel, pFormat, ap);
}

// 0x0041b8c0 — the plain printer: white, level zero.
void con_Print(char* pFormat, ...)
{
	va_list ap;

	va_start(ap, pFormat);

	g_Console.VPrintf(0xffffff, 0, pFormat, ap);
}

// 0x0041d1c0 — add text to the scrollback, one ring node per '\n'-separated
// piece.  Under the console's critical section (0x00444180 / 0x00444190):
// hand the text to the hook at +0x3b0, echo it with OutputDebugString and
// "\n" (0x004a8820) when 0x004b327c is set, then, if the ring has any
// nodes, recycle the head node for each piece -- unlink it, fill it
// (colour at +0x08, strncpy of the piece to +0x0c, 0xff when it is the
// last), and link it back in at the tail.  The unlink and the re-link are
// the list's own slot bodies (0x0041d5a0, 0x0041d550) EXPANDED here; written
// out by hand they allocate differently (ebx/edi swapped, 23 blocks).  The
// `jbe` of the entry test is GetSize()'s uint32.  (names ours)

// 0x004b327c — the "TraceConsole" console variable; dsi.cpp defines it
// under the server's name.
extern int g_CV_TraceConsole;                                // 0x004b327c

// M-P0-02: the game's console hook (ILTClient::SetConsoleOutputFn, defined
// at the bottom of this file).
typedef void (*ModernConsoleOutputFn)(void* pUser, uint32 nColor, int nLevel,
                                      const char* pText);
extern ModernConsoleOutputFn g_pfnModernConsoleOutput;
extern void*                 g_pModernConsoleOutputUser;
void CConsoleView::Sub41d1c0(uint32 nColor, int nLevel, char* pText)
{
	char*             pEnd;
	int               nLen;
	CConsoleTextLine* pLine;

	if (!m_Unknown000 || nLevel > m_nLevel)
		return;

	m_CS.Enter();
	// M-P0-02: every console line also goes to the game DLL's console
	if (g_pfnModernConsoleOutput)
		g_pfnModernConsoleOutput(g_pModernConsoleOutputUser, nColor, nLevel, pText);

	if (m_Unknown3b0)
		((void (*)(char*))m_Unknown3b0)(pText);

	if (g_CV_TraceConsole)
	{
		OutputDebugString(pText);
		OutputDebugString("\n");
	}

	if (m_Lines.GetSize() > 0)
	{
		do
		{
			pEnd = strchr(pText, '\n');
			if (pEnd && pEnd[1])
			{
				nLen = pEnd - pText;
			}
			else
			{
				pEnd = LTNULL;
				nLen = 0xff;
			}

			pLine = m_Lines.m_pLines;
			m_Lines.Remove(pLine);

			pLine->m_nColor = nColor;
			strncpy(pLine->m_szText, pText, nLen);

			if (pEnd)
			{
				pLine->m_szText[nLen] = 0;
				pText = pEnd + 1;
			}

			m_Lines.Add(pLine);
		}
		while (pEnd);
	}

	m_CS.Leave();
}

// 0x0041d320 — format and add one line, if its level passes the console's
// threshold (`cmp 0x144(%esi)` / `jg` past everything).  A 500-byte buffer
// (`sub $0x1f4,%esp`), _vsnprintf (0x0049318e) capped at 0x1f3, then
// 0x0041d1c0 with (colour, level, text).
void CConsoleView::VPrintf(uint32 nColor, int nLevel, char* pFormat, va_list ap)
{
	char szBuf[500];

	if (nLevel <= m_nLevel)
	{
		_vsnprintf(szBuf, sizeof(szBuf) - 1, pFormat, ap);
		Sub41d1c0(nColor, nLevel, szBuf);
	}
}

// 0x0041d380 — the same again on a console passed in rather than the global.
void con_PrintOn(CConsoleView* pConsole, uint32 nColor, int nLevel,
                 char* pFormat, ...)
{
	va_list ap;

	va_start(ap, pFormat);

	pConsole->VPrintf(nColor, nLevel, pFormat, ap);
}

// 0x0041d3a0 — the console's key handler (con_Sub41b8e0 hands it every key).
// A switch over VK 0x09..0x28: the byte index table at 0x0041d4f8 maps TAB,
// RETURN, PRIOR, NEXT, UP and DOWN onto six arms and everything else onto
// the default, which leaves any editing mode and hands the key to the input
// line (+0x10).  The arms are laid out RETURN, UP, DOWN, TAB, PRIOR, NEXT,
// default -- the source order.  Every modifier test is VK_CONTROL (0x11),
// the TAB arm's second one included: shipped pushes $0x11 twice (0x0041d427,
// 0x0041d43c), so control-TAB lists and the "previous" branch is dead.
// The callees are ccline.cpp's CcnConsole methods.
void CConsoleView::Sub41d3a0(int nKey)
{
	switch (nKey)
	{
		case VK_RETURN:
			OnEnter();
			return;

		case VK_UP:
			if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
				Scroll(1);
			else
				SearchHistory(-1);
			return;

		case VK_DOWN:
			if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
				Scroll(-1);
			else
				SearchHistory(1);
			return;

		case VK_TAB:
			if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
				ListMatches();
			else if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
				CompletePrev();
			else
				CompleteNext();
			return;

		case VK_PRIOR:
			if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
				Scroll(m_Lines.m_nNumLines);
			else
				Scroll(m_nNumVisible);
			return;

		case VK_NEXT:
			if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
				Scroll(-m_nScrollPos);
			else
				Scroll(-m_nNumVisible);
			return;

		default:
			ClearMode();
			m_Line.HandleKey(nKey);
			return;
	}
}

// 0x0041b820 — Term (0x0041c4e0) on the global console, the argument passed
// straight through.  Byte-for-byte the same masked shape as 0x0041b8e0
// below (only the callee differs), so without this body a
// greedy placement hands 0x0041b820 to con_Sub41b8e0 and leaves 0x0041b8e0
// unclaimed.  Callers: clientmgr2.cpp (0) and clientmgr3.cpp (1).
void con_Sub41b820(int nStage)
{
	g_Console.Term(nStage);
}

// 0x0041b830 -- `mov $0x4b2af8,%ecx; jmp 0x0041c4c0`: InitLines on the
// global console, tail-called.  Caller: clientmgr3.cpp's CClientMgr::Init.
void con_Sub41b830()
{
	g_Console.InitLines();
}

// 0x0041b840 -- `mov $0x4b2af8,%ecx; jmp 0x0041cd50`: reload console.pcx
// on the global console (LoadBackground, written in cltcursor.cpp),
// tail-called.  Caller: clientrender.cpp (after the renderer is restarted).
void con_Sub41b840()
{
	g_Console.LoadBackground();
}

// 0x0041b850 — three arguments through to Init (0x0041c3e0) with a fourth
// of zero.
void con_Sub41b850(int p1, int p2, int p3)
{
	g_Console.Init((LTRect*)p1, (void (*)(char*))p2, (uint32)p3, LTNULL);
}

// 0x0041b870
void con_Sub41b870(int nValue)
{
	g_Console.m_Unknown3b0 = nValue;
}

// 0x0041b880
void con_Sub41b880(int p1, int p2, int p3)
{
	g_Console.Sub41d1c0((uint32)p1, p2, (char*)p3);
}

// 0x0041b8e0
void con_Sub41b8e0(int p1)
{
	g_Console.Sub41d3a0(p1);
}


// The TU's last three file-scope objects, whose initialiser groups sit just
// in front of the first command handler (0x0041d640) -- which is where the
// definitions go.
//
// 0x004b2f00 — the client's console state, and the CClientMgr pointer after
// it.  Defined here, once; every other spelling of the two addresses is an
// extern of these.  ConsoleState's LTLink member has an empty inline
// constructor, so VC6 emits an initialiser that does nothing: the image has
// it, 0x0041d5e0 (`jmp 0x0041d5f0`) onto the bare `ret` at 0x0041d5f0.
ConsoleState    g_ClientConsoleState;                       // 0x004b2f00
CClientMgrView* g_pConsoleClientMgr;                        // 0x004b2f44

// 0x004b2ee8 — the completion enumerator (initialiser group 0x0041d600).
CConsoleCompletion g_ConsoleCompletion;                      // 0x004b2ee8


// ----------------------------------------------------------------------- //
//  Console command handlers
// ----------------------------------------------------------------------- //
//
// Every one of these is cdecl (int argc, char** argv) and argv does NOT carry
// the command name -- `World foo` reaches cc_World with argc 1 and argv[0]
// "foo", the same convention src/engine/server/console.cpp's cc_World has.
// The names are ours except where the usage string supplies one.

// 0x0041d640 — "ModelAdd <r> <g> <b>": three atof()s into g_ModelAdd, each
// component then clamped to [0, 250] (0x004a14d8 = 0.0f, 0x004a1eb8 =
// 250.0f; the high store is the immediate 0x437a0000).  Fewer than three
// arguments does nothing.
void cc_ModelAdd(int argc, char** argv)
{
	if (argc >= 3)
	{
		g_ModelAdd.x = (float)atof(argv[0]);
		g_ModelAdd.y = (float)atof(argv[1]);
		g_ModelAdd.z = (float)atof(argv[2]);

		if (g_ModelAdd.x < 0.0f)
			g_ModelAdd.x = 0.0f;
		else if (g_ModelAdd.x > 250.0f)
			g_ModelAdd.x = 250.0f;

		if (g_ModelAdd.y < 0.0f)
			g_ModelAdd.y = 0.0f;
		else if (g_ModelAdd.y > 250.0f)
			g_ModelAdd.y = 250.0f;

		if (g_ModelAdd.z < 0.0f)
			g_ModelAdd.z = 0.0f;
		else if (g_ModelAdd.z > 250.0f)
			g_ModelAdd.z = 250.0f;
	}
}

// 0x0041d740 — "ModelDirAdd <r> <g> <b>": the same into g_ModelDirAdd.
void cc_ModelDirAdd(int argc, char** argv)
{
	if (argc >= 3)
	{
		g_ModelDirAdd.x = (float)atof(argv[0]);
		g_ModelDirAdd.y = (float)atof(argv[1]);
		g_ModelDirAdd.z = (float)atof(argv[2]);

		if (g_ModelDirAdd.x < 0.0f)
			g_ModelDirAdd.x = 0.0f;
		else if (g_ModelDirAdd.x > 250.0f)
			g_ModelDirAdd.x = 250.0f;

		if (g_ModelDirAdd.y < 0.0f)
			g_ModelDirAdd.y = 0.0f;
		else if (g_ModelDirAdd.y > 250.0f)
			g_ModelDirAdd.y = 250.0f;

		if (g_ModelDirAdd.z < 0.0f)
			g_ModelDirAdd.z = 0.0f;
		else if (g_ModelDirAdd.z > 250.0f)
			g_ModelDirAdd.z = 250.0f;
	}
}

// 0x0041d840 — "World <world name>": start a world from the client console.
// The usage line is the ELSE arm, not a guard: the shipped code puts the
// start call first and jumps back to the message.
void cc_World(int argc, char** argv)
{
	if (argc >= 1)
		cc_DoStartWorld(argv[0], LTNULL);
	else
		con_ConsolePrint("World <world name>");
}

// 0x0041d870 — start a world by name, optionally recording a demo.
//
// The name is tried as given and then under worlds\ (the file manager is
// asked for "<name>.dat" each time, file type 0, flag 0); when only the
// worlds\ form exists the request carries that path.  With a record file
// and a connection already in a normal or hosted game, the world is changed
// by sending "world <name>" down the connection instead of starting a game
// (ltbasedefs.h: a recording server must be told to load the world).
//
// WHAT PINS IT
//   * the request is ltbasedefs.h's StartGameRequest: its constructor's
//     stores (0x0041d88d..0x0041d8d7, SDK order) then `rep stos` of 0x47e
//     dwords over the same 0x11f8 bytes -- constructed, then memset.
//   * the record name is `strncpy(.., 99)` into +0xcc (m_RecordFilename)
//     then byte 99 zeroed; the world name `strncpy(.., 100)` into +0x04, no
//     terminator; m_Type is stored 4 again just before StartGame.
//   * the second lookup stores only m_pFilename (0x0041d948): m_nFileType
//     is set once.
void cc_DoStartWorld(char* pWorldName, char* pRecordFilename)
{
	CClientFileRefView     ref;
	char                   szFile[200];
	char                   szCmd[200];
	StartGameRequest       req;
	CClientConnectionView* pConn;

	memset(&req, 0, sizeof(req));

	sprintf(szFile, "%s.dat", pWorldName);
	ref.m_pFilename = szFile;
	ref.m_nFileType = 0;

	if (!fm_GetFileRec(g_pConsoleClientMgr->m_pFileMgr, &ref, 0))
	{
		sprintf(szFile, "worlds\\%s.dat", pWorldName);
		ref.m_pFilename = szFile;

		if (fm_GetFileRec(g_pConsoleClientMgr->m_pFileMgr, &ref, 0))
		{
			sprintf(szFile, "worlds\\%s", pWorldName);
			pWorldName = szFile;
		}
	}

	if (pRecordFilename)
	{
		strncpy(req.m_RecordFilename, pRecordFilename, MAX_SGR_STRINGLEN - 1);
		req.m_RecordFilename[MAX_SGR_STRINGLEN - 1] = 0;
	}

	pConn = g_pConsoleClientMgr->m_pConnection;

	if (pConn &&
	    (pConn->m_nGameMode == STARTGAME_NORMAL || pConn->m_nGameMode == STARTGAME_HOST) &&
	    req.m_RecordFilename[0])
	{
		sprintf(szCmd, "world %s", pWorldName);
		pConn->RunConsoleString(szCmd);
		return;
	}

	req.m_Type = STARTGAME_NORMAL;
	strncpy(req.m_WorldName, pWorldName, MAX_SGR_STRINGLEN);
	g_pConsoleClientMgr->StartGame(&req);
}

// 0x0041db60 — enable an input device by name.  The call is a plain function
// pointer off CClientInput (+0x24, EnableDevice), handed the table itself,
// which clientmgr.h already lays out; the failure report is the coloured
// printer at level one.
void cc_EnableDevice(int argc, char** argv)
{
	CClientInput* pInput;

	if (argc < 1)
		return;

	pInput = g_pConsoleClientMgr->m_pInput;

	if (!pInput->EnableDevice(pInput, argv[0]))
		con_PrintColored(0xffffff, 1, "Error enabling device: %s", argv[0]);
}

// 0x0041de20 — set the screenshot name.  The destination is the char array at
// 0x004a9f18, which ships holding "Screenshot"; the copy is VC6's inline
// strcpy (a `repnz scasb` then `rep movsl`), so the source is strcpy and not
// strncpy.
void cc_Screenshot(int argc, char** argv)
{
	if (argc >= 1)
		strcpy(g_szScreenshotName, argv[0]);
}

// 0x0041de60 — one integer console command: atoi straight into a global.
void cc_Sub41de60(int argc, char** argv)
{
	if (argc >= 1)
		g_nUnknown4a9efc = atoi(argv[0]);
}

// 0x0041e130 — free the client manager's 0x1110 list, if there is a manager.
void cc_Sub41e130(int argc, char** argv)
{
	if (g_pConsoleClientMgr)
		cm_FreeList1110AndMore(g_pConsoleClientMgr);
}

// 0x0041e340 — every command in the history, then how many there were.
//
// The count is only printed when the history had a first entry at all, which
// is what the early return says; the walk itself is the FindFirst/GetName/
// FindNext trio through the vtable, and GetName failing ends it as surely as
// FindNext does.
void cc_ListHistory(int argc, char** argv)
{
	int   nCount;
	char* pName;

	// Through a POINTER, not the object: `g_Console.m_History.FindFirst()` lets
	// VC6 bind statically and emit a direct call, where the shipped code
	// keeps the virtual dispatch and folds `this` to an immediate --
	// `mov 0x4b2ec4,%eax; mov $0x4b2ec4,%ecx; call *0x14(%eax)`.  That pair
	// is what a compile-time-known pointer to a polymorphic object gives.
	CConsoleHistory* pHist = &g_Console.m_History;

	if (!pHist->FindFirst(LTNULL))
		return;

	nCount = 0;

	while ((pName = pHist->GetName()) != LTNULL)
	{
		con_ConsolePrint(pName);

		nCount++;

		if (!pHist->FindNext(LTNULL))
			break;
	}

	con_ConsolePrint("%d commands", nCount);
}

// 0x0041e3b0 — forget the history.  The three fields go down in cursor,
// write-position, oldest order and the storage is left alone, so the ring is
// reused rather than freed.
void cc_ClearHistory(int argc, char** argv)
{
	g_Console.m_History.m_nCur = 0;
	g_Console.m_History.m_nNext = 0;
	g_Console.m_History.m_nFirst = 0;

	con_ConsolePrint("Command history cleared.");
}

// 0x0041e810 — the name of every built-in console command.  The count and the
// table are both re-read each time round, which is what a printer that could
// touch the console state gives.
void cc_ListCommands(int argc, char** argv)
{
	int i;

	for (i = 0; i < g_ClientConsoleState.m_nBuiltins; i++)
		con_Print(g_ClientConsoleState.m_pBuiltins[i].m_pName);
}

// 0x0041e840 — every console variable, as the console shows it.
//
// The server's twin (0x10028b47, src/engine/server/console.cpp) prints a
// banner first and guards on the manager; the client's does neither, and the
// walk step is its own statement for the same reason it is there: nested
// inside the GetKey/GetData call, VC6's right-to-left argument order hoists it
// above the advance.
void cc_ShowGameVars(int argc, char** argv)
{
	void* hIter;
	void* hElement;

	hIter = dhash_GetFirst(g_ClientConsoleState.m_hVarHash);

	if (!hIter)
		return;

	do
	{
		hElement = dhash_GetNext(&hIter);

		if (hElement)
			con_PrintVar(&g_ClientConsoleState, (LTCommandVar*)dhash_GetData(hElement));
	}
	while (hIter);
}

// 0x0041e7a0 — one of the console state's config-file writers: the two model
// offset vectors, as the console commands that would set them again.
//
// The float arguments are promoted to double for the variadic call (`flds`
// then `fstpl`), and the two vectors are at 0x004b3228 and 0x004b3218 -- the
// second one printed is the LOWER address.
void cc_SaveModelVars(FILE* pFile)
{
	fprintf(pFile, "ModelAdd %f %f %f\n",
	        g_ModelAdd.x, g_ModelAdd.y, g_ModelAdd.z);
	fprintf(pFile, "ModelDirAdd %f %f %f\n",
	        g_ModelDirAdd.x, g_ModelDirAdd.y, g_ModelDirAdd.z);
}


// ======================================================================= //
//  ALREADY SOURCED — src/engine/server/console.cpp, compiled into
//  lithtech.exe as well.  DO NOT rewrite any of these.
// ======================================================================= //
//
// The block 0x0041ac20..0x0041b7c0 is that file's code at the client's
// addresses.  Seven of them are not yet written:
//
//   0x0041ac20  con_RunStringEx     (server 0x10005623)
//   0x0041af00  con_InternString    (server 0x10005892)
//   0x0041b070  con_FindBinding     (server 0x100059ad)  -- identical, see below
//   0x0041b0f0  con_SetVar          (server 0x100059ec)
//   0x0041b2f0  con_AddCommand      (server 0x10005b91)
//   0x0041b4db  *mid-body of con_SaveFile, which starts at 0x0041b4c0*
//   0x0041b710  parse_Token         (server 0x10005e24)
//
// and their already-identified neighbours, for the record, are
// 0x0041abe0 con_RunString, 0x0041ac00 con_RunString3, 0x0041af60
// con_CreateVar, 0x0041b010 con_ApplyBinding, 0x0041b190 con_Init,
// 0x0041b200 con_Term, 0x0041b340 con_RemoveCommand, 0x0041b3e0
// con_FindVar, 0x0041b420 con_RunStringEx's caller (clientmgr2.cpp's
// con_ParseCommands), 0x0041b4c0 con_SaveFile, 0x0041b570 con_PrintVar,
// 0x0041b590 CommandParser::Next and 0x0041b680 parse_Command.
//
// What names each: con_FindBinding walks 0x14-byte records off +0x08 with the
// count at +0x0c, folding case as it compares -- ConsoleBinding and
// con_ICompare exactly; con_AddCommand allocates 0x18 through m_pAlloc at
// +0x28 and links &pCmd->m_Link into &pState->m_CommandHead at +0x18 with
// m_pData at +0x10; con_SaveFile writes `"%s" "%s"\n` from m_pName/+0x10 and
// m_pStringVal/+0x14 for every variable whose +0x20 flags have bit 0, then
// runs the m_pSavers list; con_PrintVar pushes "%s = %s" through m_pPrint at
// +0x24.
//
// ONE THING WORTH CHECKING, NOT CHANGING.  console.cpp's comment on
// con_AddCommand records a 61-of-63 near in server.dll and blames the ORDER
// the two insert arguments are formed: the shipped server builds
// `&pState->m_CommandHead` first and ours builds `&pCmd->m_Link` first.  The
// shipped LITHTECH build at 0x0041b2f0 forms them in OUR order --
// `lea 0x8(%eax),%ecx` at +0x27 before `lea 0x18(%esi),%edx` at +0x2a -- so
// the same source may well be byte-exact at /O2 even though it is near at
// /O1.  Worth a `build/engine.sh lithtech-one console.cpp con_AddCommand`.
//
//
// 0x0041b070 con_FindBinding -- IDENTICAL.  The lithtech body has
// con_ICompare EXPANDED in its loop where server.dll's calls 0x10005851, so
// console.cpp's con_ICompare is `inline` (/O1 declines, /O2 expands).  The
// four-instruction residual (which of p1/p2 is walked) was the `c1` local:
// reading `*p1` at both tests walks p1 as shipped.  console.obj then emits no
// out-of-line copy in this image, which is also what the shipped layout says
// -- 0x0042ee70 is another TU's, written at the bottom of this file.
//
//
// ALSO FREE, FROM THE SDK.  Three _CVector<float> inlines from
// src/LT2/sdk/inc/ltvector.h, emitted out of line.
// None is redefined here -- referencing them from a caller is what makes VC6
// emit the header's own COMDAT:
//
//   0x0041ab50  _CVector<float>::Init(float,float,float)    (`ret $0xc`,
//               three integer stores -- VC6 copies a float parameter into a
//               float member with `mov`)
//   0x0041ab70  _CVector<float>::operator*(float) const     (`ret $0x8`: the
//               hidden return pointer plus the scalar, three `fmuls` into a
//               0xc-byte temp and an integer copy out)
//   0x0041abc0  _CVector<float>::operator+=(const _CVector<float>)  (`ret
//               $0xc` -- the argument is BY VALUE, three floats on the stack)


// ======================================================================= //
//  0x0040c4b0 — ILTClient::DeleteSurface
// ======================================================================= //
//
// HOME: the client interface's surface TU (src/engine/lithtech/cissurf.cpp,
// the cis_ run 0x0040c000..0x0040f2ff).  Written here because this file is
// the one that names it; the views below are local and suffixed so they
// cannot collide with cissurf.cpp's when the body is moved.
//
// Both refusals are de_error.h's RETURN_ERROR_INFO at level 1: the code is
// reported by name ("LT_ERROR", 0x004a7360) and the description is the
// reason ("(null surface)" 0x004a8110, "(renderer not initialized)"
// 0x004a80d8), with "ILTClient::DeleteSurface" 0x004a80f4 as the name.
//
// The screen surface (0x004b1620) is never freed.  Anything else is unlinked
// from the circular surface list (+0x00 prev, +0x04 next; head 0x004b17a0,
// count 0x004b179c), its system copy and renderer data are released
// (0x0040c5d0 then 0x0040c5a0, cissurf.cpp's order) and it goes back on the
// surface bank's free list (0x004b1600 + 0x18), stdlith's sb_Free expanded.
class CSurfaceM12
{
public:
	CSurfaceM12* m_pPrev;                    // 0x00  (the bank's free link once freed)
	CSurfaceM12* m_pNext;                    // 0x04
};

class CStructBankM12
{
public:
	uint8 m_pad_00[0x18];
	void* m_pFreeList;                       // 0x18
};

// The surface list: cissurf.cpp's CGLinkedListQ40C at 0x004b1798, whose
// count (+0x04) and head (+0x08) are the two words this body keeps.
class CSurfaceListM12
{
public:
	void*        m_pVTable;                  // 0x00
	uint32       m_nElements;                // 0x04  0x004b179c
	CSurfaceM12* m_pHead;                    // 0x08  0x004b17a0
};

// The objects themselves are cissurf.cpp's, under its names (it is their
// home TU): the views above are laid over them, not separate globals.
class RenderStructQ40C;
class CSurfaceQ40C;
class CGLinkedListQ40C;
class CStructBankQ40C;
extern RenderStructQ40C* g_pRender;                         // 0x004b17a4
extern CSurfaceQ40C      g_ScreenSurface;                   // 0x004b1620
extern CGLinkedListQ40C  g_SurfaceListQ40C;                 // 0x004b1798
extern CStructBankQ40C   g_SurfaceBankQ40C;                 // 0x004b1600

#define g_ScreenSurfaceM12 (*(CSurfaceM12*)&g_ScreenSurface)
#define g_SurfaceListM12   (*(CSurfaceListM12*)&g_SurfaceListQ40C)
#define g_SurfaceBankM12   (*(CStructBankM12*)&g_SurfaceBankQ40C)

void cis_FreeSysMemM12(CSurfaceM12* pSurface);              // 0x0040c5d0
void cis_FreeRenderDataM12(CSurfaceM12* pSurface);          // 0x0040c5a0

LTRESULT ci_DeleteSurface(HSURFACE hSurface)
{
	CSurfaceM12* pSurface = (CSurfaceM12*)hSurface;

	if (!pSurface)
		RETURN_ERROR_INFO(1, "ILTClient::DeleteSurface", LT_ERROR, "(null surface)");

	if (!g_pRender)
		RETURN_ERROR_INFO(1, "ILTClient::DeleteSurface", LT_ERROR, "(renderer not initialized)");

	if (pSurface != &g_ScreenSurfaceM12)
	{
		pSurface->m_pPrev->m_pNext = pSurface->m_pNext;
		pSurface->m_pNext->m_pPrev = pSurface->m_pPrev;

		if (pSurface == g_SurfaceListM12.m_pHead)
			g_SurfaceListM12.m_pHead = g_SurfaceListM12.m_pHead->m_pNext;

		g_SurfaceListM12.m_nElements--;

		if (g_SurfaceListM12.m_nElements == 0)
			g_SurfaceListM12.m_pHead = LTNULL;

		cis_FreeSysMemM12(pSurface);
		cis_FreeRenderDataM12(pSurface);

		*(void**)pSurface = g_SurfaceBankM12.m_pFreeList;
		g_SurfaceBankM12.m_pFreeList = pSurface;
	}

	return LT_OK;
}


// ======================================================================= //
//  0x0041b370 — con_FindCommand
// ======================================================================= //
//
// HOME: src/engine/server/console.cpp, between con_RemoveCommand (0x0041b340)
// and con_FindVar (0x0041b3e0) -- the shipped order puts it there, and it
// walks console.cpp's own command list (&pState->m_CommandHead at +0x18,
// each link's m_pData the ConsoleCommand).  server.dll has no copy: nothing
// on the server looks a command up by name, so the linker dropped it.
// Written here rather than in console.cpp; clientmgr.h
// declares it and clientinterface.cpp calls it.
//
// The compare is con_ICompare EXPANDED (0x0041b391..0x0041b3bf), walking the
// command's name in %esi and indexing the argument off the difference, which
// is what console.cpp's `inline` con_ICompare gives.  This TU's own
// con_ICompare below is the out-of-line 0x0042ee70 and must stay a plain
// definition, so the inline copy here carries its own name.
inline char con_FindCommandToUpper(char c)
{
	if (c >= 'a' && c <= 'z')
		return (char)(c - ('a' - 'A'));

	return c;
}

inline LTBOOL con_FindCommandCompare(char* p1, char* p2)
{
	for (;;)
	{
		if (con_FindCommandToUpper(*p1) != con_FindCommandToUpper(*p2))
			return LTFALSE;

		if (*p1 == 0)
			return LTTRUE;

		p1++;
		p2++;
	}
}

ConsoleCommand* con_FindCommand(ConsoleState* pState, char* pName)
{
	LTLink* pCur;

	for (pCur = pState->m_CommandHead.m_pNext; pCur != &pState->m_CommandHead;
	     pCur = pCur->m_pNext)
	{
		ConsoleCommand* pCmd = (ConsoleCommand*)pCur->m_pData;

		if (con_FindCommandCompare(pCmd->m_pName, pName))
			return pCmd;
	}

	return LTNULL;
}


// ======================================================================= //
//  0x0042ee70 — con_ICompare, out of line
// ======================================================================= //
//
// HOME: not this file, and not console.cpp either.  In lithtech.exe
// console.cpp's own callers (con_FindBinding 0x0041b070, con_RunStringEx's
// command walk at 0x0041ac89) have the compare EXPANDED, so console.cpp's
// definition is `inline` and at /O2 console.obj emits no copy of it.  The
// out-of-line one that clientworld.cpp (0x00423ca3), partfm.cpp (0x0045c6f4,
// 0x0045c72b) and cmodelload.cpp (0x00477a94, 0x00477acb) call sits between
// the texture module (ends 0x0042ee6a) and clientdsi.cpp's surface effects
// (from 0x0042eec0) -- another TU's plain definition, which is also why
// those three callers see only a declaration and call it.  The TU is not
// identified, so the body is written here, the same text as console.cpp's
// (server.dll's out-of-line 0x10005851 is that one at /O1).
inline char con_ToUpper(char c)
{
	if (c >= 'a' && c <= 'z')
		return (char)(c - ('a' - 'A'));

	return c;
}

LTBOOL con_ICompare(char* p1, char* p2)
{
	for (;;)
	{
		if (con_ToUpper(*p1) != con_ToUpper(*p2))
			return LTFALSE;

		if (*p1 == 0)
			return LTTRUE;

		p1++;
		p2++;
	}
}
// ======================================================================= //
//  The modern build's additions to ILTClient (NOLFSRC_MODERN only)
// ======================================================================= //
//
// src/LT2/sdk/inc/iltclient.h declares these three as virtual methods appended
// after EndQuery, the last retail slot, so every retail slot keeps its
// place. lithtech.exe's ILTClient vtable is not compiled from a class but
// assembled by tools/mkvtabobj.py from link/vtables.txt, whose
// "%ifdef NOLFSRC_MODERN" block names the three members below as slots 113
// to 115. They are plain (non-virtual) thiscall members of a class of their
// own: `this` is the CLTClient object, which none of them reads.

typedef void (*ModernConsoleCommandEnumFn)(void* pUser, const char* pName,
                                           uint32 nFlags);

ModernConsoleOutputFn g_pfnModernConsoleOutput;
void*                 g_pModernConsoleOutputUser;

class CLTClientModern
{
public:
	LTRESULT SetConsoleOutputFn(ModernConsoleOutputFn pFn, void* pUser);
	LTRESULT EnumConsoleCommands(ModernConsoleCommandEnumFn pFn, void* pUser);
	LTRESULT SetCommandToggle(int nCommand, LTBOOL bToggle);
};

// M-P0-02: one callback (NULL to remove it), handed every line the console
// prints, after the console's own level filter: exactly the lines the
// engine's console keeps.  Called inside the console's
// critical section; it must not print to the console itself.
LTRESULT CLTClientModern::SetConsoleOutputFn(ModernConsoleOutputFn pFn, void* pUser)
{
	g_Console.m_CS.Enter();
	g_pfnModernConsoleOutput   = pFn;
	g_pModernConsoleOutputUser = pUser;
	g_Console.m_CS.Leave();

	return LT_OK;
}

// M-P2-04: every command the client console knows, in registration order.
// nFlags bit 0: registered by the game (RegisterConsoleProgram, which files
// it with a user word of 1); otherwise one of the engine's own.
LTRESULT CLTClientModern::EnumConsoleCommands(ModernConsoleCommandEnumFn pFn, void* pUser)
{
	LTLink* pCur;

	if (!pFn)
		return LT_INVALIDPARAMS;

	for (pCur = g_ClientConsoleState.m_CommandHead.m_pNext;
	     pCur != &g_ClientConsoleState.m_CommandHead; pCur = pCur->m_pNext)
	{
		ConsoleCommand* pCmd = (ConsoleCommand*)pCur->m_pData;

		if (pCmd && pCmd->m_pName)
			pFn(pUser, pCmd->m_pName, (pCmd->m_pUser == (void*)1) ? 1 : 0);
	}

	return LT_OK;
}

// M-P5-19: commands whose binding toggles them.  A toggled command is on
// from one press of its control to the next, instead of while the control
// is held; the frame's input is changed before anything reads it, so the
// game, a recorded demo and the server (retail or not) all see the
// toggled state.  Only live input is changed: while the input is off (a
// menu) every command reads off, as before, and the toggle is kept.
static uint8 s_aCmdToggle[255];		// toggled, not held
static uint8 s_aCmdHeld[255];		// its control, last frame
static uint8 s_aCmdLatched[255];	// toggled on

// Any call turns the command's latch off: a mode change, or a reset (the
// game calls it again with the same mode).
LTRESULT CLTClientModern::SetCommandToggle(int nCommand, LTBOOL bToggle)
{
	if (nCommand < 0 || nCommand >= 255)
		return LT_INVALIDPARAMS;

	s_aCmdToggle[nCommand]  = bToggle ? 1 : 0;
	s_aCmdLatched[nCommand] = 0;

	return LT_OK;
}

// demodtx.cpp's GatherInput, with a frame's live command states.
void ModernApplyCommandToggles(uint8* pActions)
{
	int i;

	for (i = 0; i < 255; i++)
	{
		if (!s_aCmdToggle[i])
			continue;

		if (pActions[i] && !s_aCmdHeld[i])
			s_aCmdLatched[i] = !s_aCmdLatched[i];

		s_aCmdHeld[i] = pActions[i] ? 1 : 0;
		pActions[i]   = s_aCmdLatched[i];
	}
}
