#include "command_parser.h"
// *********************************************************************** //
//
//  MODULE:   W12_468000 (.CPP)  -- scratch file, NOT a drop-in TU
//
//  11 rows / 1,136 bytes.  Ten rows are reconstructed
//  below; one (0x0045f496) is NOT A FUNCTION and gets an exclusion line.
//
//  GROUND TRUTH: the retail lithtech.exe (cl 8168 /O2 = /Og /Oi /Ot /Oy
//  /Ob1).  The /O1 oracle is the retail server.dll (cl 8447 /O1).  Every
//  offset, constant and branch below is cited by the address it was read at.
//
//  Every symbol introduced here carries the Q468 suffix so this object
//  cannot collide.  Where a class or constant ALREADY EXISTS in the repo the
//  real name is given in the comment and the Q468 copy is only a local
//  stand-in -- see the per-row "HOME" note for where each body belongs.
//
//  ---------------------------------------------------------------------- //
//  VERDICT TABLE
//  ---------------------------------------------------------------------- //
//
//  addr        sz   verdict        what it is
//  ---------- ---- -------------- ------------------------------------------
//  0x0045c230  75  WRITE          ServerMessage::AppendBody (client-only:
//                                 NO server.dll twin exists -- see report)
//  0x0045c630  79  WRITE          w_GetShadeCellSize, commonmodel.cpp
//                                 DECLARED AND NEVER DEFINED at
//                                 commonmodel.cpp:4377 (server 0x10026350)
//  0x0045f430 102  WRITE          FMConvertMgr::Fill  (fmconvert.cpp TU)
//  0x0045f496  89  *** EXCLUDE ***  interior block of 0x0045f430.  Not a
//                                 function.  Exclusion line at the bottom.
//  0x0045f660  59  WRITE          fm_GetImageDataSize -- now lives (identical)
//                                 in src/engine/lithtech/fmconvert.cpp; the
//                                 scratch copy here was deleted.
//  0x00464060  59  WRITE          sub_100272d6, smgr.cpp
//                                 DECLARED AND NEVER DEFINED at smgr.cpp:6839
//                                 (server 0x100272d6, 50 bytes)
//  0x00468860 113  WRITE          snd_MergeSentState, smgr.cpp
//                                 DECLARED AND NEVER DEFINED at smgr.cpp:6 ->
//                                 smgr.cpp:8535 (server 0x10029b53).
//                                 *** THE TWO IMAGES ARE BYTE-IDENTICAL ***
//                                 113/113, all 113 bytes equal -- one
//                                 spelling pays twice.
//  0x00469c70 145  WRITE          sm_ModelStringKeyCallback, servermodel.cpp
//                                 DECLARED AND NEVER DEFINED at
//                                 servermodel.cpp:255 (server 0x1002a83e)
//  0x0046a3e0  55  WRITE          sm_FindObjectIDRec, smgr.cpp
//                                 DECLARED AND NEVER DEFINED at
//                                 spritecontrol.cpp:451 (server 0x1002ad18)
//  0x0046b8f0 125  WRITE          fs_FindFileInTrees, serverengine.cpp
//                                 DECLARED AND NEVER DEFINED at
//                                 serverengine.cpp:1231 AND counter.cpp:903
//                                 (server 0x1002bde9, 88 bytes)
//  0x0046bbf0 235  WRITE (LOW     sub_1002d355 with 0x1002d385's constructor
//              CONFIDENCE)        EXPANDED into it.  svnew.cpp:914 recorded
//                                 0x1002d385 as blocked on "the whole class
//                                 graph"; the graph is read off the bytes
//                                 below, but this is the one row least
//                                 likely to match first-shot.
//
//  SIX of the ten are prototypes that the repo already declares and never
//  defines.  That was the productive instrument here, not the string
//  pool.
//
// *********************************************************************** //

#include "ltbasedefs.h"


// ======================================================================= //
//
//  SECTION A -- the pixel-format converter (fmconvert.cpp's TU)
//
//  src/engine/lithtech/fmconvert.cpp already reconstructs 0x0045d530 ..
//  0x0045f080 of this module and states its shape: FMConvertMgr is 0xb94
//  bytes, its constructor is 0x0045f390, its blit dispatcher 0x0045f3d0, and
//  0x0045f5e0 / 0x0045f620 are the single-pixel write/read dispatchers over
//  the tables at 0x004aceb8 / 0x004aced8.  The two rows here are the two
//  entry points that file did not reach.
//
//  HOME for both: src/engine/lithtech/fmconvert.cpp (or whatever TU finally
//  owns 0x0045f0e0..0x0045f6a0 -- the constructor at 0x0045f390 and
//  InitTables at 0x0045f4f0 are in it).  fmconvert.cpp is CLIENT-ONLY; there
//  is no server.dll twin for either row.
//
// ======================================================================= //

// lithtech.exe only (fmconvert.cpp is client-only; no server.dll twin), so the
// server build does not compile it: its callees exist only in the client.
#ifndef LT_SERVER_BUILD

// ----------------------------------------------------------------------- //
//  The classes this section does not own.  Named FM*Q468 so this scratch TU
//  cannot collide with fmconvert.cpp's own FMFormat / FMConvertMgr.
// ----------------------------------------------------------------------- //

// fmconvert.cpp's FMFormat.  Only +0x04 (the format code) is reached here.
class FMFormatQ468
{
public:
	// 0x0045f230, 11 bytes, thiscall, no arguments, bare `ret`:
	//     mov eax,[ecx+0x4]                  ; m_nCode
	//     mov eax,[eax*4+0x4acd98]           ; g_BytesPerPixel[m_nCode]
	// The table at 0x004acd98 reads { 1,1,2,4,0,0,0,1 }, so code 2 is 16-bit
	// and code 3 is 32-bit -- which is exactly the pair FMConvertMgr::Fill
	// below accepts.  DECLARED, NOT DEFINED: it is out of this row's range
	// and 0x0045f430 CALLS it (0x0045f47d).
	uint32 GetBytesPerPixel();               // 0x0045f230

