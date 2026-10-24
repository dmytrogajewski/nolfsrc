#include "world_state.h"
using CWorldGeom = CWorldBsp;
#include "world_lighting.h"
// server.dll — the shared ILTModel tracker accessors (reconstruction). Build /O1.
#include "world_record.h"
#include <vector>
#include "world_polygon.h"
#include "world_node.h"
#include "world_object_record.h"
#include "engine/portable/string.h"
#include "platform/system.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <cmath>
#include "commonmodel.h"
#include "command_parser.h"
#include "ltquatbase.h"
#include "de_error.h"
// CModelLoadRequest, which LoadChildModels below both reads and fills in for
// each child it asks for.
#include "model_loading.h"
#include "model_file.h"

// ltquatbase.cpp's own file-scope table, at 0x10052ed0: { QY, QZ, QX }.
static int g_QNext[3] = { QY, QZ, QX };

// 0x10052ee0 — the float behind the "DefaultDrawIndexedDist" console
// variable (dsi.cpp's binding table, entry 0x1005232c), 500.0f (0x43fa0000)
// in the shipped .data. It follows 0x10052edc (commonmodel.h's
// g_pEmptyFilename) in this unit's .data (name ours).
// lithtech.exe's twin is 0x004abfc4 (500.0f), in the same place right
// after its g_pEmptyFilename (0x004abfc0), so both images define it.
extern float g_CV_DefaultDrawIndexedDist;

// The four name lookups. Note the two macro forms sitting 200 bytes apart in
// one file: GetSocket and GetNode pass the name as a literal and the code as
// its number, GetPiece and FindWeightSet hold the name in a pointer and report
// the code symbolically. Three check sites each, so the difference is the
// three bytes between 225 and 228.

LTRESULT CommonModelLT::GetSocket(HOBJECT hObj, char* pSocketName, HMODELSOCKET& hSocket)
{
	hSocket = INVALID_MODEL_SOCKET;

	if (!hObj || !pSocketName || hObj->m_ObjectType != OT_MODEL)
		RETURN_ERROR(2, "ILTModel::GetSocket", LT_INVALIDPARAMS);

	Model* pModel = ((ModelInstance*)hObj)->m_AnimTracker.m_TimeRef.m_pModel;
	if (!pModel)
		RETURN_ERROR(1, "ILTModel::GetSocket", LT_NOTINITIALIZED);

	if (pModel->FindSocket(pSocketName, &hSocket))
		return LT_OK;

	RETURN_ERROR(1, "ILTModel::GetSocket", LT_NOTFOUND);
}

LTRESULT CommonModelLT::GetNode(HOBJECT hObj, char* pNodeName, HMODELNODE& hNode)
{
	hNode = INVALID_MODEL_NODE;

	if (!hObj || !pNodeName || hObj->m_ObjectType != OT_MODEL)
		RETURN_ERROR(2, "ILTModel::GetNode", LT_INVALIDPARAMS);

	Model* pModel = ((ModelInstance*)hObj)->m_AnimTracker.m_TimeRef.m_pModel;
	if (!pModel)
		RETURN_ERROR(1, "ILTModel::GetNode", LT_NOTINITIALIZED);

	if (pModel->FindNode(pNodeName, &hNode))
		return LT_OK;

	RETURN_ERROR(1, "ILTModel::GetNode", LT_NOTFOUND);
}

LTRESULT CommonModelLT::GetPiece(HOBJECT hObj, char* pPieceName, HMODELPIECE& hPiece)
{
	hPiece = INVALID_MODEL_PIECE;

	if (!hObj || !pPieceName || hObj->m_ObjectType != OT_MODEL)
		RETURN_ERROR_NAMED(2, "ILTModel::GetPiece", LT_INVALIDPARAMS);

	Model* pModel = ((ModelInstance*)hObj)->m_AnimTracker.m_TimeRef.m_pModel;
	if (!pModel)
		RETURN_ERROR_NAMED(1, "ILTModel::GetPiece", LT_NOTINITIALIZED);

	if (pModel->FindPiece(pPieceName, &hPiece))
		return LT_OK;

	RETURN_ERROR_NAMED(1, "ILTModel::GetPiece", LT_NOTFOUND);
}

// The SDK calls this FindWeightSet; the shipped check literal says
// "GetWeightSet", which is also the name of the tracker accessor further down.
LTRESULT CommonModelLT::FindWeightSet(HOBJECT hObj, char* pSetName, HMODELWEIGHTSET& hSet)
{
	hSet = INVALID_MODEL_WEIGHTSET;

	if (!hObj || !pSetName || hObj->m_ObjectType != OT_MODEL)
		RETURN_ERROR_NAMED(2, "ILTModel::GetWeightSet", LT_INVALIDPARAMS);

	Model* pModel = ((ModelInstance*)hObj)->m_AnimTracker.m_TimeRef.m_pModel;
	if (!pModel)
		RETURN_ERROR_NAMED(1, "ILTModel::GetWeightSet", LT_NOTINITIALIZED);

	if (pModel->FindWeightSet(pSetName, &hSet))
		return LT_OK;

	RETURN_ERROR_NAMED(1, "ILTModel::GetWeightSet", LT_NOTFOUND);
}

LTRESULT CommonModelLT::GetMainTracker(HOBJECT hObj, LTAnimTracker*& pTracker)
{
	pTracker = LTNULL;

	if (!hObj || hObj->m_ObjectType != OT_MODEL)
		RETURN_ERROR_NAMED(2, "ILTModel::GetMainTracker", LT_INVALIDPARAMS);

	pTracker = &((ModelInstance*)hObj)->m_AnimTracker;
	return LT_OK;
}

LTRESULT CommonModelLT::GetPlaybackState(LTAnimTracker* pTracker, uint32& flags)
{
	flags = 0;

	if (!pTracker)
		RETURN_ERROR_NAMED(2, "ILTModel::GetPlaybackState", LT_INVALIDPARAMS);

	flags = 0;
	if (at_IsDone(pTracker))
		flags |= MS_PLAYDONE;

	return LT_OK;
}

LTRESULT CommonModelLT::GetCurAnim(LTAnimTracker* pTracker, HMODELANIM& hAnim)
{
	if (!pTracker)
		RETURN_ERROR_NAMED(2, "ILTModel::GetCurAnim", LT_INVALIDPARAMS);

	hAnim = pTracker->m_TimeRef.m_Cur.m_iAnim;
	return LT_OK;
}

LTRESULT CommonModelLT::ResetAnim(LTAnimTracker* pTracker)
{
	if (!pTracker)
		RETURN_ERROR_NAMED(2, "ILTModel::ResetAnim", LT_INVALIDPARAMS);

	at_ResetAnim(pTracker);
	return LT_OK;
}

LTRESULT CommonModelLT::GetLooping(LTAnimTracker* pTracker)
{
	if (!pTracker)
		RETURN_ERROR_NAMED(2, "ILTModel::GetLooping", LT_INVALIDPARAMS);

	if (pTracker->m_Flags & AT_LOOPING)
		return LT_YES;

	return LT_NO;
}

LTRESULT CommonModelLT::SetLooping(LTAnimTracker* pTracker, LTBOOL bLooping)
{
	if (!pTracker)
		RETURN_ERROR_NAMED(2, "ILTModel::SetLooping", LT_INVALIDPARAMS);

	if (bLooping)
		pTracker->m_Flags |= AT_LOOPING;
	else
		pTracker->m_Flags &= ~AT_LOOPING;

	return LT_OK;
}

LTRESULT CommonModelLT::GetPlaying(LTAnimTracker* pTracker)
{
	if (!pTracker)
		RETURN_ERROR_NAMED(2, "ILTModel::GetPlaying", LT_INVALIDPARAMS);

	if (pTracker->m_Flags & AT_PLAYING)
		return LT_YES;

	return LT_NO;
}

LTRESULT CommonModelLT::SetPlaying(LTAnimTracker* pTracker, LTBOOL bPlaying)
{
	if (!pTracker)
		RETURN_ERROR_NAMED(2, "ILTModel::SetPlaying", LT_INVALIDPARAMS);

	if (bPlaying)
		pTracker->m_Flags |= AT_PLAYING;
	else
		pTracker->m_Flags &= ~AT_PLAYING;

	return LT_OK;
}

LTRESULT CommonModelLT::GetWeightSet(LTAnimTracker* pTracker, HMODELWEIGHTSET& hSet)
{
	if (!pTracker)
		RETURN_ERROR_NAMED(2, "ILTModel::GetWeightSet", LT_INVALIDPARAMS);

	hSet = pTracker->m_TimeRef.m_iWeightSet;
	return LT_OK;
}

LTRESULT CommonModelLT::SetWeightSet(LTAnimTracker* pTracker, HMODELWEIGHTSET hSet)
{
	if (!pTracker)
		RETURN_ERROR_NAMED(2, "ILTModel::SetWeightSet", LT_INVALIDPARAMS);

	pTracker->m_TimeRef.m_iWeightSet = hSet;
	return LT_OK;
}

LTRESULT CommonModelLT::GetCurAnimTime(LTAnimTracker* pTracker, uint32& curTime)
{
	curTime = 0;

	if (!pTracker)
		RETURN_ERROR_NAMED(2, "ILTModel::GetCurAnimTime", LT_INVALIDPARAMS);

	curTime = pTracker->m_TimeRef.m_Cur.m_Time;
	return LT_OK;
}

LTRESULT CommonModelLT::SetCurAnimTime(LTAnimTracker* pTracker, uint32 curTime)
{
	if (!pTracker)
		RETURN_ERROR_NAMED(2, "ILTModel::SetCurAnimTime", LT_INVALIDPARAMS);

	pTracker->m_TimeRef.m_Cur.m_Time = curTime;
	return LT_OK;
}

LTRESULT CommonModelLT::GetPieceHideStatus(HOBJECT hObj, HMODELPIECE hPiece, LTBOOL& bHidden)
{
	if (!hObj || hObj->m_ObjectType != OT_MODEL)
		RETURN_ERROR_NAMED(2, "ILTModel::GetPieceHideStatus", LT_INVALIDPARAMS);

    auto* model = static_cast<ModelInstance*>(hObj)->m_AnimTracker.GetModel();
    if (!model) return LT_NOTINITIALIZED;
    if (hPiece >= 32 || hPiece >= model->m_Pieces.GetSize()) return LT_INVALIDPARAMS;
	bHidden = (((ModelInstance*)hObj)->m_HiddenPieces & (uint32{1} << hPiece)) != 0;
	return LT_OK;
}

LTRESULT CommonModelLT::SetPieceHideStatus(HOBJECT hObj, HMODELPIECE hPiece, LTBOOL bHidden)
{
	if (!hObj || hObj->m_ObjectType != OT_MODEL)
		RETURN_ERROR_NAMED(2, "ServerModelLT::SetPieceHideStatus", LT_INVALIDPARAMS);

    auto* model = static_cast<ModelInstance*>(hObj)->m_AnimTracker.GetModel();
    if (!model) return LT_NOTINITIALIZED;
    if (hPiece >= 32 || hPiece >= model->m_Pieces.GetSize()) return LT_INVALIDPARAMS;

	// The requested state is normalized to 0/1 once and kept: the shipped code
	// holds it in a callee-saved register and tests that, rather than reading
	// the parameter again for the second branch. There is no local for the
	// hidden-piece word either — reading it into one gives it a callee-saved
	// register of its own and costs six bytes.
	//
	// IDENTICAL on both images (server.dll 145, lithtech.exe 0x44ed60 168).
	// The mask is written out in the TEST and only named, below the early
	// return, for the two stores. With `dwMask = 1 << hPiece` declared up
	// front and used in the test too, server.dll came out one whole-register
	// rotation off (hObj in %edx instead of %esi, `push %edi` below the type
	// check); the rotation is the mask's live range reaching back over the
	// test. Measured alongside (server/lithtech): mask written out at all
	// three uses -- server identical, but Model::sub_1001c36c (0x1001c36c)
	// flips to a two-load near, TU-global allocation; mask named for the test
	// and written out in the stores -- server identical, lithtech
	// 0x44ed60 lost; a `bOld` local -- lithtech 0x44ed60 lost; reusing the
	// bHidden parameter for the normalised flag -- no change, and
	// 0x1001c36c lost; operand order of the compare, compound |= / &=, and
	// `!!bHidden` -- no change.
	ModelInstance* pInst = (ModelInstance*)hObj;
	LTBOOL bNewHidden = (bHidden != 0);
	uint32 dwMask;

	if (bNewHidden == ((pInst->m_HiddenPieces & (uint32{1} << hPiece)) != 0))
		return LT_NOCHANGE;

	dwMask = (uint32{1} << hPiece);
	if (bNewHidden)
		pInst->m_HiddenPieces = pInst->m_HiddenPieces | dwMask;
	else
		pInst->m_HiddenPieces = ~dwMask & pInst->m_HiddenPieces;

	return LT_OK;
}

LTRESULT CommonModelLT::GetInstanceHookFn(HOBJECT hObj, ModelInstanceHookFn& fn)
{
	if (!hObj || hObj->m_ObjectType != OT_MODEL)
		RETURN_ERROR_NAMED(2, "ILTModel::GetModelInstanceHookFn", LT_INVALIDPARAMS);

	fn = ((ModelInstance*)hObj)->m_HookFn;
	return LT_OK;
}

LTRESULT CommonModelLT::SetInstanceHookFn(HOBJECT hObj, ModelInstanceHookFn fn)
{
	if (!hObj || hObj->m_ObjectType != OT_MODEL)
		RETURN_ERROR_NAMED(2, "ILTModel::SetModelInstanceHookFn", LT_INVALIDPARAMS);

	((ModelInstance*)hObj)->m_HookFn = fn;
	return LT_OK;
}

LTRESULT CommonModelLT::GetCurAnimLength(LTAnimTracker* pTracker, uint32& length)
{
	ModelAnim* pAnim;

	length = 0;

	if (!pTracker)
		RETURN_ERROR_NAMED(2, "ILTModel::GetCurAnimLength", LT_INVALIDPARAMS);

	pAnim = pTracker->GetCurAnim();
	if (!pAnim)
		RETURN_ERROR_NAMED(1, "ILTModel::GetCurAnimLength", LT_NOTINITIALIZED);

	length = pAnim->GetLength();
	return LT_OK;
}

LTRESULT CommonModelLT::SetCurAnim(LTAnimTracker* pTracker, HMODELANIM hAnim)
{
	// The instance is read before the checks, not after — the same shape the
	// six ServerModelLT overrides use.
	ModelInstance* pInst = pTracker ? pTracker->GetModelInstance() : nullptr;
	if (!pTracker || !pInst)
		RETURN_ERROR_NAMED(2, "ILTModel::SetCurAnim", LT_INVALIDPARAMS);

	at_SetCurAnim(pTracker, hAnim, (pInst->m_Flags & FLAG_ANIMTRANSITION) != 0);
	return LT_OK;
}

// AddTracker and RemoveTracker share one name variable across two check sites
// each — the shipped code pushes the same pointer at both — and the two sites
// report the code differently, one symbolically and one as its number.

LTRESULT CommonModelLT::AddTracker(HOBJECT hObj, LTAnimTracker* pTracker)
{
	static char* pFnName = "ILTModel::AddTracker";
	LTAnimTracker** ppLink;

	if (!hObj || hObj->m_ObjectType != OT_MODEL || !pTracker)
		RETURN_ERROR_SYM(2, pFnName, LT_INVALIDPARAMS);

	if (hObj->FindTracker(pTracker, &ppLink))
		RETURN_ERROR(3, pFnName, LT_ALREADYEXISTS);

	// Walk to the end of the object's tracker list and link it on.
	ModelInstance* pInst = (ModelInstance*)hObj;
	LTAnimTracker** ppTail = &pInst->m_pTrackerList;
	while (*ppTail)
		ppTail = (LTAnimTracker**)&(*ppTail)->m_Link.m_pNext;

	// The data store comes FIRST: the shipped /O2 body stores the object at
	// +0x08 and only then zeroes the link at +0x04. /O1 emits the pair the
	// other way round because it sinks the cheap `andl $0x0,0x4(%ebx)` into
	// the slot behind the call and lets the stack adjust follow it, which is
	// a peephole and not the source order.
	at_Init(pTracker, pInst->m_AnimTracker.m_TimeRef.m_pModel, LTFALSE);
	pTracker->m_Link.m_pData = hObj;
	pTracker->m_Link.m_pNext = LTNULL;
	*ppTail = pTracker;

	return LT_OK;
}

LTRESULT CommonModelLT::RemoveTracker(HOBJECT hObj, LTAnimTracker* pTracker)
{
	static char* pFnName = "ILTModel::RemoveTracker";
	LTAnimTracker** ppLink;

	// Two RETURN_ERROR_SYM sites with the same body, not one four-term test:
	// the three argument checks are a POSITIVE guard whose failure is the
	// function's trailing statement (all three jump forward past the
	// NOTFOUND block to the very last block), while the main-tracker test
	// is a separate `if` inside the guard and gets its own copy, emitted
	// inline where the test falls through and cross-jumped into the
	// trailing copy's body. /O1 tail-merges the two copies back into one,
	// which is why server.dll shows a single shared block either way.
	if (hObj && hObj->m_ObjectType == OT_MODEL && pTracker)
	{
		// The main tracker is part of the instance and cannot be removed.
		if (pTracker == &((ModelInstance*)hObj)->m_AnimTracker)
			RETURN_ERROR_SYM(2, pFnName, LT_INVALIDPARAMS);

		// FindTracker hands back the address of the pointer that links the
		// tracker, so unlinking is one store.
		if (hObj->FindTracker(pTracker, &ppLink))
		{
			*ppLink = pTracker->GetNext();
            ++pTracker->m_MutationGeneration;
            pTracker->m_Link.m_pData = nullptr;
            pTracker->m_Link.m_pNext = nullptr;
			return LT_OK;
		}

		RETURN_ERROR(3, pFnName, LT_NOTFOUND);
	}

	RETURN_ERROR_SYM(2, pFnName, LT_INVALIDPARAMS);
}


// ---- the node and socket transforms (0x1001d478, 0x1001d77f) ------------- //
//
// IDENTICAL in both images (lithtech.exe 0x0044ee10 / 0x0044f3d0) with the
// SDK's own spellings at the end of each: `MatMul_InPlace(&mObj, &mNode)`
// for the product (its temp lives in the inlined helper's scope, so it
// shares frame space with Inverse's scratch: 0x360, not 0x3a0) and
// `GetTranslation` + `m_Rot.ConvertFromMatrix` for the output.  Those extra
// inline levels spend the /O2 budget that otherwise went on the walker's
// AnimTimeRef::Init -- shipped CALLS it (0x00447080) in the eight-element
// loop.  server.dll is unchanged by either.
//
// Both do the same three things: build the object's transform (identity when
// the caller wants model space and the walker says the model is not already
// world-relative), evaluate the node or socket through the walker, and then
// pull a position and a quaternion out of the 4x4 that comes back. Both name
// themselves through a static pointer shared by all three of their check
// sites, which is why the shipped code pushes `[mem]` where a literal would
// push an immediate.
//
// LTMatrix::Identity, Inverse and Normalize are calls here, not inlined
// bodies: the same split the engine's ltvector.h shows for Mag/MagSqr — the
// class carries their declarations and the definitions live in one place.

// 0x100175b6 — the walker's constructor. Everything it does is
// initialization: eight AnimTimeRefs built by the array constructor VC6
// generates (each one -1 in its weight set and then cleared, which is the
// SDK's inline AnimTimeRef::AnimTimeRef), then six words of walk state. The
// order of those six is the source's own — 0xe0 last, after the five above
// it.
//
// It sits HERE, above its two callers, and not down with the rest of the
// walk: VC6's inliner is forward-only, and both transforms expand this
// constructor rather than calling it. The shipped /O2 bodies open with the
// eight-element loop (`mov $0x8,%edi ... dec %edi; jne`) and the six
// zero-stores in line, and reuse the zero the stores left in %eax for the
// argument checks (`xor %eax,%eax; cmp %eax,%ebp` where a standalone call
// leaves `test %ebp,%ebp`).
//
// `inline` is load-bearing, not decoration, and it is the one knob to flip if
// server.dll moves. BOTH engine builds are /Ob1, so the only functions VC6
// will ever expand are the ones marked `inline` or written inside a class
// body — hoisting alone buys nothing. server.dll is /O1 (= /Os) and declines
// this expansion, which is why its standalone copy at 0x100175b6 is still
// called and still matches (a declined site forces the COMDAT out anyway);
// lithtech.exe is /O2 (= /Ot) and takes it, which is why our 91-byte
// ??0ModelWalker COMDAT has no home anywhere in the shipped image.
LTRESULT CommonModelLT::GetNodeTransform(HOBJECT hObj, HMODELNODE hNode,
                                         LTransform& transform, LTBOOL bWorldSpace)
{
	static char* pErrFnName = "ILTModel::GetNodeTransform";

	ModelWalker walker;
	LTMatrix    mObj;
	LTMatrix    mNode;

	if (!hObj || hObj->m_ObjectType != OT_MODEL)
		RETURN_ERROR_SYM(2, pErrFnName, LT_INVALIDPARAMS);

	Model* pModel = ((ModelInstance*)hObj)->m_AnimTracker.m_TimeRef.m_pModel;
	if (!pModel || hNode >= pModel->m_NodeTransforms.m_nElements)
		RETURN_ERROR(1, pErrFnName, LT_NOTINITIALIZED);

	hObj->SetupModelWalker(&walker);

	if (!bWorldSpace && walker.m_Unknown_ec == 0)
		mObj.Identity();
	else
		hObj->GetTransformMatrix(&mObj);

	walker.m_Unknown_e8 = 0;
	walker.m_pObjTransform = &mObj;

	// Two check sites with the same body: the shipped code keeps one copy,
	// inline where the second one is, and jumps the first into it.
	if (!walker.Evaluate(hNode))
		RETURN_ERROR(1, pErrFnName, LT_ERROR);

	mNode = pModel->m_NodeTransforms.m_pArray[hNode];

	if (!bWorldSpace && walker.m_Unknown_ec)
	{
		if (!mObj.Inverse())
			RETURN_ERROR(1, pErrFnName, LT_ERROR);

		MatMul_InPlace(&mObj, &mNode);
	}

	if (hObj->m_Scale.x != 1.0f || hObj->m_Scale.y != 1.0f || hObj->m_Scale.z != 1.0f)
		mNode.Normalize();

	mNode.GetTranslation(transform.m_Pos);
	transform.m_Rot.ConvertFromMatrix(mNode);
	return LT_OK;
}

