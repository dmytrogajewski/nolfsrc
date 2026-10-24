// *********************************************************************** //
//
//  MODULE:   W10_47C000 (.CPP)  -- scratch reconstruction
//
//  PURPOSE:  Four slots of the SERVER's object-mover vtable, the one
//            src/engine/server/spritecontrol.cpp already calls CObjectMover.
//            All four are compiled into lithtech.exe as well as server.dll,
//            so this file belongs under src/engine/server/ and must be
//            measured against BOTH images.
//
//  HOW THE CLUSTER WAS IDENTIFIED (vtable, not the string pool):
//
//    lithtech.exe's 0x004a33d4 is a thirteen-slot table whose slots line up
//    one-for-one with server.dll's 0x1004e8c8 -- the table
//    src/engine/server/spritecontrol.cpp reads off .rdata and whose slots 6
//    and 8 that file already reconstructs:
//
//      slot   lithtech    server      what
//      ----   ---------   ----------  -------------------------------------
//        0    0x0047c020  0x1003524a  SetObjectChangeFlags   <- HERE
//        1    0x0047c110  0x10035260  GetCollisionInfo (&m_pCollisionInfo)
//        2    0x0047c120  0x10035269  TouchNotify            <- HERE
//        3    0x0047c1a0  0x100352d5  Attach (sm_CreateInterObjectLink)
//        4    0x0047c1d0  0x100352f5  Detach (sm_BreakAttachments)
//                                     [also ltnew1.cpp CSub47c1d0View]
//        5    0x0047c1f0  0x1003530b  UpdateAttachments      <- HERE
//        6    0x0047c480  0x10035541  ObjectBlocks  [spritecontrol.cpp]
//        7    0x0047c180  0x100352b4  Crush (MID_CRUSH) [also lt2new1.cpp]
//        8    0x0047c4b0  0x10035568  CheckMaxPos   [spritecontrol.cpp]
//        9    0x00490730  0x100355db  return 1
//       10    0x0047c550  0x100355df  CanDeactivate          <- HERE
//       11    0x0047c590  0x100355ec  GetObjectClassName
//       12    0x00473540  0x10032332  GetUnknown928Value [spritecontrol.cpp]
//
//    That mapping is what types all four marked slots at once, and it is
//    checked three ways: slot 5 is the attachment walker spritecontrol.cpp
//    describes ("walks an object's attachments and re-places each child
//    through wm_MoveObject"); slot 0 pushes the string literal
//    "SetObjectChangeFlags" (0x004ad2dc), which is servermodel.h's own check
//    literal; and slots 6 and 8 are byte-for-byte the two bodies
//    spritecontrol.cpp already carries.
//
//    src/engine/server/servermath.cpp says of server 0x1003530b: "the
//    attachment follower ... a translation unit that has no reconstruction
//    yet.  When it gets one, that file's own calls emit these two and this
//    forcing driver should be DELETED."  This IS that reconstruction, so
//    servermath_ForceSDKInlines() can go when this file lands.
//
//  Slot 10 depends on servermgr.h's inline sm_CanDeactivate -- see the
//  note above CanDeactivate at the bottom of this file.
//
//  Every symbol introduced here carries the Q47C tag.
//
// *********************************************************************** //

#include "servermgr.h"          // CServerMgr, WMMoveState, sm_FindObjectByID,
                                // sm_CanDeactivate, LTObject, Attachment
#include "servermodel.h"        // sm_SetObjectChangeFlag (inline)
#include "sharedcommon.h"       // quat_ConvertToMatrix
#include "commonphysics.h"      // wm_MoveObject
#include "de_error.h"
#include "object_lifetime.h"
#include <vector>