	uint32 m_pVTable;                        // 0x00  -> 0x004a1ab4
	uint32 m_nCode;                          // 0x04
	uint32 m_Masks[4];                       // 0x08
	uint32 m_nBits[4];                       // 0x18
	uint32 m_nShifts[4];                     // 0x28
};                                           // 0x38

// A rectangle in the fill request.  Four int32 in left/top/right/bottom
// order, which is how 0x0045f430 reads them: +0x10 multiplies the PITCH
// (0x0045f472) so it is a ROW, +0x0c multiplies the BYTES PER PIXEL
// (0x0045f482) so it is a COLUMN, and the two spans are +0x14 - +0x0c
// (0x0045f490) and +0x18 - +0x10 (0x0045f492).
class LTRectQ468
{
public:
	int32 left;                              // 0x00
	int32 top;                               // 0x04
	int32 right;                             // 0x08
	int32 bottom;                            // 0x0c
};

// The fill request.  THIS CLASS ALREADY EXISTS in the repo as
// `CPFormatHolderView` (src/engine/lithtech/lt2new2.cpp:997), reconstructed
// from its constructor at 0x0045f340; that file types +0x04..+0x18 as
// anonymous `void*` because the constructor only zeroes them.  0x0045f430 is
// what types them, and it agrees field for field with that constructor's
// store set (0x0c,0x10,0x14,0x18, then the embedded format's vptr, then
// 0x00,0x04,0x08,0x1c).
//
// DO NOT WRITE THE CONSTRUCTOR AGAIN -- lt2new2.cpp owns 0x0045f340.
class FMFillRequestQ468
{
public:
	// 0x0045f370, 21 bytes, thiscall, no arguments, bare `ret`:
	//     cmp DWORD PTR [ecx],0 ; je fail    ; m_pFormat
	//     mov eax,[ecx+0x4] ; test ; je fail ; m_pData
	//     mov eax,1 ; ret  /  fail: xor eax,eax ; ret
	// Defined below, out of line: 0x0045f430 CALLS it (0x0045f43a).
	int IsValid();                         // 0x0045f370

	FMFormatQ468* m_pFormat;                 // 0x00
	uint8*        m_pData;                   // 0x04
	int32         m_nPitch;                  // 0x08
	LTRectQ468    m_Rect;                    // 0x0c
	uint32        m_Color;                   // 0x1c
	FMFormatQ468  m_Format;                  // 0x20 .. 0x58
};

class FMConvertMgrQ468
{
public:
	// 0x0045f5e0, 50 bytes, thiscall, `ret 0xc`.  Its second argument is a
	// PValue BY VALUE -- the callee takes its address (0x0045f5f8
	// `lea esi,[esp+0x10]`) to hand the per-format writer a pointer.
	// DECLARED, NOT DEFINED: 0x0045f430 CALLS it (0x0045f467).
	void WritePixel(FMFormatQ468* pFormat, uint32 nValue, void* pOut);

	// 0x0045f430 -- the row.  Defined below.
	LTRESULT Fill(FMFillRequestQ468* pRequest);

	uint8 m_pad_000[0xb94];
};


// 0x0045f370, 21 bytes -- a request is usable once it has a format and a
// destination.  Out of line (not in-class): 0x0045f430 CALLS it.
int FMFillRequestQ468::IsValid()
{
	if (m_pFormat && m_pData)
		return 1;
	return 0;
}


