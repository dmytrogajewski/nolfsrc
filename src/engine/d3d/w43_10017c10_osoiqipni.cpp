// *********************************************************************** //
//
//  w43_10017c10 -- ONE /Qip translation unit for the model renderer at
//  0x1008b090.  Merged from big17c14.cpp (0x10015b70,
//  0x10015c70, 0x10015e30, 0x10017c10/14, 0x1001bf30/34),
//  w38_10019644_osoiqip.cpp (0x10019644, 0x10019874, 0x10019a70,
//  0x1001b844, 0x1001bae4, 0x1001be00/08, 0x1001c2c0) and big19d64.cpp
//  (0x10019d64), whose banners follow, in that order of first use.
//
//  WHY ONE FILE (measured, taken): icl /Qip gives a member function
//  the shipped `mov 0x4(%esp),%eax` stack lead-in plus a register entry
//  exactly when the member is called from inside the same /Qip TU.  The
//  shipped 0x10019644, 0x10019874, 0x1001b844 and 0x10019d64 have that
//  lead-in and are called from 0x10017c14; 0x1001bae4 has it and is called
//  from 0x10019d64.  CD3DModelDraw (big17c14's view) and CD3DModelLight
//  (big19d64's view) are two views of the one object; 0x10017c14 reaches
//  0x10019d64 through a cast of `this`.
//
//  FLAGS /Os /Oi- /Qip /Qip_no_inlining (the _osoiqipni convention in
//  build/engine.sh, made for this file).  /Os: the TU has no loop-head
//  alignment; /Oi-: the CRT sqrt is CALLED (0x10019a70, Norm);
//  /Qip: the register entries above; /Qip_no_inlining: 0x10015e30 stays a
//  call from 0x10017c14.  Measured on this merged source: 15
//  identical at this set, 14 at /Os /Qip /Qip_no_inlining, 10 at /Os
//  /Oi- /Qip (before 0x1001bae4 closed: 14 / 13 / 9).
//
//  The three old files' banners follow, in order (big17c14.cpp,
//  w38_10019644_osoiqip.cpp, big19d64.cpp); where they say a thing is
//  near or not done, the per-function notes below are newer.
//
// *********************************************************************** //


// *********************************************************************** //
//
//  MODULE:   BIG17CEE (.CPP)
//
//  PURPOSE:  the model-drawing driver of d3d.ren.  Reconstructed
//            instruction by instruction from the retail d3d.ren under the
//            "structurally right, not byte-exact" bar: same control flow,
//            same callees in the same order, same field offsets, same
//            constants.  Nothing below is guessed; where a thing could not
//            be read off the bytes it is named by its address.
//
//  ===================================================================== //
//  FIRST: THE ROW'S ADDRESSES ARE WRONG, AND BY MORE THAN A NUDGE.
//  ===================================================================== //
//
//  The row names "0x10017cee, 6,486 bytes, end 0x10019644".  0x10017cee
//  is NOT a function entry -- it is this function's EPILOGUE:
//
//      10017cee: 5e            pop  %esi
//      10017cef: 5f            pop  %edi
//      10017cf0: 8b e5         mov  %ebp,%esp
//      10017cf2: 5d            pop  %ebp
//      10017cf3: 8b e3         mov  %ebx,%esp
//      10017cf5: 5b            pop  %ebx
//      10017cf6: c2 04 00      ret  $0x4
//
//  and SEVEN `jmp 0x10017cee` sites inside the body reach it (0x10019400,
//  0x10019420, 0x10019425, 0x1001944a, 0x100195a2, 0x1001963b, plus the
//  fall-through at 0x10017cec).  The real entry is 0x10017c14, proved by
//  scanning every E8 in .text: exactly one call reaches it,
//
//      10015ce4: b9 90 b0 08 10   mov  $0x1008b090,%ecx
//      10015ce9: 56               push %esi
//      10015cea: e8 25 1f 00 00   call 0x10017c14
//
//  so it is __thiscall on the GLOBAL object at 0x1008b090, one dword
//  argument, `ret $0x4`.  0x10017c10..0x10017c13 is a four-byte lead-in
//  (`mov 0x4(%esp),%eax`) that nothing ever executes -- 0x10019644, the
//  next function, has the identical dead lead-in at 0x10019640, so it is
//  an icl artefact of the stack-realigning prologue, not code.
//
//  DECODED RANGE: 0x10017c14 .. 0x10019643 -- 6,704 bytes, 40 direct call
//  sites and 4 indirect ones, 30 DISTINCT direct callees.  (Counting from
//  0x10017cee as the row does loses the five calls at 0x10017c3f,
//  0x10017c5c, 0x10017c82, 0x10017ca2 and 0x10017cae.)
//
//  ===================================================================== //
//  WHAT IT IS
//  ===================================================================== //
//
//  It is `CD3D_ModelDraw::Draw(ModelInstance*)` -- everything that happens
//  to one model between "the world decided to draw it" and the piece
//  renderer.  Five things settle that:
//
//    * the two strings it reaches: "Model::SetupTransforms failed for %s."
//      at 0x10019413 (the failure arm of the 0x1007ca60 call) and
//      "SetupModelLight: %d (%d lights)" at 0x10018882, whose second
//      argument is this->m_nLights2fc -- the light count big19d64.cpp
//      already named on THIS SAME `this`;
//    * 0x10019d64 is called here, at 0x1001885d, and big19d64.cpp records
//      "ONE caller: 0x1001885d", so `this` is that file's CD3DModelLight
//      and every offset it fixed (+0x2fc, +0x300, +0x310, +0x350, +0x390,
//      +0x410) is reused below unchanged;
//    * 0x10016324 -- big16324.cpp's y-locked billboard -- is called TWICE,
//      at 0x10017e2f with `0xff - fade` and at 0x10019445 with a literal
//      0xff, which is exactly the pair of call sites that file names;
//    * the distance ladder at 0x10017d79 reads four floats out of the
//      model (+0xb0, +0xb4, +0xb8, +0xbc) and cross-fades the billboard
//      against the mesh between them -- model LOD;
//    * the tail hands the whole thing to 0x10043e10 with the near plane
//      restored and one of two counters bumped.
//
//  ===================================================================== //
//  WHICH FLAG:  /O2.   Four readings, one of them against.
//
//  SUPERSEDED: the TU is built /Os /Qip /Qip_no_inlining.
//  The shipped range has NO loop-head alignment anywhere (our /O2 build put
//  eight lea/nop runs in this function), and measured against /O2 /Oi-
//  /Qip /Qip_no_inlining this function goes 8663 -> 6620 bytes (shipped
//  6704), 7429 -> 5512 differing, with 0x10015b70 and 0x10015c7e
//  byte-identical under it.  The readings below are kept for the record.
//  ===================================================================== //
//
//    * outgoing arguments are `add $-N,%esp` then `mov` at nearly every
//      site -- 0x10017c8d, 0x10017d31, 0x10017ec0, 0x10017fb9, 0x10018169,
//      0x1001819d, 0x100181cf, 0x100181f5, 0x100182fb, 0x1001842b,
//      0x1001886b, 0x100191b2, 0x1001942e, 0x10019454, 0x10019489.  That
//      is the /O2 pattern; /Os would push.
//    * COLD BLOCKS ARE SUNK PAST THE EPILOGUE, ten of them, and they are
//      the arms of ordinary `if`s and `?:`s: 0x100194d7, 0x100194df,
//      0x100194fd, 0x1001951b, 0x10019539, 0x10019557, 0x10019563,
//      0x10019579, 0x10019595, 0x10019625.  That is /O2 placement and it
//      is what big16324.cpp -- the immediate neighbour, verdict /O2 --
//      does with its six epsilon arms.
//    * `m_n6ac = 0` is DUPLICATED at 0x10017f11 and 0x10017f23 for the two
//      failing arms of one `if`.  /O2 tail duplication; /Os cross-jumps.
//    * AGAINST: there is ONE epilogue and every early exit reaches it with
//      `e9` (0x10019400, 0x10019420, 0x10019425, 0x1001944a, 0x100195a2,
//      0x1001963b).  The table calls that /Os.  But big16324.cpp is /O2 on
//      four independent readings and has the same single shared epilogue
//      at 0x10017b1e, so in this image that signal is not decisive.
//    * MIXED, stated rather than explained: `&p->m` comes out as
//      `mov $0x410,%esi ; add %edx,%esi` at 0x10017d04 and 0x10017e72
//      (the /O2 spelling) and as `lea 0x480(%edx),%edi` at 0x10017d11 and
//      `lea 0x1c0(%eax),%ecx` at 0x10017c4d (the /Os spelling), inside the
//      same basic blocks.  big19d64.cpp hit the identical contradiction on
//      the neighbouring function and also declined to invent a reason.
//
//  ===================================================================== //
//  THE FRAME -- `sub $0x6d8`, 1,752 bytes, and it decomposes exactly
//  ===================================================================== //
//
//  The prologue is icl's STACK-REALIGNING one and is not spellable in C:
//
//      push %ebx ; mov %esp,%ebx ; sub $0x8,%esp ; and $-16,%esp ;
//      add $0x8,%esp ; push %ebp ; push %ebp ; mov 0x4(%ebx),%ebp ;
//      mov %ebp,0x4(%esp) ; mov %esp,%ebp ; sub $0x6d8,%esp
//
//  so `this` is at -0x12c(%ebp), the argument at 0x8(%ebx), and the frame
//  is:
//
//      -0x68c   0x1a8+  the render-info object, ctor 0x10015950 -- its
//                       +0xe4, +0xec and +0xf4 are written here and
//                       0x1007ca60 reads its +0x1a4, so it is at least
//                       0x1a8 bytes and at most 0x248 (the gap to -0x444)
//      -0x444   12 B    the LTVector return temp of operator-
//      -0x438   64 B    matProj      (built, then `this` of 0x1001e140)
//      -0x3f8   64 B    operator* return temp, non-0x40 arm
//      -0x3b8   64 B    operator* return temp, 0x40 arm
//      -0x378   64 B    mView        -- the one matrix the rest reads
//      -0x338   64 B    A DEAD MATRIX: written once at 0x100181ea and
//                       never read.  See COIN-FLIP (1).
//      -0x2f8   64 B    operator* return temp -> m_Mat350
//      -0x2b8   64 B    operator* return temp -> m_Mat350 (other arm)
//      -0x278   64 B    MakeInverse return temp -> m_Mat390
//      -0x238   64 B    the per-bone MatMul destination
//      -0x1f8   64 B    the first stage of the two-stage MatMul
//      -0x1b8   64 B    the second stage -> m_Mat3d0
//      -0x178   12 B    the scaled vector temp
//      -0x16c   12 B    the point it is copied into
//      -0x160   16 B    the plane: normal x,y,z then distance
//      -0x150   12 B    the projected point
//      -0x140    4 B    the fildl scratch for (float)(uint8)
//      -0x12c    4 B    `this`
//      -0x128    4 B    the bone loop counter
//      -0x124    4 B    the bone loop's strength-reduced byte offset
//      -0x118    4 B    the argument;  REUSED as an x87 double spill from
//                       0x100185e7 onward, after its last read at
//                       0x10018239
//      -0x110    4 B    &m_vPos410 early;  REUSED to save g_f1008b908
//                       across the draw from 0x1001838d
//      -0x108 .. -0x1c  x87 double spill slots inside the two big MatMuls
//
//  ===================================================================== //
//  EVIDENCE, REGION BY REGION -- THE HONEST PART FIRST
//  ===================================================================== //
//
//  THE CALL-FREE REGIONS, measured as the gaps between consecutive call
//  sites, are 4,687 bytes -- 70% of the function -- and the call-sequence test is blind
//  to every one of them.  The four that matter:
//
//    A. 0x10018437 -> 0x10018852   1,051 B  the per-bone MatMul loop and
//                                           its setup.
//    B. 0x1001888d -> 0x100191ca   2,365 B  the two-stage MatMul into
//                                           m_Mat3d0 and its x87 spills.
//    C. 0x100191ca -> 0x100193d8     526 B  the clip-plane construction.
//    D. 0x10017ed6 -> 0x10017ff2     284 B  m_n478 / m_n6b0 and the frame
//                                           serial.
//
//  A, B and C together are 3,942 of those bytes and they are ALL x87.  So
//  a good call-sequence figure on this function witnesses the skeleton and very
//  little of the arithmetic; that is stated here so nobody quotes the
//  percentage on its own.
//
//  What pins BOTH is that they are not new code: every one of the 48
//  `fstps` in them lands on a 16-float block at a 0x10-apart offset, in
//  the order [0][0] [1][0] [2][0] [3][0] [0][1] ... -- COLUMN BY COLUMN --
//  and every sum is  A[i][0]*B[0][j] + A[i][1]*B[1][j] + A[i][2]*B[2][j]
//  + A[i][3]*B[3][j] with the three `faddp`s running last-term-backwards.
//  That is ltmatrix.h:172 MatMul verbatim, and it is the SAME source shape
//  big19d64.cpp already decoded out of 0x10019dc4 and 0x1001a3c2 on the
//  neighbouring function.  Its `d3d_MatMul` is reused unchanged below.
//  Three matmuls: mView*bone[i] (region A), g_Mat1008b95c*bone[nf4] and
//  that result * m_pModel00->m_Matfc (region B).  The only freedom left is
//  which x87 value went to which spill slot, and that is scheduling.
//
//  The regions the call-sequence test DOES cover:
//
//    0x10017c14..0x10017cc7  the frame, the ctor of the render-info local
//      and the three-pointer wiring.  m_p08 = &pObject->m_Sub1c0 is stored
//      BEFORE 0x100158a0 is called on that same pointer (0x10017c59 then
//      0x10017c5c), and 0x100158a0 is `mov 0x1c(%ecx),%eax`, so
//      m_pModel00 = *(pObject + 0x1c0 + 0x1c) = pObject->m_p1dc -- the
//      same +0x1dc that big16324.cpp's 0x100158c0 reads and that the
//      caller reads at 0x10015cf8 to print the model's name.  The
//      `& 0x40` fork picks between transforming the object's position
//      through g_Mat1008b91c (call 0x10015770, which is a 4x4 Apply) and
//      copying it (`rep movsl` %ecx=3).
//
//    0x10017cc8..0x10017cf8  two guards, the second sunk to 0x10019625.
//
//    0x10017cf9..0x10017e33  THE LOD CROSS-FADE.  The indirect call at
//      0x10017d29 is a REGISTER-ABI call through g_pfn1008ce8c with %ecx =
//      &m_vPos410 and %edx = &m_n480 -- it is decoded as an
//      ordinary call, so it is a __fastcall pointer below.
//      Then fDistSqr = (m_vPos410 - g_v1008bd34).MagSqr() and the ladder:
//        0x10017d85  fb4 vs fbc   ja  -> the reversed-range arm at 0x10019595
//        0x10017d92  fbc vs d2    jb  -> 0x1001942a: billboard at 0xff, return
//        0x10017d9f  fb4 vs d2    jb  -> the fade at 0x10017dbd
//        else                          m_b6a8 = m_pObject04->m_b97
//      The forward fade is (sqrt(d2) - m_fb0) / (m_fb8 - m_fb0) and the
//      reversed one is (sqrt(d2) - m_fb8) / (m_fb0 - m_fb8) -- note `DE F9`
//      at 0x10017de2 and 0x100195ef is Intel FDIVP (st(1) /= st(0)), the
//      same objdump/Intel swap as for `DE E9`.
//
//    0x10017e34..0x10017e9e  m_n458, then m_f47c = g_v1008bd34.Dist(
//      m_vPos410) (0x100154b0, `push`ed argument), then 0x10019644.
//
//    0x10017e9f..0x10017f2c  the cached-render probe.  0x10015e30 is
//      d3dlow.cpp's two-argument bit-packer and takes BOTH arguments in
//      REGISTERS (%eax, %edx) -- another tier-Y call, decoded as an
//      ordinary one.  The `m_n6ac = 0` store is duplicated for the two
//      failing arms; the succeeding arm is sunk whole to 0x1001944f.
//
//    0x10017f2d..0x10017ff6  the frame-serial resync (16-bit compare at
//      0x10017f3b, 16-bit store at 0x1001958a), m_n478, m_n6b0, and
//      0x100151b0 -- d3dbatchd.cpp's (pos, quaternion, scale, matrix)
//      builder, whose four arguments here are pObject+0x18c, +0x9c, +0xac
//      and &m_Mat310.  m_pObject04 is loaded THREE times (0x10017fbc,
//      0x10017fbf, 0x10017fd2), so the source mentions it three times.
//
//    0x10017ff7..0x10018232  the projection.  Two CLAMPS per axis, each
//      four .rdata slots because the literal appears twice in the source:
//      0.1745329f (10 degrees) at 0x100a4ba8/bac/bb0/bb4/c18/c1c/c24/c28
//      and 2.9670596f (170 degrees) at 0x100a4c04/c08/c0c/c10/c2c/c30/
//      c34/c38.  0x1007fdf4 takes ONE double and its two answers land in
//      m[0][0] and m[1][1] of an otherwise diagonal matrix with m[2][2] =
//      m[3][3] = 1.0f -- a half-angle tangent pair, i.e. tan().  The
//      matrix is handed to 0x1001e140 BY VALUE (`ret $0x40`, sixteen
//      dwords), exactly as d3dr5c.cpp decoded it.
//
//    0x10018233..0x10018379  the no-projection path: mView from one of two
//      constant matrices, then the four-way m_n6ac/m_n6b0 fork that ends
//      in m_Mat350, then MakeInverse into m_Mat390 and the 0x10019874
//      capacity check (d3dr5d.cpp's "ensure the array is at least ...").
//
//    0x1001837a..0x10018450  the near-plane override, the two virtual
//      calls on m_pObject04 (vtable +0x3c and +0x38 -- slots 15 and 14),
//      and 0x1007ca60 with its failure string.
//
//    0x1001883b..0x10018894  the bone-transform fallback (0x1001b844) when
//      no clip flags are set, the light setup (0x10019d64) and the timing
//      printf.
//
//    0x10018895..0x1001913f  m_Mat3d0: either the two-stage MatMul or a
//      straight copy of g_Mat1008b95c.
//
//    0x10019140..0x100193d1  the two function-pointer slots (+0x41c and
//      +0x420, each picked from a pair of code addresses by g_n1008cb24 --
//      0x10015a60/0x10069c90 and 0x10015aa0/0x100159d0), the __cdecl
//      callback through m_pObject04->m_pfn1b4, and, if it set bit 0 of the
//      answer, a clip plane built into this+0x20 from the answer's vector
//      and scalar.
//
//    0x100193d2..0x10019404  the tail: 0x10043e10, restore g_f1008b908,
//      bump 0x1008cea8 or 0x1008cea4.
//
//  COIN-FLIPS are listed at the bottom, each with both exact spellings.
//
// *********************************************************************** //

