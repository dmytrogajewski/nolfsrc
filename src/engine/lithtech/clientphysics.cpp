// *********************************************************************** //
//
//  MODULE:   CLIENTPHYSICS (.CPP)
//
//  PURPOSE:  The client's half of ILTPhysics.
//
// *********************************************************************** //

#include "ltbasedefs.h"
#include "ltobject_internal.h"

#include "clientphysics.h"
#include "de_error.h"


// The server tests both arguments and drops a change under 0.001 squared
// rather than send it to every client. The client tests only the object and
// always writes: there is nobody downstream to tell.
LTRESULT CPhysicsLT::SetVelocity(HOBJECT hObj, LTVector* pVel)
{
	if (!hObj)
		RETURN_ERROR(2, "CPhysicsLT::SetVelocity", LT_INVALIDPARAMS);

	((CClientObject*)hObj)->m_Velocity = *pVel;
	((CClientObject*)hObj)->m_ChangeFlags |= CHANGEFLAG_MOTION;
	return LT_OK;
}

LTRESULT CPhysicsLT::SetAcceleration(HOBJECT hObj, LTVector* pAccel)
{
	if (!hObj)
		RETURN_ERROR(2, "CPhysicsLT::SetAcceleration", LT_INVALIDPARAMS);

	((CClientObject*)hObj)->m_Acceleration = *pAccel;
	((CClientObject*)hObj)->m_ChangeFlags |= CHANGEFLAG_MOTION;
	return LT_OK;
}

// The move request is the server's WMMoveState, filled in from the client
// manager instead of the server manager: the container is the client's world
// and the object beside it is the one that world carries at its very end.
//
// The world's address is taken and then tested for null, which it can never
// be - a check the compiler kept rather than the source dropping it.
//
// 0x00404e10, identical. Three things closed it, each measured: the fill is
// an inline helper (commonphysics.cpp's wm_InitMoveState shape, copied here
// because this TU is client-only), not six stores written out; the four
// clearing stores come BEFORE the counter increment in source (VC6 still
// schedules the increment first, but the zero then goes to %esi and the
// handle to %eax as shipped); and the move flags are a local set to 1 and
// raised to 9 under `if`, not a ternary (the `mov $1,%eax ; test %cl,%al ;
// je` at 0x00404edc).
static inline void cp_InitMoveState(WMMoveState* pState, void* pContainer,
                                    void* pUnknown04, LTObject* pObj,
                                    uint32 nPriority)
{
	pState->m_pContainer = (CUnknown210*)pContainer;
	pState->m_pUnknown04 = (CUnknown6A8Obj*)pUnknown04;
	pState->m_pObj       = pObj;
	pState->m_nUnknown0C = nPriority;
	pState->m_pPushList = 0;
	pState->m_nPushCount = 0;
}

// The push variant: same fill, but with the caller's object list and count
// where MoveObject's zeroes them. MovePushObjects and RotatePushObjects
// close only through this helper.
static inline void cp_InitPushState(WMMoveState* pState, void* pContainer,
                                    void* pUnknown04, LTObject* pObj,
                                    uint32 nPriority, HOBJECT* pList,
                                    uint32 nCount)
{
	pState->m_pContainer = (CUnknown210*)pContainer;
	pState->m_pUnknown04 = (CUnknown6A8Obj*)pUnknown04;
	pState->m_pObj       = pObj;
	pState->m_nUnknown0C = nPriority;
	pState->m_pPushList  = (uint32)pList;
	pState->m_nPushCount = nCount;
}

LTRESULT CPhysicsLT::MoveObject(HOBJECT hObj, LTVector* pPos, uint32 flags)
{
	WMMoveState   state;
	CUnknown210*  pWorld;
	uint32        nMoveFlags;

	state.m_pContainer = LTNULL;
	state.m_pUnknown04 = LTNULL;
	state.m_pObj = LTNULL;
	state.m_nUnknown64 = 0;

	g_nMoveObjectCount++;

	if (!hObj)
		RETURN_ERROR(2, "CPhysicsLT::MoveObject", LT_INVALIDPARAMS);

	pWorld = (CUnknown210*)m_pClientMgr->m_World;

	if (!pWorld)
		RETURN_ERROR(3, "CPhysicsLT::MoveObject", LT_NOTINITIALIZED);

	cp_InitMoveState(&state, pWorld, m_pClientMgr->m_pUnknown508, hObj,
	                 hObj->m_BlockingPriority);

	nMoveFlags = 1;
	if (flags & 1)
		nMoveFlags = 9;

	wm_MoveObject(&state, *pPos, nMoveFlags);
	return LT_OK;
}