// ----------------------------------------------------------------------- //
//  0x0045f430, 102 bytes + the 89 the enumerator split off = 191 bytes,
//  thiscall, one argument, `ret 0x4`.  FILL A RECTANGLE OF A SURFACE WITH
//  ONE COLOUR.
//
//  EXTENT.  The row is 0x0045f430..0x0045f495 and the next row claims
//  0x0045f496..0x0045f4ee.  They are ONE FUNCTION: 0x0045f496 is the
//  `push ebp` that opens the outer loop's preheader, the `je` at 0x0045f494
//  jumps OVER it to 0x0045f4dd, nothing anywhere in .text references
//  0x0045f496 (full e8/e9/0f8x sweep of .text: zero hits), and the single
//  `ret 0x4` pair at 0x0045f4e2 / 0x0045f4ec ends the body exactly on
//  0x0045f4ef where the nop padding starts.  191 = 102 + 89 exactly.
//
//  WHO CALLS IT.  One site, 0x0040ebf6, and it settles every type:
//    * `mov eax,ds:0x4b17e0 ; lea ecx,[eax+0x1618]` -- `this` is the client
//      manager's m_TexMod, which src/engine/lithtech/clientmgr3.cpp:451
//      already types as the 0xb94-byte object constructed at 0x0045f390,
//      i.e. FMConvertMgr.
//    * the request is a stack local whose +0x00 is set to 0x004b1758
//      (0x0040ebee) -- the same global FMFormat clientinterface.cpp:1726
//      already passes to 0x0045f5e0 as `&g_Sub4b1758`.
//    * its +0x0c..+0x18 are filled from four CONSECUTIVE caller locals
//      (0x0040ebb3..0x0040ebdb: 0xc,0x10,0x14,0x18 -> 0x38,0x3c,0x40,0x44
//      of the request), which is an LTRect copied in one go.
//    * its +0x1c comes from the caller's own argument at [esp+0x90]
//      (0x0040ebcb) -- the colour.
//
//  WHAT PINS THE BODY
//    * TWO GUARDS, ONE SHARED FAILURE, AT THE VERY END.  0x0045f441 and
//      0x0045f454 both jump to 0x0045f4e5, which sits past the success
//      return at 0x0045f4dd.  That is the AND-merge: the success
//      path is wrapped in nested positive guards and `return LT_ERROR` is
//      the last statement, not two early returns.
//    * the format code is loaded ONCE into %ecx (0x0045f449) and compared
//      against 2 and 3 -- VC6 commons the two reads of the same member
//      across the `||` because nothing separates them.
//    * THE PIXEL LIVES IN THE DEAD PARAMETER SLOT.  `lea ecx,[esp+0xc]` at
//      0x0045f45d is %esp0+4, i.e. pRequest's own argument slot, which is
//      dead because pRequest is in %esi.  The inner loops re-read it out of
//      that slot on EVERY iteration (0x0045f4ab, 0x0045f4c8) because its
//      address went to WritePixel and VC6 must assume the call aliased it.
//      A plain local reproduces this; do not try to cache it in a variable.
//    * the running pointer is built LEFT TO RIGHT:
//        edi = m_Rect.top * m_nPitch        0x0045f46c/0x0045f472
//        edi += m_pData                     0x0045f47b
//        edi += GetBytesPerPixel() * left   0x0045f482/0x0045f48b
//      and m_pFormat is RE-LOADED for the GetBytesPerPixel call
//      (0x0045f476) because the WritePixel call ended the block.
//    * BOTH LOOPS ARE COUNTDOWNS WITH ONE ENTRY TEST.  The outer closes
//      `dec ebx ; jne` (0x0045f4d9) with its trip count tested once at
//      0x0045f494; each inner closes `dec edx ; jne` (0x0045f4b6,
//      0x0045f4d1) with its count tested once at 0x0045f4a3 / 0x0045f4c0.
//      Neither has a compare in the back edge, which is the
//      countdown shape.  Written as counting `for`s below because the
//      counter is dead in the body (the walk is a pointer) and VC6 converts
//      that form to the countdown; if it comes back with `cmp`/`jb` in the
//      back edge the fix is `do { } while (--n);` under an `if (n)`.
//    * the format code is re-read INSIDE the outer loop (0x0045f499) and
//      the pitch is re-read at the bottom of it (0x0045f4d4), so both are
//      written out at their uses rather than hoisted into locals.
//    * `xor eax,eax` on the success path (0x0045f4df) and `mov eax,1` on
//      the shared failure (0x0045f4e6): LT_OK / LT_ERROR.
// ----------------------------------------------------------------------- //
LTRESULT FMConvertMgrQ468::Fill(FMFillRequestQ468* pRequest)
{
	if (pRequest->IsValid())                                 // 0x0045f43a
	{
		// 2 = 16-bit, 3 = 32-bit.  0x0045f44c / 0x0045f451.
		if (pRequest->m_pFormat->m_nCode == 2 ||
			pRequest->m_pFormat->m_nCode == 3)
		{
			uint32 nPixel;
			uint8* pCur;
			uint32 nWidth;
			uint32 nHeight;
			uint32 x;
			uint32 y;

			WritePixel(pRequest->m_pFormat, pRequest->m_Color, &nPixel);

			pCur = pRequest->m_pData
				 + pRequest->m_Rect.top * pRequest->m_nPitch;
			pCur += pRequest->m_pFormat->GetBytesPerPixel()
				  * pRequest->m_Rect.left;

			nWidth  = pRequest->m_Rect.right  - pRequest->m_Rect.left;
			nHeight = pRequest->m_Rect.bottom - pRequest->m_Rect.top;

			for (y = nHeight; y; y--)
			{
				if (pRequest->m_pFormat->m_nCode == 2)       // 0x0045f49e
				{
					uint16* pDest = (uint16*)pCur;

					for (x = nWidth; x; x--)
						*pDest++ = (uint16)nPixel;           // 0x0045f4ab
				}
				else if (pRequest->m_pFormat->m_nCode == 3)  // 0x0045f4bb
				{
					uint32* pDest = (uint32*)pCur;

					for (x = nWidth; x; x--)
						*pDest++ = nPixel;                   // 0x0045f4c8
				}

				pCur += pRequest->m_nPitch;                  // 0x0045f4d4
			}

			return LT_OK;                                    // 0x0045f4df
		}
	}

	return LT_ERROR;                                         // 0x0045f4e6
}

#endif // !LT_SERVER_BUILD


// ======================================================================= //
//
//  SECTION B -- the two rows in the servermsg / commonmodel run
//               (lithtech 0x0045c230 .. 0x0045c630)
//
//  The run is pinned at both ends: 0x0045c350 is ServerMessage::WriteString
//  (server 0x10026137), 0x0045c390 is ServerMessage::WriteData (server
//  0x1002616a), 0x0045c500 is w_GetAmbientLight (server 0x10026261, already
//  reconstructed in src/engine/server/commonmodel.cpp:4037) and 0x0045c630
//  is its sibling w_GetShadeCellSize (server 0x10026350).
//
// ======================================================================= //

#include <stdlib.h>                          // atof

// src/engine/server/servermgr.h:1570.  Repeated here only so this scratch TU
// compiles standalone; the real declaration is the one to use.
using CommandParserQ468 = CommandParser;

