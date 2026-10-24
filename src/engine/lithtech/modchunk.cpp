// lithtech.exe 0x0044c000-0x00450000 -- the MODEL FILE READER cluster.
// Reconstruction scratch TU.  Build with the lithtech flags
// (/O2 /D WIN32 /D NDEBUG /D _WINDOWS, /I LT2\sdk\inc /I src/engine\server).
//
// WHAT THIS CLUSTER IS
// --------------------
// Every row in this cluster is part of src/engine/server/commonmodel.cpp -- the
// shared model translation unit, compiled a SECOND time into lithtech.exe at
// /O2 where server.dll gets it at /O1.  That is established, not guessed:
// matching src/engine/lithtech/Release/commonmodel.obj against
// the retail lithtech.exe already names the two rows either side of every target
// (0x0044ccd0 mod_FindChunk, 0x0044d2a0 AnimInfo::sub_1001bec9, 0x0044d940
// Model::sub_1001c325, 0x0044e4e0 Model::LoadHeader, 0x0044e550 Model::Load),
// and the client's function order is the SAME as server.dll's, so the
// ordinals line up one for one with the server addresses the existing
// commonmodel.h/.cpp already carry.
//
// The chunk-name pool in .data (0x004ac0bc "AnimBindings", 0x004ac0cc
// "Sockets", 0x004ac0d4 "Animation", 0x004ac0e0 "SELF", 0x004ac0e8
// "ChildModels", 0x004ac0f4 "Nodes", 0x004ac0fc "Pieces", 0x004ac104
// "Header") is NOT a dispatch table -- it has no table at all.  Each literal
// has exactly ONE reference in .text and each of those is a mod_FindChunk
// call inside a different reader:
//
//   "AnimBindings" 0x0044d9b2  -> Model::sub_1001c36c   (0x0044d9a0, target)
//   "Sockets"      0x0044db3a  -> Model::LoadSockets    (0x0044db30, written)
//   "Pieces"/"Nodes"/"ChildModels"/"SELF"/"Animation"
//                             -> Model::LoadChildModels (0x0044ddc0, written)
//   "Header"       0x0044e4e7  -> Model::LoadHeader     (0x0044e4e0, MATCHES)
//
// EXTENTS
// -------
// All eleven rows verified: every one ends on a `ret`/`ret $n` followed by
// 0x90 padding to the next 16-byte boundary, every row start is 16-byte
// aligned, and a sweep of every e8/e9 in .text for a displacement landing
// strictly inside a row found no external entry into any of them.  No row in
// the cluster is a defect.  (0x0044dfbd, the `init` row between two of them, IS
// a defect: it is unaligned and 0x0044ddc0 + 509 lands exactly on it, so
// 0x0044ddc0 is a single 1812-byte function -- Model::LoadChildModels -- and
// not two.  It is not one of the cluster's rows.)
//
// SYMBOL SUFFIX: every symbol this file INTRODUCES carries `Q44C`.  The four
// functions that are members of classes commonmodel.h already declares are
// defined under their real names, because commonmodel.cpp does not define
// them and a second definition would not link otherwise.
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "commonmodel.h"
#include "servermgr.h"
#include "de_error.h"
#include "serverengine.h"