// *********************************************************************** //
//
//  w38_10019644_osoiqip.cpp (was w3_10019644_osoi.cpp) -- written for the
//  d3d.ren link.
//
//  Members of the model renderer at 0x1008b090 -- big17c14.cpp's
//  CD3DModelDraw and big19d64.cpp's CD3DModelLight are two views of the
//  one object -- that the link was missing:
//
//    0x10019644  CD3DModelDraw::sub_10019644    LOD selection and blend
//    0x10019874  CD3DModelDraw::sub_10019874    size the two work arrays
//    0x10019a70  sub_10019a70                   the per-light world callback
//    0x1001b844  CD3DModelDraw::sub_1001b844    the bone matrices
//    0x1001bae4  CD3DModelLight::sub_1001bae4   sun visibility (0..1)
//    0x1001be00  sub_1001be00                   the sky-ray test it uses
//    0x1001c2c0  CMoArray<LTMat>::InternalNiceSetSize
//
//  Flag set /Os /Oi- (the `_osoi` suffix): 0x10019a70 CALLS the CRT sqrt
//  (0x1007fc98) and 0x1001c2c0 calls memcpy (0x100805d0); the shipped pads
//  are /Os's (lea-nop to four, then 0x90).
//
//  /Os /Oi- /Qip (the `_osoiqip` suffix).  0x1001bae4 calls 0x1001be00
//  at its /Qip REGISTER entry 0x1001be08, so the two need one /Qip TU; with
//  that, the 0x1001be00 stack entry and 0x1001be08 are byte-identical once
//  (a) the poly lookup is lithtech.exe's inline FindPoly shape (early
//  returns, lvideomgr.cpp's v_GetPolyFromHPoly), with the world-model
//  bound test a `switch` whose zero arm returns -- icl then sinks the
//  in-range load, as shipped -- and (b) GetPoly is a virtual of a BASE
//  class the table derives from: icl then loads the vtable before
//  pushing hPoly.  Nothing else in the file moved (0x1001c2c0 identical,
//  the members within a few bytes of before).
//
//  THE MEMBERS' FOUR-BYTE LEAD-INS, measured: icl /Qip gives a member
//  function the `mov 0x4(%esp),%eax` stack entry plus an `X$` register
//  entry exactly when the member is CALLED (not inlined) from inside the
//  same /Qip TU -- a test file with A::f1() called from A::f2() under
//  /Os /Qip /Qip_no_inlining reproduces the shipped lead-in byte for byte.
//  The shipped callers of 0x10019644/0x10019874/0x1001b844 are in
//  0x10017c14 (big17c14.cpp) and of 0x1001bae4 in 0x10019d64
//  (big19d64.cpp), so the shipped TU is big17c14 + these + big19d64 under
//  one /Qip flag set.  Not done here: the bodies are far off on their own
//  (block layout, not the entry), so the merge is recorded, not taken.
//
//  NEAR, ALL OF THE MEMBERS: 0x10019644, 0x10019874, 0x1001b844 and
//  0x1001bae4 are the REGISTER entries of /Qip external pairs (each has a
//  `mov 0x4(%esp),%eax` stack entry four bytes before it) and 0x1001bae4
//  calls 0x1001be00 through its register entry 0x1001be08; that needs the
//  shipped TU's /Qip membership, which no file here reproduces for a
//  member function (measured: /Qip on a member emits no stack entry --
//  true only while nothing in the TU calls it; see the note above).
//
// *********************************************************************** //


// *********************************************************************** //
//
//  MODULE:   BIG_19D64 (.CPP)  --  d3d.ren 0x10019d64 .. 0x1001b831
//
//  ONE function, 6,880 bytes as listed.  The real body ends at the
//  `ret` at 0x1001b617 plus the out-of-line arms behind it; the last one
//  ends at 0x1001b830 and 0x1001b831..0x1001b83f are 0x90 padding, so the
//  function is 0x1001b831 - 0x10019d64 = 6,861 bytes of code.
//
//  ONE caller: 0x1001885d.  __thiscall, no arguments (plain `ret`, %ecx
//  read at 0x10019d71 and never reloaded), returns void -- the three paths
//  that reach the epilogue leave %eax holding three different things.
//
//  WHAT IT IS.  Per-model lighting setup.  It builds the inverse of the
//  model's world transform into a local LTMatrix, converts the object's
//  base colour, samples the global directional light and the world's
//  light grid, then walks the dynamic-light list filling a fixed 16-entry
//  table of lights transformed into model space.
//
//  ================= HOW EACH REGION WAS PINNED =================
//
//  Everything below was read off the bytes.  The x87 regions were decoded
//  with a symbolic stack simulator over the objdump listing, honouring the
//  rule that `DE E9` is Intel's FSUBP (objdump prints "fsubrp") and
//  `DE E1` is Intel's FSUBRP (objdump prints "fsubp"); the same swap applies
//  to `DC F8+i`, which objdump prints as "fdivr %st,%st(1)" and which is
//  Intel FDIV ST(1),ST(0) -- that one instruction is the whole difference
//  between `1/(r*r)` and `(r*r)/1` at 0x1001b7a7.
//
//  * 0x10019d64..0x10019da3  frame, ctor, the light-count clamp.
//        `sub $0x400,%esp` + `push %edi` twice (icl's dead slot).
//        %ecx = &local(-0x3f4) then `call 0x10015830`, which d3dr5b.cpp
//        already decoded as a constructor (three LTVector Inits, four
//        zeroed dwords, `mov %ebx,%eax` on the way out).  The object is
//        0x34 bytes: -0x3f4 .. -0x3c0, and every field this function
//        writes lands inside that range.
//        The clamp is `mov g,%eax; mov %eax,0x300(%ebx); cmp $0x10,%eax;
//        jb L; mov $0x10,%eax; L: mov %eax,0x300(%ebx)` -- TWO stores with
//        the second one after the join, which is an assignment followed by
//        LTMIN(x,16), not `if (x > 16) x = 16;` (that would put the single
//        store inside the arm).  16 is also exactly the table size: the
//        records start at +0x3c, are 0x2c bytes (`imul $0x2c,%edx,%ebx`),
//        and 0x3c + 16*0x2c = 0x2fc, which is where m_nLights lives.
//
//  * the three-way transform branch.  `jne 0x1001a32b` on m_p00->m_n13c
//    sends the non-zero case to the LAST block, the `& 0x40` test falls
//    through to the middle block and `je 0x1001a310` picks the first --
//    i.e. `if (n13c == 0) { if (flags & 0x40) A else B } else C`, with A
//    and B both `jmp 0x1001a80d` over C's tail.
//
//    - C (0x1001a32b): three calls to 0x10015800, which d3dbatchd.cpp
//      pins as LTMatrix::operator* (hidden return slot at (%esp), `other`
//      at 0x4(%esp), `this` in %ecx).  The first `this` is &m_Mat390 and
//      its argument is 0x1001c790's answer -- d3dbatchc.cpp pins that as
//      CMoArray::Get with `shl $0x6`, i.e. a 64-byte element, i.e. an
//      LTMatrix -- indexed by m_p00->m_nf4 on the array at m_p00+0x70.
//      The results chain: -0x3c0 initialised, -0x380 initialised, then
//      -0x340 *assigned* into -0x300 by `rep movsl` %ecx=0x10.  An
//      initialisation is NRVO'd into its variable; an assignment gets the
//      return-slot temp plus a copy.  That is why the first two have no
//      copy and the third does.
//    - A (0x10019dc4): NO calls.  The simulator says the sixteen results
//      are T[i][j] = sum_k G[i][k]*M[k][j] with G = the 4x4 at 0x1008b91c
//      and M = this+0x310, stored in the order [0][0] [1][0] [2][0] [3][0]
//      [0][1] ... -- column by column, which is ltmatrix.h:172 MatMul's
//      source order verbatim.  Then `rep movsl` T -> -0x300, then a second
//      call-free block whose 3x3 is a transpose done with INTEGER `mov`s
//      (icl copies float lvalues with integer movs) and whose fourth
//      column is -(row . trans) with trans = (L[0][3],L[1][3],L[2][3]),
//      bottom row 0,0,0 and `movl $0x3f800000` -- ltmatrix.h:304
//      Mat_InverseTransformation verbatim, again into a temp (-0x200)
//      followed by `rep movsl` into -0x300.
//    - B (0x1001a310): `push &local(-0x300); push &this->m_Mat310;
//      call 0x10014ab0`.  0x10014ab0 reads its FIRST argument (%eax =
//      0x38(%esp) after the frame) and writes its SECOND (%ebx =
//      0xc(%esp)), so it is Mat_InverseTransformation(pSrc, pDest) emitted
//      out of line -- src = &m_Mat310, dest = the local.
//
//    THE INLINING IS ASYMMETRIC IN THE SHIPPED CODE and it is reproduced
//    here by SPELLING, not by hoping: MatMul is written out (inline) at the
//    two sites that carry no call, Mat_InverseTransformation is written
//    out at the one site that carries no call and CALLED at the one that
//    does, and operator*/MakeInverseTransform are declared without bodies
//    so they must become calls.  See the COIN-FLIP note at the bottom.
//
//  * 0x1001a80d  m_p04 is loaded THREE separate times for the three
//    colour bytes at +0x94/+0x95/+0x96 -- three separate mentions in the
//    source.  `movzbl` + `fildl` through a dword slot is (float)(uint8).
//    Then `m_v650 = g_p1008beb0->m_v1c` (`rep movsl` %ecx=3) and the
//    __thiscall to 0x10019a24, which d3dlow.cpp has already decoded.
//
//  * 0x1001a87f  three multiplies by 0x3b808081.  That is 1.0f/255.0f and
//    it lives in THREE separate .rdata slots, 0x100a4c94/98/9c -- icl
//    gives each source literal its own slot, so the source really does
//    write the constant three times.  The slot addresses run 94, 98, 9c in
//    the order 0x65c, 0x660, 0x664, which is how the statement order below
//    is fixed even though the code emits 0x664 first.
//
//  * 0x1001a8f8  `test $0x2,%ah` on m_p04->m_n88 is bit 0x200; the taken
//    arm at 0x1001b7fb zeroes 0x680,0x67c,0x678,0x674 (descending, i.e. a
//    chained assignment) and stores 0x437f0000 = 255.0f into 0x684, 0x688,
//    0x68c ASCENDING (three separate statements -- and there are exactly
//    three 255.0f slots in .rdata, 0x100a4cb0/b4/b8), then returns.
//
//  * 0x1001a90a  the global-light block.  Guard is
//    g_p1008beac && g_n1008af54 && the direction is not all-zero; the
//    all-zero test is spread over 0x1001a924 and the out-of-line pair at
//    0x1001b62d, and the arm order (x==0 falls INTO the y/z tests, y!=0
//    and z!=0 jump back into the body) is `x != 0 || y != 0 || z != 0`.
//    The cache test is `m_f244 < 0.0f` (fcomps against 0x100a4cbc = 0.0f,
//    `jb`) OR ((m_n8c & 0x20) AND any axis moved).  Each axis is
//    |a - b| built the way a compiler builds ABS of a difference: compare
//    a with b, then subtract in whichever order gives a positive result
//    (`DE E1` on one edge, `DE E9` on the other), then compare with the
//    threshold, which is `0.001f + g_f1008af78` computed ONCE -- 0.001f
//    has exactly one .rdata slot (0x100a4cfc), so the source mentions it
//    once.
//
//  * 0x1001aa16  MatVMul_3x3(&m_v674, &mInv, &g_p1008cbcc->m_v130) -- the
//    three sums are m[i][0]*x + m[i][1]*y + m[i][2]*z with no translation
//    term -- then the vector is negated through a temp at -0x1c0 and
//    copied back with `rep movsl` %ecx=3.
//
//  * 0x1001ab1a  the light grid.  Decoded in full; see the banner on the
//    block itself.  The three `call 0x1007fd30` are floor(double) (the
//    answer is subtracted from the value it was called on, which is the
//    fractional part), and the `fnstcw / or $0xc00 / fistpll / fldcw`
//    sandwiches are plain (int) casts.
//
//  * 0x1001b3dd  the dynamic-light loop.  g_n1008bd84 is the count and
//    g_p1008bd88 the pointer array (`mov 0x1008bd88(,%eax,4),%ebx`).
//    m_nLights < m_nMaxLights is compared THREE times -- 0x1001b47a,
//    0x1001b4e9, 0x1001b50f -- and the rule is that icl does not
//    fold redundant tests, so the source tests it three times: guarding
//    the body, breaking the loop, and guarding the callback.
//
//  * 0x1001b664  the accepted-light block, sunk below the epilogue by
//    icl's cold-block placement.  MatVMul (WITH the translation term,
//    +m[i][3]) of the light position through mInv; the direction is a copy
//    of it, then normalised -- `mag = sqrt(z*z + y*y + x*x)` (Mag's source
//    order reassociated last-term-first), `if (mag == 0.0f)` skip, else
//    multiply by 1.0f/mag -- which is _CVector::Norm() at ltvector.h:194
//    including its `nVal` default of ONE.  Then r*r and 1.0f/(r*r).
//
//  * 0x1001b517  the callback dispatch.  -0xf4 gets `this` and -0xf0 gets
//    the 16-dword copy of mInv; the callback 0x10019a70 reads `(%ecx)` as
//    an object pointer and 0x4(%ecx)..0x10(%ecx) as a matrix row, which is
//    what fixes the layout of that argument block.  The query object's
//    +0x04 gets 1, +0x08 and +0x14 get the model's bounding box built from
//    m_p00->m_fa8, +0x2c gets 0x10019a70 and +0x30 gets &the argument
//    block.  The call itself is `mov 0x6c(%edx),%eax; lea 0x6c(%edx),%edx;
//    push; mov 0x8(%eax),%eax; mov %edx,%ecx; call *%eax` -- a __thiscall
//    virtual, slot 2, on a SUBOBJECT at +0x6c of the world pointer.
//
//  ==================== /O2 vs /Os ====================
//
//  VERDICT: /Os, but the evidence is mixed and it is stated honestly.
//    * ONE epilogue.  Every other exit cross-jumps to it (`jmp 0x1001b611`
//      at 0x1001b82c, `je/jae 0x1001b611` at 0x1001b511/0x1001b51e).  /O2
//      duplicates.  This is the strongest signal and it says /Os.
//    * `&p->m` is always `lea` -- `lea 0x310(%ebx),%ecx`,
//      `lea 0x390(%ebx),%ecx`, `lea 0x650(%ebx),%edi`.  /Os.
//    * the last call pushes its argument (`push %ecx` at 0x1001b609).  /Os.
//    * AGAINST: the outgoing arguments of 0x1001c790, the three
//      operator* calls and the three floor calls are STORED into an
//      `add $-8,%esp` area (`mov %eax,0x4(%esp)`, `fstpl (%esp)`), which
//      is the /O2 idiom.  That is not reconciled with the three above, and
//      no reason is invented for it.
//
//  ================ COIN-FLIPS AND GAPS, STATED ================
//
//  1. THE INLINING, and it is the big one.  The shipped function inlines
//     MatMul at 0x10019dc4 and 0x1001a3c2, inlines Mat_InverseTransformation
//     at 0x1001a1be, and CALLS Mat_InverseTransformation at 0x1001a310 --
//     the same header inline, one site each way, a few hundred bytes apart.
//     icl is not made to reproduce that split on its own; each site is
//     SPELLED the way it came out, writing the two MatMuls and the
//     one inverse through file-local `inline` copies and leaving the fourth
//     as a call to the out-of-line 0x10014ab0.  Likewise operator* and
//     MakeInverseTransform are declared with no body so they cannot be
//     inlined.  To test icl's own inliner instead, the alternative source
//     for the whole branch is:
//         mInv = g_Mat1008b91c * m_Mat310;      // A, first half
//         mInv = mInv.MakeInverseTransform();   // A, second half
//         Mat_InverseTransformation(&m_Mat310, &mInv);   // B
//     which is one build and settles it.
//
//  2. |a - b| (three sites, 0x1001a969..0x1001a9e0).  Written as
//     `(a < b ? b - a : a - b)`; see the note on D3D_ABSDIFF below for why
//     `ABS(a - b)` and `fabs(a - b)` are both ruled out by the absence of
//     any zero constant and of `FTST`/`FABS`.  The remaining freedom is
//     whether the source wrote the conditional itself or a helper macro
//     with the same expansion; the bytes cannot tell those apart.
//
//  3. The 24 grid corners and the 12 blend temporaries are written as 36
//     named floats because that is what the frame holds (-0x190..-0x134 and
//     -0x130..-0x104, all stored, none folded).  The shipped code stores
//     each blend temporary TWICE, which is why each is two statements
//     rather than one `a*g + b*f`.  Whether the source grouped them as
//     twelve LTVectors is not decidable: no LTVector operator leaves a
//     trace here, every store is a scalar `fstps`.
//
//  4. THE ONE THING NOT NAMED (not a decode gap -- the code is
//     decoded, the PURPOSE is not): the three dwords at -0x100/-0xfc/-0xf8,
//     zeroed at 0x1001b3f0..0x1001b402 by a chained assignment and never
//     read again.  They are emitted below as three int32s so the stores
//     survive; the type and the name are unknowable from the bytes and
//     nothing is claimed about them.
//
//  5. Order-of-emission notes that are scheduling, not source, and were
//     NOT chased: 0x1001a87f emits the 0x664 multiply before 0x65c (the
//     .rdata slot order says the source runs 65c, 660, 664); the four
//     zero-stores in the two clearing blocks run downwards (chained
//     assignment); and the three `(uint8)` conversions at the end of the
//     grid block are all computed before any is stored.
//
// *********************************************************************** //


#include "ltbasedefs.h"
#include <math.h>
#include "../../LT2/lithshared/stdlith/l_allocator.h"

extern "C" void*  memcpy(void* pDst, const void* pSrc, unsigned int nBytes);


// ======================================================================= //
//  ltmatrix.h:172 MatMul, written out because the three sites that use it
//  in this function carry NO CALL.  Store order is the header's: column by
//  column, [0][0] [1][0] [2][0] [3][0] [0][1] ...   This is byte-for-byte
//  the same helper big19d64.cpp derived from the neighbouring function.
// ======================================================================= //
struct LTMat
{
	// 0x10015800 -- LTMatrix::operator* (d3dbatchd.cpp): hidden return
	// slot at (%esp), `other` at 0x4(%esp), `this` in %ecx, `ret $8`.
	LTMat  sub_10015800(LTMat& other);

	// 0x10015740 -- LTMatrix::MakeInverse() (d3dr4b.cpp), by value.
	LTMat  sub_10015740();

	// 0x100157e0 -- LTMatrix::MakeInverseTransform() (0x10019d64).
	LTMat  sub_100157e0();

	// 0x100156e0 -- the identity Init (d3dr2b.cpp / d3dlow.cpp).
	void   sub_100156e0();

	// 0x1001e140 -- a 4x4 taken BY VALUE, `ret $0x40` (d3dr5c.cpp).
	void   sub_1001e140(LTMat mat);

	// 0x10015770 -- LTMatrix::Apply, __thiscall, `ret $8`.  Its body at
	// 0x10015770 .. 0x100157d5 is
	//   pOut->x = m[0][0]*pIn->x + m[0][1]*pIn->y + m[0][2]*pIn->z + m[0][3]
	// and the two more rows, so the first stack argument is the source and
	// the second the destination.
	void   sub_10015770(const struct LTVec* pIn, struct LTVec* pOut);

	float  m[4][4];
};

#define M1 pMat1->m
#define M2 pMat2->m

inline void d3d_MatMul(LTMat *pDest, LTMat *pMat1, LTMat *pMat2)
{
	pDest->m[0][0] = M1[0][0]*M2[0][0] + M1[0][1]*M2[1][0] + M1[0][2]*M2[2][0] + M1[0][3]*M2[3][0];
	pDest->m[1][0] = M1[1][0]*M2[0][0] + M1[1][1]*M2[1][0] + M1[1][2]*M2[2][0] + M1[1][3]*M2[3][0];
	pDest->m[2][0] = M1[2][0]*M2[0][0] + M1[2][1]*M2[1][0] + M1[2][2]*M2[2][0] + M1[2][3]*M2[3][0];
	pDest->m[3][0] = M1[3][0]*M2[0][0] + M1[3][1]*M2[1][0] + M1[3][2]*M2[2][0] + M1[3][3]*M2[3][0];

	pDest->m[0][1] = M1[0][0]*M2[0][1] + M1[0][1]*M2[1][1] + M1[0][2]*M2[2][1] + M1[0][3]*M2[3][1];
	pDest->m[1][1] = M1[1][0]*M2[0][1] + M1[1][1]*M2[1][1] + M1[1][2]*M2[2][1] + M1[1][3]*M2[3][1];
	pDest->m[2][1] = M1[2][0]*M2[0][1] + M1[2][1]*M2[1][1] + M1[2][2]*M2[2][1] + M1[2][3]*M2[3][1];
	pDest->m[3][1] = M1[3][0]*M2[0][1] + M1[3][1]*M2[1][1] + M1[3][2]*M2[2][1] + M1[3][3]*M2[3][1];

	pDest->m[0][2] = M1[0][0]*M2[0][2] + M1[0][1]*M2[1][2] + M1[0][2]*M2[2][2] + M1[0][3]*M2[3][2];
	pDest->m[1][2] = M1[1][0]*M2[0][2] + M1[1][1]*M2[1][2] + M1[1][2]*M2[2][2] + M1[1][3]*M2[3][2];
	pDest->m[2][2] = M1[2][0]*M2[0][2] + M1[2][1]*M2[1][2] + M1[2][2]*M2[2][2] + M1[2][3]*M2[3][2];
	pDest->m[3][2] = M1[3][0]*M2[0][2] + M1[3][1]*M2[1][2] + M1[3][2]*M2[2][2] + M1[3][3]*M2[3][2];

	pDest->m[0][3] = M1[0][0]*M2[0][3] + M1[0][1]*M2[1][3] + M1[0][2]*M2[2][3] + M1[0][3]*M2[3][3];
	pDest->m[1][3] = M1[1][0]*M2[0][3] + M1[1][1]*M2[1][3] + M1[1][2]*M2[2][3] + M1[1][3]*M2[3][3];
	pDest->m[2][3] = M1[2][0]*M2[0][3] + M1[2][1]*M2[1][3] + M1[2][2]*M2[2][3] + M1[2][3]*M2[3][3];
	pDest->m[3][3] = M1[3][0]*M2[0][3] + M1[3][1]*M2[1][3] + M1[3][2]*M2[2][3] + M1[3][3]*M2[3][3];
}