LTRESULT CommonModelLT::GetSocketTransform(HOBJECT hObj, HMODELSOCKET hSocket,
                                           LTransform& transform, LTBOOL bWorldSpace)
{
	static char* pErrFnName = "ILTModel::GetSocketTransform";

	ModelWalker walker;
	LTMatrix    mObj;
	LTMatrix    mSocket;

	if (!hObj || hObj->m_ObjectType != OT_MODEL)
		RETURN_ERROR_SYM(2, pErrFnName, LT_INVALIDPARAMS);

	Model* pModel = ((ModelInstance*)hObj)->m_AnimTracker.m_TimeRef.m_pModel;
	if (!pModel)
		RETURN_ERROR(1, pErrFnName, LT_NOTINITIALIZED);

	// A handle past the end of the socket table is a node index biased past
	// it — the convention CreateAttachment writes.
	if (hSocket >= pModel->m_Sockets.m_nElements)
	{
		return GetNodeTransform(hObj, hSocket - pModel->m_Sockets.m_nElements,
		                                               transform, bWorldSpace);
	}

	hObj->SetupModelWalker(&walker);

	if (!bWorldSpace && walker.m_Unknown_ec == 0)
		mObj.Identity();
	else
		hObj->GetTransformMatrix(&mObj);

	walker.m_pObjTransform = &mObj;

	if (!pModel->EvalSocket(&walker, hSocket, &mSocket))
		RETURN_ERROR(1, pErrFnName, LT_ERROR);

	if (!bWorldSpace && walker.m_Unknown_ec)
	{
		if (!mObj.Inverse())
			RETURN_ERROR(1, pErrFnName, LT_ERROR);

		MatMul_InPlace(&mObj, &mSocket);
	}

	if (hObj->m_Scale.x != 1.0f || hObj->m_Scale.y != 1.0f || hObj->m_Scale.z != 1.0f)
		mSocket.Normalize();

	mSocket.GetTranslation(transform.m_Pos);
	transform.m_Rot.ConvertFromMatrix(mSocket);
	return LT_OK;
}


// ---- the model node and animation name readers (0x100135e0-0x1001372b) -- //
//
// All three report themselves as "GetNextModelNode" — the name literal was
// copied along with the check when the other two were written, and the bytes
// say so.

LTRESULT ci_GetNextModelNode(HOBJECT hObject, HMODELNODE hNode, HMODELNODE* pNext)
{
	Model* pModel;
	LTRESULT dRes;

	if (!hObject || hObject->m_ObjectType != OT_MODEL)
		RETURN_ERROR(1, "GetNextModelNode", LT_INVALIDPARAMS);

	pModel = ((ModelInstance*)hObject)->m_AnimTracker.m_TimeRef.m_pModel;

	// The invalid handle is the "start at the beginning" handle, and the
	// shipped code says so in an unusual way: it re-assigns -1 to a value it
	// has just proved is -1 and then increments unconditionally, so the two
	// arms share one `inc`. The redundant assignment survives as a two-byte
	// `or %eax,%eax` — VC6 emits the `xor %eax,%eax` that zeroes it, then
	// patches the opcode byte 0x31 to 0x0b to neutralise the store once it
	// knows the register already holds -1. Writing the arms as `iNext = 0`
	// / `iNext = hNode + 1` instead costs two bytes: our build materialises
	// the zero and jumps over the increment.
	//
	// The result goes through a local assigned on both arms and returned once
	// at the end. With the two early `return`s instead, /O1 swapped the two
	// caller-saved registers against 0x100135f1: the shipped build reads the
	// model into %ecx and then the handle over the dead %eax, ours the other
	// way round, eight instructions differing. The result local gives the
	// shipped assignment on server.dll. lithtech.exe's /O2 copy (0x436370)
	// wants the OPPOSITE assignment (handle in %ecx, model in %eax) and stays
	// at eleven differing instructions under every spelling tried, the same
	// as before this edit.
	if (hNode == INVALID_MODEL_NODE)
		hNode = (HMODELNODE)-1;

	hNode++;

	if (hNode >= pModel->m_NodeTransforms.m_nElements)
		dRes = LT_FINISHED;
	else
	{
		*pNext = hNode;
		dRes = LT_OK;
	}

	return dRes;
}

// TWO separate RETURN_ERROR statements, not one condition list, and only the
// /O2 image can tell: 0x4363f0 carries the macro expanded TWICE — once at
// 0x436420 for the node-index test and once at 0x436455 for the four handle
// tests — with the first jumping into the second's printer (`jmp 0x436469`)
// and both sharing the `jl 0x436486` return. /O1 cross-jumps the two
// identical blocks back into the single 0x10013690 that server.dll carries,
// which is why the merged spelling passes there and fails here. The model is a
// named local for the same reason it is in ci_GetNextModelNode above: the
// shipped code keeps it in %eax across the bounds test and takes m_pArray into
// %edx, where the inline chain clobbers %eax with the array instead.
LTRESULT ci_GetModelNodeName(HOBJECT hObject, HMODELNODE hNode, char* pName, uint32 maxLen)
{
	Model* pModel;

#ifdef NOLFSRC_MODERN
	// fixed: ci_GetModelNodeName refuses node 0 and does not terminate
	// (RETAIL_BUGS.md). Node 0 is a valid node; only the bound below can
	// refuse a node handle.
	if (!maxLen || !hObject || hObject->m_ObjectType != OT_MODEL)
#else
	if (!hNode || !maxLen || !hObject || hObject->m_ObjectType != OT_MODEL)
#endif
	{
#ifdef NOLFSRC_MODERN
		RETURN_ERROR(1, "GetModelNodeName", LT_INVALIDPARAMS);
#else
		RETURN_ERROR(1, "GetNextModelNode", LT_INVALIDPARAMS);
#endif
	}

	pModel = ((ModelInstance*)hObject)->m_AnimTracker.m_TimeRef.m_pModel;

	if (hNode >= pModel->m_NodeTransforms.m_nElements)
	{
#ifdef NOLFSRC_MODERN
		RETURN_ERROR(1, "GetModelNodeName", LT_INVALIDPARAMS);
#else
		RETURN_ERROR(1, "GetNextModelNode", LT_INVALIDPARAMS);
#endif
	}

	strncpy(pName, pModel->m_Nodes.m_pArray[hNode]->m_pName, maxLen - 1);
#ifdef NOLFSRC_MODERN
	pName[maxLen - 1] = '\0';
#endif
	return LT_OK;
}

const char* ci_GetAnimName(HOBJECT hObject, HMODELANIM hAnim)
{
	Model* pModel;

	if (!hObject || hObject->m_ObjectType != OT_MODEL)
		return LTNULL;

	pModel = ((ModelInstance*)hObject)->m_AnimTracker.m_TimeRef.m_pModel;

	if (hAnim >= pModel->m_AnimArray.m_nElements)
		return LTNULL;

	return pModel->m_AnimArray.m_pArray[hAnim].m_pAnim->m_pName;
}


// 0x100138b4 — a uniform float in [min, max). The scale is 1/32767, the same
// constant si_RandomScale uses.
LTFLOAT ci_Random(LTFLOAT min, LTFLOAT max)
{
	return (rand() * (1.0f / 32767.0f)) * (max - min) + min;
}


// 0x10013899 — walk a file list off and free it. It sits in the same shipped
// block as the readers above, which our reconstruction currently splits
// between this file and stringbank.cpp; the block's own translation unit has
// not been identified.



// commonmodel.h's two model globals, defined here, in the module that owns
// them: the pointer sits in this file's own .data run (0x10052edc, directly
// before the 500.0f and the "ShadowCenterOffset" this file reads), and its
// shipped value is 0x10056ff4 -- the pooled "" literal, the same zero byte
// savrest.cpp and servermessage.cpp reach as g_szNoName (aliases.rsp).
char*  g_pEmptyFilename = "";               // 0x10052edc
std::atomic_size_t g_nModelDataBytes{0};                   // 0x10058724 (uninitialised)

// 0x10016cc5 — build a model. Fourteen sub-objects run first (thirteen
// constructors and one float initialiser), then the vptr, then a body that is
// almost entirely constants.
//
// NEAR, 567 of 581 bytes, and the fourteen missing bytes are one inline
// decision that no VC6 in toolchains/ will make. The shipped build expanded
// `CMoArray<T, NoCache>::CMoArray(uint32)` inline at the first temporary —
// vptr store, m_pArray and m_nElements zeroed, then a call to the
// out-of-line `Init(0, 0)` (0x10019903) — where ours emits a call to the
// 54-byte constructor COMDAT. Everything else in the function is byte-exact
// modulo the ebx/edi swap that inline causes: the shipped code reuses the
// dying pAlloc1 register for the zero constant and allocates ebx to
// &g_DefAlloc, and ours does the reverse because the register dies later.
//
// What was ruled out, so that nobody re-runs it:
//   * cl 8168, 8447, 8804 and 8966 all refuse the same inline (8804 and
//     8966 also cost the file eight other functions, so 8447 stands).
//   * /Ot in any combination is catastrophic here (40 of 221), so the file
//     is /O1 with /Os as the rest of server.dll is.
//   * /Ob2 DOES produce this call site byte-for-byte, registers included,
//     but it also inlines Model::sub_10017add, Model::SetFilename and
//     Model::Load's callees, costing three functions that are matched now.
//     So the file is /Ob1 and the shipped compiler simply had one more
//     unit of inline budget than ours.
//   * The gate is a size gate and it sits exactly one store below what
//     this constructor needs. Probes in this file (since deleted) put a
//     class with `vptr + one store + call` on the inlined side and
//     `vptr + two stores + call` on the called side, whether the class is
//     a template or not, whether it has a base or not, and whether the two
//     stores are written directly or through an inline Clear(). The
//     shipped expansion is the second shape.
//   * Neither `m_Nodes = CMoArray<...>(0)` (a temporary) nor a block-scoped
//     named local changes the decision; the named local is kept because it
//     is what the SECOND temporary's `lea` proves (see the header).
//
// lithtech.exe /O2 (0x00446360) is 773 of 732, and it is the MIRROR of the
// above: there the shipped build inlines LESS than we do, not more. Its
// Model::Model calls every collection helper out of line -- 0x0044b980 five
// times, 0x0044bf20, 0x0044c100, 0x0044c2a0, 0x0044c500, 0x0044c810 -- and
// the temporary's teardown at 0x004464e7 is a call to the standalone
// `CMoArray<T,NoCache>::SetSize2` at 0x0044b6b0, where our /O2 build expands
// that body in line (the `cmp %edi,%eax / je` early-out and the BaseDelete
// arm that follow it). The same standalone SetSize2 is what the shipped
// LoadChildModels calls for m_Pieces at 0x0044dff6 while we inline it there
// too, so ONE decision explains both /O2 nears in this file. It is the /Ob1
// budget again, in the opposite direction from the /O1 case, and the only
// lever found for it is collection.h -- which reaches the whole tree, so it
// was not pulled.
//
// The decision depends on T, not on the template body. The same shipped
// function CALLS the LTMatrix (uint32) ctor at 0x10016e8d (0x10018ec3, Init
// expanded inside it), and 0x100165e5 calls the CModelXform one (0x10017ce0),
// while the ModelNode one is expanded here with Init left as a call. So no
// edit to the ctor's own spelling can be the answer. The ModelNode vtable
// 0x1004da34 stored here is also the one ModelNode's +0x18 child array
// gets (default ctor 0x1001831d, called from 0x10016954/0x10016996), which
// points at m_Nodes and m_Array18 being one instantiation in the original.
// Measured, and none of these moves this site at /Ob1 (still 567):
//   * the ctor written out of class, before or after Init; Init marked
//     `inline`; `Init(0)` instead of `Init(0, cacheSize)`; Clear() replaced
//     by one or two direct stores.
//   * Model::Model moved to the top or the bottom of this file, or compiled
//     alone in its own TU; an earlier or later function using the ctor
//     or Init<ModelNode*> directly; the temporary retyped to
//     CMoArray<ModelNode*, NoCache>.
//   * `#pragma inline_depth(n)`. It only takes effect as the value at the
//     END of the TU. n=1 gives the shipped shape here but costs the file
//     400 -> 246; n=2 gives 351; n>=3 changes nothing.
//   * `NOLF_FORCE_INLINE` on the ctor: expands Init too, and calls _InitArray
//     (630 bytes, file 396).
//   * Every member collection.h declares but never defines, given a body:
//     TermPointers/CopyPointers (in class and out of class), Compare,
//     operator+, AppendArray, SetSizeInit/2/4, and the (copyFrom, toAdd)
//     ctor written in class. No change.
//   * The ctor body `Clear(); SetSize2(0, &g_DefAlloc);`, with no nested
//     inline candidate, IS expanded here. So the gate is the ctor's size
//     AFTER Init has been expanded into it.
// At /Ob2 the expansion needs the rest of this file present: with only
// Model::Model in the TU it is still a call. Dropping either half of the
// file, or ModelNode::ModelNode() by itself, also makes it a call.
//
// Both parameters are stored as they arrive: the first also builds the data
// reference at +0x84, and both call sites hand it the same allocator twice.
// The casts are ours — 0x174 is the field Load later points at m_DataBlock,
// so its type comes from there and not from here.
Model::Model(LAlloc* pAlloc1, LAlloc* pAlloc2)
	: m_DataRef(pAlloc1),
	  m_f0a0(0.0f)
{
	m_pDataBlock = pAlloc1;
    m_FileFlags = 0xffff;
	m_pAlloc     = pAlloc2;

	m_f0a4 = 96.0f;

	m_pSelf010 = this;

	m_pFilename = g_pEmptyFilename;

	m_Unknown064 = 0;
	m_Flags      = 0;
	m_CacheCount = 0;

	{
		CMoArray<ModelNode*, NoCache> temp(0);

		m_Nodes = temp;
	}

	m_Unknown06c = 0;
	m_f0a8       = 50.0f;
	m_Unknown0ac = 0;

	sub_10017add(20000.0f, 20000.0f);

	m_Unknown0c0 = 0;
	m_f0cc       = 0.25f;
	m_f0c4 = m_f0c8 = 100.0f;
	m_f0d0       = 0.75f;
	m_Unknown140 = 0;
	m_f0dc = m_f0d8 = 200.0f;
	m_Unknown14c = 0;
	m_Unknown0d4 = 0;
	m_f0e4 = m_f0e0 = 50.0f;

	m_Vec0e8.Init();

	m_n0f4 = -1;
	m_n0f8 = -1;

	m_Unknown150 = 0;
	m_Unknown13c = 0;

	{
		CMoArray<LTMatrix, NoCache> temp(0);

		m_NodeTransforms = temp;
	}

	m_pCommandString = "";

	m_pSub20c = &m_Sub20c;
	m_pSub20c->SetModel(this);

	m_pChildModels[0] = &m_Sub1d4;
	m_nChildModels    = 1;
	m_pChildModels[0]->m_pOwner24 = this;
	m_pChildModels[0]->m_pOwner28 = this;

	// 0x1003f9e1 is the CRT's rand(): _getptd(), then the 0x343fd/0x269ec3
	// LCG on the per-thread seed at +0x14, shifted down 16 and masked to
	// 0x7fff. The sub-object's +0x20 is the only field of it Model writes.
	m_Sub1d4.m_Unknown20 = rand();

	g_nModelDataBytes += sizeof(Model);
}


Model::~Model() {
    sub_10017016(LTTRUE);
    g_nModelDataBytes -= sizeof(Model);
    g_nModelDataBytes -= m_DataBlock.m_nSize;
}

// 0x100174b9 — the same search FindAnim does, restricted to one model. The
// model test comes FIRST and the name compare second, which matters: the
// shipped code only reaches stricmp for entries whose m_pModel already
// matched, so a model with many animations pays one pointer compare each and
// not one string compare each.
AnimInfo* Model::FindAnimInModel(char* pName, Model* pModel, uint32* pIndex)
{
	uint32 i;

	for (i = 0; i < m_AnimArray.m_nElements; i++)
	{
		AnimInfo*  pInfo = &m_AnimArray.m_pArray[i];
		ModelAnim* pAnim = pInfo->m_pAnim;

		if (pAnim->m_pModel == pModel &&
		    nolf::ascii_compare(pAnim->GetName(), pName) == 0)
		{
			if (pIndex)
				*pIndex = i;

			return pInfo;
		}
	}

	return LTNULL;
}


// 0x1001767a — every animation this model can reach, counted. The self test
// is against m_pChildModels[0] rather than against an index, which is what
// the shipped `cmp (%edi),%ecx` says: edi holds the base of the array for the
// whole walk and the comparison is pointer against pointer.
uint32 Model::GetTotalAnimCount(LTBOOL bIncludeSelf)
{
	uint32 nTotal = 0;
	uint32 i;
	Model* pModel;

	for (i = 0; i < m_nChildModels; i++)
	{
		pModel = m_pChildModels[i]->m_pOwner28;

		if (!pModel)
			continue;

		if (m_pChildModels[i] == m_pChildModels[0] && !bIncludeSelf)
			continue;

		nTotal += pModel->m_AnimArray.GetSize();
	}

	return nTotal;
}


// 0x10017add — two lengths in, each stored and squared. Named for its
// address; the constructor's call passes 20000.0f twice.
void Model::sub_10017add(float f1, float f2)
{
	m_f0b0 = f1;
	m_f0b4 = f1 * f1;
	m_f0b8 = f2;
	m_f0bc = f2 * f2;
}


// 0x10017016 — give back everything the model's data block owns. Seventeen
// statements in offset order, and between them they say which of the model's
// members own memory and which do not: the first sub-object's own release,
// the node record at +0x2c0 (freed only when it is not the embedded one), the
// embedded record's release, five pointer collections whose elements go back
// one at a time, three collections emptied outright, the data reference, the
// filename, and one counter cleared.
//
// The `m_pSub20c = &m_Sub20c` store lands between the two pushes of the call
// after it, which is VC6 scheduling an independent store into the argument
// setup; in source it sits where the value it repairs was invalidated.
//
// The spellings below are the /O2 inline budget, measured against
// lithtech.exe's twin (0x004467c0); server.dll (/O1) is the same bytes for
// every one of them. The pointer collections go through dynarray's own
// DeleteAndClearArray pair: the wrapper puts BaseDelete one level deeper,
// so it stays a call for pieces and weight sets (0x0044c740, 0x0044c770)
// while the sockets, through DeleteAndClearArray2, have theirs expanded to
// the bare Free. The four value arrays are emptied through Term(pAlloc),
// and only the last reads the allocator through GetAlloc(): with that one
// accessor the shipped split falls out exactly — SetSize2 called for the
// transforms and the vert records (0x0044c160, 0x0044bf80), expanded for
// the nodes down to a _DeleteAndDestroyArray call, and for the float
// records down to the BaseDelete call (0x004152b0). GetAlloc() anywhere
// earlier spends the budget before the pieces loop and every collection
// helper turns into a call; all-SetSize2 or all-cast spellings expand the
// first two arrays instead (16 Term/SetSize2 combinations measured).
void Model::sub_10017016(LTBOOL bFlag)
{
	m_Sub014.sub_10024419();

	if (m_pSub20c != &m_Sub20c)
		BaseDelete((LAlloc*)m_pDataBlock, m_pSub20c, 1);

	m_Sub20c.sub_100169dd();

	m_pSub20c = &m_Sub20c;

	DeleteAndClearArray(m_Pieces, (LAlloc*)m_pDataBlock);

	sub_100170f2();

	sub_10017145(bFlag);

	DeleteAndClearArray2(m_Sockets, (LAlloc*)m_pDataBlock);
	DeleteAndClearArray(m_WeightSets, (LAlloc*)m_pDataBlock);

	m_NodeTransforms.Term((LAlloc*)m_pDataBlock);
	m_Unknown054.Term((LAlloc*)m_pDataBlock);
	m_Nodes.Term((LAlloc*)m_pDataBlock);

	m_DataRef.sub_10016366();

	FreeFilename();

	m_Unknown08c.Term(GetAlloc());

	m_Unknown064 = 0;
}


// 0x10017145 / 0x00446a00 — the child-model table back to just this model's
// own record. Every other record loses a reference on the model it stands
// for and, if the flag is set and that was the last one, that model is
// deleted outright; then the record itself is reset and, unless it is one of
// the two the model owns, handed back.
//
// m_pChildModels[0] is re-read from memory at BOTH tests (0x00446a2a and
// 0x00446a70) and pRec->m_pOwner28 is re-read for the second half of the
// reference test (0x00446a52), which is what says neither is cached.
// `delete` goes through vtable slot 0 with a literal 1 (0x00446a65), the
// shape of a virtual destructor. The release is LDelete against the model's
// own allocator, EXPANDED at /O2 (0x00446a8a, 0x00446a94) with no null
// retest -- the record is already known non-null. Byte-identical on both
// images; modelnew.cpp's ModelQ444::sub_10017145Q444, where this spelling
// was found, is the same function and is no longer compiled twice.
void Model::sub_10017145(LTBOOL bFlag)
{
	uint32 i;
	CModelSub1D4* pRec;
	Model* pModel;

	for (i = 0; i < m_nChildModels; i++)
	{
		pRec = m_pChildModels[i];
		if (pRec)
		{
			if (pRec != m_pChildModels[0])
			{
				pModel = pRec->m_pOwner28;
				if (pModel && pModel != this)
				{
					pModel->m_CacheCount--;

					if (bFlag && !m_ChildOwners[i] && pRec->m_pOwner28->m_CacheCount == 0)
						delete pRec->m_pOwner28;
				}
			}

			pRec->sub_10016501();

			if (pRec != m_pChildModels[0] && pRec != &m_Sub1d4)
			{
				LDelete(GetAlloc(), pRec);
			}
		}
	}

	for (auto& owner : m_ChildOwners) owner.reset();
	m_nChildModels = 1;
	m_pChildModels[0] = &m_Sub1d4;
}


// 0x10017b0e — the name the model was loaded under.
char* Model::GetFilename()
{
	return m_pFilename;
}


// ---- the filename pair (0x10017b12, 0x10017b4a) ------------------------- //
//
// A model with no name of its own points at a shared empty string rather
// than at NULL, so every reader can strlen it; the sentinel is what lets
// FreeFilename tell an allocated name from the shared one. Note that the
// new name is built in a local and only stored on success — a failed
// allocation leaves the model on the sentinel rather than on NULL.

LTBOOL Model::SetFilename(char* pFilename)
{
    if (!pFilename) return LTFALSE;
    const auto length = strlen(pFilename);
    std::unique_ptr<char[]> copy(new (std::nothrow) char[length + 1]);
    if (!copy) return LTFALSE;
    memcpy(copy.get(),pFilename,length + 1);
    FreeFilename();
    m_pFilename = copy.release();
    return LTTRUE;
}

void Model::FreeFilename()
{
	if (m_pFilename != g_pEmptyFilename)
		delete[] m_pFilename;

	m_pFilename = g_pEmptyFilename;
}


// ---- the four name lookups (0x1001737b-0x10017c16) ---------------------- //
//
// Four copies of the same walk over four different tables, each returning
// the record it found rather than a flag — nonzero is what the callers test.
// They disagree on small things the way copied code does: the piece and node
// lookups pass the caller's name first to the comparison, the weight-set and
// socket lookups pass their own, and the node lookup re-reads the table at
// the end instead of keeping the record in a register.

LTBOOL Model::FindPiece(char* pName, HMODELPIECE* pPiece)
{
	uint32 i;

	for (i = 0; i < m_Pieces.m_nElements; i++)
	{
		ModelPiece* pCur = m_Pieces.m_pArray[i];

		if (nolf::ascii_compare(pName, pCur->m_Name) == 0)
		{
			if (pPiece)
				*pPiece = i;

			return pCur != nullptr;
		}
	}

	return LTFALSE;
}

LTBOOL Model::FindWeightSet(char* pName, HMODELWEIGHTSET* pSet)
{
	uint32 i;

	for (i = 0; i < m_WeightSets.m_nElements; i++)
	{
		ModelWeightSet* pCur = m_WeightSets.m_pArray[i];

		if (nolf::ascii_compare(pName, pCur->m_Name) == 0)
		{
			if (pSet)
				*pSet = i;

			return pCur != nullptr;
		}
	}

	return LTFALSE;
}

