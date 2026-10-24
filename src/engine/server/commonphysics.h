// *********************************************************************** //
//  MODULE:   COMMONPHYSICS (.H)
//  PURPOSE:  The shared half of ILTPhysics — the eleven methods the SDK header
//            declares non-pure, so the engine implements them once for both
//            client and server. In server.dll they sit contiguously at
//            0x1001260b-0x100129d9.
//
//            The shipped parameter-check messages name this class three
//            different ways (ILTPhysics::, ILTCommon::, CommonLT::) and get
//            two of the method names wrong outright — SetObjectMass reports
//            "GetObjectMass", GetVelocity reports "CommonLT::GetVelocity".
//            Those literals are reproduced exactly as shipped: they are what
//            the bytes contain, and correcting them would break the match.
// *********************************************************************** //
#ifndef __COMMONPHYSICS_H__
#define __COMMONPHYSICS_H__

#include "ltobject_internal.h"
#include "iltphysics.h"
#include "world_move.h"

// The global-force state embedded in the world at +0x668. Set (0x1002c8aa)
// updates it from a vector; the current value sits at +0x14 (world +0x67c).
// The world's global force, kept three ways: the force itself, the direction
// it points in, and how strong it is — so the simulation never has to
// normalize per object. A force too small to have a direction leaves the
// direction at nothing rather than dividing by it (names ours).
//
// The first 0x14 bytes are not padding: they are the per-object request the
// frame-move worker (0x1001e088) is driven by, and the vector at +0x34 is
// what it answers with. Every field is read off two functions — the caller
// sm_GetFrameMove (0x1002a6de) filling it and the worker consuming it:
//
//   +0x00  the object being moved. The caller stores it (`mov %eax,(%ebx)`)
//          and the worker asks it what it is standing on (`0xe8(%esi)`).
//   +0x04  the frame time in seconds. The caller copies its third argument
//          into it; the worker's first act is `flds 0x4(%ebx)`, squaring it
//          and halving it for the `at²/2` term.
//   +0x08  the object's flag word: the caller stores `0x88(%eax)` (through a
//          local on one of its two paths) and the worker tests
//          `testb $0x2,0xa(%ebx)` — bit 17 of the dword, FLAG_GRAVITY.
//   +0x0c  the velocity, and +0x10 the acceleration, as POINTERS, not
//          copies: the caller passes `&pObj->m_Velocity` / `&m_Acceleration`
//          on its fast path and the addresses of two locals on the path that
//          has to adjust them first, and the worker calls MagSqr on each
//          through `ecx` and writes zeros back through them.
//   +0x34  the offset the frame produced. The worker clears it up front and
//          the caller copies it out to its fourth argument with three
//          `movsl`.
//
// So the record is 0x40 bytes, and the four force fields in the middle are
// the only part of it that outlives one call.
class GlobalForceControl
{
public:
	GlobalForceControl();                    // 0x1002c87b

	void Set(LTVector* pVec);                // 0x1002c8aa

	class LTObject* m_pObj;                  // +0x00
	float           m_fFrameTime;            // +0x04
	uint32          m_nObjFlags;             // +0x08
	LTVector*       m_pVelocity;             // +0x0c
	LTVector*       m_pAcceleration;         // +0x10
	LTVector m_Force;                        // +0x14
	LTVector m_Dir;                          // +0x20
	float    m_fMag;                         // +0x2c
	float    m_Unknown30;                    // +0x30
	LTVector m_vFrameMove;                   // +0x34
};

// 0x1001e088 — the worker the record above is filled for: it integrates the
// velocity and acceleration it was pointed at over m_fFrameTime and leaves
// the answer in m_vFrameMove. 1615 bytes and not reconstructed; the two CSV
// rows at 0x1001e127 and 0x1001e649 are false starts inside it (name ours).
//
// Extent re-confirmed from the bytes: `push ebp / mov esp,ebp / sub $0x58`
// at 0x1001e088 and the matching `leave / ret` at 0x1001e6d5, so the body is
// 0x1001e088..0x1001e6d6 and 0x1001e6d7 is the next function (a 25-byte
// three-dword copy). 159 + 1314 + 142 = 1615, which is the three enumeration
// rows tiling it exactly. A full .text sweep of every e8/e9/0f8x
// displacement finds the head reached from OUTSIDE twice, both from
// sm_GetFrameMove — 0x1002a7a1 and 0x1002a7de — and finds nothing at all
// reaching the other two rows from outside. So the head row is the real
// function with a truncated size, not a fragment: it cannot be excluded,
// only written.
//
// 629 instructions, 23 calls to 8 targets, and almost entirely x87: the
// scheduling is the whole difficulty, and it spills floats into the
// parameter slot at 0x8(%ebp) once the argument is dead, which is
// not source-reachable. The realistic ceiling is a near (only
// identical bytes count), which is why it has stayed unwritten.
int phys_ComputeFrameMove(GlobalForceControl* pControl);

// Move an object through the server world.
void sm_MoveObject(class CServerMgr*, HOBJECT, LTVector*, uint32 flags);

// 0x1001eb2d — the simulation's own move, which takes the position BY VALUE
// (twelve bytes onto the stack ahead of the flags). Name ours.
//
// void, not the `int` this was first declared as: eax is never set on any
// path. The flags word is a bit set, and every bit is read off a call site
// or a test in the body:
//
//   1 = drop whatever the object was standing on before moving it
//   2 = two notifications through the 0x6a8 object's slot 0 (2 on the way
//       in, then 2 and 0x200 on the way out)
//   4 = carry everything standing on this object with it
//   8 = move to the position the caller passed rather than the object's own
//
// The 7 that sm_MoveObject and wm_SetObjectDims pass is therefore the first
// three, and the recursion passes `(flags & 8) | 7`.
void wm_MoveObject(struct WMMoveState* pState, LTVector vPos, uint32 flags);

