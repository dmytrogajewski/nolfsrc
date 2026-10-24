// server.dll — the server's half of ILTCommon (reconstruction). Build /O1.
#include "servercommon.h"
#include "servermsg.h"
#include "servermgr.h"
#include "servermodel.h"
#include "de_error.h"
#ifndef LT_SERVER_BUILD
#include "packet.h"
#endif

// WHY IT IS `inline` AND WHY IT IS UP HERE, ABOVE ITS ONE CALLER.
// lithtech.exe (/O2) EXPANDS this into GetPolyTextureFlags below — 0x0046c6d0
// is 137 bytes and carries the shift, the +0x190 bound check, the indexed
// +0x18c load and the virtual call in line, where server.dll (/O1, /Os) calls
// it at 0x1002da86 and that is its ONLY caller in either image. VC6's inliner
// is forward-only, so the definition has to sit above the call site, and
// hoisting alone is measured to do nothing — the keyword is what raises the
// size limit past 47 bytes.
//
// 0x1002dad1 — CServerWorld::FindPoly. An HPOLY is (world index << 16) |
// (poly index within that world); the high half selects the record in the
// 0x330 table and the poly question is then that world's own.
//
// Two things read off the bytes rather than guessed:
//
//   * `this` is the manager's embedded world (the caller does
//     `add $0x1a4,%ecx`), and the two fields it reads are at +0x18c and
//     +0x190 — the manager's 0x330 and 0x334, i.e. m_pFileTrees and
//     m_nFileTrees. servermgr.h models those as CServerMgr's own, which is
//     how the rest of the image reaches them (GetHPolyObject), so they are
//     reached here through a layout cast rather than moved.
//   * both not-found paths tail-merge into one `xor eax,eax` block sitting
//     *between* the two tests, which is two separate guarded returns (the
//     second one's copy stays inline and the first jumps forward into it).
//     An `||` would sink the merged block past the call.
//
// The virtual is IWorldBsp's slot 0x0c, reached on the record's +0x04
// pointer, and it is handed the whole handle — the low half it needs and the
// high half it does not.
LTRESULT ServerCommonLT::GetPolyTextureFlags(HPOLY hPoly, uint32* pFlags)
{
	*pFlags = 0;

	WorldPolyOwner* pPoly = m_pServerMgr->m_World.FindPoly(hPoly);
	if (pPoly)
	{
		*pFlags = pPoly->m_pParent->m_nTextureFlags;
		return LT_OK;
	}

	RETURN_ERROR(2, "CommonLT::GetPolyTextureFlags", LT_ERROR);
}

