#include "model_callback.h"
#include "object_lifetime.h"
// server.dll — the server's ILTModel setter overrides (reconstruction). Build /O1.
#include "servermodel.h"
#include "servermgr.h"
#include "de_error.h"
#ifdef NOLFSRC_MODERN
#include "modern/engine/p1_frametime.h"   // M-P1-23
#endif

// sm_SetObjectChangeFlag now lives in servermodel.h.  It is not just this
// TU's helper: the shipped /O2 build expands it into CLTServer::
// SetModelAnimation / SetModelLooping / ResetModelAnimation (servermessage.cpp,
// 0x0046e400/0x0046e540/0x0046e680) and into ServerCommonLT::SetObjectFlags
// (servercommon.cpp, 0x0046c190), so the definition has to be visible to
// those TUs as well.  It stays above every caller here for the same reason it
// always did -- VC6's inliner is forward-only.

LTRESULT ServerModelLT::SetLooping(LTAnimTracker* pTracker, LTBOOL bLooping)
{
	// The instance is read before the checks, not after: the shipped code
	// loads it into a callee-saved register in the prologue.
	LTObject* pObj = pTracker ? pTracker->GetModelInstance() : nullptr;
	if (!pTracker || !pObj)
		RETURN_ERROR_NAMED(2, "ServerModelLT::SetLooping", LT_INVALIDPARAMS);

	LTRESULT dResult = CommonModelLT::SetLooping(pTracker, bLooping);
	if (dResult == LT_OK)
		sm_SetObjectChangeFlag(m_pServerMgr, pObj, CF_MODELINFO);

	return dResult;
}

LTRESULT ServerModelLT::SetPlaying(LTAnimTracker* pTracker, LTBOOL bPlaying)
{
	LTObject* pObj = pTracker ? pTracker->GetModelInstance() : nullptr;
	if (!pTracker || !pObj)
		RETURN_ERROR_NAMED(2, "ServerModelLT::SetPlaying", LT_INVALIDPARAMS);

	LTRESULT dResult = CommonModelLT::SetPlaying(pTracker, bPlaying);
	if (dResult == LT_OK)
		sm_SetObjectChangeFlag(m_pServerMgr, pObj, CF_MODELINFO);

	return dResult;
}

LTRESULT ServerModelLT::SetCurAnimTime(LTAnimTracker* pTracker, uint32 curTime)
{
	LTObject* pObj = pTracker ? pTracker->GetModelInstance() : nullptr;
	if (!pTracker || !pObj)
		RETURN_ERROR_NAMED(2, "ServerModelLT::SetCurAnimTime", LT_INVALIDPARAMS);

	LTRESULT dResult = CommonModelLT::SetCurAnimTime(pTracker, curTime);
	if (dResult == LT_OK)
	{
        ModelCallbackGuard guard(*pTracker);
		at_UpdateStringKeys(pTracker, pTracker->m_TimeRef.m_Cur.m_Time);
        if (!guard.unchanged()) return dResult;
		sm_SetObjectChangeFlag(m_pServerMgr, pObj, CF_MODELINFO);
	}

	return dResult;
}

LTRESULT ServerModelLT::SetCurAnim(LTAnimTracker* pTracker, HMODELANIM hAnim)
{
	LTObject* pObj = pTracker ? pTracker->GetModelInstance() : nullptr;
	if (!pTracker || !pObj)
		RETURN_ERROR_NAMED(2, "ServerModelLT::SetCurAnim", LT_INVALIDPARAMS);

	LTRESULT dResult = CommonModelLT::SetCurAnim(pTracker, hAnim);
	if (dResult == LT_OK)
		sm_SetObjectChangeFlag(m_pServerMgr, pObj, CF_MODELINFO);

	return dResult;
}

