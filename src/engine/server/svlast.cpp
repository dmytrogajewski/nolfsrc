// server.dll — the last of the unwritten bodies (recon). Build /O1.
//
// This file is a HOLDING PEN, not a translation unit the shipped build had.
// Every item below carries a MOVE-ME banner naming the file it belongs in;
// it was written here without touching the existing sources. Two standing warnings apply, both copied from
// svnew.cpp because they have not stopped being true:
//
//   * an isolated harness TU can invent a residual —
//     VC6's decision to merge two identical blocks depends on what else in
//     the TU uses the string literals they push. None of the three bodies
//     below pushes one — the only literals in this TU belong to the
//     CPacket::ReadType<uint16> instantiation the third body pulls in, and
//     those live in that template's own COMDAT. The risk is judged
//     acceptable; it is not zero.
//
//   * `src/engine/server/*.cpp` is compiled a SECOND time into lithtech.exe
//     at /O2. A NEW file enters the server build only, so nothing here moves
//     an already-matched function in either image. That stops being true the
//     moment one of these bodies is moved to its real home, and the client
//     side has to be re-measured then. THE TWO GLOBALS BELOW ARE THE
//     DANGEROUS ONE: moving them into dsi.cpp adds two .CRT$XCU entries and
//     two .bss objects to a TU both images compile.
//
// Everything here is evidence-backed against the retail server.dll; the
// per-address reasoning is in each banner.
#include <string.h>

#include "servermgr.h"
#include "servermsg.h"
#include "packet.h"


// *********************************************************************** //
//  MOVE-ME: dsi.cpp — two file-scope vectors, 0x1000e4c6 and 0x1000e4e4.
//
//  These are not hand-written functions at all: they are the bodies of two
//  compiler-generated dynamic initializers. The enumeration's rows start
//  five bytes earlier, at 0x1000e4c1 and 0x1000e4df, and those five bytes
//  are the `_$E` thunk VC6 emits in front of every such initializer —
//  `e9 00 00 00 00`, a `jmp rel32` whose displacement the linker resolved to
//  zero because it placed the body COMDAT immediately after the thunk
//  COMDAT. The proof is threefold:
//
//    * the image holds exactly eighteen `e9 00 00 00 00` sequences inside
//      the app region, and seventeen of them are the target of a pointer in
//      the run at .data 0x10051004..0x10051064 — that run IS `.CRT$XCU`,
//      merged into .data the way VC6 merges it. (The eighteenth, 0x10016361,
//      has no reference anywhere in the file at all; it is the same shape and
//      its object was presumably dropped from the table, not from the image.)
//      Nothing else in the app region has that shape;
//    * our own objects already emit that shape — commonphysics.obj has
//      `_$E2` (5 bytes) in front of `_$E1` (168), and a byte match places _$E1
//      at 0x1001e747, five past the row start 0x1001e742;
//    * nothing but the thunk reaches either body: a `jmp`-target scan of
//      the whole of .text finds 0x1000e4c1 -> 0x1000e4c6 and nothing else.
//
//  So the work is to DEFINE THE GLOBALS, and the thunks come for free.
//
//  The TU is dsi.cpp: the row sits between dsi_OutOfMemory (0x1000e443,
//  matched) and dsi_GetConsoleBindings (0x1000e4fd, matched), both of which
//  are already written there.
//
//  What the two objects are: twelve bytes each, three floats, all zero. The
//  one at 0x10058450 is initialised first (its thunk holds the LOWER
//  .CRT$XCU slot, 0x10051030, against 0x10051034 for 0x10058440), so it is
//  declared first here. Neither is READ anywhere — a scan for all four
//  words of each finds only the two initializers themselves, and the next
//  live global is 0x1005845c (read by 0x100281b7), which is what pins the
//  first object's length at exactly twelve bytes. Write-only file-scope
//  vectors, i.e. leftovers; the names are ours, by address.
//
//  Spelling: `NOLF_FORCE_INLINE` is not decoration, it is a measured finding —
//  at /O1 VC6 will NOT expand a constructor into the `_$E` body, not from
//  an in-class definition and not from plain `inline`; it emits
//  `mov ecx,&g; push; push; push; call ??0...`. The shipped body has the
//  global's address folded into each store and no `this` register, which is
//  the NOLF_FORCE_INLINE shape. If the original was literally
//  `LTVector g_x(0.0f, 0.0f, 0.0f);` then ltvector.h's in-class ctor is not
//  what VC6 saw, and the view class below is the spelling that reproduces
//  the bytes. THE COIN-FLIP, both spellings exact:
//      CVectorZeroInit g_Unknown58450(0.0f, 0.0f, 0.0f);        // used here
//      LTVector    g_Unknown58450(0.0f, 0.0f, 0.0f);        // needs a
//                                                           // NOLF_FORCE_INLINE
//                                                           // ctor on LTVector
//  and the second one must not be tried without re-measuring lithtech.exe,
//  because ltvector.h is shared by both images and every other TU.
//
//  Shipped, both bodies (identical up to the relocations, which is why one
//  symbol of ours covers both rows):
//      fldz ; fstps [g+0]
//      fldz ; fstps [g+4]
//      fldz ; fstps [g+8]
//      ret
//  Three separate `fldz` and no `fst`-without-pop: the zero is re-materialised
//  per store rather than kept on the FPU stack, which is the opposite of what
//  commonphysics' g_AxisDirs does with its shared -1.0f (`flds` once, `fsts`
//  twice, the survivor popped last). That is the tell that this is three
//  independent constant stores and not one common subexpression.
// *********************************************************************** //

