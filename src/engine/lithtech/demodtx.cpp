// *********************************************************************** //
//
//  MODULE:   W10_42C000 (.CPP)  — staging translation unit (name ours)
//
//  PURPOSE:  The writable rows of the 0x0042c000..0x0042f660 range of
//            lithtech.exe.  Everything below was read off
//            the retail lithtech.exe; every offset, constant and branch
//            carries the address it was read at.
//
//  THIS IS A STAGING FILE.  Three separate shipped translation units are
//  represented here and each block says where it belongs:
//
//      0x0042c050 / 0x0042c130 / 0x0042ca60   ->  the DEMO MANAGER, whose
//          other four bodies are already in
//          src/engine/lithtech/clientworld.cpp ("the demo manager
//          (0x42bd00..0x42ca50)", CDemoMgrView).
//
//      0x0042d3f0                             ->  the LOCKED LIST, whose
//          other six bodies are already in clientworld.cpp ("the locked
//          list at 0x42d210..0x42d760", CLockedItemView/CLockedOwnerView).
//
//      0x0042e830 / 0x0042edd0 / 0x0042f610 / 0x0042f640
//                                             ->  src/engine/lithtech/
//          clientdsi.cpp, which already declares TextureData and already
//          decodes (and declines) all four of these rows.
//
//  Every type I introduce carries a Q42C suffix so this object cannot
//  collide with the files above at link time.  On integration, DELETE the
//  duplicated view classes and keep only the bodies.
//
//  Build /O2 (/Og /Oi /Ot /Oy /Ob1), cl 8447, like the rest of
//  src/engine/lithtech.
//
// *********************************************************************** //

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <windows.h>
#include "ltbasedefs.h"
#include "ltlink.h"
#include "iltstream.h"

// windows.h renames engine methods out from under the SDK headers.
#undef PostMessage
#undef GetMessage
#undef PeekMessage
#undef CopyFile
#undef PlaySound
#undef GetClassName


// ======================================================================= //
//
//  SHARED DECLARATIONS
//
//  ILTStream's slot numbers, read off this run and cross-checked against
//  src/LT2/sdk/inc/iltstream.h.  The protected virtual destructor takes slot 0,
//  so Release is +0x04, Read +0x08, ReadString +0x0c, ErrorStatus +0x10,
//  SeekTo +0x14, GetPos +0x18, GetLen +0x1c, WriteStream +0x20,
//  Write +0x24, WriteString +0x28.  Four independent confirmations in this
//  range: 0x0042c050 writes through +0x24/+0x28 (Write/WriteString),
//  0x0042c130 reads through +0x08/+0x0c and releases through +0x04,
//  0x0042edd0 uses +0x08/+0x14/+0x18 (Read/SeekTo/GetPos) and 0x0042c2a0
//  releases through +0x04.  So the header's declaration order IS the
//  shipped slot order and no local view class is needed.
//
// ======================================================================= //

// 0x004290b0 / 0x004290c0 — servermgr.h's dalloc/dfree, one-argument
// forwarders onto 0x00428fd0 / 0x00429080.  The pair matters: the texture
// destructor below frees its mip data and its link records with the INNER
// function (0x00429080) and its +0xc8 buffer with `delete` (0x004290c0).
// See clientdsi.cpp's note, which establishes the two hops.
void  sub_429080(void* ptr);                             // 0x00429080

// 0x0042e6f0 — the CLIENT's dsi_ServerError; already defined in
// src/engine/lithtech/clientdsi.cpp, declared here only so 0x0042ca60 and
// 0x0042c130 can reach it.  Varargs, cdecl.
void dsi_ServerErrorQ42C(char* pFormat, ...);            // 0x0042e6f0

// 0x004297d0 — ci_DebugOut, already declared in clientinterface.h and in
// clientstreams.cpp.  Varargs, cdecl, formats into a 10000-byte buffer and
// calls OutputDebugStringA.
void ci_DebugOutQ42C(char* pMsg, ...);                   // 0x004297d0
void ModernApplyCommandToggles(uint8* pActions);         // M-P5-19, clientconsole.cpp


// ======================================================================= //
//
//  THE DEMO MANAGER — 0x0042bfa0..0x0042ca52
//
//  MOVE-ME: src/engine/lithtech/clientworld.cpp, section "the demo manager
//  (0x42bd00..0x42ca50)".  Clear (0x0042bfa0), IsRunning (0x0042bfd0),
//  SeedAndStart (0x0042bff0) and Forward58 (0x0042c020) are already there
//  as CDemoMgrView; the three bodies below are its missing entry points.
//
//  WHAT PINS THE MODULE (not the string pool — the CONSOLE VARIABLE TABLE):
//  the 20-byte records at 0x004aa008 and 0x004aa01c are
//      { "PlayDemoReps",    ..., 0x004b3238, ... }
//      { "PlayDemoProfile", ..., 0x004b323c, ... }
//  and 0x0042c6d0 reads exactly those two words at 0x0042c83e and
//  0x0042c7cd.  So the storage this run drives is the demo-playback
//  console state, which is what OWNS the code.
//
//  AND THE RETURN CODES CONFIRM IT INDEPENDENTLY: 0x0042c130 answers
//  0x45 = 69 = LT_INVALIDVERSION (ltcodes.h:248) on the version mismatch
//  at 0x0042c189, 1 = LT_ERROR on every failure and 0 = LT_OK on success.
//  Nothing was guessed about the semantics.
//
//  The fields, read off the four bodies here plus 0x0042c2a0/0x0042c5f0:
//
//      +0x00  uint32  frame counter      (0x0042c242 zeroes it)
//      +0x04  float   elapsed time       (0x0042ca0c stores it)
//      +0x08  float   start time, -1.0f  (0x0042c23b, 0x0042c5fa)
//      +0x0c  CClientMgr*                (0x0042c0bd and everywhere)
//      +0x10  ILTStream* the demo file   (0x0042c06f, 0x0042c149)
//      +0x14  ILTStream* a 0x100-chunk memory stream (0x0042c25a)
//      +0x18  uint32  mode: 1 record, 2 play (0x0042c10b, 0x0042c234)
//
// ======================================================================= //

