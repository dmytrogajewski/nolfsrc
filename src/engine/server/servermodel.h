// *********************************************************************** //
//  MODULE:   SERVERMODEL (.H)
//  PURPOSE:  ServerModelLT — the server's overrides of the six ILTModel
//            setters, at 0x1002dc22-0x1002debe. Each one calls the shared
//            implementation and, if it succeeded, marks the object as changed
//            so the change goes out to the clients on the next update. That
//            is the whole reason these exist separately.
//
//            The pattern is identical in all six, including the detail that
//            the model instance is loaded from the tracker *before* the null
//            checks rather than after.
// *********************************************************************** //
#ifndef __SERVERMODEL_H__
#define __SERVERMODEL_H__

#include "commonmodel.h"
#include "servermgr.h"
#include "de_error.h"

class CServerMgr;

// Which part of an object changed; the server collects these per object and
// sends the deltas out.  The names are not guesses: lithtech.exe's change
// trace at 0x00479160 tests each bit in turn and prints its name, so the
// whole space is read off the shipped bytes --
//
//   0x0001 CF_NEWOBJECT   0x0010 CF_SCALE       0x0080 (no name printed)
//   0x0002 CF_POSITION    0x0020 CF_MODELINFO   0x0100 CF_ATTACHMENTS
//   0x0004 CF_ROTATION    0x0040 CF_COLORINFO
//   0x0008 CF_FLAGS
//
// This is SObjData::m_ClientChangeFlags' space, which is NOT the same as
// LTObject::m_ChangeFlags at 0x120 (bit 2 = in world, 0x40 = bbox dirty).
#define CF_NEWOBJECT    0x1
#define CF_POSITION     0x2
#define CF_ROTATION     0x4
#define CF_FLAGS        0x8
#define CF_SCALE        0x10
#define CF_MODELINFO    0x20
#define CF_COLORINFO    0x40
#define CF_ATTACHMENTS  0x100

// DEFINED IN THE HEADER, `inline`, AND ABOVE EVERY CALLER.  This is not a
// servermodel.cpp private: the shipped /O2 build expands it into callers in
// three other translation units, so it has to be a candidate in all of them --
//
//   servermodel.cpp   ServerModelLT's setters, 123 bytes -> the shipped 346
//   servermessage.cpp CLTServer::SetModelAnimation   0x0046e400  266b
//                     CLTServer::SetModelLooping     0x0046e540  274b
//                     CLTServer::ResetModelAnimation 0x0046e680  329b
//   servercommon.cpp  ServerCommonLT::SetObjectFlags 0x0046c190  680b (two
//                     sites, at 0x0046c231 and 0x0046c375)
//
// VC6's inliner is forward-only, hence the position, and `inline` alone with
// the definition in the .cpp reached only the four setters in that one file.
// The standalone copy at 0x00469b80 is still emitted -- `inline` does not
// suppress it, the address is taken -- and /Os declines the expansion at all
// 25 of server.dll's call sites, which is why /O1 is unaffected.
// 0x1002a5fe — record that one part of an object changed, so the next update
// sends it. The flags live on the object's SObjData, not the object, and an
// object with none set yet is pushed onto the manager's changed list at the
// same time — so the list is exactly the objects with a nonzero
// m_ClientChangeFlags, and the guard on that field is what keeps it from being
// added twice.
//
// Read off the bytes:
//
//   * both failures are RETURN_ERROR_INFO at level 1, reporting the code by
//     name ("LT_OBJECTNOTINWORLD", "LT_ERROR") and pushing the same string
//     literal for the function at both sites. The literal is
//     "SetObjectChangeFlags", plural, where the symbol here is singular; that
//     literal is the interface's name for it and is reproduced as shipped.
//   * the description each site passes is not the usual lone space but the
//     object's class name, three loads deep
//     (`mov 0x188(%esi),%eax; mov 0x24(%eax),%eax; push (%eax)`) — so a
//     refused change says which class asked. Written with the blank
//     description the function is exactly twelve bytes short, six at each
//     site, and the pObj register rotates with it.
//   * the second check is two tests jumping to one error block placed *after*
//     the body, which is the `||` shape: the entry has to be flagged 1 and
//     have a record.
//   * the world-loaded gate is only consulted for an object that is not on
//     the list yet, so an object changed before the world finished loading
//     collects flags nothing will walk.
//   * `pObj->m_pSData` is written out at each use rather than kept in a local:
//     VC6 commons the member's *address* into a register (`lea 0x188(%esi)`)
//     and reloads the pointer through it after the store to the manager,
//     which it has to do because that store could alias. A local would keep
//     the pointer in a register across it and lose the reload.
inline LTRESULT sm_SetObjectChangeFlag(CServerMgr* pServerMgr, LTObject* pObj,
                                uint32 dwFlag)
{
	// Bit 2 of the object's flag word is "in the world"; smgr.cpp's update
	// sweep tests the same bit the same way.
	if (!(pObj->m_ChangeFlags & 4))
		RETURN_ERROR_INFO(1, "SetObjectChangeFlags", LT_OBJECTNOTINWORLD,
		                  pObj->m_pSData->m_pClass->m_ClassName);

	CObjectMapEntry* pEntry =
		&pServerMgr->m_Unknown8fc.m_pArray[pObj->m_ObjectID];

	if (pEntry->m_nFlags != 1 || !pEntry->m_pRecord)
		RETURN_ERROR_INFO(1, "SetObjectChangeFlags", LT_ERROR,
		                  pObj->m_pSData->m_pClass->m_ClassName);

	if (pObj->m_pSData->m_ClientChangeFlags == 0 && pServerMgr->m_bWorldLoaded)
	{
		pObj->m_pSData->m_pNextChanged = pServerMgr->m_pChangedObjects;

		pServerMgr->m_pChangedObjects = pObj;
	}

	pObj->m_pSData->m_ClientChangeFlags |= dwFlag;

	return LT_OK;
}