LTBOOL Model::FindNode(char* pName, HMODELNODE* pNode)
{
	uint32 i;

	for (i = 0; i < m_NodeTransforms.m_nElements; i++)
	{
		char* pCurName = m_Nodes.m_pArray[i]->m_pName;

		if (nolf::ascii_compare(pCurName, pName) == 0)
		{
			if (pNode)
				*pNode = i;

			return m_Nodes.m_pArray[i] != nullptr;
		}
	}

	return LTFALSE;
}

// The animation lookup is the odd one out: it hands back the table entry as
// well as the index, and the animation record itself as the return value.
// Its walk keeps both a counter and a byte offset into the 0x20-byte entries,
// which is the indexed form VC6 strength-reduces.
LTBOOL Model::FindAnim(char* pName, uint32* pIndex, AnimInfo** ppInfo)
{
	uint32 i;

	for (i = 0; i < m_AnimArray.m_nElements; i++)
	{
		ModelAnim* pAnim = m_AnimArray.m_pArray[i].m_pAnim;

		if (nolf::ascii_compare(pAnim->m_pName, pName) == 0)
		{
			if (pIndex)
				*pIndex = i;

			if (ppInfo)
				*ppInfo = &m_AnimArray.m_pArray[i];

			return pAnim != nullptr;
		}
	}

	return LTFALSE;
}

LTBOOL Model::FindSocket(char* pName, HMODELSOCKET* pSocket)
{
	uint32 i;

	for (i = 0; i < m_Sockets.m_nElements; i++)
	{
		ModelSocket* pCur = m_Sockets.m_pArray[i];

		if (nolf::ascii_compare(pCur->m_Name, pName) == 0)
		{
			if (pSocket)
				*pSocket = i;

			return pCur != nullptr;
		}
	}

	return LTFALSE;
}


// 0x1001cbae — the model, out of a stream. Three steps, each of which can
// stop it: the header (which is where the version is checked), then the load
// state's own answer for how many bytes the file's data needs, then one
// block of that size from the caller's allocator. The block is only taken
// when there is something to take — a file whose state reports nothing is
// LT_OK with no allocation, which is why the middle test's failing arm falls
// straight through to the zero return rather than to an error.
//
// The two codes are read off their immediates: 0x4d is LT_INVALIDFILE and
// 0x43 is LT_OUTOFMEMORY, and VC6 tail-merged the two `push $imm; pop %eax`
// returns into one, keeping the second site's `pop` and jumping the first
// into it.
LTRESULT Model::Load(ILTStream* stream, IMoAllocator* allocator) {
    if (!allocator || !model_ValidateStream(stream)) return LT_INVALIDFILE;
    // Serialized record sizes describe the 32-bit file, not native objects.
    // Native arrays allocate their actual sizeof(T) through the delegate.
    sub_10017016(0);
    m_pDataBlock = allocator;
    m_DataRef.Set(allocator);
    CModelLoadState state;
    return LoadHeader(stream,&state) ? LT_OK : LT_INVALIDFILE;
}

AnimKeyFrame::AnimKeyFrame()
{
	m_Time      = 0;
	m_pString   = "";
	m_Type      = 0;
	m_pFn       = LTNULL;
	m_Unknown10 = 0;
}


// ---- two more classes out of the model block (0x100165ac, 0x100167b4) --- //
//
// Neither is named anywhere in the image and neither is reached from anything
// reconstructed except its own deleting destructor, so both are named for the
// vtable they store. Their shape, though, is read rather than guessed:
//
//   * how many virtuals each has comes from .rdata. 0x1004d974 is followed by
//     0x1004d984 sixteen bytes later and 0x1004d9e4 by 0x1004d9f0 twelve
//     bytes later, and in both cases that next vtable is a member
//     collection's -- the destructor stores it at that member's offset -- so
//     the classes have four and three slots.
//   * the members and their offsets are the destructors' own member-teardown
//     tails, in reverse declaration order: ModelAnimationNode has a collection at
//     +0x08 (SetSize2 0x100195e4, the 79-byte body the 0x20-element arrays
//     use) and a four-byte one at +0x1c (0x1001a03d); ModelAnim has a
//     four-byte collection at +0x08 (0x100196cb, whose release is the shared
//     pointer-sized 0x1001aad4) and one whole ModelAnimationNode at +0x28.
//   * each destructor body is a single out-of-line call, and both callees are
//     reached from elsewhere too -- 0x1001662c from ModelAnim's own body
//     and 0x100167e1 from nothing else yet -- so they are members rather than
//     destructor bodies written inline.
//
// The element types are no longer a guess. The collection at +0x08 holds
// CModelXform (0x1c bytes, no constructor): the reader at 0x1001bb37 reads
// exactly 0x1c bytes per element with a stride of 0x1c, and the SetSize2 the
// collection reaches (0x100195e4) allocates through BaseNew at 0x1001b71a,
// whose `imul $0x1c,%edx,%edx` names the size and whose *missing* constructor
// loop says the element has none. (The earlier reading here — 0x20 bytes with
// a non-trivial constructor, inferred from a "79-byte" SetSize2 — was wrong:
// 0x100195e4 is 77 bytes and its loop-free allocator is the whole difference.
// It is not a POD either: it has a user-written out-of-line operator=, and
// CopyArray2 at 0x1001b6a3 CALLS it — see commonmodel.h.)
//
// The collection at +0x1c holds ModelAnimationNode*, one per child of the node
// record at +0x04: 0x1001bb37 sizes it from that record's own child count and
// then fills it with one recursively-read ModelAnimationNode apiece.

ModelAnimationNode::ModelAnimationNode()
{
	sub_100165e5();
}

ModelAnimationNode::ModelAnimationNode(ModelAnim* pOwner, ModelAnimationNode* parent)
{
	sub_100165e5();

	m_pOwner2c  = pOwner;
	m_pParent = parent;
}

ModelAnimationNode::~ModelAnimationNode()
{
	sub_1001662c();
}

// 0x1001671c — seven bytes: the owner at +0x2c, and the model at +0x20 of it.
Model* ModelAnimationNode::sub_1001671c()
{
	return m_pOwner2c ? m_pOwner2c->m_pModel : nullptr;
}

// 0x10016723 (42 bytes) / 0x004457e0 (55) -- CModelXform::operator= -- is
// defined in class in commonmodel.h; see the note there.

// 0x1001662c — both collections handed back to the model's own data block.
// The model is fetched twice, once per collection, which is two calls in the
// shipped code and therefore two in the source.
void ModelAnimationNode::sub_1001662c() {
    auto* model = sub_1001671c();
    auto* alloc = model ? model->GetAlloc() : &g_DefAlloc;
    m_Array08.Term(alloc);
    FreeArrayElements(&m_Array1c,alloc);
}

// 0x100165e5 — the shared body of both constructors. The collection at +0x08
// is not merely cleared: it is assigned from a freshly built empty one, which
// is the two-statement `{ CMoArray<T,C> temp(0); m_Array = temp; }` shape
// Model's own constructor uses twice, and the temporary's teardown (vtable
// re-stored, Term()) is the tail of the function.
void ModelAnimationNode::sub_100165e5()
{
	m_pOwner2c   = 0;
	m_pParent = nullptr;
	m_pSub20c04  = 0;

	{
		CMoArray<CModelXform, NoCache> temp(0);

		m_Array08 = temp;
	}
}

ModelAnim* ModelAnimationNode::_v04()
{
	return m_pOwner2c;
}

void ModelAnimationNode::_v08(ModelAnim* pOwner)
{
	m_pOwner2c = pOwner;
}

ModelAnimationNode* ModelAnimationNode::_v0c(ModelAnim* pOwner, ModelAnimationNode* parent)
{
	return new ModelAnimationNode(pOwner, parent);
}


// 0x1001bb37 — the animation data for one node and everything under it. Two
// arrays: one CModelXform per keyframe of the owning animation (read in one
// 0x1c-byte block each, so the file's layout and the record's are the same
// layout), and one child record per child of the node at +0x04, each read by
// this same function — the animation data is a tree with the node tree's
// shape, and the recursion is what makes a model file's node hierarchy
// readable in one pass.
//
// NEAR (301 of 306 bytes, 115 of 117 instructions, seven residual blocks).
//
// IT WAS 289 BYTES UNTIL collection.h GOT SetSizeInit3's BODY. The sizing
// below is one call to SetSizeInit3, which modchunk.cpp recovered from
// dynarray.h:703 -- but the definition it wrote lived in modchunk.cpp, and
// THAT FILE IS IN lithtech.exe's SOURCE LIST ONLY. This is a server TU, so
// the server build had a declaration, no definition, and emitted an
// out-of-line call to a symbol server.dll never defines, where the shipped
// /O1 image EXPANDS the body inline (0x1001bba5 is the SetSize2 call and
// 0x1001bbb2..0x1001bbc5 the fill loop, both reached off `this` in ebx).
// The body now sits in collection.h beside SetSize2, /Ob1 expands it here,
// and the 289 is 301 again. modchunk.cpp's note that the change "does NOT
// regress server.dll" was right about the mechanism and wrong about where
// the definition had to live.
//
// What is left is which value gets the last callee-saved register: the
// shipped build keeps `&m_Array1c` in ebx from SetSizeInit3 all the way to
// the cleanup call and puts the child loop's index in a frame slot; ours
// does the reverse, so it spills the pointer and pays five bytes. Spelling
// the array member straight into both call sites (rather than through a
// `pChildArray` local) removed two of the nine residual blocks -- it puts
// the `lea` where the shipped build has it, in the middle of the argument
// setup -- but not the register priority, which has not been reached from
// source. Using `j` for the child loop instead of `i` changes nothing
// (measured: 301 bytes, same seven blocks).
//
// Three things are read off the bytes rather than assumed. The keyframe count
// comes from the OWNING ANIMATION (`m_pOwner2c->m_Array08.m_nElements`), not
// from the file, so the animation's keyframe table has to be sized first —
// which 0x1001bc69 does, and then hands the tree to this. The child array is
// zero-filled after it is sized and before anything is put in it, so that the
// cleanup path can walk it whether or not the child that failed was reached.
// And the allocator every one of those steps uses is the owning MODEL's data
// block, fetched afresh each time through 0x1001671c — five separate calls in
// one function, which is what says the model was reached through a call and
// not cached in a local.
// ltanimtracker.h's AnimTimeRef with the one method the engine added and the
// public header does not carry. Every offset the check touches is the SDK's:
// m_pModel at 0x00, m_Prev's index and frame at 0x04/0x06, m_Cur's at
// 0x0c/0x0e and m_Percent at 0x14 (name ours).
LTBOOL AnimTimeRef::IsValid()
{
	Model* pModel = m_pModel;
	uint32 nAnims;

	if (!pModel)
		return LTFALSE;

	nAnims = pModel->m_AnimArray.m_nElements;

	if (m_Prev.m_iAnim >= nAnims)
		return LTFALSE;

	if (m_Cur.m_iAnim >= nAnims)
		return LTFALSE;

	if (!pModel->m_AnimArray[m_Prev.m_iAnim].m_pAnim || m_Prev.m_iFrame >=
	    pModel->m_AnimArray.m_pArray[m_Prev.m_iAnim].m_pAnim->m_nKeyFrames)
	{
		return LTFALSE;
	}

	if (!pModel->m_AnimArray[m_Cur.m_iAnim].m_pAnim || m_Cur.m_iFrame >=
	    pModel->m_AnimArray.m_pArray[m_Cur.m_iAnim].m_pAnim->m_nKeyFrames)
	{
		return LTFALSE;
	}

	if (!std::isfinite(m_Percent) || m_Percent < 0.0f || m_Percent > 1.0f)
		return LTFALSE;

	return LTTRUE;
}


// 0x10016300 (4 bytes, `push $0x46; pop %eax; ret`) / lithtech 0x004451b0
// (6 bytes, `mov $0x46,%eax; ret`) — the child-model hook a fresh
// CModelLoadRequest starts with (serverengine.h's constructor stores its
// address; 0x1001cb42 is the only reference in server.dll). It answers
// LT_NOCHANGE and does nothing else. It is defined HERE because both images
// place it directly in front of mod_SameNodeTree (0x10016304 / 0x004451c0),
// so it is this translation unit's code. Too small for a byte match to credit.
LTRESULT se_NoChildModel(CModelLoadRequest* pReq, Model** ppModel)
{
	return LT_NOCHANGE;
}


// 0x10016304 — the shape test two node trees have to pass before one model's
// animation can drive another's skeleton: identical child counts all the way
// down. The out-parameter names the node that failed, and only the level that
// actually tested it stores -- a failing recursion returns straight out,
// because the level below has already written its own answer there.
LTBOOL mod_SameNodeTree(ModelNode* pA, ModelNode* pB,
                        ModelNode** ppMismatch)
{
	uint32 i;

	if (pA->m_Array18.m_nElements != pB->m_Array18.m_nElements)
	{
		*ppMismatch = pA;
		return LTFALSE;
	}

	for (i = 0; i < pA->m_Array18.m_nElements; i++)
	{
		ModelNode* pChildB = pB->m_Array18.m_pArray[i];
		ModelNode* pChildA = pA->m_Array18.m_pArray[i];

		if (!mod_SameNodeTree(pChildA, pChildB, ppMismatch))
			return LTFALSE;
	}

	return LTTRUE;
}


// 0x1001668a — flatten the record tree into the owner's per-node array.
//
// The bound is the MODEL's node-transform count, reached through the owner
// (sub_1001671c), not anything this record knows, which is what says the flat
// array is one slot per model node. The counter is the caller's, so a subtree
// that overflows it stops the whole walk rather than just its own branch --
// the child loop returns LTFALSE the moment a recursion does.
//
// IDENTICAL on both images (was a size-exact near, 84 of 84 bytes).
//
// The shipped build keeps a zero in %ebx from the very top and spends it
// three times -- the array null test, the loop entry guard and the loop
// index -- and getting that was NOT a matter of spelling the zero. It
// followed from taking the node bound out of the `if`: with the guard
// written `*pnCount >= sub_1001671c()->m_NodeTransforms.m_nElements`, VC6
// hoists the `*pnCount` load ABOVE the call and parks it in %ebx, which is
// the register the zero wants; with the bound read into a local first the
// load sinks below the call into %ecx (caller-saved, so it cannot be live
// across one), %ebx comes free and the zero appears at the top by itself.
// That is the whole 83-vs-84 difference and it is worth remembering: a
// register a value is FORCED into can be moved by changing when the value
// is computed, not by naming the value.
//
// Measured and rejected, all against the shipped bytes:
//   * `uint32 i = 0;` hoisted above the guards -- 96 bytes, and VC6 spills
//     i to a frame slot and adds the ebp frame the function does not need.
//   * a named `nIndex = *pnCount;` local -- 83 bytes, identical output to
//     the nested form; naming the value changes nothing.
//   * the bound test reversed to `nElements <= *pnCount` -- 83 bytes, the
//     load still hoisted above the call.
//   * naming the child `ModelAnimationNode* pChild` in the loop -- 84 bytes and
//     the same residual.
//
// THE LAST RESIDUAL was which arm owns the epilogue: shipped puts the
// success block, a two-byte `jmp` over the `xor %eax,%eax` failure block,
// then the shared `pop/pop/pop/ret $4` (0x100166d1..0x100166db); with the
// two refusals written as early `return LTFALSE`s VC6 folds the epilogue
// into the success block and has the failure block jump back to it. The
// POSITIVE guards nested around the body, with the one `return LTFALSE`
// after them, give the shipped placement -- so the shared-exit host IS a
// source property here. (A `bRet` result local with a `break` out of the
// loop was 93 bytes.)
LTBOOL ModelAnimationNode::sub_1001668a(uint32* pnCount)
{
	uint32 i;
	uint32 nNodes;

	if (m_pOwner2c->m_ppNodeRecords)
	{
		nNodes = sub_1001671c()->m_NodeTransforms.m_nElements;

		if (*pnCount < nNodes)
		{
			m_pOwner2c->m_ppNodeRecords[*pnCount] = this;
			(*pnCount)++;

			for (i = 0; i < m_Array1c.m_nElements; i++)
			{
				if (!m_Array1c.m_pArray[i]->sub_1001668a(pnCount))
					return LTFALSE;
			}

			return LTTRUE;
		}
	}

	return LTFALSE;
}

// 0x100166de (62 bytes) — rebind this record and its subtree to a node tree:
// the node pointer is stored first, unconditionally, then the child counts
// must agree (m_Array1c's +0x24 against the node's m_Array18 count at +0x20)
// or the walk refuses, and each child is rebound to the node's child of the
// same index, recursively.  The children's answers are not looked at
// (0x10016708's result is dropped), and the one caller, ModelAnim's slot 1,
// drops this one's too.
LTBOOL ModelAnimationNode::sub_100166de(ModelNode* pSub20c)
{
	uint32 i;

	m_pSub20c04 = pSub20c;

	if (m_Array1c.GetSize() != pSub20c->m_Array18.GetSize())
		return LTFALSE;

	for (i = 0; i < m_Array1c.GetSize(); i++)
		m_Array1c[i]->sub_100166de(pSub20c->m_Array18[i]);

	return LTTRUE;
}


LTBOOL ModelAnimationNode::sub_1001bb37(ILTStream* pStream)
{
	uint32 i, j;
	ModelAnimationNode* pChild;
	CMoArray<ModelAnimationNode*, NoCache>* pChildArray;
	ModelAnimationNode* pNull;

	if (!m_Array08.SetSize2(m_pOwner2c->m_Array08.GetSize(),
	                        sub_1001671c()->GetAlloc()))
		return LTFALSE;

	for (i = 0; i < m_pOwner2c->m_Array08.GetSize(); i++) {
        w_ReadVector(pStream,&m_Array08[i].m_Pos);
        w_ReadRotation(pStream,&m_Array08[i].m_Rot);
    }

	// ONE STATEMENT, not SetSize2 plus a written-out zero loop. The shipped
	// /O2 build CALLS the 124-byte SetSizeInit3 at 0x0044e5f0 out of line
	// where our pair is expanded inline -- which is this function's 50-byte
	// overshoot. Period source recovered at dynarray.h:703 and now defined
	// in collection.h.
	//
	// server.dll IDENTICAL (0x1001bb37, 306 bytes) once three things held.
	// The call goes THROUGH pChildArray, and the failure arm hands
	// pChildArray back: shipped keeps `lea 0x1c(%esi),%ebx` alive to the
	// `push %ebx` at 0x1001bc5d and homes i in the frame slot. The allocator
	// is read into pAlloc BEFORE pChildArray is set (the `lea` follows the
	// 0x174 load at 0x1001bb9b), as in sub_1001bd54. And the failure arm's
	// allocator is `GetAlloc()`, not the `(LAlloc*)m_pDataBlock` cast --
	// with the cast, i took ebx and pChildArray the slot. The three
	// operator[] spellings are neutral on server and bought lithtech.exe one
	// COMDAT (CMoArray<CModelXform>::_DeleteAndDestroyArray, 0x44c720).
	//
	// lithtech.exe IDENTICAL too (0x0044cd80, 455 bytes) once SetSizeInit3
	// stopped being expanded here (see its definition at the foot of this
	// file) and the count and the null were named in this order: the count
	// read into nChildren, then pNull, then pChildArray. That puts the one
	// `xor %ebx,%ebx` (pNull's zero, and the child loop's i = 0 after the
	// call) and the two frame stores where 0x0044ce1f..0x0044ce2d have them.
	// Neutral on server.dll.
	LAlloc* pAlloc = sub_1001671c()->GetAlloc();
	uint32 nChildren = m_pSub20c04->m_Array18.GetSize();
	pNull = LTNULL;
	pChildArray = &m_Array1c;

	if (!pChildArray->SetSizeInit3(nChildren, pNull, pAlloc))
		return LTFALSE;

	for (i = 0; i < m_pSub20c04->m_Array18.GetSize(); i++)
	{
		pChild = BaseNew(sub_1001671c()->GetAlloc(),
		                 (ModelAnimationNode*)0, 1, _v04(), this);
		if (!pChild)
			return LTFALSE;

		pChild->m_pSub20c04 = m_pSub20c04->m_Array18[i];

		if (!pChild->sub_1001bb37(pStream))
		{
			BaseDelete(sub_1001671c()->GetAlloc(), pChild, 1);

			FreeArrayElements(pChildArray, sub_1001671c()->GetAlloc());

			return LTFALSE;
		}

		m_Array1c[i] = pChild;
	}

	return LTTRUE;
}


// ======================================================================= //
//  MOVED HERE from src/engine/lithtech/modchunk.cpp: the piece vertex-data
//  record's five classes and its two readers, 0x1001bf85 (544 bytes; the
//  enumeration splits it into 0x1001bf85/81 and 0x1001bfd6/463, and
//  0x1001bfd6 is the head of the inner three-iteration loop, a `jne` target,
//  not a function) and 0x1001c1a5.  modchunk.cpp is compiled into
//  lithtech.exe only, so neither row could ever be compiled for server.dll
//  while the bodies lived there.  Everything below is modchunk.cpp's own
//  analysis, verbatim.
//
//  WHAT THE MOVE AND THREE EDITS BOUGHT, against server.dll:
//
//    0x1001c1a5  IDENTICAL, 279 of 279 bytes.  Two things got it there.
//      The first `m_Array34.m_pArray[i]` loop needs a NAMED POINTER LOCAL
//      (`pRec`) and the second must NOT have one: with the local, the
//      shipped `mov 0x38(%esi),%ecx ; add -0x8(%ebp),%ecx` order appears
//      (base loaded first, walking offset added); without it VC6 emits the
//      two the other way round.  With the local in BOTH loops the second
//      one also moves the `push 0xc(%ebp)` of the argument after the `mov`,
//      which the shipped build does not.  `m_Array34[i]` through
//      operator[] behaves exactly like the written-out subscript and
//      changes nothing.  So the first loop's form decides the second's --
//      these are whole-function effects, not per-site ones.
//
//    0x1001bf85  544 of 544 bytes, 209 of 209 instructions, TWO BYTES.
//      `if (pLod->m_nVerts > 0)` and not `if (pLod->m_nVerts)`: an unsigned
//      word compared against a zero register gives `jbe` for `> 0` and
//      `je` for truthiness, and 0x1001c0b0 is `jbe`.  What is left is one
//      `mov`/`add` pair at 0x1001c060: the shipped build loads the walking
//      offset and adds the array base, ours loads the base and adds the
//      offset.  Four spellings were measured and all four give the base
//      first -- `&m_Array00.m_pArray[i]`, `m_Array00.m_pArray + i`,
//      `&m_Array00[i]`, and the same with `pLod` declared inside the loop
//      -- while the INNER loop's `pVert = &pLod->m_pVerts[j]`, which is the
//      same construct through a register base, already matches offset-first.
//      Swapping the two locals' declaration order changes nothing.
// ======================================================================= //