#undef M1
#undef M2


// The four field-of-view clamps at 0x10018019/0x10018044/0x10018074/
// 0x100180a5.  Each is `flds LO ; fcomp val ; jbe <arm>` with the LO arm
// falling through to a SECOND slot holding the same literal, and the arm
// itself `flds HI ; fcomp val ; jae <join>` with a second HI slot behind
// it.  Four .rdata slots per clamp, sixteen in all, which is what fixes
// the macro as mentioning each bound TWICE and as nesting the HI test
// INSIDE the LO test's false arm (the LO-taken path jumps straight to the
// join and never sees HI).  icl emits the source's LEFT operand as the
// `fcomp` destination, and the loaded-first operand here is the BOUND, so
// the bound is on the left of both tests.
#define D3D_CLAMPF(v, lo, hi)   (((lo) > (v)) ? (lo) : (((hi) < (v)) ? (hi) : (v)))


// ======================================================================= //
//  THE VECTOR -- src/LT2/sdk/inc/ltvector.h's _CVector<float>, spelled locally
//  so the out-of-line members carry their addresses in their names.  Every
//  one is DECLARED AND NOT DEFINED: the shipped function CALLS them, so
//  this translation unit must not be able to inline any.  The set and the
//  addresses are big48180.cpp's and d3dr2b.cpp's, unchanged.
// ======================================================================= //
struct LTVec
{
	LTVec  sub_10015640(const LTVec v) const;   // 0x10015640  operator -
	float  sub_10015510() const;                // 0x10015510  MagSqr
	float  sub_100154b0(const LTVec& v) const;  // 0x100154b0  Dist

	float  x;                                   // +0x00
	float  y;                                   // +0x04
	float  z;                                   // +0x08
};


// ======================================================================= //
//  THE ALLOCATOR AND THE ARRAYS
// ======================================================================= //

// 0x10091df0 is an allocator OBJECT: Alloc is vtable +0x04, Free +0x08.
struct IAllocatorBase
{
	virtual void* Slot00(uint32 nSize);                 // vtable +0x00
	virtual void* Alloc(uint32 nSize);                  // vtable +0x04
	virtual void  Free(void* pMem);                     // vtable +0x08
};
struct IAllocator : public IAllocatorBase
{
};

extern IAllocator g_10091df0;

// dynarray.h's DefaultCache.  In 0x1001c2c0 its accessors are CALLED
// (0x1000a690 / 0x1000a680 / 0x1000a670), in 0x10019874 they are read
// inline -- two instantiations built differently -- so the fields are
// public and the out-of-line accessors are declared for 0x1001c2c0 only.
struct DefaultCache
{
	uint32 m_CacheSize;                                 // +0x00
	uint32 m_WantedCache;                               // +0x04

	uint32 GetCacheSize() const;                        // 0x1000a690
	void   SetCacheSize(uint32 val);                    // 0x1000a680
	uint32 GetWantedCache() const;                      // 0x1000a670
};

struct Elem32_19874
{
	uint32  m_n[8];
};

// CMoArray<T, DefaultCache> with the SetSize2 path written the way
// dynarray.h writes it, inlined (0x10019874).
template<class T>
struct MoArr19874
{
	void*        m_pVTable;                             // +0x00
	T*           m_pArray;                              // +0x04
	uint32       m_nElements;                           // +0x08
	DefaultCache m_Cache;                               // +0x0c

	uint32 GetSize() const { return m_nElements; }
	T&     operator[](uint32 i) const { return m_pArray[i]; }
	inline uint32 GetNumAllocatedElements() { return m_nElements + m_Cache.m_CacheSize; }

	LTBOOL SetSize(uint32 newSize);
	LTBOOL SetSize2(uint32 newSize, LAlloc* pAlloc);
	void _DeleteAndDestroyArray(LAlloc* pAlloc, uint32 nElements);
	T*   _AllocateTArray(uint32 nElements, LAlloc* pAlloc);
};

template<class T>
LTBOOL MoArr19874<T>::SetSize(uint32 newSize)
{
	return SetSize2(newSize, &g_DefAlloc);
}

template<class T>
LTBOOL MoArr19874<T>::SetSize2(uint32 newSize, LAlloc* pAlloc)
{
	if ((newSize == m_nElements) && (newSize != 0))
	{
		return LTTRUE;
	}

	if (m_pArray)
	{
		_DeleteAndDestroyArray(pAlloc, GetNumAllocatedElements());
	}

	m_nElements = newSize;
	if (newSize > 0)
	{
		m_pArray = _AllocateTArray(newSize + m_Cache.m_WantedCache, pAlloc);
		if (!m_pArray)
		{
			m_nElements = 0;
			m_Cache.m_CacheSize = 0;
			return LTFALSE;
		}

		m_Cache.m_CacheSize = m_Cache.m_WantedCache;
	}

	return LTTRUE;
}

template<class T>
T* MoArr19874<T>::_AllocateTArray(uint32 nElements, LAlloc* pAlloc)
{
	T* tPtr = LNew_Array(pAlloc, T, nElements);

	return tPtr;
}

template<class T>
void MoArr19874<T>::_DeleteAndDestroyArray(LAlloc* pAlloc, uint32 nElements)
{
	if (m_pArray)
	{
		LDelete_Array(pAlloc, m_pArray, nElements);
		m_pArray = NULL;
	}

	m_Cache.m_CacheSize = 0;
}


// ======================================================================= //
//  THE MODEL INSTANCE -- the one argument, and this->m_pObject04.
//
//  Every offset below is a load or a store in these bytes.  Nothing is
//  named from outside the image.
// ======================================================================= //
struct D3DObject;
struct D3DObjectVtbl;                                   // below D3DObject

// 0x100158a0 -- `push %esi ; mov 0x1c(%ecx),%eax ; add $0x4,%esp ; ret`.
// The subobject at pObject+0x1c0 whose +0x1c is the model record.
struct D3DObjectSub1c0
{
	void*   sub_100158a0() const;                       // 0x100158a0
	uint8   m_pad_00[0x1c];                             // +0x00 .. +0x1b
	void*   m_p1c;                                      // +0x1c
};

// 0x100151b0's second argument (d3dbatchd.cpp: a sanitised quaternion).
struct D3DQuat
{
	float   x, y, z, w;
};

// The __cdecl callback in the object's +0x1b4 slot, called at 0x100191ca
// with (pObject, &this->m_nFlags0c, 0).
typedef void (*PFNObject1b4)(D3DObject* pObject, void* pOut, uint32 nFlags);

struct D3DObject
{
	// 0x100158b0 -- `lea 0x18c(%ecx),%eax`: &m_vPos18c, out of line.
	LTVec*          sub_100158b0();

	D3DObjectVtbl*  m_pVtbl;                            // +0x000
	uint8           m_pad_04[0x88 - 0x04];              // +0x004
	uint32          m_nFlags88;                         // +0x088
	uint8           m_pad_8c[0x97 - 0x8c];              // +0x08c
	uint8           m_bAlpha97;                         // +0x097
	uint8           m_pad_98[0x9c - 0x98];              // +0x098
	D3DQuat         m_Rot9c;                            // +0x09c
	LTVec           m_vScaleac;                         // +0x0ac
	uint8           m_pad_b8[0x180 - 0xb8];             // +0x0b8
	uint16          m_nFlags180;                        // +0x180
	uint8           m_pad_182[0x18c - 0x182];           // +0x182
	LTVec           m_vPos18c;                          // +0x18c
	uint8           m_pad_198[0x1b4 - 0x198];           // +0x198
	PFNObject1b4    m_pfn1b4;                           // +0x1b4
	uint8           m_pad_1b8[0x1c0 - 0x1b8];           // +0x1b8
	D3DObjectSub1c0 m_Sub1c0;                           // +0x1c0 (+0x1c: the model)
	uint16          m_wAnim1e0;                         // +0x1e0 (0x10015c70)
	uint8           m_pad_1e2[0x1e8 - 0x1e2];           // +0x1e2
	uint16          m_wAnim1e8;                         // +0x1e8 (0x10015c70)
	uint8           m_pad_1ea[0x1fc - 0x1ea];           // +0x1ea
	uint32          m_n1fc;                             // +0x1fc
};

// The vtable.  Only two slots are reached: +0x38 (index 14) at 0x10018401
// and +0x3c (index 15) at 0x100183d4, both __thiscall with one pointer
// argument and no caller-side unwind (the shipped `mov 0x4(%eax),%ecx`,
// `call *%eax` with ONE argument stored at (%esp); lithtech.exe's bodies,
// e.g. 0x0045b390, read `this` from %ecx and `ret $0x4`).  The slots are
// pointers to members of the complete (single-inheritance, non-virtual)
// D3DObject, so each is one code pointer and a call through one is a
// __thiscall.  (They were `__stdcall` with `this` pushed as an argument:
// the callee then took a stale %ecx as `this` -- a page fault in
// lithtech.exe at 0x0045b3a0 the first time a model drew.)
struct D3DObjectVtbl
{
	void*  m_pad_00[14];                                // +0x00 .. +0x34
	void   (D3DObject::* Slot14)(void*);                // +0x38
	void   (D3DObject::* Slot15)(void*);                // +0x3c
};


// ======================================================================= //
//  THE MODEL RECORD -- this->m_pModel00, i.e. *(pObject + 0x1dc).
//
//  +0x74/+0x78 are the bone-matrix array and its count (`add $0x40` per
//  step at 0x1001882a, `imul $0x40` at 0x100188cb).  +0xfc is a whole 4x4
//  -- big19d64.cpp's m_Matfc -- read at +0xfc .. +0x138 by the second
//  MatMul.  +0x13c and +0xf4 are that file's m_n13c and m_nIndexf4.  The
//  four fade distances at +0xb0/+0xb4/+0xb8/+0xbc are new here.
// ======================================================================= //
// The model's node-transform array (commonmodel.h's
// CMoArray<LTMatrix, NoCache> m_NodeTransforms at +0x70): 0x1001b844
// reaches it through these accessors (see there); the union keeps the
// older raw names other functions here use.
struct XfArr70
{
	void*  m_pVtbl;
	LTMat* m_pArray;
	uint32 m_nElements;
	uint32 GetSize() const { return m_nElements; }
	LTMat& operator[](uint32 i) const { return m_pArray[i]; }
};

struct D3DModel
{
	// 0x100736f0 -- `push %esi ; mov 0x4(%ecx),%eax ; add $0x4,%esp ; ret`
	// (d3dr4b.cpp): the model's name, printed at 0x10019413.
	const char*     sub_100736f0() const;

	// 0x10019644's LOD table (w38_10019644_osoiqip.cpp's D3DModel19644).
	uint32 GetNumLODs() const { return m_nLODDists94 + 1; }

	// 0x10019874's two counts.
	uint32 NumVerts() const { return m_n64; }
	uint32 NumNodes() const { return m_nBones78; }

	float* GetLODDist(uint32 i)
	{
		if (i == 0)
			return &m_fLODDista0;

		if (i - 1 < (uint32)m_nLODDists94)
			return &m_pLODDists90[i - 1];

		return LTNULL;
	}

	uint8           m_pad_00[0x64];                     // +0x000
	uint32          m_n64;                              // +0x064
	uint8           m_pad_68[0x70 - 0x68];              // +0x068
	union {
	struct {
	void*           m_pVtbl70;                          // +0x070
	LTMat*          m_pBones74;                         // +0x074
	uint32          m_nBones78;                         // +0x078
	};
	XfArr70         m_Xf70;
	};
	uint8           m_pad_7c[0x90 - 0x7c];              // +0x07c
	float*          m_pLODDists90;                      // +0x090  LODs 1..n (0x10019644)
	int32           m_nLODDists94;                      // +0x094
	uint8           m_pad_98[0xa0 - 0x98];              // +0x098
	float           m_fLODDista0;                       // +0x0a0  LOD 0
	float           m_fRadiusa4;                        // +0x0a4 (0x10015b70)
	float           m_fRadiusa8;                        // +0x0a8 (0x10019a70)
	uint8           m_pad_ac[0xb0 - 0xac];              // +0x0ac
	float           m_fFade0b0;                         // +0x0b0
	float           m_fFade1b4;                         // +0x0b4
	float           m_fFade2b8;                         // +0x0b8
	float           m_fFade3bc;                         // +0x0bc
	uint8           m_pad_c0[0xf4 - 0xc0];              // +0x0c0
	uint32          m_nIndexf4;                         // +0x0f4
	uint8           m_pad_f8[0xfc - 0xf8];              // +0x0f8
	LTMat           m_Matfc;                            // +0x0fc
	uint32          m_n13c;                             // +0x13c
	uint32          m_n140;                             // +0x140
	float           m_f144;                             // +0x144
	float           m_f148;                             // +0x148
	uint8           m_pad_14c[0x150 - 0x14c];           // +0x14c
	uint32          m_n150;                             // +0x150
	uint8           m_pad_154[0x16c - 0x154];           // +0x154
	uint32          m_nAnims16c;                        // +0x16c (0x10015c70)
};


// ======================================================================= //
//  THE RENDER-INFO LOCAL at -0x68c.
//
//  Constructed by 0x10015950, which loops EIGHT times over 0x1c-byte
//  records (`mov $0x8,%edi ; ... add $0x1c,%ebx ; dec %edi ; jne`) writing
//  -1 to each record's +0x18 and calling 0x1001e0e0 on it, then zeroes
//  +0xe0 .. +0xf4.  This function writes +0xe4, +0xec and +0xf4 and hands
//  the object to 0x1007ca60, which reads its +0x1a4 -- so it is at least
//  0x1a8 bytes.  Only the three fields it touches are named.
// ======================================================================= //
struct D3DRenderInfo
{
	D3DRenderInfo();                                    // 0x10015950
	int32   sub_1007ca60();                             // 0x1007ca60

	uint8   m_pad_00[0xe4];                             // +0x000
	LTMat*  m_pMate4;                                   // +0x0e4
	uint32  m_n0e8;                                     // +0x0e8
	uint32  m_n0ec;                                     // +0x0ec
	uint32  m_n0f0;                                     // +0x0f0
	D3DObject* m_pObjectf4;                             // +0x0f4
	uint8   m_pad_f8[0x1b0 - 0xf8];                     // +0x0f8
};


// ======================================================================= //
//  THE MODEL CACHE at 0x10091d80 -- d3dneutral.cpp already pinned
//  0x10015470 as `return &g_10091d80;`, six bytes, so the global and the
//  accessor are the same object reached two ways.  Both spellings appear
//  in the shipped code and both are reproduced below; see COIN-FLIP (3).
// ======================================================================= //
class D3DModelCache
{
public:
	int32   sub_1007e3c0(D3DObject* pObject, uint32 nColor);
	int32   sub_1007e430(D3DObject* pObject, uint32 nColor,
	                     uint32 nTris, uint32 nFlags);
	void    sub_1007e7c0();
};

extern D3DModelCache  g_Cache10091d80;                  // 0x10091d80
extern D3DModelCache* sub_10015470();                   // 0x10015470


// ======================================================================= //
//  A PLANE -- four consecutive floats.  The `rep movsl` at 0x100193c9 with
//  %ecx = 4 copies -0x160 .. -0x154 into this+0x20 in one move, which is
//  what makes it one object rather than a vector and a loose float.
// ======================================================================= //
// Named Plane17c14, not LTPlane: the SDK's ltplane.h is reachable through
// ltbasedefs.h and declares that name with different member names, so the
// local view needs its own. Same layout, which is the point.
struct Plane17c14
{
	LTVec   m_vNormal;                                  // +0x00
	float   m_fDist;                                    // +0x0c
};


// ======================================================================= //
//  GLOBALS.  Every one of these is an address this function touches; the
//  shapes are read off the accesses and nothing else.
// ======================================================================= //

// 0x1008b91c, 0x1008b95c, 0x1008b9dc, 0x1008ba1c, 0x1008badc, 0x1008bb1c,
// 0x1008bb5c -- seven 0x40-byte matrices, 0x40 apart.  Each is either
// `rep movsl` %ecx=0x10'd whole (0x10018243, 0x10018257, 0x1001912e) or
// handed to a matrix routine, except g_Mat1008b9dc, whose LAST ROW is
// also read element-wise at 0x1001921f (+0x30, +0x34, +0x38, +0x3c).
extern LTMat    g_Mat1008b91c;                          // 0x1008b91c
extern LTMat    g_Mat1008b95c;                          // 0x1008b95c
extern LTMat    g_Mat1008b9dc;                          // 0x1008b9dc
extern LTMat    g_Mat1008ba1c;                          // 0x1008ba1c
extern LTMat    g_Mat1008badc;                          // 0x1008badc
extern LTMat    g_Mat1008bb1c;                          // 0x1008bb1c
extern LTMat    g_Mat1008bb5c;                          // 0x1008bb5c

// 0x1008bd34 -- the eye point.  big16324.cpp reaches the same global as
// the point its billboard yaws to face.
extern LTVec    g_v1008bd34;                            // 0x1008bd34

// The clipper's state; big16324.cpp and d3dr3c.cpp name both.
extern uint32   g_n1008b874;                            // 0x1008b874  flags
extern float    g_f1008b908;                            // 0x1008b908  near

// The global light record at *0x1008cbcc: +0x104 is read by 0x10017c14,
// the sun direction +0x130 by 0x10019d64 and 0x1001bae4, the sun colour
// +0x13c by 0x10019d64.
struct Src19d64GlobalLight
{
	uint8       m_pad_00[0x104];                        // +0x000
	uint32      m_n104;                                 // +0x104
	uint8       m_pad_108[0x130 - 0x108];               // +0x108
	LTVector    m_vDir130;                              // +0x130
	LTVector    m_vColor13c;                            // +0x13c
};
extern Src19d64GlobalLight*  g_p1008cbcc;               // 0x1008cbcc