// CClientMgr, only the fields this block reaches.  All five are already
// named by address elsewhere in src/engine/lithtech (clientmgr3.cpp puts
// m_DemoMgr itself at +0x158c, cchandler.cpp puts the file manager at
// +0x1534).  Offsets read at 0x0042c0c2 (+0x125c), 0x0042c0c8 (+0x1254),
// 0x0042c0f6 (+0x1258), 0x0042c101 (+0x14f4), 0x0042c214 (+0x14ac) and
// 0x0042c226 (+0x12ac).
class CDemoClientMgrQ42C
{
public:
	uint8  m_pad_0000[0x1254];
	float  m_fPrevTime;                      // +0x1254
	uint32 m_n1258;                          // +0x1258
	float  m_fCurTime;                       // +0x125c
	uint8  m_pad_1260[0x12ac - 0x1260];
	// Two 255-byte action-state buffers, double-buffered by the +0x14ac
	// index (GatherInput below reads them as [2][255]: 0x0042c46f's
	// `shl $8; sub` is the *255).
	union
	{
		uint8  m_Buf12ac[0x14ac - 0x12ac];   // +0x12ac, cleared 0xff bytes
		uint8  m_Actions[2][255];
	};
	uint32 m_n14ac;                          // +0x14ac  which buffer is current
	uint8  m_pad_14b0[0x14f4 - 0x14b0];
	uint32 m_n14f4;                          // +0x14f4
	uint8  m_pad_14f8[0x14fc - 0x14f8];
	float  m_fAxisOffsets[3];                // +0x14fc  (GatherInput)
	class InputMgrQC5* m_pInputMgr;          // +0x1508
	uint8  m_pad_150c[0x151c - 0x150c];
	uint32 m_n151c;                          // +0x151c
	uint32 m_n1520;                          // +0x1520  nonzero: do not read
};

class CDemoMgrQ42C
{
public:
	LTRESULT StartRecording(char* pShellName, char* pFilename);   // 0x0042c050
	LTRESULT StartPlayback(char* pFilename, char* pShellName,
	                       uint32 nMaxBytes);                     // 0x0042c130
	void     Stop();                                              // 0x0042c2a0
	~CDemoMgrQ42C();                                              // 0x0042bfc0
	void     Forward58(void* pA, void* pB);                       // 0x0042c020
	void     SeedAndStart();                                      // 0x0042bff0
	// 0x0042c6d0 -- the playback step.
	LTRESULT Sub42c6d0(int nFlag);                                // 0x0042c6d0
	LTRESULT GatherInput(int* pChanged, int* pnChanged, int* pOn,
	                     int* pnOn, LTBOOL bClear);               // 0x0042c310

public:
	uint32              m_nFrames;           // +0x00
	float               m_fElapsed;          // +0x04
	float               m_fStartTime;        // +0x08
	CDemoClientMgrQ42C* m_pClientMgr;        // +0x0c
	ILTStream*          m_pStream;           // +0x10
	ILTStream*          m_pMemStream;        // +0x14
	uint32              m_nMode;             // +0x18
};

// 0x00483f50 — fopen(pFilename, pMode) then `new` a 12-byte CFileStream
// around it; answers LTNULL if either step fails.  cdecl, two arguments.
ILTStream* sub_483f50Q42C(char* pFilename, char* pMode);          // 0x00483f50

// 0x00484440 — src/engine/lithtech/clientstreams.cpp already reconstructs
// this as growstream_OpenFile and already names 0x0042c141 (the call site
// below) as its only caller.  Declared, not redefined.
ILTStream* growstream_OpenFileQ42C(char* pFilename);              // 0x00484440

// 0x00484170 — clientstreams.cpp's other memory stream: `operator new(0x20)`,
// CBaseStream's vptr at +0x00 (0x004a374c) and a CMoArray at +0x04.  One
// cdecl argument, the chunk size.
ILTStream* sub_484170Q42C(uint32 nChunkSize);                     // 0x00484170


// ----------------------------------------------------------------------- //
// 0x0042bfc0 -- 5 bytes, `jmp 0x0042c2a0`: the destructor is Stop(),
// tail-called.  CClientMgr's destructor runs it for the member at +0x158c
// (0x004110c0).
// ----------------------------------------------------------------------- //
CDemoMgrQ42C::~CDemoMgrQ42C()
{
	Stop();
}


// ----------------------------------------------------------------------- //
// 0x0042c050 — 214 bytes, thiscall, `ret $8`.  Start recording.
//
// Read off the bytes:
//
//   * `mov %ecx,%esi; call 0x0042c2a0` with nothing pushed — Stop() on
//     `this`, the first statement.
//   * the OPEN takes the SECOND parameter (`mov 0x10(%esp),%eax` at
//     0x0042c059 with two registers pushed, i.e. entry [esp+8]) and the
//     mode string 0x004a9728, whose bytes are "wb\0".  The FIRST parameter
//     is what WriteString sends at 0x0042c0ba (`mov 0x10(%esp),%eax` at
//     0x0042c0b3 with three registers pushed).  So the argument order is
//     (name-to-record, filename) and not the other way round.
//   * THE VERSION AND THE SIXTEEN ZEROES are iltstream.h's WriteVal: its
//     by-value parameter, once inlined, lives in the DEAD SECOND PARAMETER
//     SLOT (`lea 0x14(%esp),%ecx` at 0x0042c081 with three pushes and no
//     frame is entry [esp+8], the filename, already consumed by the open),
//     and its store is scheduled AFTER the pushes, right before the call
//     (0x0042c08a, 0x0042c0a6).  A function-scope temporary got its own
//     `push %ecx` slot (217 bytes); block-scoped temporaries took the
//     parameter slot but stored ahead of the pushes (214, 6 blocks).
//   * `mov $0x10,%edi` / `dec %edi` / `jne` is a constant trip count, which
//     is what `for (i = 0; i < 16; i++)` collapses to when `i` is unused.
//   * ebx is the function's materialised zero (0x0042c068) and serves the
//     null test (`cmp %ebx,%eax`), the loop's stored zero, the two
//     CClientMgr clears and the literal 0 argument.  Don't read
//     it as an initialisation the source asked for.
//   * the failure block sits at 0x0042c074, immediately after the test and
//     ahead of the body, so the guard is the NEGATED early-return form.
//     There is only one `return LT_ERROR` here, so there is no shared exit
//     to re-host.
// ----------------------------------------------------------------------- //
LTRESULT CDemoMgrQ42C::StartRecording(char* pShellName, char* pFilename)
{
	uint32 i;

	Stop();

	m_pStream = sub_483f50Q42C(pFilename, "wb");     // 0x004a9728 == "wb"

	if (!m_pStream)
		return LT_ERROR;

	m_pStream->WriteVal((uint32)0xf0f0aabc);          // 0x0042c08a, slot +0x24

	for (i = 0; i < 16; i++)                          // 0x0042c095
		m_pStream->WriteVal((uint32)0);

	m_pStream->WriteString(pShellName);               // slot +0x28

	m_pClientMgr->m_fPrevTime = m_pClientMgr->m_fCurTime;          // 0x0042c0c8
	m_pStream->Write(&m_pClientMgr->m_fCurTime, 4);                // 0x0042c0dc
	m_pStream->Write(&m_pClientMgr->m_fPrevTime, 4);               // 0x0042c0ef
	m_pClientMgr->m_n1258 = 0;                                     // 0x0042c0f6
	m_pClientMgr->m_n14f4 = 0;                                     // 0x0042c101

	m_nMode = 1;                                                   // 0x0042c10b

	Forward58(m_pStream, 0);                                       // 0x0042c112
	SeedAndStart();                                                // 0x0042c119

	return LT_OK;
}