// --------------------------------------------------------------------- //
//  The piece's vertex-data record, and the piece itself.
// --------------------------------------------------------------------- //
//
// commonmodel.h already saw this class from the outside: "The first 0x24
// bytes are a record of their own -- the vertex-data record 0x1001bf85 reads,
// which is called both on the piece itself and on each element of the
// 0x24-strided collection at +0x34, so the piece opens with one of them and
// carries a list of the rest".  The two reader bodies below give it its
// interior:
//
//   +0x00  CMoArray<ModelPieceLod,  NoCache>   (allocator 0x0044c970, which
//          multiplies by 0x20 AND runs a constructor; release 0x0044c720)
//   +0x10  CMoArray<ModelPieceDimension,  NoCache>   (allocator 0x0044ca90, the
//          22-byte constructor-free one, also 0x20 per element)
//   +0x20  Model*                              (dereferenced at +0x174 for
//          the allocator, and what the setter at 0x00446090 writes)
//
// which is 0x24 exactly, and 0x0044cae0's `lea (%edi,%edi,8); shl $2` -- the
// allocator for the collection of them -- multiplies by 0x24 in the clear.
//
// The element constructor at 0x00445410 is what fixes ModelPieceLod's shape:
// it zeroes a dword at +0x00, a WORD at +0x04 (and not the word at +0x06),
// and six dwords at +0x08..+0x1c.  The reader fills +0x04 and +0x06 off the
// wire, points +0x00 into the model's own pool, and reads the six dwords
// last.
//
// ModelPieceDimension has no constructor at all (0x0044ca90 is 22 bytes: allocate,
// null-test, return) and the reader walks it with TWO strides at once -- 8
// bytes for a pair of floats and 2 bytes for a word, three times -- which is
// three (float, float, uint16) triples against a 0x20-byte record.
//
// None of these classes is declared anywhere yet, so all three are Q44C and
// all three BELONG IN commonmodel.h; ModelPiece's own m_pad_00[8] / m_nSize /
// m_pad_0c[0x18] and its m_Array34[0x10] are what they should replace.
//
// The constructors, destructors and the +0x20 setter are DECLARED AND NOT
// DEFINED on purpose: defining them here would let /Ob2 expand the setter
// into sub_1001c1a5's loop, where the shipped build calls it (0x0044d7dc).
// This TU is for measuring the two readers, not for linking.

// Model::m_Unknown054's element: 0x14 bytes, which the `lea (%ecx,%ecx,4)` /
// `lea (%edx,%ecx,4)` pair at 0x0044d5da fixes.  The header types that
// collection `CMoArray<void*, NoCache>`, which is a placeholder -- the cast
// below is what the indexing says it really is.  Fields by offset; the reader
// fills +0x10 FIRST and then +0x00 through +0x0c.
// --------------------------------------------------------------------- //
//  0x0044d3f0   749 bytes   ModelPieceData::sub_1001bf85
//                           (server.dll 0x1001bf85 -- the enumeration there
//                            splits it into 0x1001bf85/81 + 0x1001bfd6/463;
//                            it is one 544-byte function, and 749/544 is the
//                            same 1.3-1.5x /O2-over-/O1 ratio every other
//                            function in this cluster shows)
// --------------------------------------------------------------------- //
//
// One piece's vertex data off the wire, in two passes.
//
// FIRST PASS -- triangles. A count, then three (float u, float v, uint16
// vertex index) corners per record. The compiler runs the inner
// three-iteration loop with TWO induction variables at once (edi striding by
// 8 for the float pair, [esp+0x1c] striding by 2 for the word), and reloads
// m_Array10.m_pArray before EVERY ONE of the three reads -- three loads per
// iteration, which is the "written out at each use" fingerprint and says the
// subscript is spelled out in full each time rather than cached in a pointer.
//
// SECOND PASS -- vertices. Each has weight count, replacement index, a
// slice of the MODEL's weight pool, bind position and normal. The record
// points into Model::m_Unknown054 at the caller's running
// offset (`lea (%ecx,%ecx,4); lea (%edx,%ecx,4)` = base + offset*0x14) and
// then ADVANCES that offset by its own count.  That is what the second
// argument is -- a cursor shared by the piece and every record under it,
// which ModelPiece::sub_1001c1a5 threads through all of them in order.
//
// The bounds check is against the pool's element count and is made BEFORE the
// zero test, so a record with no vertices still has to fit; a record that
// would run past the end fails the whole read.
//
// Both SetSize2 calls are INLINED here where 0x0044d090's is a call, and
// that is size, not source: SetSize2 over an element type with a constructor
// CALLS its BaseNew (0x0044c970 / 0x0044ca90) and stays small enough to
// expand, while SetSize2 over a bare pointer EXPANDS BaseNew and grows to the
// 103 bytes at 0x0044b6b0 that the compiler will not inline.  Nothing in the
// source picks between them.
//
// IDENTICAL on server.dll (544 of 544).  Both element reads go through
// CMoArray::operator[] (`m_Array10[i]`, `&m_Array00[i]`), not a bare
// `.m_pArray[i]`: with the bare subscript the record pointer came out as
// `mov 0x4(%ebx),%edi; add -0x18(%ebp),%edi` where shipped adds the strided
// offset first (0x1001c060: `mov -0x18(%ebp),%edi; add 0x4(%ebx),%edi`), the
// only difference in the function.  On lithtech.exe the accessor also took
// 0x0044d3f0 from 790 bytes to 757 of 749: the Lod array's BaseNew is now
// CALLED (0x0044c970, as shipped) instead of expanded.  What was left there
// was that shipped also calls _DeleteAndDestroyArray (0x0044c720) out of line
// for both arrays where ours expanded it (GetSize()/GetArray() on
// m_Unknown054 did not move it). Every read written as the SDK's
// `*pStream >> x` closed it (749 of 749): the bytes of each read are the
// same, but its two inline levels (operator>> -> LTStream_Read) spend the /O2
// inline budget that was otherwise going on _DeleteAndDestroyArray. The same
// spelling closed ModelAnim::sub_1001bc69; server.dll does not move.
LTBOOL ModelPieceData::ReadVertexData(ILTStream* pStream, uint32* pnBase)
{
	uint32 i, j, k;
	uint32 nDims;
	uint32 nLods;
	uint16 n;
	ModelPieceLod* pLod;
	CModelVertRec* pVert;

	*pStream >> nDims;

	if (!m_Array10.SetSize2(nDims, m_pModel->GetAlloc()))
		return LTFALSE;

	for (i = 0; i < nDims; i++)
	{
		for (k = 0; k < 3; k++)
		{
			*pStream >> m_Array10[i].uv[k][0];
			*pStream >> m_Array10[i].uv[k][1];
			*pStream >> m_Array10[i].indices[k];
		}
	}

	*pStream >> nLods;

	if (!m_Array00.SetSize2(nLods, m_pModel->GetAlloc()))
		return LTFALSE;

	for (i = 0; i < nLods; i++)
	{
		pLod = &m_Array00[i];

		*pStream >> n;
		pLod->weight_count = n;

		*pStream >> n;
		pLod->replacement = n;

		if (*pnBase + pLod->weight_count > m_pModel->m_Unknown054.m_nElements)
			return LTFALSE;

		if (pLod->weight_count > 0)
		{
			pLod->weights = (CModelVertRec*)m_pModel->m_Unknown054.m_pArray
			                 + *pnBase;

			*pnBase += pLod->weight_count;

			for (j = 0; j < pLod->weight_count; j++)
			{
				pVert = &pLod->weights[j];

				*pStream >> pVert->m_n10;
				*pStream >> pVert->m_f[0];
				*pStream >> pVert->m_f[1];
				*pStream >> pVert->m_f[2];
				*pStream >> pVert->m_f[3];
			}
		}

		*pStream >> pLod->position.x;
		*pStream >> pLod->position.y;
		*pStream >> pLod->position.z;
		*pStream >> pLod->normal.x;
		*pStream >> pLod->normal.y;
		*pStream >> pLod->normal.z;
	}

	return LTTRUE;
}


// --------------------------------------------------------------------- //
//  0x0044d6e0   390 bytes   ModelPiece::sub_1001c1a5
//                           (server.dll 0x1001c1a5, 279 bytes)
// --------------------------------------------------------------------- //
//
// One piece.  commonmodel.h already declares it -- "0x1001c1a5 -- this piece
// off the wire.  The second argument is a running base the reader advances"
// -- and the body confirms that reading of the second argument: the piece
// never touches it itself, it just hands the same pointer to its own
// vertex-data record and then to each of the records under it, in order.
//
// The word at +0x24 comes first and is widened to a dword by the store, which
// is what the header already recorded.  Then two floats, then -- ONLY from
// file version 10 -- one more dword at +0x44, gated on the MODEL's version
// (0x0044d725 reads m_nFileVersion through m_pModel68) rather than on
// anything the piece carries.
//
// Then a word and that many dwords READ AND DISCARDED (0x0044d752-0x0044d76d
// reads into one stack slot and never looks at it).  That is a skip of a
// block the client build does not want, and it is the reason the piece
// reader is version-tolerant.
//
// The record collection at +0x34 is sized from the MODEL's count at +0x94 --
// Model::m_Unknown08c's element count, which is the collection the header
// notes as "the one DefaultCache collection of the thirteen" -- and every
// element is then pointed back at the model through the +0x20 setter.  The
// count is kept in a local across the rest of the function (ebx is never
// reloaded), which is why it is read into `nRecs` and not spelled out again.
//
// The name is read with a limit of 0x20 straight into +0x48, which is what
// fixes m_Name's extent, and 0x48 + 0x20 + the model pointer lands exactly on
// 0x6c.
//
// IDENTICAL on both images. lithtech.exe went 486 -> 390 of 390 by
// indexing the records through CMoArray::operator[] (`m_Array34[i]`) rather
// than `.m_pArray[i]`: the accessor spends the /O2 inline budget that was
// expanding BaseDelete/BaseNew inside the inlined SetSize2, and shipped
// calls both (0x0044cab0 at 0x0044d799, 0x0044cae0 at 0x0044d7ba) -- the
// same lever as sub_1001bf85 above.
LTBOOL ModelPiece::sub_1001c1a5(ILTStream* pStream, uint32* pnBase)
{
	uint32 i;
	uint32 nRecs;
	uint32 nDiscard;
	uint16 n;
	uint16 nSkip;
	ModelPieceData* pRec;

	pStream->Read(&n, sizeof(n));
	m_Unknown24 = n;

	pStream->Read(&m_f2c, sizeof(m_f2c));
	pStream->Read(&m_f30, sizeof(m_f30));

	if (m_pModel68->m_nFileVersion >= 10)
		pStream->Read(&m_Unknown44, sizeof(m_Unknown44));

	pStream->Read(&nSkip, sizeof(nSkip));

	for (i = 0; i < nSkip; i++)
		pStream->Read(&nDiscard, sizeof(nDiscard));

	nRecs = m_pModel68->m_Unknown08c.GetSize();

	if (!m_Array34.SetSize2(nRecs, m_pModel68->GetAlloc()))
		return LTFALSE;

	for (i = 0; i < nRecs; i++)
	{
		pRec = &m_Array34[i];
		pRec->SetModel(m_pModel68);
	}

	if (pStream->ReadString(m_Name, sizeof(m_Name)) != LT_OK)
		return LTFALSE;

	if (!ReadVertexData(pStream, pnBase))
		return LTFALSE;

	for (i = 0; i < nRecs; i++)
	{
		if (!m_Array34[i].ReadVertexData(pStream, pnBase))
			return LTFALSE;
	}

	return LTTRUE;
}


// ======================================================================= //
//  0x1001bd54 / 372 bytes (lithtech.exe 0x0044d090, 526 bytes)
//
//  MOVED HERE from src/engine/lithtech/modchunk.cpp.  That file is in
//  lithtech.exe's source list only; commonmodel.h declares this member and
//  commonmodel.cpp calls it, so while the definition lived there server.dll
//  had a call to a symbol its own build never compiled and the row could
//  not close.  Everything below is modchunk.cpp's own analysis, verbatim.
// ======================================================================= //
// --------------------------------------------------------------------- //
//  0x0044d090   526 bytes   ModelNode::sub_1001bd54
//                           (server.dll 0x1001bd54, 372 bytes)
// --------------------------------------------------------------------- //
//
// The "Nodes" chunk: one node of the skeleton and, recursively, its whole
// subtree.  commonmodel.h already DECLARES this ("0x1001bd54 -- the `Nodes`
// chunk: the node tree out of the file") and no file defines it.
//
// Identification is by ordinal against server.dll and confirmed by every
// offset: `this` is 0xb4 bytes' worth of ModelNode (the recursion allocates
// exactly 0xb4 at 0x0044d1bc), +0xac is the model (read then dereferenced at
// +0x174 for the allocator, 0x0044d1c1), +0xb0 is the name (handed to
// Model::sub_1001c325 at 0x0044d0b1, which is the function every name in a
// model file goes through), +0x10/+0x12 are the uint16/uint8 the reset at
// 0x10016a00 clears with `andw`/`andb`, +0x2c and +0x6c are the two 4x4s, and
// +0x18 is the child collection.  The recursive call at 0x0044d1e1 is this
// function's own address.
//
// WHAT IT READS, in order: the name; a word; a byte; a 4x4 in ROW-MAJOR
// order, sixteen separate four-byte reads (the nested pair of counted loops
// at 0x0044d0ed/0x0044d102, which the compiler has strength-reduced to one
// walking pointer); then the child count and the children.
//
// THE TWO MATRICES.  The shipped body copies the matrix it just read THREE
// times: into a second stack local (0x0044d121), into m_Mat2c (0x0044d130),
// and then Mat_InverseTransformation is called with the SECOND LOCAL as its
// source (0x0044d132 pushes 0x5c(%esp), 0x0044d12f pushed 0x9c(%esp) as the
// destination) and its result is copied into m_Mat6c (0x0044d14e).  Passing
// &mat directly would have cost nothing, so the copy is in the source, not
// the codegen; the local below is what produces it.  0x00446fc0 is
// ltmatrix.h's own `Mat_InverseTransformation` -- an orthonormal inverse,
// transpose of the upper 3x3 and -(R^T * t) in the translation column, with
// the bottom row set to (0,0,0,1) -- too big for /Ob2 to expand, so it
// survives as one out-of-line copy the whole image shares.
//
// THE FAILURE PATHS, and they are not all the same one.  The name read, the
// SetSize2 and the per-child allocation all return LTFALSE with NO cleanup
// (0x0044d0ba, and 0x0044d180/0x0044d1d0 both jumping straight to the bare
// `xor eax,eax` exit at 0x0044d27d).  Only a child that fails to READ runs
// the cleanup -- delete that child, then hand the whole array back -- and
// that is the same shape ModelAnimationNode::sub_1001bb37 already carries in
// commonmodel.cpp, which is this function's sibling: the two are near-twins,
// reading a transform record and then recursing over a child array, and both
// end their failure arm with BaseDelete + FreeArrayElements.
//
// One `this->m_pModel` load per iteration serves both the allocator and the
// constructor argument (0x0044d1b6 into edi, used at 0x0044d1c1 and
// 0x0044d1d6), which is one expression, not two -- hence the single BaseNew
// call below rather than a separate `GetAlloc()` statement.
//
// server.dll IDENTICAL (372 bytes). The SetSize2 allocator is a NAMED local
// read before pChildArray is set: shipped materialises it
// (`mov 0x174(%eax),%eax ; push %eax` at 0x1001be1b), where the nested
// `m_pModel->GetAlloc()` argument folded into `push 0x174(%eax)`, one byte
// short. `m_Array18.SetSize2`, and the bare cast in place of GetAlloc(),
// were both neutral.
//
// lithtech.exe (0x0044d090, 526): NEAR at 528 with the two changes below
// now KEPT.  The MatVMul_3x3 objection no longer holds: objbank, servermath,
// transform and sdkforce all emit an identical copy, and 0x00431650 stays
// identical without this TU's.  History, as first measured --
// 744 bytes as written, 528 with two changes then NOT kept: `m_Mat6c = ~mToInvert;` (no mInverse)
// leaves Mat_InverseTransformation a call as shipped (0x0044d137) and gives
// the 0xcc frame; `pChildArray->SetSizeInit4(nChildren, LTNULL, pAlloc)` in
// place of SetSize2 + the zero loop calls SetSize2 (0x0044d179) and lets
// the failure arm's SetSize2(0) expand, as shipped. What is left: shipped
// CALLS _DeleteAndDestroyArray (0x0044d271) inside that arm, ours expands
// it and calls BaseDelete, plus the esi/edi swap it drags. Not kept because
// the ~ spelling stops this TU emitting MatVMul_3x3, whose only matching
// copy (0x00431650) is this file's, so the address count drops by one.
// Neutral on server.dll (IDENTICAL either way). Tried against the 528 state
// without effect: `*pStream >>` for any read, `m_Array18[i]`, GetAlloc()
// vs the cast at either failure-arm site, MakeInverseTransform(), and in
// commonmodel.h FreeArrayElements marked inline / Term(pAlloc) / accessors.
LTBOOL ModelNode::sub_1001bd54(ILTStream* pStream)
{
	uint32 i, j;
	uint32 nChildren;
	ModelNode* pChild;
	CMoArray<ModelNode*, NoCache>* pChildArray;
	LTMatrix mToInvert;
	LTMatrix mat;

	if (!m_pModel->sub_1001c325(pStream, &m_pName))
		return LTFALSE;

	pStream->Read(&m_n10, sizeof(m_n10));
	pStream->Read(&m_b12, sizeof(m_b12));

	for (i = 0; i < 4; i++)
	{
		for (j = 0; j < 4; j++)
			pStream->Read(&mat.m[i][j], sizeof(float));
	}

	mToInvert = mat;
	m_Mat2c = mat;

	m_Mat6c = ~mToInvert;

	pStream->Read(&nChildren, sizeof(nChildren));

	LAlloc* pAlloc = m_pModel->GetAlloc();
	pChildArray = &m_Array18;

	if (!pChildArray->SetSizeInit4(nChildren, LTNULL, pAlloc))
		return LTFALSE;

	for (i = 0; i < nChildren; i++)
	{
		pChild = BaseNew(m_pModel->GetAlloc(), (ModelNode*)0, 1, m_pModel);

		if (!pChild)
			return LTFALSE;

		if (!pChild->sub_1001bd54(pStream))
		{
			BaseDelete(m_pModel->GetAlloc(), pChild, 1);

			FreeArrayElements(pChildArray, (LAlloc*)m_pModel->m_pDataBlock);

			return LTFALSE;
		}

		m_Array18.m_pArray[i] = pChild;
	}

	return LTTRUE;
}


// 0x1001674d — build one against a model. Two member records run first (the
// collection at +0x08 and the ModelAnimationNode at +0x28, in declaration order),
// then the constants, then the embedded record is pointed back at this one
// through its own setter — a virtual call, which is why it is a call at all.
//
// The five constants are in the order the shipped /O2 build emits them, and
// that order is readable ONLY from lithtech.exe: 0x004459e7 stores the vptr,
// then +0x20, +0x24, +0x04, +0x18, +0x1c, and /Ot does not reorder
// independent adjacent stores — our own /O2 output reproduces whatever order
// this body is written in, instruction for instruction. server.dll cannot
// say: /Os rewrites the two zero/-1 stores into the short `andl $0x0` and
// `orl $0xffffffff` read-modify-write forms and hoists BOTH of them above
// the compiler's own vptr store (0x10016767), which no source order can
// produce and which it produces from either order — this file matched
// 0x1001674d with +0x04 and +0x18 written first, and matches it still with
// them written last.
ModelAnim::ModelAnim(Model* pModel)
{
	m_pModel = pModel;
	m_pName  = "";

	m_ppNodeRecords = 0;
	m_n18 = -1;

	m_Unknown1c = 0xc8;

	m_p58 = &m_Sub28;
	m_p58->_v08(this);
}

ModelAnim::~ModelAnim()
{
	sub_100167e1();
}

// 0x100167e1 — the release. Three things happen, and one oddity does not: the
// shipped code loads the model's node-transform count into a frame slot that
// nothing ever reads — `mov 0x78(%eax),%eax; mov %eax,0x8(%esp)`, into the
// slot the prologue's `push %ecx` reserves — and then calls the allocator's
// Free with the pointer alone. Nothing in the function or its callers reads
// that slot, and the epilogue's `pop %ecx` simply discards it. It is a dead
// store in the shipped binary, and the local it belongs to is dead in the
// shipped source too.
//
// NEAR (65 of 74 bytes). The dead local is exactly the nine-byte difference:
// VC6 deletes it, as it should. Marking it `volatile` does keep the store,
// and gets to 76 of 74 with three residual blocks — the shipped build pushes
// Free's argument before the volatile store and loads the allocator straight
// into `ecx`, ours does the store first and needs an extra `mov` — so the
// volatile is not the shape either. Written the plain way, which is what the
// rest of the function says the source looked like.
void ModelAnim::sub_100167e1() {
    auto* alloc = m_pModel ? m_pModel->GetAlloc() : &g_DefAlloc;
    if (m_ppNodeRecords) alloc->Free(m_ppNodeRecords);
    m_ppNodeRecords = nullptr;
    m_Array08.Term(alloc);
    m_Sub28.sub_1001662c();
    sub_10016874();
}

// 0x1001682b — slot 1: re-home the name on the new model, keep the model, and
// pass the model's own +0x2c0 record down to whatever the +0x58 slot points
// at. The +0x58 load happens before the name store, which is what says the
// two are separate statements rather than one chained expression.
void ModelAnim::_v04(Model* pModel)
{
	m_pName  = pModel->sub_100173fa(m_pName);
	m_pModel = pModel;

	m_p58->sub_100166de(pModel->m_pSub20c);
}

ModelAnim* ModelAnim::_v08(Model* pModel)
{
	return new ModelAnim(pModel);
}

// 0x10016874 — put the slot back on the embedded record. The `delete` is a
// virtual one (`call *(%eax)` with a literal 1, the deleting destructor) and
// carries its own null test, which is why the pointer is tested twice.
void ModelAnim::sub_10016874()
{
	if (m_p58 && m_p58 != &m_Sub28)
		delete m_p58;

	m_p58 = &m_Sub28;
}

// 0x10016897 — the forwarder: one argument passed straight on, the answer
// passed straight back.  0x100168a3 is the very next function (the call's
// displacement is 3).
LTBOOL ModelAnim::sub_10016897(LTBOOL bFlag)
{
	return sub_100168a3(bFlag);
}

// 0x100168a3 (105 bytes; lithtech twin 0x00445bb0, 117) — (re)build the flat
// node array and fill it from the record tree.
//
// The allocator is reached twice through the model's data block and the
// count twice through the model's node-transform array (0x00445bd6 /
// 0x00445bdc and 0x00445beb / 0x00445bf1) -- two separate reads, not one
// cached pair.  `LDelete_Array` and `LNew_Array` are both EXPANDED: the
// delete's element count is materialised into a dead stack slot
// (0x00445bdf), and the new's construction loop folds away for a pointer
// element, leaving only its null test (the two-byte `jne` onto the next
// instruction at 0x00445bff).
LTBOOL ModelAnim::sub_100168a3(LTBOOL bForce)
{
	uint32 index;
	ModelAnimationNode* pRoot;

	// The root record named in its own local: lithtech loads m_p58 BEFORE the
	// `lea/push` of &index and stores the zero after the push (0x00445c0c);
	// written as `m_p58->sub_1001668a(&index)` it stored first and loaded
	// last.  server.dll is identical either way.
	//
	// 0x00445bb5 / 0x00445bbb: a negative pair, and a real early out -- the
	// `return LTTRUE` at 0x00445bc2 sits BEFORE everything else.
	if (!bForce && m_ppNodeRecords)
		return LTTRUE;

	if (m_ppNodeRecords)
	{
		LDelete_Array(m_pModel->GetAlloc(), m_ppNodeRecords,
		              m_pModel->m_NodeTransforms.m_nElements);
	}

	m_ppNodeRecords = LNew_Array(m_pModel->GetAlloc(), ModelAnimationNode*,
	                             m_pModel->m_NodeTransforms.m_nElements);
	if (!m_ppNodeRecords)
		return LTFALSE;

	pRoot = m_p58;
	index = 0;
	return pRoot->sub_1001668a(&index);
}