extern uint32   g_n10089408;                            // 0x10089408
extern uint16   g_w1008aed0;                            // 0x1008aed0
extern uint32   g_n1008aed4;                            // 0x1008aed4
extern uint32   g_n1008aef4;                            // 0x1008aef4
extern uint32   g_n1008af14;                            // 0x1008af14
extern uint32   g_n1008af94;                            // 0x1008af94
// 0x1008afb8: the value field (+4) of the console variable at 0x1008afb4
// (Bank10021f40) -- one object, not a float of its own.                            // 0x1008afb8
struct Bank10021f40;
extern struct Bank10021f40 g_1008afb4;
#define g_f1008afb8 (*(float*)((char*)&g_1008afb4 + 4))
// 0x1008afd8: the value field (+4) of the console variable at 0x1008afd4
// (Bank10021f40) -- one object, not a float of its own.                            // 0x1008afd8
struct Bank10021f40;
extern struct Bank10021f40 g_1008afd4;
#define g_f1008afd8 (*(float*)((char*)&g_1008afd4 + 4))
extern float    g_f1008b8cc;                            // 0x1008b8cc
extern float    g_f1008b8d0;                            // 0x1008b8d0
extern float    g_fModernFovX4x3;                       // M-P0-01, w4_1001eb90_os.cpp
extern float    g_fModernFovXScale;                     // M-P0-01, w4_1001eb90_os.cpp
extern uint16   g_w1008bea8;                            // 0x1008bea8
extern uint32   g_n1008ca38;                            // 0x1008ca38
extern uint32   g_n1008cb24;                            // 0x1008cb24
extern uint32   g_n1008cb64;                            // 0x1008cb64
extern uint32   g_n1008cb68;                            // 0x1008cb68
extern uint32   g_n1008cbc8;                            // 0x1008cbc8
extern uint32   g_n1008cea4;                            // 0x1008cea4
extern uint32   g_n1008cea8;                            // 0x1008cea8

extern int32    g_n1008b034;                            // 0x1008b034  LOD bias
extern uint32   g_n1008b014;                            // 0x1008b014  LOD blending on
// 0x1008aff8: the value field (+4) of the console variable at 0x1008aff4
// (Bank10021f40) -- one object, not a float of its own.
extern struct Bank10021f40 g_1008aff4;
#define g_f1008aff8 (*(float*)((char*)&g_1008aff4 + 4))

// 0x1008ce8c -- the indirect call at 0x10017d29 takes BOTH arguments in
// registers, %ecx = &m_vPos410 and %edx = &m_n480, and nothing is pushed.
// A register-ABI call is still a call, so it is decoded as one.
// __fastcall is the only spelling with that register pair.
typedef void (__fastcall *PFN1008ce8c)(LTVec* pPos, uint32* pOut);
extern PFN1008ce8c  g_pfn1008ce8c;                      // 0x1008ce8c

// The four code addresses stored into +0x41c and +0x420 at 0x1001916b,
// 0x10019172, 0x1001918c and 0x10019193.  Materialised as immediates, so
// they are function addresses, not data.
extern "C" void sub_10015a60();                         // 0x10015a60
extern "C" void sub_10015aa0();                         // 0x10015aa0
extern "C" void sub_100159d0();                         // 0x100159d0
extern "C" void sub_10069c90();                         // 0x10069c90

// 0x10015190 -- d3dbatchd.cpp's round-to-nearest float-to-int, __cdecl,
// one float, answer in %eax and used through `movzbl %al`.
extern "C" int32 sub_10015190(float fVal);

// 0x100151b0 -- d3dbatchd.cpp: (position, quaternion, scale, matrix).
extern "C" void sub_100151b0(LTVec* pPos, D3DQuat* pRot, LTVec* pScale,
                             LTMat* pMat);

// 0x10021d10 -- the renderer's warning printer (d3ddraw.cpp), __cdecl,
// variadic.
extern "C" void sub_10021d10(const char* pFmt, ...);


// ======================================================================= //
//  `this` -- the GLOBAL object at 0x1008b090.  This is big19d64.cpp's
//  CD3DModelLight seen from the other side: m_pModel00, m_pObject04,
//  m_nLights2fc, m_Mat310, m_Mat350, m_Mat390 and m_vPos410 are that
//  file's fields at that file's offsets, and 0x10019d64 is a member here.
//  Everything from +0x00c to +0x030 and from +0x41c up is new and every
//  one of those offsets is a load or a store in these bytes.
// ======================================================================= //
class CD3DModelDraw
{
public:
	void        sub_10017c10(D3DObject* pObject);       // 0x10017c10 stack entry, 0x10017c14 register body  THIS
	void        sub_10016324(D3DObject* pObject, uint8 nAlpha); // 0x10016324
	void        sub_10019640();                         // 0x10019644
	void        sub_1001b840(LTMat* pMat);              // 0x1001b844
	uint32      sub_1001bf30();                         // 0x1001bf30/34 (/Qip pair)
	int32       sub_10019870();                         // 0x10019874
	void        sub_10043e10();                         // 0x10043e10

	D3DModel*   m_pModel00;                             // +0x000
	D3DObject*  m_pObject04;                            // +0x004
	D3DObjectSub1c0* m_pSub08;                          // +0x008
	uint32      m_nFlags0c;                             // +0x00c
	LTVec       m_v010;                                 // +0x010
	float       m_f01c;                                 // +0x01c
	Plane17c14     m_Plane020;                             // +0x020
	uint32      m_n030;                                 // +0x030
	uint8       m_pad_034[0x2fc - 0x034];               // +0x034
	uint32      m_nLights2fc;                           // +0x2fc
	uint8       m_pad_300[0x310 - 0x300];               // +0x300
	LTMat       m_Mat310;                               // +0x310
	LTMat       m_Mat350;                               // +0x350
	LTMat       m_Mat390;                               // +0x390
	LTMat       m_Mat3d0;                               // +0x3d0
	LTVec       m_vPos410;                              // +0x410
	void*       m_pfn41c;                               // +0x41c
	void*       m_pfn420;                               // +0x420
	uint8       m_pad_424[0x44c - 0x424];               // +0x424
	uint32      m_n44c;                                 // +0x44c
	uint32      m_n450;                                 // +0x450
	float       m_fBlend454;                            // +0x454 (0x10019644)
	uint32      m_n458;                                 // +0x458
	uint8       m_pad_45c[0x474 - 0x45c];               // +0x45c
	float       m_f474;                                 // +0x474 (0x10015b70)
	uint32      m_n478;                                 // +0x478
	float       m_f47c;                                 // +0x47c
	uint32      m_n480;                                 // +0x480
	uint8       m_pad_484[0x4e4 - 0x484];               // +0x484
	uint32      m_n4e4;                                 // +0x4e4
	uint8       m_pad_4e8[0x5c8 - 0x4e8];               // +0x4e8
	uint32      m_n5c8;                                 // +0x5c8
	uint32      m_n5cc;                                 // +0x5cc
	uint8       m_pad_5d0[0x628 - 0x5d0];               // +0x5d0
	MoArr19874<Elem32_19874> m_Arr628;                  // +0x628 (0x10019874)
	MoArr19874<LTMat>        m_Arr63c;                  // +0x63c
	uint8       m_pad_650[0x6a8 - 0x650];               // +0x650
	uint8       m_bAlpha6a8;                            // +0x6a8
	uint8       m_pad_6a9[3];                           // +0x6a9
	uint32      m_n6ac;                                 // +0x6ac
	uint32      m_n6b0;                                 // +0x6b0
};


// ======================================================================= //
//  0x10015b70 and 0x10015c70 belong to this TU.  0x10015b70 is a
//  register-only (%eax drawer, %edx object) static whose one caller,
//  0x10015c70, loads %eax/%edx/%ecx (0x10015cd6); under this file's
//  /Qip_no_inlining it stays out of line as shipped.  0x10015c70 is the
//  per-model callback of the object walks (d3dr3c.cpp's 0x1007b8e0,
//  w7_10040cc0.cpp, d3dr4c.cpp) and calls 0x10017c14 below; it opens with
//  the 14-byte stack-aligning entry because its callee needs the aligned
//  stack.  0x10015e30 moved down after them (address order).
// ======================================================================= //
struct Ctx1007b8e0;

struct Ticks10015c70
{
	uint32			m_n00;                              // +0x00
};

struct Scene10015c70
{
	uint8			m_pad_00[0x08];
	Ticks10015c70*	m_pTicks08;                         // +0x08
};

extern CD3DModelDraw	g_1008b090;                     // 0x1008b090
extern uint32			g_1008b874;                     // 0x1008b874 clip flags
extern Plane17c14		g_1008bc50[6];                  // 0x1008bc50 frustum
extern Plane17c14		g_1008bcb0[6];                  // 0x1008bcb0 portal frustum
extern Scene10015c70*	g_1008beb0;                     // 0x1008beb0

// ======================================================================= //
//  0x10015b70 -- 244 bytes, register-only (drawer in %eax, object in
//  %edx).  The bounding-sphere frustum test.
//
//  Byte-identical at /Os /Qip /Qip_no_inlining with two edits.  The
//  caller passes the drawer through a local (`pDraw = &g_1008b090`): /Qip
//  propagates a literal `&global` argument into the callee (ours read
//  `mov $g_1008b090,%ecx` where the shipped code reads %eax) and still
//  propagates the literal `&g_1008b874` exactly as shipped.  And the loop
//  walks its own pointer copied from the chosen frustum after the clip
//  store (`mov %eax,%ebx`); `for (i = 0, pPlane = pPlanes; ...)` and an
//  indexed `&pPlanes[i]` are each 4 bytes off.
// ======================================================================= //
static LTBOOL sub_10015b70(CD3DModelDraw* pDraw, D3DObject* pObj,
                           uint32* pClip)
{
	Plane17c14*		pPlanes;
	Plane17c14*		pPlane;
	float			fRadius, fDist;
	int32			i;

	pDraw->m_f474 = ((D3DModel*)pObj->m_Sub1c0.m_p1c)->m_fRadiusa4;
	pDraw->m_f474 *= LTMAX(pObj->m_vScaleac.x,
	                             LTMAX(pObj->m_vScaleac.y, pObj->m_vScaleac.z));

	if (pObj->m_nFlags88 & 0x40)
		pPlanes = g_1008bcb0;
	else
		pPlanes = g_1008bc50;

	fRadius = pDraw->m_f474;
	*pClip = 0x3f;

	pPlane = pPlanes;
	for (i = 0; i < 6; i++)
	{
		fDist = pPlane->m_vNormal.x * pObj->m_vPos18c.x + pPlane->m_vNormal.y * pObj->m_vPos18c.y +
		        pPlane->m_vNormal.z * pObj->m_vPos18c.z - pPlane->m_fDist;
		if (fDist < -fRadius)
			return LTFALSE;

		if (fDist > fRadius)
			*pClip &= ~(1 << i);

		pPlane++;
	}

	return LTTRUE;
}


// ======================================================================= //
//  0x10015c70 -- 178 bytes.  The per-model draw callback.
//
//  Byte-identical (0x10015c7e, the body behind the 14-byte stub) at
//  /Os /Qip /Qip_no_inlining.  Measured, each step on its own:
//    * one condition, `!pModel || ... || ...`: the null test's je goes
//      straight to the shared epilogue (two ifs park a jmp at the end);
//    * nTicks is passed ONCE and added back (`+= nTicks`): icl then
//      pushes the undefined value as `push %edi ; push $0`, exactly the
//      shipped pair -- the `0, nTicks` spelling gave `xor edx; push edx x2`;
//    * the second anim word read through a one-site volatile: the shipped
//      code reloads m_nAnims16c for the second compare, after the movzwl;
//      volatile on the count instead hoists the count load ahead of the
//      movzwl (9 and 10 bytes differ).
// ======================================================================= //
void sub_10015c70(Ctx1007b8e0* pContext, void* pElem)
{
	D3DObject*	pObj = (D3DObject*)pElem;
	D3DModel*	pModel;
	CD3DModelDraw*	pDraw = &g_1008b090;      // a local: see 0x10015b70
	uint32		nTicks;                 // never set: the shipped call pushes %edi

	pModel = (D3DModel*)pObj->m_Sub1c0.m_p1c;
	if (!pModel || pObj->m_wAnim1e8 >= pModel->m_nAnims16c ||
	    *(volatile uint16*)&pObj->m_wAnim1e0 >= pModel->m_nAnims16c)
		return;

	if (!sub_10015b70(pDraw, pObj, &g_1008b874))
		return;

	g_1008b090.sub_10017c10(pObj);

	if (g_n1008ca38)
		sub_10021d10("%s: %d ticks", ((D3DModel*)pObj->m_Sub1c0.m_p1c)->sub_100736f0(),
		             nTicks);

	g_1008beb0->m_pTicks08->m_n00 += nTicks;
}


// 0x10015e30 -- the two-argument byte packer (70 bytes; the ret is at
// 0x10015e75, then `8b f6` and twelve nops).  BOTH arguments arrive in
// registers (%eax, %edx), no stack entry, plain `ret`: that is icl's /Qip
// register convention for an INTERNAL-linkage function, so it is static here
// beside its one caller (0x10017ed6, below).  Plain /Qip inlines it away;
// with /Qip_no_inlining (this file's flag set, see D3D_QIP_OI_TUS) it is
// byte-identical.  The `mov %edi,0x4(%esp)` save into the frame reserve is
// the /O2 tell -- it was in d3dlow.cpp (/Os) and could not match there.
//
// Per byte lane: out.byte[i] = (a.byte[i] >> 2) | (((b >> 2*i) & 3) << 6),
// written word-wide with the shipped constants; the OR order is the shipped
// order.
static uint32 sub_10015e30(uint32 a, uint32 b)
{
	return ((a >> 2) & 0x3f3f3f3f)
	     | ((b & 0x03) << 6)
	     | ((b & 0x0c) << 12)
	     | ((b & 0x30) << 18)
	     | ((b & 0xc0) << 24);
}




// ======================================================================= //
//  big19d64.cpp's declarations: CD3DModelLight (the same object as
//  CD3DModelDraw, seen from 0x10019d64) and the light records.
// ======================================================================= //


// The three axis tests at 0x1001a969..0x1001a9e0 compare the two operands
// against EACH OTHER (`fcom %st(1)`) and then subtract in whichever order
// gives a positive answer -- `DE E1` on the fall-through edge, `DE E9` in
// the out-of-line arms at 0x1001b618/0x1001b61f/0x1001b626.  There is no
// comparison against zero anywhere in the three: no 0.0f constant is
// loaded and no `FTST` is emitted, and icl gives every source float
// literal its own .rdata slot, so a spelling that took the absolute value
// of a difference would have left a zero behind.  It did not, so the
// source compared a with b.
#define D3D_ABSDIFF(a, b)   ((a) < (b) ? (b) - (a) : (a) - (b))


// 0x10014ab0 -- ltmatrix.h:304 Mat_InverseTransformation emitted out of
// line.  It reads its FIRST argument and writes its SECOND.
extern "C" void sub_10014ab0(LTMat* pSrc, LTMat* pDest);


// ltmatrix.h:304, written out here for the ONE site (inside the A arm of
// the transform branch) that carries no call.  MatVMul_3x3 is folded in
// because the shipped code folds it: `newTrans` never reaches memory.
inline void d3d_MatInverseTransformation(LTMat *pSrc, LTMat *pDest)
{
	LTVector trans, newTrans;

	trans.x = pSrc->m[0][3];
	trans.y = pSrc->m[1][3];
	trans.z = pSrc->m[2][3];

	pDest->m[0][0] = pSrc->m[0][0];  pDest->m[0][1] = pSrc->m[1][0]; pDest->m[0][2] = pSrc->m[2][0];
	pDest->m[1][0] = pSrc->m[0][1];  pDest->m[1][1] = pSrc->m[1][1]; pDest->m[1][2] = pSrc->m[2][1];
	pDest->m[2][0] = pSrc->m[0][2];  pDest->m[2][1] = pSrc->m[1][2]; pDest->m[2][2] = pSrc->m[2][2];

	newTrans.x = pDest->m[0][0]*trans.x + pDest->m[0][1]*trans.y + pDest->m[0][2]*trans.z;
	newTrans.y = pDest->m[1][0]*trans.x + pDest->m[1][1]*trans.y + pDest->m[1][2]*trans.z;
	newTrans.z = pDest->m[2][0]*trans.x + pDest->m[2][1]*trans.y + pDest->m[2][2]*trans.z;

	pDest->m[0][3] = -newTrans.x;
	pDest->m[1][3] = -newTrans.y;
	pDest->m[2][3] = -newTrans.z;

	pDest->m[3][0] = pDest->m[3][1] = pDest->m[3][2] = 0.0f;
	pDest->m[3][3] = 1.0f;
}


// ---------------------------------------------------------------------- //
//  The neighbours this function reaches.
// ---------------------------------------------------------------------- //

// 0x1001c790 -- CMoArray<LTMat>::Get, `shl $0x6` (d3dbatchc.cpp).
class Arr1001c790
{
public:
	LTMat*	sub_1001c790(uint32 nIndex);
};


// The model-instance-side object hanging off this+0x00.  Every field named
// here is read by this function and by nothing else that had to be guessed.
struct Src19d64Model
{
	uint8			m_pad_00[0x70];                     // +0x00
	Arr1001c790		m_Array70;                          // +0x70  64-byte elems
	uint8			m_pad_74[0xa8 - 0x74];              // +0x74
	float			m_fRadiusa8;                        // +0xa8
	uint8			m_pad_ac[0xf4 - 0xac];              // +0xac
	uint32			m_nIndexf4;                         // +0xf4
	uint8			m_pad_f8[0xfc - 0xf8];              // +0xf8
	LTMat			m_Matfc;                            // +0xfc
	uint32			m_n13c;                             // +0x13c
};


// The game-object-side record hanging off this+0x04.
struct Src19d64Object
{
	uint8			m_pad_00[0x88];                     // +0x00
	uint32			m_nFlags88;                         // +0x88
	uint32			m_nFlags8c;                         // +0x8c
	uint8			m_pad_90[0x94 - 0x90];              // +0x90
	uint8			m_bR94;                             // +0x94
	uint8			m_bG95;                             // +0x95
	uint8			m_bB96;                             // +0x96
	uint8			m_pad_97[0x238 - 0x97];             // +0x97
	LTVector		m_vCached238;                       // +0x238
	float			m_fCached244;                       // +0x244
};


// One dynamic light, out of the g_p1008bd88 array.
struct Src19d64Light
{
	uint8			m_pad_00[0x94];                     // +0x00
	uint8			m_bR94;                             // +0x94
	uint8			m_bG95;                             // +0x95
	uint8			m_bB96;                             // +0x96
	uint8			m_pad_97[0x18c - 0x97];             // +0x97
	LTVector		m_vPos18c;                          // +0x18c
	uint8			m_pad_198[0x1a8 - 0x198];           // +0x198
	float			m_fRadius1a8;                       // +0x1a8
};


// The global directional light at *0x1008cbcc: Src19d64GlobalLight above.


// *0x1008beb0 -- only its +0x1c vector is touched here.
struct Src19d64Sink
{
	uint8			m_pad_00[0x1c];                     // +0x00
	LTVector		m_v1c;                              // +0x1c
};


// The query object constructed at 0x10015830 and handed to the world's
// slot-2 virtual.  0x34 bytes: -0x3f4 .. -0x3c0 in the shipped frame, and
// d3dr5b.cpp's decode of the constructor gives the three LTVectors and the
// four dwords.
struct Query10015830
{
	uint32			m_n00;                              // +0x00
	uint32			m_n04;                              // +0x04
	LTVector		m_vMin08;                           // +0x08
	LTVector		m_vMax14;                           // +0x14
	LTVector		m_v20;                              // +0x20
	void*			m_pfnCallback2c;                    // +0x2c
	void*			m_pUser30;                          // +0x30

	Query10015830();                                    // 0x10015830
};


// The world/light-grid object at *0x1008beac.  +0x6c is a SUBOBJECT with
// its own vtable; slot 2 takes the query.
class GridQuery19d64
{
public:
	virtual void	Slot0();
	virtual void	Slot1();
	virtual void	Slot2(Query10015830* pQuery);
};

// One light-grid cell: four bytes, read at +0, +1 and +2 (and at +4, +5,
// +6, which is the SAME field triple of the next cell along x).
struct GridCell19d64
{
	uint8			m_b0;
	uint8			m_b1;
	uint8			m_b2;
	uint8			m_b3;
};