// ----------------------------------------------------------------------- //
//  0x0045c630, 79 bytes, __cdecl, one argument, returns a float.
//  THE POINT-SHADE GRID'S CELL SIZE FOR A NAMED WORLD.
//
//  ALREADY DECLARED AND NEVER DEFINED:
//      src/engine/server/commonmodel.cpp:4377
//          float w_GetShadeCellSize(char* pWorldName);   // 0x10026350
//  and its only caller, w_SetupPointShade (commonmodel.cpp:4400), is already
//  written.  So this row is a one-function hole in a file that is otherwise
//  finished, and it pays in BOTH images (server 0x10026350, 68 bytes).
//
//  WHAT PINS IT
//    * `mov eax,0x1508 ; call 0x00492c60` (0x0045c630) is __chkstk for a
//      0x1508-byte frame, and 0x1508 is sizeof(CommandParser) exactly --
//      the whole frame is the parser, as it is in w_GetAmbientLight.
//    * `lea ecx,[esp+0xc]` (0x0045c64a) is the frame base and
//      `mov [esp+0x110],eax` (0x0045c64e) is base+0x104 = m_pPos, so the
//      world name is dropped straight into the parser's cursor.
//    * Find("LightTableRes", 0, 2) -- the literal is at 0x004acd60 and the
//      pushes are 2, 0, string (0x0045c641..0x0045c645).  Argument minimum
//      TWO, because the value is m_ArgPointers[1].
//    * `mov ecx,[esp+0x4]` (0x0045c65e) after the call is base+0x04, i.e.
//      m_ArgPointers[1], handed to 0x00493515 = atof.
//    * the miss answers `fld DWORD PTR ds:0x4a1eb8` (0x0045c672), which
//      reads 250.0f.
//    * NO NARROWING STORE anywhere: atof's double is left in %st(0) and
//      returned as it stands.  Both images do this, so `(float)atof(...)`
//      in a `float`-returning function is the spelling -- exactly as
//      commonmodel.cpp already records for w_GetAmbientLight's three
//      component assignments (there the `fstps` IS the narrowing, here the
//      return has nowhere to narrow to).
//    * ORDER: the parameter is loaded (0x0045c63a) before Find's three
//      pushes and the m_pPos store lands between them.  Same in server
//      (0x1002635d before the pushes, 0x1002636f after).  `parser.m_pPos =
//      pWorldName;` first, `Find` second.
//
//  HOME: src/engine/server/commonmodel.cpp, immediately below the
//  w_GetShadeCellSize declaration at :4377 (COFF order says it sits between
//  w_GetAmbientLight and w_ReadRotation in that file's run).
// ----------------------------------------------------------------------- //
float w_GetShadeCellSizeQ468(char* pWorldName)
{
	CommandParserQ468 parser;

	parser.m_pPos = pWorldName;                          // 0x0045c64e

	if (parser.Find("LightTableRes", 0, 2))              // 0x0045c655
		return (float)atof(parser.m_ArgPointers[1]);     // 0x0045c663

	return 250.0f;                                       // 0x0045c672 -> 0x004a1eb8
}


// ----------------------------------------------------------------------- //
//  0x0045c230, 75 bytes, thiscall, one argument, `ret 0x4`.
//  APPEND ANOTHER MESSAGE'S PAYLOAD TO THIS ONE, IF IT FITS.
//
//  NO SERVER TWIN.  server.dll's copy of this run is 0x1002609f (= our
//  0x0045c280, the string skip), 0x100260d9 (ReadData), 0x10026137
//  (WriteString), 0x1002616a (WriteData), and nothing in server.dll is this
//  function: a full sweep of server.dll's `e8` edges finds thirteen callers
//  of WriteData and thirteen of GetSpaceLeft (0x1000e845) and no function
//  that calls BOTH and is under 100 bytes.  So this body is client-side only
//  and cannot be cross-checked at /O1.
//
//  WHAT PINS IT
//    * `this` is a ServerMessage: +0x36 is m_MaxDataLen and +0x38 the write
//      cursor (0x0045c232, 0x0045c23a), which are servermsg.h's names, and
//      %ecx is never reassigned so the WriteData call at 0x0045c265 is a
//      thiscall on the same object.
//    * THE OPENING SIX INSTRUCTIONS ARE GetSpaceLeft() EXPANDED.  Compare
//      src/engine/server/servermessage.cpp:1419:
//          int nLeft = (int)m_MaxDataLen - (int)m_Unknown38 - 7;
//          return nLeft < 0 ? 0 : nLeft;
//      against 0x0045c232..0x0045c245: cursor zero-extended first, limit
//      second, `sub`, `sub $7`, `jns`, `xor`.  lithtech.exe's /O2 expands
//      the view's in-class body; server.dll's /O1 calls 0x1000e845.
//    * the argument's +0x34 is m_Unknown34 (CPacket::m_DataLen) and its
//      +0x54 is m_pData.  The call pushes (m_pData + 1, m_DataLen - 1), so
//      the packet-id byte at [0] is deliberately skipped -- and the caller
//      at 0x0041593d has just written the id 7 into the DESTINATION's [0].
//    * TWO EVALUATIONS OF `m_DataLen - 1` off ONE load.  0x0045c24d loads
//      `dx` once; 0x0045c251..0x0045c259 zero-extends it into %edi and
//      decrements that for the compare, and 0x0045c261 decrements %edx
//      itself for the push (legal because the high half is known zero from
//      0x0045c236).  One load, two subtractions -> the source writes the
//      member once per use inside two different expressions, which is what
//      is written below.
//    * `cmp eax,edi ; jl` is SIGNED, so both sides are `int`: GetSpaceLeft's
//      int against the uint16's integral promotion.
//    * the failure block sits at 0x0045c274, AFTER the success return at
//      0x0045c26a.  /O2 preserves duplicated epilogues, so this is a real
//      trailing `return LTFALSE;` under a positive guard, not a nest.
//
//  HOME: src/engine/server/servermsg.cpp (the ServerMessage methods'
//  file), with the declaration added to servermsg.h's ServerMessage.
//  Because there is no server.dll address for it, annotate it with the
//  lithtech address.
// ----------------------------------------------------------------------- //

// lithtech.exe only (no server.dll twin, see above): not in the server build.
#ifndef LT_SERVER_BUILD

// src/engine/server/servermsg.h:313's ServerMessage, cut down to the four
// fields this row reads.  GetSpaceLeft is IN-CLASS (servermessage.cpp:1419
// already establishes that spelling and why); WriteData is declared
// OUT-OF-CLASS-style -- a non-template member with no body is the one thing
// /Ob1 never expands, which is what keeps 0x0045c265 a call.
class ServerMessageQ468
{
public:
	int GetSpaceLeft()                       // 0x1000e845 / expanded here
	{
		int nLeft = (int)m_MaxDataLen - (int)m_Unknown38 - 7;

		if (nLeft < 0)
			nLeft = 0;

		return nLeft;
	}

	// The count is a uint16: the callee masks it (0x0045c3af) and this
	// caller pushes an unmasked `dec %edx` (0x0045c261).
	void WriteData(void* pData, uint16 nBytes);   // 0x0045c390 / 0x1002616a

	LTBOOL AppendBody(ServerMessageQ468* pMsg);   // 0x0045c230

