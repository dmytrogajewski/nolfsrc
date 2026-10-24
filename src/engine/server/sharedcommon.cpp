#include "servermgr.h"
#include "servermsg.h"
#include "engine/portable/string.h"

// 0x10021c55 — the net manager's queued send, reached through a view of the
// embedded manager because servermgr.h's CNetMgr does not model it. Same
// class, name and signature smgr.cpp uses, so the two translation units
// reference one symbol.


// 0x10029609 — put the packet id in the first payload byte and hand the
// message to every client that still has a connection. The id is stored as a
// byte and the walk is the manager's client list at +0x7e8.
//
// This is sm_BroadcastToClients (0x100297b8, smgr.cpp) with the send flags
// handed in and *without* the m_State == 3 test: every connected client gets
// it, in the world or not. smgr.cpp already declares it under this name.
void sm_BroadcastToClientsEx(CServerMgr* pMgr, int nPacketID,
                             ServerMessage* pMsg, uint32 flags)
{
	LTLink* pHead;
	LTLink* pCur;

	pMsg->m_pData[0] = (uint8)nPacketID;

	pHead = &pMgr->m_ClientList.m_Head;
	pCur = pHead->m_pNext;

	while (pCur != pHead)
	{
		Client* pClient = (Client*)pCur->m_pData;

		if (pClient->m_pConnection)
		{
			(&pMgr->m_NetMgr)->SendToConnection(
				pMsg, pClient->m_pConnection, flags);
		}

		pCur = pCur->m_pNext;
	}
}

// 0x1003485a — restart every object's deactivation countdown from its stored
// time, re-file the ones that were waiting on it, and wake their cells (name
// ours). Only the AutoDeactivate variable reaches it.
//
// The body is si_SetDeactivationTime (0x10031583) run over every live object
// in the 0x8dc list, with two differences the bytes make plain: the countdown
// is reloaded from the object's own stored time rather than from a parameter
// (so the 0x14 -> 0x18 copy is an integer move, not an FPU round trip), and
// the container bump is gated on the object's +0x60 list being empty instead
// of on m_Unknown58 == -1. ltobject_internal.h still carries +0x60 as
// padding and what that list holds is not established, so it is reached by
// offset here rather than named in the header.
//
// The list is walked with the node advanced before the body, which is what
// lets the object pointer and the cursor share edi/esi.
void sm_ResetDeactivationTimers(CServerMgr* pMgr)
{
	LTLink* pHead;
	LTLink* pCur;

	pHead = &pMgr->m_List8dc.m_Head;
	pCur = pHead->m_pNext;

	while (pCur != pHead)
	{
		LTObject* pObj = (LTObject*)pCur->m_pData;
		SObjData* pSData;

		pCur = pCur->m_pNext;

		pSData = pObj->m_pSData;

		if (pSData)
		{
			uint32 nChangeFlags;

			pSData->m_UpdateTimer = pSData->m_DeactivationTime;

			nChangeFlags = pObj->m_ChangeFlags;

			if (nChangeFlags & 0x20)
				sm_SetObjectState(pMgr, pObj, nChangeFlags & 0x18);

			if (g_bAutoDeactivate)
			{
				LTObject* pView = pObj;

				if (pView->m_Link60.m_pNext == &pView->m_Link60)
					pMgr->m_Unknown210.BumpObject(pObj, 0);
			}
		}
	}
}

// 0x10028e97 — a console variable was created: tell the clients, and if it
// was AutoDeactivate, act on it here as well.
//
// Three things read off the bytes:
//
//   * the packet is allocated and then *released* immediately — the scoped
//     reference keeps the only count. That is the same pair of statements
//     ServerCommonLT::SetObjectFilenames opens with.
//   * `pIO` lives in the dead first parameter's stack slot: the console
//     state is cached in edi before the allocation, so `lea 0x8(%ebp)` is
//     the local's address and not an argument's.
//   * the deactivation sweep is reached through the second global manager
//     pointer, not through the state's own — `push 0x10058bf0` is a memory
//     push, so what it hands over is g_pServerMgr's value.
void cc_OnVarCreated(ConsoleState* pState, LTCommandVar* pVar)
{
	CMessageRef msg;

	if (pState->m_pServerMgr)
	{
		CAbstractIO* pIO;

		msg.Init(msg_AllocPacket(&pIO, 0x1000, 0x1000));
		if (pIO)
			pIO->Release();

		if (nolf::ascii_compare(pVar->m_pName, "AutoDeactivate") == 0)
			sm_ResetDeactivationTimers(g_pServerMgr);

		{
			ServerMessage* pMsg = msg.m_pMsg;

			pMsg->m_Unknown38 = 1;
			pMsg->m_Unknown34 = 1;
		}

		(msg.m_pMsg)->WriteString(pVar->m_pName);
		(msg.m_pMsg)->WriteString(pVar->m_pStringVal);

		sm_BroadcastToClientsEx(pState->m_pServerMgr, 0x10, msg.m_pMsg, 0x80);
	}
}

// 0x10028f42 / 0x0046ef0 — a console variable changed: exactly the same
// notification, forwarded. (The two share one enumerated range in
// the server.dll function enumeration, which is why the entry reads 187 bytes and
// the created half is 171.)
//
// THE LITHTECH ADDRESS IS 0x00466ef0, and writing it down here is the point:
// this row spent the whole project on the unexamined list because the banner
// carried only the server address. It also corrects the decoded map, which
// names 0x00466e30 `cc_OnVarCreated` on `bridged-funcptr` evidence -- the
// pointer that is actually STORED is 0x00466ef0, at 0x00466f67
// (`movl $0x00466ef0,0x34(%esi)`, bytes c7 46 34 f0 6e 46 00), and
// 0x00466e30's only inbound `e8` in the image is from inside this forwarder.
// So 0x00466ef0 is the installed hook and 0x00466e30 is its out-of-line body.
void cc_OnVarChanged(ConsoleState* pState, LTCommandVar* pVar)
{
	cc_OnVarCreated(pState, pVar);
}


// 0x00466f80, 12 bytes — the console teardown forwarder.
//
//      8b 44 24 04   mov 0x4(%esp),%eax
//      50            push %eax
//      e8 ..         call 0x0041b200
//      59            pop %ecx
//      c3            ret
//
// cdecl, one argument, forwarded unchanged. The callee 0x0041b200 is
// con_Term — clientconsole.cpp:975 and cmgrinit.cpp:88 both name it. The one
// caller, 0x00473696, passes `lea 0xa74(%esi),%edx`, the ConsoleState
// embedded in the manager at +0xa74. `pop %ecx` rather than `add $4,%esp` is
// the one-argument cdecl cleanup /O2 always picks.
void con_Term(ConsoleState* pState);         // 0x0041b200

void cc_TermConsole(ConsoleState* pState)
{
	con_Term(pState);
}