// ----------------------------------------------------------------------- //
// 0x0042c130 — 365 bytes, thiscall, `ret $0xc`.  Start playback.
//
// Read off the bytes:
//
//   * `sub $0x8,%esp` BEFORE the register saves: two dword locals.  Under
//     /Oy the bump allocator runs down from the top of the frame in
//     DECLARATION order, so entry-4 is the first-declared and entry-8 the
//     second.  entry-4 (`lea 0xc(%esp),%eax`, 0x0042c19e) is the sixteen
//     dummy reads; entry-8 (`lea 0x8(%esp),%ecx`, 0x0042c15f) is the
//     version.  Hence the declaration order below, which is NOT use order.
//   * the version literal is pushed a SECOND time at 0x0042c176 for the
//     error message, so the source spells 0xf0f0aabc at both sites rather
//     than naming it — two immediates, one comparison and one argument.
//   * `test %eax,%eax; je` on ReadString's answer at 0x0042c1c2 with the
//     Release arm falling through: a negated guard with the error inline.
//     `mov 0x10(%esi),%ecx` is hoisted ABOVE that branch because both arms
//     read m_pStream.
//   * THREE duplicated `mov $0x1,%eax` epilogues (0x0042c151, 0x0042c1cc,
//     0x0042c269) — several duplicated exits mean several
//     separate early returns, not one shared tail.
//   * `mov $0xbf800000,0x8(%esi)` is `m_fStartTime = -1.0f`: at /Ot VC6
//     stores a float constant as an integer immediate and never touches the
//     FPU (the ci_SetCameraFOV fingerprint).  0x0042c5fa compares the same
//     field against the same immediate, which is what confirms the field is
//     the float and the immediate is its bit pattern.
//   * `mov $0x3f,%ecx; rep stos; stos %ax; stos %al` is a CONSTANT-size
//     inline memset of 0x3f*4 + 2 + 1 = 0xff bytes at CClientMgr+0x12ac.
//   * `push $0x100` at 0x0042c200 is sub_484170's argument, scheduled
//     fifty bytes ahead of its call at 0x0042c252; esp is untouched in
//     between, which is what lets VC6 move it.
//   * CClientMgr is RELOADED from +0x0c before each of the five uses in
//     the tail (0x0042c1fd, 0x0042c20f, 0x0042c21e, 0x0042c231) —
//     repeated member loads mean the member is written out at
//     each use.
// ----------------------------------------------------------------------- //
LTRESULT CDemoMgrQ42C::StartPlayback(char* pFilename, char* pShellName,
                                     uint32 nMaxBytes)
{
	uint32 nTemp;                                    // entry-0x04
	uint32 nVersion;                                 // entry-0x08
	uint32 i;

	Stop();

	m_pStream = growstream_OpenFileQ42C(pFilename);

	if (!m_pStream)
		return LT_ERROR;

	m_pStream->Read(&nVersion, 4);                   // slot +0x08

	if (nVersion != 0xf0f0aabc)                      // 0x0042c16f
	{
		dsi_ServerErrorQ42C("Invalid version (%d, wanted %d)",
		                    nVersion, 0xf0f0aabc);   // 0x004a9974
		return LT_INVALIDVERSION;                    // 0x45
	}

	for (i = 0; i < 16; i++)                         // 0x0042c196
		m_pStream->Read(&nTemp, 4);

	if (m_pStream->ReadString(pShellName, nMaxBytes))     // slot +0x0c
	{
		m_pStream->Release();                            // slot +0x04
		return LT_ERROR;
	}

	m_pStream->Read(&m_pClientMgr->m_fCurTime, 4);        // 0x0042c1e6
	m_pStream->Read(&m_pClientMgr->m_fPrevTime, 4);       // 0x0042c1fa

	m_pClientMgr->m_n1258 = 0;                            // 0x0042c205
	m_pClientMgr->m_n14ac = 0;                            // 0x0042c214
	memset(m_pClientMgr->m_Buf12ac, 0, 0xff);             // 0x0042c22c

	m_nMode = 2;                                          // 0x0042c234
	m_fStartTime = -1.0f;                                 // 0x0042c23b
	m_nFrames = 0;                                        // 0x0042c242
	m_pClientMgr->m_n14f4 = 0;                            // 0x0042c248

	m_pMemStream = sub_484170Q42C(0x100);                 // 0x0042c252

	if (!m_pMemStream)
	{
		Stop();                                           // 0x0042c263
		return LT_ERROR;
	}

	Forward58(m_pMemStream, 0);                           // 0x0042c27a
	Forward58(m_pStream, (void*)1);                       // 0x0042c287
	SeedAndStart();                                       // 0x0042c28e

	return LT_OK;
}


// ----------------------------------------------------------------------- //
// 0x0042c2a0 — 105 bytes, thiscall, no arguments.  Stop recording or
// playback; the first statement of both Start functions above.
//
//   * recording (mode 1) ends the file with one byte, 2, written through
//     WriteVal((uint8)2): the byte temporary sits in the `push %ecx` slot
//     and its `movb $0x2` is scheduled after the pushes (0x0042c2bf), the
//     same inlined-WriteVal shape as StartRecording's.
//   * playback (mode 2) with a memory stream rewinds it (SeekTo(0), slot
//     +0x14) and hands it to Forward58 with 1.
//   * then each stream is Released (slot +0x04) and nulled, and the mode
//     cleared -- off the one zero register.
// ----------------------------------------------------------------------- //
void CDemoMgrQ42C::Stop()
{
	if (m_pStream)
	{
		if (m_nMode == 1)
		{
			m_pStream->WriteVal((uint8)2);
		}
		else if (m_nMode == 2 && m_pMemStream)
		{
			m_pMemStream->SeekTo(0);
			Forward58(m_pMemStream, (void*)1);
		}

		m_pStream->Release();
		m_pStream = LTNULL;
	}

	if (m_pMemStream)
	{
		m_pMemStream->Release();
		m_pMemStream = LTNULL;
	}

	m_nMode = 0;
}