// --------------------------------------------------------------------- //
//  0x0044e5f0   124 bytes   CMoArray<T, NoCache>::SetSizeInit3
//                           (server.dll has no copy: the /O1 build inlines
//                            it into its one caller)
// --------------------------------------------------------------------- //
//
// collection.h DECLARES this and three siblings and defines none of them:
//
//     LTBOOL SetSizeInit (uint32 newSize, T& val);
//     LTBOOL SetSizeInit2(uint32 newSize, T  val);
//     LTBOOL SetSizeInit3(uint32 newSize, T& val, LAlloc* pAlloc);
//     LTBOOL SetSizeInit4(uint32 newSize, T  val, LAlloc* pAlloc);
//
// 0x0044e5f0 is SetSizeInit3, and the signature is read straight off the
// code rather than picked from that list:
//
//   * `ret $0xc` with `this` in ecx -- a thiscall with three dwords, so it is
//     one of the *3/*4 pair, not the two-argument pair;
//   * the third dword ([esp+0x18] at 0x0044e607) is handed to BaseDelete and
//     BaseNew as the allocator, which is the `LAlloc* pAlloc`;
//   * the second ([esp+0x14]) is DEREFERENCED, and dereferenced INSIDE the
//     fill loop -- `mov 0x14(%esp),%ecx; mov (%ecx),%edi` at 0x0044e64d /
//     0x0044e654, reloaded on every iteration.  A by-value `T val` would sit
//     in the slot itself; a reference is reloaded through it.  That is
//     SetSizeInit3 and not SetSizeInit4.
//
// The body is SetSize2 followed by a fill loop, and the shipped code says the
// SetSize2 is a CALL in the source that /O2 inlined rather than a copy of the
// body written out here:
//
//   * the `(newSize == m_nElements) && (newSize != 0)` early-out at
//     0x0044e5fc-0x0044e602 jumps to the FILL LOOP (0x0044e644), which is
//     exactly SetSize2 returning LTTRUE and the caller carrying on;
//   * the allocation-failure arm at 0x0044e63b writes `m_nElements = 0` and
//     returns with eax already zero -- SetSize2's own failure, tail-merged
//     with this function's `return LTFALSE`;
//   * BaseDelete (0x004152b0) and BaseNew (0x004131d0) are CALLED, not
//     expanded.  collection.h's own note measures the rule: a helper at
//     inline depth 1-2 is expanded and one at depth 4+ is called.  Writing
//     SetSize2's body out here puts _AllocateTArray at depth 1 and BaseNew at
//     depth 2 -- which is the standalone SetSize2 at 0x00414fa0, and that one
//     EXPANDS BaseNew.  Calling SetSize2 adds the level that turns both back
//     into calls, which is what the shipped bytes have.
//
// The element type is a four-byte pointer: the store is
// `mov %edi,(%edx,%eax,4)` and the two helpers are the folded four-byte
// COMDATs (0x004131d0 BaseNew is 22 bytes with no constructor loop,
// 0x004152b0 BaseDelete is the 19-byte no-destructor one).
//
// THE CALLER, and why this function exists at all: 0x0044e5f0's ONLY caller
// in the image is 0x0044ce31, inside CUnknownD974::sub_1001bb37
// (0x0044cd80).  commonmodel.cpp writes that spot as
//
//     if (!pChildArray->SetSize2(..., ...))
//         return LTFALSE;
//     for (j = 0; j < pChildArray->GetSize(); j++)
//         pChildArray->m_pArray[j] = 0;
//
// and the shipped build calls SetSizeInit3 with a stack local holding zero
// instead (`mov %ebx,0x1c(%esp)` at 0x0044ce29 is the local being set to 0,
// `lea 0x14(%esp),%eax` at 0x0044ce1b is its address being pushed).  So
// sub_1001bb37's two statements are ONE statement in the original, and that
// -- not a codegen accident -- is a large part of why 0x0044cd80 is 505 bytes
// of ours against 455 shipped.  See the note on that function at the foot of
// this file.
//
// THE DEFINITION BELONGS IN collection.h beside SetSize2.  It is here because
// that file is not mine to edit; the instantiation below is only what forces
// VC6 to emit the COMDAT so the bytes can be measured.

class CQ44CElem;                     // a four-byte pointer's worth of element

// THE PERIOD SOURCE FOR THIS FUNCTION SURVIVES, and it is in the tree:
// src/LT2/lithshared/stdlith/dynarray.h line 703 carries
//
//     DYNA_TEMPLATE
//     BOOL CMoArray<T, C>::SetSizeInit3( DWORD newSize, T &val, LAlloc *pAlloc )
//     {
//         DWORD i;
//
//         if(!SetSize2(newSize, pAlloc))
//             return FALSE;
//
//         for(i=0; i < GetSize(); i++)
//         {
//             m_pArray[i] = val;
//         }
//
//         return TRUE;
//     }
//
// -- three lines, exactly the shape the shipped bytes have, and its three
// siblings there are one-liners that forward into it (SetSizeInit and
// SetSizeInit2 with &g_DefAlloc, SetSizeInit4 by value).  That settles
// WHICH of the four 0x0044e5f0 is without relying on the reading above:
// only *3 and *4 take an allocator, *4 forwards by value, and the shipped
// loop dereferences its second argument on every trip.  Transcribed below
// in this file's types.
// MOVED: the definition now lives in src/engine/server/collection.h beside
// SetSize2, where both images' TUs can see it.  It had to: server.dll's
// CUnknownD974::sub_1001bb37 calls it, commonmodel.cpp is a SERVER TU, and
// this file is not in server.dll's source list -- so the server build was
// emitting a call to an undefined symbol where the shipped /O1 image expands
// the body inline.  Only the forcing instantiation below stays here.