LTRESULT ServerModelLT::ResetAnim(LTAnimTracker* pTracker)
{
	LTObject* pObj = pTracker ? pTracker->GetModelInstance() : nullptr;
	if (!pTracker || !pObj)
		RETURN_ERROR_NAMED(2, "ServerModelLT::ResetAnim", LT_INVALIDPARAMS);

	LTRESULT dResult = CommonModelLT::ResetAnim(pTracker);
	if (dResult == LT_OK)
		sm_SetObjectChangeFlag(m_pServerMgr, pObj, CF_MODELINFO);

	return dResult;
}

LTRESULT ServerModelLT::SetWeightSet(LTAnimTracker* pTracker, HMODELWEIGHTSET hSet)
{
	LTObject* pObj = pTracker ? pTracker->GetModelInstance() : nullptr;
	if (!pTracker || !pObj)
		RETURN_ERROR_NAMED(2, "ServerModelLT::SetWeightSet", LT_INVALIDPARAMS);

	LTRESULT dResult = CommonModelLT::SetWeightSet(pTracker, hSet);
	if (dResult == LT_OK)
		sm_SetObjectChangeFlag(m_pServerMgr, pObj, CF_MODELINFO);

	return dResult;
}


// The same shape as the six setters above, but taking the object rather than
// a tracker: there is no instance to look up, so the object the caller named
// is the one marked.
LTRESULT ServerModelLT::SetPieceHideStatus(HOBJECT hObj, HMODELPIECE hPiece,
                                           LTBOOL bHidden)
{
	LTRESULT dResult = CommonModelLT::SetPieceHideStatus(hObj, hPiece, bHidden);

	if (!dResult)
		sm_SetObjectChangeFlag(m_pServerMgr, hObj, CF_ATTACHMENTS);

	return dResult;
}

LTRESULT ServerModelLT::AddTracker(HOBJECT hObj, LTAnimTracker* pTracker)
{
	LTRESULT dResult = CommonModelLT::AddTracker(hObj, pTracker);

	if (!dResult)
		sm_SetObjectChangeFlag(m_pServerMgr, hObj, CF_MODELINFO);

	return dResult;
}

LTRESULT ServerModelLT::RemoveTracker(HOBJECT hObj, LTAnimTracker* pTracker)
{
	LTRESULT dResult = CommonModelLT::RemoveTracker(hObj, pTracker);

	// A game animator also unlinks its trackers during object destruction.
	// The removal notice already replaces any animation-state notification.
	if (!dResult && !(hObj->m_ChangeFlags & 1))
		sm_SetObjectChangeFlag(m_pServerMgr, hObj, CF_MODELINFO);

	return dResult;
}