// ----------------------------------------------------------------------- //
// 0x0042c310 -- 729 bytes, thiscall, `ret $0x14`.  One frame of input for
// the demo: played back off the stream in play mode, otherwise read live
// from the input manager (and written to the stream in record mode).  The
// caller is clientmgr3.cpp's UpdateCounters (0x00410860), which declares
// this as GatherCounters (name ours, from what it does).
//
//   * both counts are zeroed before anything else (0x0042c320).
//   * play mode asks 0x0042c6d0 for the next frame; LT_FINISHED (2) drops
//     through to the live read, any other failure is handed straight back
//     (0x0042c349 -> 0x0042c5df).
//   * a frame is the three axis offsets, a byte count of changed actions
//     (0xff and up refused) and that many action bytes (the same), each of
//     which TOGGLES the action in the first state buffer (`cmpb $0; sete`);
//     then every action that is on is listed.
//   * live: the other buffer is cleared, the input manager's +0x14 entry
//     (a cdecl function-pointer table, the manager first) fills it unless
//     +0x1520 says not to, and it is cleared again with the axes unless
//     +0x151c is set and bClear is not.  Actions that are on and actions
//     that differ from the current buffer are listed, and the index flips.
//   * record mode writes a 1 byte, the axes, the change count and the
//     changes, one byte each.
// ----------------------------------------------------------------------- //
class InputMgrQC5
{
public:
	uint8 m_pad_00[0x14];
	void  (*m_ReadInput)(InputMgrQC5* pMgr, uint8* pActions,
	                     float* pAxisOffsets);                    // +0x14
};

LTRESULT CDemoMgrQ42C::GatherInput(int* pChanged, int* pnChanged, int* pOn,
                                   int* pnOn, LTBOOL bClear)
{
	LTRESULT dResult;
	uint8    nChanges;
	uint8    nChange;
	uint8*   pOld;
	uint8*   pNew;
	int      i;

	*pnChanged = 0;
	*pnOn = 0;

	if (m_nMode == 2)
	{
		dResult = Sub42c6d0(1);

		if (dResult != LT_FINISHED)
		{
			if (dResult != LT_OK)
				return dResult;

			for (i = 0; i < 3; i++)
				m_pStream->Read(&m_pClientMgr->m_fAxisOffsets[i], 4);

			m_pStream->Read(&nChanges, 1);

			if (nChanges >= 0xff)
			{
				dsi_ServerErrorQ42C("DemoMgr: Invalid number of changes");
				return LT_ERROR;
			}

			for (i = 0; i < nChanges; i++)
			{
				m_pStream->Read(&nChange, 1);

				if (nChange >= 0xff)
				{
					dsi_ServerErrorQ42C("DemoMgr: Invalid change");
					return LT_ERROR;
				}

				pChanged[i] = nChange;
			}

			*pnChanged = nChanges;

			for (i = 0; i < *pnChanged; i++)
			{
				// Spelled out twice, not through a pointer local: the
				// shipped code loads the manager before the index
				// (0x0042c3f6); through `pNew` it loads the index first.
				m_pClientMgr->m_Buf12ac[pChanged[i]] = !m_pClientMgr->m_Buf12ac[pChanged[i]];
			}

			for (i = 0; i < 0xff; i++)
			{
				if (m_pClientMgr->m_Buf12ac[i])
				{
					pOn[*pnOn] = i;
					(*pnOn)++;
				}
			}

			return LT_OK;
		}
	}

	pOld = m_pClientMgr->m_Actions[m_pClientMgr->m_n14ac];
	pNew = m_pClientMgr->m_Actions[!m_pClientMgr->m_n14ac];

	memset(pNew, 0, 0xff);

	if (!m_pClientMgr->m_n1520)
		m_pClientMgr->m_pInputMgr->m_ReadInput(m_pClientMgr->m_pInputMgr, pNew,
		                                       m_pClientMgr->m_fAxisOffsets);

	if (!m_pClientMgr->m_n151c || bClear)
	{
		memset(pNew, 0, 0xff);

		// memset, not three 0.0f stores: shipped takes the address once
		// and stores one zero register three times (0x0042c4e4).
		memset(m_pClientMgr->m_fAxisOffsets, 0, sizeof(m_pClientMgr->m_fAxisOffsets));
	}
	// M-P5-19: toggled commands (clientconsole.cpp), on live input only
	else if (!m_pClientMgr->m_n1520)
		ModernApplyCommandToggles(pNew);

	for (i = 0; i < 0xff; i++)
	{
		if (pNew[i])
		{
			pOn[*pnOn] = i;
			(*pnOn)++;
		}

		if (pNew[i] != pOld[i])
		{
			pChanged[*pnChanged] = i;
			(*pnChanged)++;
		}
	}

	m_pClientMgr->m_n14ac = !m_pClientMgr->m_n14ac;

	if (m_nMode == 1)
	{
		m_pStream->WriteVal((uint8)1);

		for (i = 0; i < 3; i++)
			m_pStream->Write(&m_pClientMgr->m_fAxisOffsets[i], 4);

		m_pStream->WriteVal((uint8)*pnChanged);

		for (i = 0; i < *pnChanged; i++)
			m_pStream->WriteVal((uint8)pChanged[i]);
	}

	return LT_OK;
}


// ----------------------------------------------------------------------- //
// 0x0042c6d0 -- 899 bytes, thiscall, `ret $4`.  Read the next frame's sync
// byte off the demo in play mode.  The caller passes the byte it expects
// (GatherInput passes 1 and 0x0042c5f0 passes 0); a 2 is the end of the
// demo.  At the end: the run's time and frame rate are printed and folded
// into the min/max over every run (PlayDemoReps), each counter in the
// table at 0x004b3098 is printed when PlayDemoProfile is set and folded
// into its record's min/max, and after the last run the summary goes out;
// any earlier run asks the client to shut down (dsi_OnClientShutdown with
// a null, 0x0042c9e0) so it can be run again.  Then the flag setter
// (0x0042e680), Stop, and the elapsed time.  LT_FINISHED for the end and
// for a wrong sync byte, LT_OK otherwise.
//
// Every uint32 -> double goes through a zero-extended int64 (`mov %edi,
// 0x4(slot); fildll`), which is VC6's unsigned conversion, so the counters
// and the frame count are unsigned.  The min/max folds are LTMIN/LTMAX
// (`cmp; jb/ja` over one register) with the counter re-read between them.
// Two spellings the bytes decide: the counter loops' index is UNSIGNED
// (VC6 turns it into a biased pointer and compares with `jb`; an int gave
// `jl`), and the record's counter pointer is named once for the two folds
// (read once at 0x0042c816; through the record it was read twice).
// ----------------------------------------------------------------------- //
// collelem.cpp's seventeen-record counter table (0x004b3098 .. 0x004b31a7),
// declared with that file's class so the reference is its definition.
class CCounterEntryQ428
{
public:
	CCounterEntryQ428(uint32* pCounter, const char* pName);