// NOT commonphysics.h's CVectorInit, which is deliberately OUT OF LINE
// because the shipped build calls it at 0x1001e6d7. This one must
// EXPAND -- the initialiser body is three fldz/fstps pairs and a ret,
// with no call in it -- so it is a separate type with its own name.
class CVectorZeroInit
{
public:
	NOLF_FORCE_INLINE CVectorZeroInit(float fx, float fy, float fz)
	{
		x = fx;
		y = fy;
		z = fz;
	}

	float x;
	float y;
	float z;
};

// 0x10058450 — .CRT$XCU slot 0x10051030, initializer 0x1000e4c1/0x1000e4c6.
CVectorZeroInit g_Unknown58450(0.0f, 0.0f, 0.0f);

// 0x10058440 — .CRT$XCU slot 0x10051034, initializer 0x1000e4df/0x1000e4e4.
// Four bytes at 0x1005844c sit between the two and are referenced by
// nothing, so whether they are padding or a third dead global is open.
CVectorZeroInit g_Unknown58440(0.0f, 0.0f, 0.0f);


// *********************************************************************** //
//  MOVE-ME: console.cpp — beside cc_ShowGameVars / cc_World, which are the
//  neighbouring entries of g_ConsoleCommands and are already written there.
//
//  0x10028d47, 55 bytes, reached from ONE call site: 0x10028c6b, inside the
//  "objectinfo" console command at 0x10028be3 (console.cpp already names
//  that address from the command table at 0x10053d80).
//
//  The caller is what fixes every type. It builds three parallel int[500]
//  counter arrays (0x7d0 bytes each, at -0x7e0, -0xfb0 and -0x1780) and one
//  name table (-0x7928 .. -0x1780, which is 0x61a8 = 500 * 0x32), walks the
//  server's object list, and for each object pushes
//
//      push [[pObj->m_pClass...+0x24]]   the class name
//      push [ebp-8]                      the table cursor
//      push [ebp-4]                      how many rows are filled
//      lea  eax,[ebp-0x10] ; push eax    where to put the row index
//      call 0x10028d47 ; add esp,0x10
//
//  then, on a zero answer, `strncpy(row, name, 0x31)` (0x1003f050) and
//  `[ebp-4]++ ; [ebp-8] += 0x32` — so the stride is 50 and the names are
//  `char[50]` with 49 significant characters. The row index it wrote is
//  then used to bump the three counters. cdecl, four arguments, `int`
//  return: 1 found, 0 not.
//
//  0x1003e1f0 is strcmp, NOT stricmp — console.cpp:555 and
//  commonmodel.cpp:2495 both already pin that address, and the engine's own
//  case-folding compare is a different function (0x1000e48a, which
//  uppercases a..z inline).
//
//  Two shapes in the bytes worth keeping honest about:
//
//    * the EBP frame. /O1 is /Oy, and the sibling at 0x10029f42 below has
//      no frame — the difference is the number of static parameter
//      references. Five here (0x10(%ebp) twice, plus 0x8, 0xc and 0x14
//      once each) at one byte saved apiece beats the four bytes a frame
//      costs; three there does not. Nothing in the source chooses this.
//
//    * the loop increment order is `inc %esi ; add $0x32,%edi`, so the
//      cursor advance has to live in the `for` increment and not at the
//      bottom of the body — written the other way the two come out swapped.
//
//    * the shared exit: `return 0` is the textually LAST return and gets
//      the inline `pop edi ; pop esi ; pop ebp ; ret`; the `return 1` block
//      is moved out past it and jumps back in (`push $1 ; pop eax ; jmp`).
//      That is the standing lever, spelled the standing way.
//
//  Name ours.
// *********************************************************************** //