// 0x100011e7 — put the tracker back where it belongs for a given time. The
// interface's SetCurAnimTime writes the time and then calls this, which winds
// the tracker back to the start of the animation (at_ResetAnim) and walks the
// keyframes forward again until it finds the pair the time falls between.
//
// Like dl_MakeLink in serverengine.cpp, the shipped address is in the model
// module's own run (0x10001013 onward) rather than beside the server's ILTModel
// overrides; it is here because these are the files this reconstruction owns.
// The `at_UpdateStringKeys` name came from the caller's context before the
// bytes were read and is ours: nothing here touches m_StringKeyCallback, and
// what the function actually does is re-derive m_CurKey, the two frame indices,
// the two times and the percentage between them.
//
// Read off the bytes, and three of them are surprising:
//
//   * m_CurKey is a uint32 field the found block reads as a *word*
//     (`mov 0x14(%esi),%cx`), so the source narrows it before storing the two
//     uint16 frame indices — and `test %cx,%cx; jbe` is an unsigned `> 0`,
//     not `!= 0`.
//   * the non-looping clamp of m_Cur.m_Time is DEAD WORK. at_ResetAnim has
//     just zeroed that field three instructions earlier, so the minimum is
//     always the zero already there. The block reads exactly like the same
//     clamp in the ordinary advance path, which is presumably where it was
//     copied from; the shipped bytes contain it and so does this.
//   * the divide is `fildll` for the numerator and `fidivl` for the
//     denominator: VC6 lays down the unsigned-to-float widening (zero the
//     high dword of the eight-byte temp, store the low) for *both*, and then
//     divides by the low dword only — so the second `andl $0x0,-0x4(%ebp)` is
//     dead, and a keyframe span at or above 2^31 would divide as a negative.
//     Both come out of plain `(float)` casts on uint32 values.
//
// One register rotation, and the fix is the predictable one. Written
// with a `uint32 nRange = nKeyTime - nPrevTime;` local the function came out
// 205 of 207 with the whole allocation rotated (nTime in edi rather than edx,
// the animation in ecx rather than edi) and one instruction missing: ours
// updated nKeyTime's register in place (`sub %eax,%ecx`) where the shipped
// build copies it and subtracts into the copy (`mov %edi,%eax; sub %ecx,%eax`),
// keeping nKeyTime alive. Writing the difference out twice — VC6 commons it
// itself — restores the copy and the rotation goes with it. So an *extra*
// named local rotates the allocation exactly as a missing one does.
void at_UpdateStringKeys(LTAnimTracker* pTracker, uint32 nTime)
{
	uint32 nPrevTime;

	at_ResetAnim(pTracker);

	ModelAnim* pAnim = pTracker->GetModel()->
		m_AnimArray.m_pArray[pTracker->m_TimeRef.m_Cur.m_iAnim].m_pAnim;

	uint32 nKeys = pAnim->m_nKeyFrames;

	if (nKeys <= 1)
		return;

	uint32 nLast    = nKeys - 1;
	uint32 nEndTime = pAnim->m_pKeyFrames[nLast].m_Time;

	if (nTime > nEndTime)
		nTime = nEndTime;

	if (!(pTracker->m_Flags & AT_LOOPING))
	{
		pTracker->m_TimeRef.m_Cur.m_Time =
			LTMIN(pTracker->m_TimeRef.m_Cur.m_Time, nEndTime + 1);
	}

	nPrevTime = 0;

	while (pTracker->m_CurKey <= nLast)
	{
		uint32 nKeyTime = pAnim->m_pKeyFrames[pTracker->m_CurKey].m_Time;

		if (nTime <= nKeyTime)
		{
			uint16 iKey = (uint16)pTracker->m_CurKey;

			pTracker->m_TimeRef.m_Prev.m_iFrame = (iKey > 0) ? iKey - 1 : 0;
			pTracker->m_TimeRef.m_Cur.m_iFrame  = iKey;

			if (nKeyTime - nPrevTime == 0)
			{
				pTracker->m_TimeRef.m_Percent = 0.0f;
			}
			else
			{
				pTracker->m_TimeRef.m_Cur.m_Time  = nTime;
				pTracker->m_TimeRef.m_Prev.m_Time = nPrevTime;

				pTracker->m_TimeRef.m_Percent =
					(float)(nTime - nPrevTime) / (float)(nKeyTime - nPrevTime);
			}

			return;
		}

		nPrevTime = nKeyTime;

		pTracker->m_CurKey++;
	}
}


// ---- one object's update (0x1002a431) ---------------------------------- //

// The game object hung off SObjData+0x20 is the SDK's own BaseClass
// (ltengineobjects.h), and its declaration order is what puts the one virtual
// this function reaches at slot 0x4: the destructor is virtual and comes
// first. The call site pushes three arguments and the callee cleans, which is
// `EngineMessageFn(uint32, void*, LTFLOAT)` exactly.

// MID_UPDATE, from ltserverobj.h. Nothing in the server sources includes that
// header, so the value is spelled out rather than pulled in.
#define MID_UPDATE 2

// 0x1002a83e — the string-key callback the server hangs on every tracker it
// advances: it walks the animation's string keys and sends each one on to the
// object's game class as MID_MODELSTRINGKEY. Declared, not reconstructed;
// only its address is needed here, and that address is a masked relocation
// (name ours).
void sm_ModelStringKeyCallback(LTAnimTracker* pTracker, AnimKeyFrame* pFrame);