	uint32*     m_pCounter;                  // +0x00  its first dword is the count
	const char* m_pName;                     // +0x04
	uint32      m_nMin;                      // +0x08
	uint32      m_nMax;                      // +0x0c
};

extern CCounterEntryQ428 g_CounterTableQ428[17];   // 0x004b3098 (collelem.cpp)

// The min/max over every PlayDemoReps run.  They are this block's own: the
// two initialised ones are .data words 100000.0f (0x47c35000) and the other
// two are the .bss pair directly past the counter table.  Names ours.
float  g_fDemoMinTimeQC5 = 100000.0f;        // 0x004a9880
float  g_fDemoMinFPSQC5  = 100000.0f;        // 0x004a9884
float  g_fDemoMaxTimeQC5;                    // 0x004b31a8
float  g_fDemoMaxFPSQC5;                     // 0x004b31ac

// The "PlayDemoReps" / "PlayDemoProfile" console variables (dsi.cpp's
// names) and clientwin.cpp's repeat-run counter.
extern int    g_CV_PlayDemoReps;             // 0x004b3238  "PlayDemoReps"
extern int    g_CV_PlayDemoProfile;          // 0x004b323c  "PlayDemoProfile"
extern uint32 g_nCluCurRun;                  // 0x004b13ec  clientwin.cpp

float  cmg_GetClockQC5();                    // 0x00489600
uint32 cnt_GetFrequencyQC5();                // 0x0041eb50
void   sub_42e680(uint32 nValue);            // 0x0042e680  clientdsi.cpp
void   dsi_OnClientShutdownQC5(char* pMsg);  // 0x0042e6a0
void   dm_PrintQ42C(char* pMsg, ...);        // 0x0042ca60, below

LTRESULT CDemoMgrQ42C::Sub42c6d0(int nFlag)
{
	uint8  nSync;
	float  fTime;
	float  fFPS;
	double fVariance;
	uint32* pCounter;
	uint32 i;

	if (!m_pStream || m_nMode != 2)
		return LT_OK;

	m_pStream->Read(&nSync, 1);

	if (nSync == 2)
	{
		fTime = cmg_GetClockQC5() - m_fStartTime;
		fFPS = (float)m_nFrames / fTime;

		dm_PrintQ42C("Demo played in %.3f seconds", fTime);
		dm_PrintQ42C("%d frames drawn, average %.3f fps", m_nFrames, fFPS);

		if (g_fDemoMinTimeQC5 >= fTime)
			g_fDemoMinTimeQC5 = fTime;

		if (g_fDemoMaxTimeQC5 <= fTime)
			g_fDemoMaxTimeQC5 = fTime;

		if (g_fDemoMinFPSQC5 >= fFPS)
			g_fDemoMinFPSQC5 = fFPS;

		if (g_fDemoMaxFPSQC5 <= fFPS)
			g_fDemoMaxFPSQC5 = fFPS;

		for (i = 0; i < 17; i++)
		{
			if (g_CV_PlayDemoProfile)
			{
				dm_PrintQ42C("%s: %.3f", g_CounterTableQ428[i].m_pName,
				             (double)*g_CounterTableQ428[i].m_pCounter / cnt_GetFrequencyQC5());
			}

			pCounter = g_CounterTableQ428[i].m_pCounter;

			g_CounterTableQ428[i].m_nMin = LTMIN(g_CounterTableQ428[i].m_nMin, *pCounter);
			g_CounterTableQ428[i].m_nMax = LTMAX(g_CounterTableQ428[i].m_nMax, *pCounter);
		}

		if (g_CV_PlayDemoReps > 1)
		{
			if (g_nCluCurRun == g_CV_PlayDemoReps)
			{
				dm_PrintQ42C("PlayDemoReps info -------------");
				dm_PrintQ42C("Min time: %.3f, Max time: %.3f, diff: %.3f (%.3f%% error)",
				             g_fDemoMinTimeQC5, g_fDemoMaxTimeQC5,
				             g_fDemoMaxTimeQC5 - g_fDemoMinTimeQC5,
				             g_fDemoMaxTimeQC5 * 100.0f / g_fDemoMinTimeQC5 - 100.0f);
				dm_PrintQ42C("Min FPS: %.3f, Max FPS: %.3f, diff: %.3f",
				             g_fDemoMinFPSQC5, g_fDemoMaxFPSQC5,
				             g_fDemoMaxFPSQC5 - g_fDemoMinFPSQC5);

				if (g_CV_PlayDemoProfile)
				{
					dm_PrintQ42C("value: (min time / max time) variance");

					for (i = 0; i < 17; i++)
					{
						fVariance = (double)(g_CounterTableQ428[i].m_nMax - g_CounterTableQ428[i].m_nMin) /
						            cnt_GetFrequencyQC5();

						dm_PrintQ42C("%s: (%.3f / %.3f), variance: %.3f (%.1f%% total)",
						             g_CounterTableQ428[i].m_pName,
						             (double)g_CounterTableQ428[i].m_nMin / cnt_GetFrequencyQC5(),
						             (double)g_CounterTableQ428[i].m_nMax / cnt_GetFrequencyQC5(),
						             fVariance,
						             (fVariance / (g_fDemoMaxTimeQC5 - g_fDemoMinTimeQC5)) * 100.0);
					}
				}
			}
			else
			{
				dsi_OnClientShutdownQC5(LTNULL);
			}
		}

		sub_42e680(1);

		Stop();

		m_fElapsed = m_pClientMgr->m_fCurTime - cmg_GetClockQC5();

		return LT_FINISHED;
	}
	else if (nSync != nFlag)
	{
		dsi_ServerErrorQ42C("DemoMgr: Sync byte wrong (wanted %d, got %d)", nFlag, nSync);

		Stop();

		return LT_FINISHED;
	}

	return LT_OK;
}