// 0x100170f2 — the animation table. Each record's animation is asked which
// model it belongs to and only this model's are released; the table is then
// emptied against m_pAlloc rather than m_pDataBlock. Nothing tests the
// animation pointer for null before reading its model, which is the reader's
// guarantee showing through: an entry in the table always has a record.
void Model::sub_100170f2()
{
	uint32 i;

	for (i = 0; i < m_AnimArray.m_nElements; i++)
	{
		ModelAnim* pAnim = (ModelAnim*)m_AnimArray.m_pArray[i].m_pAnim;

		if (pAnim && pAnim->m_pModel == this)
			BaseDelete((LAlloc*)m_pDataBlock, pAnim, 1);
	}

	m_AnimArray.SetSize2(0, (LAlloc*)m_pAlloc);
}


// 0x100171f4 — the model's derived state rebuilt. Called twice by the
// reader, once part-way through the file with the flag set and once at the
// end with it clear, and the flag is passed straight down to everything it
// calls: it means "rebuild even if the sizes already look right".
//
// The piece layout is the interesting part. Every piece is given the running
// total of the sizes of the pieces before it, which lays them out end to end
// in one block of vertex data — and the shipped code reloads the piece array
// for each of the two accesses, which is two subscripts in the source, not
// one cached element pointer.
//
// The animation walk names the table element TWICE in the source — once for
// the model test and once for the call — with no element pointer held
// between them (the optimiser folds the two into the one load the shipped
// code has, so the bytes alone do not show it). The single-local spelling (the shape sub_100170f2 uses)
// came out one byte off here: the SIB in the model test read
// `mov (%eax,%edi,1),%ecx` where the shipped build (0x10017248) has
// `mov (%edi,%eax,1),%ecx`. Writing the subscript out at both uses gives the
// shipped base/index roles, and the same edit closes the /O2 copy at
// 0x446ad0 in lithtech.exe.
LTBOOL Model::sub_100171f4(LTBOOL bFlag)
{
	uint32 i;
	uint32 nOffset;

	if (sub_1001728c(bFlag) && sub_100172e3(bFlag))
	{
		nOffset = 0;

		for (i = 0; i < m_Pieces.m_nElements; i++)
		{
			m_Pieces.m_pArray[i]->m_nOffset = nOffset;

			nOffset += m_Pieces.m_pArray[i]->m_nSize;
		}

		for (i = 0; i < m_AnimArray.m_nElements; i++)
		{
			if (((ModelAnim*)m_AnimArray.m_pArray[i].m_pAnim)->m_pModel == this)
			{
				if (!((ModelAnim*)m_AnimArray.m_pArray[i].m_pAnim)->sub_10016897(bFlag))
					return LTFALSE;
			}
		}

		m_Unknown064 = sub_10017660();
		m_Unknown068 = sub_1001761d(LTFALSE);

		return LTTRUE;
	}

	return LTFALSE;
}


// 0x1001728c — the node-transform table sized to the node count. The early
// answer is the whole point of the flag: with it clear and the table already
// exactly as long as the tree is deep in nodes, there is nothing to do. The
// rebuild empties the table against the DEFAULT allocator and refills it
// against the model's own data block, which is Term() followed by a sized
// SetSize2 rather than one call.
//
// The tail is `if (!SetSize2(...)) return LTFALSE; return LTTRUE;`, not
// `return SetSize2(...) != 0`, and the /O2 image is what says so: 0x446c1a
// is ONE `mov $1,%eax` epilogue reached from the skip (0x446ba0), both of the
// expanded SetSize2's own LTTRUE returns (0x446bdd, 0x446bfa) and its success
// (0x446c0c). The `!= 0` spelling carries the conversion through as real
// code (`xor %ecx,%ecx / cmp / setne %cl` on every path, 195 bytes against
// 166). server.dll is unaffected -- MEASURED with the early skip kept as its
// own nested `return LTTRUE`: /Os renders the guarded return as the same
// branchless `neg / sbb / neg` at 0x100172d8, so that sequence is not evidence
// of a `!= 0`. (What cost server.dll this function earlier was the
// POSITIVE-guard rewrite of the skip, not the guarded tail.)
//
// lithtech.exe is now size-exact (166/166): the residual is inside the
// expanded SetSize2 -- shipped loads the allocator into %ebx before
// m_nElements (into %eax); we load m_nElements (into %ecx) first. Measured
// without effect: a pAlloc local, GetAlloc() for the cast, a pointer local
// for the array, SetSize(0)/Term(&g_DefAlloc) for Term(), GetSize() for nCur,
// and the SetSize2 call as an argument (worse). Treated as allocation.
LTBOOL Model::sub_1001728c(LTBOOL bFlag)
{
	uint32 nNodes;

	if (!bFlag)
	{
		uint32 nCur = m_NodeTransforms.m_nElements;

		if (nCur == m_pSub20c->sub_10016a9b())
			return LTTRUE;
	}

	m_NodeTransforms.Term();

	nNodes = m_pSub20c->sub_10016a9b();

	if (!m_NodeTransforms.SetSize2(nNodes, (LAlloc*)m_pDataBlock))
		return LTFALSE;

	return LTTRUE;
}


// 0x100172e3 — the node array itself. Sized to the node count, cleared slot
// by slot, and then handed to the tree, which walks itself in and takes each
// index from the counter the caller keeps on its stack. The count divided by
// four and rounded up is cached at +0x6c, and the tree's own index is put
// back to all-ones at the end.
//
// The data block is reached through a CSE'd address (`lea 0x174(%esi),%ebx`
// then `push (%ebx)` twice), which is what two uses of the same member
// address costs less than two displacement pushes.
LTBOOL Model::sub_100172e3(LTBOOL bFlag)
{
	uint32 i;
	uint32 nNodes;
	uint32 nIndex;
	CMoArray<ModelNode*, NoCache>* pNodes;

	if (!bFlag && m_Nodes.m_nElements == m_NodeTransforms.m_nElements)
		return LTTRUE;

	pNodes = &m_Nodes;

	// Term(), not SetSize2(0, ...). The two spellings are the same call at
	// /O1 — /Os expands the one-line in-class Term and leaves the SetSize2
	// call behind, which is server.dll's `call 0x1001a03d` at 0x10017309 —
	// but they are NOT the same at /O2: the shipped client CALLS SetSize2
	// here (0x446c5a) and EXPANDS it for the sized one below, and the
	// materialised `lea 0x24(%edi),%esi` that the call's `this` forces is
	// what every later access to the collection then uses. Spelled as a
	// direct SetSize2 the first one expands too, the `lea` never appears,
	// and the whole tail reads the collection off `this` by displacement.
	//
	// The accessors (GetAlloc(), GetSize(), operator[]) are what make the
	// shipped client CALL that first SetSize2 at all: each in-class accessor
	// is an /O2 inline expansion, and the budget goes to the LAST sites in
	// source order first, so with bare members nothing runs out and all of
	// both SetSize2 chains expand (228 bytes, 20 blocks). With them the first
	// SetSize2 is called, as at 0x446c5a. server.dll identical either way.
	// Still one level short at the second site: shipped (0x446c7e) CALLS
	// _DeleteAndDestroyArray, we expand it (233 bytes against 224).
	pNodes->Term(GetAlloc());

	nNodes = m_NodeTransforms.GetSize();

	if (!pNodes->SetSize2(nNodes, GetAlloc()))
		return LTFALSE;

	for (i = 0; i < pNodes->GetSize(); i++)
		(*pNodes)[i] = 0;

	nIndex = 0;

	if (!m_pSub20c->sub_10016ac3(&nIndex))
		return LTFALSE;

	m_Unknown06c = nNodes >> 2;

	if (nNodes & 3)
		m_Unknown06c = (nNodes >> 2) + 1;

	m_pSub20c->sub_10016b11((uint32)-1);

	return LTTRUE;
}


// 0x10017517 — every node told where it sits relative to its parent. The
// parent's transform is inverted, the node's own is applied to it, and the
// translation of the product is what is kept — so what these three floats
// hold is the offset from the parent, in the parent's frame.
//
// The frame is exactly three matrices and two dwords, and that is the whole
// reading of the expression: one named local for the product and the two
// temporaries `(~a) * b` needs, because `operator~` returns its matrix by
// value and `operator*` takes an lvalue for its left operand, so the inverse
// has to be copied out of the first temporary into the second before it can
// be multiplied (the `rep movsl` of 0x10 dwords).
//
// A node whose parent index is not a valid node index is a root, and gets the
// origin instead — the tree writes all-ones into the index, so the test is
// unsigned and against the node count.
//
// THE OFFSET IS AN LTVector, WRITTEN THROUGH ITS OWN INLINES. The shipped
// /O2 copy (0x00446f20, 157 bytes) CALLS Mat_InverseTransformation
// (0x00446fc0) where three plain float stores per arm let /O2 expand it
// (374 bytes). Inline budget is handed out to the LATER sites first, and
// the two arms are the later sites: `mRel.GetTranslation(v)` in the first
// arm alone left MatVMul_3x3 a call (332), and `v.Init()` in the else arm
// then left Mat_InverseTransformation one too -- lithtech.exe IDENTICAL,
// and its out-of-line Mat_InverseTransformation COMDAT matches 0x00446fc0
// with it. Emitted bytes of both arms are unchanged (integer moves, the
// xor-zero register). server.dll stays IDENTICAL (0x10017517). The three
// floats at +0x04 are ModelNode's offset vector; ModelNode is still the
// stand-in declaration of that class (commonmodel.h), hence the cast.
void Model::sub_10017517()
{
	uint32 i;

	for (i = 0; i < m_NodeTransforms.m_nElements; i++)
	{
		ModelNode* pNode = m_Nodes.m_pArray[i];

		if (pNode->m_iParent < m_NodeTransforms.m_nElements)
		{
			LTMatrix mRel = (~m_Nodes.m_pArray[pNode->m_iParent]->m_Mat2c)
			                * pNode->m_Mat2c;
            pNode->m_f04 = mRel.m[0][3]; pNode->m_f08 = mRel.m[1][3]; pNode->m_f0c = mRel.m[2][3];
		}
		else
		{
			pNode->m_f04 = pNode->m_f08 = pNode->m_f0c = 0;
		}
	}
}


// 0x10016c95 — a socket's default state.
ModelSocket::ModelSocket()
{
	m_Name[0] = '\0';
	m_iNode = 0;

	m_vPos.Init();
	m_rRot.Init();

	m_Unknown30 = 0;
}


// The Win32 import the child loop yields through. Declared rather than
// included: lthread.h pulls windows.h in and renames methods out from under
// this file (see the note there).



// 0x1001c604 — the whole model-data reader, and the child models are one of
// the things it finds.
//
// It walks four named chunks in order -- "Pieces", "Nodes", "ChildModels",
// "Animation" -- through the same mod_FindChunk that LoadHeader uses, sizing
// a collection off the wire before each and then filling it record by record.
//
// Two things in it are worth pointing at.
//
// A failed size on the collection at 0x8c returns LT_OK. Every other failure
// in the function jumps to the shared exit that answers the recorded result,
// and that one alone jumps to the SUCCESS return -- so a model whose
// out-of-bounds-box table cannot be allocated loads as if nothing happened,
// with none of its four chunks read.
//
// And the sixty-two single-byte reads are a skip, not an array: the same
// scratch byte is passed every time, so the loop discards sixty-two bytes of
// header it has no field for.
//
// BYTE-IDENTICAL on server.dll (0x1001c604). Four things closed it, each
// read off the shipped code:
//   * The animation and merge loops index the tables (`m_AnimArray[i]`,
//     `m_AnimArray[nInner]`) and VC6 strength-reduces them itself; the byte
//     offsets at -0x18/-0x1c are its induction temporaries, not locals. The
//     SIB roles `(%edi,%edx,1)` at 0x1001c9d1 are what an index gives.
//   * `pStream = pSaved` after the merge loop was not source: -0x40 is VC6's
//     spill of the stream across `rep movsl`, and the shipped `jbe` at
//     0x1001ca6c skips the reload when the inner loop never runs.
//   * The allocator named BEFORE the collection pointer (0x1001c73c loads
//     0x174 ahead of the `lea 0x34`).
//   * The success tail as a positive `if` holding `done:`; with the refusal
//     written first the `fail:` block ends up hosted at the end instead of
//     after the `!pPiece` block (0x1001c84f), and every jcc into it grows.
// lithtech.exe /O2 (0x0044ddc0) went 1979 -> 1853 of 1812 and now has the
// shipped three calls to sub_10017016 (the tail-duplication is gone). What
// is left is mostly the out-of-line CMoArray<T,NoCache>::SetSize2 for
// m_Pieces (0x0044dff6), which is the collection.h /Ob budget noted above
// Model::Model, plus the merge loop's store at -0x1c(%edx,%eax,1).
LTRESULT Model::LoadChildModels(CModelLoadRequest* pRequest)
{
    if (!pRequest || !model_ValidateStream(pRequest->m_pStream)) return LT_INVALIDFILE;
	CModelLoadRequest childReq;
	CModelLoadState   state;
	// The declaration order below is the frame, read off the shipped slots:
	// VC6 6.0 hands out -0x4 downward in declaration order, so the uint16
	// second and the uint8 twelfth are what put the two-byte hole at -0x8 and
	// the three-byte one at -0x30. `i` is one variable across the child, the
	// animation and the merge loops, which is why it is in memory (-0x4) while
	// the early loops' counters stay in registers. -0x18, -0x1c and -0x40 are
	// compiler temporaries (two induction offsets and a spill of pStream).
	uint32     i;            // -0x04
	uint16     nOOBB;        // -0x06
	ModelAnim* pAnim;     // -0x0c
	uint32     nChildren;    // -0x10
	uint32     nAnims;       // -0x14
	LTRESULT   dResult;      // -0x20
	uint32     k;            // -0x24
	uint32     nInner;       // -0x28
	uint32     nPieces;      // -0x2c
	uint8      nSkip;        // -0x2d
	Model*     pLoaded;      // -0x34
	uint32     n38;          // -0x38
	uint32     nPieceBase;   // -0x3c
	ModelNode* nOOBB2;       // -0x44
	// Never spilled, so their order is not observable.
	ILTStream* pStream;
	uint32     j;
	uint32     nZero;
	uint32     nSkipLeft;
	uint32     nAnimTotal;
	LTRESULT   dChild;
	CMoArray<ModelPiece*, NoCache>* pPieces;
	LAlloc*    pAlloc;

	// One guard, one answer, and the flag at +0x10 is "load the children too":
	// set it and the callback becomes mandatory, clear it and the callback is
	// never reached (the loop below answers LT_NOCHANGE for every child).
	if (!pRequest->m_pStream ||
	    (pRequest->m_bUnknown10 && !pRequest->m_pfnLoadChild))
		return LT_INVALIDPARAMS;

	sub_10017016(0);

	pStream = pRequest->m_pStream;
	dResult = LT_ERROR;

	if (!LoadHeader(pStream, &state))
		goto fail;

	if (!sub_1001c325(pStream, &m_pCommandString))
		goto fail;

	pStream->Read(&m_f0a4, sizeof(m_f0a4));
	pStream->Read(&nOOBB, sizeof(nOOBB));

	// Sixty-two bytes discarded, not read into anything: the same scratch byte
	// is passed every time round.
	for (nSkipLeft = 62; nSkipLeft; nSkipLeft--)
		pStream->Read(&nSkip, sizeof(nSkip));

	// The one failure in this function that answers LT_OK. Every other one
	// jumps to the shared exit and reports what it recorded; this jumps to the
	// SUCCESS return, so a model whose table at 0x8c cannot be allocated loads
	// as though nothing were wrong, with none of its four chunks read.
	if (!m_Unknown08c.SetSize2(nOOBB, (LAlloc*)m_pDataBlock))
#ifdef NOLFSRC_MODERN
		// fixed: Model::LoadChildModels reports success when it cannot size
		// one table (RETAIL_BUGS.md).
		goto fail;
#else
		goto done;
#endif

	for (j = 0; j < nOOBB; j++)
		pStream->Read(&m_Unknown08c.m_pArray[j], 4);

	if (!mod_FindChunk(pStream, "Pieces"))
		goto fail;

	pStream->Read(&n38, sizeof(n38));

	if (!m_Unknown054.SetSize2(n38, (LAlloc*)m_pDataBlock))
		goto fail;

	pStream->Read(&nPieces, sizeof(nPieces));

	// Through a pointer to the collection, which is what the shipped code
	// keeps in a register across the SetSize2 call and then reads the size and
	// the array off. Reaching for the members through `this` instead is a byte
	// cheaper per access and VC6 takes that deal, which then costs the piece
	// index below its register.
	pAlloc  = GetAlloc();
	pPieces = &m_Pieces;

	if (!pPieces->SetSize2(nPieces, pAlloc))
		goto fail;

	for (nZero = 0; nZero < pPieces->GetSize(); nZero++)
		(*pPieces)[nZero] = 0;

	nPieceBase = 0;

	for (j = 0; j < nPieces; j++)
	{
		ModelPiece* pPiece = BaseNew(GetAlloc(), (ModelPiece*)0, 1, this);

		if (!pPiece)
		{
			sub_10017016(0);
			goto fail;
		}

		m_Pieces.m_pArray[j] = pPiece;

		if (!pPiece->sub_1001c1a5(pStream, &nPieceBase))
			goto fail;
	}

	if (!mod_FindChunk(pStream, "Nodes"))
		goto fail;

	if (!m_pSub20c->sub_1001bd54(pStream))
		goto fail;

	if (!sub_1001c560(pStream))
		goto fail;

	if (!sub_100171f4(1))
		goto fail;

	if (!mod_FindChunk(pStream, "ChildModels"))
		goto fail;

	pStream->Read(&nChildren, sizeof(nChildren));

	if (nChildren == 0)
		goto fail;

	if (nChildren > 16)
		goto fail;

	for (i = 0; i < nChildren; i++)
	{
		CModelSub1D4* pRec;

		if (i == 0)
		{
			// Slot zero is the model itself, under the name the engine gives
			// it rather than one off the wire.
			pRec = &m_Sub1d4;

			if (!sub_10017b8a(0, pRec, this, "SELF"))
				goto fail;
		}
		else
		{
			// The name the file gave the child goes straight into the request
			// the reader's callback is about to be handed.
			if (!sub_1001c325(pStream, &childReq.m_pChildName))
				goto fail;

			childReq.m_pStream      = LTNULL;
			childReq.m_bUnknown10   = LTFALSE;
			childReq.m_pfnLoadChild = pRequest->m_pfnLoadChild;
			childReq.m_pContext     = pRequest->m_pContext;
            childReq.m_UserData = pRequest->m_UserData;

			pLoaded = LTNULL;

			if (pRequest->m_bUnknown10)
				dChild = pRequest->m_pfnLoadChild(&childReq, &pLoaded);
			else
				dChild = LT_NOCHANGE;

			if (dChild != LT_OK && dChild != LT_NOCHANGE)
			{
				dResult = dChild;
				goto fail;
			}

			// A callback that answered without producing a model is not a
			// failure; the request just records that not every child was found.
			if (!pLoaded)
				pRequest->m_bChildrenFound = LTFALSE;
			else if (pLoaded->m_nChildModels > 1)
			{
				dResult = 0x4d;
				goto fail;
			}

			pRec = BaseNew(GetAlloc(), (CModelSub1D4*)0, 1);

			if (!pRec)
				goto fail;

			m_pChildModels[i] = pRec;
			m_nChildModels++;

			if (!sub_10017b8a(i, pRec, pLoaded, childReq.m_pChildName))
				goto fail;
		}

		if (!pRec->sub_1001bf0d(pStream))
			goto fail;

		pRec->m_Unknown2c = 0;

		if (pRec->m_pOwner28) {
			pRec->m_Unknown2c = sub_10017b69(pRec->m_pOwner28, &nOOBB2);
            if (!pRec->m_Unknown2c) pRequest->m_bTreesValid = LTFALSE;
        }

		// Every child is a file read, so give the rest of the machine a turn.
		nolf::platform::yield_thread();
	}

	if (!mod_FindChunk(pStream, "Animation"))
		goto fail;

	pStream->Read(&nAnims, sizeof(nAnims));

	nAnimTotal = GetTotalAnimCount(0) + nAnims;
    if (nAnimTotal > 65536) goto fail;

	// The animation table is the one collection here sized out of m_pAlloc
	// rather than the model's own data block.
	if (!m_AnimArray.SetSize2(nAnimTotal, (LAlloc*)m_pAlloc))
		goto fail;

	for (i = 0; i < nAnims; i++)
	{
		// ModelAnim above is ModelAnim under its vtable's name: 0x5c bytes
		// with a virtual destructor, which is what this sizes and what the
		// release on the failure path below sizes too.
		pAnim = BaseNew(GetAlloc(), (ModelAnim*)0, 1, this);

		if (!pAnim)
			goto fail;

		m_AnimArray[i].m_pAnim       = (ModelAnim*)pAnim;
		m_AnimArray[i].m_pChildModel = &m_Sub1d4;

		if (!m_AnimArray[i].sub_1001bec9(pStream))
		{
			goto fail;
		}
	}

	// Every child model's animations are appended to ours, each one tagged
	// with the child record it came in with, and each child told where in our
	// table its own block begins.
	nInner = nAnims;

	for (i = 0; i < m_nChildModels; i++)
	{
		CModelSub1D4* pRec = m_pChildModels[i];

		if (!pRec->m_pOwner28 || pRec->m_pOwner28 == this)
			continue;

		pRec->m_Unknown18 = nInner;

		for (k = 0; k < pRec->m_pOwner28->m_AnimArray.m_nElements; k++)
		{
			m_AnimArray[nInner] = pRec->m_pOwner28->m_AnimArray[k];
			m_AnimArray[nInner].m_pChildModel = pRec;
			nInner++;
		}
	}

	if (!sub_1001c36c(pRequest, pStream, 1))
		goto fail;

	if (!LoadSockets(pStream))
		goto fail;

	if (pStream->ErrorStatus() != LT_OK)
		goto fail;

	if (sub_100171f4(0))
	{
		sub_100176dc();
		sub_10017517();
		done:
		return LT_OK;
	}

fail:
	sub_10017016(0);
	return dResult;
}




// --------------------------------------------------------------------- //
//  0x00447180    37 bytes (server.dll 0x100176b7) -- declared, not defined
// --------------------------------------------------------------------- //
//
// Not one of this file's rows, but sub_1001c36c below calls it and nothing declares
// it.  It walks m_AnimArray (array 0x168, count 0x16c, stride 0x20 = one
// AnimInfo) and counts the entries whose animation belongs to THIS model --
// `mov (%edx),%edi; cmp %ecx,0x20(%edi)`, i.e. m_pArray[i].m_pAnim->m_pModel
// == this.  It is the per-model half of GetTotalAnimCount (0x00447130 /
// 0x1001767a), which sums the same thing over the child records.
//
// Declared through a shim rather than added to Model, so that no header
// moves with this body.  IT BELONGS ON Model, beside GetTotalAnimCount, as
//
//     uint32 sub_100176b7();               // 0x100176b7
//
// and the call below should then lose the cast.