// BACK AGAIN, for lithtech.exe only.  commonmodel.cpp now defines the body
// for the SERVER build alone: whenever the definition is visible in
// commonmodel.cpp, /O2 expands it into CUnknownD974::sub_1001bb37, and the
// shipped 0x0044ce31 CALLS 0x0044e5f0.  With no definition there the client
// caller is 455 of 455 bytes (identical) and this file supplies the
// out-of-line body.  Taking its address (not a forcing caller, which /O2
// would simply expand; an explicit member instantiation emitted nothing) is
// what makes VC6 emit the COMDAT.


// Forces the COMDAT out.  Not shipped code.
static LTBOOL (CMoArray<CQ44CElem*, NoCache>::*s_pSetSizeInit3Q44C)(uint32, CQ44CElem*&,
                                                                    LAlloc*) =
	&CMoArray<CQ44CElem*, NoCache>::SetSizeInit3;


// --------------------------------------------------------------------- //
//  0x0044d090   526 bytes   Model::CModelSub20C::sub_1001bd54
//                           (server.dll 0x1001bd54, 372 bytes)
// --------------------------------------------------------------------- //
//
//  MOVED to src/engine/server/commonmodel.cpp, where the whole analysis
//  that used to sit here now lives.  It had to move: this file is in
//  lithtech.exe's source list ONLY, the declaration is in the shared
//  commonmodel.h, and commonmodel.cpp (a server TU) calls it -- so
//  server.dll's 0x1001bd54 could never be compiled at all while the
//  definition lived here.  commonmodel.cpp is built into BOTH images, so
//  lithtech.exe keeps its copy.



// --------------------------------------------------------------------- //
//  0x0044d9a0   386 bytes   Model::sub_1001c36c  -- the "AnimBindings" chunk
// --------------------------------------------------------------------- //
//
//  MOVED to src/engine/server/commonmodel.cpp, together with the two views
//  it needs (CModelQ44C for 0x00447180 / 0x100176b7, and CAnimInfoQ44C for
//  AnimInfo+0x14).  Same reason as 0x1001bd54 above: this file is in
//  lithtech.exe's source list ONLY, the declaration is in the shared
//  commonmodel.h, and commonmodel.cpp (a server TU) CALLS it from
//  LoadChildModels -- so server.dll's 0x1001c36c (271 bytes) was never
//  compiled for that image at all while the body lived here.
//  commonmodel.cpp is built into BOTH, so lithtech.exe keeps 0x0044d9a0.


// --------------------------------------------------------------------- //
//  The piece's vertex-data record, and the piece itself.
// --------------------------------------------------------------------- //
//
//  MOVED to src/engine/server/commonmodel.cpp -- the five Q44C classes and
//  both reader bodies.  This file is in lithtech.exe's source list only, so
//  while they lived here server.dll's 0x1001bf85 (544 bytes, which its own
//  enumeration splits into 0x1001bf85/81 + 0x1001bfd6/463) and 0x1001c1a5
//  were never compiled for that image at all.  commonmodel.cpp is built into
//  BOTH, so lithtech.exe keeps 0x0044d3f0 and 0x0044d6e0 unchanged.