int cc_FindNameIndex(char* pName, char* pTable, int nCount, int* pnIndex)
{
	int i;

	for (i = 0; i < nCount; i++, pTable += 50)
	{
		if (strcmp(pName, pTable) == 0)
		{
			*pnIndex = i;

			return 1;
		}
	}

	return 0;
}


// sm_Unknown29f42 (0x10029f42) moved to smgr.cpp, above sm_ReadClientCommands.


// *********************************************************************** //
//  THE REST OF THE ROWS: rows that are not functions to write.
//
//  Nine more enumerated addresses, and none of them needs a body.
//  They are recorded here rather than dropped, because a row nobody explains
//  comes back around, and because an address comment is what marks a row
//  as accounted for.
//
//  --- five `gap` rows that are NOT FUNCTIONS ---------------------------- //
//
//  Each is an interior point of a function that starts EARLIER, and in four
//  of the five the enumeration also lists that real start as its own short
//  row — the split happens at an early-out `ret`, which looks like an end.
//  An extent check validates a row's EXTENT and never its START, so all
//  five pass it: the extent runs to the real function's `ret` every time.
//
//    0x10019eba (58)  — the `rep movsl` copy loop inside
//                       CMoArray<LTMatrix,NoCache>::InternalNiceSetSize at
//                       0x10019e60, which a byte match places from
//                       collection.obj at 148 bytes (0x10019e60..0x10019ef4).
//                       0x10019eba is reached only by the `jbe` at
//                       0x10019eb8 falling through, and the `pop %edi` that
//                       ends the block pairs with a `push %edi` above it.
//                       The enumeration cut the row at 0x10019e60 + 90.
//
//    0x10029f4f (103) — the fall-through of sm_Unknown29f42's null guard;
//                       see that function above. Real start 0x10029f42.
//
//    0x10030f43 (69)  — the tail of si_GetLastCollision (0x10030ef3), which
//                       a byte match places from commonphysics.obj at 149 bytes
//                       (0x10030ef3..0x10030f88). The enumeration gave the
//                       first row 80 bytes, cutting at the `jne 0x10030f43`
//                       that jumps over the `push $1 ; pop %eax` early
//                       return; %esi at 0x10030f46 is loaded before the cut
//                       and `pop %esi` at 0x10030f86 closes the real
//                       prologue.
//
//    0x10035d64 (102) — the body of sb_CreateStringRaw (0x10035d4b), placed
//                       from stringbank.obj at 127 bytes; the cut is at the
//                       `jne 0x10035d64` over the empty-string early return,
//                       and %ebx is the caller's argument register from
//                       0x10035d4c through the `pop %ebx` at 0x10035dc8.
//
//    0x10035de4 (90)  — the same thing one function later:
//                       sb_CreateString (0x10035dca), placed at 116 bytes.
//                       Four pops close three pushes plus the enclosing
//                       function's `push %ebp`.
//
//  --- four rows that are a `_$E` thunk in front of a body we already have //
//
//  Same shape as the two dsi vectors at the top of this file: the row starts
//  five bytes early, on the compiler's dynamic-initializer thunk, and the
//  body behind it is already written and already placed. The rows can never
//  be credited as they stand — a byte match skips a 5-byte symbol whose only
//  four variable bytes are a relocation, and a short-symbol
//  shape test wants the row LENGTH to match. The fix is to the enumeration
//  (split each of these into a 5-byte row and a body row), not to the
//  sources.
//
//    0x10001f00 (209) — thunk for _$E13 at 0x10001f05, 204 bytes: the six
//                       LTPlane axis planes, already worldsim.cpp's
//                       g_BoxPlanes[6] and already matched from worldsim.obj.
//
//    0x10005f12 (38)  — thunk for the body at 0x10005f17, 33 bytes, already
//                       counter.cpp's cnt_Init and already matched. Note
//                       what the thunk means: NOTHING in the image calls
//                       0x10005f17 (a whole-of-.text scan of `call`/`jmp`
//                       rel32 finds only 0x10005f12's `jmp`), so the shipped
//                       source had no free function here at all — it had a
//                       file-scope object whose constructor is this body.
//                       That change is to counter.cpp, which
//                       lithtech.exe compiles too, and it buys five
//                       unverifiable bytes.
//
//    0x1001e742 (173) — thunk for _$E1 at 0x1001e747, 168 bytes, already
//                       commonphysics.cpp's g_AxisDirs[6] and already
//                       matched from commonphysics.obj.
//
//    0x10005fab (56)  — NOT a stub. The body counter.cpp already has —
//                       `uint32 CBaseCounterReadView::sub_10005fab()
//                       { return Read(); }` — is the whole of this function:
//                       five bytes, `e9 b5 ff ff ff`, `jmp 0x10005f65`, and
//                       0x10005f65 is CBaseCounter::Read. The 56-byte row is
//                       an extent artefact of the same kind as the four
//                       above, in the other direction: 0x10005fb0 is
//                       CBaseCounter::ReadRaw (51 bytes, matched from
//                       counter.obj) and it is missing from the start list,
//                       so the row swallowed it. 5 + 51 = 56. Three call
//                       sites reach the thunk (0x10015fa6, 0x10023e06,
//                       0x10023e71) and one reaches Read directly
//                       (0x10023860), which is exactly the distinction
//                       a call-target check is there to keep.
// *********************************************************************** //