// 0x1001e85d — put an object onto another one, or onto nothing. ltobject.cpp
// reconstructs it (byte-identical) beside the two unlinkers servermgr.h
// declares as obj_UnlinkTrackersA (0x1001e7ef) and obj_UnlinkTrackersB
// (0x1001e828), which are what wm_MoveObject calls; it had no declaration of
// its own until this one, and servermgr.h is not ours to add it to (name
// ours, from ltobject.cpp).
void obj_SetStandingOn(class LTObject* pObj, class LTObject* pOn,
                       WorldPoly* pPoly);

// The two box tests wm_MoveObject shares with the resize walk (names ours);
// see commonphysics.cpp for what each reads off the shipped bytes.
int wm_BoxesOverlap(LTVector* pMin1, LTVector* pMax1,
                    LTVector* pMin2, LTVector* pMax2, float fTolerance);
int wm_ObjectsIntersect(class LTObject* pObj1, class LTObject* pObj2,
                        LTVector* pMin1, LTVector* pMax1,
                        LTVector* pMin2, LTVector* pMax2,
                        float fTolerance, int* pbUsedGeometry);
void wm_PushIntersectingObjects(struct WMMoveState* pState, LTVector* pOrigPos,
                                LTVector* pNewPos);

// How much of a move is small enough to be no move at all. The squared
// length is compared against this as a FLOAT (`fcomps`, so the literal in
// the source carries an `f`); the per-component clamp inside the body
// compares a `fabs` result against the DOUBLE 0.001 (`fcompl`). Both
// constants exist only in the shipped bytes.
#define WM_MOVE_EPSILON_SQR 0.0001f

// names ours: resize a world object through the simulation (0x10020611;
// nonzero = the dims were applied) and the "DebugMaxDims" console gate.
int wm_SetObjectDims(struct WMMoveState* pState, LTVector* pDims, int bFlag, int bUnknown);
extern float g_DebugMaxDims;                 // 0x10051d28

// 0x1002082b — try to grow the object along one axis (0 = x, 1 = y, 2 = z),
// given the address of the wanted component; it writes the component back with
// whatever it managed. Name ours.
void wm_ResizeAxis(struct WMMoveState* pState, int nAxis, float* pfWanted);

// 0x1001eb0c — retransform the object if it is a world model and the flag is
// set. The type test is against 2 and 9, i.e. OT_WORLDMODEL and OT_CONTAINER
// (name ours).
void obj_MaybeRetransformWorldModel(class LTObject* pObj, int bFlag);

// Both SDK physics interfaces use the same native object properties. The
// client specialization adds its movement slots through ILTClientPhysics;
// object ownership checks remain the responsibility of its scene adapter.
template<class Interface>
class PhysicsProperties : public Interface
{
public:
	LTRESULT GetFrictionCoefficient(HOBJECT hObj, float& coeff);
	LTRESULT SetFrictionCoefficient(HOBJECT hObj, float coeff);

	LTRESULT GetForceIgnoreLimit(HOBJECT hObj, float& limit);
	LTRESULT SetForceIgnoreLimit(HOBJECT hObj, float limit);

	LTRESULT GetVelocity(HOBJECT hObj, LTVector* pVel);
	LTRESULT GetAcceleration(HOBJECT hObj, LTVector* pAccel);

	LTRESULT GetObjectMass(HOBJECT hObj, float& mass);
	LTRESULT SetObjectMass(HOBJECT hObj, float mass);

	LTRESULT GetStandingOn(HOBJECT hObj, CollisionInfo* pInfo);
	LTRESULT GetObjectDims(HOBJECT hObj, LTVector* pDims);
	LTRESULT IsWorldObject(HOBJECT hObj);
protected:
    virtual bool ValidObject(HOBJECT object) const { return object != nullptr; }
};
using CommonPhysicsLT = PhysicsProperties<ILTPhysics>;


// The server's half of ILTPhysics (0x1002d472-0x1002d5a8, 0x1002f0e1-0x1002f172).
class SPhysicsLT : public CommonPhysicsLT
{
public:
	// Defined `inline` in commonphysics.cpp, the one TU that constructs one:
	// CLTServer's constructor expands it (0x1002d3d7..0x1002d3e7).
	SPhysicsLT(class CServerMgr* pServerMgr);

	LTRESULT SetVelocity(HOBJECT hObj, LTVector* pVel);
	LTRESULT SetAcceleration(HOBJECT hObj, LTVector* pAccel);
	LTRESULT SetObjectDims(HOBJECT hObj, LTVector* pNewDims, uint32 flags);  // 0x1002d5a9, not yet reconstructed
	LTRESULT MoveObject(HOBJECT hObj, LTVector* pPos, uint32 flags);

	// iltphysics.h's own tail, in its declaration order — named from the
	// class's vtable at 0x1004e560+. References,
	// so no null checks are possible, and none are compiled.
	LTRESULT GetGlobalForce(LTVector& vec);
	LTRESULT SetGlobalForce(LTVector& vec);
	LTRESULT GetStairHeight(float& fHeight);
	LTRESULT SetStairHeight(float fHeight);

public:
	float      m_StairHeight;                // 0x08 (m_ClientServerType fills 0x04)
	CServerMgr* m_pWorld;                     // 0x0c
};

#endif