// 0x1002a7f2 — advance the object's extra animation trackers by the frame
// time, converted to whole milliseconds and only for a model.
//
// Read off the bytes rather than guessed:
//
//   * the millisecond count is unsigned. `test %edi,%edi; jbe` is what
//     `> 0` gives on an unsigned; a signed `> 0` would be `jle` and a plain
//     `!= 0` would be `je`.
//   * the three exits all jump to one epilogue, so the two tests nest rather
//     than being written as early returns.
//   * the callback slot is stored on every trip round the loop, not once
//     before it: the store sits between the two argument pushes, inside the
//     body.
//   * the main tracker is not touched here — only the list at
//     ModelInstance+0x1f8. CServerMgr::UpdateObjects does the main one
//     through UpdateMainTracker.
//
// `inline`, and defined above its one caller, because the shipped /O2 client
// EXPANDS it: lithtech.exe 0x004697e0 opens with this body at 0x004697ef
// (`cmpb $0x1,0xbc(%esi)` / __ftol / the tracker walk) and emits no standalone
// copy.  /Os declines it -- server.dll 0x1002a7f2 is called out of line from
// its single site at 0x1002a43d -- so /O1 is unaffected.  File-private, so
// nothing outside servermodel.cpp can move either way.
inline void sm_UpdateObjectTrackers(CServerMgr* manager, LTObject* object) {
    if (object->m_ObjectType != OT_MODEL) return;
#ifdef NOLFSRC_MODERN
    static P1TickMs tick;
    const auto elapsed = static_cast<uint32>(P1_TickMs(&tick, manager->m_FrameTime, manager->m_Time));
#else
    const auto elapsed = static_cast<uint32>(manager->m_FrameTime * 1000.0f);
#endif
    model_UpdateTrackers(*static_cast<ModelInstance*>(object), elapsed, sm_ModelStringKeyCallback);
}