// ===================================================================== //
//  THE FIVE ROWS THAT ARE ALREADY WRITTEN, AND WHAT IS ACTUALLY WRONG
//  WITH THEM.  Nothing below is code -- it is the part of the cluster's
//  eleven rows that must NOT be re-derived, with the evidence.
// ===================================================================== //
//
// 0x0044cd80 / 455  ==  CUnknownD974::sub_1001bb37
//     commonmodel.cpp already has it (the recursion at 0x0044ce95 is its own
//     address; the sibling at 0x0044d090 above is the same shape).  Our
//     client object compiles it at 505 bytes.
//
//     THE 50 BYTES ARE ONE SOURCE STATEMENT, and 0x0044e5f0 is the proof.
//     The source there reads
//
//         if (!pChildArray->SetSize2(m_pSub20c04->m_Array18.GetSize(),
//                                    sub_1001671c()->GetAlloc()))
//             return LTFALSE;
//
//         for (j = 0; j < pChildArray->GetSize(); j++)
//             pChildArray->m_pArray[j] = 0;
//
//     and the shipped build has ONE call there instead -- to SetSizeInit3,
//     with a stack local holding the null (0x0044ce1b pushes its address,
//     0x0044ce29 sets it to zero) -- because the original wrote the sizing
//     and the zero-fill as a single call.  /O2 then leaves SetSizeInit3
//     out of line (it is too big to expand: 124 bytes) where it expands our
//     SetSize2 plus the loop inline, which is where the 50 bytes go.
//
//     THE EDIT, in commonmodel.cpp:
//
//         CUnknownD974* pNull;
//         ...
//         pNull = LTNULL;
//         pChildArray = &m_Array1c;
//
//         if (!pChildArray->SetSizeInit3(m_pSub20c04->m_Array18.GetSize(),
//                                        pNull, sub_1001671c()->GetAlloc()))
//             return LTFALSE;
//
//     with the loop deleted, and SetSizeInit3's body (above) moved into
//     collection.h beside SetSize2.  Note the ORDER the shipped code
//     computes things in: the allocator first (0x0044ce0c), then
//     m_pSub20c04->m_Array18.GetSize() (0x0044ce1f), then pChildArray
//     (0x0044ce24) -- so `pChildArray = &m_Array1c;` sits immediately above
//     the call and not, as now, above the sizing statement it replaced.
//
//     APPLIED, AND THE WARNING ABOVE WAS HALF RIGHT.  "This does NOT
//     regress server.dll: at /O1 SetSizeInit3 is expanded into its one
//     caller either way" is true of the MECHANISM and false of where the
//     definition was put: SetSizeInit3's body went into THIS file, which is
//     in lithtech.exe's source list only, so the server build saw a
//     declaration with no definition and emitted an out-of-line call.
//     server.dll's copy went from 301 of 306 bytes to 289.  The body now
//     lives in src/engine/server/collection.h beside SetSize2, where both
//     images' TUs see it, and the server side is 301 again (115 of 117
//     instructions).  Only the forcing instantiation stays here.
//
// 0x0044d870 / 208  ==  ModelWeightSet::sub_1001c2bc
//     Written, and commonmodel.cpp's note on it is right: server.dll's copy
//     is 105 of 105 bytes and 49 of 49 instructions with a pure esi/edi
//     rotation.  The source is proven; the client's 233-against-208 is the
//     /O2 compile, not the source.  Leave it.
//
// 0x0044db30 / 299  ==  Model::LoadSockets
//     Written ("Sockets" is at 0x0044db3a).  server.dll: 232 of 229 bytes,
//     94 against 93 instructions, one stack slot.  Same verdict.
//     SUPERSEDED: identical on both images now -- the sizing is one
//     SetSizeInit4 call (commonmodel.cpp has the note).
//
// 0x0044dc60 / 339  ==  Model::sub_1001c560
//     Written, and it MATCHES server.dll BYTE FOR BYTE (0x1001c560, 164
//     bytes, src/engine/server/Release/commonmodel.obj).
//     By the usual rule that is source proven right and a client
//     difference that must not be chased: editing it to close the client's
//     413-against-339 would regress the server.
//     SUPERSEDED: it was chased, and the server did not move -- identical
//     on both images (a real ~ModelWeightSet, SetSizeInit4, and a chained
//     operator[] store; commonmodel.cpp has the note).
//
// 0x0044ee10 / 1182 ==  CommonModelLT::GetNodeTransform
// 0x0044f3d0 / 1121 ==  CommonModelLT::GetSocketTransform
//     Both written; our client object compiles them at 1185 and 1124, i.e.
//     THREE BYTES over each.  These are the two flagged as sitting
//     in GetNodeTransform/GetSocketTransform territory, and they are exactly
//     that: 0x0044ee10 sits between SetPieceHideStatus (0x0044ed60, matched)
//     and GetInstanceHookFn (0x0044f840, matched), which is where
//     commonmodel.h's declaration order puts them, and the server pair is
//     0x1001d478 / 0x1001d77f.  They are the known AnimTimeRef::Init block.
//     Not re-derived.
//
//
// WHAT THIS FILE LEAVES OPEN
// --------------------------
//   * CPieceLodQ44C's six trailing dwords, CPieceDimQ44C's three
//     (float, float, uint16) triples and CModelVertQ44C's five dwords are
//     named for their offsets.  Nothing in this cluster reads any of them --
//     the reader writes them and the consumer is in the renderer, outside
//     0x0044c000-0x00450000 -- so there is no evidence here for better
//     names and guessing them would be guessing.
//   * Model::sub_1001c36c's third parameter is pushed by its only caller
//     (as 1) and read nowhere.  `ret $0xc` is all that fixes its existence;
//     its type is a guess constrained only to four bytes.
//   * Model::m_Unknown054's element type is 0x14 bytes, not the `void*` the
//     header currently declares.  Only the indexing in sub_1001bf85 says so,
//     and only that it is 0x14 -- changing the header's declaration would
//     change the SetSize2 COMDAT that LoadChildModels (0x1001c604, not
//     reconstructed) reaches, so the cast above is the conservative form
//     until that function is written.