// *********************************************************************** //
//  server.dll — scratch TU.  Build /O1 (/Og /Os /Oy /Ob1).
//
//  FOUR ENUMERATED ROWS.  ONLY ONE NEEDS A BODY.
//
//    0x100142ae  607  RECONSTRUCTED BELOW (the g_AxisBases dynamic init).
//    0x100305dd   65  ALREADY WRITTEN — smgr.cpp:3337 si_GetPropRotationEuler.
//    0x100314dc   71  ALREADY WRITTEN — smgr.cpp:4251 si_GetNextObject.
//    0x10031523   74  ALREADY WRITTEN — smgr.cpp:4267 si_GetNextInactiveObject.
//
//  The three already-written rows are all DOCUMENTED NEARS in smgr.cpp and
//  the address-grep screen missed them because smgr.cpp identifies those
//  three functions by their ServerDE slot offset (0x118 / 0x170 / 0x174)
//  and never writes their addresses down.  See the banners
//  further down for the exclude lines and for what each near still needs.
//
//  Everything below is read off the retail server.dll, cross-checked
//  against the /O2 twin in the retail lithtech.exe, and cited by address.
// *********************************************************************** //

#include "ltbasedefs.h"


// ======================================================================= //
//  >>> ROW 0x100142ae, 607 bytes (0x100142ae..0x1001450c). <<<
//
//  MOVE-ME: src/engine/server/svlast.cpp, NOT servermath.cpp.  See the
//  "WHICH FILE" banner at the bottom of this block — putting it in
//  servermath.cpp would give lithtech.exe a SECOND copy of an initializer
//  it already matches out of src/engine/lithtech/wgrid.cpp.
//
//  WHAT IT IS.  The compiler-generated dynamic initializer (`_$E`) for a
//  file-scope array of six 36-byte basis records.  Confirmed, not assumed:
//
//    * 0x100142a9 is `e9 00 00 00 00`, a five-byte thunk whose displacement
//      the linker resolved to zero because it placed the body COMDAT
//      immediately after it.  .data 0x10051050 — a slot in the .CRT$XCU run
//      0x10051004..0x1005107c — holds 0x100142a9.  (The `??__E`
//      shape; svlast.cpp already reconstructs two of these.)
//    * The /O2 twin proves the whole construct outright:
//         lithtech.exe 0x0043c840  e9 0b 00 00 00  jmp 0x0043c850
//         server.dll   0x100142a9  e9 00 00 00 00  jmp 0x100142ae
//      Same two-COMDAT shape, displacement zero only in the image whose
//      linker needed no padding.
//    * EXTENT SOUND.  A sweep of every e8 / e9 / 0f8x / short-jcc in .text
//      for displacements landing in 0x100142ae..0x1001450c finds exactly
//      one edge from outside: the thunk.  The body opens `push ebp` at
//      0x100142ae and the last instruction is `ret` at 0x1001450c, so
//      0x1001450d - 0x100142ae = 0x25f = 607 exactly.  0x1001450d is the
//      next function (the constructor).  Nothing to split, nothing to trim.
//
//  THE ARRAY.  Six `rep movs` of nine dwords each land at
//
//      0x1001430a call -> 0x10058610      0x100143d1 call -> 0x10058658
//      0x10014370 call -> 0x10058634      0x10014434 call -> 0x1005867c
//      0x10014497 call -> 0x100586a0      0x100144f8 call -> 0x100586c4
//
//  0x24 apart and contiguous: one array of six 36-byte records, base
//  0x10058610.  servermath.cpp already names it `g_AxisBases` and already
//  carries the three functions that use it, all three byte-identical:
//
//      0x1001450d  the constructor        (identical)
//      0x10014533  math_FindClosestAxisBasis, walks 0x1005862c ..
//                  0x10058704 in steps of 0x24 = six records   (identical)
//      0x1001457f  math_GetAxisBasisVectors                    (identical)
//
//  WHY A COPY AND NOT AN IN-PLACE CONSTRUCTION — THE ONE THING THAT IS EASY
//  TO GET WRONG.  Each site builds three LTVector temporaries in the frame,
//  pushes them by value, constructs ONE 36-byte temporary at [ebp-0x48]
//  (the same slot all six times), and only then `rep movs` nine dwords out
//  of the constructor's returned `this` into the global.  An array whose
//  ELEMENT TYPE is the constructed type would be built in place —
//  `lea ecx,&g_AxisBases[k]` and no copy at all.  The copy is there because
//  the element type is the PLAIN base and the initializer expression is a
//  DERIVED class carrying the constructor, so the slice cannot be elided.
//  That is exactly the spelling src/engine/lithtech/wgrid.cpp already uses
//  for the /O2 twin at 0x0043c850, where it comes out 1031 of 1031 bytes
//  byte-identical.  This block is that proven source, retagged.
//
//  THE VALUES.  Read off the /O1 x87 immediates (fldz = 0.0f, fld1 = 1.0f,
//  flds .rdata 0x1004d1f4 = -1.0f) and then re-read INDEPENDENTLY off the
//  /O2 twin's integer immediates (0x3f800000 / 0xbf800000) at
//  0x0043c858 / 0x0043c902 / 0x0043c9a7 / 0x0043ca56 / 0x0043cafb /
//  0x0043cbaa.  Both readings agree on all eighteen vectors:
//
//    [0] (1,0,0)  (0,0,-1) (0,1,0)    normal +Y
//    [1] (1,0,0)  (0,0,1)  (0,-1,0)   normal -Y
//    [2] (1,0,0)  (0,1,0)  (0,0,1)    normal +Z
//    [3] (1,0,0)  (0,-1,0) (0,0,-1)   normal -Z
//    [4] (0,0,1)  (0,-1,0) (1,0,0)    normal +X
//    [5] (0,0,-1) (0,-1,0) (-1,0,0)   normal -X
//
//  Every row is right-handed (right x up == normal), which is the
//  independent check that the argument order is not transposed.  The
//  argument order itself is pinned twice over: the constructor at
//  0x1001450d copies [esp+0xc] -> this+0x00, [esp+0x18] -> this+0x0c and
//  [esp+0x24] -> this+0x18, and the LOWEST push slot at each call site
//  (the `sub esp,0xc` issued LAST) is the one that carries the first
//  argument.  NOTE the trap: the three frame slots -0x24 / -0x18 / -0xc
//  ROTATE between the six sites — statement 1 pushes arg1 out of [ebp-0x24]
//  and statements 2..6 push arg1 out of [ebp-0xc].  That is stack-slot
//  recycling, i.e. allocation, not source; read the `sub esp,0xc` order,
//  never the slot number.  (Reading the slot number gives a table in which
//  three records share a normal vector, which cannot be right.)
//
//  THE SHARED -1.0f IS A CSE AND COMES FOR FREE.  Statements using -1.0f
//  once emit `mov DWORD PTR [ebp-0x??],0xbf800000` (0x100142c9, 0x10014340,
//  0x1001447f); the statement using it twice loads it once and keeps it on
//  the FPU stack (`fld` 0x100143ec, `fst` 0x100143f4, `fstp` 0x100143fc);
//  the statement using it three times does the same with two `fst`s
//  (0x1001449c / 0x100144a6 / 0x100144c2 / 0x100144db).  That is the same
//  fingerprint commonphysics.cpp's g_AxisDirs (0x1001e747) already turns
//  on, and it follows from writing the literals out — no spelling needed.
//
//  WHICH FILE.  svlast.cpp, which is compiled into server.dll ONLY.
//  servermath.cpp is compiled into BOTH images (build/engine.sh lines 178
//  and 331), and lithtech.exe ALREADY MATCHES this initializer at
//  0x0043c850 out of src/engine/lithtech/wgrid.cpp
//  (1031 bytes, identical).
//  Defining the array in servermath.cpp would emit a second 1031-byte
//  initializer COMDAT plus a second .CRT$XCU slot into lithtech.exe —
//  precisely the hazard svlast.cpp's own banner flags as "THE DANGEROUS
//  ONE".  servermath.cpp declares it `extern SAxisBasis g_AxisBases[6];`
//  with the same two types spelled the same way, so for the whole-DLL link
//  this is the one definition that extern resolves to.
//
//  THE CONSTRUCTOR IS DECLARED HERE AND NOT DEFINED.  servermath.cpp
//  already emits it (0x1001450d, identical); defining it again here would
//  put a duplicate 38-byte COMDAT in the image for nothing.  Declaring it
//  out-of-class is also what keeps the shipped CALL: /Ob1 never expands a
//  non-template function declared out of class, and the
//  shipped body calls at all six sites.
// ======================================================================= //




