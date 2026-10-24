// server.dll — the server's ILTLightAnim (reconstruction). Build /O1.
#include "serverlightanim.h"
#include "servermsg.h"
#include "packet.h"
#include "de_error.h"

// 0x1002def5 — the interface's first method, and the only one that does not
// index the array itself: it hands the name straight to the world and turns
// the world's LTBOOL into an LTRESULT.
//
// The world object at CServerMgr+0x1a4 is POLYMORPHIC. The shipped code does
// `lea 0x1a4(%eax),%ecx` / `mov (%ecx),%eax` / `call *(%eax)`, so the first
// dword of the object is its vptr and this is virtual slot 0 — servermgr.h
// models CServerWorld as eight opaque bytes and is not this reconstruction's
// to change, so the slot is reached through the local view below.
//
// Read off the bytes: there is no error report at all (no dsi_OnReturnError,
// no g_DebugLevel test), the failure code is 0x3d = LT_NOTFOUND, and the
// out-parameter is set to -1 only on failure, with `orl $0xffffffff,(%esi)`
// — /O1's four-byte spelling of a -1 store. The reference parameter is
// loaded into a callee-saved register before the call and used again after
// it, which is what one reference used twice gives.
LTRESULT ServerLightAnimLT::FindLightAnim(const char* pName, HLIGHTANIM& hLightAnim)
{
	if (m_pServerMgr->m_World.FindLightAnim(pName, &hLightAnim))
		return LT_OK;

	hLightAnim = (HLIGHTANIM)-1;

	return LT_NOTFOUND;
}

LTRESULT ServerLightAnimLT::GetNumFrames(HLIGHTANIM hLightAnim, uint32& nFrames)
{
	nFrames = 0;

	if (hLightAnim >= m_pServerMgr->m_nLightAnims)
		RETURN_ERROR_NAMED(2, "ServerLightAnimLT::GetNumFrames", LT_INVALIDPARAMS);

	nFrames = m_pServerMgr->m_pLightAnims[hLightAnim].m_nFrames;
	return LT_OK;
}

LTRESULT ServerLightAnimLT::GetLightAnimInfo(HLIGHTANIM hLightAnim, LAInfo& info)
{
	if (hLightAnim >= m_pServerMgr->m_nLightAnims)
		RETURN_ERROR_NAMED(2, "ServerLightAnimLT::GetLightAnimInfo", LT_INVALIDPARAMS);

	la_GetInfo(&m_pServerMgr->m_pLightAnims[hLightAnim], &info);
	return LT_OK;
}

LTRESULT ServerLightAnimLT::SetLightAnimInfo(HLIGHTANIM hLightAnim, LAInfo& info)
{
	if (hLightAnim >= m_pServerMgr->m_nLightAnims)
		RETURN_ERROR_NAMED(2, "ServerLightAnimLT::SetLightAnimInfo", LT_INVALIDPARAMS);

	// The comparison runs before the write, so the clients are only told about
	// a change that actually happened.
	uint32 dwChangeFlags;
	LightAnim* pAnim = &m_pServerMgr->m_pLightAnims[hLightAnim];
	LTBOOL bChanged = la_CompareInfo(pAnim, &info, &dwChangeFlags);

	la_SetInfo(pAnim, &info);

	if (bChanged)
		sm_LightAnimChanged(m_pServerMgr, hLightAnim, dwChangeFlags);

	return LT_OK;
}


// 0x1002896d — is there already a queued change for this animation on this
// client? The queue is short and unsorted, so it is a linear walk; the index
// is written only on a hit and the answer is a plain int (`xor %eax,%eax` for
// the miss, `push $1; pop %eax` for the hit).
//
// Read off the bytes: the count is loaded once into %ecx and the guard is
// `test %ecx,%ecx; jbe`, which is the rotated `for` — the walk itself is the
// indexed source form strength-reduced to `add $0xc` before the loop and
// `add $0x4` inside it. The compare is sixteen bits on both sides with the
// record's field as the destination operand, and the field is written first
// in the source comparison. The PARAMETER is 32 bits, cast at the compare:
// lithtech's /O2 copy (0x00466610) hoists it as `mov 0xc(%esp),%esi` (a
// uint16 parameter gives `mov 0xc(%esp),%si`, one byte longer), and the
// caller passes its HLIGHTANIM uncast (server.dll identical either way).
// 0x100289a2 — every client that is listening gets told, by having the change
// queued on it. An animation index the world does not have is dropped before
// the walk even starts.
//
// Read off the bytes: the client's queue is searched first, so a second change
// to the same animation ORs into the entry already there rather than adding a
// second one; and the overflow test is `>= 0x40` *before* the append, so a
// full queue is flushed by 0x10026755 and then appended to anyway — the flush
// is what makes room.
void sm_LightAnimChanged(CServerMgr* pServerMgr, HLIGHTANIM hLightAnim,
                         uint32 dwChangeFlags)
{
	if (hLightAnim > UINT16_MAX || hLightAnim >= pServerMgr->m_nLightAnims)
		return;

	LTLink* pCur = pServerMgr->m_ClientList.m_Head.m_pNext;

	while (pCur != &pServerMgr->m_ClientList.m_Head)
	{
		Client* pClient = static_cast<Client*>(pCur->m_pData);
        if (!pClient->QueueLightChange(hLightAnim,dwChangeFlags)) {
            sm_Unknown26755(pServerMgr,pClient);
            pClient->QueueLightChange(hLightAnim,dwChangeFlags);
        }

		pCur = pCur->m_pNext;
	}
}