// 0x1002a6de — how far the object should move this frame, given the frame
// time: it answers whether there is any movement at all and writes the
// offset through its fourth argument (name ours).
//
// Every constant in it is the SDK's rather than invented. The guard and the
// tail both use CHANGEFLAG_MOTION (0x40), so the function only computes a
// move for an object already flagged as moving, and re-flags it when a
// container touched its acceleration. FLAG_CONTAINER is 1<<29 and
// ltbasedefs.h documents it as "will get an MID_AFFECTPHYSICS message for
// each object" -- and MID_AFFECTPHYSICS is 9, which is exactly the message
// this sends through BaseClass's slot 1. So the two halves are: an object
// that IS a container integrates its own velocity and acceleration in place,
// and an object that is not, but is linked into one, integrates COPIES that
// each containing container gets a chance to modify first.
//
// The walk is over the object's inter-object links, which thread through
// SObjData::m_Links -- so m_pSData doubles as the list head, which is why the
// emptiness test is `pHead->m_pNext != pHead` on the object's own m_pSData.
// Only INTERLINK_CONTAINER links whose owner really carries FLAG_CONTAINER
// are notified, and the cursor advances before the record is used because the
// message can break the link.
//
// THE MESSAGE PAYLOAD IS THE SDK's ContainerPhysics (ltserverobj.h:
// m_Acceleration, m_Velocity, m_Flags, m_hObject), which is what
// MID_AFFECTPHYSICS documents as its pData. That one struct is what the
// earlier NEAR here was missing: the "extra spilled copy of pObject at -0x4
// that nothing reloads" is cp.m_hObject, nFlags at -0x8 is cp.m_Flags, and
// the two vectors at -0x20/-0x14 are its first two members -- all stored
// because &cp escapes into EngineMessageFn. m_Flags is stored into cp only on
// the walking path (0x1002a748 after the pHead test), so the test reads the
// member and the store sits in the branch. Now IDENTICAL on server.dll
// (276 bytes). lithtech.exe agrees independently: 0x004698b0 calls
// ContainerPhysics's compiler-generated constructor (0x004040e0, the folded
// `mov %ecx,%eax; ret`) on the 0x20-byte block at 0x34(%esp), which is
// exactly this struct.
//
// RETURN VALUE: phys_ComputeFrameMove's own result, not a shipped bug. The
// callee (0x1001e088, unwritten) returns 0 after zeroing the velocity and
// acceleration and clearing CHANGEFLAG_MOTION (`xor %eax,%eax; jmp` at
// 0x1001e640) and 1 otherwise (`push $1; pop %eax` before its epilogue at
// 0x1001e6ce). Nothing after the call here touches eax -- the motion-flag OR
// goes through ecx at 0x1002a7b6 -- and the caller tests it at 0x1002a4d2;
// lithtech.exe's expansion keeps it in eax across the offset copy and tests
// it at 0x004699be. Returning it also moved the guard's `xor/leave/ret`
// ahead of the callee-saved pushes, as shipped has it (8 blocks -> 4).
//
// `inline`: lithtech.exe expands it into 0x004697e0 at 0x004698b5..0x004699b8
// and has no standalone copy.  server.dll 0x1002a6de is called out of line
// from its one site at 0x1002a4ca, so /Os declines it at this size and /O1 is
// unaffected -- with the caveat that this body is still a NEAR at /O1 (276 of
// 276 bytes, see above), so it is the one of the three whose /O1 behaviour is
// not proven byte-for-byte.  At 276 bytes it is far past anything /Os expands.
inline int sm_GetFrameMove(CServerMgr* manager, LTObject* object, float seconds, LTVector* offset) {
    if (!(object->m_ChangeFlags & CHANGEFLAG_MOTION)) return 0;
    ObjectCallbackGuard guard(*object);
    ContainerPhysics physics{};
    physics.m_Acceleration = object->m_Acceleration;
    physics.m_Velocity = object->m_Velocity;
    physics.m_Flags = object->m_Flags;
    physics.m_hObject = object;
    const bool use_local = !(object->m_Flags & FLAG_CONTAINER) && object->m_pSData && !object->m_pSData->m_Links.IsTiedOff();
    struct Context { CServerMgr* manager; ObjectCallbackGuard& guard; ContainerPhysics& physics; int notified = 0; } context{manager,guard,physics};
    if (use_local) {
        manager->m_InterObjectLinks.VisitContainers(object,[](void* pointer,LTObject* container) {
            auto& context = *static_cast<Context*>(pointer);
            if (!context.guard.unchanged()) return false;
            if ((container->m_Flags & FLAG_CONTAINER) && container->m_pSData &&
                !container->m_pSData->m_Removing && container->m_pSData->m_pBaseClass) {
                context.manager->m_ClassMgr.message(container->m_pSData->m_pBaseClass,MID_AFFECTPHYSICS,&context.physics,0);
                ++context.notified;
            }
            return context.guard.unchanged();
        },&context);
    }
    if (!guard.unchanged()) return 0;
    auto& control = manager->m_ForceControl;
    // Game callbacks can recursively compute motion for other objects.
    // Restore all shared computation inputs after those callbacks finish.
    control.m_fFrameTime = seconds; control.m_pObj = object;
    control.m_pVelocity = use_local ? &physics.m_Velocity : &object->m_Velocity;
    control.m_pAcceleration = use_local ? &physics.m_Acceleration : &object->m_Acceleration;
    control.m_nObjFlags = use_local ? physics.m_Flags : object->m_Flags;
    const auto result = phys_ComputeFrameMove(&control);
    *offset = control.m_vFrameMove;
    if (context.notified) object->m_ChangeFlags |= CHANGEFLAG_MOTION;
    return result;
}

