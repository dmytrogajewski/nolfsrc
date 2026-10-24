#include "servermgr.h"
#include "servermsg.h"
#include "de_error.h"
#include "objtypes.h"

// *********************************************************************** //
//  0x10031dfa — sm_ClaimObjectID. Take one slot out of the object-id map's
//  free list and move it to the in-use list, then wipe every client's
//  pending-change entry for that slot so nobody hears about the previous
//  tenant.
//
//  0xffff means "any": the free list is walked for the first record whose
//  0x40000000 bit is clear. A real id instead grows the map until the id is
//  in range and then looks the record up. Either way the map can refuse,
//  and the answer is nonzero.
//
//  Read off the bytes:
//
//    * the walk writes *ppRec on EVERY iteration, not only on the hit — the
//      store is inside the loop body ahead of the test — so a full free list
//      leaves the caller's pointer holding the last record examined before
//      the grow overwrites it.
//    * the "any" path falls out of the loop into the grow, and the hit
//      leaves the loop by a jump into the shared tail; the real-id path
//      reaches the same tail from its own success test. That is one label,
//      not two copies of the tail.
//    * the grow loop for a real id is a rotated `while`: the range test is
//      duplicated, once as the entry guard (`jb`) and once as the back edge
//      (`jae`).
//    * `*ppRec` is re-read from memory at every use in the tail — four
//      times, including once per client — rather than cached in a local.
//    * `&pClient->m_ObjInfos[index]` IS a named local: both stores fold the
//      index back into their own addressing and the shipped code is left
//      with a dead `lea (%esi,%ecx,4),%ecx` at the bottom of the loop, which
//      is the pointer-local fingerprint.
//    * the client list's head is a NAMED LOCAL, not the bound written out
//      inline. Both spellings walk the same list, but `&pMgr->m_ClientList
//      .m_Head` written in the loop condition lets VC6 destroy the manager's
//      register in place (`add $0x7e8,%esi`) while `LTLink* pHead =
//      &pMgr->m_ClientList.m_Head;` makes it compute the address into a
//      fresh register with `lea`, which is what the shipped code does and
//      which then re-binds every register in the loop body. Same size, and
//      it was the last seven bytes.
//    * the error site is RETURN_ERROR(1, "sm_AllocateID", 1) with the name
//      as a literal (`push $imm32`, not `push [imm32]`), and the code really
//      is 1: the shipped `.data` carries "1", not "LT_ERROR".
//
//  The name "sm_AllocateID" is the shipped check literal, so it is the
//  original's name for the function; sm_ClaimObjectID is this file's own
//  earlier name for the address and is kept because the two call sites use
//  it.
// *********************************************************************** //

// One record of the object-id map. The link is the record itself, and the
// third dword is the slot index it stands for: the client tables are indexed
// with a full DWORD load of +0x08, which is what says the four bytes are one
// field. Bit 0x40000000 of that dword marks a record that is not available;
// the shipped code tests it as `testb $0x40,0xb(%eax)` and clears it with
// `andb $0xbf,0xb(%eax)`, which is VC6 narrowing a 32-bit mask to the one
// byte it touches — so this spelling and a separate `uint8` flag byte at
// +0x0b are indistinguishable from the bytes alone, and the dword index load
// is the tie-breaker.


#define OBJIDREC_UNAVAILABLE 0x40000000

// 0x100345b6 — make the object-id map bigger, handing back a record through
// the same out-parameter this function uses. Nonzero means it could not.
// The name is shipped (its REPORT_ERROR literal); the body is smgrnew.cpp's.
int sm_CreateNewID(CServerMgr* pMgr, void** ppRec);

// 0x1002ad18 — the record for one id, or null if the map does not have it
// (name ours).
void* sm_FindObjectIDRec(CServerMgr* pMgr, uint16 nID);

// 0x10033253 — a CServerMgr method taking the map's element count, run only
// when the count has passed the 200 Init sets at +0x8b8. servermgr.h does not
// carry it and is not this reconstruction's to change, so the manager is cast
// to reach it (name and view ours).


int sm_ClaimObjectID(CServerMgr* pMgr, void** ppRec, uint16 nID)
{
	if (nID == 0xffff)
	{
		CObjIDRec* pCur;

		for (pCur = (CObjIDRec*)pMgr->m_Link8c4.m_pNext;
		     pCur != (CObjIDRec*)&pMgr->m_Link8c4;
		     pCur = static_cast<CObjIDRec*>(pCur->m_pNext))
		{
			*ppRec = pCur;

			if (!(pCur->m_nIndex & OBJIDREC_UNAVAILABLE))
				goto claimed;
		}

		{
			int dResult = sm_CreateNewID(pMgr, ppRec);

			if (dResult != 0)
				return dResult;
		}
	}
	else
	{
		while (nID >= pMgr->m_Unknown8bc)
		{
			int dResult = sm_CreateNewID(pMgr, ppRec);

			if (dResult != 0)
				return dResult;
		}

		*ppRec = sm_FindObjectIDRec(pMgr, nID);

		if (!*ppRec)
			RETURN_ERROR(1, "sm_AllocateID", 1);
	}

claimed:
    if (pMgr->m_Unknown8bc > pMgr->m_Unknown8b8 && !pMgr->GrowClientObjInfos(pMgr->m_Unknown8bc))
        return LT_OUTOFMEMORY;
    ((CObjIDRec*)*ppRec)->m_nIndex &= ~OBJIDREC_UNAVAILABLE;
    dl_Remove((CheapLTLink*)*ppRec);
    dl_Insert((CheapLTLink*)&pMgr->m_Link8d0, (CheapLTLink*)*ppRec);

	LTLink* pHead = &pMgr->m_ClientList.m_Head;

	for (LTLink* pLink = pHead->m_pNext; pLink != pHead; pLink = pLink->m_pNext)
	{
		Client*   pClient = (Client*)pLink->m_pData;
		CObjInfo* pInfo   = &pClient->m_ObjInfos[((CObjIDRec*)*ppRec)->m_nIndex];

		pInfo->m_ChangeFlags = 0;
		pInfo->m_Unknown02   = 0;
	}

	return 0;
}