// Written by copying the server's, and only half renamed: the first check
// still reports itself as SPhysicsLT::SetObjectDims, at the server's debug
// level 2, while the world check underneath it says CPhysicsLT and reports at
// level 1. The refusal for containers and world models shares its return with
// both of them, which is why one LT_INVALIDPARAMS tail serves four paths.
//
// 0x00405000, identical once the six state stores became MoveObject's
// cp_InitMoveState call. Written out as six stores it was a 334-byte near
// (shipped 341) with the flags, dims pointer and state pointer in a
// different three of %eax/%ecx/%edx and the error tails cross-jumped one
// instruction earlier; reordering the stores only moved that residual.
LTRESULT CPhysicsLT::SetObjectDims(HOBJECT hObj, LTVector* pNewDims, uint32 flags)
{
	WMMoveState  state;
	LTVector     vDims;
	CUnknown210* pWorld;

	state.m_pContainer = LTNULL;
	state.m_pUnknown04 = LTNULL;
	state.m_pObj = LTNULL;
	state.m_nUnknown64 = 0;

	if (!hObj || !pNewDims)
		RETURN_ERROR(1, "CPhysicsLT::SetObjectDims", LT_INVALIDPARAMS);

	pWorld = (CUnknown210*)m_pClientMgr->m_World;

	if (!pWorld)
		RETURN_ERROR(1, "CPhysicsLT::SetObjectDims", LT_INVALIDPARAMS);

	vDims = *pNewDims;

	if (hObj->m_ObjectType == OT_CONTAINER || hObj->m_ObjectType == OT_WORLDMODEL)
		return LT_INVALIDPARAMS;

	cp_InitMoveState(&state, pWorld, m_pClientMgr->m_pUnknown508, hObj,
	                 hObj->m_BlockingPriority);

	if (wm_SetObjectDims(&state, &vDims, flags & 1, 1))
		return LT_OK;

	// Refused: hand back the dims the simulation would have accepted.
	*pNewDims = vDims;
	return LT_ERROR;
}


// ======================================================================= //
//  MovePushObjects / RotatePushObjects (moved here from the cltapi.cpp
//  scratch reconstruction).
//
//  The last two slots of ILTClientPhysics.  vtable 0x004a1770 is CPhysicsLT's
//  and the header's declaration order fixes every slot:
//
//    ...  17 GetStairHeight 0x00405490   18 SetStairHeight 0x004054a0
//         19 UpdateMovement 0x00404f30   20 MovePushObjects 0x00405230
//         21 RotatePushObjects 0x00405350
//
//  WHAT BOTH FUNCTIONS SHARE WITH THE THREE ABOVE
//    * `sub $0x68` and a WMMoveState at frame top, zeroed at +0x00/+0x04/
//      +0x08/+0x64 (0x0040523d..0x0040524c, and 0x0040535d..0x0040536c).
//    * `mov 0xc(%ecx),%eax` -> m_pClientMgr, `lea 0x6c(%eax)` -> m_World,
//      tested for null though it never can be (0x0040525f, 0x0040537f).
//    * `mov 0x508(%eax),%eax` -> m_pUnknown508 (0x004052a9, 0x00405402).
//    * `mov 0xbd(%esi/%edx),%al` -> m_BlockingPriority (0x004052b7,0x00405412)
//
//  WHAT IS NEW HERE AND IS THE ONLY REAL FINDING: THE NAME IS A VARIABLE.
//  Both functions load the check-site name out of .data rather than pushing
//  the literal:
//
//    0x0040527d  mov 0x4a7898,%eax     [0x004a7898] == 0x004a787c ==
//                                      "CPhysicsLT::MovePushObjects"
//    0x0040539d  mov 0x4a78bc,%eax     [0x004a78bc] == 0x004a789c ==
//                                      "CPhysicsLT::RotatePushObjects"
//
//  de_error.h already documents that spelling -- "a `char*` variable holding
//  it assembles as `push [imm32]` ... a property of what the caller passes"
//  -- and servercommon.cpp's ServerCommonLT::SetObjectFilenames is the
//  worked precedent: one `static char* pErrFnName` shared by both sites.
//  RotatePushObjects proves the two really are independent, because its
//  THIRD site (0x004053fb) pushes the literal 0x004a789c DIRECTLY.  So that
//  one site is a plain RETURN_ERROR with the string written out and the
//  other two go through the variable.
//
//  Which macro each site uses is read off the code string it pushes:
//    "73"               (0x004a7168) -> RETURN_ERROR, LT_NOTINITIALIZED
//    "60"               (0x004a7160) -> RETURN_ERROR, LT_INVALIDPARAMS
//    "LT_INVALIDPARAMS" (0x004a77c8) -> RETURN_ERROR_SYM
// ======================================================================= //