	uint8  m_pad_00[0x34];
	uint16 m_Unknown34;                      // 0x34  CPacket::m_DataLen
	uint16 m_MaxDataLen;                     // 0x36
	uint16 m_Unknown38;                      // 0x38  CPacket::m_Pos
	uint8  m_pad_3a[0x54 - 0x3a];
	uint8* m_pData;                          // 0x54
};

LTBOOL ServerMessageQ468::AppendBody(ServerMessageQ468* pMsg)
{
	if (GetSpaceLeft() >= pMsg->m_Unknown34 - 1)          // 0x0045c25a
	{
		WriteData(pMsg->m_pData + 1, pMsg->m_Unknown34 - 1);  // 0x0045c265

		return LTTRUE;                                    // 0x0045c26b
	}

	return LTFALSE;                                       // 0x0045c275
}

#endif // !LT_SERVER_BUILD


// ======================================================================= //
//
//  SECTION C -- the server manager's run (smgr.cpp / servermodel.cpp)
//
//  All four rows here are PROTOTYPES THE REPO ALREADY DECLARES AND NEVER
//  DEFINES, and all four pay in both images.
//
// ======================================================================= //

// stdlith's LTLink, as src/engine uses it.
class LTLinkQ468
{
public:
	LTLinkQ468* m_pPrev;                     // 0x00
	LTLinkQ468* m_pNext;                     // 0x04
	void*       m_pData;                     // 0x08
};

// ----------------------------------------------------------------------- //
//  0x00468860, 113 bytes, __cdecl, two arguments.
//  MERGE ONE FRESHLY COMPUTED SOUND STATE INTO THE STORED ONE.
//
//  ALREADY DECLARED AND NEVER DEFINED:
//      src/engine/server/smgr.cpp:8535
//          void snd_MergeSentState(CSoundSentState* pStored,
//                                  CSoundSentState* pNew);   // 0x10029b53
//  and CSoundSentState (smgr.cpp:8514) already has every field at the right
//  offset, taken from sb_Init2(&m_Bank89C, 10, ...).
//
//  *** THE TWO IMAGES ARE BYTE-IDENTICAL. ***  lithtech.exe 0x00468860 and
//  server.dll 0x10029b53 are both 113 bytes and all 113 bytes compare equal
//  (verified byte for byte).  /Os and /Ot agree here, so one spelling
//  collects two addresses and there is no /O2-shape question to answer.
//
//  WHAT PINS IT
//    * three blocks, one per change bit, in bit order 1, 2, 4 -- which is
//      the field order 0x02, 0x04, 0x06/0x08 that smgr.cpp:8510 already
//      names SNDSENT_FLAGS / SNDSENT_BYTE04 / SNDSENT_RANGE.
//    * each block's guard is `test dl,BIT ; jne INTO-THE-BLOCK` followed by
//      a compare whose equality skips it (0x0046886f/0x00468879,
//      0x0046888d/0x00468895, 0x004688a7/0x004688b9).  That is
//      `(flags & BIT) || a != b` -- the bit ALREADY set forces the copy
//      unconditionally, which is exactly smgr.cpp:8532's description.
//    * the change byte is RE-READ at the top of every block (0x00468888,
//      0x004688a2) rather than kept in a register across them, so it is
//      written out at each use.  Within one block it is NOT re-read: the
//      `or dl,BIT ; mov [eax],dl` pair (0x0046887b) reuses the loaded copy,
//      which is what `|=` gives.
//    * the compares load the STORED side into a register and compare
//      against the NEW side in memory (`mov si,[eax+2] ; cmp si,[ecx+2]`),
//      so the source writes `pStored->x != pNew->x` in that operand order.
//    * the third block guards TWO fields with one bit and copies both
//      (0x004688c0 the word, 0x004688c8 the byte), so its condition is a
//      three-term `||`.
//
//  HOME: src/engine/server/smgr.cpp, at the declaration on :8535.
// ----------------------------------------------------------------------- //

// src/engine/server/smgr.cpp:8510 and :8514, unchanged.
// ----------------------------------------------------------------------- //
//  0x0046a3e0, 55 bytes, __cdecl, two arguments.
//  THE OBJECT-ID MAP'S RECORD FOR ONE ID.
//
//  ALREADY DECLARED AND NEVER DEFINED:
//      src/engine/server/spritecontrol.cpp:451
//          void* sm_FindObjectIDRec(CServerMgr* pMgr, uint16 nID);  // 0x1002ad18
//  and CObjIDRec plus OBJIDREC_UNAVAILABLE (spritecontrol.cpp:434, :441) are
//  already there, read off sm_ClaimObjectID which walks the same list.
//  server.dll's copy is 0x1002ad18, 50 bytes.
//
//  WHAT PINS IT
//    * the list is CServerMgr::m_Link8c4 (servermgr.h:1807): `lea` of +0x8c4
//      for the head and a load of +0x8c8 for its m_pNext (0x0046a3e5,
//      0x0046a3eb), walked through +0x04 (0x0046a40c).
//    * the key is the record's +0x08 with OBJIDREC_UNAVAILABLE masked OUT
//      (`and esi,0xbfffffff`, 0x0046a402) -- the same 0x40000000 bit
//      sm_ClaimObjectID tests -- compared against the id.
//    * the id is widened from 16 bits (`and edx,0xffff`, 0x0046a3f9;
//      server.dll uses `movzx edx,WORD PTR [esp+0xc]`), so the parameter is
//      uint16 as declared, and the widening is hoisted into the preheader
//      because it is loop-invariant.
//    * `xor eax,eax` sits on the exhausted path ONLY (0x0046a413); the hit
//      returns the walking register, so the answer is the RECORD.
//    * the `lea` of the head precedes the load of m_pNext here only because
//      %eax is the base and the load destroys it -- no source order can be
//      read off that, so the `for` is written in the same shape as its
//      sibling sm_ClaimObjectID (spritecontrol.cpp:468).
//
//  HOME: src/engine/server/smgr.cpp (0x1002acfa / 0x1002ad18 / 0x1002ad4a
//  are one run and the other two are smgr.cpp's), with the declaration in
//  spritecontrol.cpp:451 left as it stands.
// ----------------------------------------------------------------------- //