// *********************************************************************** //
//  Two more strays from the same unreconstructed neighbour, parked here for
//  the same reason the two sound-manager functions above are: they sit
//  between commonphysics.cpp's sm_MoveObject (0x100355fe) and this file's
//  own block, and neither is sensitive to which translation unit compiles
//  it.
//
//  Both are slots of ONE vtable, at 0x1004e8c8 — twelve entries, installed
//  by a `mov $0x1004e8c8,...` at 0x10031fc0. Reading the table off .rdata
//  is what gives the slot numbers: 0x1003524a, 0x10035260, 0x10035269,
//  0x100352d5, 0x100352f5, 0x1003530b, 0x10035541, 0x100352b4, 0x10035568,
//  0x100355db, 0x100355df, 0x100355ec — so 0x10035541 is slot 6 and
//  0x10035568 is slot 8. (Slot 9, 0x100355db, is a four-byte body
//  /OPT:ICF has folded together with LThread::IsValid, which is why
//  lthread.h already claims that address.)
//
//  The class is the object mover the world simulation drives: slot 5
//  (0x1003530b) walks an object's attachments and re-places each child
//  through wm_MoveObject, and it reaches the server manager at `this+4`.
//  Only the two slots below are implemented here, so the view is abstract
//  and no vtable of ours is emitted (names ours).
// *********************************************************************** //

// 0x10058530 — the float behind the "DebugMaxPos" console variable. The
// registration table at 0x100523b8 pairs the name string with this address,
// which is what names it; zero or less disables the check (name ours).
// Zero-initialised (.bss), so the check starts disabled. Defined here, its
// only reader, until the TU that owns the console-variable table
// (0x10052000-0x10052430, not reconstructed) exists to hold it.
extern float g_CV_DebugMaxPos;

// 0x10035541. `this` is never touched and the second argument is never
// read; the shipped code takes both and cleans twelve bytes off the stack.
//
// Read off the bytes: both failures reach ONE `xor %eax,%eax` at the very
// end and the success is `push $0x1; pop %eax; jmp` over it, which is a
// single `&&` guarding `return 1` with a trailing `return 0`. Two separate
// guarded `return 0`s are one byte shorter and branchless — VC6 collapses
// `if (!x) return 0; return 1;` into `neg/sbb/neg`, so the shipped branch
// pair is itself the evidence for the `&&`. The priority compare has the
// request's dword as the destination operand and the object's byte
// zero-extended into a register, so the request's field is the left-hand
// side in the source and the object's is a uint8.
int ServerObjectMover::ObjectBlocks(WMMoveState* pRequest, LTObject* pUnused,
                               LTObject* pObj)
{
	if (pRequest->m_nUnknown0C > pObj->m_BlockingPriority &&
	    pObj->_v30())
	{
		return 1;
	}

	return 0;
}

// 0x10035568. Complain when something is put a very long way from the
// origin — a debugging aid whose console variable ships disabled.
//
// Read off the bytes:
//
//   * the gate is `<= 0.0f` against a .rdata zero (`fcomps`), not a test of
//     the bit pattern, so a negative value disables it too.
//   * the radius test is the squared magnitude against the square of the
//     variable, with the variable read from memory all three times rather
//     than held in a register — which is what naming no local gives.
//   * the two messages are two separate calls to the variadic printer, and
//     the first one's argument slot is never popped: the second call's
//     first `double` needs eight bytes, VC6 pushes four more and writes
//     across the dead slot, and one `add $0x20,%esp` cleans both calls at
//     the end. Thirty-two bytes for what looks like thirty-six.
//   * the class name is the first field of the class record hanging off
//     SObjData+0x24, reached with three loads and pushed straight through.
void ServerObjectMover::CheckMaxPos(WMMoveState* pRequest, LTVector* pPos)
{
	if (g_CV_DebugMaxPos <= 0.0f)
		return;

	if (pPos->MagSqr() <= g_CV_DebugMaxPos * g_CV_DebugMaxPos)
		return;

	dsi_ServerError("Server: position larger than 'DebugMaxPos'");

	dsi_ServerError("Object class: %s, position (%.2f, %.2f, %.2f)",
		pRequest->m_pObj->m_pSData->m_pClass->m_ClassName,
		pPos->x, pPos->y, pPos->z);
}


// 0x10032332 — slot 12 of the mover's table: three loads and a return. Out of
// line rather than in the class body, because nothing in this translation
// unit calls it and the class is abstract, so an inline definition is never
// emitted at all.
ILTPhysics* ServerObjectMover::Physics()
{
	return m_pMgr->m_pUnknown928 ? m_pMgr->m_pUnknown928->Physics() : nullptr;
}