// 0x00405230, 281 bytes.  Extent verified: 0x00405230..0x00405348 with
// 0x00405349..0x0040534f nops, no e8/e9 in .text lands inside it, and the
// last instruction (`ret $0x10` at 0x00405346) ends on the boundary.  The
// enum-suspect row "start missing at 0x004052a9" is a FALSE POSITIVE: that
// address is the `jne` target of the world check at 0x00405267, i.e. the
// body, not a prologue.
//
// SHAPE NOTES
//   * The two parameter guards are ONE statement: both `je` go forward to
//     0x00405308, which sits PAST the success return at 0x00405305, so the
//     failures are ||-merged and the error block sank.
//   * The world guard is separate and NEGATED -- its block sits inline at
//     0x00405269, ahead of the body -- exactly as MoveObject writes it.
//   * The state fill is cp_InitPushState, an inline helper in declaration
//     order.  The EMITTED order (04,00,08,0c,14,10 at 0x004052b1..0x004052df)
//     is VC6's schedule, not source order: six stores written in exactly
//     that order gave a 280-byte near; the helper is identical.
//   * 0x15 is the flag word at 0x004052af.  MoveObject passes 1 or 9 to the
//     same 0x00451000, so the extra 4 (carry what is standing on it) and
//     0x10 are this call site's.
LTRESULT CPhysicsLT::MovePushObjects(HOBJECT hToMove, LTVector& newPos,
                                     HOBJECT* hPushObjects, uint32 nPushObjects)
{
	WMMoveState  state;
	CUnknown210* pWorld;

	static char* pErrFnName = "CPhysicsLT::MovePushObjects";

	state.m_pContainer = LTNULL;
	state.m_pUnknown04 = LTNULL;
	state.m_pObj = LTNULL;
	state.m_nUnknown64 = 0;

	if (!hToMove || !hPushObjects)
		RETURN_ERROR_SYM(2, pErrFnName, LT_INVALIDPARAMS);

	pWorld = (CUnknown210*)m_pClientMgr->m_World;

	if (!pWorld)
		RETURN_ERROR(3, pErrFnName, LT_NOTINITIALIZED);

	cp_InitPushState(&state, pWorld, m_pClientMgr->m_pUnknown508, hToMove,
	                 hToMove->m_BlockingPriority, hPushObjects, nPushObjects);

	wm_MoveObject(&state, newPos, 0x15);
	return LT_OK;
}


// 0x00405350, 318 bytes.  Extent verified: body 0x00405350..0x0040548b with
// nops to 0x0040548f, next function 0x00405490 (GetStairHeight, four
// instructions).  "start missing at 0x0040544d" is again the shared
// ||-merged error block.
//
// SHAPE NOTES
//   * Same two ||-merged parameter guards (0x0040536d, 0x00405379 -> the
//     block at 0x0040544d which is past the success return at 0x0040544a).
//   * Same negated world guard inline at 0x00405389.
//   * The type test at 0x004053c9 is a THIRD, separate statement, written
//     negated: `cmp $0x2; je body` then `cmp $0x9; je body` then the error
//     falls through.  Two `je` off one compare into the body and the error
//     between them is the shape a `!=  && !=` guard gives, and it is the
//     site that pushes the literal rather than the variable.
//   * The error at 0x004053d9 reports at level 3 and code string "60", so
//     it is RETURN_ERROR (numeric), not RETURN_ERROR_SYM; its tail
//     cross-jumps at 0x00405400 into the other block's format push -- a
//     compiler merge, not a source fact.
//   * 0x00454530 is the client's copy of wm_RotateWorldModel (server
//     0x10020a53, declared in src/engine/server/servermgr.h with exactly
//     this (WMMoveState*, LTRotation*, int) signature); the 1 is at
//     0x00405427.
//   * State fill: the same cp_InitPushState helper.  Emitted 04,00,0c,08,
//     10,14 (0x0040540c..0x00405437); six stores in that order were a
//     324-byte near, the helper is identical.
LTRESULT CPhysicsLT::RotatePushObjects(HOBJECT hToMove, LTRotation& newRot,
                                       HOBJECT* hPushObjects, uint32 nPushObjects)
{
	WMMoveState  state;
	CUnknown210* pWorld;

	static char* pErrFnName = "CPhysicsLT::RotatePushObjects";

	state.m_pContainer = LTNULL;
	state.m_pUnknown04 = LTNULL;
	state.m_pObj = LTNULL;
	state.m_nUnknown64 = 0;

	if (!hToMove || !hPushObjects)
		RETURN_ERROR_SYM(2, pErrFnName, LT_INVALIDPARAMS);

	pWorld = (CUnknown210*)m_pClientMgr->m_World;

	if (!pWorld)
		RETURN_ERROR(3, pErrFnName, LT_NOTINITIALIZED);

	if (hToMove->m_ObjectType != OT_WORLDMODEL &&
	    hToMove->m_ObjectType != OT_CONTAINER)
		RETURN_ERROR(3, "CPhysicsLT::RotatePushObjects", LT_INVALIDPARAMS);

	cp_InitPushState(&state, pWorld, m_pClientMgr->m_pUnknown508, hToMove,
	                 hToMove->m_BlockingPriority, hPushObjects, nPushObjects);

	wm_RotateWorldModel(&state, &newRot, 1);
	return LT_OK;
}