// ONE `return LT_OK`, reached through nested positive tests, not three early
// returns. That is what decides where edi is saved. The shipped build
// shrink-wraps it: `push %edi` comes after the null guard (0x1002d77b, /O2
// 0x0046c1dd), and the guard's error return skips `pop %edi` (/O1 jumps to
// `pop %esi` at 0x1002d827; /O2 gives it its own epilogue with no edi).
// VC6 saves a callee-saved register around the smallest single-exit region
// that holds its variable (edi is dwFlags, used only in the OFT_Flags arm).
// With `if (nOld == dwFlags) return LT_OK;` and a second `return LT_OK` at the
// end of that arm, those returns leave the region separately and meet the
// error return at the epilogue, so edi was saved at entry. Written as one
// shared return, the whole post-guard part is a single-exit region and the
// save moves down to it, as shipped. Byte-identical on both images.
// Measured and negative with the early returns kept: else after the guard; a
// function-scope name static with RETURN_ERROR_SYM; a copy of dwFlags (block-
// scoped, function-scoped, `register`, in one arm or both); nOld/nType/nPrev at
// function scope; `dwFlags == nOld`; the branches inverted; a switch; the guard
// written positively with the body inside it (250 bytes); the body moved into
// an inline helper (not expanded); CL_REV 8168 and 8966.
LTRESULT ServerCommonLT::SetObjectFlags(HOBJECT hObj, const ObjFlagType flagType, uint32 dwFlags)
{
	if (!hObj)
		RETURN_ERROR_NAMED(2, "ServerCommonLT::SetObjectFlags", LT_INVALIDPARAMS);

	if (flagType == OFT_Flags)
	{
		uint32 nOld = hObj->m_Flags;
		if (nOld != dwFlags)
		{
			hObj->m_ChangeFlags |= 0x40;

			if ((nOld & FLAG_SOLID) && !(dwFlags & FLAG_SOLID))
				obj_UnlinkTrackersA(hObj);

			if ((hObj->m_Flags ^ dwFlags) & 0xffff)
				sm_SetObjectChangeFlag(m_pServerMgr, hObj, 8);

			char nType = hObj->m_ObjectType;
			uint32 nPrev = hObj->m_Flags;
			hObj->m_Flags = dwFlags;

			// A world model losing box physics needs its transform re-derived.
			if ((nType == OT_WORLDMODEL || nType == OT_CONTAINER) &&
			    (nPrev & FLAG_BOXPHYSICS) && !(dwFlags & FLAG_BOXPHYSICS))
				obj_RetransformWorldModel(hObj);

			sm_UpdateObjectBBox(m_pServerMgr, hObj);
		}
	}
	else if (hObj->m_Flags2 != dwFlags)
	{
		hObj->m_Flags2 = dwFlags;
		sm_SetObjectChangeFlag(m_pServerMgr, hObj, 8);
	}

	return LT_OK;
}


LTRESULT ServerCommonLT::SetObjectFilenames(HOBJECT pObj, ObjectCreateStruct* pStruct)
{
	CMessageRef msg;
	CSavedFileState state;
	Model* pOldModel;
	LTRESULT dResult;
	uint32 nFound;

	// One name variable for both check sites, so the identical error bodies
	// tail-merge (two RETURN_ERROR_NAMEDs would each get their own static).
	static char* pErrFnName = "ServerCommonLT::SetObjectFilenames";

	// ONE refusal for all three tests: lithtech.exe's /O2 copy (0x0046c440)
	// has a single error body at 0x0046c63d that all four branches reach;
	// written as two refusals /O2 kept a second head for the type test.
	// server.dll's /O1 bytes are the same either way.
	char nType;
	if (!pStruct || !pObj ||
	    ((nType = pObj->m_ObjectType) != OT_MODEL && nType != OT_SPRITE))
		RETURN_ERROR_SYM(2, pErrFnName, LT_INVALIDPARAMS);

	pOldModel = LTNULL;
	if (nType == OT_MODEL)
		pOldModel = ((ModelInstance*)pObj)->m_AnimTracker.m_TimeRef.m_pModel;

	{
		CAbstractIO* pIO;
		msg.Init(msg_AllocPacket(&pIO, 0x1000, 0x1000));
		if (pIO)
			pIO->Release();
	}

	w_SaveObjectFileState(pObj, &state);
	sm_RemoveFromWorldTree(m_pServerMgr, pObj);

	dResult = w_ApplyObjectFilenames(m_pServerMgr, pObj, pStruct);
	if (dResult != LT_OK)
	{
		w_RestoreObjectFileState(pObj, &state);
		return dResult;
	}

	{
		ServerMessage* pMsg = msg.m_pMsg;
		pMsg->m_Unknown38 = 1;
		pMsg->m_Unknown34 = 1;
	}
	msg.m_pMsg->WriteWord(pObj->m_ObjectID);
	sm_WriteObjectFiles(pObj, msg.m_pMsg);

	// Every client whose table already carries the object learns the names.
	LTLink* pListHead = &m_pServerMgr->m_ClientList.m_Head;
	LTLink* pCur;
	for (pCur = pListHead->m_pNext; pCur != pListHead; pCur = pCur->m_pNext)
	{
		Client* pClient = (Client*)pCur->m_pData;
		if (pClient->m_ObjInfos[pObj->m_ObjectID].m_ChangeFlags & 0x1000)
			sm_SendToClient(m_pServerMgr, pClient, 0xf, msg.m_pMsg, 0, 0x80);
	}

	// Re-bind each attachment's socket/node index onto the new model by name.
	if (pOldModel)
	{
		Model* pNewModel = ((ModelInstance*)pObj)->m_AnimTracker.m_TimeRef.m_pModel;
		// collection.h's GetSize()/operator[] rather than bare m_nElements /
		// m_pArray: identical code on both images, but the accessor
		// expansions are what spend lithtech.exe's /O2 inline budget so that
		// AnimTimeRef::Init, at the top of the function, stays a call
		// (0x0046c465) as shipped. Bare fields: Init expanded, 600 bytes.
		Attachment* pAttach;
		for (pAttach = pObj->m_pAttachments; pAttach; pAttach = pAttach->m_pNext)
		{
			uint32 nOldSockets = pOldModel->m_Sockets.GetSize();
			uint32 iNode = pAttach->m_iNode;

			if (iNode < nOldSockets)
			{
				ModelSocket* pSocket = pOldModel->m_Sockets[iNode];
				if (pNewModel->FindSocket(pSocket->m_Name, &nFound))
					pAttach->m_iNode = nFound;
			}
			else if (iNode < nOldSockets + pOldModel->m_NodeTransforms.GetSize())
			{
				char* pName = pOldModel->m_Nodes[iNode - nOldSockets]->m_pName;
				if (pNewModel->FindNode(pName, &nFound))
					pAttach->m_iNode = pNewModel->m_Sockets.GetSize() + nFound;
			}
		}
	}

	return LT_OK;
}