struct Src19d64Grid
{
	uint8			m_pad_00[0x6c];                     // +0x00
	GridQuery19d64	m_Query6c;                          // +0x6c
	uint8			m_pad_70[0xb8 - 0x70];              // +0x70
	float			m_fSizeb8;                          // +0xb8 (0x1001bae4)
	uint8			m_pad_bc[0xfc - 0xbc];              // +0xbc
	GridCell19d64*	m_pCellsfc;                         // +0xfc
	uint8			m_pad_100[0x104 - 0x100];           // +0x100
	uint32			m_nStrideY104;                      // +0x104
	uint8			m_pad_108[0x110 - 0x108];           // +0x108
	int32			m_nMaxX110;                         // +0x110
	int32			m_nMaxY114;                         // +0x114
	int32			m_nMaxZ118;                         // +0x118
	uint32			m_nStrideZ11c;                      // +0x11c
	uint8			m_pad_120[0x12c - 0x120];           // +0x120
	float			m_fScale12c;                        // +0x12c
	float			m_fScale130;                        // +0x130
	float			m_fScale134;                        // +0x134
	LTVector		m_vMin138;                          // +0x138
	uint8			m_pad_144[0x18c - 0x144];           // +0x144
	struct WorldModel1be00** m_pModels18c;              // +0x18c (0x1001be00)
	uint32			m_nModels190;                       // +0x190
};


// The argument block the callback at 0x10019a70 receives: it reads (%ecx)
// as an object pointer and 0x4(%ecx).. as matrix rows.
class CD3DModelLight;

struct Callback19a70Data
{
	CD3DModelLight*	m_pOwner;                           // +0x00
	LTMat			m_mInv;                             // +0x04
};


// 0x10019a70 -- the callback.  Plain `ret` with its two arguments at
// 0x14(%esp) and 0x18(%esp) after four pushes: __cdecl, two dwords.
struct Light19a70;
extern "C" void sub_10019a70(Light19a70* pLight, Callback19a70Data* pData);


// ---------------------------------------------------------------------- //
//  Globals.  Relocations are masked, so only the shapes matter.
// ---------------------------------------------------------------------- //
extern LTMat				g_Mat1008b91c;              // 0x1008b91c
extern uint32				g_n1008ca3c;                // 0x1008ca3c
extern uint32				g_n1008cb98;                // 0x1008cb98
// 0x1008af38: the value field (+4) of the console variable at 0x1008af34
// (Bank10021f40) -- one object, not a float of its own.                // 0x1008af38
struct Bank10021f40;
extern struct Bank10021f40 g_1008af34;
#define g_f1008af38 (*(float*)((char*)&g_1008af34 + 4))
extern uint32				g_n1008af54;                // 0x1008af54
// 0x1008af78: the value field (+4) of the console variable at 0x1008af74
// (Bank10021f40) -- one object, not a float of its own.                // 0x1008af78
struct Bank10021f40;
extern struct Bank10021f40 g_1008af74;
#define g_f1008af78 (*(float*)((char*)&g_1008af74 + 4))
extern Src19d64Sink*		g_p1008beb0;                // 0x1008beb0
extern Src19d64Grid*		g_p1008beac;                // 0x1008beac
extern uint32				g_n1008bd84;                // 0x1008bd84
extern Src19d64Light*		g_p1008bd88[1];             // 0x1008bd88


// ---------------------------------------------------------------------- //
//  One entry of the 16-slot model-light table at this+0x3c.  Stride 0x2c
//  from `imul $0x2c,%edx,%ebx`, and every one of the eleven floats below
//  is written by the accepted-light block.
// ---------------------------------------------------------------------- //
struct ModelLight19d64
{
	LTVector		m_vPos00;                           // +0x00
	LTVector		m_vDir0c;                           // +0x0c
	float			m_fRadiusSqr18;                     // +0x18
	float			m_fInvRadiusSqr1c;                  // +0x1c
	float			m_fR20;                             // +0x20
	float			m_fG24;                             // +0x24
	float			m_fB28;                             // +0x28
};


class CD3DModelLight
{
public:
	void			sub_10019d60();                     // 0x10019d64
	void			sub_10019a24();                     // 0x10019a24 (d3dlow)
	float			sub_1001bae0();                     // 0x1001bae4

	// 0x1001bae4 reaches m_pObject04's vtable +0x44 (CD3DObject1bae4).

	Src19d64Model*	m_pModel00;                         // +0x000
	Src19d64Object*	m_pObject04;                        // +0x004
	uint8			m_pad_08[0x3c - 0x08];              // +0x008
	ModelLight19d64	m_Lights3c[16];                     // +0x03c
	uint32			m_nLights2fc;                       // +0x2fc
	uint32			m_nMaxLights300;                    // +0x300
	uint8			m_pad_304[0x310 - 0x304];           // +0x304
	LTMat			m_Mat310;                           // +0x310
	uint8			m_pad_350[0x390 - 0x350];           // +0x350
	LTMat			m_Mat390;                           // +0x390
	uint8			m_pad_3d0[0x410 - 0x3d0];           // +0x3d0
	LTVector		m_vPos410;                          // +0x410
	uint8			m_pad_41c[0x650 - 0x41c];           // +0x41c
	LTVector		m_v650;                             // +0x650
	float			m_fR65c;                            // +0x65c
	float			m_fG660;                            // +0x660
	float			m_fB664;                            // +0x664
	LTVector		m_v668;                             // +0x668
	LTVector		m_vLightDir674;                     // +0x674
	float			m_f680;                             // +0x680
	LTVector		m_vAmbient684;                      // +0x684
};


// ======================================================================= //


// ----------------------------------------------------------------------- //
//  0x10017c14 -- __thiscall, one dword argument, `ret $0x4`, void.
//  One caller: 0x10015cea, on the global object at 0x1008b090.
// ----------------------------------------------------------------------- //
void CD3DModelDraw::sub_10017c10(D3DObject* pObject)
{
	//  Only the SIX matrices the source has to name are declared.  The
	//  five at -0x3f8, -0x3b8, -0x2f8, -0x2b8 and -0x278 are the return
	//  temps of the five by-value calls that are ASSIGNED rather than
	//  initialised; icl makes those itself and copies out of them, which
	//  is exactly what the shipped `rep movsl`s after each call are.
	D3DRenderInfo   info;               // -0x68c, ctor 0x10015950
	LTMat           matProj;            // -0x438
	LTMat           mView;              // -0x378
	LTMat           matDead;            // -0x338  written, never read
	LTMat           matBone;            // -0x238
	LTMat           matStage1;          // -0x1f8
	LTMat           matStage2;          // -0x1b8
	LTVec           vScaled;            // -0x178
	LTVec           vPoint;             // -0x16c
	Plane17c14         plane;              // -0x160
	LTVec           vProj;              // -0x150
	float           fSaveNear;          // -0x110
	float           fDistSqr;
	float           fT;
	float           fFovX;
	float           fFovY;
	float           fW;
	uint32          i;
	uint32          nColor;
	uint8           nAlpha;

	// ------------------------------------------------------------------ //
	//  0x10017c33 .. 0x10017cc7 -- wiring.
	//
	//  m_pSub08 is STORED (0x10017c59) before 0x100158a0 is called on the
	//  same pointer (0x10017c5c), and 0x100158a0 is `mov 0x1c(%ecx),%eax`,
	//  so m_pModel00 is *(pObject + 0x1c0 + 0x1c).  m_pObject04 is written
	//  after m_pModel00 (`mov %eax,(%edx)` then `mov %ecx,0x4(%edx)`).
	// ------------------------------------------------------------------ //
	m_pSub08    = &pObject->m_Sub1c0;
	m_pModel00  = (D3DModel*)m_pSub08->sub_100158a0();
	m_pObject04 = pObject;

	if (pObject->m_nFlags88 & 0x40)
	{
		// 0x10017c7d -- `add $-8,%esp`, source at (%esp), destination at
		// 0x4(%esp), the matrix in %ecx.
		g_Mat1008b91c.sub_10015770(m_pObject04->sub_100158b0(), &m_vPos410);
	}
	else
	{
		// 0x10017ca9 -- `rep movsl` %ecx=3.
		m_vPos410 = *m_pObject04->sub_100158b0();
	}

	// 0x10017cc8 -- the false arm is sunk to 0x10019625 and comes back to
	// 0x10017cd5, so the two tests really are one `&&`.
	if (!g_n1008af14 && m_pModel00->m_n150)
		return;

	// 0x10017cd5 -- and the second one falls straight into the epilogue.
	if (!g_n1008cbc8 && (pObject->m_nFlags88 & 0x40))
		return;

	// ------------------------------------------------------------------ //
	//  0x10017cf9 -- the LOD cross-fade against the billboard.
	// ------------------------------------------------------------------ //
	m_n030 = ((m_pObject04->m_nFlags88 & 8) != 0);

	// 0x10017d29 -- register-ABI indirect call, %ecx and %edx.
	(*g_pfn1008ce8c)(&m_vPos410, &m_n480);

	// 0x10017d46 .. 0x10017d6c -- the operator- temp at -0x444 is the
	// `this` of MagSqr, so the two calls are one chained expression.
	fDistSqr = m_vPos410.sub_10015640(g_v1008bd34).sub_10015510();

	if (m_pModel00->m_fFade1b4 > m_pModel00->m_fFade3bc)
	{
		// 0x10019595 -- the reversed range, sunk below the epilogue.
		if (m_pModel00->m_fFade3bc > fDistSqr)
			return;

		if (m_pModel00->m_fFade1b4 > fDistSqr)
		{
			// 0x100195ca.  `DE F9` at 0x100195ef is Intel FDIVP.
			fT = (float)(((float)sqrt((double)fDistSqr) - m_pModel00->m_fFade2b8)
			             / (m_pModel00->m_fFade0b0 - m_pModel00->m_fFade2b8));

			// 0x10019607 -- no `fxch` before the `fmulp`, so the value
			// already in %st is the LEFT operand.
			m_bAlpha6a8 = (uint8)sub_10015190(
				fT * (float)m_pObject04->m_bAlpha97);
		}
		else
		{
			// 0x100195b0.
			m_bAlpha6a8 = m_pObject04->m_bAlpha97;
		}
	}
	else if (m_pModel00->m_fFade3bc < fDistSqr)
	{
		// 0x1001942a -- past the far edge: the billboard alone, opaque,
		// and RETURN.
		sub_10016324(pObject, 0xff);
		return;
	}
	else if (m_pModel00->m_fFade1b4 < fDistSqr)
	{
		// 0x10017dbd.
		fT = (float)(((float)sqrt((double)fDistSqr) - m_pModel00->m_fFade0b0)
		             / (m_pModel00->m_fFade2b8 - m_pModel00->m_fFade0b0));

		// 0x10017df4 -- the `fildl` is emitted BEFORE the `fsubrs`, with
		// an `fxch` between, so the alpha is the left operand here.
		// 0x100a4ba0 is 1.0f.
		nAlpha = (uint8)sub_10015190(
			(float)m_pObject04->m_bAlpha97 * (1.0f - fT));

		m_bAlpha6a8 = nAlpha;

		// 0x10017e2f -- `neg %dl ; add $0xff,%dl`.
		sub_10016324(pObject, (uint8)(0xff - nAlpha));
	}
	else
	{
		// 0x10017da6.
		m_bAlpha6a8 = m_pObject04->m_bAlpha97;
	}

	// ------------------------------------------------------------------ //
	//  0x10017e34.
	// ------------------------------------------------------------------ //
	m_n458 = ((g_n1008cb68 || (pObject->m_nFlags88 & 0x20))
	          && g_n1008cb64 && g_p1008cbcc->m_n104);

	// 0x10017e84 -- the argument is PUSHed here, unlike every other call
	// in the function.
	m_f47c = g_v1008bd34.sub_100154b0(m_vPos410);

	// 0x10017e9a.
	sub_10019640();

	// ------------------------------------------------------------------ //
	//  0x10017e9f -- the cached-render probe.  `m_n6ac = 0` is duplicated
	//  at 0x10017f11 and 0x10017f23, one copy per failing arm.
	// ------------------------------------------------------------------ //
	if (m_pModel00->m_n150 && g_n1008aef4)
	{
		// 0x10017ed6 -- both arguments in registers.
		nColor = sub_10015e30(m_n480, m_n44c);

		if (!m_n450 && m_bAlpha6a8 == m_pObject04->m_bAlpha97)
		{
			// 0x1001944f, sunk whole below the epilogue.
			if (sub_10015470()->sub_1007e3c0(m_pObject04, nColor))
			{
				m_n6ac = 2;
			}
			else if (g_Cache10091d80.sub_1007e430(m_pObject04, nColor,
			                                      sub_1001bf30(), 0))
			{
				m_n6ac = 1;
			}
			else
			{
				m_n6ac = 0;
			}
		}
		else
		{
			m_n6ac = 0;
		}
	}
	else
	{
		m_n6ac = 0;
	}

	// ------------------------------------------------------------------ //
	//  0x10017f2d -- the frame serial.  Both the compare (0x10017f3b) and
	//  the store (0x1001958a) are 16-bit.  The body is at 0x10019579.
	// ------------------------------------------------------------------ //
	if (g_w1008aed0 != g_w1008bea8)
	{
		g_Cache10091d80.sub_1007e7c0();
		g_w1008aed0 = g_w1008bea8;
	}

	// 0x10017f43 -- the true arm is the two-instruction block at
	// 0x100194d7 (`xor %eax,%eax ; inc %eax`).
	m_n478 = (g_n10089408 && (pObject->m_nFlags88 & 4)
	          && pObject->m_bAlpha97 == 0xff);

	// 0x10017f88 -- `m_n6ac != 0` short-circuits to 1 at 0x10017fb2 and
	// the three remaining tests all fall to 0 at 0x10017fb7.
	m_n6b0 = (m_n6ac
	          || (g_n1008aed4
	              && !(m_pObject04->m_nFlags88 & 0x40)
	              && !m_pModel00->m_n140));

	// 0x10017fb9 -- m_pObject04 is loaded three times (0x10017fbc,
	// 0x10017fbf, 0x10017fd2), so it is mentioned three times.
	sub_100151b0(&m_pObject04->m_vPos18c, &m_pObject04->m_Rot9c,
	             &m_pObject04->m_vScaleac, &m_Mat310);

	// ------------------------------------------------------------------ //
	//  0x10017ff7 -- the view matrix.
	//
	//  The two bounds are 10 and 170 DEGREES in radians: 0.17453292f is
	//  0x3e32b8c2 and 2.96705961f is 0x403de44e, and each appears in EIGHT
	//  .rdata slots (four clamps x two mentions).  0x1007fdf4 takes one
	//  double and its two answers land in m[0][0] and m[1][1] of a matrix
	//  that is otherwise diag(1,1) -- half-angle tangents.
	// ------------------------------------------------------------------ //
	if (m_pModel00->m_n140)
	{
		LTMat   matFov;

		// M-P0-01: widescreen (Hor+) field of view.  A model with a field
		// of view of its own (the player-view weapons) is scaled across by
		// tan(fovX/2) of the SCENE's field of view plus its offset, so the
		// widened scene field of view would stretch it sideways.  Start
		// from the field of view the game asked for (its 4:3 one,
		// w4_1001eb90_os.cpp), and narrow the result below by the same
		// factor the scene was widened by: the model then looks exactly as
		// it does at 4:3.
		if (g_n1008af94)
		{
			fFovX = D3D_CLAMPF(
				(m_pModel00->m_f144 + g_f1008afd8) * g_fModernFovX4x3,
				0.17453292f, 2.96705961f);
			fFovY = D3D_CLAMPF(
				(m_pModel00->m_f148 + g_f1008afb8) * g_f1008b8d0,
				0.17453292f, 2.96705961f);
		}
		else
		{
			fFovX = D3D_CLAMPF(
				g_f1008afd8 * 0.017453292f + m_pModel00->m_f144
					+ g_fModernFovX4x3,
				0.17453292f, 2.96705961f);
			fFovY = D3D_CLAMPF(
				g_f1008afb8 * 0.017453292f + m_pModel00->m_f148
					+ g_f1008b8d0,
				0.17453292f, 2.96705961f);
		}

		// 0x100180d9 / 0x100180f0 -- note the operand order differs
		// between the two: `fmuls 0.5` on the value already in %st for X,
		// `flds 0.5 ; fmull` for Y, whose operand had to be spilled as a
		// double across the first call.
		// M-P0-01: narrowed as the scene was widened (see above)
		matFov.m[0][0] = (float)tan((double)(fFovX * 0.5f)) / g_fModernFovXScale;
		matFov.m[0][1] = 0.0f;
		matFov.m[0][2] = 0.0f;
		matFov.m[0][3] = 0.0f;

		matFov.m[1][0] = 0.0f;
		matFov.m[1][1] = (float)tan((double)(0.5f * fFovY));
		matFov.m[1][2] = 0.0f;
		matFov.m[1][3] = 0.0f;

		matFov.m[2][0] = 0.0f;
		matFov.m[2][1] = 0.0f;
		matFov.m[2][2] = 1.0f;
		matFov.m[2][3] = 0.0f;

		matFov.m[3][0] = 0.0f;
		matFov.m[3][1] = 0.0f;
		matFov.m[3][2] = 0.0f;
		matFov.m[3][3] = 1.0f;

		// 0x10018154 -- by value, `ret $0x40`.
		matProj.sub_1001e140(matFov);

		// 0x10018169 / 0x1001819d -- an ASSIGNMENT, so each arm gets its
		// own return temp (-0x3b8 and -0x3f8) plus a `rep movsl`.
		if (pObject->m_nFlags88 & 0x40)
			mView = matProj.sub_10015800(g_Mat1008badc);
		else
			mView = matProj.sub_10015800(g_Mat1008bb1c);

		// 0x100181ea -- THE DEAD ONE.  -0x338 is written here and read
		// nowhere in the function; icl does not eliminate dead stores
		// through memory, so the statement is in the source.
		matDead = g_Mat1008ba1c.sub_10015800(mView);

		// 0x10018210.
		m_Mat350 = mView.sub_10015800(m_Mat310);
	}
	else
	{
		// 0x10018233 -- `rep movsl` %ecx=0x10 out of a constant matrix.
		if (pObject->m_nFlags88 & 0x40)
			mView = g_Mat1008bb5c;
		else
			mView = g_Mat1008b9dc;

		// 0x10018269 -- cmp $1, cmp $2, then m_n6b0.
		if (m_n6ac == 1)
		{
			m_Mat350 = m_Mat310;
			g_n1008b874 = 1;
		}
		else if (m_n6ac == 2)
		{
			m_Mat350.sub_100156e0();
			g_n1008b874 = 1;
		}
		else if (m_n6b0)
		{
			m_Mat350 = m_Mat310;
			g_n1008b874 = 1;
		}
		else
		{
			m_Mat350 = mView.sub_10015800(m_Mat310);
		}
	}

	// 0x1001833a -- an assignment, so the by-value answer lands in a temp
	// at -0x278 and is copied.  (written with the real
	// LTMatrix and ltmatrix.h's inline MakeInverse, this TU's flags inline
	// it -- and Inverse, with a CRT fabs call -- into this function, where
	// the shipped code CALLS 0x10015740; so the call stays on the address-
	// named view.  0x1000ac10, the out-of-line Inverse, is emitted by
	// big0ae20.cpp, not here.)
	m_Mat390 = m_Mat350.sub_10015740();

	// 0x1001836b.
	if (!sub_10019870())
		return;

	// ------------------------------------------------------------------ //
	//  0x1001837e -- the near plane is saved across the draw and forced to
	//  0.01f (0x3c23d70a) for the `& 0x40` case.
	// ------------------------------------------------------------------ //
	fSaveNear = g_f1008b908;

	if (m_pObject04->m_nFlags88 & 0x40)
	{
		g_n1008b874 |= 1;
		g_f1008b908 = 0.01f;
	}

	// 0x100183ae.
	if (m_n6ac != 2)
	{
		// 0x100183c1 -- vtable +0x3c, one pointer argument, callee-clean.
		(m_pObject04->*m_pObject04->m_pVtbl->Slot15)(&m_n4e4);

		// 0x100183e5 -- +0x5cc first, then +0x5c8.
		m_n5cc = 0x20;
		m_n5c8 = (uint32)m_Arr628.m_pArray;

		// 0x10018401 -- vtable +0x38.
		(m_pObject04->*m_pObject04->m_pVtbl->Slot14)(&info);

		// 0x10018403 -- the +0xf4 store is emitted between the load of
		// +0xec and the branch on it, so it precedes the test in source.
		// The `else` arm is the sunk block at 0x10019563.
		info.m_pObjectf4 = m_pObject04;

		if (info.m_n0ec)
			info.m_pMate4 = &m_Mat310;
		else
			info.m_pMate4 = &m_Mat350;

		// 0x10018437 -- the failure arm is at 0x10019405 and returns.
		if (!info.sub_1007ca60())
		{
			sub_10021d10("Model::SetupTransforms failed for %s.",
			             m_pModel00->sub_100736f0());
			return;
		}

		// 0x10018444 -- +0xec is tested a SECOND time; icl does not fold
		// redundant tests, so the source tests it twice.
		if (info.m_n0ec)
		{
			// ---------------------------------------------------------- //
			//  0x10018452 .. 0x10018839 -- CALL-FREE REGION A, 868 bytes
			//  of it.  ltmatrix.h:172 MatMul into a temp at -0x238 and a
			//  `rep movsl` %ecx=0x10 back over the bone, once per bone.
			//  The bone array is loaded TWICE per iteration (0x1001848b
			//  and 0x100184a2), which is why it is written twice here.
			//  The counter and a 0x40-strided byte offset are both kept
			//  live (-0x128 and -0x124); the offset is icl's strength
			//  reduction of the subscript, not a second variable.
			// ---------------------------------------------------------- //
			for (i = 0; i < m_pModel00->m_nBones78; i++)
			{
				d3d_MatMul(&matBone, &mView, &m_pModel00->m_pBones74[i]);
				m_pModel00->m_pBones74[i] = matBone;
			}
		}

		// 0x1001883b.
		if (!g_n1008b874)
			sub_1001b840(&g_Mat1008ba1c);

		// 0x1001885d -- big19d64.cpp's per-model lighting setup, its one
		// caller.
		((CD3DModelLight*)this)->sub_10019d60();

		// 0x10018862 -- the first argument is a literal 0.
		if (g_n1008ca38)
			sub_10021d10("SetupModelLight: %d (%d lights)", 0,
			             m_nLights2fc);
	}

	// ------------------------------------------------------------------ //
	//  0x10018895 .. 0x1001913f -- CALL-FREE REGION B, 2,120 bytes.  Two
	//  chained MatMuls into m_Mat3d0, or a straight copy of the constant.
	//  Store order in both is column by column, [0][0] [1][0] [2][0]
	//  [3][0] [0][1] ... at 0x10 apart, which is d3d_MatMul verbatim.
	// ------------------------------------------------------------------ //
	if (m_n458 && m_pModel00->m_n13c)
	{
		// 0x100188bf -- `imul $0x40,%edx,%edx` on +0xf4, so a 64-byte
		// element, i.e. a bone matrix.
		d3d_MatMul(&matStage1, &g_Mat1008b95c,
		           &m_pModel00->m_pBones74[m_pModel00->m_nIndexf4]);

		// 0x10018d13 -- the second operand is the model's own 4x4 at
		// +0xfc, read at +0xfc .. +0x138.
		d3d_MatMul(&matStage2, &matStage1, &m_pModel00->m_Matfc);

		// 0x10019124 -- `rep movsl` %ecx=0x10 into this+0x3d0.
		m_Mat3d0 = matStage2;
	}
	else
	{
		// 0x10019128.
		m_Mat3d0 = g_Mat1008b95c;
	}

	// ------------------------------------------------------------------ //
	//  0x10019140 -- the clip flags, the two function-pointer slots and
	//  the object's own callback.
	// ------------------------------------------------------------------ //
	if (m_pObject04->m_n1fc && !(m_pObject04->m_nFlags180 & 8))
		g_n1008b874 |= 0x3f;                    // 0x10019557

	// 0x10019162 / 0x10019183 -- g_n1008cb24 is read TWICE.
	m_pfn41c = g_n1008cb24 ? (void*)sub_10015a60 : (void*)sub_10069c90;
	m_pfn420 = g_n1008cb24 ? (void*)sub_10015aa0 : (void*)sub_100159d0;

	// 0x100191a1.
	m_nFlags0c = 0;

	if (m_pObject04->m_pfn1b4)
	{
		// 0x100191ca -- __cdecl, three arguments, 0x10 unwound after.
		(*m_pObject04->m_pfn1b4)(m_pObject04, &m_nFlags0c, 0);
	}

	// 0x100191cf.
	if (m_nFlags0c & 1)
	{
		// 0x100191e0 -- built into a temp at -0x178 and copied to -0x16c
		// (`rep movsl` %ecx=3), so the source assigns a returned value.
		// The operand order really does differ between .x and .y/.z.
		vScaled.x = m_v010.x * m_f01c;
		vScaled.y = m_f01c * m_v010.y;
		vScaled.z = m_f01c * m_v010.z;
		vPoint = vScaled;

		// 0x1001921f -- 0x1008ba0c/ba10/ba14/ba18 is the LAST ROW of the
		// matrix at 0x1008b9dc (+0x30, +0x34, +0x38, +0x3c), and
		// `fdivrs 0x100a4c00` with 0x100a4c00 == 1.0f is the reciprocal.
		fW = 1.0f / (g_Mat1008b9dc.m[3][0] * vPoint.x
		           + g_Mat1008b9dc.m[3][1] * vPoint.y
		           + g_Mat1008b9dc.m[3][2] * vPoint.z
		           + g_Mat1008b9dc.m[3][3]);

		// 0x10019253 .. 0x10019309.
		vProj.x = (g_Mat1008b9dc.m[0][0] * vPoint.x
		         + g_Mat1008b9dc.m[0][1] * vPoint.y
		         + g_Mat1008b9dc.m[0][2] * vPoint.z
		         + g_Mat1008b9dc.m[0][3]) * fW;

		vProj.y = (g_Mat1008b9dc.m[1][0] * vPoint.x
		         + g_Mat1008b9dc.m[1][1] * vPoint.y
		         + g_Mat1008b9dc.m[1][2] * vPoint.z
		         + g_Mat1008b9dc.m[1][3]) * fW;

		vProj.z = (g_Mat1008b9dc.m[2][0] * vPoint.x
		         + g_Mat1008b9dc.m[2][1] * vPoint.y
		         + g_Mat1008b9dc.m[2][2] * vPoint.z
		         + g_Mat1008b9dc.m[2][3]) * fW;

		// 0x1001930f .. 0x1001937f -- the SAME matrix applied to the raw
		// vector with NO translation term: a 3x3 normal transform.
		plane.m_vNormal.x = g_Mat1008b9dc.m[0][0] * m_v010.x
		                  + g_Mat1008b9dc.m[0][1] * m_v010.y
		                  + g_Mat1008b9dc.m[0][2] * m_v010.z;

		plane.m_vNormal.y = g_Mat1008b9dc.m[1][0] * m_v010.x
		                  + g_Mat1008b9dc.m[1][1] * m_v010.y
		                  + g_Mat1008b9dc.m[1][2] * m_v010.z;

		plane.m_vNormal.z = g_Mat1008b9dc.m[2][0] * m_v010.x
		                  + g_Mat1008b9dc.m[2][1] * m_v010.y
		                  + g_Mat1008b9dc.m[2][2] * m_v010.z;

		// 0x10019385 -- `rep movsl` %ecx=3, -0x150 into -0x16c.
		vPoint = vProj;

		// 0x10019387 -- the dot lands at -0x154, four bytes past the
		// normal, and the whole 16 bytes go out in ONE `rep movsl`
		// %ecx=4 at 0x100193c9, which is what makes it a plane.
		plane.m_fDist = plane.m_vNormal.x * vPoint.x
		              + plane.m_vNormal.y * vPoint.y
		              + plane.m_vNormal.z * vPoint.z;

		m_Plane020 = plane;

		// 0x100193cb.
		g_n1008b874 |= 1;
	}

	// ------------------------------------------------------------------ //
	//  0x100193d2 -- the tail.
	// ------------------------------------------------------------------ //
	sub_10043e10();

	// 0x100193e9 -- an INTEGER `mov` of a float lvalue, which is the
	// float-lvalue rule, not an integer field.
	g_f1008b908 = fSaveNear;

	if (g_n1008b874)
		g_n1008cea8++;
	else
		g_n1008cea4++;
}