// src/engine/server/spritecontrol.cpp:434 and :441, unchanged.
#define OBJIDREC_UNAVAILABLE_Q468 0x40000000

class CObjIDRecQ468
{
public:
	CObjIDRecQ468* m_pPrev;                  // +0x00
	CObjIDRecQ468* m_pNext;                  // +0x04
	uint32         m_nIndex;                 // +0x08
};

class CServerMgrIDListQ468
{
public:
	uint8      m_pad_000[0x8c4];
	LTLinkQ468 m_Link8c4;                    // 0x8c4
};

void* sm_FindObjectIDRecQ468(CServerMgrIDListQ468* pMgr, uint16 nID)
{
	CObjIDRecQ468* pCur;
	CObjIDRecQ468* pHead;

	// THE HEAD IS NAMED IN A LOCAL, declared BEFORE the walk pointer.
	// Written as the cast inline in the loop condition, VC6 recomputes the
	// head address with `add` inside the loop; naming it first makes the
	// address live from the top and the shipped bytes use `lea`.
	pHead = (CObjIDRecQ468*)&pMgr->m_Link8c4;

	for (pCur = (CObjIDRecQ468*)pMgr->m_Link8c4.m_pNext;      // 0x0046a3eb
	     pCur != pHead;                                       // 0x0046a3f1
	     pCur = pCur->m_pNext)                                // 0x0046a40c
	{
		if ((pCur->m_nIndex & ~OBJIDREC_UNAVAILABLE_Q468) == nID)  // 0x0046a402
			return pCur;                                      // 0x0046a40a
	}

	return LTNULL;                                            // 0x0046a413
}


// ======================================================================= //
//
//  SECTION D -- 0x0046bbf0, 235 bytes.  *** LOWEST-CONFIDENCE ROW ***
//
//  BUILD THE SERVER INTERFACE OBJECT.
//
//  WHAT IT CORRESPONDS TO.  server.dll splits this into TWO functions and
//  lithtech.exe's /O2 build FUSES them:
//
//      0x1002d355   48 bytes   the allocator wrapper.  ALREADY DECLARED AND
//                              NEVER DEFINED at src/engine/server/smgr.cpp:4985
//                                  class CUnknown928Obj* sub_1002d355(CServerMgr*);
//                              (sm_CreateUnknown928, smgr.cpp:4987, calls it).
//      0x1002d385  208 bytes   the constructor.  src/engine/server/svnew.cpp:914
//                              lists it as NOT RECONSTRUCTED and blocked on
//                              "the whole class graph".
//
//  48 + 208 = 256 against lithtech's 235: /Ot expanded the constructor into
//  the wrapper and then folded the wrapper's second null test, because the
//  pointer is provably non-null once the `new` guard has passed.  server.dll
//  keeps that test (0x1002d376) because the constructor is a call there.
//  The only `push 0x260` in either image is this pair (0x0046bbf1 /
//  0x1002d356), so the correspondence is not a guess.
//
//  WHAT CALLS IT: lithtech 0x004734e6, storing the result into
//  CServerMgr+0x928 -- servermgr.h:1817's m_pUnknown928.  The tail call is
//  0x00470130, which is si_SetupInterface (server 0x1002fd9b, smgr.cpp:2488);
//  the two disassemblies line up instruction for instruction at their heads
//  (`mov eax,[esp+4] ; mov ecx,<addr> ; mov [eax+0x20],<addr> ...`), so the
//  argument it takes is this whole object and the 125 function pointers it
//  fills start at the object's +0x20.
//
//  THE LAYOUT, read store by store off 0x0046bbf0 (server addresses agree
//  field for field at 0x1002d385):
//
//    +0x000  vptr        0x004a31ec written at 0x0046bc22, OVERWRITTEN with
//                        0x004a2ef4 at 0x0046bc7d.  Two stores survive
//                        because the sub-object constructor call at
//                        0x0046bc3d sits between them.
//    +0x004  vptr        0x004a19c8 (0x0046bc0f).  Written ONCE.
//    +0x008  -> +0x234   (0x0046bc89)
//    +0x00c  -> +0x248   (0x0046bc92)
//    +0x010  -> +0x22c   (0x0046bc8f)
//    +0x014  -> +0x228   (0x0046bc8c)
//    +0x018  -> +0x258   (0x0046bc95)
//    +0x01c  pOwner ? pOwner + 4 : NULL   (0x0046bc98..0x0046bca1)
//    +0x020..+0x223      si_SetupInterface's territory; untouched here
//    +0x224  pOwner      (0x0046bc83)
//    +0x228  vptr        0x004a16c8 (0x0046bc28).  That vtable's slot 3 is
//                        0x004896d0 = CLTTransform::GetRot (already written,
//                        src/engine/lithtech/lt2new2.cpp), so this sub-object
//                        is ILTTransform.
//    +0x22c  vptr        0x004a3180 (0x0046bc34)
//    +0x230  pOwner      (0x0046bc3a)
//    +0x234  vptr        0x004a236c by the base constructor CALL at
//                        0x0046bc3d (0x004347a0 stores the vptr and zeroes
//                        +0x0c), then 0x004a3138 at 0x0046bc4e
//    +0x238  -> +0x228   (0x0046bcc5)
//    +0x23c  -> +0x22c   (0x0046bcb6)
//    +0x240  <- set to 0 by the base constructor, then to &this->+0x04 by
//                        the CALL at 0x0046bcaa (0x004347b0 is a one-line
//                        setter writing its argument to this+0x0c)
//    +0x244  pOwner      (0x0046bcaf)
//    +0x248  vptr        0x004a30ec (0x0046bc62)
//    +0x24c  1           (0x0046bc6b)
//    +0x250  -1.0f       (0x0046bc5b, 0xbf800000)
//    +0x254  pOwner      (0x0046bc68)
//    +0x258  vptr        0x004a30dc (0x0046bc72)
//    +0x25c  pOwner      (0x0046bc78)
//    ------  0x260 total, which is what `new` asks for.
//
//  RESOLVED (both images identical, lithtech 0x0046bbf0 and server's
//  constructor 0x1002d385).  The earlier two-base reading was wrong on three
//  counts, each read off the store order:
//    1. +0x04 is a MEMBER of the +0x00 base, not a second base: it is
//       constructed before that base's own vptr store and never rewritten.
//    2. the sub-objects' pOwner stores sit between THEIR vptr stores, so
//       they are member constructors run from an initialiser list;
//       +0x248's -1.0f precedes its vptr, so it is a base constructor's.
//    3. SetTable is passed &m_Iface (this+4), not &m_pSub234 (this+8) --
//       the masked displacement hid it.
//  If this comes back wrong, the field map above is still worth keeping --
//  it is what svnew.cpp:914 was missing.
//
//  HOME: src/engine/server/commonphysics.cpp for the constructor (svnew.cpp
//  already calls that), src/engine/server/smgr.cpp for the wrapper.
//
// ======================================================================= //