// 0x1002a49f — the motion half of one object's update: work out the frame's
// offset, clear the acceleration, move the object through the world
// simulation, throw it away if it left the world, and tell the clients if it
// moved far enough to be worth a message (name ours).
//
// Read off the bytes rather than guessed:
//
//   * the old position is copied into a local as the very first statement,
//     and the copy's three `movsl` are interleaved with the argument pushes
//     for the offset call.
//   * both magnitude tests are calls — `_CVector::MagSqr` at 0x1000552d and
//     `_CVector::DistSqr` at 0x1000f8f8 — because the SDK's inline
//     definitions are ones VC6 declines to expand, and both compare against
//     a `.rdata` 0.001f.
//   * the new position is one vector `operator+`: it builds the sum into a
//     temporary and then copies the temporary into the local that gets its
//     address taken, which is the fourth twelve-byte slot in a 0x30 frame.
//   * the out-of-world check is one `||` chain of six comparisons with the
//     removal after it, and the "still inside" path drops into the
//     change-flag test — an `else if`, not a return.
//
// `inline`, AND HOISTED ABOVE sm_UpdateObject -- VC6's inliner is
// forward-only, so the old forward-declare-here / define-below arrangement
// made it a candidate nowhere.  The shipped /O2 client expands it: the whole
// body is the tail of lithtech.exe 0x004697e0, through the
// FLAG_REMOVEIFOUTSIDE box test to the `push $2 / push %esi / push %ebx /
// call 0x00469b80` at 0x00469b66, and there is no standalone copy.  /Os
// declines it -- server.dll 0x1002a49f is called out of line from its single
// site at 0x1002a495 -- so /O1 is unaffected.
//
// Note what 0x004697e0 does NOT expand: sm_SetObjectChangeFlag, which it
// calls at 0x00469b66 even though the same header-visible body is expanded
// elsewhere in the client.  /O2 /Ob1 decides per CALL SITE, so that is not a
// contradiction; it does mean this caller has to end up big enough for VC6 to
// decline the 346-byte expansion the way the shipped build did.
//
// lithtech.exe 0x004697e0 RESIDUAL (956 vs 918 bytes, measured): with
// ContainerPhysics in sm_GetFrameMove everything up to `m_Pos + vOffset`
// matches, but /O2 still expands that operator+ (calling the three-float
// constructor inside it) where shipped CALLS it (0x00469a1f -> 0x004316d0).
// The budget is short by very little: one extra three-store inline anywhere
// before the site closes every other block, and `vOldPos.Init(m_Pos.x,
// m_Pos.y, m_Pos.z)` gives 918 of 918 with only the old-position copy left
// (x87 loads where shipped copies dwords through a pointer in %eax). Not
// moved by: vOldPos copy-constructed, an inline returning m_Pos by value /
// reference / const reference or through an out pointer, Init(0,0,0) or
// VEC_INIT for the acceleration clear, or Dot for MagSqr.
inline void sm_MoveObjectForFrame(CServerMgr* pMgr, LTObject* pObject)
{
    ObjectCallbackGuard guard(*pObject);
	LTVector vOldPos;
	LTVector vNewPos;
	LTVector vOffset;

	vOldPos = pObject->m_Pos;

	if (!sm_GetFrameMove(pMgr, pObject, pMgr->m_FrameTime, &vOffset))
		return;

	pObject->m_Acceleration.Init();

	if (vOffset.MagSqr() > 0.001f)
	{
		vNewPos = pObject->m_Pos + vOffset;

		sm_MoveObject(pMgr, pObject, &vNewPos, 5);
        if (!guard.unchanged()) return;

		if ((pObject->m_Flags & FLAG_REMOVEIFOUTSIDE) && pMgr->m_pWorldFile &&
		    (pObject->m_Pos.x < pMgr->m_WorldBoxMin.x ||
		     pObject->m_Pos.y < pMgr->m_WorldBoxMin.y ||
		     pObject->m_Pos.z < pMgr->m_WorldBoxMin.z ||
		     pObject->m_Pos.x > pMgr->m_WorldBoxMax.x ||
		     pObject->m_Pos.y > pMgr->m_WorldBoxMax.y ||
		     pObject->m_Pos.z > pMgr->m_WorldBoxMax.z))
		{
			sm_RemoveObject(pMgr, pObject);
		}
		else if (pObject->m_ChangeFlags & 4)
		{
			if (vOldPos.DistSqr(pObject->m_Pos) > 0.001f)
				sm_SetObjectChangeFlag(pMgr, pObject, 2);
		}
	}
}