// ----------------------------------------------------------------------- //
// ----------------------------------------------------------------------- //
//  0x10019644 -- pick the model's LOD from the distance in +0x47c, biased
//  by g_n1008b034 whole LODs, and the blend factor towards the next one.
// ----------------------------------------------------------------------- //
// GetLODDist as one ?: expression, as `if (i != 0)` /
// `if (i > 0)` first, or through a result variable gives the same bytes:
// the shipped i==0-inline / i!=0-sunk placement is not reached; nor by
// building this TU as /Os /Qip /Qip_no_inlining (same) or /O2 /Qip
// /Qip_no_inlining (519 bytes, still unpaired).
void CD3DModelDraw::sub_10019640()
{
	float   fDist;
	float*  pNext;
	float*  pCur;
	float   fRange;
	float   fLeft;
	uint32  i;
	int32   nBias;

	nBias = g_n1008b034;

	if (nBias > 0)
	{
		fDist = m_f47c +
		        *m_pModel00->GetLODDist(LTCLAMP(nBias, 0, m_pModel00->m_nLODDists94));
	}
	else if (nBias == 0)
	{
		fDist = m_f47c;
	}
	else
	{
		fDist = m_f47c -
		        *m_pModel00->GetLODDist(LTCLAMP(-nBias, 0, m_pModel00->m_nLODDists94));
	}

	m_n44c = 0;
	for (i = 0; i < m_pModel00->GetNumLODs(); i++)
	{
		if (*m_pModel00->GetLODDist(i) < fDist)
			m_n44c = i;
	}

	m_n450 = 0;
	if (g_n1008b014)
	{
		if (m_n44c + 1 < m_pModel00->GetNumLODs())
		{
			pNext  = m_pModel00->GetLODDist(m_n44c + 1);
			fRange = g_f1008aff8;
			pCur   = m_pModel00->GetLODDist(m_n44c);

			if ((*pNext - *pCur) * 0.5f <= fRange)
				fRange = (*pNext - *pCur) * 0.5f;

			fLeft = *pNext - fDist;
			if (fLeft < fRange)
			{
				m_n450 = 1;
				m_fBlend454 = 1.0f - fLeft / fRange;
			}
		}
	}
}


// ----------------------------------------------------------------------- //
//  0x10019874 -- make sure the vertex work array (+0x628, 32-byte
//  elements, the model's +0x64 count plus 32 spare) and the bone matrix
//  array (+0x63c, one LTMat per bone) are big enough.  0 on an allocation
//  failure.
// ----------------------------------------------------------------------- //
int32 CD3DModelDraw::sub_10019870()
{
	if (m_pModel00->NumVerts() > m_Arr628.GetSize())
	{
		if (!m_Arr628.SetSize(m_pModel00->NumVerts() + 32))
			return 0;
	}

	if (m_pModel00->NumNodes() > m_Arr63c.GetSize())
	{
		if (!m_Arr63c.SetSize(m_pModel00->NumNodes()))
			return 0;
	}

	return 1;
}


// ----------------------------------------------------------------------- //
//  0x1001b844 -- bone[i] = *pMat * model bone[i], for every bone:
//  ltmatrix.h's MatMul, inlined (the sixteen sums in its order, m[0][0],
//  m[1][0], m[2][0], m[3][0], m[0][1], ...).
//  NEAR (615 -> 598 bytes).  Through the arrays' accessors -- the
//  count by NumNodes(), the bone by the model's transform array's
//  operator[] (XfArr70, the CMoArray at +0x70), the destination by
//  m_Arr63c[i] -- icl re-reads this->m_pModel00 and the array bases in
//  the loop and indexes the bones as (base,offset), as shipped.  LEFT:
//  (a) the x87 term order inside each of the sixteen sums (icl
//  reassociates them; parenthesising/reversing changes nothing), and
//  (b) the destination's `(%eax,%ebp,1)`, which ours spells
//  `0x0(%ebp,%eax,1)` (two bytes longer).  Raw fields instead of the
//  accessors: 612-615.
// ----------------------------------------------------------------------- //
#define M1 pMat1->m
#define M2 pMat2->m

static inline void MatMul1b844(LTMat *pDest, LTMat *pMat1, LTMat *pMat2)
{
	pDest->m[0][0] = M1[0][0]*M2[0][0] + M1[0][1]*M2[1][0] + M1[0][2]*M2[2][0] + M1[0][3]*M2[3][0];
	pDest->m[1][0] = M1[1][0]*M2[0][0] + M1[1][1]*M2[1][0] + M1[1][2]*M2[2][0] + M1[1][3]*M2[3][0];
	pDest->m[2][0] = M1[2][0]*M2[0][0] + M1[2][1]*M2[1][0] + M1[2][2]*M2[2][0] + M1[2][3]*M2[3][0];
	pDest->m[3][0] = M1[3][0]*M2[0][0] + M1[3][1]*M2[1][0] + M1[3][2]*M2[2][0] + M1[3][3]*M2[3][0];

	pDest->m[0][1] = M1[0][0]*M2[0][1] + M1[0][1]*M2[1][1] + M1[0][2]*M2[2][1] + M1[0][3]*M2[3][1];
	pDest->m[1][1] = M1[1][0]*M2[0][1] + M1[1][1]*M2[1][1] + M1[1][2]*M2[2][1] + M1[1][3]*M2[3][1];
	pDest->m[2][1] = M1[2][0]*M2[0][1] + M1[2][1]*M2[1][1] + M1[2][2]*M2[2][1] + M1[2][3]*M2[3][1];
	pDest->m[3][1] = M1[3][0]*M2[0][1] + M1[3][1]*M2[1][1] + M1[3][2]*M2[2][1] + M1[3][3]*M2[3][1];

	pDest->m[0][2] = M1[0][0]*M2[0][2] + M1[0][1]*M2[1][2] + M1[0][2]*M2[2][2] + M1[0][3]*M2[3][2];
	pDest->m[1][2] = M1[1][0]*M2[0][2] + M1[1][1]*M2[1][2] + M1[1][2]*M2[2][2] + M1[1][3]*M2[3][2];
	pDest->m[2][2] = M1[2][0]*M2[0][2] + M1[2][1]*M2[1][2] + M1[2][2]*M2[2][2] + M1[2][3]*M2[3][2];
	pDest->m[3][2] = M1[3][0]*M2[0][2] + M1[3][1]*M2[1][2] + M1[3][2]*M2[2][2] + M1[3][3]*M2[3][2];

	pDest->m[0][3] = M1[0][0]*M2[0][3] + M1[0][1]*M2[1][3] + M1[0][2]*M2[2][3] + M1[0][3]*M2[3][3];
	pDest->m[1][3] = M1[1][0]*M2[0][3] + M1[1][1]*M2[1][3] + M1[1][2]*M2[2][3] + M1[1][3]*M2[3][3];
	pDest->m[2][3] = M1[2][0]*M2[0][3] + M1[2][1]*M2[1][3] + M1[2][2]*M2[2][3] + M1[2][3]*M2[3][3];
	pDest->m[3][3] = M1[3][0]*M2[0][3] + M1[3][1]*M2[1][3] + M1[3][2]*M2[2][3] + M1[3][3]*M2[3][3];
}

#undef M1
#undef M2

void CD3DModelDraw::sub_1001b840(LTMat* pMat)
{
	uint32 i;

	for (i = 0; i < m_pModel00->NumNodes(); i++)
		MatMul1b844(&m_Arr63c[i], pMat, &m_pModel00->m_Xf70[i]);
}




// ----------------------------------------------------------------------- //
// ----------------------------------------------------------------------- //
//  0x10019a70 -- the world query's callback for each dynamic light near
//  the model (big19d64.cpp's 0x1001b517 hands it over with the owner and
//  the inverse model matrix).  A light that reaches the model's bounding
//  sphere becomes the next entry of the owner's light table; a spot light
//  (cone cosine strictly between -1 and 1) is dropped outside its cone
//  and otherwise blends its inner colour (+0x78) towards its outer colour
//  (+0x94) across the cone.
//
//  NEAR (738 of 737 bytes of body).  Ship's layout comes from the early
//  `return` on the radius test (the epilogue sits right after it), the
//  blend as the `fCone <= fDot` arm with `else return` (inline, not a cold
//  block), the record colours stored R, G, B (that order is what gives the
//  x87 spill slots ship's assignment) and the position through MatVMul.
//  LEFT: (a) the dot product -- ship forms dir.z*dZ, dir.y*dY, dir.x*dX
//  and then scales each by fInv; ours scales dir.y and dir.x by fInv
//  first (tried: fInv first, d first, parenthesised, terms reversed); (b)
//  MatVMul's second row sums y, z, x where ship (and rows 1, 3) go z, y, x.
// ----------------------------------------------------------------------- //
struct Light19a70
{
	uint8           m_pad_00[0x68];                     // +0x00
	LTVector        m_vPos68;                           // +0x68
	float           m_fRadius74;                        // +0x74
	LTVector        m_vColor78;                         // +0x78
	LTVector        m_vDir84;                           // +0x84
	float           m_fCone90;                          // +0x90
	LTVector        m_vColor94;                         // +0x94
};

// ltmatrix.h's MatVMul (with the translation term), inlined: written
// through it, the first row's sum comes out z, y, x as shipped.
static inline void MatVMul19a70(LTVector *pDest, LTMat *pMat, LTVector *pSrc)
{
	pDest->x = pMat->m[0][0]*pSrc->x + pMat->m[0][1]*pSrc->y + pMat->m[0][2]*pSrc->z + pMat->m[0][3];
	pDest->y = pMat->m[1][0]*pSrc->x + pMat->m[1][1]*pSrc->y + pMat->m[1][2]*pSrc->z + pMat->m[1][3];
	pDest->z = pMat->m[2][0]*pSrc->x + pMat->m[2][1]*pSrc->y + pMat->m[2][2]*pSrc->z + pMat->m[2][3];
}

extern "C" void sub_10019a70(Light19a70* pLight, Callback19a70Data* pData)
{
	CD3DModelLight*  pOwner = pData->m_pOwner;
	ModelLight19d64* pRec;
	float            fRadius, fR, fG, fB, fR2, fG2, fB2, fCone;
	float            fDX, fDY, fDZ;
	float            fDist, fEdge, fInv, fDot, fT;

	fRadius = pLight->m_fRadius74;
	fR      = pLight->m_vColor78.x;
	fG      = pLight->m_vColor78.y;
	fB      = pLight->m_vColor78.z;
	fR2     = pLight->m_vColor94.x;
	fG2     = pLight->m_vColor94.y;
	fB2     = pLight->m_vColor94.z;
	fCone   = pLight->m_fCone90;

	if (pOwner->m_nLights2fc < pOwner->m_nMaxLights300)
	{
		fDX = pOwner->m_vPos410.x - pLight->m_vPos68.x;
		fDY = pOwner->m_vPos410.y - pLight->m_vPos68.y;
		fDZ = pOwner->m_vPos410.z - pLight->m_vPos68.z;

		fDist = (float)sqrt(fDX*fDX + fDY*fDY + fDZ*fDZ);
		fEdge = fDist - pOwner->m_pModel00->m_fRadiusa8;

		if (fEdge >= fRadius)
			return;
		{
			// The cone test normalises the light-to-model vector by the
			// DISTANCE, not by the edge distance the radius test uses: the
			// shipped code keeps sqrt's result on the x87 stack under the
			// subtraction (`fsubr %st(1),%st ; fcompl ; fstpl 0x10(%esp)`
			// stores the distance) and both the != 0 test and the 1/x read
			// that slot.  With fEdge the dot product came out too large and
			// spot lights reached models outside their cone (at runtime:
			// the snow-covered trees in T01S01's opening lit up white).
			if (fCone > -1.0f && fCone < 1.0f && fDist != 0.0f)
			{
				fInv = 1.0f / fDist;
				fDot = pLight->m_vDir84.x*fDX*fInv + pLight->m_vDir84.y*fDY*fInv + pLight->m_vDir84.z*fDZ*fInv;
				if (fCone <= fDot)
				{
					fT = (1.0f - (0.5f*fDot + 0.5f)) / (1.0f - (0.5f*fCone + 0.5f));

					fR = fR*(1.0f - fT) + fR2*fT;
					fG = fG*(1.0f - fT) + fG2*fT;
					fB = fB*(1.0f - fT) + fB2*fT;
				}
				else
					return;
			}

			pRec = &pOwner->m_Lights3c[pOwner->m_nLights2fc];

			pRec->m_fR20 = fR;
			pRec->m_fG24 = fG;
			pRec->m_fB28 = fB;

			MatVMul19a70(&pRec->m_vPos00, &pData->m_mInv, &pLight->m_vPos68);

			pRec->m_vDir0c = pRec->m_vPos00;
			pRec->m_vDir0c.Norm();

			pRec->m_fRadiusSqr18    = fRadius * fRadius;
			pRec->m_fInvRadiusSqr1c = 1.0f / (fRadius * fRadius);

			pOwner->m_nLights2fc++;
		}
	}
}