//  MOVED.  Both halves now live in src/engine/server/commonphysics.cpp under
//  their real names: CLTServer::CLTServer (0x1002d385, marked `inline` so
//  lithtech.exe's /O2 expands it) and sub_1002d355 (the wrapper smgr.cpp
//  declares), which match 0x1002d385 / 0x1002d355 on server.dll and
//  0x0046bbf0 on lithtech.exe.  The Q468 class graph that stood here was a
//  byte-identical stand-in whose vtables named virtuals nothing defines, so
//  it could not link.


// ======================================================================= //
//
//  SECTION E -- 0x0046b8f0, 125 bytes, __cdecl, four arguments.
//  WHICH MOUNTED TREE HAS THIS FILE?
//
//  ALREADY DECLARED AND NEVER DEFINED, TWICE:
//      src/engine/server/serverengine.cpp:1231
//      src/engine/server/counter.cpp:903
//          int fs_FindFileInTrees(CFileMgr* pFileMgr, char* pFilename,
//                                 CFileTreeRec** ppTreeRec, uint32* pUnknown);
//  called by se_LoadFile (serverengine.cpp:1300, and the shipped call is
//  lithtech 0x0046ba2e inside se_LoadFile at 0x0046b9d0) and by
//  counter.cpp:958.  server.dll's copy is 0x1002bde9, 88 bytes; it has an
//  %ebp frame where lithtech's is %esp-relative, which is the only
//  structural difference between the two.
//
//  Its CLIENT TWIN IS ALREADY WRITTEN and says what the loop is:
//  src/engine/lithtech/clientwin.cpp:1715, clu_fm_FindTreeWithFile
//  (0x004043b0, 89 bytes) -- the same walk over the same 0x110-byte find
//  record, without this one's two out-parameters.
//
//  WHAT PINS IT
//    * `sub $0x110,%esp` and nothing else on the frame: the whole frame is
//      src/engine/server/filemgr.cpp:66's CFileFindState, whose size that
//      file already fixes at 0x110 from ar_FindNext's own reach to +0x10c.
//    * the first argument is walked as a list head -- `mov esi,[ebx+4]` and
//      `cmp esi,ebx` (0x0046b900, 0x0046b904) -- which is CFileMgr::m_Trees
//      at offset zero (servermgr.h:1536).
//    * `mov ecx,[edi+0xc]` (0x0046b918) off the link's m_pData is
//      CFileTreeRec::m_pTree (serverengine.h:74), handed to 0x00420d50 =
//      server 0x10006fb4, the per-tree lookup clientwin.cpp:186 names
//      clu_fs_GetFileInfo.
//    * the SECOND out-parameter takes `[esp+0x118]` (0x0046b954), which is
//      %esp0-8 -- INSIDE the frame, at state + 0x108.  That is
//      CFileFindState::m_nSize, and it is exactly the field
//      serverengine.cpp:1228 describes as "the dword at its +0x108".
//    * both out-parameters are null-tested (0x0046b943, 0x0046b950) and both
//      tests jump FORWARD over their own store, so both are plain
//      `if (p) *p = ...;` statements, not a nest.
//    * the two answers are SEPARATE returns with separate epilogues
//      (0x0046b92f and 0x0046b95d); /O2 preserves them.  server.dll's /O1
//      tail-merges them onto one `leave ; ret` (0x1002be38 jumps into
//      0x1002be3c), which is the whole of the 125-vs-88 difference along
//      with the frame pointer.
//    * pFilename is hoisted into %ebp in the preheader (0x0046b908) -- a
//      loop-invariant hoist with no source consequence.
//
//  HOME: src/engine/server/serverengine.cpp, immediately above se_LoadFile,
//  where the declaration on :1231 already is.  COFF order agrees: server.dll
//  puts 0x1002bde9 directly before se_LoadFile at 0x1002be8c, and
//  lithtech.exe puts 0x0046b8f0 directly before se_LoadFile at 0x0046b9d0.
//
// ======================================================================= //

