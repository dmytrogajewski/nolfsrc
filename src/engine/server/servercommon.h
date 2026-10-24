// *********************************************************************** //
//  MODULE:   SERVERCOMMON (.H)
//  PURPOSE:  ServerCommonLT — the server's half of ILTCommon, at
//            0x1002da71-0x1002dbc3. Where SharedCommonLT can answer from the
//            object alone, these need the server's own tables.
// *********************************************************************** //
#ifndef __SERVERCOMMON_H__
#define __SERVERCOMMON_H__

#include "sharedcommon.h"
#include "servermgr.h"

// 0x1002acd3 — resolve a 16-bit network object id. The id is passed without
// being widened, so the callee's parameter really is uint16.
HOBJECT sm_FindObject(CServerMgr* pServerMgr, uint16 nObjectID);

// 0x10012cf3 — is this point inside the world at all? (name ours)
LTBOOL world_IsPointInside(class CUnknown210* pWorld, LTVector* pPoint);

// The record world_IsPointInside hands its callback: the point it is asking
// about and the answer, which the callback only ever clears (names ours).
class CPointQuery
{
public:
	LTVector m_vPoint;                       // 0x00
	int      m_bInside;                      // 0x0c
};

typedef void (*PointQueryFn)(class LTObject* pObj, CPointQuery* pQuery);

// 0x10012c9c — that callback (name ours).
void world_PointInsideCB(class LTObject* pObj, CPointQuery* pQuery);

// The container at CServerMgr+0x210 is polymorphic, which servermgr.h's
// CUnknown210 does not model; only slot 3 is reached from here, so the
// interface is declared locally and the pointer cast (names ours).
class ServerCommonLT : public SharedCommonLT
{
public:
	LTRESULT GetAttachmentObjects(HATTACHMENT hAttachment, HOBJECT& hParent, HOBJECT& hChild);
	LTRESULT GetPolyTextureFlags(HPOLY hPoly, uint32* pFlags);
	virtual LTRESULT SetObjectFlags(HOBJECT hObj, const ObjFlagType flagType,
	                                uint32 dwFlags);               // 0x1002d733
	virtual LTRESULT SetObjectFilenames(HOBJECT pObj, ObjectCreateStruct* pStruct);  // 0x1002d82c

	// ILTCommon's CreateMessage(ILTMessage*&), slot 14: it must OVERRIDE
	// SharedCommonLT's stub. Declared with an LMessageImpl*& it overrode
	// nothing -- the ILTCommon slot kept the stub and this became a 19th
	// slot no caller knows -- which is how the C++ link's ServerCommonLT
	// table came to differ from the shipped one (0x004a3138, 18 slots).
	virtual LTRESULT CreateMessage(class ILTMessage*& pMsg);        // 0x1002db00
	virtual LTRESULT GetPointStatus(LTVector* pPoint);              // 0x1002da41
	virtual LTRESULT GetPointShade(LTVector* pPoint, LTVector* pColor);  // 0x1002da5f

public:
	// SharedCommonLT ends at 0x10 — ILTCommon's two pointers at 0x04 and
	// 0x08, then the math implementation at 0x0c.
	CServerMgr* m_pServerMgr;                // 0x10
};

// The record SetObjectFilenames rolls back on failure, read off the pair that
// fills and empties it (0x1002badb / 0x1002bb52): 0x38 bytes copied wholesale
// out of a model instance's animation tracker, then the object's two file
// identifiers. `rep movsl` with a count of 0xe is what fixes the first field
// at exactly sizeof(LTAnimTracker), and 0x30/0x34 on SObjData is what names
// the other two. The struct name is ours; those two functions report
// themselves as "BackupExtraData" and "RestoreExtraData", so "extra data" is
// the shipped name for the tracker half.
//
// The tracker is modelled here rather than taken straight from
// ltanimtracker.h, for one reason worth recording. The layout is the SDK's —
// LTAnimTracker is 0x38 bytes with its AnimTimeRef at +0x1c, which the
// 0xe-dword `rep movsl` and the constructor's `ecx = &state+0x1c` prove
// between them — but the SDK copy defines AnimTimeRef's constructor *and*
// Clear() inside the class body, and /Ob1 then expands the constructor into
// SetObjectFilenames (four bytes over, and that function stops matching).
// The shipped build calls both — 0x1002da31 stores -1 and calls 0x100175fd —
// which is what you get from members declared in the class and defined out of
// line, so that is how they are written. Names ours; the fields are
// AnimTimeRef's, and every offset the reset touches lands on one.
struct CSavedFileState {
    LTAnimTracker m_Tracker{};
    void* m_pModelFile = nullptr;
    void* m_SkinFiles[MAX_MODEL_TEXTURES]{};
};

// The world-side file swap: save the object's file state, pull it from the
// tree, apply the new names, restore on failure; then the per-object file
// packet writer. Save and restore are the only two of the four whose shipped
// names survive, in their own parameter-check literals — "BackupExtraData"
// and "RestoreExtraData"; the other two names are ours. Both report failure
// as an LTRESULT, which SetObjectFilenames ignores.
LTRESULT w_SaveObjectFileState(class LTObject* pObj, CSavedFileState* pState);   // 0x1002badb
LTRESULT w_ApplyObjectFilenames(class CServerMgr* pMgr, class LTObject* pObj,
                                ObjectCreateStruct* pStruct);                    // 0x1002ba90
LTRESULT w_RestoreObjectFileState(class LTObject* pObj, CSavedFileState* pState);// 0x1002bb52
void     sm_WriteObjectFiles(class LTObject* pObj, class ServerMessage* pMsg);   // 0x10029087

// SObjData's three file identifiers, which sm_WriteObjectFiles indexes as one
// array: ltobject_internal.h names the first two (m_pModelFile 0x30,
// m_pSkinFile 0x34) and stops, but the writer's loop runs 0x30, 0x34, 0x38.
// Name ours; the cast is what keeps the two-level load the shipped loop
// repeats every iteration.


#endif