// 0x10028858 (sm_RemoveObjectFromSky) is NOT here: smgr.cpp already carries
// it, as the documented lea/displacement near. Four more spellings were tried
// against it from this file — the value temp, the compound assignment, the
// two-statement store, and cl 8168 — and all four reproduce smgr.cpp's exact
// two-block difference, so the near class stands.


// 0x10026584 — write one animation's changed fields into a message: the id as
// a word, the change bits as a byte, and then whichever of the frame pair,
// the two percentages, the position, the colour and the radius those bits ask
// for. Name ours.
//
// Read off the bytes:
//
//   * a non-shadow-mapped animation has bits 8/0x10/0x20 masked off before
//     the bit byte goes out (`and $0xffffffc7,%ebx`), which is the same rule
//     la_CompareInfo applies from the other end — so the receiver is never
//     told about a position, colour or radius it could not have changed.
//   * the frame pair is written as two WORDS out of uint32 fields: the loads
//     are `mov 0x34(%esi),%ax`, sixteen bits, so a frame index above 65535
//     would truncate on the wire.
//   * both percentages are scaled by 255.0f, not by the 32.0f la_CompareInfo
//     quantizes them with. The comparison is therefore eight times coarser
//     than the wire format it is protecting, so a change of between 1/255 and
//     1/32 of full scale is dropped rather than sent.
//   * the colour goes out as three raw bytes with no scale at all, which is
//     what says the stored vector is already 0..255.
//   * the position goes through the compressed-position codec against the
//     server's world (&pMgr->m_World, the CPosRef the codec measures a
//     position as a fraction of), and it is handed the message's embedded
//     LMessageImpl at +0x08, not the message itself.
//   * the radius is the only field written as a full float, through
//     CPacket::WriteType<float>.
void la_WriteChange(CServerMgr* pMgr, LightAnim* pAnim, uint32 hLightAnim,
                    ServerMessage* pMsg, uint32 nChangeFlags)
{
	if (!pAnim->m_bShadowMap)
		nChangeFlags &= ~0x38;

	pMsg->WriteWord(hLightAnim);
	pMsg->WriteByte(nChangeFlags);

	if (nChangeFlags & 1)
	{
		pMsg->WriteWord(pAnim->m_iFrames[0]);
		pMsg->WriteWord(pAnim->m_iFrames[1]);
	}

	if (nChangeFlags & 2)
		pMsg->WriteByte((uint8)(pAnim->m_fPercentBetween * 255.0f));

	if (nChangeFlags & 4)
		pMsg->WriteByte((uint8)(pAnim->m_fBlendPercent * 255.0f));

	if (nChangeFlags & 8)
	{
		lmsg_WriteCompPos(&pMsg->m_Write, &pAnim->m_vLightPos,
		                  pMgr->m_World.PositionReference());
	}

	// The colour bytes go through CPacket::WriteType itself, not WriteByte:
	// lithtech.exe's /O2 build expands them (0x00462c4a..0x00462e68, each
	// with Insert2 left as a call to 0x00412f50) as it does the radius,
	// while the earlier writes stay calls (0x0042fa50, 0x004159f0) -- the
	// /O2 budget spent from the end backwards. Written as WriteType
	// throughout, all nine expand (1341 bytes against 861, measured).
	// server.dll calls 0x1001393e either way.
	if (nChangeFlags & 0x10)
	{
		((CPacket*)pMsg)->WriteType((uint8)pAnim->m_vLightColor.x);
		((CPacket*)pMsg)->WriteType((uint8)pAnim->m_vLightColor.y);
		((CPacket*)pMsg)->WriteType((uint8)pAnim->m_vLightColor.z);
	}

	if (nChangeFlags & 0x20)
		((CPacket*)pMsg)->WriteType(pAnim->m_fLightRadius);
}

// 0x1000e845 (GetSpaceLeft) and 0x10026755 (sm_Unknown26755) are in
// smgr.cpp, which is where both images put the latter: lithtech.exe's
// 0x00462fd0 sits between two of smgr.cpp's functions (0x00462ee0,
// 0x004630f0), as server.dll's 0x10026755 sits inside that file's run.