// ======================================================================= //
//  SLOT 0 -- 0x0047c020, 229 bytes (0x0047c020..0x0047c105, then eleven
//  alignment nops; the last instruction is the `ret $8` at 0x0047c102, so
//  the extent is exact).  server.dll twin 0x1003524a, 22 bytes.
//
//  WHAT PINS IT.  Both error sites push 0x004ad2dc = "SetObjectChangeFlags"
//  (read at 0x0047c05f and 0x0047c0f3) with "LT_OBJECTNOTINWORLD"
//  (0x004ad2f4) and "LT_ERROR" (0x004a7360) as the code strings and the
//  object's class name (three loads: 0x188 -> 0x24 -> (%eax)) as the
//  description.  That is servermodel.h's sm_SetObjectChangeFlag character
//  for character, and the server twin proves it structurally:
//  0x1003524a is `push [esp+8]; push [esp+8]; push [ecx+4];
//  call 0x1002a5fe; add esp,0xc; ret $8`, and 0x1002a5fe is the standalone
//  sm_SetObjectChangeFlag that servermodel.h already documents.
//
//  WHY IT IS `void`.  lithtech.exe expands the inline (/Ot accepts what /Os
//  declines at all 25 of server.dll's sites) and then sets %eax on NO path:
//  0x0047c0c4 returns with %eax still holding m_pMgr from 0x0047c020, and
//  both error blocks fall into a bare `ret $8`.  An LTRESULT return would
//  have to materialise the code.  The server twin cannot tell you this --
//  it forwards %eax by accident.
// ======================================================================= //
void ServerObjectMover::SetObjectChangeFlags(LTObject* pObj, uint32 nFlags)
{
	sm_SetObjectChangeFlag(m_pMgr, pObj, nFlags);
}


// ======================================================================= //
//  SLOT 1 -- 0x0047c110 / 0x10035260, 9 bytes on both: `mov 0x4(%ecx),%eax;
//  add $0x6ac,%eax; ret` -- the ADDRESS of CServerMgr::m_pCollisionInfo.
//  Below the 8-unmasked-byte floor, so a masked byte match cannot credit it.
// ======================================================================= //
CollisionInfo** ServerObjectMover::GetCollisionInfo()
{
	return &m_pMgr->m_pCollisionInfo;
}


// ======================================================================= //
//  SLOT 2 -- 0x0047c120, 83 bytes (0x0047c120..0x0047c173, `ret $0x10` at
//  0x0047c170, then thirteen alignment nops).  server.dll twin 0x10035269,
//  75 bytes.
//
//  The engine's touch notification: record the collision in the manager's
//  CollisionInfo and hand the object's game-side class MID_TOUCHNOTIFY.
//
//  WHAT PINS IT.
//   * `0x4(%ecx)` then `0x6ac(%eax)` is servermgr.h's
//     CServerMgr::m_pCollisionInfo (0x6ac); slot 1 of this same table
//     (0x0047c110: `mov 0x4(%ecx),%eax; add $0x6ac,%eax; ret`) returns that
//     field's ADDRESS, which is what says the 0x6ac dword is the pointer
//     and not the record.
//   * +0x18 and +0x10 of the record are ltbasedefs.h's CollisionInfo
//     m_vStopVel and m_hObject -- the same two offsets smgr.cpp's
//     si_GetLastCollision reads (m_Plane 0x00, m_hObject 0x10, m_hPoly
//     0x14, m_vStopVel 0x18).
//   * `mov 0x188(%edx),%ecx; mov 0x20(%ecx),%ecx; mov (%ecx),%edx;
//     call *0x4(%edx)` (0x0047c161) is SObjData::m_pBaseClass then slot 1,
//     EngineMessageFn.  The id pushed at 0x0047c15c is 3; slot 7 of this
//     table pushes 6 = MID_CRUSH, so the numbering is ltserverobj.h's.
//   * the null test is the ONLY conditional and it jumps to the shared
//     `ret $0x10`, so the body is one positive guard with no trailing
//     statement.
//   * m_pCollisionInfo is loaded TWICE (0x0047c123 and 0x0047c151) and
//     m_pMgr with it -- the members are written out at each use rather than
//     cached in a local.  A local removes the reload and
//     moves the function.
//   * the vector store adjusts the base (`add $0x18,%eax` then (%eax),
//     0x4(%eax), 0x8(%eax)) rather than indexing the record, which
//     reads as a WHOLE-VECTOR copy -- `= *pStopVel`, not three
//     component assignments.  server.dll spells the same copy `movs` x3.
//   * the float argument reaches the callee as an INTEGER move in
//     lithtech.exe (`mov 0x14(%esp),%esi; push %esi`, 0x0047c13f) and as
//     `fld`/`fstp` in server.dll.  One source, two schedulers.
// ======================================================================= //
void ServerObjectMover::TouchNotify(LTObject* pObj, HOBJECT hToucher,
                                   LTVector* pStopVel, float fForceMag)
{
	if (m_pMgr->m_pCollisionInfo)
	{
		m_pMgr->m_pCollisionInfo->m_vStopVel = *pStopVel;
		m_pMgr->m_pCollisionInfo->m_hObject  = hToucher;

		m_pMgr->m_ClassMgr.message(pObj->m_pSData->m_pBaseClass,
		    MID_TOUCHNOTIFY, hToucher, fForceMag);
	}
}