// ======================================================================= //
//  0x10019d64 -- 6,861 bytes of body.  __thiscall, no arguments, void.
// ======================================================================= //

void CD3DModelLight::sub_10019d60()
{
	Query10015830		query;              // -0x3f4, ctor 0x10015830
	LTMat				mInv;               // -0x300
	LTMat				mMulTmp;            // -0x240 in the shipped frame
	LTMat				mInvTmp;            // -0x200
	LTMat				mMulTmp2;           // -0x280
	Src19d64Grid*		pGrid;
	Src19d64Light*		pLight;
	GridCell19d64*		pBase;
	GridCell19d64*		pZ;
	LTVector			vNegDir;            // -0x1c0
	LTVector			vGridTmp;           // -0x1b4
	LTVector			vGrid;              // -0x1a8
	LTVector			vBoxMin;            // -0xb0
	LTVector			vBoxMax;            // -0xa4
	int32				nX, nY, nZ;
	float				fC00, fC01, fC02, fC03, fC04, fC05;
	float				fC06, fC07, fC08, fC09, fC10, fC11;
	float				fC12, fC13, fC14, fC15, fC16, fC17;
	float				fC18, fC19, fC20, fC21, fC22, fC23;
	float				fW0, fW1, fW2, fW3, fW4, fW5;
	float				fW6, fW7, fW8, fW9, fW10, fW11;
	float				fInvX, fInvY, fInvZ;
	float				fEps, fMag, fInvMag, fRadSqr;
	int32				nUnused0, nUnused1, nUnused2;
	Callback19a70Data	cbData;
	uint32				i;

	// -- 0x10019d7e .. 0x10019da3 -------------------------------------- //
	m_nLights2fc    = 0;
	m_nMaxLights300 = g_n1008ca3c;
	m_nMaxLights300 = LTMIN(m_nMaxLights300, 16);

	// -- the transform branch, 0x10019da3 .. 0x1001a80b ---------------- //
	if (m_pModel00->m_n13c == 0)
	{
		if (m_pObject04->m_nFlags88 & 0x40)
		{
			// 0x10019dc4: MatMul inlined, then Mat_InverseTransformation
			// inlined, each through its own temp plus a `rep movsl`.
			d3d_MatMul(&mMulTmp, &g_Mat1008b91c, &m_Mat310);
			mInv = mMulTmp;
			d3d_MatInverseTransformation(&mInv, &mInvTmp);
			mInv = mInvTmp;
		}
		else
		{
			// 0x1001a310
			sub_10014ab0(&m_Mat310, &mInv);
		}
	}
	else
	{
		// 0x1001a32b -- three out-of-line operator* calls.  The first two
		// results are INITIALISED (NRVO, no copy); the third is ASSIGNED
		// into mInv, which is where the `rep movsl` at 0x1001a3af comes
		// from.
		LTMat mNode = m_Mat390.sub_10015800(
						*m_pModel00->m_Array70.sub_1001c790(m_pModel00->m_nIndexf4));
		LTMat mWorld = mNode.sub_10015800(m_pModel00->m_Matfc);
		mInv = m_Mat310.sub_10015800(mWorld);

		if (m_pObject04->m_nFlags88 & 0x40)
		{
			// 0x1001a3c2 -- MatMul inlined again, in place.
			d3d_MatMul(&mMulTmp2, &g_Mat1008b91c, &mInv);
			mInv = mMulTmp2;
		}

		// 0x1001a7e8 -- MakeInverseTransform, out of line.
		mInv = mInv.sub_100157e0();
	}

	// -- 0x1001a80d --------------------------------------------------- //
	m_fR65c = m_pObject04->m_bR94;
	m_fG660 = m_pObject04->m_bG95;
	m_fB664 = m_pObject04->m_bB96;
	m_v650  = g_p1008beb0->m_v1c;

	sub_10019a24();

	m_fR65c *= (1.0f / 255.0f);
	m_fG660 *= (1.0f / 255.0f);
	m_fB664 *= (1.0f / 255.0f);

	if (g_n1008cb98)
	{
		m_fR65c *= g_f1008af38;
		m_fG660 *= g_f1008af38;
		m_fB664 *= g_f1008af38;
	}

	// -- 0x1001a8f8 / 0x1001b7fb -------------------------------------- //
	if (m_pObject04->m_nFlags88 & 0x200)
	{
		m_vLightDir674.x = m_vLightDir674.y = m_vLightDir674.z = m_f680 = 0.0f;
		m_vAmbient684.x = 255.0f;
		m_vAmbient684.y = 255.0f;
		m_vAmbient684.z = 255.0f;
		return;
	}

	// -- 0x1001a90a: the global directional light --------------------- //
	// g_p1008cbcc is loaded THREE times in the shipped code -- 0x1001a924,
	// 0x1001aa16 and 0x1001ab02 -- so it is written out three times here
	// rather than cached in a local.
	if (g_p1008beac && g_n1008af54 &&
	    (g_p1008cbcc->m_vColor13c.x != 0.0f || g_p1008cbcc->m_vColor13c.y != 0.0f || g_p1008cbcc->m_vColor13c.z != 0.0f))
	{
		fEps = 0.001f + g_f1008af78;

		if (m_pObject04->m_fCached244 < 0.0f ||
		    ((m_pObject04->m_nFlags8c & 0x20) &&
		     (D3D_ABSDIFF(m_vPos410.x, m_pObject04->m_vCached238.x) > fEps ||
		      D3D_ABSDIFF(m_vPos410.y, m_pObject04->m_vCached238.y) > fEps ||
		      D3D_ABSDIFF(m_vPos410.z, m_pObject04->m_vCached238.z) > fEps)))
		{
			// 0x1001a9ea
			m_pObject04->m_vCached238 = m_vPos410;
			m_pObject04->m_fCached244 = m_f680 = sub_1001bae0();
		}
		else
		{
			// 0x1001a9e2 -- a float lvalue copied with an integer `mov`.
			m_f680 = m_pObject04->m_fCached244;
		}

		// 0x1001aa16 -- MatVMul_3x3 through mInv, then negate.
		m_vLightDir674.x = mInv.m[0][0]*g_p1008cbcc->m_vDir130.x + mInv.m[0][1]*g_p1008cbcc->m_vDir130.y + mInv.m[0][2]*g_p1008cbcc->m_vDir130.z;
		m_vLightDir674.y = mInv.m[1][0]*g_p1008cbcc->m_vDir130.x + mInv.m[1][1]*g_p1008cbcc->m_vDir130.y + mInv.m[1][2]*g_p1008cbcc->m_vDir130.z;
		m_vLightDir674.z = mInv.m[2][0]*g_p1008cbcc->m_vDir130.x + mInv.m[2][1]*g_p1008cbcc->m_vDir130.y + mInv.m[2][2]*g_p1008cbcc->m_vDir130.z;

		vNegDir.x = -m_vLightDir674.x;
		vNegDir.y = -m_vLightDir674.y;
		vNegDir.z = -m_vLightDir674.z;
		m_vLightDir674 = vNegDir;
	}
	else
	{
		// 0x1001aae8
		m_vLightDir674.x = m_vLightDir674.y = m_vLightDir674.z = m_f680 = 0.0f;
	}

	// -- 0x1001ab02 --------------------------------------------------- //
	m_v668 = g_p1008cbcc->m_vColor13c;

	// -- 0x1001ab1a: the light grid ----------------------------------- //
	//
	//  Trilinear sample of an 8-bit-per-channel grid.  The cell is four
	//  bytes and each corner contributes its +2, +1 and +0 bytes in that
	//  order to the three outputs; the +4/+5/+6 reads are the same triple
	//  of the next cell along x, which is why there is no separate x+1
	//  index.  The neighbour strides are m_nStrideY104 and m_nStrideZ11c,
	//  both in CELLS (`lea (%edi,%eax,4)`), and m_nStrideY104 is RELOADED
	//  from the object at every single use -- twelve times -- so it is
	//  written out at every use here rather than cached in a local.
	//
	if (g_p1008beac)
	{
		pGrid = g_p1008beac;

		vGridTmp.x = m_vPos410.x - pGrid->m_vMin138.x;
		vGridTmp.y = m_vPos410.y - pGrid->m_vMin138.y;
		vGridTmp.z = m_vPos410.z - pGrid->m_vMin138.z;
		vGrid = vGridTmp;

		vGrid.x *= pGrid->m_fScale12c;
		vGrid.y *= pGrid->m_fScale130;
		vGrid.z *= pGrid->m_fScale134;

		nX = (int32)vGrid.x;
		nY = (int32)vGrid.y;
		nZ = (int32)vGrid.z;

		nX = LTCLAMP(nX, 0, pGrid->m_nMaxX110);
		nY = LTCLAMP(nY, 0, pGrid->m_nMaxY114);
		nZ = LTCLAMP(nZ, 0, pGrid->m_nMaxZ118);

		vGrid.x -= (float)floor(vGrid.x);
		vGrid.y -= (float)floor(vGrid.y);
		vGrid.z -= (float)floor(vGrid.z);

		fInvX = 1.0f - vGrid.x;
		fInvY = 1.0f - vGrid.y;
		fInvZ = 1.0f - vGrid.z;

		pBase = pGrid->m_pCellsfc + (nX + nY * pGrid->m_nStrideY104
		                                + nZ * pGrid->m_nStrideZ11c);

		fC00 = pBase[pGrid->m_nStrideY104].m_b2;
		fC01 = pBase[pGrid->m_nStrideY104].m_b1;
		fC02 = pBase[pGrid->m_nStrideY104].m_b0;
		fC03 = pBase[pGrid->m_nStrideY104 + 1].m_b2;
		fC04 = pBase[pGrid->m_nStrideY104 + 1].m_b1;
		fC05 = pBase[pGrid->m_nStrideY104 + 1].m_b0;
		fC06 = pBase[0].m_b2;
		fC07 = pBase[0].m_b1;
		fC08 = pBase[0].m_b0;
		fC09 = pBase[1].m_b2;
		fC10 = pBase[1].m_b1;
		fC11 = pBase[1].m_b0;

		pZ = pBase + pGrid->m_nStrideZ11c;

		fC12 = pZ[pGrid->m_nStrideY104].m_b2;
		fC13 = pZ[pGrid->m_nStrideY104].m_b1;
		fC14 = pZ[pGrid->m_nStrideY104].m_b0;
		fC15 = pZ[pGrid->m_nStrideY104 + 1].m_b2;
		fC16 = pZ[pGrid->m_nStrideY104 + 1].m_b1;
		fC17 = pZ[pGrid->m_nStrideY104 + 1].m_b0;
		fC18 = pZ[0].m_b2;
		fC19 = pZ[0].m_b1;
		fC20 = pZ[0].m_b0;
		fC21 = pZ[1].m_b2;
		fC22 = pZ[1].m_b1;
		fC23 = pZ[1].m_b0;

		// z = 0 plane: blend in y, then in x.  Each temporary is stored
		// TWICE in the shipped code, so each is two statements.
		fW0 = fC06 * fInvY;
		fW1 = fC07 * fInvY;
		fW2 = fC08 * fInvY;
		fW0 += fC00 * vGrid.y;
		fW1 += fC01 * vGrid.y;
		fW2 += fC02 * vGrid.y;

		fW3 = fC09 * fInvY;
		fW4 = fC10 * fInvY;
		fW5 = fC11 * fInvY;
		fW3 += fC03 * vGrid.y;
		fW4 += fC04 * vGrid.y;
		fW5 += fC05 * vGrid.y;

		fW6 = fW0 * fInvX;
		fW7 = fW1 * fInvX;
		fW8 = fW2 * fInvX;
		fW6 += fW3 * vGrid.x;
		fW7 += fW4 * vGrid.x;
		fW8 += fW5 * vGrid.x;

		// z = 1 plane: the same six temporaries again.
		fW0 = fC18 * fInvY;
		fW1 = fC19 * fInvY;
		fW2 = fC20 * fInvY;
		fW0 += fC12 * vGrid.y;
		fW1 += fC13 * vGrid.y;
		fW2 += fC14 * vGrid.y;

		fW3 = fC21 * fInvY;
		fW4 = fC22 * fInvY;
		fW5 = fC23 * fInvY;
		fW3 += fC15 * vGrid.y;
		fW4 += fC16 * vGrid.y;
		fW5 += fC17 * vGrid.y;

		fW9  = fW0 * fInvX;
		fW10 = fW1 * fInvX;
		fW11 = fW2 * fInvX;
		fW9  += fW3 * vGrid.x;
		fW10 += fW4 * vGrid.x;
		fW11 += fW5 * vGrid.x;

		// blend in z, truncate to a byte, convert back.
		m_vAmbient684.x = (uint8)(fW6  * fInvZ + fW9  * vGrid.z);
		m_vAmbient684.y = (uint8)(fW7  * fInvZ + fW10 * vGrid.z);
		m_vAmbient684.z = (uint8)(fW8  * fInvZ + fW11 * vGrid.z);
	}
	else
	{
		// 0x1001b7e2
		m_vAmbient684.x = m_vAmbient684.y = m_vAmbient684.z = 0.0f;
	}

	// -- 0x1001b3dd: the dynamic-light pass --------------------------- //
	m_nLights2fc = 0;

	// UNDECODED-PURPOSE (not undecoded CODE): three dwords at -0x100,
	// -0xfc and -0xf8 are zeroed here -- `xor %edx,%edx` shared with the
	// loop counter, stores descending, i.e. one chained assignment -- and
	// are never read again anywhere in the function.  They are emitted
	// because icl does not eliminate dead stores through memory.  What
	// they were called cannot be recovered from the bytes; the store
	// order and width are all that is claimed.
	nUnused0 = nUnused1 = nUnused2 = 0;

	for (i = 0; i < g_n1008bd84; i++)
	{
		pLight = g_p1008bd88[i];

		if (m_nLights2fc < m_nMaxLights300 &&
		    (float)sqrt((m_vPos410.x - pLight->m_vPos18c.x) * (m_vPos410.x - pLight->m_vPos18c.x)
		              + (m_vPos410.y - pLight->m_vPos18c.y) * (m_vPos410.y - pLight->m_vPos18c.y)
		              + (m_vPos410.z - pLight->m_vPos18c.z) * (m_vPos410.z - pLight->m_vPos18c.z))
		        - m_pModel00->m_fRadiusa8 < pLight->m_fRadius1a8)
		{
			// 0x1001b664 -- sunk below the epilogue by icl.
			ModelLight19d64* pRec = &m_Lights3c[m_nLights2fc];

			pRec->m_fR20 = pLight->m_bR94;
			pRec->m_fG24 = pLight->m_bG95;
			pRec->m_fB28 = pLight->m_bB96;

			pRec->m_vPos00.x = mInv.m[0][0]*pLight->m_vPos18c.x + mInv.m[0][1]*pLight->m_vPos18c.y + mInv.m[0][2]*pLight->m_vPos18c.z + mInv.m[0][3];
			pRec->m_vPos00.y = mInv.m[1][0]*pLight->m_vPos18c.x + mInv.m[1][1]*pLight->m_vPos18c.y + mInv.m[1][2]*pLight->m_vPos18c.z + mInv.m[1][3];
			pRec->m_vPos00.z = mInv.m[2][0]*pLight->m_vPos18c.x + mInv.m[2][1]*pLight->m_vPos18c.y + mInv.m[2][2]*pLight->m_vPos18c.z + mInv.m[2][3];

			pRec->m_vDir0c = pRec->m_vPos00;

			// _CVector::Norm(), ltvector.h:194, written out because the
			// shipped code inlines it (the sqrt call is ours, not a
			// helper's).
			fMag = (float)sqrt((pRec->m_vDir0c.x * pRec->m_vDir0c.x)
			                 + (pRec->m_vDir0c.y * pRec->m_vDir0c.y)
			                 + (pRec->m_vDir0c.z * pRec->m_vDir0c.z));
			if (fMag != 0.0f)
			{
				fInvMag = 1.0f / fMag;
				pRec->m_vDir0c.x = pRec->m_vDir0c.x * fInvMag;
				pRec->m_vDir0c.y = pRec->m_vDir0c.y * fInvMag;
				pRec->m_vDir0c.z = pRec->m_vDir0c.z * fInvMag;
			}

			fRadSqr = pLight->m_fRadius1a8 * pLight->m_fRadius1a8;
			pRec->m_fRadiusSqr18    = fRadSqr;
			pRec->m_fInvRadiusSqr1c = 1.0f / fRadSqr;

			m_nLights2fc++;
		}

		if (m_nLights2fc >= m_nMaxLights300)
			break;
	}

	// -- 0x1001b50f: the world query ---------------------------------- //
	if (m_nLights2fc < m_nMaxLights300 && g_p1008beac)
	{
		cbData.m_pOwner = this;
		cbData.m_mInv   = mInv;

		query.m_n04 = 1;

		vBoxMin.x = m_vPos410.x - m_pModel00->m_fRadiusa8;
		vBoxMin.y = m_vPos410.y - m_pModel00->m_fRadiusa8;
		vBoxMin.z = m_vPos410.z - m_pModel00->m_fRadiusa8;
		query.m_vMin08 = vBoxMin;

		vBoxMax.x = m_vPos410.x + m_pModel00->m_fRadiusa8;
		vBoxMax.y = m_vPos410.y + m_pModel00->m_fRadiusa8;
		vBoxMax.z = m_vPos410.z + m_pModel00->m_fRadiusa8;
		query.m_vMax14 = vBoxMax;

		query.m_pfnCallback2c = (void*)sub_10019a70;
		query.m_pUser30       = (void*)&cbData;

		g_p1008beac->m_Query6c.Slot2(&query);
	}
}



// ----------------------------------------------------------------------- //
class CD3DObject1bae4
{
public:
	virtual void    Slot00();
	virtual void    Slot04();
	virtual void    Slot08();
	virtual void    Slot0c();
	virtual void    Slot10();
	virtual void    Slot14();
	virtual void    Slot18();
	virtual void    Slot1c();
	virtual void    Slot20();
	virtual void    Slot24();
	virtual void    Slot28();
	virtual void    Slot2c();
	virtual void    Slot30();
	virtual void    Slot34();
	virtual void    Slot38();
	virtual void    Slot3c();
	virtual void    Slot40();
	virtual float   GetRadius();                        // vtable +0x44
};

// ----------------------------------------------------------------------- //
//  0x1001be00 -- does a ray from pPos along pRay hit SKY?  An
//  IntersectQuery (ltbasedefs.h, constructor inlined) from pPos to
//  pPos + pRay with flags 4 goes to the world intersector at 0x10069a10;
//  the poly it hits is looked up through the world's per-model table at
//  +0x18c (index = hPoly >> 16) and the answer is its surface's flag 0x10.
// ----------------------------------------------------------------------- //
struct Surface1be00
{
	uint8           m_pad_00[0x38];
	uint32          m_nFlags38;                         // +0x38
};

struct Poly1be00
{
	uint8           m_pad_00[0x28];
	Surface1be00*   m_pSurface28;                       // +0x28
};