// 0x100011e7 — re-walk the string keys after a time jump.
void at_UpdateStringKeys(LTAnimTracker* pTracker, uint32 nTime);


class ServerModelLT : public CommonModelLT
{
public:
	// In-class: CLTServer's constructor expands it (0x1002d3b7, before the
	// SharedCommonLT call on the next member).
	ServerModelLT(CServerMgr* pServerMgr) { m_pServerMgr = pServerMgr; }

	LTRESULT SetLooping(LTAnimTracker* pTracker, LTBOOL bLooping);
	LTRESULT SetPlaying(LTAnimTracker* pTracker, LTBOOL bPlaying);
	LTRESULT SetCurAnimTime(LTAnimTracker* pTracker, uint32 curTime);
	LTRESULT SetCurAnim(LTAnimTracker* pTracker, HMODELANIM hAnim);
	LTRESULT ResetAnim(LTAnimTracker* pTracker);
	LTRESULT SetWeightSet(LTAnimTracker* pTracker, HMODELWEIGHTSET hSet);

	// Three more of the same shape, reached through the interface rather
	// than through a tracker: these take the object directly, so they mark
	// what they were handed instead of what a tracker points at.
	LTRESULT SetPieceHideStatus(HOBJECT hObj, HMODELPIECE hPiece,
	                            LTBOOL bHidden);                  // 0x1002debf
	LTRESULT AddTracker(HOBJECT hObj, LTAnimTracker* pTracker);    // 0x1002dbc4
	LTRESULT RemoveTracker(HOBJECT hObj, LTAnimTracker* pTracker); // 0x1002dbf3

public:
	CServerMgr* m_pServerMgr;                // 0x04 (0x00 is the vtable)
};

// structbank.cpp's sm_AllocateObject (0x10001c54) and smgr.cpp's
// sm_FreeObject (0x1002abf8), `inline` under local names, IN THIS HEADER
// because two translation units expand them in lithtech.exe's /O2 build:
//
//   smgr.cpp        si_CreateObjectProps (0x00470c8d..0x00470cd9, and its
//                   failure tail), CServerMgr::CreateObject (0x004745a0,
//                   0x00474631), sub_100334cc (0x00474a86)
//   servermodel.cpp sm_LoadWorldObjects (0x0046a048: the class-stats index
//                   off +0x834, the bank at +8, the allocate with its
//                   sb_Grow, the two clears, ConstructFn through +0xc;
//                   0x0046a14c: DestructFn through +0x10, then the free)
//
// while server.dll calls 0x10001c54 / 0x1002abf8 at every one of those
// sites. /Os declines both bodies -- the allocate for the `if` inside
// sb_Allocate, the free for its do/while(0) -- and /Ot takes them, and the
// flow folds away so the emitted code is unchanged. Most likely the free was
// a do/while(0) macro in the original; the control flow is measured. The
// out-of-line definitions stay where they are (servermgr.h declares them and
// other callers use them); the bodies are identical, so /OPT:ICF folds the
// COMDATs these emit onto the same two addresses in server.dll.
inline LPBASECLASS sm_AllocateObjectInline(CServerMgr* pMgr, ClassDef* pClassDef)
{
	return pMgr->m_ClassMgr.allocate(pClassDef);
}

inline void sm_FreeObjectInline(CServerMgr* pMgr, ClassDef* pClassDef,
                                LPBASECLASS pObject)
{
	(void)pClassDef;
	pMgr->m_ClassMgr.destroy(pObject);
}

#endif