// ======================================================================= //
//  ROW 0x100305dd, 65 bytes — NOT WORK.  ALREADY WRITTEN.
//
//  It is `si_GetPropRotationEuler`, src/engine/server/smgr.cpp:3337,
//  declared at smgr.cpp:2564 and installed at smgr.cpp:3690.  smgr.cpp
//  already carries it as a documented NEAR of 53 of 65 bytes.
//
//  IDENTIFICATION, not by string pool: the ServerDE slot installers in
//  0x1002fec4..0x1002ff20 assign the property readers to slots
//  0xf8..0x120 in one run, and 0x1002ff0c stores 0x100305dd into +0x118.
//  servermsg.h:824 has `LTRESULT (*GetPropRotationEuler)(char*, LTVector*)`
//  at +0x118, and src/LT2/sdk/inc/iltserver.h:459 has the same prototype.
//
//  EXTENT SOUND: `push ebp` at 0x100305dd, `ret` at 0x1003061d,
//  0x1003061e - 0x100305dd = 0x41 = 65; the only edges into the range are
//  its own two `je`/`jne` to the 0x10030619 error tail.
//
//  WHAT THE REMAINING 12 BYTES ARE, for whoever works the near next.  The
//  instruction sequences are otherwise the same; shipped HOMES y and z in
//  stack slots and copies them to the destination THROUGH x87 where ours
//  copy-propagates the integer registers straight into the stores:
//
//      100305f5  fld   DWORD PTR [eax+0x58]     ; x, propagated
//      100305f8  mov   ecx,[eax+0x5c]           ; y, integer load
//      100305fb  mov   eax,[eax+0x60]           ; z, integer load
//      100305fe  mov   [ebp-0x8],eax            ; z homed
//      10030601  mov   eax,[ebp+0xc]
//      10030604  fstp  DWORD PTR [eax]
//      10030606  mov   [ebp-0xc],ecx            ; y homed
//      10030609  fld   DWORD PTR [ebp-0xc]
//      1003060c  fstp  DWORD PTR [eax+0x4]
//      1003060f  fld   DWORD PTR [ebp-0x8]
//      10030612  fstp  DWORD PTR [eax+0x8]
//
//  Two `mov`s (6 bytes) against six instructions (18 bytes) is the whole
//  +12.  The slot evidence is the lever nobody has used yet: the frame is
//  `sub esp,0x10` — SIXTEEN bytes — and only [ebp-0xc] and [ebp-0x8] are
//  touched, with y at -0xc and z at -0x8.  A 12-byte object based at
//  [ebp-0x10] lays out exactly that way (x at -0x10, propagated away and
//  never stored; y at -0xc; z at -0x8; -0x4 unused), whereas three separate
//  `float` locals under VC6's usual first-declared-at--0x4 rule would put x
//  at -0x4 and z at -0xc, which is the opposite of what shipped stores.
//  So the shipped source most likely materialised an LTVector-shaped
//  temporary and copied it out member-by-member, not three scalar locals.
//  Candidate spellings, in the order to measure them:
//      (a) pAngles->Init(pProp->m_Vec[0], pProp->m_Vec[1], pProp->m_Vec[2]);
//      (b) LTVector v(pProp->m_Vec[0], pProp->m_Vec[1], pProp->m_Vec[2]);
//          pAngles->x = v.x; pAngles->y = v.y; pAngles->z = v.z;
//  NOT `*pAngles = *(LTVector*)pProp->m_Vec;` and not
//  `*pAngles = LTVector(...)` — a whole-object copy emits `rep movs`,
//  which is what the sibling si_GetPropVector (0x100304dc) does and what
//  this function conspicuously does not.  Not settled without a
//  build; both candidates are cheap one-line edits.
//
//  Exclude lines:
//    0x100305dd,"already written: si_GetPropRotationEuler at src/engine/server/smgr.cpp:3337 - ServerDE slot 0x118 installed by mov at 0x1002ff0c - carried there as a documented near of 53 of 65 bytes"
// ======================================================================= //