// ======================================================================= //
//  SLOT 7 -- 0x0047c180 (30 bytes) / 0x100352b4 (33 bytes).  Tell the
//  object's game-side class it is being crushed: EngineMessageFn(MID_CRUSH,
//  pData, 0.0f) through SObjData::m_pBaseClass (0x188 -> 0x20, slot 1),
//  its result returned (the call falls straight into `ret $8`).  `this` is
//  never read.  lithtech.exe pushes the zero as an integer immediate,
//  server.dll as `fldz; fstps (%esp)`.
// ======================================================================= //
uint32 ServerObjectMover::Crush(LTObject* pObj, void* pData)
{
	return m_pMgr->m_ClassMgr.message(pObj->m_pSData->m_pBaseClass,
	    MID_CRUSH, pData, 0.0f);
}


// ======================================================================= //
//  SLOT 3 -- 0x0047c1a0 (39 bytes) / 0x100352d5 (32 bytes).  Hang pObj off
//  pParent: flag the object's change bit 0x40 (m_ChangeFlags, +0x120) and
//  thread a type-1 link through packet.cpp's sm_CreateInterObjectLink
//  (0x0043bdb0 / 0x10013b63) with the PARENT as the owner -- the pushes are
//  (1, pObj, pParent, m_pMgr).  The flag store sits among the argument
//  pushes on both images, so it is the statement before the call.
// ======================================================================= //
void ServerObjectMover::Attach(LTObject* pObj, LTObject* pParent)
{
	pObj->m_ChangeFlags |= 0x40;
	sm_CreateInterObjectLink(m_pMgr, pParent, pObj, 1);
}


// ======================================================================= //
//  SLOT 4 -- 0x0047c1d0 (21 bytes) / 0x100352f5 (22 bytes).  Break the
//  object's type-1 links: packet.cpp's sm_BreakAttachments (0x0043bcc0 /
//  0x10013a87) with (m_pMgr, pObj, 1, 0).  `ret $4`, nothing returned.
//  lithtech.exe's copy was first written as ltnew1.cpp's CSub47c1d0View.
// ======================================================================= //
void ServerObjectMover::Detach(LTObject* pObj)
{
	sm_BreakAttachments(m_pMgr, pObj, 1, 0);
}