// AnimInfo::m_pad_14 in commonmodel.h is this: the three floats the
// "AnimBindings" chunk writes into the table entry (0x0044dad1-0x0044dade).
// IT BELONGS IN commonmodel.h as `LTVector m_vAnimBinding;  // 0x14`,
// replacing m_pad_14; the overlay is here only so that this move touches no
// header (a commonmodel.h change has to be measured against both images).
// --------------------------------------------------------------------- //
//  0x1001c36c   271 bytes   Model::sub_1001c36c
//                           (lithtech.exe 0x0044d9a0, 386 bytes)
//
//  MOVED HERE from src/engine/lithtech/modchunk.cpp, which is in
//  lithtech.exe's source list only -- so while the body lived there,
//  server.dll's copy was never compiled even though LoadChildModels above
//  calls it and commonmodel.h declares it.  lithtech.exe's 0x0044d9a0 is
//  byte-identical and must stay so.
// --------------------------------------------------------------------- //
//
// The "AnimBindings" chunk.  commonmodel.h declares it from LoadChildModels'
// call site and says "271 bytes, not reconstructed"; the call site at
// 0x0044e469 pushes `1`, the stream and the request in that order, which is
// the (pRequest, pStream, nFlag) the declaration already has.  nFlag is
// pushed by the caller and READ NOWHERE in the body -- there is no reference
// to either of the last two argument slots -- so the third parameter is
// carried and unused, and `ret $0xc` is what keeps it in the signature.
//
// What it does: for each child-model record the model holds (m_pChildModels
// at 0x190, m_nChildModels at 0x1d0), read how many animation bindings the
// file has for it, then read that many <name, dims, binding> records and
// push the binding onto the matching entry of the child's own animation
// table.
//
// The VALIDATION is the interesting half, and it reports rather than fails:
// when the record has an owner, the count off the wire has to agree with the
// owner's own animation count AND the record's m_Unknown20 has to agree with
// the owner's own record's (0x1f4 = 0x1d4 + 0x20 = Model::m_Sub1d4's
// m_Unknown20).  If either disagrees the request's m_bUnknown18 is cleared
// and the read CARRIES ON -- so that flag is "the animation bindings in this
// file still describe the models it names", which is exactly the shape
// serverengine.h already records for the four request flags ("the reader
// clears the ones it cannot satisfy").
//
// Six floats per binding, three of them read and thrown away.  The first
// triple (0x0044da59-0x0044da80) is never stored anywhere; only the second
// (0x0044da83-0x0044daac) reaches the table entry, at +0x14.  Both are read
// through the stream so neither can be optimised out, and the discarded one
// is in the file format whether or not this build wants it.
//
// The name buffer is 0x100 bytes (the ReadString limit at 0x0044da46), which
// is a quarter of the kilobyte Model::sub_1001c325 uses -- so this reader
// does NOT go through sub_1001c325, and indeed it does not: the name is only
// a lookup key here, never stored, so there is nothing to re-home onto the
// model.
LTBOOL Model::sub_1001c36c(CModelLoadRequest* pRequest, ILTStream* pStream,
                           int nFlag)
{
	uint32 i, j;
	uint32 nAnims;
	uint32 iAnim;
	CModelSub1D4* pRec;
	AnimInfo* pInfo;
	LTVector vBinding;
	LTVector vDims;
	char szName[0x100];

	if (!mod_FindChunk(pStream, "AnimBindings"))
		return LTFALSE;

	for (i = 0; i < m_nChildModels; i++)
	{
		pRec = m_pChildModels[i];

		pStream->Read(&nAnims, sizeof(nAnims));

		if (pRec->m_pOwner28)
		{
			if (nAnims != pRec->m_pOwner28->OwnedAnimationCount() ||
			    pRec->m_Unknown20 != pRec->m_pOwner28->m_Sub1d4.m_Unknown20)
			{
				pRequest->m_bUnknown18 = LTFALSE;
			}
		}

		for (j = 0; j < nAnims; j++)
		{
			if (pStream->ReadString(szName, sizeof(szName)) != LT_OK)
				return LTFALSE;

			w_ReadVector(pStream, &vDims);
			w_ReadVector(pStream, &vBinding);

			if (pRec->m_pOwner28)
			{
				pInfo = FindAnimInModel(szName, pRec->m_pOwner28, &iAnim);

				if (pInfo)
					pInfo->m_vAnimBinding = vBinding;
			}
		}
	}

	return LTTRUE;
}


// 0x1001c47b — the socket table off the wire, and the near-twin of the
// weight-set reader below. Two things differ and both are visible in the
// bytes: nothing is handed back first (there is no FreeArrayElements call, so
// re-reading a model over a live one leaks its old sockets where re-reading
// its weight sets does not), and the filling loop counts against the number
// READ rather than against the array's own size.
//
// Each socket is checked as it arrives: the node it hangs off has to be a real
// node of this model, which is the only validation in the reader.
//
// IDENTICAL, 229 of 229 bytes, and TWO STATEMENT POSITIONS did it. The
// shipped frame is a single dword and puts the fill loop's counter in the
// pStream PARAMETER's slot -- dead once the stream is cached in %ebx -- where
// ours spilled it to a local of its own and paid four bytes plus a swap of
// the registers holding `this` and &m_Sockets. That whole cascade came from
// `pSockets = &m_Sockets;` sitting ABOVE the count read: it made &m_Sockets
// live across ILTStream::Read, which took the callee-saved register %ebx
// wanted for pStream, which left the parameter slot live, which cost the
// frame slot. Moved BELOW the SetSize2 call, every one of those resolves at
// once.
//
// The second is the sizing call's own `this`. Shipped evaluates the
// allocator into a register BEFORE taking &m_Sockets (`mov 0x174(%esi),%eax;
// lea 0x154(%esi),%edi; push %eax`), which only happens when the `lea` is
// part of the CALL and not of an earlier assignment: written
// `pSockets->SetSize2(...)` with pSockets already in hand, VC6 folds the
// allocator into `push 0x174(%edi)` and the function is 228 bytes with one
// instruction missing. `m_Sockets.SetSize2(...)`, with pSockets taken from
// the CSE afterwards, is the shipped shape. (Both are one `lea` either way
// -- this is about where it lands, not how many.)
//
// Also settled here, since three earlier shapes were tried against the old
// ordering and none of them worked: one counter for both loops, and no
// `pSockets` local at all (237 bytes). Neither is the answer; the ordering
// was.
//
// LITHTECH.EXE (0x0044db30, 299 bytes): IDENTICAL once the sizing and the
// zero-fill became ONE call, `SetSizeInit4(n, LTNULL, alloc)` -- dynarray's
// own member, which is SetSize2 followed by exactly the reloading
// `i < GetSize()` fill loop the shipped bytes carry. /O2 then CALLS SetSize2
// (0x0044b6b0) from inside it, as shipped, where the hand-written pair had
// it expanded (381 bytes). The rest is inline budget: it takes TWO of the
// four accessor spellings below (`*pStream >>` for either read,
// `m_Sockets[i]`, GetSize()) to leave SetSize2 out of line -- any two do
// it, and all four are kept as the one consistent spelling. server.dll
// does not move (229 of 229). The `pSockets` local, and the placement
// argument above, went with the hand-written loop.
LTBOOL Model::LoadSockets(ILTStream* pStream)
{
	uint32 i;
	uint32 nSockets;
	ModelSocket* pSocket;

	if (!mod_FindChunk(pStream, "Sockets"))
		return LTFALSE;

	*pStream >> nSockets;

	if (!m_Sockets.SetSizeInit4(nSockets, LTNULL, GetAlloc()))
		return LTFALSE;

	for (i = 0; i < nSockets; i++)
	{
		pSocket = BaseNew(GetAlloc(), (ModelSocket*)0, 1);

		if (!pSocket)
			return LTFALSE;

		m_Sockets[i] = pSocket;

		*pStream >> pSocket->m_iNode;

		if (pSocket->m_iNode >= m_NodeTransforms.GetSize())
			return LTFALSE;

		if (pStream->ReadString(pSocket->m_Name, sizeof(pSocket->m_Name)) != LT_OK)
			return LTFALSE;

		w_ReadRotation(pStream, &pSocket->m_rRot);
		w_ReadVector(pStream, &pSocket->m_vPos);
	}

	return LTTRUE;
}


// 0x1001c560 — the weight-set table out of the file. The old table is given
// back first, so re-reading a model over a live one is safe, and then one
// record is allocated per entry and asked to read itself. The array is
// zero-filled between the sizing and the filling, which is what lets the
// failure path hand the whole thing back whether or not the entry that
// failed had been stored yet.
//
// LITHTECH.EXE (0x0044dc60, 339 bytes): IDENTICAL, from 413, by three
// source facts the /O1 server (164 of 164 either way) cannot see:
//  - ModelWeightSet has a real destructor, out of line (0x004462c0 /
//    0x10016c69, declared in commonmodel.h). Without it VC6 expanded the
//    implicit one into both release loops; shipped calls it (0x0044dd6e).
//  - the sizing and zero-fill are one SetSizeInit4 call (see LoadSockets),
//    which puts SetSize2 out of line (0x0044dcd0) as shipped.
//  - `pSet = m_WeightSets[i] = BaseNew(...)`: the reference operator[]
//    returns is the shipped `lea (%ecx,%ebp,4),%eax` that is then dropped
//    for the stored value (0x0044dd23). Going through operator[] at all
//    is also what turns the FIRST release loop's destructor into the
//    shipped call to ??_G with 0 (0x0044dc8d): budget again.
LTBOOL Model::sub_1001c560(ILTStream* pStream)
{
	uint32 i;
	uint32 nSets;
	ModelWeightSet* pSet;
	CMoArray<ModelWeightSet*, NoCache>* pSets;

	pSets = &m_WeightSets;

	FreeArrayElements(pSets, (LAlloc*)m_pDataBlock);

	pStream->Read(&nSets, sizeof(nSets));

	if (!pSets->SetSizeInit4(nSets, LTNULL, (LAlloc*)m_pDataBlock))
		return LTFALSE;

	for (i = 0; i < m_WeightSets.m_nElements; i++)
	{
		pSet = m_WeightSets[i] = BaseNew(GetAlloc(), (ModelWeightSet*)0, 1, this);

		if (!pSet || !pSet->sub_1001c2bc(pStream))
		{
			FreeArrayElements(pSets, (LAlloc*)m_pDataBlock);

			return LTFALSE;
		}
	}

	return LTTRUE;
}


// 0x10016c69 (lithtech.exe 0x004462c0) -- give the weights back to the
// model's allocator.  The member's own destructor then empties it again
// against the default one, which is the second Term in both images.
ModelWeightSet::~ModelWeightSet()
{
	m_Weights.Term(m_pModel ? m_pModel->GetAlloc() : &g_DefAlloc);
}


// 0x1001c2bc — one weight set: the name, then one four-byte weight per node
// of the model. The count is read off the wire rather than taken from the
// model, and it is what the fill loop runs to — so a set that names fewer
// weights than the model has nodes leaves the rest of the array as the
// sizing left it (name ours).
//
// Two in-class accessors, and each closes one image. The allocator comes
// through Model::GetAlloc() rather than a cast of m_pDataBlock: with the
// cast, /O1 (0x1001c2bc) swapped esi/edi (stream in esi, `this` in edi, the
// shipped build the other way round) with the prologue pushes straddling the
// stream load. The weight slot is `&m_Weights[i]` through operator[] rather
// than `&m_Weights.m_pArray[i]`: with the bare member, /O2 (0x44d870 in
// lithtech.exe, where SetSize2 is expanded in place) came out 233 bytes to
// the shipped 208. Neither accessor changes the other image's bytes.
LTBOOL ModelWeightSet::sub_1001c2bc(ILTStream* pStream)
{
	uint32 i;
	uint32 nWeights;

	if (pStream->ReadString(m_Name, sizeof(m_Name)) != LT_OK)
		return LTFALSE;

	pStream->Read(&nWeights, sizeof(nWeights));

	if (!m_Weights.SetSize2(nWeights, m_pModel->GetAlloc()))
		return LTFALSE;

	for (i = 0; i < nWeights; i++)
		pStream->Read(&m_Weights[i], sizeof(uint32));

	return LTTRUE;
}


// 0x1001bc69 — one animation out of the model file. It starts by giving back
// whatever the last read left behind (the release at 0x100167e1), which is
// what makes re-reading a model over a live one safe, and then: the name, two
// dwords the FILE VERSION gates (0x18 from version 11 on, the interpolation
// time from version 12 on — older files keep the constructor's -1 and 0xc8),
// the keyframe table sized off the wire, and then per keyframe a time and a
// string key. The string keys go through the model's own name store, so a
// keyframe's script text is owned by the model and not by the animation.
//
// The tree comes last: the record at +0x58 is pointed at the model's root
// node (+0x2c0) and asked to read itself, which pulls in the whole node
// hierarchy's transforms (0x1001bb37). Two separate loads of +0x58 for those
// two statements, which is what says they are two statements.
//
// The four fixed-size reads are the SDK's `*pStream >> x` (iltstream.h's
// operator>> -> LTStream_Read -> Read), not direct Read calls. The bytes are
// the same, but the two inline levels per read spend /O2 inline budget: with
// direct Read calls lithtech.exe (0x0044cf50) expanded BaseDelete and BaseNew
// inside the keyframe SetSize2 (342 bytes); with the operator it CALLS them
// (0x004152b0, 0x0044c7d0) as shipped, 306 of 306. The one in the keyframe
// loop alone is enough; server.dll is unchanged either way.
LTBOOL ModelAnim::sub_1001bc69(ILTStream* pStream)
{
	uint32 i, nKeys;

	sub_100167e1();

	if (!m_pModel->sub_1001c325(pStream, &m_pName))
		return LTFALSE;

	if (m_pModel->m_nFileVersion >= 11)
		*pStream >> m_n18;

	if (m_pModel->m_nFileVersion >= 12)
		*pStream >> m_Unknown1c;

	*pStream >> nKeys;

	if (!m_Array08.SetSize2(nKeys, (LAlloc*)m_pModel->m_pDataBlock))
		return LTFALSE;

	for (i = 0; i < nKeys; i++)
	{
		AnimKeyFrame* pKey = &m_Array08.m_pArray[i];

		*pStream >> pKey->m_Time;

		if (!m_pModel->sub_1001c325(pStream, &pKey->m_pString))
			return LTFALSE;
	}

	m_p58->m_pSub20c04 = m_pModel->m_pSub20c;

	if (m_p58->sub_1001bb37(pStream))
		return sub_10016897(LTTRUE);

	return LTFALSE;
}


// 0x10016352 — fifteen bytes: keep the pointer, clear the claim flag.
// The store order is lithtech.exe's (0x00445220: `mov %ecx,0x4(%eax)` then
// `movl $0x0,(%eax)`), which follows source order; /O1 hoists its
// `andl $0x0` to the top whichever way the source reads.
CMoDataRef::CMoDataRef(void* pData)
{
	m_pAlloc = (LAlloc*)pData;
	m_pLinks = 0;
}


// 0x10016361 — five bytes: a tail `jmp` into the release below, which is what
// VC6 emits for a destructor whose whole body is one call and which has no
// sub-objects of its own to tear down.
CMoDataRef::~CMoDataRef()
{
	sub_10016366();
}


// 0x10016366 — every block back to the allocator, then the head cleared.
// The next pointer is read out of each block before it goes, and the block
// is released through LDelete: that is the second null test inside the loop
// (0x10016376, on a pointer the loop condition has already tested) and the
// allocator loaded ahead of it, BaseDelete's own `if (!ptr) return` expanded
// in place with its pAlloc argument already evaluated.
void CMoDataRef::sub_10016366()
{
	CMoDataLink* pCur;
	CMoDataLink* pNext;

	pCur = m_pLinks;

	while (pCur)
	{
		pNext = pCur->m_pNext;
		LDelete(m_pAlloc, pCur);
		pCur = pNext;
	}

	m_pLinks = 0;
}


// 0x100163fa — fill the data-reference slot, once. The first word is the
// "already claimed" flag and nothing here sets it, so what refuses a second
// call is whatever else in the model writes it; from this side all that is
// visible is that a nonzero first word means the pointer is left alone and
// the answer is LTFALSE. Twenty-two bytes with the two returns spelled the
// /O1 way: `xor %eax,%eax` for the refusal and `push $1; pop %eax` for the
// success.
LTBOOL CMoDataRef::Set(void* pData)
{
	if (m_pLinks)
		return LTFALSE;

	m_pAlloc = (LAlloc*)pData;

	return LTTRUE;
}


// 0x1001638c — a name into the model's string store. The store is searched
// first and an identical name comes straight back, so a model file naming
// the same thing twice costs one block; a new name gets a block of its own,
// twelve bytes of header and the string, pushed on the front of the list.
//
// Three things it does not do, and each is in the bytes: it never answers
// null (a null name, an empty name and a refused allocation all come back as
// the shared empty string, which is why every reader can strlen the answer);
// it never compares case-insensitively (0x1003e1f0 is strcmp); and it never
// checks whether the block it just took is big enough, because the size it
// asked for was computed from the same strlen.
//
// The argument push is SHARED between the two library calls: the loop pushes
// pName at its top, uses it as strcmp's second argument each time round, and
// the exit path — reached with that push still standing — uses it as
// strlen's only argument. That is the compiler's, not the source's.
char* CMoDataRef::sub_1001638c(char* pName)
{
	CMoDataLink* pCur;
	CMoDataLink* pNew;
	uint32 nSize;

	if (pName && pName[0])
	{
		pCur = m_pLinks;

		while (pCur)
		{
			if (strcmp(pCur->m_Name, pName) == 0)
				return pCur->m_Name;

			pCur = pCur->m_pNext;
		}

		const auto length = strlen(pName);
        if (length > std::numeric_limits<uint32>::max() - sizeof(CMoDataLink)) return "";
        nSize = static_cast<uint32>(sizeof(CMoDataLink) + length);

		pNew = (CMoDataLink*)m_pAlloc->Alloc(nSize);

		if (pNew)
		{
			pNew->m_nSize = nSize;
			pNew->m_pNext = m_pLinks;
			m_pLinks      = pNew;

			strcpy(pNew->m_Name, pName);

			return pNew->m_Name;
		}
	}

	return "";
}


// ---- the model file reader (0x1001bac6, 0x1001cb56, 0x1001cdd5) --------- //

// 0x1001bac6 — walk the file's chunk directory looking for one by name
// (name ours; the only naming evidence is the "Header" literal LoadHeader
// hands it). The file is a singly linked list of chunks: each record is a
// string and then the offset of the next one, and all-ones ends the list. The
// walk is capped at 0x200 records, so a file whose list loops on itself is
// refused rather than followed forever.
//
// Two things read off the bytes. The name buffer is 0x100 bytes and it is the
// whole frame, because the next-chunk offset lives in the dead pStream
// parameter slot rather than in a word of its own. And the cleanup after
// stricmp is two `pop %ecx` rather than `add $0x8,%esp` — /O1 spending two
// bytes instead of three, not a varargs prototype.
LTBOOL mod_FindChunk(ILTStream* pStream, char* pName)
{
	char   sName[0x100];
	uint32 i;
	uint32 nNext;

	pStream->SeekTo(0);

	for (i = 0; i < 0x200; i++)
	{
		pStream->ReadString(sName, sizeof(sName));
		pStream->Read(&nNext, sizeof(nNext));

		if (strcmp(pName, sName) == 0)
			return LTTRUE;

		if (nNext == 0xffffffff)
			break;

		pStream->SeekTo(nNext);
	}

	return LTFALSE;
}

// 0x1001cb56 — the "Header" chunk. Four versions are accepted and the test
// is written the long way round: four `cmp`/`je` against 9, 10, 11 and 12,
// the last one jumping backwards into the chunk-not-found return, which is
// VC6 tail-merging the second `return LTFALSE` onto the first.
//
// The version word is read straight into the model's own field rather than
// into a local — `add $0x204,%esi` puts &m_nFileVersion in a register and the
// value is then re-read from memory for the comparisons.
LTBOOL Model::LoadHeader(ILTStream* pStream, CModelLoadState* pState)
{
	if (!mod_FindChunk(pStream, "Header"))
	{
		return LTFALSE;
	}
	else
	{
		pStream->Read(&m_nFileVersion, sizeof(m_nFileVersion));

		if (m_nFileVersion != 9 && m_nFileVersion != 10 &&
		    m_nFileVersion != 11 && m_nFileVersion != 12)
		{
			return LTFALSE;
		}
		else
		{
			return pState->m_Counts.Read(pStream) != 0;
		}
	}
}

// 0x1001cdd5 — nothing but the sub-object's constructor and the `this` the
// caller gets back: `push %esi; mov %ecx,%esi; call; mov %esi,%eax`.
// Defined AHEAD of Clear, which is the shipped order: 0x1001cdd5 + 12 is
// 0x1001cde1 and the call's displacement is 4. The
// counts record's own constructor has to be out of line for the call to
// survive — /O1 implies /Ob1, so a body in the class declaration would be
// expanded here instead.
CModelLoadState::CModelLoadState()
{
	m_Counts.Clear();
}

// 0x1001cde1 — the counts record cleared. Thirteen stores off one `xor
// eax,eax`, which is what says the source assigned each field rather than
// memsetting the record (a memset would be `rep stos`) — and, with an array
// standing in for thirteen fields whose names are not recoverable, thirteen
// subscripts is the only spelling that produces it: /O1 does not unroll a
// loop.
//
// A constructor is what this looks like and is not what it is: written as
// `CModelFileCounts::CModelFileCounts()` our build comes out 43 bytes, two
// over, because VC6 makes a constructor return `this` (`mov %ecx,%eax`) and
// the shipped function leaves the zero register in eax instead. So the
// shipped source has an ordinary void member here and calls it from
// CModelLoadState's constructor.
void CModelFileCounts::Clear()
{
	m_n[0]  = 0;
	m_n[1]  = 0;
	m_n[2]  = 0;
	m_n[3]  = 0;
	m_n[4]  = 0;
	m_n[5]  = 0;
	m_n[6]  = 0;
	m_n[7]  = 0;
	m_n[8]  = 0;
	m_n[9]  = 0;
	m_n[10] = 0;
	m_n[11] = 0;
	m_n[12] = 0;
}

// 0x1001ce0a — and the counts read back off the wire, thirteen four-byte
// reads and then one question to the stream about whether any of them
// failed. Nothing checks the individual reads, which is exactly what
// ILTStream::Read's contract says to do: it zero-fills on failure so that a
// caller can read everything and test ErrorStatus() once at the end.
//
// The four is hoisted into ebx (`push $0x4; pop %ebx`) and pushed thirteen
// times from there, so every read uses the same size constant. The answer is
// `neg; sbb; inc` — an `== 0` compare, i.e. against LT_OK, not a `!= 0`.
LTBOOL CModelFileCounts::Read(ILTStream* pStream)
{
	pStream->Read(&m_n[0],  sizeof(m_n[0]));
	pStream->Read(&m_n[1],  sizeof(m_n[1]));
	pStream->Read(&m_n[2],  sizeof(m_n[2]));
	pStream->Read(&m_n[3],  sizeof(m_n[3]));
	pStream->Read(&m_n[4],  sizeof(m_n[4]));
	pStream->Read(&m_n[5],  sizeof(m_n[5]));
	pStream->Read(&m_n[6],  sizeof(m_n[6]));
	pStream->Read(&m_n[7],  sizeof(m_n[7]));
	pStream->Read(&m_n[8],  sizeof(m_n[8]));
	pStream->Read(&m_n[9],  sizeof(m_n[9]));
	pStream->Read(&m_n[10], sizeof(m_n[10]));
	pStream->Read(&m_n[11], sizeof(m_n[11]));
	pStream->Read(&m_n[12], sizeof(m_n[12]));

	return pStream->ErrorStatus() == LT_OK;
}