// 0x1002a431 — one object's update, which UpdateObjects times.
//
// The countdown is the raw field SetNextUpdate writes, and it is only
// decremented while it is still positive: an object whose timer has already
// expired and been serviced stays expired until the game sets it again. The
// MID_UPDATE goes out on the tick the countdown crosses zero, and only then.
//
// Read off the bytes rather than guessed:
//
//   * the engine data pointer is re-loaded from the object after the store
//     to the countdown (`mov 0x188(%esi),%ecx` twice) and shared across the
//     compare and the subtraction before it. That is three separate
//     `pObject->m_pSData->...` expressions, with the first two commoned
//     because nothing writes in between; a single local pointer would be
//     loaded once and kept.
//   * both float compares are `fcomps` against a .rdata zero rather than an
//     `ftst`, which is what `> 0.0f` and `<= 0.0f` in the source give.
//   * the guard after the message is `testb $0x4,0x120(%esi)`, a byte-wide
//     test of the object's change flags, and the branch skips the tail call
//     when the bit is clear — so the source returns there rather than
//     wrapping the tail.
void sm_UpdateObject(CServerMgr* pMgr, LTObject* pObject)
{
    ObjectCallbackGuard guard(*pObject);
    if (!pObject->m_pSData) return;
	sm_UpdateObjectTrackers(pMgr, pObject);
    if (!guard.unchanged()) return;

	if (pObject->m_pSData->m_NextUpdate > 0.0f)
	{
		pObject->m_pSData->m_NextUpdate -= pMgr->m_FrameTime;

		if (pObject->m_pSData->m_NextUpdate <= 0.0f)
		{
            if (pObject->m_pSData->m_pBaseClass)
                pMgr->m_ClassMgr.message(pObject->m_pSData->m_pBaseClass,MID_UPDATE, LTNULL, 0.0f);
            if (!guard.unchanged()) return;

			if (!(pObject->m_ChangeFlags & 0x04))
				return;
		}
	}

	sm_MoveObjectForFrame(pMgr, pObject);
}


// ---- the world file's object list (0x1002a8b6) ------------------------- //

// ltbasedefs.h's ObjectCreateStruct is used here as the header has it — the
// local redeclaration this file used to carry, with `void Clear();` out of
// line, is gone.  Its premise was that /Ob1 would otherwise expand the SDK's
// in-class body into every caller and lose the two calls the shipped code
// makes.  The shipped bytes say the opposite, in both images and in THIS
// function:
//
//   * server.dll's copy (0x1002a8b6, which we match) calls 0x10001bd7 twice,
//     at 0x1002a8c8 for the constructor and 0x1002a989 for the explicit
//     Clear() — so /Os DECLINED the 125-byte body even though it was visible.
//     Twelve more call sites across five TUs do the same.
//   * lithtech.exe's copy (0x00469d10..0x0046a27f, 1392 bytes) has that body
//     EXPANDED at both sites — the constructor's at 0x00469d2f and the
//     explicit one at 0x00469ea3, each recognisable by the field pattern
//     (+0x00/+0x02 zero words, `movl $1,+0x04`, 1.0f at +0x1c/+0x20/+0x24,
//     the MAX_MODEL_TEXTURES=2 loop, the two trailing floats at +0x27c and
//     +0x280) — and emits no standalone copy.
//
// One spelling, both shapes: the /O1-vs-/O2 split exactly.  smgr.cpp made the
// same correction for PrecacheModels and 0x00475670.
//
// lithtech.exe's constructor-side Clear() is not expanded whole, though: it
// CALLS LTVector::Init for m_Scale (0x00469d55 -> 0x0041ab50) and
// LTRotation::Init (0x00469d66 -> 0x00402ea0); the explicit one expands
// everything.  That is /O2's per-function inline budget (spent from the end
// backwards) running out, and what spends exactly enough of it is in the
// body below: sm_AllocateObjectInline / sm_FreeObjectInline (servermodel.h)
// expanded at 0x0046a048 / 0x0046a14c, and ILTStream's inline GetPos() for
// the record position.  Without GetPos() m_Scale's Init stays inline too;
// with the two helpers as calls the function is 1210 bytes.
//
// The layout is unchanged either way: 0x284 bytes, which is precisely the
// span this function's frame gives it (-0x2b4 to -0x30), and every field it
// touches lands on a named one — the type at +0x00, m_Flags at +0x08, m_Pos
// at +0x10, the class name at +0x3c with its 0x3f-byte strncpy and the
// terminator at +0x7b, m_Filename[0] at +0x7c and m_SkinName[0] at +0xfc.