// A message to fill in is the manager's to hand out; what the caller gets is
// the writer embedded in it, eight bytes along, not the message itself.
LTRESULT ServerCommonLT::CreateMessage(ILTMessage*& pMsg)
{
	ServerMessage* pNew = m_pServerMgr->StartMessage();

	pMsg = (ILTMessage*)&pNew->m_Write;

	return LT_OK;
}

// Inside or outside — both are answers, so neither is an error code.
LTRESULT ServerCommonLT::GetPointStatus(LTVector* pPoint)
{
	return world_IsPointInside(&m_pServerMgr->m_Unknown210, pPoint)
	           ? LT_INSIDE : LT_OUTSIDE;
}

// 0x1002da5f — the light-grid sample is the server interface's own
// (smgr.cpp, 0x1002cee7), and this slot answers whatever it does: two
// arguments straight through, `ret $0x8`. si_GetPointShade answers LTTRUE,
// which is LT_ERROR as an LTRESULT; the forwarder does not translate it.
LTBOOL si_GetPointShade(LTVector* pPoint, LTVector* pColor);

LTRESULT ServerCommonLT::GetPointShade(LTVector* pPoint, LTVector* pColor)
{
	return si_GetPointShade(pPoint, pColor);
}

// 0x1002da31 — the time reference starts on no weight set and no animation.
// Declared in the class and defined here rather than in the class body: /Ob1
// would otherwise expand it into SetObjectFilenames, and the shipped build
// calls it.


// 0x100175fd — AnimTimeRef::Clear, which is Init(NULL,0,0,0,0,0.0f) with the
// arguments folded (ltanimtracker.h has the era source). Shared with the
// model walker's eight of them, which is why it is a call and not eight
// inline stores at each site. The `fldz` leads the block: the FPU store is
// scheduled first and the seven integer stores then follow declaration order
// off one zero register.



// ---- the world-side file swap -------------------------------------------- //