// 0x1001cec1 — how many bytes the file's data will need, from the thirteen
// counts the header chunk carried. Twenty-four terms, each one a count (or a
// product of two) times a record size, accumulated through the caller's
// pointer: every `+=` stores, so the shipped code has a `mov %reg,(%eax)`
// after each and re-reads each count out of `this` afterwards, because a
// store through `uint32*` may have hit the counts. It forwards its own store
// into the accumulator register, which is why the *running total* is never
// re-read.
//
// A file that declares none of whatever the ninth count counts is refused
// outright, and the compare says how it was written: `cmpl $0x1,0x20(%ecx)`
// with `jae` past the failure is `< 1` on an unsigned, not `== 0` (which
// would compare against zero).
//
// The record sizes are read straight off the multipliers — 12, 28, 4, 48,
// 20, 36, 108, 40, 52, 20, 64, 92, 46, 32 — and 46 is not a mistake: it
// really is `imul $0x2e`, folded together with a neighbouring term so that
// the common `* 4` is factored out. Which count means what is not readable
// from here (only the arithmetic is), so they stay an array.
//
// The `(m_n[2] - 1)` at the top is loaded before the first store and reused
// three times over, and `(m_n[2] - 1) * 46` twice; the counts VC6 does NOT
// hold across a store it reloads every time. Naming the first as a local is
// what puts the load above the store.
//
// NEAR (309 of 309 bytes, 118 of 118 instructions, zero mnemonic blocks): two
// bytes differ, and they are the two memory operands of ONE commutative
// product swapping roles. For `m_n[2] * m_n[10] * 4` the shipped build emits
// `mov 0x8(%ecx),%esi; imul 0x28(%ecx),%esi` and ours the other way round.
// Every other product in the function agrees, and they all follow the same
// rule — move the higher offset, multiply by the lower (0x08*0x00,
// 0x10*0x08, 0x20*0x0c) — so it is this one term in the shipped code that
// breaks the pattern, not ours.
//
// Probed to a firm negative: both operand orders, `(a*b)*4`, `a*(b*4)`,
// `(a*4)*b`, `4*a*b`, `a*4*b`, an `(int)` cast on either side, and a
// block-scoped local holding m_n[2] before the multiply — VC6 canonicalises
// all nine to the same two instructions (so do `(a*b) << 2`, `b*sizeof(uint32)*a`
// and `(a+0)*b*4`; `(b<<2)*a` adds a shl; `(nOther + 1) * m_n[10] * 4`
// gives 311 bytes and 12 blocks; dropping nOther at every use, or at any
// one of its three uses, gives 309/115, 311/119, 311/119 or 309/116; all
// counts read through a `uint32* pn = m_Counts.m_n` local gives 10 differing
// instructions; `*(&m_n[2]) * *(&m_n[10])` is unchanged) — and cl 8168, 8804 and 8966 emit
// what 8447 does. Same class as the SIB base/index roles recorded in
// 09-codegen-patterns for SpriteControl::GetNumFrames and
// CServerMgr::EndWorld: which operand of a symmetric pair becomes the
// accumulator is not reachable from source.
LTBOOL CModelLoadState::sub_1001cec1(uint32* pnBytes)
{
	if (m_Counts.m_n[8] < 1)
		return LTFALSE;

	*pnBytes = 0;

	uint32 nOther = m_Counts.m_n[2] - 1;

	*pnBytes += m_Counts.m_n[11] * 12;
	*pnBytes += ((m_Counts.m_n[11] + 1) * 3 + m_Counts.m_n[12]) & 0xfffffffc;
	*pnBytes += m_Counts.m_n[0] * m_Counts.m_n[2] * 28;
	*pnBytes += nOther * m_Counts.m_n[1] * 4;
	*pnBytes += nOther * m_Counts.m_n[1] * 48;
	*pnBytes += m_Counts.m_n[0] * 20;
	*pnBytes += (m_Counts.m_n[2] * m_Counts.m_n[1] + nOther * 46) * 4;
	*pnBytes += m_Counts.m_n[4] * m_Counts.m_n[2] * 28;
	*pnBytes += m_Counts.m_n[5] * 32;
	*pnBytes += m_Counts.m_n[6] * 32;
	*pnBytes += (m_Counts.m_n[8] - 1) * m_Counts.m_n[3] * 36;
	*pnBytes += m_Counts.m_n[3] * 4;
	*pnBytes += m_Counts.m_n[3] * 108;
	*pnBytes += m_Counts.m_n[2] * m_Counts.m_n[10] * 4;
	*pnBytes += m_Counts.m_n[10] * 4;
	*pnBytes += m_Counts.m_n[10] * 40;
	*pnBytes += m_Counts.m_n[9] * 4;
	*pnBytes += m_Counts.m_n[9] * 52;
	*pnBytes += (m_Counts.m_n[8] - 1) * 4;
	*pnBytes += m_Counts.m_n[7] * 20;
	*pnBytes += m_Counts.m_n[2] * 64;
	*pnBytes += m_Counts.m_n[2] * 4;
	*pnBytes += (m_Counts.m_n[4] - 1) * 48;
	*pnBytes += m_Counts.m_n[1] * 92;

	return LTTRUE;
}


// 0x1001c325 — one name out of the file and onto the model. The buffer is a
// kilobyte of the caller's stack (`mov $0x1000,%eax; call __chkstk`, which is
// what fixes its size), the stream's ReadString fills it, and the copy the
// model owns is what the caller gets back. Every name in a model file comes
// through here: the model's own, each animation's, and each keyframe's string
// key (name ours).
LTBOOL Model::sub_1001c325(ILTStream* stream, char** name) {
    if (!stream || !name) return LTFALSE;
    unsigned char size[2];
    if (stream->Read(size,2) != LT_OK) return LTFALSE;
    std::string text(static_cast<std::size_t>(size[0]) | (static_cast<std::size_t>(size[1]) << 8),'\0');
    if (stream->Read(text.data(),static_cast<uint32>(text.size())) != LT_OK) return LTFALSE;
    *name = sub_100173fa(text.data());
    return text.empty() || (*name)[0];
}

// 0x1001bec9 — one entry of the animation table, off the wire: the three
// floats ModelEdit measured the animation's bounds as, and then the animation
// itself. An entry with no animation record answers LTFALSE, which is what
// makes the null test the interesting half — the table is sized and its
// records allocated before this runs, so a null here is an allocation that
// failed (name ours).
LTBOOL AnimInfo::sub_1001bec9(ILTStream* pStream)
{
	pStream->Read(&m_vDims.x, 4);
	pStream->Read(&m_vDims.y, 4);
	pStream->Read(&m_vDims.z, 4);

	if (m_pAnim)
		return ((ModelAnim*)m_pAnim)->sub_1001bc69(pStream);

	return LTFALSE;
}

// 0x10015ce2 — the rotation half of a 4x4, back out as a quaternion. This
// one needs no reconstruction: it is `quat_ConvertFromMatrix` from the SDK's
// own ltquatbase.cpp, and the shipped bytes agree with that source line for
// line — the +1.0f folded into the diagonal sum, the QUAT_TOLERANCE compare
// against 0.00001f, the `return` inside the small-diagonal arm, and the
// static { QY, QZ, QX } table at 0x10052ed0 (which reads back as 1, 2, 0).
// The two arms take their square root through the CRT with the usual
// `push ecx; push ecx; fstpl (%esp)` double widening.
//
// The engine's own copy of the file is a separate translation unit from the
// model code (it sits at 0x10015bcd-0x100160xx, beside quat_ConvertToMatrix);
// it lives here because that unit has not been split out.



// 0x10015e17 and 0x10015e69 — the other two functions of ltquatbase.cpp,
// beside quat_ConvertFromMatrix above and quat_ConvertToMatrix (0x10015bcd,
// reconstructed in sharedcommon.cpp). Neither needed reconstructing either:
// both are the SDK's own source verbatim, and the bytes confirm every line.
//
// The two names collide with servermath.h's, which are OURS and wrong: the
// 25-byte 0x10011802 and the 29-byte 0x1001181b that file calls
// quat_GetVectors/quat_Slerp are the math module's forwarders *into* these,
// and the real ltquatbase names belong here. Keeping both spellings in one
// program is what the shipped image does — two addresses apiece — so the
// declarations are left alone and these are the SDK prototypes.
void quat_ConvertToMatrix(const float* pQuat, float mat[4][4]);   // 0x10015bcd

// The 0x40-byte frame is the `float mat[4][4]` whose address is taken, which
// is also why this one carries an ebp frame at all. The nine copies out are
// three FPU loads (one per destination vector's x) and six integer moves —
// only the first component of each vector goes through st0, because only the
// first store is where VC6 has already got a float in a register.


// The interpolation. Three details are worth reading off the bytes because
// they are what says this is the SDK source rather than something like it:
//
//  - the 0x14 frame is `rot1q[4]` plus one float, and that float is
//    scalerot0; scalerot1 never leaves st0, and `omega` and `sinom` are
//    parked in the dead pQ1 and pQ2 parameter slots (0xc(%ebp), 0x10(%ebp)),
//    which is where VC6 puts a local once the argument it displaces is in a
//    register;
//  - the sign-adjust loop is written out twice, once negating and once as a
//    four-dword `movsl` run, because one arm is `-pQ2[j]` and the other a
//    plain copy — a `rep movs` would not survive the negation;
//  - `(1.0 - cosom) > 0.00001f` is compared in *double* (`fcompl` against
//    0x1004d938, which reads as 1e-05), because the SDK wrote the 1.0 as a
//    double literal. The `1.0 - t` beside it does the same.



// ---- the socket walk (0x100175b6, 0x10017c17) --------------------------- //

// (ModelWalker's constructor is defined at the top of this file, above
// GetNodeTransform and GetSocketTransform: VC6's inliner is forward-only and
// both of them expand it.)

// One socket's 4x4, in whatever space the walker was set up in. The socket
// is an offset and a rotation hung off a node, so the answer is the node's
// transform composed with the socket's own — built here as a matrix from
// the quaternion with the offset dropped into its fourth column.
//
// The three guards and the failed evaluation all end at one `xor eax,eax`
// immediately before the epilogue, so they are AND-merged rather than four
// separate `return LTFALSE` statements: the success path is nested inside
// them and the failure return is the function's last statement.
//
// Two things read off the bytes rather than guessed. The node index is read
// out of the socket twice — once for the bounds check and once for the
// evaluation — where a local would have been kept in a register. And the
// two cdecl calls' cleanups batch into one `add esp,0x14`, which is what
// says both prototypes are fixed rather than varargs.
//
// IDENTICAL on both images (server 0x10017c17, 149 bytes; lithtech
// 0x004478c0, 183).  The translation goes in through LTMatrix::SetTranslation
// (ltmatrix.h), whose LTVector parameter is BY VALUE: that is the 12-byte
// movsl copy at 0x10017c6a.  Spelled out as a vPos local and three
// component stores, the same bytes came out 155 long -- the table pointer
// at +0x74 was loaded first and became the add's accumulator (`mov
// 0x74(%ecx),%ecx; ... add %eax,%ecx`) where shipped scales the index and
// folds the table in (`shl $0x6,%eax; add 0x74(%ecx),%eax`), and the extra
// bytes widened the entry `je`.  The earlier verdict that this fold was not
// reachable from source was wrong; the inline call at the join is what
// reorders it (every spelling of the MatMul argument, cl 8168/8966/8804 and
// a GetNodeTransform accessor had been probed without it).
LTBOOL Model::EvalSocket(ModelWalker* pWalker, HMODELSOCKET hSocket, LTMatrix* pOut)
{
	LTMatrix     mSocket;
	ModelSocket* pSocket;

	if (pOut && hSocket < m_Sockets.m_nElements)
	{
		pSocket = m_Sockets.m_pArray[hSocket];

		if (pSocket->m_iNode < m_NodeTransforms.m_nElements)
		{
			pWalker->m_Unknown_e8 = 0;

			if (pWalker->Evaluate(pSocket->m_iNode))
			{
				quat_ConvertToMatrix(&pSocket->m_rRot, &mSocket);

				mSocket.SetTranslation(pSocket->m_vPos);

				MatMul(pOut, &m_NodeTransforms.m_pArray[pSocket->m_iNode], &mSocket);
				return LTTRUE;
			}
		}
	}

	return LTFALSE;
}


// 0x100136c2 — the animation index a name stands for on this object's model.
// Anything that is not a model has no animations, and a name the model does
// not know is the same answer as no model at all (name ours).
HMODELANIM ci_GetAnimIndex(LTObject* pObj, char* pName)
{
	Model* pModel;
	uint32 nIndex;

	if (pObj && pObj->m_ObjectType == OT_MODEL)
	{
		pModel = ((ModelInstance*)pObj)->m_AnimTracker.m_TimeRef.m_pModel;

		if (pModel->FindAnim(pName, &nIndex, 0))
			return nIndex;
	}

	return (HMODELANIM)-1;
}


// The interface measures the step in seconds and the tracker counts
// milliseconds, so this is where the units change. A failure to find the
// tracker is handed straight back — there is nothing to advance.
LTRESULT CommonModelLT::UpdateMainTracker(HOBJECT hObj, float fUpdateDelta)
{
	LTAnimTracker* pTracker;
	LTRESULT       dResult = GetMainTracker(hObj, pTracker);

	if (dResult)
		return dResult;

	if (!std::isfinite(fUpdateDelta) || fUpdateDelta < 0) return LT_INVALIDPARAMS;
    at_UpdateTracker(pTracker, static_cast<uint32>(std::min<double>(UINT32_MAX,double(fUpdateDelta)*1000)));

	return LT_OK;
}


// ======================================================================== //
//  PARKED: the server-world module (0x10007dfa .. 0x1000a000)
//
//  These functions are not model code. They belong with the rest of
//  CServerWorld — Term (0x10009be5), TermObjects (0x10009d1e), LoadObjects
//  (0x10009f82) and AttachWorldModel (0x10009e31) — in a serverworld TU that
//  does not exist yet, and they sit here only because that TU has to be added
//  to build/engine.sh's server list before it can be compiled. Move the whole
//  block when it is.
//
//  servermgr.h gives CServerWorld eight bytes at CServerMgr+0x1a4 and
//  servercommon.cpp's FindPoly already reaches past that through a layout
//  cast. The constructor below settles the question: it builds nine
//  sub-objects running from +0x04 to at least +0x19c, so the class really is
//  ~0x1b0 bytes and everything servermgr.h names as CServerMgr's own between
//  0x1a8 and ~0x354 is a member of this class. Until servermgr.h is re-laid
//  out, the wide view lives here and the two functions that need it cast.
// ======================================================================== //

// The nine sub-objects the constructor builds, in the order it builds them,
// which is member declaration order. Each is named for its offset and sized
// by where the next one starts — nothing proves an extent, only a start. The
// constructor addresses are the evidence and are recorded beside each one
// (names ours).
//
// The first five are stdlith's own CMoArray<T, DefaultCache> (collection.h),
// and lithtech.exe 0x004222f0 is what says so. /O2 spends its inline budget
// on this constructor and the shipped bytes show every CMoArray piece where
// VC6 left it: each array's vptr, then Clear()'s four zeroes or a call to the
// folded out-of-line Clear (0x004283a0), then Init(0, 0) as a call -- except
// at +0x18, where Init itself is expanded down to Term's SetSize2
// (0x00414fa0), _InitArray (0x00433570) and SetSize's SetSize2. Declared-only
// stand-in classes could not produce any of that (101 bytes against 260);
// the real template does, identical on the first build, with no source
// steering. server.dll's /O1 still calls the five constructors (0x1000b7e6
// ...) and its teardown (0x1002c9e3) still expands `~CMoArray() { Term(); }`
// as a vptr restore and one SetSize2 call per array.
//
// The element types come off the resizes. +0x04's BaseNew (0x00413190)
// strides 0x60 bytes and runs the constructor at 0x00422240 over each slot
// (collection.cpp's CCollElem60C has that body): worldmodels.cpp's CLARecord,
// the light-animation record. +0x18 is four bytes (the CLALight pair of
// words), +0x2c four (a pointer: CLAFrameEntry*), +0x40 one byte (no scale
// in its allocation), +0x54 eight (lea 0(,%eax,8): CLAFrameEntry). The
// classes below carry only size and constructor profile (names ours; the
// real ones live in worldmodels.cpp, another TU).
int w_ReadWorldHeader(class ILTStream* pStream, uint32* pnVersion, uint32* pn2,
                      uint32* pn3)
{
	uint32 nUnused;

	pStream->Read(pnVersion, 4);

	if (*pnVersion != 66)
		return 0;

	pStream->Read(pn2, 4);
	pStream->Read(pn3, 4);

	pStream->Read(&nUnused, 4);
	pStream->Read(&nUnused, 4);
	pStream->Read(&nUnused, 4);
	pStream->Read(&nUnused, 4);
	pStream->Read(&nUnused, 4);
	pStream->Read(&nUnused, 4);
	pStream->Read(&nUnused, 4);
	pStream->Read(&nUnused, 4);

	return 1;
}

#ifndef LT_SERVER_BUILD
// lithtech.exe 0x00421be0 (138 bytes) -- lithtech-only: server.dll goes
// straight from w_ReadWorldHeader to w_TermWorldData, and the only caller is
// the client's ci_GetWorldInfoString (clientworldinfo.cpp, which declares
// it). Reads the header, then the length-prefixed world info string in
// place: truncated to maxLen-1 and NUL-terminated, an empty buffer when
// maxLen is 0, and the stored length reported through pActualLen when that
// is non-null. LT_INVALIDWORLDFILE (0x2a) when the header will not read.
LTRESULT sub_421be0(ILTStream* pStream, char* pInfoString, uint32 maxLen,
                    uint32* pActualLen)
{
	uint32 nHeader3, nHeader2, nVersion;
	uint32 nLen;

	if (!w_ReadWorldHeader(pStream, &nVersion, &nHeader2, &nHeader3))
		return LT_INVALIDWORLDFILE;

	pStream->Read(&nLen, 4);

	if (maxLen == 0)
	{
		pInfoString[0] = 0;
	}
	else
	{
		if (nLen + 1 > maxLen)
			nLen = maxLen - 1;

		pStream->Read(pInfoString, nLen);
		pInfoString[nLen] = 0;
	}

	if (pActualLen)
		*pActualLen = nLen;

	return LT_OK;
}
#endif

// 0x1000d040 — three four-byte reads, one per component, not one twelve-byte
// read: `Read(pVec, 4)`, `Read(&pVec->y, 4)` (a `lea 0x4`) and
// `Read(&pVec->z, 4)` (an `add $0x8` on the same register, VC6 reusing the
// pointer for the last use). Thirteen call sites across the image, which is
// why it is out of line at all (name ours).
// MARKED INLINE, and the oracle is a byte match across both images: this symbol
// stands alone in server.dll and matches nowhere in lithtech.exe, so the
// client build expanded it at every call site. /Os declines the expansion,
// which is why the server copy survives -- and VC6 emits the COMDAT anyway
// when it declines, so nothing is lost there. w_ReadRotation is NOT marked:
// it is still out of line in both images.
inline void w_ReadVector(class ILTStream* pStream, LTVector* pVec)
{
	pStream->Read(&pVec->x, 4);
	pStream->Read(&pVec->y, 4);
	pStream->Read(&pVec->z, 4);
}

// 0x10026394 — and the same for a rotation: four four-byte reads, one per
// component. The last one is `add $0xc` on the running pointer rather than a
// fourth `lea`, exactly as w_ReadVector's third read is — VC6 reuses the
// pointer register once the address is not needed again (name ours, to match
// its neighbour).
void w_ReadRotation(class ILTStream* pStream, LTRotation* pRot)
{
	pStream->Read(&pRot->m_Quat[0], 4);
	pStream->Read(&pRot->m_Quat[1], 4);
	pStream->Read(&pRot->m_Quat[2], 4);
	pStream->Read(&pRot->m_Quat[3], 4);
}
// 0x10026261 — the world's ambient light, out of its own property text. The
// world name is handed to the console's command tokenizer (servermgr.h's
// CommandParser, whose 0x1508 bytes are this function's whole frame — hence
// the `__chkstk`), which is walked until an "AmbientLight" command with at
// least four arguments turns up; its three arguments are then `atof`-ed into
// the colour and clamped. A world with no such command keeps the zeroes and
// answers 0; Load does not look at the answer.
//
// Read off the bytes rather than guessed:
//
//   * the clamp is the SDK's VEC_CLAMP over 0 and 255 — one `fstps` per
//     component shared by all three arms of one conditional expression, and
//     the upper bound hoisted into `%st` before the branch (which is what
//     reverses the second compare into `fcoms` against memory).
//   * the third component's value is kept in the dead `pWorldName`
//     parameter slot and clamped from there, where the first two are
//     re-read out of the caller's vector. It is a compiler temporary, not a
//     third variable — and WHICH dead parameter slot it lands in is decided
//     by source statement order: with `pColor->Init()` written first the
//     temp takes pColor's slot (0xc) and the function is 239 of 239 bytes
//     with four displacement bytes wrong; with the parser's `m_pPos` store
//     first it takes pWorldName's slot (0x8) and the function is exact.
//     VC6 loads both parameters in the same order either way, so it is the
//     order the source *consumes* them that picks the slot, not the order
//     the prologue reads them.
//   * `atof` returns a double and every store is an `fstps`, so the source
//     narrows to float at each assignment.
int w_GetAmbientLight(char* pWorldName, LTVector* pColor)
{
	CommandParser parser;

	parser.m_pPos = pWorldName;

	pColor->Init();

	if (parser.Find("AmbientLight", 0, 4))
	{
		pColor->x = (float)atof(parser.m_ArgPointers[1]);
		pColor->y = (float)atof(parser.m_ArgPointers[2]);
		pColor->z = (float)atof(parser.m_ArgPointers[3]);

		VEC_CLAMP(*pColor, 0.0f, 255.0f);

		return 1;
	}

	return 0;
}
WorldPortal* w_FindPortalInWorld(CWorldGeom* pBsp, char* pName, uint32* pnPortal)
{
	uint32 i;

	for (i = 0; i < pBsp->m_nChunks; i++)
	{
		if (strcmp(pBsp->m_pChunks[i].m_pName, pName) == 0)
		{
			if (pnPortal)
				*pnPortal = i;

			return (WorldPortal*)&pBsp->m_pChunks[i];
		}
	}

	return NULL;
}

// 0x10007dfa — the same lookup across every loaded world, handing back which
// world it was found in. Both out-parameters are optional and are written
// only on a hit; the world index is checked for null here and the portal
// index inside the inner search, so the two guards live in different
// functions.
WorldPortal* w_FindPortal(CServerWorld* pWorld, char* pName,
                          uint32* pnWorld, uint32* pnPortal)
{
	CServerWorld* pThis = (CServerWorld*)pWorld;
	uint32            i;

	for (i = 0; i < pThis->m_Worlds.m_nElements; i++)
	{
		WorldPortal* pPortal = w_FindPortalInWorld(pThis->m_Worlds.m_pArray[i]->m_pWorldBsp,
		                                           pName, pnPortal);

		if (pPortal)
		{
			if (pnWorld)
				*pnWorld = i;

			return pPortal;
		}
	}

	return NULL;
}