// src/engine/server/filemgr.cpp:66.
// *********************************************************************** //
//
//  CLOSING REPORT
//
// *********************************************************************** //
//
//  ---- EXTENT DISCIPLINE -------------------------------------------------
//
//  Every one of the eleven rows was swept with a full .text scan of every
//  e8 / e9 / 0f8x / short-jcc displacement landing inside it.  Results:
//
//    0x0045c230  75  sound.  One caller (0x00415942, inside 0x00415810).
//                    Last insn `ret 0x4` at 0x0045c278, padding from
//                    0x0045c27b.  No branch enters from outside.
//    0x0045c630  79  sound.  One caller (0x00423dad).  `ret` at 0x0045c67e,
//                    padding at 0x0045c67f.
//    0x0045f430 102  *** TRUNCATED ***.  The real function is 191 bytes,
//                    0x0045f430..0x0045f4ee.  See the exclusion line below.
//    0x0045f496  89  *** NOT A FUNCTION *** -- interior block.
//    0x0045f660  59  sound.  Four callers (0x0042e908, 0x0042ea40,
//                    0x0042ec71, 0x0045d278).  `ret` at 0x0045f69a.
//    0x00464060  59  sound.  One caller (0x004640d1, inside sm_RemoveClient
//                    at 0x004640a0).  `ret` at 0x0046409a.
//    0x00468860 113  sound.  Two callers (0x004684d6, 0x00468ad2).  `ret` at
//                    0x004688d0.
//    0x00469c70 145  sound.  0x00469c70 + 145 = 0x00469d01; `ret` at
//                    0x00469d00, padding after.  The one apparent inbound
//                    jump the sweep reported (0x00469d6e -> 0x00469cf8) is a
//                    FALSE HIT: 0x00469d6e is the last byte of
//                    `mov DWORD PTR [esp+0x7c],ebx` at 0x00469d6b, i.e. the
//                    displacement 0x7c read as a `js rel8` opcode.
//    0x0046a3e0  55  sound.  One caller (0x00473059).  `ret` at 0x0046a416.
//    0x0046b8f0 125  sound.  Two callers (0x0042d964 and 0x0046ba2e, the
//                    latter inside se_LoadFile at 0x0046b9d0).  `ret` at
//                    0x0046b96c.  The apparent inbound jump
//                    (0x0046b984 -> 0x0046b949) is a FALSE HIT: 0x0046b984
//                    is the third byte of `add esp,0x78 ; ret` at
//                    0x0046b982.
//    0x0046bbf0 235  sound.  One caller (0x004734e6).  `ret` at 0x0046bcda,
//                    padding from 0x0046bcdb, and 0x0046bce0 is the next
//                    function (a `??_G` scalar deleting destructor).
//
//  ---- EXCLUSION LINE (0x0045f496 is not a function) --------------------
//
//  0x0045f496,"interior block of the function at 0x0045f430, not a function. 0x0045f430 + 102 = 0x0045f496 exactly and the two rows tile ONE 191-byte function 0x0045f430..0x0045f4ee. 0x0045f496 is the push ebp that opens the fill loops preheader; the je at 0x0045f494 jumps over it to 0x0045f4dd; a full e8/e9/0f8x/short-jcc sweep of .text finds ZERO references to 0x0045f496 from anywhere in the image; the range has one prologue (push esi at 0x0045f430) and its two ret 4 at 0x0045f4e2 and 0x0045f4ec end exactly where the nop padding starts at 0x0045f4ef. Same family as the truncated-head plus interior-block shape seen at 0x00442947. The whole function is reconstructed as FMConvertMgr::Fill in the 0x00468000 cluster."
//
//  ---- WHAT BELONGS IN ANOTHER FILE --------------------------------------
//
//  Nothing here is a drop-in TU.  Per-row homes, all of them existing files:
//
//    src/engine/lithtech/fmconvert.cpp   0x0045f430, 0x0045f660
//    src/engine/server/commonmodel.cpp   0x0045c630  (declaration at :4377)
//    src/engine/server/servermsg.cpp     0x0045c230  (+ a declaration in
//                                        servermsg.h's ServerMessage)
//    src/engine/server/smgr.cpp          0x00464060 (:6839), 0x00468860
//                                        (:8535), 0x0046a3e0 (declaration
//                                        lives in spritecontrol.cpp:451),
//                                        and 0x0046bbf0's wrapper (:4985)
//    src/engine/server/servermodel.cpp   0x00469c70  (declaration at :255)
//    src/engine/server/serverengine.cpp  0x0046b8f0  (declaration at :1231;
//                                        counter.cpp:903 declares it too)
//    src/engine/server/commonphysics.cpp 0x0046bbf0's constructor
//                                        (svnew.cpp:914 says so)
//
//  ONE DECLARATION NEEDS CORRECTING: smgr.cpp:6840 types sub_100272d6 as
//  `void`; it returns LT_OK.  sm_RemoveClient discards the result, so the
//  caller's bytes cannot move.
//
//  ---- UNSETTLED COIN-FLIPS, both spellings given -----------------------
//
//  1. 0x0045c230, the doubled `m_DataLen - 1`.  ONE 16-bit load feeds TWO
//     decrements (0x0045c259 on the zero-extended copy, 0x0045c261 on the
//     partial register).  Written as the member expression twice:
//         if (GetSpaceLeft() >= pMsg->m_Unknown34 - 1)
//             WriteData(pMsg->m_pData + 1, pMsg->m_Unknown34 - 1);
//     ALTERNATIVE, identical tree and identical load count:
//         uint16 nLen = pMsg->m_Unknown34;
//         if (GetSpaceLeft() >= nLen - 1)
//             WriteData(pMsg->m_pData + 1, nLen - 1);
//     If VC6 commons the subtraction and emits ONE `dec`, neither spelling
//     is the answer and the two uses differ in the source in some way the
//     bytes do not show.
//
//  2. 0x0045f660, `if`-chain versus `switch`.  Written as the chain; a
//     three-label switch over 4..6 that VC6 declines to table produces the
//     same sequential compares.  Size-neutral either way.
//
//  3. 0x0045f430's two inner loops and its outer loop.  Written as counting
//     `for`s, because the counter is dead in every body (the walk is a
//     pointer) and that is the form VC6 converts to the countdown the bytes
//     show.  If the back edge comes back with a `cmp` in it, the spelling is
//         if (n) { do { ... } while (--n); }
//     at all three sites.
//
//  4. 0x0045f430's `pRequest->m_pFormat->m_nCode` reads.  Written out at
//     each use; VC6 commons the pointer load inside a block, which is why
//     0x0045f447 loads it once for both comparisons and 0x0045f476 reloads
//     it after the WritePixel call.  A `FMFormat* pFormat` local at the top
//     of the guard is the alternative and should produce the same bytes --
//     but it would ALSO have to be reloaded after the call, which a local
//     will not do, so the member expression is the safer spelling.
//
//  5. 0x0046bbf0's whole class graph -- see the three numbered risks in
//     Section D.  This is the row to measure first and revert first.
//
// *********************************************************************** //