// ----------------------------------------------------------------------- //
// 0x0042ca60 — 71 bytes, cdecl varargs.  The demo manager's own reporter.
//
// PROVENANCE: its EIGHT callers (0x0042c73e, 0x0042c755, 0x0042c80e,
// 0x0042c85d, 0x0042c8b0, 0x0042c8e5, 0x0042c8ff, 0x0042c9c6) are all
// inside 0x0042c6d0, and there are no others in the image — a full
// e8/e9 sweep of .text.  So it is local to the demo block and it prints
// each line twice, to the console and to the debugger.
//
// Read off the bytes:
//
//   * `sub $0x400,%esp` fixes the buffer at 1024 and the count pushed to
//     _vsnprintf is 0x3ff, i.e. sizeof(msg) - 1.
//   * `lea 0x408(%esp),%eax` after the sub is &pMsg + 4 — the va_list, kept
//     in a register.  No `ap = 0` store anywhere, so there is no va_end.
//   * 0x004a74bc reads back as "%s\0" and 0x004a9b30 as "%s\n\0", so the
//     two printers get a bare pass-through format each.
//   * nothing is cleaned until `add $0x420,%esp` — 0x400 of buffer plus
//     eight pushed dwords.  VC6 /O2 defers every cdecl cleanup in the
//     function into the epilogue; this is not evidence about prototypes.
// ----------------------------------------------------------------------- //
void dm_PrintQ42C(char* pMsg, ...)
{
	char    msg[1024];
	va_list marker;

	va_start(marker, pMsg);
	_vsnprintf(msg, sizeof(msg) - 1, pMsg, marker);

	dsi_ServerErrorQ42C("%s", msg);                   // 0x0042ca8c
	ci_DebugOutQ42C("%s\n", msg);                     // 0x0042ca9b
}


// ======================================================================= //
//
//  THE LOCKED LIST — 0x0042d210..0x0042d760
//
//  MOVE-ME: src/engine/lithtech/clientworld.cpp, section "the locked list
//  at 0x42d210..0x42d760".  TryDiscard (0x0042d2d0), Release (0x0042d3b0),
//  Init (0x0042d520), Term (0x0042d540) and PurgeOne (0x0042d6d0) are
//  already there.  0x0042d3f0 is the one entry point still missing.
//
//  clientworld.cpp's CLockedOwnerView needs ONE correction and it is
//  readable off three bodies at once: what it calls `m_pItems` at +0x18 is
//  the HEAD FIELD OF A THREE-WORD LIST, and there is a SECOND list at
//  +0x24.  The proof is the pair 0x0042d3f0 / 0x0042d4b0, which are exact
//  inverses:
//
//      Load    (0x0042d46f)  --m_List18? no: --owner+0x24+8, Delete from +0x24
//                            ++owner+0x18+8, Insert into +0x18
//      Discard (0x0042d4ed)  --owner+0x18+8, Delete from +0x18
//                            ++owner+0x24+8, Insert into +0x24
//
//  and Touch (0x0042d710) reads owner+0x20 (the +0x18 list's count)
//  against owner+0x30 (the limit) and walks BACKWARDS from owner+0x1c
//  (that list's tail) through item+0x04.  0x0048de30 / 0x0048dee0 are the
//  list Insert/Delete: both are thiscall on a {head,tail} object and take
//  one item whose next is at +0x00 and prev at +0x04.
//
//  So +0x18 is the LOADED list and +0x24 the UNLOADED one, the count is at
//  list+0x08, and m_nLimit at +0x30 caps the loaded list.  PurgeOne's
//  `pCur = m_pItems` is `pCur = m_Loaded.m_pHead` under this reading and
//  its bytes do not change.
//
// ======================================================================= //

class CLockedItemQ42C;

// 0x0048de30 / 0x0048dee0.  Out-of-line thiscall helpers that live below
// rezmgr's block; declared, never defined here.  Add/Remove are in-class so
// /Ob1 expands them, which is what produces the shipped single base
// register: `mov 0xc(%esi),%ecx; add $0x24,%ecx; dec 0x8(%ecx); call`.
class CLockedListQ42C
{
public:
	void Insert(CLockedItemQ42C* pItem);     // 0x0048de30
	void Delete(CLockedItemQ42C* pItem);     // 0x0048dee0

	void Add(CLockedItemQ42C* pItem)    { m_nCount++; Insert(pItem); }
	void Remove(CLockedItemQ42C* pItem) { m_nCount--; Delete(pItem); }

public:
	CLockedItemQ42C* m_pHead;                // +0x00
	CLockedItemQ42C* m_pTail;                // +0x04
	uint32           m_nCount;               // +0x08
};

class CLockedOwnerQ42C
{
public:
	void   FreeItem(CLockedItemQ42C* pItem); // 0x0042d610 (dmloader.cpp's FreeRecord)
	void   Flush();                          // 0x0042d680
	LTBOOL PurgeOne();                       // 0x0042d6d0 (clientworld.cpp)
	void   Touch();                          // 0x0042d710

public:
	CRITICAL_SECTION m_CS;                   // +0x00  (0x18 bytes)
	CLockedListQ42C  m_Loaded;               // +0x18
	CLockedListQ42C  m_Unloaded;             // +0x24
	uint32           m_nLimit;               // +0x30
};

// src/LT2/sdk/inc's CClientFileRef shape, as src/engine/lithtech/
// clientworldinfo.cpp already models it for the same callee: a type word,
// a name and a 16-bit id.  0x0042d42e writes zero through the FIRST word
// and 0x004048cf reads that word against 2, which is what types it.
class CClientFileRefQ42C
{
public:
	uint32 m_nType;                          // +0x00
	char*  m_pFilename;                      // +0x04
	uint16 m_nID;                            // +0x08
};

class CLockedItemQ42C
{
public:
	LTBOOL Load();                           // 0x0042d3f0  (name ours)
	LTBOOL Discard();                        // 0x0042d4b0

public:
	CLockedItemQ42C*   m_pNext;              // +0x00
	CLockedItemQ42C*   m_pPrev;              // +0x04
	ILTStream*         m_pStream;            // +0x08
	CLockedOwnerQ42C*  m_pOwner;             // +0x0c
	uint32             m_nRefs;              // +0x10
	CClientFileRefQ42C m_Ref;                // +0x14
	uint32             m_nOffset;            // +0x20
};

// 0x004048c0 — src/engine/lithtech/clientworldinfo.cpp already declares
// this one, with this exact prototype, for the same purpose.
ILTStream* sub_4048c0Q42C(void* pFileMgr, CClientFileRefQ42C* pRef);

// The client manager global.  ccclient.cpp / clientmgr2.cpp / cltnew.cpp
// all already pin 0x004b17e0 as g_pClientMgr and 0x1534 as its file
// manager; only those two facts are needed here.
class CFileMgrHolderQ42C
{
public:
	uint8  m_pad_0000[0x1534];
	void*  m_pFileMgr;                       // +0x1534
};

extern CFileMgrHolderQ42C* g_pClientMgrQ42C;               // 0x004b17e0
CFileMgrHolderQ42C* g_pClientMgrQ42C;