// 0x10001741 — the class module's linear name lookup (counter.cpp declares
// the same address; see its note). 0x10001778 — the first class in a chain
// carrying any of the given flags (saveio.cpp's cls_FindFlagged; its first
// parameter is never read).
struct ClassDef* sub_10001741(void* pModule, char* pName);

// 0x10001778 — cdecl, three arguments, and the first one is never read: the
// body loads 0x8(%esp) and 0xc(%esp) and nothing else, so whatever the two
// call sites pass as the class manager is dead weight. saveio.cpp declares
// the same address the same way. The flags word is re-read out of its
// argument slot on every trip round the loop, which is what says it was
// never copied into a local (name ours).
struct ClassDef* cls_FindFlagged(void* pUnused, struct ClassDef* pClass, uint32 nFlags)
{
	while (pClass)
	{
		if (pClass->m_ClassFlags & nFlags)
			return pClass;

		pClass = pClass->m_ParentClass;
	}

	return NULL;
}

// Create authored world actors from the bounded decoder's owned inventory.
// Missing classes retain the original warning-and-skip behavior; editor-only
// classes and classes outside the caller's CF_ALWAYSLOAD policy are filtered.
LTRESULT sm_LoadWorldObjects(CServerMgr* manager, ILTStream* stream, char* world_name, uint32 load_all) {
    if (!manager || !stream || !world_name) return LT_INVALIDPARAMS;
    ServerCallbackGuard server(manager->m_Lifetime);
    try {
        // A precreate callback can replace or end the world. Keep this authored
        // inventory independent of the live world's storage throughout dispatch.
        const auto definitions = manager->m_World.ObjectDefinitions();
        for (const auto& authored : definitions) {
            auto* record = manager->m_ClassMgr.find(authored.class_name);
            if (!record) {
                dsi_ServerError("Server is missing class %s", authored.class_name.c_str());
                continue;
            }
            auto* definition = record->m_pClassDef;
            if ((definition->m_ClassFlags & CF_NORUNTIME) ||
                (!load_all && !cls_FindFlagged(&manager->m_ClassMgr, definition, CF_ALWAYSLOAD))) continue;
            auto properties = std::make_shared<ObjectProperties>(std::span<const WorldProperty>(authored.properties));
            ObjectCreateStruct create;
            manager->CreateObjectWithProperties(reinterpret_cast<HCLASS>(record), &create, std::move(properties), PRECREATE_WORLDFILE);
            if (!server.unchanged()) return LT_ERROR;
        }
        return LT_OK;
    } catch (const std::exception& error) {
        nolf::platform::log("world object properties", error.what());
        if (server.unchanged()) manager->SetupError(LT_INVALIDWORLDFILE, world_name);
        return LT_INVALIDWORLDFILE;
    }
}