// ======================================================================= //
//  SLOT 5 -- 0x0047c1f0, 518 bytes (0x0047c1f0..0x0047c3f6, `ret $4` at
//  0x0047c3f3, then ten alignment nops).  server.dll twin 0x1003530b, 349.
//
//  Re-place every attachment of the object that just moved: rotate the
//  attachment's stored offset by the parent's rotation, move the child
//  there through the world simulation, and give the child the parent's
//  rotation composed with the attachment's.
//
//  EXTENT.  Every branch is internal (0x0047c3e9 and 0x0047c3de are the
//  loop tail and the skip, 0x0047c22b the back edge).  Sweeping .text for
//  e8/e9 displacements landing inside the range finds none, so the row is
//  one function.
//
//  WHAT PINS IT.
//   * ltobject_internal.h gives every offset: m_pAttachments 0x098,
//     m_Rotation 0x09c, m_BlockingPriority 0x0bd, m_pSData 0x188, m_Pos
//     0x18c; Attachment is m_Offset (LTransform: m_Pos 0x00, m_Rot 0x0c),
//     m_nChildID 0x1e, m_pNext 0x24, all read at exactly those offsets.
//   * the four dword stores before the loop are servermgr.h's WMMoveState
//     prologue: +0x00, +0x04, +0x08 and +0x64 zeroed, the idiom
//     serverengine.cpp's se_InitWorldModel and clientphysics.cpp's
//     CPhysicsLT::MoveObject already carry verbatim.  The local's base is
//     E-0xa8 and its 0x64 field lands at E-0x44, which is the 0x68-byte
//     WMMoveState exactly.
//   * 0x0046a380 is smgr.cpp's sm_FindObjectByID (server 0x1002acd3): the
//     argument is a WORD load (`mov 0x1e(%esi),%cx`) pushed as a dword,
//     which is how VC6 passes a uint16 parameter.
//   * 0x00444880 is quat_ConvertToMatrix (server 0x10015bcd), named by
//     sharedcommon.h and modelnew.cpp; 0x0047c400 is
//     LTMatrix::Apply3x3(const LTVector&, LTVector&) (server 0x100354f0,
//     same 81 bytes), and 0x0047c460 is the four-dword LTRotation copy
//     (lt2new1.cpp).
//   * THE DEAD COPY IS THE PROOF OF WHICH Apply3x3.  0x0047c28f copies the
//     out-parameter back over the vector that was just passed IN and then
//     never reads it.  ltmatrix.h's one-argument overload is literally
//     `LTVector temp; Apply3x3(vec, temp); vec = temp;` -- so the source
//     calls `mRot.Apply3x3(vOffset)`, and the copy is the inline's last
//     line rather than anything the caller wrote.
//   * THE TWO QUATERNION TEMPORARIES ARE THE PROOF OF operator*.
//     lithtech.exe expands quat_Mul in place (0x0047c324..0x0047c3ad,
//     ltquatbase.h's four rows, which servermath.cpp has already shown
//     compile byte-identical) into E-0xb8, then CALLS the four-dword copy
//     at 0x0047c460 into a SECOND sixteen-byte slot at E-0xc8, and only
//     then stores that into the child at +0x9c.  ltrotation.h's
//     `LTRotation operator*` is `LTRotation qTemp; quat_Mul(qTemp.m_Quat,
//     m_Quat, other.m_Quat); return qTemp;` -- qTemp is E-0xb8, the return
//     value is E-0xc8, and the copy between them is the return, with the
//     final assignment into the object expanded at its own call site
//     (the decision is per call site).  server.dll, which
//     calls quat_Mul out of line, needs only one slot and collapses the
//     pair.  Writing this as a bare `quat_Mul(&rNew, ...)` gives ONE
//     temporary and no call, which is the shape server.dll has and
//     lithtech.exe does not.
//   * the left operand of the product is the PARENT's rotation: the
//     expansion multiplies 0x0c/0x10/0x14/0x18 of %esi (the attachment)
//     against (%eax)..0xc(%eax) where %eax is pObj+0x9c, and ltquatbase.h's
//     first term is a[QW]*b[QX] -- `fld 0xc(%eax); fmul 0xc(%esi)`.
//   * 0x00451000 is wm_MoveObject (server 0x1001eb2d), which takes the
//     position BY VALUE: `sub $0xc,%esp` then three stores through %esp
//     ahead of the flags.  The flag word is 0xd = 1|4|8, which
//     commonphysics.h reads as drop-what-it-stood-on, carry-what-stands-on-
//     it, use-the-caller's-position.
//   * pState->m_pObj is re-loaded at 0x0047c201, 0x0047c261, 0x0047c29f and
//     0x0047c319 -- once per statement that mentions it, so it is written
//     out and not cached.
//
//  FRAME (lithtech.exe, `sub $0xf0,%esp` plus four pushes; E is the
//  return-address slot).  The declaration order below is read off it, the
//  first-declared aggregate taking the highest address the way
//  se_InitWorldModel's WMMoveState does at 0x0046b330:
//
//      E-0x40  mRot     LTMatrix     (64)
//      E-0xa8  state    WMMoveState  (104)
//      E-0xb8  operator*'s qTemp                 (compiler)
//      E-0xc8  operator*'s return value          (compiler)
//      E-0xcc  `this` spill                      (compiler)
//      E-0xd8  operator+'s result                (compiler)
//      E-0xe4  vOffset  LTVector     (12)
//      E-0xf0  Apply3x3's temp                   (compiler)
//
//  server.dll's frame carries the same six objects in the OPPOSITE order
//  (mRot lowest at ebp-0xe4, vOffset highest at ebp-0x3c), so the two
//  images disagree about aggregate placement and only one of them can be
//  satisfied by a declaration order.  lithtech.exe is the ground truth
//  here; if the server twin comes out near and the client exact, that is
//  why, and swapping the three declarations is the only lever.
//
//  SERVER TWIN: byte-identical (349 bytes, 113 instructions).  It was
//  357/117; two edits got it to size-exact, both of them about
//  _CVector's operator+, which ltvector.h declares
//  `_CVector<T> operator+ (const _CVector<T> v) const` -- the argument is
//  BY VALUE, not by reference, so whichever operand is written on the RIGHT
//  is copied into a twelve-byte temporary before the three fadds:
//
//    * the sum is `pState->m_pObj->m_Pos + vOffset`, not
//      `vOffset + pState->m_pObj->m_Pos`.  Written the second way, m_Pos is
//      the by-value argument and VC6 copies it (`lea -0x2c(%ebp),%edi` and
//      three `movsl`) and then adds out of the copy.  Written the first
//      way, m_Pos is `this` and is added in place -- `fadds (%eax)`,
//      `fadds 0x4(%eax)`, `fadds 0x8(%eax)` at 0x100353b5/bd/cd, off the
//      `mov 0x8(%ebx),%eax / add $0x18c,%eax` two instructions earlier --
//      while the by-value copy of vOffset costs nothing because VC6 serves
//      it out of Apply3x3's own output temp at -0x14, which it has just
//      copied into vOffset.  That is the whole eight-byte and four-
//      instruction gap.
//    * the sum goes into a NAMED local, `vNew`, rather than being written
//      inline as wm_MoveObject's second argument.  It costs no frame (the
//      compiler temp it replaces was the same twelve bytes at -0x20) and it
//      is what puts the three fadds AHEAD of the `state.*` stores, the way
//      the shipped code has them -- inline, VC6 emits all six stores first
//      and the arithmetic after, which is eight residual blocks instead of
//      six.
//
//  THE LAST SIX BLOCKS WERE AN INLINE HELPER.  The shipped build loads
//  pState->m_pContainer into %edx and the child's blocking priority into
//  %ecx (0x100353c0, 0x100353d3) above the last two fstps, and only then
//  stores obj, container, unknown04, priority (0x100353ed..0x10035409).
//  Loads that are all live at once before any of the stores are ARGUMENTS:
//  the six fields are filled by an inline function taking the four values,
//  the same six-field fill commonphysics.cpp already has as wm_InitMoveState
//  (and lithtech.exe says the same thing: 0x0047c2b3/0x0047c2b5 load both
//  pointers before the first fadds, and the priority goes through a
//  `xor eax / mov al` widening to the uint32 parameter).  Written as six
//  assignments, no ordering of them is identical -- all 720 were measured,
//  best 4 blocks.  commonphysics.cpp's helper is file-static, so this file
//  carries its own copy under the same body (om_InitMoveState, name ours).
// ======================================================================= //
// The six-field move-request fill, as commonphysics.cpp's wm_InitMoveState
// (see the note above UpdateAttachments for why it has to be a function).
static inline void om_InitMoveState(WMMoveState* pState, void* pContainer,
                                    void* pUnknown04, LTObject* pObj,
                                    uint32 nPriority)
{
	pState->m_pContainer = (CUnknown210*)pContainer;
	pState->m_pUnknown04 = static_cast<IObjectMover*>(pUnknown04);
	pState->m_pObj       = pObj;
	pState->m_nUnknown0C = nPriority;
	pState->m_pPushList  = 0;
	pState->m_nPushCount = 0;
}