// 0x1002badb and 0x1002bb52 — one pair, mirror images. Both report themselves
// as "BackupExtraData" / "RestoreExtraData", which is the only naming
// information left for either; both check level 1.
//
// Two things read off the bytes rather than guessed:
//
//   * the tracker is copied WHOLE and only for a model — `cmpb $0x1,0xbc` is
//     m_ObjectType against OT_MODEL, and `rep movsl` with a count of 0xe is
//     0x38 bytes, which is exactly sizeof(LTAnimTracker) and exactly what
//     sits at ModelInstance+0x1c0. A sprite therefore saves its two file
//     identifiers and nothing else.
//   * both return an LTRESULT (`xor eax,eax` on the way out, 0x3c on the
//     guard), which SetObjectFilenames discards.
//
// The shipped code keeps `&pObj->m_pSData` in a register across the first
// store and reloads the pointer through it for the second field, because the
// store to the caller's record could alias — which is just what writing the
// two members out separately gives you.
LTRESULT w_SaveObjectFileState(LTObject* pObj, CSavedFileState* pState)
{
	if (!pObj || !pObj->m_pSData)
		RETURN_ERROR(1, "BackupExtraData", LT_INVALIDPARAMS);

	pState->m_pModelFile = pObj->m_pSData->m_pModelFile;
	for (uint32 i = 0; i < MAX_MODEL_TEXTURES; ++i) pState->m_SkinFiles[i] = pObj->m_pSData->m_SkinFiles[i];

	if (pObj->m_ObjectType == OT_MODEL)
		pState->m_Tracker = ((ModelInstance*)pObj)->m_AnimTracker;

	return LT_OK;
}

LTRESULT w_RestoreObjectFileState(LTObject* pObj, CSavedFileState* pState)
{
	if (!pObj || !pObj->m_pSData)
		RETURN_ERROR(1, "RestoreExtraData", LT_INVALIDPARAMS);

	pObj->m_pSData->m_pModelFile = pState->m_pModelFile;
	for (uint32 i = 0; i < MAX_MODEL_TEXTURES; ++i) pObj->m_pSData->m_SkinFiles[i] = pState->m_SkinFiles[i];

	if (pObj->m_ObjectType == OT_MODEL)
		((ModelInstance*)pObj)->m_AnimTracker = pState->m_Tracker;

	return LT_OK;
}


// 0x10029087 — the three file identifiers an object carries, as three words.
//
// Three things the bytes settle:
//
//   * the first identifier is dereferenced without a null check and the other
//     two are not (0x10029092 goes straight through; 0x100290b4 tests). A
//     missing model or skin sends 0xffff; a missing first one faults. That
//     asymmetry is shipped and is reproduced as shipped.
//   * the counter is sixteen bits. With `uint32 i` VC6 drops the trip counter
//     and compares the reduced byte offset against 0x3c instead, which is
//     three bytes shorter than the shipped `push $2; pop ebx` / `dec ebx`
//     pair; a `uint16` counter is what keeps the separate down-counter.
//   * the identifier goes through a `uint16` local, not a ternary. The
//     shipped true arm is `mov 0x8(%eax),%ax` — a 16-bit load — where an
//     int-typed `?:` widens it with `movzwl`.
//
// The loop reloads m_pSData every iteration and keeps only the byte offset in
// a register, which is what indexing off the member gives you once a call
// stands between two reads of it.
//
// lithtech.exe 0x004670a0 is the same source at /O2 with both writes
// expanded: the "CPacket::WriteType" overrun report and the Append loop
// through 0x00412f50 appear twice, so both sites are WriteType<uint16>
// there. server.dll calls ServerMessage::WriteWord (0x10014a53) at both.
void sm_WriteObjectFiles(LTObject* object, ServerMessage* message) {
    const auto* data = object->m_pSData;
    const auto write = [&](void* handle) {
        const auto* file = static_cast<CFileRef*>(handle);
        message->WriteWord(file ? file->m_nFileID : uint16(0xffff));
    };
    write(data->m_pModelFile);
    for (auto* skin : data->m_SkinFiles) write(skin);
}