// ----------------------------------------------------------------------- //
// 0x0042d3f0 — 179 bytes, thiscall, no arguments.  Open this item's file
// and move it onto the loaded list.  The exact inverse of Discard
// (0x0042d4b0), which is already decoded in clientworld.cpp.
//
// Extent: the last instruction is the `ret` at 0x0042d4a2, and
// 0x0042d3f0 + 179 = 0x0042d4a3.  No branch target leaves the range: the
// four forward jumps are 0x0042d415, 0x0042d42a, 0x0042d460, 0x0042d46f,
// all inside it.
//
// Read off the bytes:
//
//   * FOUR separate LeaveCriticalSection sites (0x0042d408, 0x0042d420,
//     0x0042d453, 0x0042d499), one per return.  That is four
//     `Leave(); return X;` statements, exactly the shape clientworld.cpp
//     already reconstructed for the sibling TryDiscard.
//   * the lock is taken through the OWNER (`mov 0xc(%esi),%eax`), never
//     through `this`.
//   * `lea 0x14(%esi),%eax; push %eax; movl $0x0,(%eax)` — the address is
//     computed once and the type-word clear goes through it, so the clear
//     and the argument are one addressing expression in the source.
//   * `mov 0x20(%esi),%ecx; test; je` then `call *0x14(%edx)` on the new
//     stream is ILTStream::SeekTo at slot +0x14, guarded on a non-zero
//     offset.
//   * the two list moves are single-base-register (`mov 0xc(%esi),%ecx;
//     add $0x24,%ecx; dec 0x8(%ecx); call`), which is what an in-class
//     Remove/Add on the list sub-object gives; the owner is reloaded from
//     +0x0c for each of the three tail statements.
// ----------------------------------------------------------------------- //
LTBOOL CLockedItemQ42C::Load()
{
	EnterCriticalSection(&m_pOwner->m_CS);

	if (!m_Ref.m_pFilename)                          // 0x0042d3fd
	{
		LeaveCriticalSection(&m_pOwner->m_CS);

		return LTTRUE;
	}

	if (m_pStream)                                   // 0x0042d415
	{
		LeaveCriticalSection(&m_pOwner->m_CS);

		return LTFALSE;
	}

	m_Ref.m_nType = 0;                               // 0x0042d42e
	m_pStream = sub_4048c0Q42C(g_pClientMgrQ42C->m_pFileMgr, &m_Ref);

	if (!m_pStream)                                  // 0x0042d44d
	{
		LeaveCriticalSection(&m_pOwner->m_CS);

		return LTTRUE;
	}

	if (m_nOffset)                                   // 0x0042d460
		m_pStream->SeekTo(m_nOffset);                // slot +0x14

	m_pOwner->m_Unloaded.Remove(this);               // 0x0042d46f
	m_pOwner->m_Loaded.Add(this);                    // 0x0042d47e
	m_pOwner->Touch();                               // 0x0042d490

	LeaveCriticalSection(&m_pOwner->m_CS);

	return LTFALSE;
}

// ----------------------------------------------------------------------- //
// 0x0042d4b0 — 102 bytes, thiscall, no arguments.  Close this item's file
// and move it onto the unloaded list: Load's exact inverse.
//
// Read off the bytes: two Leave sites (0x0042d4c8, 0x0042d50c), both
// answering zero.  GetPos (slot +0x18) saves the position into +0x20 so a
// later Load can SeekTo it; Release (slot +0x04) closes the stream; then
// the pointer is cleared and the item moves +0x18 -> +0x24.
// ----------------------------------------------------------------------- //
LTBOOL CLockedItemQ42C::Discard()
{
	EnterCriticalSection(&m_pOwner->m_CS);

	if (!m_pStream)
	{
		LeaveCriticalSection(&m_pOwner->m_CS);

		return LTFALSE;
	}

	m_pStream->GetPos((unsigned long*)&m_nOffset);   // slot +0x18
	m_pStream->Release();                            // slot +0x04
	m_pStream = LTNULL;

	m_pOwner->m_Loaded.Remove(this);                 // 0x0042d4ed
	m_pOwner->m_Unloaded.Add(this);                  // 0x0042d4fd

	LeaveCriticalSection(&m_pOwner->m_CS);

	return LTFALSE;
}

// ----------------------------------------------------------------------- //
// 0x0042d680 — 74 bytes, thiscall, no arguments.  Free every item.  Term
// (0x0042d540) is its caller.
//
// Read off the bytes: the loaded list is only emptied when PurgeOne
// (0x0042d6d0) answers nonzero — its `je` skips to the second loop, not to
// the Leave — and each list is drained by handing its head to FreeItem
// (0x0042d610) until the head is null.
// ----------------------------------------------------------------------- //
void CLockedOwnerQ42C::Flush()
{
	EnterCriticalSection(&m_CS);

	if (PurgeOne())
	{
		while (m_Loaded.m_pHead)
			FreeItem(m_Loaded.m_pHead);
	}

	while (m_Unloaded.m_pHead)
		FreeItem(m_Unloaded.m_pHead);

	LeaveCriticalSection(&m_CS);
}

// ----------------------------------------------------------------------- //
// 0x0042d710 — 65 bytes, thiscall, no arguments.  Enforce the open-file
// limit: while the loaded list's count is above +0x30, walk it BACKWARDS
// from the tail (+0x1c, prev link at item+0x04) discarding every item
// nobody holds.  The prev link is read before Discard can unlink the item.
// ----------------------------------------------------------------------- //
void CLockedOwnerQ42C::Touch()
{
	CLockedItemQ42C* pCur;
	CLockedItemQ42C* pPrev;

	EnterCriticalSection(&m_CS);

	pCur = m_Loaded.m_pTail;

	while (m_Loaded.m_nCount > m_nLimit && pCur)
	{
		pPrev = pCur->m_pPrev;

		if (!pCur->m_nRefs)
			pCur->Discard();

		pCur = pPrev;
	}

	LeaveCriticalSection(&m_CS);
}


// ======================================================================= //
//
//  THE TEXTURE MODULE — 0x0042e7d0..0x0042ee6a
//
//  clientdsi.cpp declares TextureData / TextureMipData / TextureLink and
//  now holds the destructor that used to be written here; 0x0042edd0
//  below is the one body of the block still in this file.
//
// ======================================================================= //

// TextureData's destructor (0x0042e830) now lives in clientdsi.cpp with the
// class itself, so the vtable it stores (0x004a22ac) is TextureData's.