// 0x10016478 / 0x004453f0 — one element of Model::m_Unknown054, zeroed.
// The floats go from the top and the dword at +0x10 LAST: that is
// lithtech.exe's order (0x004453f0, one zero register, +0xc +8 +4 +0 then
// +0x10), and server.dll's /O1 hoists the dword's `andl $0x0` to the front
// whichever way the statements are written, so only this order gives both.
CModelVertRec::CModelVertRec()
{
	m_f[3] = 0.0f;
	m_f[2] = 0.0f;
	m_f[1] = 0.0f;
	m_f[0] = 0.0f;
	m_n10 = 0;
}


// 0x100164bb — Model's twelfth sub-object built. The collection at offset
// zero runs first (the compiler's), then six stores: the name onto the shared
// empty string, four fields cleared, and the last one set to 1. The two
// fields Model's constructor then overwrites with its own `this` are among
// the cleared four, which is what says they are the back-pointers.
Model::CModelSub1D4::CModelSub1D4()
{
	m_pName     = "";
	m_Unknown18 = 0;
	m_pOwner28  = 0;
	m_pOwner24  = 0;
	m_Unknown14 = 0;
	m_Unknown2c = 1;
}


// 0x100164e3 and 0x10016501 — Model's twelfth sub-object. The destructor is
// one body call and then the collection at offset zero, destroyed by the
// compiler; the body puts the record back the way its constructor left it and
// gives the collection's memory back to the model's own block rather than to
// g_DefAlloc, which is what says +0x24 is the owning model (Model's
// constructor stores `this` there) and +0x174 of it the allocator.
Model::CModelSub1D4::~CModelSub1D4()
{
	sub_10016501();
}

// 0x1001bf0d — this child-model record's half of the model file: one dword,
// then one position and one rotation per node of the owning model. The count
// is the MODEL's node-transform count, not anything in the file, and the
// record's own owner pointer is what supplies both it and the allocator — so
// a record that has not been given an owner cannot be read at all and shares
// the failure return with a refused allocation.
//
// The array pointer is re-loaded for each of the two reads, which is two
// separate subscript expressions in the source rather than one cached
// element pointer.
LTBOOL Model::CModelSub1D4::sub_1001bf0d(ILTStream* pStream)
{
	uint32 i;

	if (!m_pOwner24)
		return LTFALSE;

	pStream->Read(&m_Unknown20, sizeof(m_Unknown20));

	if (!m_Array00.SetSize2(m_pOwner24->m_NodeTransforms.GetSize(),
	                        m_pOwner24->GetAlloc()))
		return LTFALSE;

	for (i = 0; i < m_pOwner24->m_NodeTransforms.m_nElements; i++)
	{
		w_ReadVector(pStream, (LTVector*)&m_Array00.m_pArray[i].m_Pos);
		w_ReadRotation(pStream, (LTRotation*)&m_Array00.m_pArray[i].m_Rot);
	}

	return LTTRUE;
}


void Model::CModelSub1D4::sub_10016501()
{
	Model* pModel = (Model*)m_pOwner24;

	m_pOwner28 = 0;
	m_pName    = "";

	m_Array00.SetSize2(0, pModel ? pModel->GetAlloc() : &g_DefAlloc);
}


// 0x10016922 — one animation-table entry: no animation, no child record,
// the default dimensions 128 x 16 x 16 (128.0f stored as the integer
// immediate 0x43000000, the one 16.0f loaded once and stored twice) and no
// binding offset. The two Init() calls are load-bearing: three component
// stores per vector give the same fourteen instructions with the integer
// stores scheduled one FPU gap later.
AnimInfo::AnimInfo()
{
	m_pAnim = LTNULL;
	m_pChildModel = LTNULL;
	m_vDims.Init(128.0f, 16.0f, 16.0f);
	m_vAnimBinding.Init(0.0f, 0.0f, 0.0f);
}


// 0x1001694e — Model's last sub-object built: the collection at +0x18 first
// (the compiler's), then this object's own vtable, then the reset, then the
// model back-pointer cleared. The vtable store between the member's
// constructor and the body is the ordinary shape of a polymorphic class's
// constructor and is what pairs this with the destructor below.
ModelNode::ModelNode()
{
	sub_10016a00();

	m_pModel = LTNULL;
}


// 0x10016990 — the same record built against a model, and the constructor the
// factory below runs on freshly allocated memory.
ModelNode::ModelNode(Model* pModel)
{
	sub_10016a00();

	m_pModel = pModel;
}


// 0x10016971 — slot 0x04: `ret $0xc`, three dwords of arguments and nothing
// done with them. The argument count is the only thing the bytes say.
void ModelNode::_v04(uint32 a, uint32 b, uint32 c)
{
}


// 0x10016a37 — slot 0x08: one of these on the heap. The literal 0xb4 the
// allocator is asked for is this class's own size, which is where the extent
// in the header comes from, and the null arm is `new`'s own.
ModelNode* ModelNode::Create(Model* pModel)
{
	return new ModelNode(pModel);
}


// 0x10016a9b — how many nodes hang off this one, itself included. Forty
// bytes of recursion: the count starts at one (`push $1; pop %ebx`, /O1's
// three-byte small constant) and every child is asked the same question. It
// is the function that names the collection at +0x18 as the child list, and
// the reason the model can size its node tables before it has walked the
// tree.
uint32 ModelNode::sub_10016a9b()
{
	uint32 nNodes = 1;
	uint32 i;

	for (i = 0; i < m_Array18.m_nElements; i++)
		nNodes += m_Array18.m_pArray[i]->sub_10016a9b();

	return nNodes;
}


// 0x10016a58 — slot 0x0c, and the one Model's constructor calls: take the
// model, re-home the name on it, and pass the same model down to every child
// record. The walk is what identifies the collection's element type — it
// calls slot 0x0c, which is this function.
void ModelNode::SetModel(Model* pModel)
{
	uint32 i;

	m_pModel = pModel;
	m_pName  = pModel->sub_100173fa(m_pName);

	for (i = 0; i < m_Array18.m_nElements; i++)
		m_Array18.m_pArray[i]->SetModel(pModel);
}


// 0x100169dd — the release. It is a member and not the destructor's body
// because Model's own release (0x10017016) calls it too: the collection at
// +0x18 goes back to the owning model's data block rather than to g_DefAlloc,
// and then the record is reset to the state the constructor left it in.
void ModelNode::sub_100169dd()
{
	FreeArrayElements(&m_Array18, m_pModel ? m_pModel->GetAlloc() : &g_DefAlloc);

	sub_10016a00();
}


// 0x10016a00 — the reset, shared by the constructor and the release. Three
// floats, a word and a byte cleared, the name back on the shared empty
// string, and the two 4x4s set to identity.
//
// The three floats are one LTVector cleared with Init(), written AFTER the
// name store. Three separate `= 0.0f` stores left one adjacent pair swapped
// on each image: /O1 (0x10016a0e) emits `movl $"",0xb0(%esi)` before the
// `lea 0x2c(%esi)` of the first Identity call and ours had it after, and /O2
// (0x445dd2 in lithtech.exe) puts the name store ahead of the 1.0f
// materialisation where ours had it behind. Init() in this position closes
// both; in other positions it was worse. The header still spells the three
// as separate floats, hence the cast — an LTVector member at +0x04 is what
// the source almost certainly had.
void ModelNode::sub_10016a00()
{
	m_n10 = 0;
	m_b12 = 0;

	m_pName = "";

	m_f04 = m_f08 = m_f0c = 0;
    m_iParent = 0xffffffff;

	m_Mat2c.Identity();
	m_Mat6c.Identity();
}


// 0x100169b8 — Model's last sub-object taken apart: its own vtable restored,
// one body call, and then the collection at +0x18 destroyed by the compiler.
// The vtable it restores is the one the constructor stores, which is what
// pairs the two.
ModelNode::~ModelNode()
{
	sub_100169dd();
}


// 0x100243f6 and 0x10024409 — Model's first sub-object, and the only two
// functions of its own it has. The constructor is defined out of line
// deliberately: /O1 implies /Ob1, so a body written inside the class would be
// expanded into Model's constructor and the shipped out-of-line call would
// have nowhere to come from.
//
// The setter zeroes the first dword and takes the second by argument, and
// answers 1 unconditionally -- `push $1; pop %eax`, which is a byte shorter
// than moving the constant.
// One row of the seven-entry name/float* table sub_100176dc builds on its
// own stack (name ours).  It has to be a file-scope type: the two pointer
// members and the eight-byte stride are read straight off the frame, and a
// type declared inside the function body would be unusable from anywhere
// else in commonmodel.cpp.
struct ModelPropQB2
{
	char*  m_pName;
	float* m_pValue;
};


// ======================================================================= //
//  server.dll 0x100176dc (1025 bytes, thiscall, no arguments, one `ret`)
//  lithtech.exe 0x004471b0 (1195 bytes)
//
//  APPLY THE MODEL FILE'S COMMAND STRING TO THE MODEL.
//
//  Declared commonmodel.h:691 and called once, from LoadChildModels
//  (commonmodel.cpp:2398, shipped inside 0x1001c604) right after
//  sub_100171f4(0) and right before sub_10017517().  modelnew.cpp:81-91
//  already carries the client twin's address, 0x004471b0; its note that
//  "ltnew2.cpp has it" is STALE -- `nm` on both Release/commonmodel.obj
//  copies reports `?sub_100176dc@Model@@QAEXXZ` as an undefined external.
//
//  ---- THE FRAME DECOMPOSES EXACTLY, IN BOTH IMAGES --------------------
//
//  `mov $0x1788,%eax ; call 0x1003e740` (__chkstk) -- 6,024 bytes:
//
//      -0x1788 .. -0x0280   0x1508  CommandParser  (servermgr.h:1577 gives
//                                   0x1508 exactly; m_nArgs is read at
//                                   -0x1688 = base+0x100 and m_pPos written
//                                   at -0x1684 = base+0x104)
//      -0x0280 .. -0x0038   0x0248  ModelWalker    (commonmodel.h:1121 gives
//                                   0x248; its +0xe0 is written at -0x1a0)
//      -0x0038 .. -0x0000   0x0038  ModelPropQB2 props[7]
//
//  0x1508 + 0x248 + 0x38 = 0x1788.  Nothing left over, nothing overlapping.
//  The client's `sub $0x1788` frame puts the same three at 0x290, 0x48 and
//  0x10 of its esp frame -- the same order read the other way, which is the
//  two-image agreement to ask for.
//
//  RECONNAISSANCE CORRECTION: the table is SEVEN entries, not eight.  The
//  names are stored at -0x38, -0x30, -0x28, -0x20, -0x18, -0x10, -0x08 and
//  the pointers at -0x34 .. -0x04 (0x10017704..0x1001776f), and the loop
//  that walks them is `push $0x7 ; pop %edi` (0x100177ad).  The client is
//  explicit: `mov $0x7,%edi` at 0x004472eb.
//
//  ---- WHY parser.m_pPos = LTNULL COMES FIRST --------------------------
//
//  0x100176e9 `andl $0x0,-0x1684(%ebp)` is emitted BEFORE the walker's
//  constructor call at 0x100176f9, and the client does the same
//  (0x004471c8 before the inlined constructor at 0x004471cf).  The parser
//  has no constructor and the walker does, so declaration order is the only
//  thing that can put a plain store ahead of a constructor -- the exact
//  argument src/engine/lithtech/cmodelload.cpp:998 already makes for
//  cm_RegisterModel, where declaring the constructed object first costs
//  that function its match.  `parser.m_pPos = LTNULL;` is also the idiom
//  smgr.cpp:2752 uses.  (`andl $0x0` rather than `movl $0x0` is /Os picking
//  the 7-byte encoding over the 10-byte one; same for the `orl $0xffffffff`
//  pair below.)
//
//  ---- THE DEAD ARM IS IN THE SHIPPED BYTES ----------------------------
//
//  The `>= 4` arm is unreachable.  0x10017953 `cmp $0x3,%eax ; jl
//  0x10017a74` sends only nArgs < 3 to 0x10017a74, and 0x10017a74 is
//  `cmp $0x4,%eax ; jl <end>`.  Nothing else branches to 0x10017a74 (a full
//  sweep of the function's displacements finds one).  The client has the
//  same pair at 0x004474cb and 0x004475ec.  So "ShadowCenterOffset" never
//  fires in the shipped game: the else-if chain was written `>= 3` before
//  `>= 4`.  "VC6 keeps both branches of an unsatisfiable guard"
//  is why the arm is still emitted.  Written below exactly as shipped --
//  swapping the two tests would change the bytes.
// ======================================================================= //

void Model::sub_100176dc()
{
	CommandParser parser;

	parser.m_pPos = LTNULL;                  // 0x100176e9

	ModelWalker walker;                   // 0x100176f9

	// 0x10017704 .. 0x1001776f.  The seven `lea` of `this + N` and the
	// seven string immediates are interleaved by the scheduler in the
	// shipped code; the initialiser list is what emits both runs.
	ModelPropQB2 props[7] =
	{
		{ "VisRadius",           &m_f0a8 },  // 0x10052fcc / 0x0a8
		{ "AmbientLight",        &m_f0cc },  // 0x10052fbc / 0x0cc
		{ "DirLight",            &m_f0d0 },  // 0x100519c0 / 0x0d0
		{ "ShadowProjectLength", &m_f0d8 },  // 0x10052fa8 / 0x0d8
		{ "ShadowLightDist",     &m_f0dc },  // 0x10052f98 / 0x0dc
		{ "ShadowSizeX",         &m_f0e0 },  // 0x10052f8c / 0x0e0
		{ "ShadowSizeY",         &m_f0e4 },  // 0x10052f80 / 0x0e4
	};

	uint32 i;

	// 0x10017744 / 0x10017776 -- the only early exit.
	if (!m_pCommandString)
		return;

	m_Unknown140 = 0;                        // 0x1001777c
	parser.m_pPos = m_pCommandString;        // 0x10017789

	// 0x1001778f is the entry test and 0x10017acb the back edge, so this is
	// a plain rotated `while`.  %esi and %edi are pushed at 0x1001779c,
	// AFTER the entry test -- VC6 sinking the saves past the early exit,
	// which happens on its own.
	while (parser.Next())
	{
		// 0x1001779e -- m_nArgs is SIGNED: 0x10017953 and 0x10017a77 are
		// `jl`, not `jb`.
		if (parser.m_nArgs == 2)             // 0x100177a4
		{
			// -- 0x100177ad .. 0x100177df ------------------------------ //
			//
			// No `break` and no `else`: `dec %edi ; jne` runs all seven
			// comparisons whatever matched, and the FadeRangeMin test
			// below is reached by falling out of the loop.  Strength-
			// reduced to %esi walking props[i].m_pValue by eight with the
			// name read as `-0x4(%esi)`.
			//
			// 0x10047080 is stricmp (already named by dsi.cpp:266 and
			// console.cpp:556); 0x1003e51a is atof.
			//
			// NEAR RISK, and it is /O1-only: the shipped store here is
			// `fld %st(0) ; fstps (%eax) ; fstp %st(0)` (0x100177d5),
			// where the client emits the bare `fstps (%ecx)` at
			// 0x0042731a and every other atof store in THIS function is
			// also bare.  Nothing in the source distinguishes them, so
			// this is 4 bytes of /Os scheduling to watch for, not a
			// different statement.
			for (i = 0; i < 7; i++)
			{
				if (nolf::ascii_compare(props[i].m_pName, parser.m_ArgPointers[0]) == 0)
					*props[i].m_pValue = (float)atof(parser.m_ArgPointers[1]);
			}

			// -- 0x100177e1 -------------------------------------------- //
			//
			// sub_10017add(f1, f2) writes f1 to +0xb0 and f2 to +0xb8 and
			// caches each square (0x10017ae9 / 0x10017af2), so the pair is
			// (min, max) and each arm passes the other field through.  The
			// pushes are right-to-left: 0x100177fd pushes m_f0b8 first,
			// making it the SECOND parameter.
			//
			// /O1 tail-merges the two arms at 0x10017839 (the shared
			// `fstps (%esp) ; mov %ebx,%ecx ; call`); /O2 keeps both
			// copies (0x00447354 and 0x00447390).  That is the /O1-vs-/O2
			// signature and it needs no source support.
			if (nolf::ascii_compare("FadeRangeMin", parser.m_ArgPointers[0]) == 0)
			{
				sub_10017add((float)atof(parser.m_ArgPointers[1]), m_f0b8);
			}
			else if (nolf::ascii_compare("FadeRangeMax", parser.m_ArgPointers[0]) == 0)
			{
				sub_10017add(m_f0b0, (float)atof(parser.m_ArgPointers[1]));
			}
			// -- 0x10017848 -------------------------------------------- //
			//
			// 0x1004dc80 is 0x3c8efa35 = 0.01745329f, which is
			// ltbasedefs.h:1238's MATH_DEGREES_TO_RADIANS constant to the
			// bit -- and it is a FOUR-byte load (`fmuls`), so the literal
			// is the macro's `0.01745329251994f` and not a double.  That
			// needs the INNER (float) cast: atof's double beside the float
			// literal widens it to an 8-byte `fmull`.
			//
			// Every stricmp in this function takes the literal (or the
			// table name) FIRST: shipped pushes parser.m_ArgPointers[0]
			// before the immediate (0x100177e1, 0x1001780e, 0x10017848)
			// and before -0x4(%esi) in the table loop (0x100177b3).
			else if (nolf::ascii_compare("FovXOffset", parser.m_ArgPointers[0]) == 0)
			{
				m_f144 = (float)MATH_DEGREES_TO_RADIANS(
				             (float)atof(parser.m_ArgPointers[1]));
				m_Unknown140 = 1;            // 0x10017876
			}
			// -- 0x10017885; its flag store tail-merges into the one above
			//    at 0x100178b7 `jmp 0x10017876`.
			else if (nolf::ascii_compare("FovYOffset", parser.m_ArgPointers[0]) == 0)
			{
				m_f148 = (float)MATH_DEGREES_TO_RADIANS(
				             (float)atof(parser.m_ArgPointers[1]));
				m_Unknown140 = 1;
			}
		}
		// -- 0x100178b9 ------------------------------------------------ //
		//
		// %esi is loaded with 1 once (`push $0x1 ; pop %esi`) and three of
		// the four stores use it; the fourth spends the 10-byte immediate
		// (0x10017941).  The CLIENT does exactly the same (0x00447426 and
		// 0x004474bc), so it is the allocator's live range ending and not
		// a source difference.
		else if (parser.m_nArgs == 1)
		{
			if (nolf::ascii_compare("NoAnimation", parser.m_ArgPointers[0]) == 0)
			{
				m_Unknown0ac = 1;            // 0x100178da
			}
			else if (nolf::ascii_compare("ShadowEnable", parser.m_ArgPointers[0]) == 0)
			{
				m_Unknown0d4 = 1;            // 0x100178fb
			}
			else if (nolf::ascii_compare("SpecularEnable", parser.m_ArgPointers[0]) == 0)
			{
				m_Unknown14c = 1;            // 0x1001791c
			}
			else if (nolf::ascii_compare("Rigid", parser.m_ArgPointers[0]) == 0)
			{
				m_Unknown150 = 1;            // 0x10017941
			}
		}
		// -- 0x10017950 ------------------------------------------------ //
		else if (parser.m_nArgs >= 3)
		{
			// 0x10017959.  Two atof calls whose cdecl cleanups batch into
			// two `pop %ecx` at 0x10017991 -- /Os spelling `add $0x8,%esp`.
			if (nolf::ascii_compare("FadeSpriteSize", parser.m_ArgPointers[0]) == 0)
			{
				m_f0c4 = (float)atof(parser.m_ArgPointers[1]);
				m_f0c8 = (float)atof(parser.m_ArgPointers[2]);
			}
			// -- 0x10017998 -------------------------------------------- //
			//
			// The one arm with structure.  `and %eax,0x13c(%ebx)` at
			// 0x100179b2 is `m_Unknown13c = 0` with the zero the stricmp
			// test just proved in %eax (6 bytes against 7 for `andl $0x0`),
			// and the two `orl $0xffffffff` are `= -1` in 7 bytes.
			//
			// Each step guards the next and every failure falls to the
			// loop bottom (0x100179d5, 0x100179fa, 0x10017a51 all `je
			// 0x10017ac5`), which is nested `if`s and not an && chain: an
			// && chain would have to evaluate FindAnim's arguments before
			// m_n0f8 = -1 was stored.
			else if (nolf::ascii_compare("NormalRef", parser.m_ArgPointers[0]) == 0)
			{
				m_Unknown13c = 0;
				m_n0f4 = -1;

				if (FindNode(parser.m_ArgPointers[1], (HMODELNODE*)&m_n0f4))
				{
					m_n0f8 = -1;

					// 0x100179e8 pushes 0 for the third argument, so the
					// AnimInfo** out-parameter is not wanted here.
					if (FindAnim(parser.m_ArgPointers[2],
					             (uint32*)&m_n0f8, LTNULL))
					{
						// 0x10017a00..0x10017a39 -- eight stores in
						// AnimTimeRef::Init's own order (m_pModel, then
						// Prev's three, then Cur's three, then m_Percent),
						// with both frame locators given the SAME anim
						// index and frame 0.  The index is re-read from
						// m_n0f8 (`mov (%esi),%eax`) because FindAnim
						// wrote it through the pointer.
						walker.m_TimeRefs[0].Init(this, m_n0f8, 0,
						                          m_n0f8, 0, 0.0f);

						// 0x10017a40.  (The last two Init arguments could
						// be left to their defaults; the expansion is
						// identical either way.)
						walker.m_Unknown_e0 = 1;

						if (walker.sub_100386c6())   // 0x10017a4a
						{
							// 0x10017a53 -- `shl $0x6` is sizeof(LTMatrix)
							// and `rep movsl` with $0x10 is the 64-byte
							// struct assignment.  m_NodeTransforms.m_pArray
							// is the +0x074 read.
							m_mNormalRef0fc =
								m_NodeTransforms.m_pArray[m_n0f4];

							m_Unknown13c = 1;        // 0x10017a68
						}
					}
				}
			}
		}
		// -- 0x10017a74 -- UNREACHABLE; see the header note.  Kept because
		//    the shipped bytes keep it.
		else if (parser.m_nArgs >= 4)
		{
			// 0x10017a79.  Three atof calls, one cleanup (`add $0xc,%esp`
			// at 0x10017ac2), components in x, y, z order.
			if (nolf::ascii_compare("ShadowCenterOffset", parser.m_ArgPointers[0]) == 0)
			{
				m_Vec0e8.x = (float)atof(parser.m_ArgPointers[1]);
				m_Vec0e8.y = (float)atof(parser.m_ArgPointers[2]);
				m_Vec0e8.z = (float)atof(parser.m_ArgPointers[3]);
			}
		}
	}
}

// 0x10017b69 (33 bytes) — does a child model's node tree have this model's
// shape?  One call to mod_SameNodeTree (0x10016304) with the two root
// records at +0x2c0, this model's first, and the caller's out-slot passed on
// as the mismatch pointer.  LoadChildModels (the only caller) stores the
// answer in the child record's +0x2c and hands it &nOOBB2, a dword, which is
// why the header's prototype carries the slot as uint32* (name ours).
// Both roots go through named locals: passed as bare member reads they fold
// into `push 0x2c0(%reg)` (31 bytes), where shipped loads each into a
// register first (0x10017b71, 0x10017b77).  IDENTICAL on both images.
uint32 Model::sub_10017b69(Model* pModel, ModelNode** pOut)
{
	ModelNode* pMine   = m_pSub20c;
	ModelNode* pTheirs = pModel->m_pSub20c;

	return mod_SameNodeTree(pMine, pTheirs, pOut);
}