// 0x1000f878 is `MatVMul_H` — and that is not a name of ours, it is
// ltmatrix.h's own inline (line 222). Referencing it from here makes VC6
// emit the header's out-of-line COMDAT for it, and that COMDAT is
// byte-identical to the shipped 128 bytes at 0x1000f878, term order and
// reciprocal included. So the SDK header names a shipped function for free,
// and this caller's bare `fstp %st(0)` is the discarded `one_over_w`.

// 0x10012c3a — is a point inside a BSP tree? Zero means it is (worldsim.h
// carries the same declaration as bsp_PointInside; that header is not
// included here).
int bsp_PointInside(BSPNode* pRoot, LTVector* pPoint);

// The LTObject virtual at slot 0x34 answers whether the object takes part in
// the containment test at all. ltobject_internal.h declares the slot as
// `void _p34()` and is not this reconstruction's to change, so it is
// redeclared on a layout-identical view whose only purpose is to give the
// slot a return value (names ours; the view's vptr is the object's own, at
// offset 0, which is where the shipped `mov (%esi),%eax` reads it).

// 0x10012c9c — the callback world_IsPointInside installs. One object gets
// to veto the point, and only ever in that direction.
//
// Read off the bytes:
//
//   * the two guards are separate `return`s, not an `||`: each has its own
//     jump to the epilogue, and the epilogue they reach is *past* the
//     record store, not a shared tail.
//   * the point is pushed into the object's own space through the second of
//     the two world-model 4x4s (+0x230), which is the pair's inverse — the
//     same matrix sm_GatherContainers uses for the same purpose, and an
//     independent confirmation of which of +0x1f0/+0x230 is which.
//   * `bsp_PointInside`'s two arguments are evaluated right to left with a
//     call in between, which is why `&vLocal` is pushed BEFORE the
//     `GetRoot()` that produces the first argument. Reading that push as
//     the virtual's own argument makes the virtual look like it takes one,
//     and it takes none (`ret`, not `ret $4`).
//   * the veto is `and %eax,0xc(%edi)` — three bytes rather than the four
//     an immediate zero would need, VC6 reusing the register the test
//     already proved to be zero. It is a plain store of zero in source.
//   * the source point goes through a named `pSrc` local. server.dll calls
//     MatVMul_H (0x1000f878) either way; lithtech.exe expands it
//     (0x004355fd..0x004356a6) and every row takes its z product before x
//     and y last. With `&pQuery->m_vPoint` passed straight in, /O2 took y
//     first and z last (16 differing instructions, zero mnemonic blocks);
//     the named pointer flips that tie-break to the shipped order.
void world_PointInsideCB(LTObject* pObj, CPointQuery* pQuery)
{
	LTVector vLocal;

	if (pObj->m_Unknown54 != 0)
		return;

	if (!pObj->_v34())
		return;

	LTVector* pSrc = &pQuery->m_vPoint;
	MatVMul_H(&vLocal, &((WorldModelObject*)pObj)->m_Transform230, pSrc);

	if (!bsp_PointInside(pObj->m_pWorldBsp->GetRoot(), &vLocal))
		pQuery->m_bInside = 0;
}


// 0x10012cf3 — is this point inside the world at all? (name ours)
//
// The container is asked for whatever is at the point and answers through a
// callback; the query record carries both the point and the answer, and the
// answer starts as "inside". 0x10012c9c is the only thing that ever changes
// it, and only in one direction: it runs a test through each object it is
// handed and, when that comes back zero, does `and %eax,0xc(%edi)` on the
// record — one dword past the twelve bytes of the point, with eax already
// zero. So a point nothing rejects stays inside, and the callback's argument
// is the record itself.
LTBOOL world_IsPointInside(CUnknown210* pWorld, LTVector* pPoint)
{
	CPointQuery query;

	query.m_vPoint = *pPoint;
	query.m_bInside = 1;

	pWorld->QueryPoint(*pPoint, [&](LTObject* object) { world_PointInsideCB(object, &query); });

	return query.m_bInside;
}