// ----------------------------------------------------------------------- //
// 0x0042edd0 — 91 bytes, cdecl, four arguments.  Read a block, or skip it
// and hand back zeroes.
//
// Extent: the last instruction is the `ret` at 0x0042ee2a and
// 0x0042edd0 + 91 = 0x0042ee2b.  One forward branch, to 0x0042ee17, inside
// the range.
//
// PROVENANCE: two callers, 0x0042ec58 and 0x0042ec7d, both inside
// dtx_Create's mip loop (dtx_Create is 0x0042eab0..0x0042edc1; the
// enumeration's 0x0042ebf7 row is interior to it, as clientdsi.cpp already
// records).  Both push four arguments and clean them with `add $0x10,%esp`,
// and neither reads the answer — so the return type is void.
//
// Read off the bytes:
//
//   * the skip arm is the FALL-THROUGH and the read arm is jumped to at
//     0x0042ee17, past the skip arm's own `ret`.  Both arms end the
//     function, which is why each has its own epilogue and there is no
//     `jmp` joining them: a two-armed if/else with nothing after it.
//   * `shr $2,%ecx; rep stos; and $3,%ecx; rep stosb` on a VARIABLE count
//     is VC6 /Oi's inline memset.
//   * `lea 0x14(%esp),%ecx; push %ecx; call *0x18(%eax)` then
//     `mov 0x14(%esp),%eax` — 0x18 is ILTStream::GetPos(unsigned long*),
//     and the out-parameter sits in the DEAD SECOND-ARGUMENT SLOT because
//     pStream is already cached in esi.  That is iltstream.h's
//     inline `GetPos()` helper, whose own temporary VC6 parks in a
//     parameter home slot.  So the source calls the no-argument overload
//     and never declares a local.
//   * `add %ebx,%eax; push %eax; call *0x14(%edx)` is SeekTo(GetPos() + n).
// ----------------------------------------------------------------------- //
void sub_42edd0Q42C(int bSkip, ILTStream* pStream, void* pData, uint32 nSize)
{
	if (bSkip)
	{
		memset(pData, 0, nSize);                      // 0x0042edec

		pStream->SeekTo(pStream->GetPos() + nSize);   // 0x0042ee02 / 0x0042ee10
	}
	else
	{
		pStream->Read(pData, nSize);                  // 0x0042ee27
	}
}


// ======================================================================= //
//
//  THE TWO ZERO VECTORS — 0x0042f610 and 0x0042f640
//
//  MOVE-ME: src/engine/lithtech/clientdsi.cpp.  Its decode note lists
//  0x0042f600/0x0042f630 (and 0x00430590/0x004305b0/0x004305d0) as "ALL
//  FIVE HAVE THE SAME UNEXPLAINED SHAPE: a five-byte `jmp` forward, then
//  eleven nops, then the body ... No source shape I can name produces
//  `jmp` over its own alignment padding".  IT IS EXPLAINED, and the proof
//  is in server.dll:
//
//      lithtech.exe 0x0042f630  e9 0b 00 00 00   jmp 0x0042f640
//      server.dll   0x1000e4df  e9 00 00 00 00   jmp 0x1000e4e4
//
//  The SAME construct, with the SAME zero-vector body behind it, and in
//  server.dll the displacement is literally 0 — the thunk falls straight
//  into the body.  It is VC6's `??__E<object>` dynamic initializer: a
//  tail-jump into the constructor body for a file-scope object that has a
//  constructor and NO destructor (with a destructor it is instead the
//  ten-byte `call ctor; jmp atexit-thunk` that 0x0042cab0 and 0x0041d600
//  both show).  The two are SEPARATE COMDATs; in lithtech.exe the linker
//  16-byte-aligns the second one and the displacement becomes 0x0b, which
//  is the whole of the "jmp over its own padding" mystery.
//
//  0x004a705c and 0x004a7060 hold 0x0042f600 and 0x0042f630, and that
//  table (0x004a7000..0x004a70fc) is the CRT `__xc` initializer list —
//  0x0042cab0, dhash.cpp's g_HashElementBank initializer, is in it too.
//
//  src/engine/server/svlast.cpp ALREADY reconstructs this exact construct
//  for server.dll's pair, with the same reasoning, under the name
//  CVectorZeroInit.  It is server-only (svlast.cpp is not in the lithtech
//  object list), so the class has to be repeated here.
//
//  WHY THE BODIES DIFFER BETWEEN THE IMAGES, AND WHY THAT IS THE SAME
//  SOURCE: server.dll's is `fldz; fstps` three times, 25 bytes;
//  lithtech.exe's is `movl $0x0,<abs>` three times, 31 bytes.  /O1 is /Os
//  and picks the 8-byte FPU form; /O2 is /Ot and picks the 10-byte
//  immediate store.  Both sizes come out exactly right, which is the
//  cross-check that one source produces both.
//
//  WHAT THE OBJECTS ARE — and here the evidence is a console command, not
//  the string pool.  The command table at 0x004a8860 / 0x004a886c is
//      { "ModelAdd",    0x0041d640 }
//      { "ModelDirAdd", 0x0041d740 }
//  and 0x0041d640 atof()s its three arguments into 0x004b3228/2c/30 while
//  0x0041d740 does the same into 0x004b3218/1c/20.  0x0041e7a0 prints them
//  back as "ModelAdd %f %f %f\n" (0x004a902c) and "ModelDirAdd %f %f %f\n"
//  (0x004a9014).  So they are three-float debug offsets and the names are
//  the shipped command names.
//
//  DECLARATION ORDER is the CRT slot order: 0x004a705c (g_ModelAdd) comes
//  before 0x004a7060 (g_ModelDirAdd), even though g_ModelDirAdd is the
//  lower .bss address.  server.dll's pair reverses the same way.
//
// ======================================================================= //

// NOT commonphysics.h's CVectorInit, which is deliberately out of line.
// This one must EXPAND into the initializer: the body has no call in it and
// the global's address is folded into every store with no `this` register,
// which is the __forceinline shape and which plain
// `inline` and an in-class definition both fail to produce.
class CVectorZeroInitQ42C
{
public:
	__forceinline CVectorZeroInitQ42C(float fx, float fy, float fz)
	{
		x = fx;
		y = fy;
		z = fz;
	}

	float x;
	float y;
	float z;
};

// 0x004b3228 — "ModelAdd".  Initializer 0x0042f600 -> body 0x0042f610.
CVectorZeroInitQ42C g_ModelAddQ42C(0.0f, 0.0f, 0.0f);

// 0x004b3218 — "ModelDirAdd".  Initializer 0x0042f630 -> body 0x0042f640.
CVectorZeroInitQ42C g_ModelDirAddQ42C(0.0f, 0.0f, 0.0f);