void ServerObjectMover::UpdateAttachments(WMMoveState* request)
{
    auto* manager = m_pMgr;
    auto* parent = request->m_pObj;
    ServerCallbackGuard server(manager->m_Lifetime);
    ObjectCallbackGuard parent_guard(*parent);
    struct Entry {
        Attachment* record;
        uint64 generation;
        LTObject* child;
        ObjectCallbackGuard guard;
        LTransform offset;
    };
    std::vector<Entry> entries;
    for (auto* attachment = parent->m_pAttachments; attachment; attachment = attachment->m_pNext)
        entries.push_back({attachment,attachment->m_Generation,attachment->m_pChild,ObjectCallbackGuard(*attachment->m_pChild),attachment->m_Offset});
    for (const auto& entry : entries) {
        if (!server.unchanged() || !parent_guard.unchanged()) return;
        if (!entry.guard.unchanged()) continue;
        auto* record = am_FindAttachment(&manager->m_Unknown390,reinterpret_cast<HATTACHMENT>(entry.record));
        if (!record || record->m_Generation != entry.generation) continue;
        LTMatrix rotation; quat_ConvertToMatrix(&parent->m_Rotation,&rotation);
        auto offset = entry.offset.m_Pos; rotation.Apply3x3(offset);
        const auto orientation = parent->m_Rotation * entry.offset.m_Rot;
        WMMoveState state;
        state.Init(request->m_pContainer,request->m_pUnknown04,entry.child,entry.child->m_BlockingPriority);
        wm_MoveObject(&state,parent->m_Pos + offset,0xd);
        if (!server.unchanged() || !parent_guard.unchanged()) return;
        if (!entry.guard.unchanged()) continue;
        record = am_FindAttachment(&manager->m_Unknown390,reinterpret_cast<HATTACHMENT>(entry.record));
        if (record && record->m_Generation == entry.generation) entry.child->m_Rotation = orientation;
    }
}