class PolyTableBase1be00
{
public:
	virtual void        Slot00();
	virtual void        Slot04();
	virtual void        Slot08();
	virtual Poly1be00*  GetPoly(HPOLY hPoly);           // vtable +0x0c
};

class PolyTable1be00 : public PolyTableBase1be00
{
};

struct WorldModel1be00
{
	uint32              m_n00;
	PolyTable1be00*     m_pPolys04;                     // +0x04
};

// 0x10069a10 -- the world intersector (query, info, tree root, 0).
extern "C" LTBOOL sub_10069a10(IntersectQuery* pQuery, IntersectInfo* pInfo,
                               void* pTree, uint32 nUnused);

inline WorldModel1be00* GetWM1be00(Src19d64Grid* pWorld, uint32 i)
{
	switch (i < pWorld->m_nModels190)
	{
		case 0:
			return LTNULL;
	}
	return pWorld->m_pModels18c[i];
}
inline Poly1be00* FindPoly1be00(Src19d64Grid* pWorld, HPOLY hPoly)
{
	WorldModel1be00* pModel = GetWM1be00(pWorld, hPoly >> 16);
	if (!pModel)
		return LTNULL;
	return pModel->m_pPolys04->GetPoly(hPoly);
}

extern "C" int32 sub_1001be00(LTVector* pPos, LTVector* pRay)
{
	IntersectQuery      iq;
	IntersectInfo       ii;
	WorldModel1be00*    pModel;
	Poly1be00*          pPoly;
	uint32              nIndex;

	iq.m_From  = *pPos;
	iq.m_To    = *pPos + *pRay;
	iq.m_Flags = 4;

	if (!sub_10069a10(&iq, &ii, &g_p1008beac->m_Query6c, 0))
		return 0;

	pPoly = FindPoly1be00(g_p1008beac, ii.m_hPoly);
	if (!pPoly)
		return 0;

	if (pPoly->m_pSurface28->m_nFlags38 & 0x10)
		return 1;

	return 0;
}


// ----------------------------------------------------------------------- //
//  0x1001bae4 -- how much of the model sees the sun (0..1).  With no
//  penumbra (g_f1008af78 == 0) it is one sky test from the model's
//  position.  Otherwise the two edges of the model across the sun
//  (position -/+ radius along sunDir x the model's right axis) are tested,
//  and if they disagree the boundary is bisected until the step drops
//  under g_f1008af78.
// ----------------------------------------------------------------------- //
// (g_p1008cbcc's record and g_f1008af78 are declared above.)

float CD3DModelLight::sub_1001bae0()
{
	LTVector    vRay;
	LTVector    vRight;
	LTVector    vPerp;
	LTVector    vHiTest;
	LTVector    vLoTest;
	LTVector    vTest;
	int32       bHi, bLo;
	LTBOOL      bHit;
	float       fStep, fLo, fHi, fMid, fResult;

	vRay = g_p1008cbcc->m_vDir130 * (g_p1008beac->m_fSizeb8 * -2.0f);

	if (g_f1008af78 == 0.0f)
		return sub_1001be00(&m_vPos410, &vRay) ? 1.0f : 0.0f;

	vRight.x = m_Mat310.m[0][0];
	vRight.y = m_Mat310.m[1][0];
	vRight.z = m_Mat310.m[2][0];
	vPerp = g_p1008cbcc->m_vDir130.Cross(vRight);

	vPerp *= ((CD3DObject1bae4*)m_pObject04)->GetRadius();

	vHiTest = m_vPos410 + vPerp;
	bHi = sub_1001be00(&vHiTest, &vRay);

	vLoTest = m_vPos410 - vPerp;
	bLo = sub_1001be00(&vLoTest, &vRay);

	if (bHi == bLo)
		return bHi ? 1.0f : 0.0f;

	fStep   = 1.0f;
	fHi     = 1.0f;
	fLo     = -1.0f;
	fResult = 1.0f;

	while (fStep > g_f1008af78 && bLo != bHi)
	{
		fMid  = (fLo + fHi) * 0.5f;
		vTest = m_vPos410 + vPerp * fMid;
		bHit  = sub_1001be00(&vTest, &vRay);

		fStep *= 0.5f;
		if (!bHit)
			fResult -= fStep;

		if (bHit == bHi)
		{
			fHi = fMid;
			bHi = bHit;
		}
		else
		{
			fLo = fMid;
			bLo = bHit;
		}
	}

	return fResult;
}


// ======================================================================= //
//  0x1001c2c0 -- CMoArray<LTMat, DefaultCache>::InternalNiceSetSize,
//  dynarray.h:727 verbatim.  d3dr2c.cpp's 0x1001c7b0 forwards to it with
//  bFast = 0 and &g_10091df0.  The cache accessors, _AllocateTArray
//  (0x1001c450), GetNumAllocatedElements (0x10021a80) and
//  _DeleteAndDestroyArray (0x1001cf00) are all CALLED here, and memcpy is
//  the CRT's (0x100805d0).
// ======================================================================= //
struct Arr1001c2c0
{
	void*        m_pVTable;                             // +0x00
	LTMat*       m_pArray;                              // +0x04
	uint32       m_nElements;                           // +0x08
	DefaultCache m_Cache;                               // +0x0c

	LTMat*  _AllocateTArray(uint32 nElements, IAllocator* pAlloc);   // 0x1001c450
	void    _DeleteAndDestroyArray(IAllocator* pAlloc, uint32 n);    // 0x1001cf00
	uint32  GetNumAllocatedElements();                               // 0x10021a80

	int32   sub_1001c2c0(uint32 newSize, int32 bFast, IAllocator* pAlloc);
};

int32 Arr1001c2c0::sub_1001c2c0(uint32 newSize, int32 bFast, IAllocator* pAlloc)
{
	LTMat*  pNewArray;
	uint32  i, nToCopy;

	// Trivial reject..
	if (newSize < m_nElements)
	{
		m_Cache.SetCacheSize(m_Cache.GetCacheSize() + (m_nElements - newSize));
		m_nElements = newSize;
		return 1;
	}
	else if (newSize > m_nElements && (m_nElements + m_Cache.GetCacheSize()) >= newSize)
	{
		m_Cache.SetCacheSize(m_Cache.GetCacheSize() - (newSize - m_nElements));
		m_nElements = newSize;
		return 1;
	}
	else if (newSize == m_nElements)
	{
		return 1;
	}

	pNewArray = _AllocateTArray(newSize + m_Cache.GetWantedCache(), pAlloc);
	if (!pNewArray)
		return 0;

	nToCopy = m_nElements;
	if (nToCopy > newSize)
		nToCopy = newSize;

	// Copy as many elements as we can.
	if (bFast)
	{
		memcpy(pNewArray, m_pArray, sizeof(LTMat) * nToCopy);
	}
	else
	{
		for (i = 0; i < nToCopy; i++)
		{
			pNewArray[i] = m_pArray[i];
		}
	}

	// Get rid of the old array and point at the new one.
	_DeleteAndDestroyArray(pAlloc, GetNumAllocatedElements());
	m_pArray = pNewArray;
	m_nElements = newSize;
	m_Cache.SetCacheSize(m_Cache.GetWantedCache());

	return 1;
}



// ======================================================================= //
//  0x1001bf34 -- ninety-six bytes, a triangle count over a model's pieces
//
//      57 56 55 53 56           four saves + the dead slot
//      33 db                    total = 0
//      8b c1 / 33 d2            i = 0
//      8b 08                    pModel = this->m_p000
//      8b 69 3c / 85 ed / 76 3e if (pModel->m_nPieces == 0) skip the loop
//      8b a8 4c 04 00 00        nLOD = this->m_44c        (hoisted)
//   L: 8b 41 38 / 85 ed
//      8b 04 90                 pPiece = pModel->m_pPieces[i]
//      74 1f                    if (nLOD == 0) use pPiece itself
//      8d 75 ff / 8b 78 3c / 3b f7 / 72 04
//      33 c0 / eb 11                pLOD = NULL
//      8d 34 ad fc ff ff ff     (nLOD-1) * 4
//      8d 34 f6                 * 9  ->  (nLOD-1) * 36
//      8b f8 / 8b c6 / 03 47 38     pLOD = pPiece->m_pLODs + that
//      85 c0 / 74 03 / 03 58 18 if (pLOD) total += pLOD->m_nTris
//      42 / 8b 41 3c / 3b d0 / 72 c8
//      8d 04 5b                 return total * 3
//
//  WHAT PINS THE STRUCTURE: the stride.  `lea -0x4(,%ebp,4)` then
//  `lea (%esi,%esi,8)` is *4 then *9 -- a 36-byte element -- and the array
//  it indexes is `pPiece->m_p38`.  The SAME offset 0x18 is then read
//  whether the answer came from that array or from pPiece itself, and the
//  `use pPiece itself` path costs no instructions at all: that is an
//  implicit derived-to-base pointer conversion, so ModelPiece begins with
//  a 36-byte LOD record.  That is what makes the two readings consistent.
//
//  `lea (%ebx,%ebx,2)` on the way out is total*3, i.e. the answer is
//  indices/vertices where the accumulator counted triangles.
//
//  INFERRED, not decoded-with-certainty: every instruction is read, but the
//  source shape that produces this exact register allocation is a
//  reconstruction.  The two decisions to revisit first are (a) whether
//  `pModel` is a real local (it is loaded once into %ecx and kept, while
//  `pModel->m_nPieces` is re-read every iteration, which is what a local
//  plus an un-cached loop bound gives) and (b) whether the inner test is
//  the `if/else` written below -- the NULL arm is emitted FIRST and jumped
//  over, which is icl's layout for `if (a < b) X; else Y;`.
// ======================================================================= //
struct LOD1001bf34
{
	uint8  m_pad_00[0x18];
	uint32 m_nTris;                                     // +0x18
	uint8  m_pad_1c[0x24 - 0x1c];
};                                                      // sizeof == 36

struct LODArr1001bf34                                   // CMoArray<LOD>
{
	LOD1001bf34&	operator[](uint32 i) const { return m_pArray[i]; }
	uint32			GetSize() const { return m_nElements; }

	LOD1001bf34*	m_pArray;                           // +0x00
	uint32			m_nElements;                        // +0x04
};

struct Piece1001bf34 : public LOD1001bf34
{
	LOD1001bf34* GetLOD(uint32 iLOD)
	{
		if (iLOD == 0)
			return this;
		iLOD--;
		if (iLOD < m_LODs.GetSize())
			return &m_LODs[iLOD];
		return LTNULL;
	}

	uint8          m_pad_24[0x38 - 0x24];
	LODArr1001bf34 m_LODs;                              // +0x38
};

struct PieceArr1001bf34                                 // CMoArray<Piece*>
{
	Piece1001bf34*&	operator[](uint32 i) const { return m_pArray[i]; }
	uint32			GetSize() const { return m_nElements; }

	Piece1001bf34**	m_pArray;                           // +0x00
	uint32			m_nElements;                        // +0x04
};

struct Model1001bf34
{
	uint8            m_pad_00[0x38];
	PieceArr1001bf34 m_Pieces;                          // +0x38
};


// 0x1001bf30 / 0x1001bf34 -- moved here from d3dr4b.cpp.  A /Qip
// two-entry pair: 0x1001bf30 is the four-byte stack lead-in
// (`mov 0x4(%esp),%eax`) and the one caller, 0x10019484 in 0x10017c14
// above, calls the register entry 0x1001bf34, so the pair lives in its
// caller's TU.
// IDENTICAL (was 81 bytes off).  The piece's LOD lookup is an inline
// member, LithTech's ModelPiece::GetLOD shape -- LOD 0 is the piece
// itself, then `iLOD--` and a bounds test returning &m_LODs[iLOD] or NULL
// -- called once per piece; that alone gives the shipped `push %edi`, the
// un-threaded NULL re-test and %eax built on both arms.  Measured: with
// `(iLOD - 1) < GetSize()` / `GetSize() > (iLOD - 1)` / `>=` reversed /
// an int32 parameter it is 2 bytes off (`cmp %esi,%edi ; ja` against the
// shipped `cmp %edi,%esi ; jb`); the decrement in place closes it.  Raw
// m_nElements / m_pArray instead of the accessors: 19-49 bytes off.  icl
// also emits out-of-line COMDAT copies of LODArr's GetSize/operator[]
// (unreferenced; the link drops them -- not in d3d.map).
// Earlier notes: the shipped loop re-reads m_pPieces and m_nPieces
// every iteration and walks an INDEX, which the CMoArray-style inline
// accessors give, with m_44c hoisted into the preheader after the
// zero-trip test exactly as shipped.
uint32 CD3DModelDraw::sub_1001bf30()
{
	Model1001bf34* pModel;
	LOD1001bf34*   pLOD;
	uint32         i, nTotal;

	nTotal = 0;
	pModel = (Model1001bf34*)m_pModel00;

	for (i = 0; i < pModel->m_Pieces.GetSize(); ++i)
	{
		pLOD = pModel->m_Pieces[i]->GetLOD(m_n44c);
		if (pLOD)
			nTotal += pLOD->m_nTris;
	}

	return nTotal * 3;
}



// *********************************************************************** //
//
//  COIN-FLIPS -- both spellings given, so one build settles each.
//
//  (1) THE DEAD MATRIX at -0x338, and it is the one worth testing first
//      because it is a whole extra CALL in the sequence.  0x100181ea
//      computes `g_Mat1008ba1c * mView` into -0x338 and nothing in the
//      6,704 bytes reads -0x338 again -- `grep -c '\-0x338(%ebp)'` over
//      the disassembly gives exactly 1, and it is the `lea` that hands
//      the slot to 0x10015800 as its return slot.  Written above as
//
//          matDead = g_Mat1008ba1c.sub_10015800(mView);
//
//      with `matDead` a named local, because icl does no dead-store
//      elimination through memory and an unread named local is the only
//      shape that survives.  If icl drops it anyway, the spelling that
//      cannot be dropped is
//
//          LTMat* pKeep = &matDead;
//          *pKeep = g_Mat1008ba1c.sub_10015800(mView);
//
//      Either way the CALL must appear; if the call-sequence test reports one ours-only
//      or shipped-only 0x10015800 in this region, this is the cause.
//
//  (2) THE TWO FADE PRODUCTS.  0x10017e02 emits `fildl ; fxch ; fsubrs
//      1.0f ; fmulp` -- alpha loaded FIRST, so alpha is the left operand
//      and the file writes `(float)alpha * (1.0f - fT)`.  0x10019607 emits
//      `fdivrp ; fildl ; fmulp` with NO `fxch`, so the value already in
//      %st is the left operand and the file writes `fT * (float)alpha`.
//      The asymmetry is real in the bytes but it is one `fxch`, so the
//      alternative worth one build is making them match:
//
//          m_bAlpha6a8 = (uint8)sub_10015190(
//              (float)m_pObject04->m_bAlpha97 * fT);
//
//      in the reversed arm.  Nothing else changes.
//
//  (3) THE CACHE, REACHED TWO WAYS.  0x1001944f CALLS 0x10015470 (which
//      d3dneutral.cpp pins as the six-byte `return &g_10091d80;`) and
//      0x1001948c materialises the SAME address as an immediate for the
//      second call.  Written above as an accessor call and then the
//      global, which is what reproduces the call at 0x1001944f.  The
//      alternative -- one accessor call kept in a local --
//
//          D3DModelCache* pCache = sub_10015470();
//          if (pCache->sub_1007e3c0(...)) ...
//          else if (pCache->sub_1007e430(...)) ...
//
//      loses nothing in the sequence but would put the pointer in a
//      register for the second call instead of an immediate; if the
//      immediate matters, the file as written is right.
//
//  (4) THE CLAMP MACRO.  Four sites, sixteen .rdata slots, and the shape
//      is fixed: the LO test's taken arm jumps straight to the join and
//      never reaches the HI test, so the HI test is nested inside the LO
//      test's FALSE arm, and the bound is the `fcomp` destination in both,
//      so the bound is the source's left operand.  D3D_CLAMPF above is
//
//          (((lo) > (v)) ? (lo) : (((hi) < (v)) ? (hi) : (v)))
//
//      The other spelling with the same expansion, if the project's real
//      header turns up, is LTCLAMP's usual
//
//          (((v) < (lo)) ? (lo) : (((v) > (hi)) ? (hi) : (v)))
//
//      which differs only in which side of each `fcomp` the bound lands
//      on -- one build tells them apart and neither moves a call.
//
//  (5) THE BONE LOOP FORM.  0x1001845c/0x10018475 initialise BOTH a
//      counter (-0x128) and a 0x40-strided byte offset (-0x124), and the
//      exit test at 0x1001882d compares the COUNTER with the count.  That
//      is icl strength-reducing `pBones[i]`, so the file writes
//
//          for (i = 0; i < m_pModel00->m_nBones78; i++)
//
//      If the shipped `jbe` guard plus `jb` back-edge wants the other
//      form, it is d3dr2a.cpp's:
//
//          pWalk = m_pModel00->m_pBones74;  n = m_pModel00->m_nBones78;
//          while (n--) { d3d_MatMul(&matBone, &mView, pWalk);
//                        *pWalk = matBone; pWalk++; }
//
//      -- but that loses the SECOND load of +0x74 per iteration, which
//      the shipped code definitely makes, so the indexed form is the one
//      the bytes support.
//
//  (6) THE SCALED VECTOR at 0x100191e0.  The three products are emitted
//      as `x*s`, `s*y`, `s*z` -- the first has the field first and the
//      other two have the scalar first.  Written literally above.  If
//      that is scheduling rather than source, the uniform spelling is
//
//          vScaled.x = m_v010.x * m_f01c;
//          vScaled.y = m_v010.y * m_f01c;
//          vScaled.z = m_v010.z * m_f01c;
//
//      and it is the more likely real source (an inlined
//      _CVector::operator*(T)), but the bytes as they stand say otherwise
//      and this file follows the bytes.
//
//  (7) THE PROJECTION MATRIX'S HOME.  0x1001810a/0x1001810e build the
//      sixteen dwords DIRECTLY in the outgoing area with no `rep movsl`
//      in front of them.  d3dr5c.cpp saw the same thing on 0x10027f70 and
//      concluded "the matrix IS a named local whose copy into the
//      argument area icl elided", which is why `matFov` is a named local
//      here.  The alternative, if icl insists on materialising it, is to
//      give 0x1001e140 sixteen float parameters instead of one by-value
//      struct:
//
//          void sub_1001e140(float e11, float e12, ..., float e44);
//
//      `ret $0x40` fits both and the two are indistinguishable in the
//      callee.
//
//  (8) sqrt AND tan.  0x1007fc98 (sqrt) is a known toolchain gap
//      -- icl holds it as an intrinsic and emits `fsqrt`, so the two
//      calls at 0x10017dc3 and 0x100195d0 are expected to come back as
//      shipped-only.  0x1007fdf4 is tan: one double in, one double out,
//      and its two answers are the diagonal of a matrix whose other
//      diagonal entries are 1.0f, which is a half-angle tangent pair.
//      If icl expands tan too, the two calls at 0x100180e5 and 0x100180fb
//      go the same way and the fix is the same as for sqrt -- there
//      isn't one, and `#pragma function` is NOT the fix.
//
//  (9) THE PROLOGUE.  `push %ebx ; mov %esp,%ebx ; sub $8 ; and $-16 ;
//      add $8 ; push %ebp ; push %ebp ; mov 0x4(%ebx),%ebp ; mov %ebp,
//      0x4(%esp) ; mov %esp,%ebp ; sub $0x6d8,%esp` and the matching
//      `mov %ebp,%esp ; pop %ebp ; mov %ebx,%esp ; pop %ebx ; ret $4` is
//      icl REALIGNING THE STACK, and nothing in the source text asks for
//      it.  The dead four-byte lead-in at 0x10017c10 is part of the same
//      idiom (0x10019644 has the identical one at 0x10019640).  It is not
//      reproducible from C and no attempt is made to; it costs bytes, not
//      structure.
//
// *********************************************************************** //