// ======================================================================= //
//  ROWS 0x100314dc (71) and 0x10031523 (74) — NOT WORK.  ALREADY WRITTEN.
//
//  They are `si_GetNextObject` (smgr.cpp:4251) and
//  `si_GetNextInactiveObject` (smgr.cpp:4267), declared at smgr.cpp:2585
//  and 2586 and installed at smgr.cpp:3735/3736.  smgr.cpp carries both as
//  documented NEARS, 6 bytes short each, with a long banner of failed
//  probes at smgr.cpp:4227.
//
//  IDENTIFICATION: 0x10030088 stores 0x100314dc into ServerDE +0x170 and
//  0x10030092 stores 0x10031523 into +0x174; servermsg.h:846/847 are
//  `HOBJECT (*GetNextObject)(HOBJECT)` and
//  `HOBJECT (*GetNextInactiveObject)(HOBJECT)` at exactly those slots.
//  Every offset in both bodies is already named in the tree:
//  LTObject::m_pSData +0x188, SObjData::m_StateLink +0x3c (LTLink is
//  m_pPrev+0x0 / m_pNext+0x4 / m_pData+0x8, src/LT2/sdk/inc/ltlink.h), the
//  sentinel g_pServerMgr+0x8e0 = &CServerMgr::m_List8dc.m_Head (LTList is
//  {m_nElements; m_Head}, so the 0x8dc list's head is at 0x8e0), and
//  LTObject::m_ChangeFlags +0x120 with the 0x38 mask servermgr.h:2003
//  already documents as sm_SetObjectState's state bits.
//
//  EXTENTS SOUND: 0x100314dc `mov eax,[esp+4]` .. `ret` at 0x10031522
//  (0x47 = 71); 0x10031523 `mov eax,[esp+4]` .. `ret` at 0x1003156c
//  (0x4a = 74).  No edge from outside either range; 0x1003156d is
//  si_SetNextUpdate.  They do not tile one function.
//
//  THE UNTRIED SPELLING FOR THE 6 BYTES.  Shipped materialises
//  g_pServerMgr SEPARATELY IN EACH ARM; ours hoists one load above the
//  branch, and one `mov ecx,ds:g` is 6 bytes, which is the entire delta in
//  both functions.  smgr.cpp's banner records two probes: naming the HEAD
//  in each arm (duplicates the `add $0x8e0` too, wrong for GetNextObject)
//  and naming the MANAGER in the ELSE ARM ONLY (right shape, wrong register
//  roles).  What has NOT been tried is naming the manager in a local in
//  BOTH arms and then writing the sentinel THROUGH THAT LOCAL:
//
//      CServerMgr* pMgr;
//      LTLink*     pLink;
//
//      if (hObj) { pMgr = g_pServerMgr;
//                  pLink = hObj->m_pSData->m_StateLink.m_pNext; }
//      else      { pMgr = g_pServerMgr;
//                  pLink = pMgr->m_List8dc.m_Head.m_pNext; }
//
//      if (pLink == &pMgr->m_List8dc.m_Head)
//          return LTNULL;
//
//  This one source shape predicts BOTH shipped bodies including the
//  asymmetry between them, which is why it is taken as the answer:
//
//    * GetNextObject needs [g+0x8e4] for the link and g+0x8e0 for the
//      sentinel — two displacements — so VC6 keeps the raw manager in ecx
//      in both arms and defers the `add ecx,0x8e0` to the join.  Shipped:
//      `mov ecx,ds:g` at 0x100314ea AND at 0x100314f5, `add ecx,0x8e0`
//      once at 0x10031501.
//    * GetNextInactiveObject needs [g+0x8e0] for the link and g+0x8e0 for
//      the sentinel — ONE displacement — so VC6 loads the manager into eax
//      with the 5-byte `a1` form, leas the sentinel into ecx and reloads
//      eax from the same displacement, which sinks the head computation
//      into both arms.  Shipped: `add ecx,0x8e0` at 0x10031537 in arm 1
//      and `lea ecx,[eax+0x8e0]` at 0x10031547 in arm 2, nothing at the
//      join.
//
//  Note the sentinel must be written through pMgr, not through a named
//  head pointer: shipped arm 2 of GetNextInactiveObject reads the link off
//  the MANAGER (`mov eax,[eax+0x8e0]` at 0x1003154d), where the probe that
//  named the head read it through the head pointer (`mov (%eax),%ecx`).
//  Honest confidence: this is a hypothesis with a mechanism, not a measured
//  result.  It is two small edits and one build.
//
//  Exclude lines:
//    0x100314dc,"already written: si_GetNextObject at src/engine/server/smgr.cpp:4251 - ServerDE slot 0x170 installed by mov at 0x10030088 - carried there as a documented near 6 bytes short"
//    0x10031523,"already written: si_GetNextInactiveObject at src/engine/server/smgr.cpp:4267 - ServerDE slot 0x174 installed by mov at 0x10030092 - carried there as a documented near 6 bytes short"
// ======================================================================= //