// ======================================================================= //
//  SLOT 10 -- 0x0047c550, 50 bytes (0x0047c550..0x0047c582, `ret $4` at
//  0x0047c57f, then fourteen alignment nops).  server.dll twin 0x100355df,
//  13 bytes.
//
//  WHAT PINS IT.  The server twin is `push [esp+4]; call 0x1002a239;
//  pop ecx; ret $4`, and 0x1002a239 is smgr.cpp's sm_CanDeactivate -- which
//  smgr.cpp already reconstructs, guard for guard.  lithtech.exe's 50 bytes
//  are that body EXPANDED: `0x188 -> 0x2c` test, then `test $0x21213001` /
//  `test $0x10000000` on m_Flags, with the two `return LTTRUE` sites folded
//  onto one `mov $0x1,%eax` at 0x0047c575, which is the nesting smgr.cpp
//  already writes.
//
//  CLOSED BY servermgr.h: sm_CanDeactivate is now `inline` there, with a
//  `pSData` local that is what tips /Os into declining the expansion (see
//  the note above it).  Byte-identical on both images: 13 bytes and a call
//  in server.dll, the 50-byte expansion in lithtech.exe.
// ======================================================================= //
// SLOT 9 -- 0x100355db (lithtech.exe 0x00490730), four bytes: `push 1 ; pop eax ; ret`, directly
// ahead of slot 10 in .text. Read by CWMMoveSetup::Setup (0x1001efd8, commonphysics.h) into
// the move record's +0x18. lthread.h's LCriticalSection::IsValid claims the
// same address (the same body; /OPT:ICF).
LTBOOL ServerObjectMover::CanPlace(LTObject* pObj)
{
	return !pObj->m_pAttachments && pObj->m_StandingOnMe.IsTiedOff() && sm_CanDeactivate(pObj);
}


// ======================================================================= //
//  SLOT 11 -- 0x0047c590 (18 bytes) / 0x100355ec (18 bytes).  The object's
//  class name: m_pSData (0x188) -> m_pClass (0x24) -> the name (+0x00).
// ======================================================================= //
const char* ServerObjectMover::GetObjectClassName(LTObject* pObj)
{
	return pObj->m_pSData->m_pClass->m_ClassName;
}
