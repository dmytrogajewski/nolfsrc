// server.dll — CNetMgr (reconstruction). Build /O1.
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <new>
#include "servermgr.h"
#include "servermsg.h"
#include "packet.h"
#include "de_error.h"

char g_NetNameBuf[1]{};

// 0x10058778 — the net trace's timestamp counter, started at load (mode 1).
// Its dynamic initialiser is server.dll 0x10021179 (the zero-displacement
// jmp head) / 0x1002117e (push 1; mov ecx,&g_NetCounter; call the
// 0x10005f38 constructor; ret), the first code of this TU -- which is why
// the definition is first here. CBaseCounter has no destructor, so there
// is no atexit piece. server.dll only: lithtech.exe has no such
// initialiser anywhere (no `push 1; mov ecx,imm; call; ret` in its .text)
// and its trace lines push a literal 0 instead (see NET_TRACE_TIME).
#ifdef LT_SERVER_BUILD
CBaseCounter g_NetCounter(1);
#endif

// 0x1002136b / 0x00455310. The two scalar stores are in *this* order and not
// the other way round because lithtech.exe (size-exact, 242 of 242 bytes, and
// byte-identical for the first 213) puts `mov %ebx,0x100(%esi)` ahead of
// `mov %ebx,0xd4(%esi)` -- a straight transposition of two adjacent
// independent stores, which at /O2 (where the memset is expanded and the
// whole body is straight-line) follows source order. server.dll cannot read
// the order back: there the memset is a CALL, m_FrameTime goes out as an x87
// `fstps` whose position is pinned to the `fldz` the previous call leaves
// room for, and the m_pMainDriver store is sunk between two of memset's
// pushes.
CNetMgr::CNetMgr()
{
	m_pMainDriver = LTNULL;
	m_FrameTime = 0.0f;
    m_pHandler = nullptr;
    m_pTraceName = m_Name;
    m_Name[0] = 0;
    m_LastTime = 0;
    m_UnknownD8 = m_UnknownDC = 0;
	memset(&m_Guid, 0, sizeof(m_Guid));
	m_Flags = 0;
}

LTRESULT CNetMgr::Init()
{
	TermDrivers();

	if (!AddDriver("local") || !AddDriver("internet")) {
        TermDrivers();
        return LT_ERROR;
    }

	return LT_OK;
}

// 0x100213ee — the manager handed back. Term() is the whole body; the five
// sub-objects are the compiler's, expanded in reverse declaration order
// (m_QueueC4, m_QueueB8, m_Drivers, m_Connections, m_PacketBank).
CNetMgr::~CNetMgr()
{
	Term();
}


void CNetMgr::Term()
{
	TermDrivers();

	m_Name[0] = 0;
	m_pHandler = LTNULL;
	m_UnknownD8 = 0;

	TermQueue((LMessageList*)&m_QueueB8);
	TermConnections();
}

LTBOOL CNetMgr::SetNetName(char* pName)
{
	char* pDst = m_Name;

	m_UnknownD8 = 0;
	pDst[0] = 0;
	m_PacketBank.m_Bank.m_Unknown08 = 0x100;
	m_UnknownDC = 0;
	m_pTraceName = g_NetNameBuf;

	if (!pName) return LTFALSE;
    strncpy(pDst, pName, sizeof(m_Name) - 1);
    pDst[sizeof(m_Name) - 1] = 0;

	m_LastTime = dsi_GetTime();
	SetGuid(LTNULL);
	m_pMainDriver = LTNULL;

	return LTTRUE;
}

LTRESULT CNetMgr::GetServiceList(NetService*& pListHead)
{
	uint32 i;

	pListHead = LTNULL;
	for (i = 0; i < m_Drivers.m_nElements; i++)
		m_Drivers.m_pArray[i]->GetServiceList(&pListHead);

	return LT_OK;
}

LTRESULT CNetMgr::FreeServiceList(NetService* pListHead)
{
	while (pListHead)
	{
		NetService* pNext = pListHead->m_pNext;
		::operator delete(pListHead);
		pListHead = pNext;
	}

	return LT_OK;
}

LTRESULT CNetMgr::SelectService(HNETSERVICE service)
{
    if (!service) return LT_INVALIDPARAMS;
    for (uint32 i = 0; i < m_Drivers.m_nElements; ++i) {
        auto* driver = m_Drivers.m_pArray[i];
        if (driver->SelectService(service) == 0) {
            m_pMainDriver = driver;
            return LT_OK;
        }
    }
    return LT_NOTINITIALIZED;
}

LTRESULT CNetMgr::UpdateSessionName(const char* sName)
{
	if (!m_pMainDriver)
		return LT_NOTINITIALIZED;

	return m_pMainDriver->UpdateSessionName(sName);
}

LTRESULT CNetMgr::GetTcpIpAddress(char* sAddress, uint32 dwBufferSize,
                                  unsigned short& hostPort)
{
	if (!m_pMainDriver)
	{
		sAddress[0] = 0;
		return LT_NOTINITIALIZED;
	}

	return m_pMainDriver->GetTcpIpAddress(sAddress, dwBufferSize, hostPort);
}

void CNetMgr::Disconnect(NetConnection* pConn, uint32 nReason, uint32 flags)
{
	if (!pConn)
		return;

	RemoveConnection(pConn);
	DebugPrint(pConn, 1, "CNetMgr::Disconnect called, (reason %d).", nReason);

	pConn->m_pDriver->Disconnect(pConn, nReason, flags);
}

void CNetMgr::Update(char* pTraceName, float fCurTime, int bAllowTimeout)
{
	CMessageRef msg;
	uint32 i;
	float fDelta;

	m_pTraceName = pTraceName;

	fDelta = fCurTime - m_LastTime;
	fDelta = LTCLAMP(fDelta, 0.001f, 0.3f);

	m_LastTime = fCurTime;
	m_FrameTime = fDelta;

	{
		CAbstractIO* pIO;
		msg.Init(msg_AllocPacket(&pIO, 0x1000, 0x1000));
		if (pIO)
			pIO->Release();
	}

	for (i = 0; i < m_Connections.m_nElements; i++)
	{
		NetConnection* pConn = m_Connections.m_pArray[i];

		// A connection flagged for death gets told three times, then dropped.
		if (pConn->m_Flags & 4)
		{
			{
				ServerMessage* pMsg = msg.m_pMsg;
				pMsg->m_Unknown38 = 1;
				pMsg->m_Unknown34 = 1;
			}
			msg.m_pMsg->m_pData[0] = 1;
			msg.m_pMsg->WriteByte(1);

			SendPacket(msg.m_pMsg, pConn, 0);
			SendPacket(msg.m_pMsg, pConn, 0);
			SendPacket(msg.m_pMsg, pConn, 0);

			Disconnect(pConn, 2, 0);

			i = (uint32)-1;
			continue;
		}

		// Resend whatever has waited too long for its ack.
		CNetSentPacket* pCur = (CNetSentPacket*)pConn->m_SentList.m_pTail;
		while (pCur)
		{
			CNetSentPacket* pRec = pCur;
			pCur = (pCur->m_pNext == (CNetSentPacket*)pConn->m_SentList.m_pTail) ?
				LTNULL : static_cast<decltype(pCur)>(pCur->m_pNext);

			pRec->m_ResendTimer += fDelta;
			if (pRec->m_ResendTimer >= g_NetResendTime)
			{
				SendPacketData(&pRec->m_Packet, pConn);
				((LMessageList*)&pConn->m_SentList)->Remove((LThreadMessage*)pRec);
				m_PacketBank.Free(pRec);
			}
		}

		pConn->m_SendPPS.Update(fDelta);
		pConn->m_SendBPS.Update(fDelta);
		pConn->m_RecvPPS.Update(fDelta);
		pConn->m_RecvBPS.Update(fDelta);

		pConn->m_Unknown98 += fDelta;
		pConn->m_AckTimer += fDelta;
		pConn->m_UnknownB4 -= fDelta;
		pConn->m_RecvWait += fDelta;

		if (!(pConn->m_Flags & 1))
		{
			if (pConn->m_AckTimer > g_NetAckTime)
			{
				DebugPrint(pConn, 2, "Sending ack  local: %d remote: %d.",
					pConn->m_PacketID - 1, pConn->m_RemoteAckID);

				{
					CAbstractIO* pIO;
					msg.Init(msg_AllocPacket(&pIO, 0x1000, 0x1000));
					if (pIO)
						pIO->Release();
				}

				WriteAckPacket(pConn, msg.m_pMsg);
				SendPacket(msg.m_pMsg, pConn, 0);
			}
		}

		if (bAllowTimeout && g_NetTimeouts)
		{
			if (pConn->m_RecvWait > pConn->m_pDriver->m_Timeout)
			{
				i--;

				if (g_DebugLevel > 0)
					dsi_ServerError("Connection dead (timed out).");

				RemoveConnection(pConn);
				m_pHandler->_v10(5, g_NetNameBuf, 0);
				pConn->m_pDriver->Disconnect(pConn, 3, 0);
			}
		}
		else
		{
			pConn->m_RecvWait = 0.0f;
		}
	}

	m_RateE0.Update(fDelta);

	if (g_NetShowStats)
	{
		for (i = 0; i < m_Connections.m_nElements; i++)
		{
			NetConnection* pConn = m_Connections.m_pArray[i];

			DebugPrint(pConn, 0, "Conn %d Info - Ping: %.2f", i, pConn->m_Ping);
			DebugPrint(pConn, 0, "Conn %d Recv - Wait: %.1f, Q Size: %d, PPS: %.1f, BPS: %.1f",
				i, pConn->m_RecvWait, pConn->m_GuaranteedQueue.m_nMessages,
				pConn->m_RecvPPS.m_Value / pConn->m_RecvPPS.m_Time,
				pConn->m_RecvBPS.m_Value / pConn->m_RecvBPS.m_Time);
			DebugPrint(pConn, 0, "Conn %d Send - Wait: %.1f, Q Size: %d, PPS: %.1f, BPS: %.1f",
				i, pConn->m_SendWait, pConn->m_SendQueue.m_nMessages,
				pConn->m_SendPPS.m_Value / pConn->m_SendPPS.m_Time,
				pConn->m_SendBPS.m_Value / pConn->m_SendBPS.m_Time);
		}
	}

	for (i = 0; i < m_Drivers.m_nElements; i++)
		m_Drivers.m_pArray[i]->Update();
}

void CNetMgr::StartGettingPackets()
{
	m_Flags |= 1;
}

void CNetMgr::EndGettingPackets()
{
	m_Flags &= ~1;
}

// Byte-identical on both images. Two spellings carry it:
//
// * The guaranteed loop runs on a LOCAL, `bDone`, which starts as the
//   connection's flag and is then set from TranslatePacket. The `if (bDone)
//   return` just before the back edge tells VC6 that bDone is zero there, so
//   the back edge skips the loop test and jumps straight to the
//   FindQueuedPacket pushes (0x10022930 `jmp 0x100228db`). No value is live
//   across a call, so nothing is spilled. The flag spelled out in the `while`
//   re-tests memory on every pass. A local that holds only the flag (never
//   reassigned) stays live across the loop and gets spilled (+9 bytes).
// * The memcpy destination is read through `uint8** ppData = &pMsg->m_pData`.
//   Taking the field's address stops VC6 hoisting the m_pData load above the
//   m_Unknown34 store. /O1 does not care, but /O2 (lithtech 0x004579d6) keeps
//   the store first and loads src, then dest. Same fix as WriteAckPacket below.
LTBOOL CNetMgr::GetPacket(ServerMessage* pMsg)
{
	CBaseDriver* pDriver;
	NetConnection* pConn;
	uint32 i;

	// Fresh arrivals straight off the drivers.
	for (i = 0; i < m_Drivers.m_nElements; i++)
	{
		pDriver = m_Drivers.m_pArray[i];
		while (pDriver->ReceivePacket(pMsg))
		{
			AddRecvBytes(pMsg->m_pConnection, pDriver->GetRecvSize() + pMsg->m_Unknown34);
			pMsg->m_Unknown38 = 1;

			if (g_NetLogPackets)
			{
				net_LogPacket(pMsg, pMsg->m_Unknown34, 1);
				pMsg->m_Unknown38 = 1;
			}

			if (TranslatePacket(pMsg, pMsg->m_pConnection, 1))
				return LTTRUE;
		}
	}

	for (i = 0; i < m_Connections.m_nElements; i++)
	{
		pConn = m_Connections.m_pArray[i];

		// Whatever already arrived and waited.
		CNetRecvPacket* pCur = (CNetRecvPacket*)pConn->m_RecvList.m_pTail;
		while (pCur)
		{
			CNetRecvPacket* pRec = pCur;
			pCur = (pCur->m_pNext == (CNetRecvPacket*)pConn->m_RecvList.m_pTail) ?
				LTNULL : static_cast<decltype(pCur)>(pCur->m_pNext);

            if (!pRec->m_Len || !pMsg->Reserve(pRec->m_Len)) {
                pConn->m_RecvList.Remove(pRec); FreeRecvPacket(pRec); continue;
            }
            memcpy(pMsg->m_pData, pRec->m_pData, pRec->m_Len);
			pMsg->m_Unknown34 = (uint16)pRec->m_Len;
			pMsg->m_Unknown38 = 1;
            pMsg->m_pConnection = pConn;
            pMsg->m_Flags = 0;

            pConn->m_RecvList.Remove((LThreadMessage*)pRec);
			FreeRecvPacket(pRec);

			if (TranslatePacket(pMsg, pConn, 0))
				return LTTRUE;
		}

		// Guaranteed packets become visible in order.
		int bDone = pConn->m_Flags & 1;
		while (!bDone)
		{
			CNetRecvPacket* pRec = (CNetRecvPacket*)FindQueuedPacket(&pConn->m_GuaranteedQueue,
			                                                         pConn->m_PacketID);
			if (!pRec)
				break;

			pMsg->m_pConnection = pConn;
			uint16 nLen = (uint16)pRec->m_Len;
			pMsg->m_Unknown34 = nLen;
            if (!nLen || !pMsg->Reserve(nLen)) break;
            memcpy(pMsg->m_pData, pRec->m_pData, nLen);
            pMsg->m_Flags = 0;

            DebugPrint(pConn, 1, "Resolving guaranteed packet %d", pConn->m_PacketID);
			pMsg->m_Unknown38 = 1;

			bDone = TranslatePacket(pMsg, pConn, 0);
			if (bDone)
				return LTTRUE;
		}
	}

	return LTFALSE;
}


// ---- the constructors and the bank free --------------------------------- //

// A tracker starts at zero with a tenth-of-a-millisecond floor and a two
// second window (the constants are what they are; the names are ours).


// 0x10023fe7 and 0x10024097 are `CMoArray<T, DefaultCache>::CMoArray()`, and
// the two lists are now servermgr.h's typedefs onto collection.h's template,
// so the constructor, the destructor and every array body below came off this
// file with them. What used to stand here was a hand-written 34-byte
// constructor that could only get NEAR (the shipped build materialises the
// zero after caching `this` in esi and pushes Init's two arguments apart),
// because a non-template member declared out-of-class is the one construct
// /Ob1 never expands -- which is also why lithtech.exe could never match it:
// there (0x00455310) the whole constructor is expanded into CNetMgr's.

// Removing a driver is expected to shorten the list; if it does not, stop
// rather than spin.
void CNetMgr::TermDrivers()
{
	uint32 nCount;
	uint32 nBefore;

	nCount = m_Drivers.m_nElements;
	while (nCount > 0)
	{
		nBefore = nCount;
		RemoveDriver(m_Drivers.m_pArray[0]);
		nCount = m_Drivers.m_nElements;

		if (nBefore == nCount)
			break;
	}
}

void CNetMgr::TermConnections()
{
	// ReleaseQueued (0x10023542), not CloseConnection (0x10022bc8): the
	// queue this drains is the pending-record queue, and this was read off the
	// shipped call target rather than trusting the name.
	while (m_QueueC4.m_pTail)
		ReleaseQueued((CNetPending*)m_QueueC4.m_pTail);
}

// Both trackers are fed per packet: one counts bytes, the other counts the
// packet itself. The constant really is 1.0f — the packets-per-second
// tracker gains one per packet. (It read 0.001f here until the send side
// was reconstructed and the two `fadds` turned out to share one .rdata
// address; a float operand is a masked relocation, so the byte comparison cannot tell
// the two constants apart and had been reporting a match either way.)
void CNetMgr::AddRecvBytes(NetConnection* pConn, uint32 nBytes)
{
	if (!pConn)
		return;

	pConn->m_RecvPPS.m_Value += 1.0f;
	pConn->m_RecvBPS.m_Value += nBytes;
}


// With no driver selected there is no session to name, and the caller's
// buffer is emptied rather than left as it was.
LTRESULT CNetMgr::GetSessionName(char* sName, uint32 dwBufferSize)
{
	CBaseDriver* pDriver = m_pMainDriver;

	if (!pDriver)
	{
		sName[0] = 0;
		return LT_NOTINITIALIZED;
	}

	return pDriver->GetSessionName(sName, dwBufferSize);
}


// ---- the shared message list -------------------------------------------- //

// CMessageQueueBase::Append USED to be defined here, and the out-of-line copy
// still sits in this module's contribution in both images (server.dll
// 0x100242e2, lithtech 0x45a1c0). It is now in-class in lthread.h, because
// lithtech expands it two levels deep — inside a Prepend that is itself
// expanded — at 0x41d580 (GenAppend), 0x43a03a (GenAppendList) and 0x444384
// (PostMessage), which /Ob1 can only do for a definition the header carries.
//
// The COMDAT this module emits for it is the one the linker keeps: three
// callers here decline the expansion at /Ot (0x457bce, 0x45840a, 0x459227)
// and every caller declines at /Os.


// ---- one connection ----------------------------------------------------- //

// The driver pointer is deliberately left alone: whoever builds the
// connection fills it in before anything reaches it.


// 0x1002425d — the CNetBank flavour of tm_FreeMessages: walk the list with
// the cursor-alias trick, hand each record to the bank's Free (a thiscall on
// the bank, so the bank is the second argument here and `this` there), and
// leave the list empty (name ours, from its shape).
//
// `inline`: lithtech.exe has no standalone copy -- NetConnection::Term
// (0x004551c0) carries it expanded, with Free expanded inside it. server.dll
// still calls 0x1002425d out of line (/Os declines it) and still matches.
inline void netbank_FreeMessages(LMessageList* pList, CNetBank* pBank)
{
	LThreadMessage* pCur = pList->m_pTail;
	LThreadMessage* pFree;

	while (pCur)
	{
		pFree = pCur;
		pCur = (pCur->m_pPrev == pList->m_pTail) ? LTNULL : pCur->m_pPrev;

		pBank->Free(pFree);
	}

	pList->m_pTail = LTNULL;
	pList->m_nMessages = 0;
}

// Two things NetConnection's declaration in servermgr.h does not carry.
//
// First, three fields it leaves in padding — +0x78, +0x80 and a three-float
// array at +0xa0 — plus a name for +0x74. The two counters are named by the
// format string NetConnection::Term prints them with, "total sent: %d,
// resent: %d", which is the only attestation either of them has;
// servermgr.h calls +0x74 m_RemoteAckID, and the string says it is the
// packets-sent total. The +0xa0 array is an array because
// CNetMgr::AddConnection zeroes it with a *loop* (offset 0xa0 to 0xac in
// steps of four) rather than three stores.
//
// The rest of the fields are servermgr.h's under servermgr.h's names; they
// are repeated here so the whole of AddConnection's reset can run through
// one view instead of alternating between two.


// ServerMessage's dword writer and the room-left query. servermsg.h names
// the byte, word and block writers but neither of these (names ours — the
// second from the two places its answer is compared against, a length plus
// four and the 0x2c an ack packet needs; it is signed, since both compares
// are `jge`/`jl`).


// A receive record's last field, one dword past where servermgr.h's
// CNetRecvPacket stops: SendPacketData sets it to 1 on a record it queued
// but did not send, and the send-queue walk clears it once the record has
// gone out (name ours).
using CNetRecvPacketSendView = CNetRecvPacket;

// CBaseDriver's slot 0x60 is the raw send: servermgr.h has it as `_p60` with
// no arguments because nothing had reached it. It takes the payload, its
// length, the spare capacity behind it and the connection, and returns
// whatever the driver made of it — which is SendPacketData's own return value
// (name ours).


// 0x100211e2 — a free function of one message: nonzero means the message can
// take more data as it stands, so the send path does not have to swap in a
// fresh one. Cdecl, one argument (name ours, from what the two call sites
// branch on).
int net_CanAppend(class ServerMessage* pMsg);

// The three-word scratch SendPacketData keeps a guaranteed packet's length
// and write position in while it appends the sequence number, so it can put
// them back afterwards. The restore appears at every one of the function's
// exits, which is what says it is a destructor and not a statement — and the
// three slots are contiguous (-0x18, -0x14, -0x10 of a 0x18-byte frame) in
// exactly this order, which is what says they are one object (names ours).
class CMsgLenSave
{
public:
	CMsgLenSave() { m_nLen = 0; m_nPos = 0; m_pMsg = LTNULL; }

	~CMsgLenSave()
	{
		if (m_pMsg)
		{
			m_pMsg->m_Unknown34 = (uint16)m_nLen;
			m_pMsg->m_Unknown38 = (uint16)m_nPos;
		}
	}

	class ServerMessage* m_pMsg;             // +0x00
	uint32               m_nLen;             // +0x04
	uint32               m_nPos;             // +0x08
};

// 0x10053364 — the ack timer past which an ack is piggybacked onto whatever
// is going out anyway; a plain initialised global (0.125f), with no console
// binding of its own, sitting immediately before g_NetAckTime (name ours).
// Defined here because both are this TU's own .data: they open it, directly
// ahead of the "Conn %p closing" literal NetConnection::Term prints.
float g_NetPiggybackTime = 0.125f;

// 0x10053368 — servermgr.h's g_NetAckTime, one third of a second
// (0x3eaaaaab).
float g_NetAckTime = 1.0f / 3.0f;

// CBaseMgr's slot 4 is the new-connection notification: the handler is given
// the connection and one bit off its flags, and answers whether it took it.
// servermgr.h types the slot as two `void*` because nothing had pinned the
// arguments down; this view gives it the shape the call site shows, with a
// placeholder in slot 0 for the destructor.


// And the queues' destructors are not empty: each stores its vptr (as any
// polymorphic destructor does) and then clears m_pTail and m_nMessages, in
// that order — the same order CNetMgr::TermQueue and 0x1002425d empty a list
// in. lthread.h and servermgr.h model both destructors as `{}` because
// nothing else reconstructed expands one, so the bodies live here on views
// with the same layout. Three of Term's four queues are LMessageList and the
// fourth is CNetSentQueue, which is exactly why the shipped code loads one
// vtable address into a register and stores it three times while the fourth
// is its own immediate.
//
// The vptr the views restore is the REAL class's: NetConnection's
// constructor (0x10021200, byte-identical) stores ??_7LMessageList@@6B@ as
// 0x1004dd2c and ??_7CNetSentQueue@@6B@ as 0x1004dcfc, and Term's two
// stores read back exactly those addresses. A polymorphic view would store
// a vtable of its own (and leave a never-defined slot for the link to
// trip over), so the vptr is a plain member and the store names the table
// by address, as CTermAbstractIOB3 above does; link/aliases.rsp binds the
// two names to the real vtables (names ours).
// g_NetDebugLevel is 0x100584ec, and the console name it is registered
// under is "TransportDebug" — the variable table at 0x10051f94 pairs that
// literal with this address. It doubles as the switch on the closing
// statistics here and as CNetMgr::DebugPrint's threshold.


// 0x1002126a — tear a connection's four queues down and put them back to
// empty. Three are message lists the manager knows how to drain; the fourth,
// the awaiting-ack list, holds bank records instead and goes back to the
// manager's packet bank.
//
// The manager is re-read off the driver at every one of the four calls —
// `mov 0x30(%esi),%eax; mov 0x48(%eax),%ecx` four times over — so there is no
// local for it. Each queue's *address*, on the other hand, is used twice (the
// drain and the clear) and gets exactly one `lea` each, which is what says
// those four are locals.
//
// The resend ratio is a **float** divide of two `uint32` counters, and that
// is the one thing here that took probing rather than reading. Both operands
// go through the unsigned-to-64-bit store pair (low dword, then a zero high
// dword) into one shared 8-byte frame temp, but only the numerator gets a
// `fildll`: the divide is `fidivl` on the divisor's *low dword*, which
// leaves the zero it just wrote to the high dword dead. Written as a
// `double` divide instead, VC6 emits a second `fildll` and an `fdivrp` and
// the function is two bytes long; written with either operand signed it
// drops the dead store and comes out three bytes short. None of the four
// compiler revisions moves any of that, so the float is source, not
// scheduling — a new fingerprint: **a dead zero store into the high half of
// an x87 conversion temp, followed by `fidivl` on the low half, is a float
// divide by an unsigned**.



// Nothing has been heard for longer than the timeout, and the connection is
// past the handshake. The message goes out only when the net console
// variable asks for it.



// ---- the drivers -------------------------------------------------------- //

// Two driver names are understood, and an unknown one simply produces
// nothing. A driver that fails its own initialization is destroyed rather
// than kept in the list.
CBaseDriver* CNetMgr::AddDriver(char* pDriverName)
{
	CBaseDriver* pDriver = LTNULL;

	if (strcmp(pDriverName, "local") == 0)
	{
		pDriver = ldrv_Create();
	}
	else if (strcmp(pDriverName, "internet") == 0)
	{
		pDriver = tcp_Create();
	}

	if (pDriver)
	{
		pDriver->m_pNetMgr = this;
        pDriver->set_host(this);

		if (!pDriver->Init())
		{
			if (pDriver)
				delete pDriver;

			return LTNULL;
		}

		CDriverList* pList = &m_Drivers;
		if (!pList->Insert2(pList->m_nElements, pDriver, &g_DefAlloc)) {
            delete pDriver;
            return nullptr;
        }
	}

	return pDriver;
}

// The driver is dropped from the list whether or not it was in it: a -1
// index is what the array takes to mean "nothing to remove".
//
// The search is dynarray.h's CMoArray::FindElement (dynarray.h:509), which
// collection.h declares and never defines; it is defined HERE rather than
// there because every `template class CMoArray<...>` explicit instantiation
// in the tree would then instantiate it too, and for struct element types
// with no operator== that does not compile. It is the lever for lithtech.exe
// 0x004561e0: written as an open loop, VC6's /O2 inline budget had room
// left to expand BaseNew inside Remove2 (`call *0x4(%edx)`); the FindElement
// expansion spends that room and BaseNew stays the 3-argument cdecl call
// (0x004131d0) the shipped function makes. That took the row from 321
// bytes / 11 blocks to size-exact 315 / 2 blocks. The residual is inside
// Remove2's second copy loop (`pNewArray[i] = m_pArray[i+1]`): shipped
// loads m_pArray into a spare register (ebp) and addresses through it, ours
// folds it into edi -- the same template body compiles to both forms in
// this very object (GenRemoveAt has ours, AddConnection's expansion has
// shipped's), so it is register allocation, not source. server.dll's
// 0x10021bbc is unchanged by the rewrite.

void CNetMgr::RemoveDriver(CBaseDriver* pDriver)
{
	uint32 iFound = m_Drivers.FindElement(pDriver);

	if (pDriver == m_pMainDriver)
		m_pMainDriver = LTNULL;

	if (pDriver)
		delete pDriver;

	m_Drivers.Remove2(iFound, &g_DefAlloc);
}


// ---- connections and their queues --------------------------------------- //

// The notice goes out before the handler is told, and only a connection that
// was actually in the list is announced.
//
// The handler is told which connection died: the notification slot takes it
// as an argument, which is what the shipped call pushes.
//
// The search is CMoArray::FindElement, as in RemoveDriver above. An open
// loop gives the same /O1 bytes (0x10022bc8) but leaves lithtech.exe's /O2
// budget room to expand BaseNew inside Remove2 (`call *0x4(%edx)`, 338
// bytes); shipped 0x00458030 CALLS BaseNew (0x004131d0), 321 bytes, which
// the FindElement expansion reproduces exactly.
void CNetMgr::CloseConnection(NetConnection* pConn)
{
	uint32 iFound = m_Connections.FindElement(pConn);

	con_Printf("DisconnectNotify\n");

	if (iFound != BAD_INDEX)
	{
		m_Connections.Remove2(iFound, &g_DefAlloc);
		if (m_pHandler)
			m_pHandler->_v08(pConn);
	}
}

// Everything still queued goes back to the allocator, payloads first. The
// manager's outstanding-bytes running total is wound back as it goes.
void CNetMgr::TermQueue(LMessageList* pQueue)
{
	CNetRecvPacket* pCur = (CNetRecvPacket*)pQueue->m_pTail;
	CNetRecvPacket* pRec;

	while (pCur)
	{
		pRec = pCur;
		pCur = (pCur->m_pNext == (CNetRecvPacket*)pQueue->m_pTail) ?
		           LTNULL : static_cast<decltype(pCur)>(pCur->m_pNext);

		if (pRec->m_pData)
		{
			m_UnknownD8 -= pRec->m_nAlloced;
		}

		delete pRec;
	}

	pQueue->m_pTail = LTNULL;
	pQueue->m_nMessages = 0;
}

// The queue is in sequence order, so a record past the one asked for says it
// is not there at all.
//
// NEAR (50 of 46 bytes): the shipped build folds the "not found" exit into
// the loop's own top test and reaches the loop back edge with the same
// compare that answers the two hits; ours rotates the loop, peels the first
// null test and pays a duplicated one at the bottom for it. Four source
// shapes give byte-for-byte the same rotated code: this while/continue/break
// form, the same with `break` spelt `return LTNULL`, `for(;;)` with the null
// test as an explicit early return, and a literal label with a backward
// `goto`. VC6 canonicalises the loop before it lays anything out, so the
// shape of the source is not the lever here.
void* CNetMgr::FindQueuedPacket(LMessageList* pList, uint32 nID)
{
	CNetRecvPacket* pTail = (CNetRecvPacket*)pList->m_pTail;
	// RE-READ THE MEMBER, do not copy pTail. Written `pCur = pTail;`
	// VC6 tests the tail and sinks the copy; the shipped bytes copy first
	// and test the copy. Proven in-tree by tcpdriver.cpp's tcp_FindConnection,
	// which reads the member twice and is byte-identical.
	CNetRecvPacket* pCur = (CNetRecvPacket*)pList->m_pTail;

	while (pCur)
	{
		CNetRecvPacket* pRec = pCur;

		pCur = (pCur->m_pPrev == pTail) ? LTNULL : static_cast<decltype(pCur)>(pCur->m_pPrev);

		if (pRec->m_ID == nID)
			return pRec;

		if (pRec->m_ID > nID)
			break;
	}

	return LTNULL;
}

// The app guid the drivers advertise; a null argument clears it.
void CNetMgr::SetGuid(LTGUID* pGuid)
{
	if (pGuid)
		memcpy(&m_Guid, pGuid, sizeof(m_Guid));
	else
		memset(&m_Guid, 0, sizeof(m_Guid));
}

// 0x10005fab — the counter's tail-jump forwarder to Read (defined in
// counter.cpp, which is where the whole class lives). DebugPrint reaches it
// rather than Read itself: the shipped call sets ecx to the counter and
// pushes nothing, and the target is five bytes of `jmp`. Named by its
// address; the forward is a masked relocation either way, so only the
// shipped disassembly separates the two.


// The "(%d)" in the trace line is the net counter's elapsed time in
// server.dll only. Both lithtech.exe copies (0x459bb0 CNetMgr::DebugPrint,
// 0x459b50 the connectionless overload) push a literal `$0x0` there -- no
// counter call, no counter reference at all -- so the executable's build
// passes a constant zero. The guard is the stand-in for however the original
// configured that out.
#ifdef LT_SERVER_BUILD
#define NET_TRACE_TIME() (g_NetCounter.Read())
#else
#define NET_TRACE_TIME() 0
#endif

// The trace line carries the elapsed counter and whatever name the update is
// running under, so a client and a server log interleave readably. Quiet
// while the connection is still coming up unless the console asks for
// everything.
void CNetMgr::DebugPrint(NetConnection* pConn, int nLevel, char* pFormat, ...)
{
	char    msg[500];
	va_list marker;

	if (nLevel > g_NetDebugLevel)
		return;

	if (!g_NetDebugAll && (pConn->m_Flags & 1))
		return;

	va_start(marker, pFormat);
	vsnprintf(msg, sizeof(msg) - 1, pFormat, marker);

	dsi_ServerError("(%d) %s%s", NET_TRACE_TIME(), m_pTraceName, msg);
}


// 0x100263e7 — age the window by one frame. A tracker with no window length
// set never rolls over; one that has runs its rollover as soon as the window
// is past.



// 0x10026413 — the window closes and reopens a tenth as wide: the rate over
// the window that just ended is scaled into the new one, so a tracker that
// has been counting for two seconds carries a proportional head start rather
// than restarting at zero. Both the new window and the scale are the same
// 0.1f, loaded once and used twice — which is why the multiply takes its
// operand from the stack register rather than reloading the field that was
// just written.



// 0x10023569 — the four pieces a pending record owns are released back to
// front, each through its own vtable. A piece that was never filled in is
// skipped (name ours).
//
// `inline`, and the proof is both-sided rather than a size guess. The shipped
// /O2 client EXPANDS this body into ReleaseQueued: lithtech.exe 0x00458d50 is
// byte-for-byte our ReleaseQueued for its first 0x44 bytes and then carries
// this loop verbatim -- `lea 0x18(%edi),%esi; mov $0x4,%ebx;
// mov -0x4(%esi),%ecx; sub $0x4,%esi; test %ecx,%ecx; je; mov (%ecx),%edx;
// call *0x8(%edx); dec %ebx; jne` -- with net_Free after it, so Term was an
// inline candidate in the true source. The shipped /O1 server CALLS it: the
// only `e8` in server.dll reaching 0x10023569 is at 0x10023559, inside
// ReleaseQueued's own 39 bytes, so /Os declined the expansion at that site
// WITH THAT SAME SOURCE. Our body is byte-identical at /O1 (0x10023569, 29 of
// 29), so the keyword cannot move server.dll: not this COMDAT, which the
// declined call site keeps alive, and not ReleaseQueued, which keeps its
// call. It is the 31-byte size that argues against marking it and the two
// shipped images that settle it.
// 0x10023542 — take a pending record off the queue, let go of what it owns,
// and hand the record itself back (name ours).
void CNetMgr::ReleaseQueued(CNetPending* pRec)
{
	if (pRec)
	{
		((LMessageList*)&m_QueueC4)->Remove((LThreadMessage*)pRec);

		delete pRec;
	}
}


// NEAR (106 of 106 bytes): an ebx/edi transposition, nothing else.
//
// 0x10023bb5 — a receive buffer goes back into the pool, which is kept in
// size order so a later request can take the smallest one that will do. The
// walk runs back from the largest and stops at the first buffer this one is
// not smaller than; running off the end means it is the smallest there is.
void CNetMgr::FreeRecvPacket(CNetRecvPacket* pRec)
{
	CNetRecvPacket* pTail;
	CNetRecvPacket* pCur;
	CNetRecvPacket* pAt = LTNULL;

	pRec->m_Len = 0;

	pTail = (CNetRecvPacket*)m_QueueB8.m_pTail;
	// RE-READ THE MEMBER, do not copy pTail. Written `pCur = pTail;`
	// VC6 tests the tail and sinks the copy; the shipped bytes copy first
	// and test the copy. Proven in-tree by tcpdriver.cpp's tcp_FindConnection,
	// which reads the member twice and is byte-identical.
	pCur = (CNetRecvPacket*)m_QueueB8.m_pTail;

	while (pCur)
	{
		pAt = pCur;
		pCur = (pCur->m_pNext == pTail) ? LTNULL : static_cast<decltype(pCur)>(pCur->m_pNext);

		if (pRec->m_nAlloced >= pAt->m_nAlloced)
			break;
	}

	if (pCur)
	{
		((LMessageList*)&m_QueueB8)->InsertAfter((LThreadMessage*)pAt,
		                                         (LThreadMessage*)pRec);
	}
	else
	{
		((LMessageList*)&m_QueueB8)->Prepend((LThreadMessage*)pRec);
	}
}

// 0x100235a3 — everything still pending for a connection goes away with it.
// A record is also dropped when its own connection has already moved past
// the packet it was waiting on: it will never be answered.
void CNetMgr::RemoveConnection(NetConnection* pConn)
{
	CNetPending* pCur = (CNetPending*)m_QueueC4.m_pTail;
	CNetPending* pRec;

	while (pCur)
	{
		pRec = pCur;
		pCur = ((LThreadMessage*)pCur->m_pPrev == m_QueueC4.m_pTail) ?
		           LTNULL : (CNetPending*)pCur->m_pPrev;

		if (pRec->m_pConnection)
		{
			if (pRec->m_pConnection == pConn ||
			    pRec->m_PacketID < pRec->m_pConnection->m_PacketID)
			{
				ReleaseQueued(pRec);
			}
		}
	}
}


// ---- sending ------------------------------------------------------------- //
//
// Three things this side of the manager needs that servermgr.h does not
// model, all declared as local views and reached by casting, the way
// smgr.cpp reaches IObjectDeactivate.

// The record CNetBank hands out is 0x70 bytes, and the 0x64 after its three
// header dwords are a whole ServerMessage: 0x0c + 0x64 = 0x70 exactly, and
// every offset the send path touches lines up — the record's 0x40, 0x44 and
// 0x60 are the packet's m_Unknown34, m_Unknown38 and m_pData. servermgr.h's
// CNetSentPacket keeps that tail as an opaque byte run (m_Data0C); this is
// the same record with the packet named. What confirms it beyond arithmetic
// is CNetBank::Allocate (0x10024162), which runs ServerMessage's constructor
// (0x1002417f) on exactly this+0xc after the struct bank hands the record
// back.
using CNetSentPacketView = CNetSentPacket;

// servermgr.h has the send path's two signatures wrong — SendPacket and
// SendPacketData both return LTBOOL, not void (see the note on SendPacket) —
// and does not model the manager's send-byte accumulator or the member at
// +0xe0 it feeds. All three go on a view derived from CNetMgr: the base has
// no virtual functions, so deriving changes nothing about the layout or
// about `this`, and the redeclared members simply hide the base's. The
// symbols the byte comparison verifies are then this class's rather than CNetMgr's,
// which costs nothing — the verification is per function on machine code.


// 0x10024162 — one sent-packet record out of the bank, constructed in
// place. The bank hands back raw memory, so the packet at +0x0c gets its
// constructor run explicitly; a failed allocation comes back as LTNULL
// without the constructor. The same shape as LThreadMessageBank::Allocate
// (0x10015b2e), down to the `add $4,%ecx` that reaches the embedded struct
// bank and the `pop %ecx` that cleans up sb_Allocate's one argument.
//
// `inline` in lithtech.exe only. That image has no standalone Allocate:
// SendPacket (0x00457ab0) carries it expanded, sb_Allocate's pop-and-grow
// included (`call 0x00490c20` sb_Grow, then the ServerMessage constructor
// call 0x00457c40), and CNetBank's slot 1 (0x00459c30, startload.cpp)
// carries another copy. server.dll keeps the out-of-line 0x10024162 and
// the `call` to it at 0x10022990, which /Os would not keep if this were
// inline there.
// 0x10023a8c — the post-accept hook: five bytes of `return 0`. Two
// arguments and `ret 8`, and nothing in the body, so the connection and
// the message are both unread. Whatever it was meant to do (a per-packet
// handler notification, on the evidence of where TranslatePacket calls it —
// twice, on both the local and the guaranteed path, each time treating a
// nonzero answer as "the packet has been consumed") was gone by the time
// this shipped.
int CNetMgr::sub_10023a8c(NetConnection* pConn, ServerMessage* pMsg)
{
	return 0;
}


// 0x10022ac2 — the in-order step, run when the packet just received is the
// one the connection was expecting. Everything still queued at or below the
// current id is released, and the id then advances.
//
// `this` is never loaded for the inner call: nothing between the entry and
// the call touches ecx, so VC6 leaves the incoming `this` in place. The
// null test on the connection is the function's own — its caller has
// already dereferenced pConn several times.
void CNetMgr::sub_10022ac2(NetConnection* pConn)
{
	if (!pConn)
		return;

	sub_10023a91(&pConn->m_GuaranteedQueue, pConn->m_PacketID);

	pConn->m_PacketID++;
}


// The rule these two settle, which decides four other loops in this file:
// cl 8447 reproduces the shipped *unrotated* loop — one test block that both
// the entry and the back edge reach — exactly when the back edge is an
// unconditional `jmp`, and rotates the loop (peeling the head test and
// duplicating it at the bottom) when the back edge is conditional. Both of
// these have an unconditional back edge and both match; FindQueuedPacket,
// sub_100233ad and sub_10023c4d end their bodies with a conditional
// break/continue, rotate, and are four-to-six-byte nears for it. No source
// shape moves them (while/continue, `for(;;)` with the test as a `break` or
// an early return, and a literal backward `goto` all give the rotated code),
// and neither does any held compiler revision.
//
// 0x10023a91 — release every record queued out of order whose id has now
// been overtaken. The queue is kept in id order, so the walk stops at the
// first record that belongs after the id handed in.
//
// The cursor-alias walk: the record in hand is taken out of the list before
// the cursor is advanced, so the advance reads the *record's* next pointer
// and re-reads the list's tail to decide whether it has come back round.
// The loop's back edge jumps to the `while` test rather than to the body,
// which is what says the test is shared and not duplicated.
void CNetMgr::sub_10023a91(LMessageList* pList, uint32 nID)
{
	CNetRecvPacket* pCur = (CNetRecvPacket*)pList->m_pTail;
	CNetRecvPacket* pRec;

	while (pCur)
	{
		pRec = pCur;
		pCur = (pCur->m_pPrev == (CNetRecvPacket*)pList->m_pTail) ?
		           LTNULL : static_cast<decltype(pCur)>(pCur->m_pPrev);

		if (pRec->m_ID > nID)
			break;

		pList->Remove((LThreadMessage*)pRec);

		FreeRecvPacket(pRec);
	}
}


// 0x10023ad7 — the same walk with the comparison the other way round: it
// releases everything queued *above* the id and keeps going to the end of
// the list rather than stopping. Because nothing breaks out of it, VC6
// rotates the loop (the test moves to the bottom) and shrink-wraps ebx into
// the body, which is the whole structural difference between the two.
void CNetMgr::sub_10023ad7(LMessageList* pList, uint32 nID)
{
	CNetRecvPacket* pCur = (CNetRecvPacket*)pList->m_pTail;
	CNetRecvPacket* pRec;

	while (pCur)
	{
		pRec = pCur;
		pCur = (pCur->m_pPrev == (CNetRecvPacket*)pList->m_pTail) ?
		           LTNULL : static_cast<decltype(pCur)>(pCur->m_pPrev);

		if (pRec->m_ID > nID)
		{
			pList->Remove((LThreadMessage*)pRec);

			FreeRecvPacket(pRec);
		}
	}
}


// The record the fragment reassembler collects a split packet in (names
// ours, from the offsets the handler touches): four fragment messages, the
// packet id they belong to and the connection they came from. Four, not a
// count — the replay loop is a literal `i = 4`.
// One of the four fragment slots. It is a counted reference, not a bare
// pointer: the reassembler fills a slot through CMessageRef::Init
// (0x1000ebab) and clears one through CMsgCursor::Init (0x10023e86), which
// is two names for the same four-byte record. Declared here with both entry
// points and no destructor — nothing in the image destroys a slot, and a
// destructor VC6 emitted would have no shipped counterpart (names ours).
// The three CPacket member templates servermsg.h does not name on
// ServerMessage. 0x100138d6, 0x1000ebd0 and 0x100240f8 are
// CPacket::ReadType<uint8/uint16/uint32> (packet.cpp matches all three) and
// 0x1001393e, 0x1000ec3d and 0x1000ecd6 are the WriteType trio; the dummy
// pointer that carries the template argument is what every call site pushes
// as a literal zero. Declared here as ordinary members so this file can emit
// the calls without pulling packet.h in (names ours).



// 0x100235e8 — the internal packet handler, run before anything else looks
// at an arrived packet. Only packet id 1 is internal; everything else comes
// straight back out as zero so the caller hands it on. The first payload
// byte then selects one of five things (the names are the ones its own error
// string uses: "Error in SUB_ACK"):
//
//   0 — the acknowledgement. Optionally answers a ping request with a
//       sub-type 2 of its own, releases everything the far end has
//       acknowledged, and resends up to eight NAKed frames.
//   1 — a forced disconnect: the connection goes, the driver is told, and
//       the handler gets event 4 with the reason the packet carried.
//   2 — a ping reply. Only the sequence number the connection is waiting
//       for counts; the round trip goes into a three-deep ring and the ping
//       becomes their average.
//   3 — a group of packets, each length-prefixed, split back out into the
//       connection's receive list.
//   4 — one fragment of a split packet. Once all four have arrived the
//       original is rebuilt in place and the answer is zero, so the caller
//       hands the reassembled packet on.
//
// Everything except the completed reassembly answers one, meaning "handled;
// do not hand this packet out".
//
// Three readings worth writing down.
//
// The ping average is a shipped BUG: the three-sample ring is summed into
// pConn->m_Ping and then divided by the number of non-zero samples, but
// m_Ping is never cleared first — so it accumulates across every ping reply
// for the life of the connection and the number the stats line prints grows
// without bound. There is no store to +0x9c anywhere in the function, and
// AddConnection's reset is the only other one.
//
// The sub-type-4 arm opens with `RemoveConnection(LTNULL)`. The argument
// really is the zero the function has been carrying in edi since the
// prologue — nothing on the path to that arm loads the connection into it —
// so the walk runs with a null connection, which its own body survives but
// which cannot have been what was meant.
//
// And one codegen note: the "Invalid fragment count received!" arm ends with
// `cmp %edi,%ecx` where the shared exit block has `test %ecx,%ecx`. VC6
// cross-jumped that path into the *middle* of the reference's destructor,
// one instruction past its own test, using the zero already in edi to set
// the flags. Nothing in the source asks for that, and it reproduces.
//
// NEAR (1188 bytes against 1188, 406 instructions against 406, the same
// registers, the same frame and the same slot assignments): three bytes, one
// size-neutral schedule. The reassembly's `mov 0x54(%esi),%edx` (the
// destination packet's payload pointer) is hoisted above the two `movw $1`
// stores that the shipped build puts first (0x100239f5). Measured and
// negative: the stores through a CPacket view, the destination through
// `((CPacket*)pMsg)->m_Data.m_pData`, the stores in an inline member (a file-
// scope view class, and a local class, which /Ob1 does not expand), a walking
// CNetFragSlot pointer for the copy and the loop (+2 bytes), and volatile
// stores (this one keeps the load below the stores but moves the `lea` and the
// `add $0x14`). CNetMgr::WriteAckPacket has the same residual.
//
// The ping sum reads the ring through a named `float* pSamples` indexed by
// i. The evidence is 0x10023880 (`flds 0x9c(%esi) ; fadds (%edi)`) in server.dll
// and 0x00459142 (`flds (%ecx) ; fadds 0x9c(%esi)`) in lithtech.exe. The bare
// `m_UnknownA0[i]` gets lithtech right and server.dll commuted. A walking
// `*pSample++` gets server.dll right and lithtech commuted. Only the indexed
// pointer gets both. Using the same pointer for the shift loop above it is
// worse on both images.
//
// Everything else that was off has been closed and is worth recording,
// because each was a real source fact: the two length tests are *unsigned*
// (`(uint32)m_Unknown34 - (uint32)m_Unknown38`, giving `jb`/`jae` where an
// int difference gives `jl`/`jge`); the fragment-count byte is read once
// into a local and masked with `~0x0f`, not `0xf0`, and not read twice; the
// sub-type-3 length and the fragment replay length are both passed to
// ReadData/WriteData as *uint16* (the call sites push the raw dword their
// uint16 locals sit in, with no zero-extension, which a uint32 parameter
// would have forced); the two `movw $1` stores on the fresh ping packet
// share one load of `ref.m_pMsg` and so need a block-local pointer, while
// everything after them re-reads the reference; and `nValid` is assigned
// zero *after* the sample shift, or the zero VC6 is already holding in edi
// gets spilled to a frame slot and reloaded. All four revisions (8168,
// 8447, 8804, 8966) emit the same five bytes.
int CNetMgr::sub_100235e8(NetConnection* pConn, ServerMessage* pMsg)
{
	CMessageRef ref;
	uint32      nAcks[16];
	uint32      nAckID;
	uint32      nCount;
	uint32      i;
	uint8       nSubType;

	if ((pMsg->m_pData[0] & 0x3f) != 1)
		return 0;

    if (!pMsg->CanRead(1)) return 1;
    nSubType = pMsg->ReadByte(0);
    const uint32 remaining = pMsg->m_DataLen - pMsg->m_Pos;
    if (nSubType == 0) {
        if (!remaining) return 1;
        const uint32 minimum = pMsg->m_pData[pMsg->m_Pos] ? 11 : 9;
        if (remaining < minimum || (remaining - minimum) % 4 || remaining - minimum > 32) return 1;
        const auto ack = nolf::wire::load<uint32>(pMsg->m_pData + pMsg->m_Pos + (minimum == 11 ? 3 : 1));
        if (ack != 0xffffffff && static_cast<int32>(ack - pConn->m_nSent) >= 0) return 1;
    } else if (nSubType == 1) {
        if (remaining != 0 && remaining < 4) return 1;
    } else if (nSubType == 2) {
        if (remaining < 2) return 1;
    } else if (nSubType == 3) {
        uint32 cursor = pMsg->m_Pos;
        while (cursor < pMsg->m_DataLen) {
            if (pMsg->m_DataLen - cursor < 2) return 1;
            const auto length = nolf::wire::load<uint16>(pMsg->m_pData + cursor);
            cursor += 2;
            if (!length || length > pMsg->m_DataLen - cursor) return 1;
            cursor += length;
        }
    } else if (nSubType == 4) {
        if (remaining < 5 || (pMsg->m_pData[pMsg->m_Pos] & 0xfc) != 0x30) return 1;
    }

    if (nSubType == 0)
	{
		(pConn)->m_Unknown98 = 0.0f;

		if (pMsg->ReadByte(0))
		{
			uint16       nPingSeq = ((ServerMessage*)pMsg)->ReadWord(0);
			CAbstractIO* pIO;

			ref.Init(msg_AllocPacket(&pIO, 0x1000, 0x1000));
			if (pIO)
				pIO->Release();

			ServerMessage* pNew = ref.m_pMsg;

			pNew->m_Unknown38 = 1;
			pNew->m_Unknown34 = 1;

			ref.m_pMsg->m_pData[0] = 1;

			ref.m_pMsg->WriteByte(2);
			ref.m_pMsg->WriteWord(nPingSeq);

			SendToConnection(ref.m_pMsg, pConn, 0);
		}

		nAckID = ((ServerMessage*)pMsg)->ReadDWord(0);

        // A reordered ACK may describe an older send frontier. It cannot
        // invalidate reliable frames that have already arrived successfully.
        const auto remote_sent = pMsg->ReadDWord(nullptr);
        pConn->m_Unknown80 = std::max(pConn->m_Unknown80,remote_sent);

		nCount = 0;

		while ((uint32)pMsg->m_Unknown34 - (uint32)pMsg->m_Unknown38 >= 4)
		{
			nAcks[nCount] = ((ServerMessage*)pMsg)->ReadDWord(0);

			nCount++;

			if (nCount >= 8)
				break;
		}

		if (nAckID != (uint32)-1)
		{
			sub_10023a91(&pConn->m_SendQueue, nAckID);

			CNetMgr::DebugPrint(pConn, 2, "Got an ack on local %d, remote %d",
				nAckID, (pConn)->m_Unknown80);
		}

		for (i = 0; i < nCount; i++)
		{
			CNetRecvPacket* pRec =
				(CNetRecvPacket*)FindQueuedPacket(&pConn->m_SendQueue, nAcks[i]);

			if (pRec)
			{
				if ((pRec->m_Unknown14 + i) % 5 == 0)
				{
					CNetMgr::DebugPrint(pConn, 1, "Resending frame %d", nAcks[i]);

					sub_10022c2f(pRec->m_pData, pRec->m_Len,
					             pRec->m_nAlloced - pRec->m_Len, pConn);
				}

				pRec->m_Unknown14++;
			}
			else
			{
				DebugPrint(1, "Error in SUB_ACK: missing requested packet %d",
					nAcks[i]);
			}
		}
	}
	else if (nSubType == 1)
	{
		CNetMgr::DebugPrint(pConn, 1, "Forced disconnect!");

		RemoveConnection(pConn);

		pConn->m_pDriver->Disconnect(pConn, 4, 0);

        const uint32 reason = pMsg->CanRead(4) ? pMsg->ReadDWord(nullptr) : 0;
        if (m_pHandler) m_pHandler->_v10(4, g_EmptyString, reason);
	}
	else if (nSubType == 2)
	{
		if (((ServerMessage*)pMsg)->ReadWord(0) ==
		        (pConn)->m_UnknownB8)
		{
			uint32 nValid;

			for (i = 0; i < 2; i++)
				(pConn)->m_UnknownA0[i] =
					(pConn)->m_UnknownA0[i + 1];

			(pConn)->m_UnknownA0[2] =
				pConn->m_Counter.Read() * 0.001f;

			nValid = 0;

			float* pSamples = (pConn)->m_UnknownA0;

			for (i = 0; i < 3; i++)
			{
				(pConn)->m_Ping += pSamples[i];

				if (pSamples[i] > 0.0f)
					nValid++;
			}

			if (nValid)
				(pConn)->m_Ping /= nValid;
		}
	}
	else if (nSubType == 3)
	{
		while ((uint32)pMsg->m_Unknown34 - (uint32)pMsg->m_Unknown38 >= 2)
		{
			uint16 nSubLen = ((ServerMessage*)pMsg)->ReadWord(0);

			if (nSubLen <= pMsg->m_Unknown34 - pMsg->m_Unknown38)
			{
                CNetRecvPacket* pRec = sub_10023c4d(nSubLen);
                if (!pRec) return 1;
                ((ServerMessage*)pMsg)->ReadData(pRec->m_pData, nSubLen);

				pRec->m_Len = nSubLen;

				((LMessageList*)&pConn->m_RecvList)->Prepend((LThreadMessage*)pRec);
			}
			else
			{
				DebugPrint(0, "Invalid PACKETGROUP subpacket length");
			}
		}
	}
	else if (nSubType == 4)
	{
		uint8        nCountByte;
		uint8        nIndex;
		uint32       nFragID;
		CNetFragRec* pRec;

		RemoveConnection(LTNULL);

		nCountByte = pMsg->ReadByte(0);

		if ((nCountByte & ~0x0f) != 0x30)
		{
			CNetMgr::DebugPrint(pConn, 0, "Invalid fragment count received!");

			return 1;
		}

		nIndex = (uint8)(nCountByte & 0x0f);

		// packet.h's ReadType<uint32> spelled directly, like the WriteType of
		// the same id below. lithtech.exe expands both of them: see the
		// REPORT_ERROR pair at 0x004592ec/0x00459314, and the Insert2 call at
		// 0x00459400 inside the expanded write. The other reads and writes in
		// this function stay calls there. server.dll calls both either way.
		// lithtech NEAR, 1625 of 1622 bytes (it was 1374). Shipped declines
		// the Append inside the sub-type-3 Prepend (0x00459227 ->
		// 0x0045a1c0), and ours still expands it. The /O2 budget runs in
		// source order: a declared-only Append view frees budget, and the
		// Insert2 below then expands (1816). Writing the ping arm's header
		// through an inline helper expands its WriteType (1802). Spelling
		// the count byte as ReadType expands it (1723). So shipped spent more
		// before sub-type 3 than any spelling here does. The rest of the
		// diff is register allocation that follows from this.
		nFragID = ((CPacket*)pMsg)->ReadType((uint32*)0);

		pRec = sub_100233ad(nFragID, pConn);

		if (!pRec)
		{
			pRec = sub_10023495();

#ifdef NOLFSRC_MODERN
			// No record: the fragment is dropped, like any packet the
			// reassembly cannot take.
			if (!pRec)
				return 1;

#endif
			pRec->m_ID    = nFragID;
			pRec->m_pConn = pConn;
		}

		sub_100233e3(pMsg, nIndex, pRec);

		if (sub_10023474(pRec))
		{
            uint32 total = 5;
            for (const auto& part : pRec->m_pFrags) total += part.m_pMsg->m_DataLen - 1;
            if (total > 0xffff || !pMsg->Reserve(total)) { ReleaseQueued(pRec); return 1; }
            pMsg->m_MaxDataLen = static_cast<uint16>(std::max<uint32>(pMsg->m_MaxDataLen,total));
            CNetMgr::DebugPrint(pConn, 2, "Completed fragmented packet %d", pRec->m_ID);

			pMsg->m_Unknown38 = 1;
			pMsg->m_Unknown34 = 1;

			pMsg->m_pData[0] = pRec->m_pFrags[0].m_pMsg->m_pData[0];

			for (i = 0; i < 4; i++)
				((ServerMessage*)pMsg)->WriteData(
					pRec->m_pFrags[i].m_pMsg->m_pData + 1,
					pRec->m_pFrags[i].m_pMsg->m_Unknown34 - 1);

			((CPacket*)pMsg)->WriteType(nFragID);

			pMsg->m_Unknown38 = 1;

			ReleaseQueued((CNetPending*)pRec);

			return 0;
		}
	}
	else
	{
		CNetMgr::DebugPrint(pConn, 1, "Got an invalid internal packet!");
	}

	return 1;
}


// 0x10022a7b — the send-side counterpart of CNetMgr::AddRecvBytes, with one
// extra term: the manager's own rate tracker at +0xe0 gains the byte count
// too, but only for a remote connection (the connection's m_Flags bit 0 is
// the local flag).
//
// The byte count is converted once and used twice — `fildll` of the dead
// dword pair the frame reserves, then `fld %st(0)` to duplicate it — which
// is what says the two `+= nBytes` are two expressions over one conversion
// and not two conversions. `this` survives in ecx across the whole function
// because the byte count is loaded into edx rather than ecx: the two
// `push %ecx` in the prologue are the /O1 stack reservation for the
// conversion's eight bytes, not arguments.
//
// The packets-per-second add goes through a named pointer to its tracker.
// Written as a bare `pConn->m_SendPPS.m_Value += 1.0f`, VC6 hoists the
// `testb $0x1,0x70(%eax)` above that store on both images. The shipped code
// has it after the `fildll` (0x10022aa0; /O2 0x457deb, where ours also
// loaded the flags into %dl). Through the pointer, VC6 cannot move the test
// above the store, and both images come out byte-identical. An inline
// tracker-add helper used on the PPS line gives the same result. Earlier
// negatives, none of which moved the test: an early `return`; an if/else with
// the bytes add in both arms (78 bytes); a `float` local for the conversion;
// all four compiler revisions; `(flags&1)==0`; `PPS = PPS + 1.0f`; the flag
// read through a byte cast or a view class.
void CNetMgr::AddSentBytes(NetConnection* pConn, uint32 nBytes)
{
	if (!pConn)
		return;

	CRateTracker* pPPS = &pConn->m_SendPPS;
	pPPS->m_Value += 1.0f;
	pConn->m_SendBPS.m_Value += nBytes;

	if (!(pConn->m_Flags & 1))
		m_RateE0.m_Value += nBytes;
}


// 0x1003e280 is the CRT's memset (lithtech/ccobj.cpp reads its return
// value correctly: the destination), a call in server.dll because /O1
// carries no /Oi, and `rep stos` in lithtech.exe's /O2.

// 0x10051c8c — how many packets the guaranteed queue will hold before the
// insert starts dropping them. A plain initialised global (32), signed, with
// no console binding (name ours).
extern int g_NetMaxQueued;


// 0x10023c4d — a receive record with room for nLen bytes, off the manager's
// free list at +0xb8 if one there is big enough and freshly allocated
// otherwise.
//
// Two extra bytes are always reserved (`inc %esi` twice, which is a byte
// shorter than `add $2`), and the length the record records is the reserved
// one, so a record's usable capacity is always two more than whatever asked
// for it. The comparison is signed on both sides — the record's own capacity
// field is an `int` — which is why the length arrives as one.
//
// The record and its payload are two separate allocations, both through
// `::operator new` (0x1000d25d, the forwarder in front of dalloc), and the record is
// cleared with memset rather than a constructor: the three
// calls' cleanups all batch into one `add $0x14,%esp`, so all three have
// fixed prototypes.
//
// The cursor re-reads the member instead of copying pTail, as sub_100233ad
// and tcp_FindConnection do. With `pCur = pTail`, VC6 tested the tail
// register, peeled that test off the loop and copied it to the back edge
// (117 bytes). The shipped code copies first and tests the copy
// (`mov %ecx,%eax ; test %eax,%eax`, 0x10023c5e). That test is the one both
// the entry and the `jl` back edge reach (0x10023c76), and the function is
// byte-identical. The earlier verdict here ("rotation, not reachable", with
// while/continue, for(;;), goto and four compiler revisions as negatives) was
// wrong. The `if (... < nLen) continue;` spelling also matches once the member
// is re-read. The lithtech twin (0x4597a0, 179 bytes) is byte-identical as
// well. It clears the record with an inline `rep stos` (0x4597e7): the same
// memset, expanded by /O2's /Oi, where server.dll's /O1 calls 0x1003e280.
CNetRecvPacket* CNetMgr::sub_10023c4d(int nLen)
{
	if (nLen < 0 || nLen > 0xffff) return nullptr;
	CNetRecvPacketSendView* pRec;
	LThreadMessage*         pTail = m_QueueB8.m_pTail;
	LThreadMessage*         pCur  = m_QueueB8.m_pTail;

	nLen += 2;

	while (pCur)
	{
		pRec = (CNetRecvPacketSendView*)pCur;
		pCur = (pCur->m_pPrev == pTail) ? LTNULL : pCur->m_pPrev;

		if (pRec->m_nAlloced >= nLen)
		{
			((LMessageList*)&m_QueueB8)->Remove((LThreadMessage*)pRec);

			pRec->m_Unknown14 = 0;
			pRec->m_bPending  = 0;

			return pRec;
		}
	}

	pRec = new (std::nothrow) CNetRecvPacketSendView;
	if (!pRec) return nullptr;

	pRec->m_pData = static_cast<uint8*>(::operator new(nLen, std::nothrow));
	if (!pRec->m_pData) { delete pRec; return nullptr; }
	pRec->m_nAlloced = nLen;

	m_UnknownD8 += nLen;

	return pRec;
}


// 0x10023b1f — put a received guaranteed packet into the connection's
// out-of-order queue, in packet-id order. LTFALSE means the queue was full
// and the record was not taken.
//
// The walk runs from the tail with the usual cursor alias and stops at the
// first record whose id is above the new one, splicing in front of it; past
// the end of the queue the record goes on the front. The overflow test is
// written at both places — once at the head of the loop and once after it —
// but the report exists once: the in-loop test `break`s and the post-loop
// test carries the only DebugPrint, which is why the block sits between the
// post-loop test and the append. Writing the report (and its return) in both
// places is the same at /O1 but gives lithtech.exe (/O2) two copies of the
// tail (249 bytes against 219); the `break` spelling matches both images.
//
// The splice is LMessageList::InsertAfter expanded inline, the tail fix-up
// included; the append is a real call to Prepend. lthread.h has both, and
// this is the caller its note about the inline copy refers to.
//
// NEAR (150 bytes against 150, sixty-two instructions against sixty-two,
// the same frame and the same slots): two independent prologue stores are
// in the other order — the shipped build spills `this` to -0x8 before
// zeroing nCount at -0x4, ours zeroes first — and esi/edi are swapped
// throughout (ours holds the tail in esi and the cursor in edi, the shipped
// build the other way round). Nothing else differs. `nCount` has to be the
// first declaration with an initialiser or it lands in the dead pList
// parameter slot instead of a frame slot of its own, which was worth a byte
// and the whole slot layout; past that, all four revisions (8168, 8447,
// 8804, 8966) emit our order. Callee-saved rotation plus a store-order
// tie-break.
LTBOOL CNetMgr::sub_10023b1f(LMessageList* pList, CNetRecvPacket* pRec)
{
	int             nCount;
	LThreadMessage* pTail;
	LThreadMessage* pCur;

	nCount = 0;
	pTail  = pList->m_pTail;
	// RE-READ THE MEMBER, do not copy pTail. Written `pCur = pTail;`
	// VC6 tests the tail and sinks the copy; the shipped bytes copy first
	// and test the copy. Proven in-tree by tcpdriver.cpp's tcp_FindConnection,
	// which reads the member twice and is byte-identical.
	pCur   = pList->m_pTail;

	while (pCur)
	{
		LThreadMessage* pAt;

		if (nCount >= g_NetMaxQueued)
			break;

		pAt  = pCur;
		pCur = (pCur->m_pPrev == pTail) ? LTNULL : pCur->m_pPrev;

		nCount++;

		if (pRec->m_ID < ((CNetRecvPacket*)pAt)->m_ID)
		{
			pList->InsertAfter(pAt, (LThreadMessage*)pRec);

			return LTTRUE;
		}
	}

	if (nCount >= g_NetMaxQueued)
	{
		DebugPrint(1, "Dropped guaranteed packet due to queue overflow");

		return LTFALSE;
	}

	pList->Prepend((LThreadMessage*)pRec);

	return LTTRUE;
}


// 0x1002118b — the two-byte checksum the remote packets carry in their last
// two bytes; cdecl, and both the return and the length are uint16. The
// length being uint16 is what the call site proves: the shipped code loads
// the packet length with a 16-bit `mov 0x34(%edi),%cx`, subtracts two from
// the *whole* of ecx and pushes ecx, so the high half of the pushed dword is
// whatever was in the register before. A uint32 parameter would have forced a
// `movzwl` first. The callee truncates it straight back (`mov %dx,%cx`), so
// nothing is wrong — but it does mean the argument only ever carried sixteen
// meaningful bits. Name ours.
uint16 net_Checksum(uint8* pData, uint16 nLen)
{
	uint16 nSum   = 0;
	uint16 nWords = nLen >> 1;
	uint16 nRem   = nLen - nWords * 2;

	// Every intermediate is sixteen bits wide, and the bytes say so twice
	// over: the sum is accumulated with 32-bit `add` instructions over
	// registers whose upper halves are never cleaned (`mov (%edi),%bx`
	// leaves garbage above bx), and the odd trailing byte is sign-extended
	// to a *word* (`movsbw`) rather than to a dword. A uint32 accumulator
	// would force a `movzwl` at every load and a `movsbl` at the last one.
	//
	// Both loops are the post-decrement form: `i = n; while (i--)` gives the
	// `add $0xffff` / `movzwl` / `inc` trip-count arithmetic and the
	// zero-guard that goes with it, where a `for` collapses to one `movzwl`
	// of the limit. The -1 is materialised into esi once and
	// shared by both.
	while (nWords--)
	{
		nSum += nolf::wire::load<uint16>(pData);

		pData += 2;
	}

	while (nRem--)
	{
		nSum += static_cast<int8>(*pData);

		pData++;
	}

	return nSum;
}

// g_NetDropRate is 0x100584bc, registered under the console name "DropRate"
// (the variable table at 0x10052368 pairs the literal with this address). It
// is a percentage: a packet is dropped when a fresh 0..100 random draw comes
// out under it.
extern float g_NetDropRate;

// g_NetResendTime is 0x100584b8, and the console name it is registered
// under is "LatencySim" — the variable table at 0x10052354 pairs that
// literal with this address. Above a millisecond the send path stops
// sending and starts queueing, which is what the name describes.


// 0x10022ade — take a new connection into the manager's list.
//
// Everything about the connection's timing and sequencing state is reset
// first, then the connection goes on the end of the list and the handler is
// asked whether it wants it; a refusal takes the entry straight back off
// again. The two lists' counts are read differently at the two calls — the
// insert reads it through the list pointer, the remove through `this` — and
// that asymmetry is in the bytes, not a slip.
//
// The list pointer is a local — one `lea` serves both the insert and the
// remove — but it has to be *assigned* just before the insert rather than
// initialised at its declaration: initialised, VC6 hoists the `lea` to the
// top of the function, which rotates ebx and edi and costs nothing in size
// but every byte of the two calls' register operands.
//
// Every one of the fourteen resets reloads `pConn` from its parameter slot,
// because `Insert2` takes its element by const reference: the parameter is
// address-taken, so the compiler has to assume each store could alias it.
// That is also why the three-float run at +0xa0 stays a loop — VC6 strength-reduces the index to
// a byte offset that starts at 0xa0 and stops at 0xac — instead of being
// unrolled into three stores like the rest.
//
// lithtech.exe 0x00457e40 is BYTE-IDENTICAL through the CConnListO2 view
// below. Shipped CALLS Insert2 out of line (0x0045a240, `push $g_DefAlloc;
// push &pConn; push m_nElements`) -- which is also why every reset reloads
// pConn from its slot -- and then expands Remove2 completely, BaseNew and
// the release included; through the real template /O2 expanded both, each
// with BaseNew and BaseDelete left as calls (581 bytes). The view declares
// Insert2 without a body and restates Remove2 with collection.h's own
// helper levels (_AllocateTArray, _DeleteAndDestroyArray, the DefaultCache
// accessors -- without the helpers the dead SetCacheSize(0) store before
// the reseat is eliminated, 474 bytes).
#ifndef LT_SERVER_BUILD
// STAND-IN (lithtech.exe only): CConnList seen through a same-layout view
// whose Insert2 is DECLARED ONLY (so it stays a call) and whose Remove2 is
// an in-class copy of collection.h's, helper for helper.  Shipped
// 0x00457e40 CALLS Insert2 out of line (0x00457ef0 -> 0x0045a240) and
// expands Remove2 down to the allocator's own virtuals (0x00457f4c
// `call *0x4(%edx)`, 0x00457fc5 `call *0x8(%edx)`); through the real
// template /O2 does the opposite at depth 3.  server.dll keeps the template.
class CConnListO2
{
public:
	LTBOOL Insert2(uint32 index, NetConnection* const& toInsert, LAlloc* pAlloc);

	inline uint32 GetNumAllocatedElements() {return m_nElements + m_Cache.GetCacheSize();}

	NetConnection** _AllocateTArray(uint32 nElements, LAlloc* pAlloc)
	{
		NetConnection** tPtr = LNew_Array(pAlloc, NetConnection*, nElements);

		return tPtr;
	}

	void _DeleteAndDestroyArray(LAlloc* pAlloc, uint32 nElements)
	{
		if (m_pArray)
		{
			LDelete_Array(pAlloc, m_pArray, nElements);
			m_pArray = 0;
		}

		m_Cache.SetCacheSize(0);
	}

	void Remove2(uint32 index, LAlloc* pAlloc)
	{
		uint32          i, newSize, newAllocSize;
		NetConnection** pNewArray;
		LTBOOL          bSlideDown;

		bSlideDown = LTTRUE;
		if (m_Cache.GetCacheSize() >= (m_Cache.GetWantedCache() * 2))
		{
			newSize = m_nElements - 1;
			newAllocSize = newSize + m_Cache.GetWantedCache();
			pNewArray = _AllocateTArray(newAllocSize, pAlloc);

			if (pNewArray || newAllocSize == 0)
			{
				for (i = 0; i < index; i++)
					pNewArray[i] = m_pArray[i];

				for (i = index; i < m_nElements - 1; i++)
					pNewArray[i] = m_pArray[i + 1];

				_DeleteAndDestroyArray(pAlloc, GetNumAllocatedElements());
				m_pArray = pNewArray;

				m_Cache.SetCacheSize(m_Cache.GetWantedCache());
				bSlideDown = LTFALSE;
			}
		}

		if (bSlideDown)
		{
			m_Cache.SetCacheSize(m_Cache.GetCacheSize() + 1);

			for (i = index; i < m_nElements - 1; i++)
				m_pArray[i] = m_pArray[i + 1];
		}

		--m_nElements;
	}

	void*           m_pVtbl;                 // +0x00
	NetConnection** m_pArray;                // +0x04
	uint32          m_nElements;             // +0x08
	DefaultCache    m_Cache;                 // +0x0c
};
#endif

LTBOOL CNetMgr::AddConnection(NetConnection* pConn)
{
	uint32     i;

	con_Printf("NewConnectionNotify\n");

	if (!m_pHandler || !pConn || m_Connections.FindElement(pConn) != BAD_INDEX)
		return LTFALSE;
	const auto lifetime = pConn->m_Lifetime;

	(pConn)->m_Unknown80 = 0;
	(pConn)->m_Unknown7C = 0;
	(pConn)->m_nSent = 0;
	(pConn)->m_SendWait = 0.0f;
	(pConn)->m_RecvWait = 0.0f;
	(pConn)->m_Unknown6C = 0;
	(pConn)->m_UnknownB4 = 0.0f;
	(pConn)->m_Ping = 0.0f;

	for (i = 0; i < 3; i++)
		(pConn)->m_UnknownA0[i] = 0.0f;

	(pConn)->m_Unknown98 = 0.0f;
	(pConn)->m_AckTimer = 0.0f;
	(pConn)->m_nResent = 0;

    if (!m_Connections.Insert2(m_Connections.GetSize(),pConn,&g_DefAlloc)) return LTFALSE;

	pConn->m_Admitting = true;
    const auto accepted = m_pHandler->_v04(pConn,pConn->m_Flags & 1);
    if (!*lifetime) return LTFALSE;
    pConn->m_Admitting = false;
    const auto index = m_Connections.FindElement(pConn);
    if (index == BAD_INDEX) return LTFALSE;
    if (accepted) return LTTRUE;
    m_Connections.Remove2(index,&g_DefAlloc);

	return LTFALSE;
}


// 0x100219db — build the acknowledgement/NAK packet for one connection.
//
// The message is rewound to a single byte, its type byte is poked in
// directly (0x41), and then everything the far end needs is appended: a ping
// request when the connection's countdown at +0xb4 has gone negative, the
// last packet id received (or -1 if none has been), the sent count, and up to
// eight NAKs.
//
// The NAKs come from two places, which is why the byte cap is tested in three
// of them. The guaranteed queue holds the packets that arrived out of order,
// in id order, so every gap between the id expected next and the id of the
// record in hand is a missing frame; then, past the end of the queue,
// everything from there up to +0x80 is missing too. `nCount` caps the whole
// lot at eight, and the shipped code re-tests it once more between the two
// loops on a path where it has just been tested — the redundant compare at
// 0x10021af5 is the second loop's own entry guard, unreachable-but-emitted
// because the queue-empty path jumps straight past it into the loop.
//
// Both parameter slots are reused: the connection's holds the queue cursor
// and the message's holds first the `rand()` result and then `nCount`, since
// both parameters are cached in registers on the first line. `this` goes to
// a real frame slot because the varargs DebugPrint needs it on the stack and
// every other call here clobbers ecx.
//
// The ping timer is reset to `(float(rand()) / float(RAND_MAX)) * 3.0f`, and the
// parentheses are load-bearing: written flat as `rand() / 32767.0f * 3.0f`,
// VC6 turns the division into a reciprocal multiply and then folds it with
// the 3.0f into one constant, and the function comes out six bytes short with
// a single `fmuls`. Parenthesised, the two `fmuls` the shipped code has
// survive — a division does not reassociate out of its own parentheses.
//
// Byte-identical. The header byte is written through `uint8** ppData =
// &pMsg->m_pData`. With the field's address taken, VC6 can no longer move
// the m_pData load (and the `push %edi` that goes with it) above the two
// `movw $1` stores, and the shipped order at 0x100219e8-f7 follows. Spelled
// `pMsg->m_pData[0]`, the load moves up. Earlier attempts that left it
// there: a chained store, `*pMsg->m_pData`, a scoped `uint8*` local, a
// CPacket view, a static inline helper, a copied pMsg pointer, and cl
// 8168/8966. A `volatile` access fixed the load but let the push float.
//
// Two things did move, and both are worth keeping. The initialisation order
// of `nID` and `nCount` decides whether VC6 materialises a shared zero
// register: with `nCount = 0` written *first* it keeps a zero in edi for the
// whole prologue, spends it on the two `WriteByte(0)` pushes and the
// packet-id test, and the function is a byte short; with `nID` first the zero
// never appears and every use is an immediate, which is what ships. And the
// second NAK loop needs its `if (nCount >= 8) break;` written out even though
// the `if (nCount < 8)` above it says the same thing — without it VC6
// shares the entry test with the back edge and drops six bytes.
//
// lithtech.exe 0x00455c50 is byte-identical too. Two source facts close it.
// (1) The last three writes (the sent count and the two NAK writes) are
// packet.h's CPacket::WriteType<uint32> spelled directly. /O2 expands those
// three with Insert2 left as a call (0x00455d3b, 0x00455e2a, 0x00455f25). It
// CALLS the first five, through the ServerMessage/view members (0x004159f0,
// 0x0042fa50, 0x004303c0). Spelled WriteType, all eight are candidates and
// /O2 spends its budget on the first seven (1512 bytes). server.dll calls
// every one, so both spellings reach the same COMDATs there.
// (2) The second loop is `if (nCount < 8) { while (nID < m_Unknown80) ... }`.
// With the old `while (nCount < 8) { if (nID >= ...) break; ... }`, /O2
// jumped back to the top test. Shipped rotates the loop and re-tests
// m_Unknown80 at the bottom (0x00455fb2), and this spelling does the same.
// server.dll is identical either way.
void CNetMgr::WriteAckPacket(NetConnection* pConn, ServerMessage* pMsg)
{
	CNetRecvPacket* pCur;
	CNetRecvPacket* pRec;
	uint32          nID;
	uint32          nCount;

	pMsg->m_Unknown38 = 1;
	pMsg->m_Unknown34 = 1;
	uint8** ppData = &pMsg->m_pData;
	(*ppData)[0] = 0x41;

	pMsg->WriteByte(0);

	if ((pConn)->m_UnknownB4 < 0.0f)
	{
		(pConn)->m_UnknownB8++;

		pConn->m_Counter.Start();

		(pConn)->m_UnknownB4 =
			(float(rand()) / float(RAND_MAX)) * 3.0f;

		pMsg->WriteByte(1);
		pMsg->WriteWord((pConn)->m_UnknownB8);
	}
	else
	{
		pMsg->WriteByte(0);
	}

	((ServerMessage*)pMsg)->WriteDWord(
		pConn->m_PacketID ? pConn->m_PacketID - 1 : (uint32)-1);

	((CPacket*)pMsg)->WriteType(
		(uint32)(pConn)->m_nSent);

	pCur = (CNetRecvPacket*)pConn->m_GuaranteedQueue.m_pTail;
	nID = pConn->m_PacketID;
	nCount = 0;

	while (pCur)
	{
		pRec = pCur;
		pCur = (pCur->m_pPrev == (CNetRecvPacket*)pConn->m_GuaranteedQueue.m_pTail) ?
		           LTNULL : static_cast<decltype(pCur)>(pCur->m_pPrev);

		if (pRec)
		{
			while (nID < pRec->m_ID && nCount < 8)
			{
				((CPacket*)pMsg)->WriteType(nID);

				this->DebugPrint(1,
					"NAK sent on packet frame %d", nID);

				nID++;
				nCount++;
			}

			nID++;
		}

		if (nCount >= 8)
			break;
	}

	if (nCount < 8)
	{
		while (nID < (pConn)->m_Unknown80)
		{
			((CPacket*)pMsg)->WriteType(nID);

			this->DebugPrint(1,
				"NAK sent on packet frame %d", nID);

			nCount++;

			if (nCount >= 8)
				break;

			nID++;
		}
	}

	pConn->m_AckTimer = 0.0f;
}


// 0x10022d6c — the send itself. servermgr.h has it as `void`; it is not —
// every exit leaves a value in esi that the shared epilogue moves to eax, so
// it is LTBOOL like SendPacket above.
//
// A guaranteed packet has its length and write position saved off and the
// sequence number appended, and the saved pair is put back on the way out —
// which is why the restore appears at both exits: the two returns hand back
// different values, so there is no shared tail to hang one copy on.
//
// The two message references are scoped (servermsg.h's CMessageRef), so their
// Releases are the destructors and appear at both exits for free.
//
// NEAR (766 of 787 bytes, 273 of 277 instructions). Every block of the
// mnemonic diff is one fault: the shipped build keeps the saved-message
// pointer (`save.m_pMsg`) in ebx across the whole function and reloads `this`
// from its spill slot at each of the six member calls, where ours keeps
// `this` in ebx and leaves the saved pointer in memory. That is worth exactly
// the twenty-one bytes: `mov -0x4(%ebp),%ecx` against `mov %ebx,%ecx` six
// times, `push -0x4(%ebp)` against `push %ebx` twice, and one
// `mov -0x4(%ebp),%eax; testb $0x1,(%eax)` against `testb $0x1,(%ebx)`. It
// also cascades: with ebx taken, the shipped build has to park the queued
// record in the dead `pMsg` parameter slot and reload it after every call,
// and its `push $1; pop %esi` lands in esi where ours lands in eax.
//
// Probed and negative: the three saved values as three plain locals with the
// restore written out at both exits (byte-for-byte the same as the destructor
// form above, which is why the destructor is kept — the contiguous
// -0x18/-0x14/-0x10 run is what argues for it); the queued record merged into
// the loop's own record variable; `save` declared before the two message
// references and after them; the pointer assigned before the two lengths
// rather than after; and cl 8168 and 8966 as well as 8447. All 766.
//
// Three readings worth keeping. The local-connection test is written twice —
// once as the last conjunct of the piggyback guard and once at the head of
// the send choice — and VC6 propagates the first one's failure straight into
// the second, which is why one `testb $0x1,0x70(%edi)` jumps all the way to
// the driver send. `pMsg` is a reassigned parameter: the two spill paths
// replace it with the fresh message they made, and the register that holds it
// is later reused for the return value. And the record's pending flag and the
// `return LTTRUE` beside it share one materialised `1`.
// 0x10022c2f — the local-connection send, and the fragmenter behind it.
// Anything under 0x555 bytes goes straight out through the driver's slot 0x60
// and this function is a forward. Anything larger is split into at most four
// pieces and each piece sent as its own packet (name ours).
//
// Read off the bytes, and two of these are the reason the function is worth
// having: the ORIGINAL packet id is the last four bytes of the payload, not a
// field -- `mov -0x4(%ebx,%esi,1),%eax` before nLen is reduced by four -- so
// the id trails the data on the wire and the fragmenter has to lift it out
// and re-send it with every piece. And the piece size is `(nLen >> 2) + 1`
// computed ONCE before the loop, which is what makes four pieces enough: the
// clamp to nLen only ever shortens the last one.
//
// Each fragment reuses one message: m_Unknown38 and m_Unknown34 are both
// rewound to 1 (past the header byte m_pData[0] = 1 written before the loop),
// then the sub-id 4, the fragment number ORed with 0x30, the original packet
// id and the slice. The driver gets the message's own buffer with its spare
// computed as m_DataSize - m_Unknown34, exactly as SendPacketData does.
//
// The nEnd == nOffset arm cannot fire on the first pass (the piece size is at
// least 1) and reports through the connectionless DebugPrint, which is what
// says the failure is about the message rather than the peer.
//
// nFrag is UNSIGNED, and both images say so: the /O1 back edge is
// `cmpl $0x4,-0x4(%ebp) ; jb` (0x10022d32) and the /O2 one is
// `cmp $0x4,%eax ; jb` (0x004582a4). An `int` gives `jge`. Size-neutral in
// both images, so it is a correctness fix, not a size one.
//
// Byte-identical on both images. Four source facts decide it, and each was
// read off a specific site:
//
//  * `pOut` and `nRoom` are named locals, assigned just before the driver
//    send, m_DataSize FIRST (0x10022d09 loads 0x58 before 0x54 and 0x34;
//    0x00458285 likewise). Without the named pointer local the /O1 frame
//    has no -0x10 slot for m_pData, VC6 keeps the loop's end offset in ebx
//    instead of the payload base, and the message is homed in nSpare's slot
//    rather than pData's -- the whole thirteen-block residual this function
//    used to carry. With it, the base goes to ebx (0x10022c5d), the message
//    to +0x8 and nEnd to +0x10 exactly as shipped.
//  * A `for` loop, not a do-while: the shipped back edge is `jb` to the head
//    with a `jmp` past it to the exit (0x10022d36/0x10022d3c; 0x004582ab).
//  * The clamp is the ternary `nEnd = (nOffset + nPiece < nLen) ? ... : nLen`
//    (0x00458229: nLen in ebx, the sum compared against it and moved over).
//  * `nOffset = 0` is written above nPiece (the `xor %edi,%edi` at
//    0x10022c92 precedes the stack cleanup) and `m_pData[0] = 1` above
//    bResult, which is what keeps /O2 from reusing bResult's 1 in cl for the
//    byte store (`movb $0x1,(%edx)` at 0x00458207).
LTBOOL CNetMgr::sub_10022c2f(void* pData, uint32 nLen, uint32 nSpare,
                                     NetConnection* pConn)
{
	if (!pData || !nLen || nLen > 65535 || !pConn || !pConn->m_pDriver) return LTFALSE;
	ServerMessage* pMsg;
	uint8*         pBytes;
	uint32         nPacketID;
	uint32         nOffset;
	uint32         nEnd;
	uint32         nPiece;
	uint32         nFrag;
	LTBOOL         bResult;
	uint8*         pOut;
	uint32         nRoom;

	if (nLen < 0x555)
	{
		return pConn->m_pDriver->SendData(
			pData, nLen, nSpare, pConn);
	}

	pBytes    = (uint8*)pData;
	nPacketID = nolf::wire::load<uint32>(pBytes + nLen - 4);
	nLen     -= 4;

	CNetMgr::DebugPrint(pConn, 2, "Fragmenting packet %d", nPacketID);

	nOffset = 0;
	nPiece  = (nLen >> 2) + 1;
	pMsg = nullptr;
	CAbstractIO* allocation = nullptr;
	msg_AllocPacket(&allocation, nPiece + 7, 0x1000);
	pMsg = static_cast<ServerMessage*>(allocation);
	if (!pMsg) return LTFALSE;
	pMsg->m_pData[0] = 1;
	bResult = LTTRUE;

	for (nFrag = 0; nFrag < 4; nFrag++)
	{
		nEnd = (nOffset + nPiece < nLen) ? nOffset + nPiece : nLen;

		if (nEnd == nOffset)
		{
			DebugPrint(0, "Unable to complete fragment upload!");
			break;
		}

		{
			// A block-local copy, and it is worth three bytes and one
			// residual block: the shipped code has `mov 0x8(%ebp),%eax`
			// once and both stores off it, where writing pMsg-> twice
			// reloads between them -- the slot is address-exposed
			// (&pMsg went to msg_AllocPacket), so a member expression
			// cannot be kept. Scoped to these two statements only,
			// because the shipped code reloads again immediately after
			// for WriteByte's `this`.
			ServerMessage* pM = pMsg;

			pM->m_Unknown38 = 1;
			pM->m_Unknown34 = 1;
		}

		pMsg->WriteByte(4);
		pMsg->WriteByte((uint8)(nFrag | 0x30));
		((ServerMessage*)pMsg)->WriteDWord(nPacketID);
		if (!pMsg->WriteData(pBytes + nOffset, nEnd - nOffset)) { bResult = LTFALSE; break; }

		nRoom = pMsg->m_DataSize;
		pOut  = pMsg->m_pData;

		if (!pConn->m_pDriver->SendData(
			pOut, pMsg->m_Unknown34, nRoom - pMsg->m_Unknown34, pConn))
		{
			bResult = LTFALSE;
			break;
		}

		nOffset = nEnd;
	}

	if (pMsg)
		pMsg->Release();

	return bResult;
}


// The two sends at the bottom each RETURN their result: both images carry
// the CMsgLenSave restore and both CMessageRef releases twice, once behind
// each send (server.dll 0x10023029 `jmp` into a shared tail only because /O1
// merges it; lithtech.exe 0x0045865c / 0x004586b4 two full epilogues). A
// `bSent = ...; ... return bSent;` gives one copy (lithtech 971 bytes against
// 1026). The save's stores are length, message, position: server.dll stores
// the message (-0x18) between the other two (0x10022da7).
//
// The two sends take the buffer and the size through the named locals pOut
// and nRoom, m_DataSize FIRST -- sub_10022c2f's own spelling of the same
// send (0x10023014/0x1002302b load 0x58 before 0x54 and 0x34, and the
// driver path keeps m_pData in a frame slot across the vtable load).
// Spelled inline it was 781 of 787 with three blocks.
//
// NEAR on both images. lithtech.exe 0x004582f0 (1045 of 1026, 12 blocks;
// 17 before the locals): shipped declines the whole Prepend splice at
// 0x004583f9 (`call 0x0045a210` InsertBefore, `call 0x0045a1c0` Append)
// where ours expands both -- the /O2 budget again, see lthread.h's
// Append. server.dll 0x10022d6c: 787 of 787, 277 instructions, ONE
// residual: shipped homes pOut in the dead pMsg slot (+0x8, which pCur and
// pIO also share) and ours in pConn's (+0xc, nLen's); 0x10023037 and
// 0x10023042. Not moved by the declaration order of any of the locals
// (pOut first, last, beside pCur, nLen at function scope), by pOut/nRoom
// block-scoped in either arm, by assigning pOut before nRoom, or by pIO at
// function scope (worse: 14 instructions).
LTBOOL CNetMgr::SendPacketData(ServerMessage* pMsg, NetConnection* pConn)
{
	CMessageRef             refSpill;
	CMessageRef             refAck;
	CMsgLenSave             save;
	CNetRecvPacketSendView* pRec;
	CNetRecvPacketSendView* pCur;
	uint8*                  pOut;
	uint32                  nRoom;
	LTBOOL                  bSent;   // unused since the sends return directly; deleting it costs lithtech one block

	if (pMsg->m_pData[0] & 0x80)
	{
		save.m_nLen = pMsg->m_Unknown34;
		save.m_pMsg = pMsg;
		save.m_nPos = pMsg->m_Unknown38;

		((ServerMessage*)pMsg)->WriteDWord(
			(pConn)->m_nSent);
	}

	if (pMsg->m_MsgFlags & 6)
	{
		if (pMsg->m_MsgFlags & 2)
			dsi_ServerError("*** Packet read overflow, disconnecting ***");
		else
			dsi_ServerError("*** Packet write overflow, disconnecting ***");

		pConn->m_Flags |= 4;
		pMsg->m_MsgFlags &= ~6;

		return LTFALSE;
	}

	pRec = LTNULL;

	if (pMsg->m_pData[0] & 0x80)
	{
		if (!(pConn->m_Flags & 1))
		{
			pRec = (CNetRecvPacketSendView*)sub_10023c4d(pMsg->m_Unknown34);

			if (!pRec) return LTFALSE;
			memcpy(pRec->m_pData, pMsg->m_pData, pMsg->m_Unknown34);

			pRec->m_Len = pMsg->m_Unknown34;
			pRec->m_ID = (pConn)->m_nSent;

			((LMessageList*)&pConn->m_SendQueue)->Prepend((LThreadMessage*)pRec);
		}

		(pConn)->m_nSent++;
	}

	if (((pMsg->m_MsgFlags & 8) || pConn->m_Admitting) && pRec)
	{
		pRec->m_bPending = 1;

		return LTTRUE;
	}
	{
		CNetMgr::DebugPrint(pConn, 5, "Sent packet %d to connection %p (packet ID %d (%d), length %d).",
			(pConn)->m_nSent, pConn,
			pMsg->m_pData[0] & 0x3f, pMsg->m_pData[0], pMsg->m_Unknown34);

		if (pMsg->m_MsgFlags & 0x10)
		{
			pCur = (CNetRecvPacketSendView*)pConn->m_SendQueue.m_pTail;

			while (pCur)
			{
				pRec = pCur;
				pCur = (pCur->m_pNext == (CNetRecvPacket*)pConn->m_SendQueue.m_pTail) ?
				           LTNULL : (CNetRecvPacketSendView*)pCur->m_pNext;

				if (pRec->m_bPending)
				{
					uint32 nLen = pRec->m_Len;

					if ((int)(nLen + 4) < ((ServerMessage*)pMsg)->GetSpaceLeft())
					{
						if (!net_CanAppend(pMsg))
						{
							sub_10023d16(&refSpill);
							sub_10023d5b(refSpill.m_pMsg, pMsg);

							pMsg = refSpill.m_pMsg;
						}

						sub_10023d73(pMsg, pRec->m_pData, pRec->m_Len);
					}
					else
					{
						sub_10022c2f(pRec->m_pData, nLen, pRec->m_nAlloced - nLen, pConn);

						pRec->m_bPending = 0;
					}
				}
			}
		}

		if (pConn->m_AckTimer > g_NetPiggybackTime && !(m_Flags & 1) &&
		    !(pConn->m_Flags & 1) &&
		    ((ServerMessage*)pMsg)->GetSpaceLeft() >= 0x2c)
		{
			CAbstractIO* pIO;

			CNetMgr::DebugPrint(pConn, 2, "Piggybacking ack local: %d, remote: %d.",
				(pConn)->m_nSent, pConn->m_PacketID);

			refAck.Init(msg_AllocPacket(&pIO, 0x1000, 0x1000));

			if (pIO)
				pIO->Release();

			WriteAckPacket(pConn, refAck.m_pMsg);

			if (net_CanAppend(pMsg))
			{
				sub_10023d5b(pMsg, refAck.m_pMsg);
			}
			else
			{
				sub_10023d16(&refSpill);
				sub_10023d5b(refSpill.m_pMsg, pMsg);
				sub_10023d5b(refSpill.m_pMsg, refAck.m_pMsg);

				pMsg = refSpill.m_pMsg;
			}
		}

		if (!(pConn->m_Flags & 1) && (pMsg->m_pData[0] & 0x80))
		{
			nRoom = pMsg->m_DataSize;
			pOut  = pMsg->m_pData;

			return sub_10022c2f(pOut, pMsg->m_Unknown34,
				nRoom - pMsg->m_Unknown34, pConn);
		}
		else
		{
			nRoom = pMsg->m_DataSize;
			pOut  = pMsg->m_pData;

			return pConn->m_pDriver->SendData(
				pOut, pMsg->m_Unknown34,
				nRoom - pMsg->m_Unknown34, pConn);
		}
	}
}


// 0x10022990 — put one packet on the wire, or park it if latency simulation
// is on.
//
// servermgr.h had this as `void`; it is not. Both exits leave a value in eax
// — a literal 1 from the queued path and SendPacketData's own answer from the
// real one — and the `ret 0xc` epilogue is shared between them, so the return
// type is LTBOOL. SendPacketData is likewise not void (below).
//
// The last statement is written out in both arms rather than once after the
// branch: the shipped code has `mov eax,[esi+0x54]; mov [eax],cl` twice, and
// it has to, because the two arms return different values and there is no
// shared tail to hang it on. It restores the first payload byte, which
// SendPacketData overwrites with the wire header.
//
// The result local lives in the *parameter* slot the now-dead pConn
// occupied — the frame has no `sub esp` at all — which is the usual /O1
// reuse and needs no help from the source.
LTBOOL CNetMgr::SendPacket(ServerMessage* pMsg, NetConnection* pConn, uint8 n)
{
	LTBOOL bSent;

	if (g_NetResendTime > 0.001f)
	{
		if (pMsg->m_DataLen > 0xffff - 0x32) return LTFALSE;
		CNetSentPacketView* pRec = m_PacketBank.Allocate();
		if (!pRec) return LTFALSE;

		pRec->m_Packet.Init(pMsg->m_Unknown34 + 0x32, 0x1000);
		if (pRec->m_Packet.m_Flags & PACKETFLAG_WRITEOVERFLOW) { m_PacketBank.Free(pRec); return LTFALSE; }

		memcpy(pRec->m_Packet.m_pData, pMsg->m_pData, pMsg->m_Unknown34);
		pRec->m_Packet.m_Unknown34 = pMsg->m_Unknown34;
		pRec->m_Packet.m_Unknown38 = pMsg->m_Unknown38;

		((LMessageList*)&pConn->m_SentList)->Prepend((LThreadMessage*)pRec);

		pRec->m_ResendTimer = 0.0f;

		*pMsg->m_pData = (uint8)n;

		return LTTRUE;
	}

	bSent = SendPacketData(pMsg, pConn);
	if (bSent)
		AddSentBytes(pConn, pConn->m_pDriver->GetRecvSize() + pMsg->m_Unknown34);

	*pMsg->m_pData = (uint8)n;

	return bSent;
}


// 0x1002307f — take one arrived packet apart: drop it if the drop-rate knob
// says so, check its checksum, put it in sequence if it is guaranteed, and
// strip the trailer so the caller sees only the payload. Nonzero means the
// packet is ready to be handed out.
//
// Three of the readings here are worth writing down.
//
// The connection's flag word is read *once* into a local for the first two
// tests (local, then has-checksum) and then re-read from memory at the three
// later tests, which is why there is a local at all: one `mov 0x70(%esi),%eax`
// serving two `test %al` and three separate `testb $0x2,0x70(%esi)`.
//
// The sequence-number comparison is a three-armed if/else chain against the
// same pair, and VC6 keeps the third arm's branch even though it can prove it
// (`jne`, then `jae`, then a `cmp`/`jbe` whose fall-through is unreachable).
// That third `nID > m_PacketID` has to be written out; an `else` in its place
// deletes the compare.
//
// The trailer length is computed twice from the same expression, once for the
// bounds check and once for the strip, in two different registers — so it is
// two expressions in the source and not one local. All three of those are
// written as an assignment followed by a conditional override rather than as
// a `?:`: `(flags & 2) ? 6 : 4` is `4 | (flags & 2)` and VC6 knows it, so the
// conditional-expression form comes out as `and $2` / `or $4` with no branch
// at all and the function loses ten bytes. The shipped code materialises the
// default and jumps over the override, which is what
// `n = 4; if (flags & 2) n = 6;` gives.
//
// The lengths are `uint16` locals throughout, and the 16-bit compares
// (`mov 0x34(%edi),%dx; cmp %bx,%dx`) are the tell: a `uint32` local forces a
// `movzwl` at the load and widens every compare.
//
// lithtech.exe 0x458700 (973, IDENTICAL) settled two more:
//   * each arm carries its OWN `if (len < nStrip) return 0;` (0x458a5f and
//     0x458a9c, each with its own `jae`); one shared test after the if/else
//     gave a `jmp` into a single compare. server.dll is the same either way.
//   * no `nType` local: the first byte is read through pData at each use.
//     With a uint8 local /O2 put it in %al and spilled it across the flag
//     test; shipped keeps it in %dl (0x4588ad) and reuses %edi for nID.
int CNetMgr::TranslatePacket(ServerMessage* pMsg, NetConnection* pConn, int bRaw)
{
    if (!pMsg || !pConn || !pMsg->m_DataLen || pMsg->m_DataLen > pMsg->m_DataSize ||
        pMsg->m_Pos > pMsg->m_DataLen || (pMsg->m_Flags & PACKETFLAG_READOVERFLOW)) return 0;
    const uint32 trailer = pMsg->m_pData[0] & 0x80 ? ((pConn->m_Flags & 2) ? 6 : 4) : ((pConn->m_Flags & 2) ? 2 : 0);
    if (pMsg->m_DataLen < trailer + 1) return 0;
	uint32 nFlags;
	uint32 nID;
	uint16 nStrip;
	uint8* pData;
	CNetRecvPacket* pRec;

	if (g_NetDropRate > 0.0001f && bRaw)
	{
		if (pConn->m_Flags & 1)
		{
			dsi_ServerError("Warning: using DropRate on local connection");
			dsi_ServerError("         MUST use ForceRemote in addition");
		}

		if (((float(rand()) / float(RAND_MAX)) * 100.0f) < g_NetDropRate)
		{
			uint32 nDropID;

			m_UnknownDC++;

			nDropID = (uint32)-1;

			if (pMsg->m_pData[0] & 0x80)
				nDropID = nolf::wire::load<uint32>(pMsg->m_pData + pMsg->m_DataLen - 4);

			DebugPrint(pConn, 1, "Dropping packet %lu", nDropID);

			return 0;
		}
	}

	nFlags = pConn->m_Flags;

	pConn->m_RecvWait = 0.0f;

	if (nFlags & 1)
	{
		(pConn)->m_nPacketsRecv++;

		DebugPrint(pConn, 4, "Got packet %d from connection %p (packet ID %d, length %d).",
			(pConn)->m_nPacketsRecv, pConn,
			pMsg->m_pData[0] & 0x3f, pMsg->m_Unknown34);

		// lithtech.exe 0x458801/0x458812: both refusals `jne` to ONE return-0
		// block, the one the final strip check (0x458aa5) ends in; with two
		// separate ifs /O2 gives each its own epilogue (+18 bytes). server.dll
		// cannot take the merged form: its single exit is unaffected, but the
		// extra join flips which of the `DebugPrint(pConn, 1..2, ...); return 0`
		// tails VC6 cross-jumps into (shipped 0x100231d3 / 0x100232ea both go
		// to the drop arm; the merged spelling sends the drop arm to them).
		// Measured with ||, `!a && !b` and nested positive guards: all three
		// close lithtech and all three lose server. Hence the image split.
#ifdef LT_SERVER_BUILD
		if (this->sub_10023a8c(pConn, pMsg))
			return 0;

		if (this->sub_100235e8(pConn, pMsg))
			return 0;
#else
		if (this->sub_10023a8c(pConn, pMsg) ||
			this->sub_100235e8(pConn, pMsg))
			return 0;
#endif

		if (pMsg->m_pData[0] & 0x80)
			pMsg->m_Unknown34 -= 4;

		return 1;
	}

	if (nFlags & 2)
	{
		uint16 nLen = pMsg->m_Unknown34;
		uint16 nCRC;

		if (nLen < 2)
			return 0;

		nCRC = nolf::wire::load<uint16>(pMsg->m_pData + nLen - 2);

		if (nCRC != net_Checksum(pMsg->m_pData, nLen - 2))
		{
			DebugPrint(pConn, 1, "Bad CRC packet .. first byte %d.",
				(int)(char)pMsg->m_pData[0]);

			return 0;
		}
	}

	if (this->sub_100235e8(pConn, pMsg))
		return 0;

	pData = pMsg->m_pData;

	if (pData[0] & 0x80)
	{
		uint16 nTail = 4;

		if (pConn->m_Flags & 2)
			nTail = 6;

		if (pMsg->m_Unknown34 < nTail)
			return 0;

		nID = nolf::wire::load<uint32>(pData + pMsg->m_Unknown34 - nTail);
        if (nID != 0xffffffff) pConn->m_Unknown80 = std::max(pConn->m_Unknown80,nID+1);

		if (nID == pConn->m_PacketID)
		{
			DebugPrint(pConn, 2, "Received guaranteed packet %d", nID);

			this->sub_10022ac2(pConn);
		}
		else if (nID < pConn->m_PacketID)
		{
			DebugPrint(pConn, 1, "Ignoring duplicate packet %d/%d",
				nID, pConn->m_PacketID);

			return 0;
		}
		else if (nID > pConn->m_PacketID)
		{
			if (nID > pConn->m_PacketID + 0x100)
				DebugPrint(pConn, 1, "Warning! Suspicious frame count %d on msg ID %d",
					nID, pData[0]);

			if (FindQueuedPacket(&pConn->m_GuaranteedQueue, nID))
			{
				DebugPrint(pConn, 2, "Received duplicate out of order packet %d", nID);

				return 0;
			}

			DebugPrint(pConn, 2, "Received out of order packet %d", nID);

			pRec = this->sub_10023c4d(pMsg->m_Unknown34);
			pRec->m_Len = pMsg->m_Unknown34;

			memcpy(pRec->m_pData, pMsg->m_pData, pMsg->m_Unknown34);

			pRec->m_ID = nID;

			if (!this->sub_10023b1f(&pConn->m_GuaranteedQueue, pRec))
				FreeRecvPacket(pRec);

			pConn->m_AckTimer += g_NetAckTime * 0.5f;

			return 0;
		}

		DebugPrint(pConn, 4, "Got packet %d from %p (packet ID %d (%d), length %d).",
			pConn->m_PacketID, pConn, pMsg->m_pData[0] & 0x3f,
			pMsg->m_pData[0], pMsg->m_Unknown34);

		if (this->sub_10023a8c(pConn, pMsg))
			return 0;

		nStrip = 4;

		if (pConn->m_Flags & 2)
			nStrip = 6;

		if (pMsg->m_Unknown34 < nStrip)
			return 0;
	}
	else
	{
		DebugPrint(pConn, 4, "Got packet from %p (packet ID %d, length %d).",
			pConn, pData[0] & 0x3f, pMsg->m_Unknown34);

		nStrip = 0;

		if (pConn->m_Flags & 2)
			nStrip = 2;

		if (pMsg->m_Unknown34 < nStrip)
			return 0;
	}

	pMsg->m_Unknown34 -= nStrip;

	(pConn)->m_nPacketsRecv++;

	return 1;
}


// ---- growing and shrinking ----------------------------------------------- //
//
// `Insert2` (0x1002431f) and `Remove2` (0x1000cd8e) used to be hand-written
// here as InsertAt/RemoveAt and were both NEAR -- 193 bytes against 191 with
// the item count reloaded rather than kept in ecx, and 182 against 182 with
// two adjacent instructions transposed. They are collection.h's template
// members now, which collection.cpp already matches at both addresses
// exactly, and it is Remove2 that lithtech.exe expands whole into
// CNetMgr::RemoveDriver at 0x004561e0.

// 0x10058780 — the list every CUnknown390 links itself into (objtypes.cpp's
// un390_Init / un390_Term reach it by extern). A file-scope DECLARE_LTLINK:
// its dynamic initialiser is server.dll 0x10024486 (the _$E head, a
// zero-displacement jmp) and 0x1002448b (the body, ltlink.h's TieOff
// expanded on a constant `this`: m_pNext stored before m_pPrev, as the
// chained assignment gives). Both sit directly in front of 0x1002449b below,
// which is what places the definition here.
DECLARE_LTLINK(g_Un390List)

// ---- 0x1002449b, RECONSTRUCTED (385 of 385, byte-identical) ------------- //
//
// servermgr.h's CUnknown390 (the manager's +0x390) constructor. It was never
// a codegen problem, it was a layout one, and the layout it needed was
// already in src/engine/server/objbank.cpp -- CObjMgrView, the same view
// whose destructor (0x1002ca7c) that file matches exactly. The constructor
// now lives beside it there; what follows is only the evidence, kept because
// this is where it was first written down.
//
// The body is twelve sub-objects and an array. Eleven of the sub-objects are
// the vptr-over-StructBank shape CNetBank and CMessageBank have, spaced 0x24
// apart, each built as `vptr = <table>; sb_Init2(&bank, structSize, 0x20,
// growSize)` with 0x20 for the cache size every time:
//
//   offset  vtable      structSize  growSize
//   0x68    0x1004df58  0x1a8       0x20
//   0x8c    0x1004df48  0x248       0x100
//   0xb0    0x1004df38  0x270       0x20
//   0xd4    0x1004df28  0x1c8       0x80
//   0xf8    0x1004df18  0x1ac       0x20
//   0x11c   0x1004df08  0x1d0       0x20
//   0x140   0x1004def8  0x25c       0x20
//   0x164   0x1004dee8  0x11f8      0x20
//   0x188   0x1004ded8  0x21c       0x20
//   0x1ac   0x1004dec8  0x274       0x20
//   0x1d0   0x1004deb8  0x1b4       0x20
//
// objbank.cpp's eleven CObjectBank<T> instantiations reproduce every one of
// those structSize immediates out of sizeof(T), so the grow size is the only
// thing the constructor has to be told.
//
// Then an array of eleven sixteen-byte records at 0x220, built by a real loop
// (`mov $0xb,%edx` down-counted, stride 0x10): each record's first dword is
// cleared and the LTLink at its +0x04 is tied off to itself. That is an array
// of eleven LTLists -- ltlink.h's own default constructor, one member array,
// one loop.
//
// THE POINTER AT OFFSET 0x00 IS NOT A vptr, and that is what had this
// blocked. The constructor writes it TWICE -- 0x1004df68 before the eleven
// banks and 0x1004deb0 after them -- and the two cannot be a base/derived
// pair: 0x1004df68 is four slots ({0x1003ea24, 0x1003ea24, 0x10024690,
// 0x10024afc}, the first two _purecall) and 0x1004deb0 is two
// ({0x1002461c, 0x10017b0e}, the second folded onto Model::GetFilename), and
// no derived table is shorter than its base's. The destructor decides it: a
// polymorphic class restores its own vftable at the top of its destructor,
// and 0x1002ca7c has no store to (%esi) anywhere. So offset zero is plain
// data written by hand, exactly as servermgr.h's CUnknown8FC already does,
// and the only trick is that the FIRST store has to precede eleven member
// constructors -- which a one-member base class with an inline constructor
// puts there and a body statement never could.

// ---- a two-dword list, built here because this is where its bytes land -- //
//
// 0x100243f6 — the smallest of the sub-objects the bigger engine records hang
// off themselves: two dwords, a head and a back-pointer, and a constructor
// whose whole body is one call to the initialiser with a null owner. It sits
// at Model+0x14 (commonmodel.h's note lists that whole run of thirteen
// sub-object constructors) and at +0x28 of the object whose vtable is
// 0x1004d344 — and that second call site passes its own `this` as the owner,
// which is what says the constructor's zero is a default and not the only
// value the field ever takes. Named for the offset it occupies in Model, the
// way worldbsp.h names CWorldTreeSub28.
//
// Only the constructor is reconstructed here; the initialiser is declared and
// never defined, which is all the per-object verification needs. It also has
// to stay out of line — /O1 implies /Ob1, so a body written inside the class
// declaration would be expanded into the constructor and the call would
// disappear.
//
// What the other three functions of the family do, read off the shipped
// bytes so the next attempt does not have to re-read them:
//   0x10024409  Init(pOwner): `m_pHead = NULL; m_pOwner = pOwner;`, returns
//               LTTRUE (`push $1; pop eax`), `ret 4`.
//   0x10024404  the destructor — a bare `jmp` to 0x10024419, so its body is
//               exactly one tail call.
//   0x10024419  calls 0x10024431(0, &nOne) with a `1` in a stack local.
//   0x10024431  walks the chain: each node at m_pHead carries an inner list
//               at its +0x00 and the next node at its +0x08; each inner node
//               carries a function pointer at +0x00 and its own next at
//               +0x04, and every function is called cdecl with
//               (this, pNode, arg1, arg2). Returns 0.
// ---- the fragment reassembler and the message helpers -------------------- //

// 0x100211e2 — nonzero when the message in hand is a guaranteed packet whose
// first sub-type is 3 (the appendable sub-packet stream), so the send path
// can keep writing into it instead of starting a fresh one. Cdecl, one
// argument, and it never looks at anything but the first two payload bytes.
int net_CanAppend(ServerMessage* pMsg)
{
	uint8* pData = pMsg->m_pData;

	if (pMsg->m_DataLen >= 2 && (pData[0] & 0x3f) == 1 && pData[1] == 3)
		return 1;

	return 0;
}


// 0x10023474 — nonzero once all four fragments of a split packet have
// arrived. `this` is never read; the record is the only argument.
int CNetMgr::sub_10023474(CNetFragRec* pRec)
{
    return pRec && pRec->Complete();
}


// NEAR (56 of 54 bytes, one instruction over): ours rotates the loop —
// peeling the head test and duplicating it into the back edge — where the
// shipped build keeps one shared test that both the entry and the two `jne`
// back edges reach. Same class as CNetMgr::FindQueuedPacket and
// sub_10023c4d below, and the rule behind all three is in the note on
// sub_10023a91: cl 8447 rotates a loop whose back edge is *conditional* and
// leaves one whose back edge is an unconditional `jmp` alone.
//
// 0x100233ad — find the record collecting a given packet id for a given
// connection. The manager's second queue is walked from its tail with the
// cursor alias, and a record has to match on both the id and the connection:
// two ids can be in flight at once and two connections can be sending the
// same id.
CNetFragRec* CNetMgr::sub_100233ad(uint32 nID, NetConnection* pConn)
{
	CNetFragRec* pTail = (CNetFragRec*)m_QueueC4.m_pTail;
	// RE-READ THE MEMBER, do not copy pTail. Written `pCur = pTail;`
	// VC6 tests the tail and sinks the copy; the shipped bytes copy first
	// and test the copy. Proven in-tree by tcpdriver.cpp's tcp_FindConnection,
	// which reads the member twice and is byte-identical.
	CNetFragRec* pCur = (CNetFragRec*)m_QueueC4.m_pTail;
	CNetFragRec* pRec;

	while (pCur)
	{
		pRec = pCur;
		pCur = (pCur->m_pPrev == (LThreadMessage*)pTail) ?
		           LTNULL : (CNetFragRec*)pCur->m_pPrev;

		if (pRec->m_ID == nID && pRec->m_pConn == pConn)
			return pRec;
	}

	return LTNULL;
}


// 0x10023d73 — append a length-prefixed block to a message: the length as a
// word and then the bytes. `this` is unread, so the message it writes to is
// the first argument rather than a member.
//
// Both images are byte-identical. The word goes through packet.h's
// CPacket::WriteType<uint16> directly: server.dll calls it (0x1000ec3d, the
// same target the old WriteWord declaration named), and lithtech.exe
// 0x004599f0 expands it. The block goes through a one-line inline forwarder
// onto WriteData (0x1002616a / 0x0045c390). That forwarder is not decoration.
// /O2 spends its inline budget on the LAST sites first, and expanding the
// forwarder after the WriteType is what keeps Insert2 a call inside the
// expanded write (0x00459a31 -> 0x00412f50). Without it Insert2 expands too
// (365 bytes against 195).
void CNetMgr::sub_10023d73(ServerMessage* pMsg, void* pData, uint32 nLen)
{
	((CPacket*)pMsg)->WriteType((uint16)nLen);

	((ServerMessage*)pMsg)->WriteData(pData, (uint16)nLen);
}


// 0x10023d5b — the same append with the block taken from another message's
// payload: everything written into pSrc so far goes onto pDst as one sub-
// packet. `this` survives in ecx across the call because nothing in between
// touches it.
void CNetMgr::sub_10023d5b(ServerMessage* pDst, ServerMessage* pSrc)
{
	sub_10023d73(pDst, pSrc->m_pData, pSrc->m_Unknown34);
}


// 0x10023d16 — start a fresh internal packet in the reference handed in: a
// message off the packet allocator, its first payload byte set to the
// internal packet id (1), and the sub-type written straight after it (3, the
// appendable sub-packet stream).
#ifndef LT_SERVER_BUILD
// STAND-IN (lithtech.exe only, name ours): the header byte and the sub-type
// written by one inline helper. lithtech.exe 0x004598d0 expands WriteType
// but CALLS Insert2 (0x00459956 -> 0x00412f50); with the two writes in the
// function body /O2 has budget left and expands Insert2 as well (413 bytes).
// One inline level around both writes spends it, as a destructor after the
// write does in smgr.cpp's sm_BroadcastPrintf (0x00476010).
inline void NetStartPacket(CMessageRef* pRef, uint8 nID, uint8 nSubType)
{
	pRef->m_pMsg->m_pData[0] = nID;

	((CPacket*)pRef->m_pMsg)->WriteType(nSubType);
}
#endif

// 0x10023d16 — start a fresh internal packet in the reference handed in: a
// message off the packet allocator, its first payload byte set to the
// internal packet id (1), and the sub-type written straight after it (3, the
// appendable sub-packet stream).
void CNetMgr::sub_10023d16(CMessageRef* pRef)
{
	CAbstractIO* pIO;

	pRef->Init(msg_AllocPacket(&pIO, 0x1000, 0x1000));
	if (pIO)
		pIO->Release();

#ifdef LT_SERVER_BUILD
	pRef->m_pMsg->m_pData[0] = 1;

	((CPacket*)pRef->m_pMsg)->WriteType((uint8)3);
#else
	NetStartPacket(pRef, 1, 3);
#endif
}


// 0x10023dc2 — CNetMgr::DebugPrint without the connection: the same level
// test and the same trace line, minus the per-connection quiet filter that
// needs a connection to read m_Flags off. An overload rather than a second
// name, which is what the two symbols look like from the call sites.
void CNetMgr::DebugPrint(int nLevel, char* pFormat, ...)
{
	char    msg[500];
	va_list marker;

	if (nLevel > g_NetDebugLevel)
		return;

	va_start(marker, pFormat);
	vsnprintf(msg, sizeof(msg) - 1, pFormat, marker);

	dsi_ServerError("(%d) %s%s", NET_TRACE_TIME(), m_pTraceName, msg);
}


// 0x10051c78 — the queue depth past which the send side starts refusing
// unguaranteed traffic; a plain initialised global (70) registered with the
// console as "ConnTroubleCount2" (its table entry at 0x10052214 names it).
extern int g_NetConnTroubleCount2;

// 0x10021c55 — send one message to one connection, with the wire header's
// two top bits taken from the caller.
//
// The refusal is the interesting half: with neither the "must go" bit nor
// the local-connection flag set, a connection with more than 70 entries
// backed up on *either* queue is answered LTTRUE without anything going out.
// The two `&&` tests fall through into the send below them and the two `||`
// tests jump forward into the shared `return LTTRUE`, which is the whole of
// how the branch polarity reads back.
//
// The header byte is saved into the now-dead connection parameter slot as a
// *byte*, which is what says SendPacket's third argument is a uint8: the
// call pushes the whole dword that slot sits in, upper three bytes and all,
// with no zero extension.
LTBOOL CNetMgr::SendToConnection(ServerMessage* pMsg, NetConnection* pConn,
                                     int nFlags)
{
	uint8 nType;

	if (!pConn)
		return LTFALSE;

	nFlags &= 0xc0;

	if (!(nFlags & 0x80) && !(pConn->m_Flags & 1) &&
	    (pConn->m_GuaranteedQueue.m_nMessages > (uint32)g_NetConnTroubleCount2 ||
	     pConn->m_SendQueue.m_nMessages > (uint32)g_NetConnTroubleCount2))
	{
		return LTTRUE;
	}

	pConn->m_SendWait = 0.0f;

	nType = pMsg->m_pData[0];

	pMsg->m_pData[0] = (uint8)((nType & 0x3f) | nFlags);

	return SendPacket(pMsg, pConn, nType);
}


// 0x1002294f — hand everything parked on a connection's send queue to the
// local-connection pipe. Each record carries its own payload, the length
// written into it and the capacity it was given, and the pipe takes the
// length and the slack separately. The records are not taken off the queue
// here: the walk is the cursor alias and the list is left as it was.
void CNetMgr::sub_1002294f(NetConnection* pConn)
{
	CNetRecvPacket* pCur;
	CNetRecvPacket* pRec;

	if (!pConn)
		return;

	pCur = (CNetRecvPacket*)pConn->m_SendQueue.m_pTail;

	while (pCur)
	{
		pRec = pCur;
		pCur = (pCur->m_pPrev == (CNetRecvPacket*)pConn->m_SendQueue.m_pTail) ?
		           LTNULL : static_cast<decltype(pCur)>(pCur->m_pPrev);

		sub_10022c2f(pRec->m_pData, pRec->m_Len,
		             pRec->m_nAlloced - pRec->m_Len, pConn);
	}
}


// lithtech.exe has no standalone copy of the constructor or of Clear:
// sub_10023495's /O2 twin (0x00458c00) expands both, and the slot release
// inside Clear with them (0x00458c7a / 0x00458cc8: `mov (%edi),%ecx ; test ;
// call *0x8(%eax) ; movl $0,(%edi)`, the cursor Init with a null argument).
// server.dll calls 0x10023505 and 0x1002351c out of line, and Clear calls
// 0x10023e86. So both are `inline` only for the client image, and Clear
// reaches the slot through an inline twin of the cursor Init there.
#ifdef LT_SERVER_BUILD
#define NETFRAG_INLINE
#else
#define NETFRAG_INLINE inline
#endif

// 0x10023495 — get a record to collect a split packet in. The manager keeps
// at most six of them: with more than that already queued the oldest (the
// head, one past the tail on the circular list) is taken off, emptied and
// reused, and otherwise a new one is allocated. Either way the record goes
// on the back of the queue, so "oldest" really is the least recently issued.
//
// The counting walk reads the tail through a pointer to the field, and the
// recycle arm reads the field itself. That is what the bytes say: both
// images hoist the tail into a register for the loop (server 0x1002349b,
// lithtech 0x458c00) and then RE-READ m_QueueC4.m_pTail for the arm
// (0x100234be, 0x458c2f). With the field named directly at both places VC6
// reused the loop's register in the arm, and the esi/edi pair for `this`
// and the alias temp came out transposed (server 106/112). Earlier measured
// negatives: a pTail local, a `pCur = pTail` copy, block-scoped pCur, a
// `for` over LThreadMessage*, nCount++ before the advance, the reversed
// ternary, the arm through an (LMessageList*) cast, a pList local (110),
// GenListPos with qualified GenBegin/GenGetNext (114), an in-class
// CountFrags (declined at /Ob1; 106 forced). lithtech used to get the reload
// from a stand-in inline helper; this one body is identical on both images.
CNetFragRec* CNetMgr::sub_10023495()
{
	CNetFragRec*     pRec;
	CNetFragRec*     pCur;
	uint32           nCount = 0;
	LThreadMessage** ppTail = &m_QueueC4.m_pTail;

	pCur = (CNetFragRec*)*ppTail;

	while (pCur)
	{
		pCur = (pCur->m_pPrev == *ppTail) ?
		           LTNULL : (CNetFragRec*)pCur->m_pPrev;

		nCount++;
	}

	if (nCount > 5)
	{
		pRec = (CNetFragRec*)((CNetFragRec*)m_QueueC4.m_pTail)->m_pNext;

		((LMessageList*)&m_QueueC4)->Remove((LThreadMessage*)pRec);

		pRec->Clear();
	}
	else
	{
		pRec = new CNetFragRec;
	}

#ifdef NOLFSRC_MODERN
	// fixed: the fragment-record allocator passes a null to the list splice
	// (RETAIL_BUGS.md). Out of memory, there is no record to queue.
	if (pRec)
		((LMessageList*)&m_QueueC4)->Append((LThreadMessage*)pRec);
#else
	((LMessageList*)&m_QueueC4)->Append((LThreadMessage*)pRec);
#endif

	return pRec;
}


// 0x100233e3 — put one fragment into a record at its index. A slot that is
// already filled means a duplicate, and the whole call is refused.
//
// Fragment zero is special: the packet's own header byte is read off the
// wire and written into the fragment's payload, so that when the four are
// replayed the first one carries the reassembled packet's type. The rest of
// the arriving message — everything from its read position to its write
// position — is then appended to the fragment.
//
// The now-dead record parameter's slot holds the allocator's out-parameter,
// which is why the frame has no locals of its own.
//
// Two facts the bytes had to give up. The first fragment's message is read
// into a local *before* the byte is read off the wire — written as one
// statement the load sinks past the call — and the two `movw $1` stores
// share one load of the slot while the WriteData that follows them re-reads
// it, because a store through the loaded pointer could have overwritten the
// slot itself. One named local for the first pair and the raw expression for
// the third use is the only shape that gives both.
//
// The header byte is read into its own `uint8` local before the store. Written
// as one assignment, server.dll swapped esi and edi for the whole function:
// the slot went in esi and the message in edi, where shipped has the slot in
// edi (0x100233f4) and the message in esi (0x1002342a). It was a 145/145 near
// that looked like pure allocation. Byte-identical on both images now
// (lithtech 0x458b10, 180 bytes). Negatives, no change on server: the
// declarations reordered; `pRec->m_pFrags + nIndex`; `!nIndex`;
// `&m_pData[...]`; the source message through a copy, or its data pointer
// through a local; the length passed without the (uint16) cast.
LTBOOL CNetMgr::sub_100233e3(ServerMessage* pMsg, uint8 nIndex,
                                     CNetFragRec* pRec)
{
    return pMsg && pRec && pRec->AddFragment(*pMsg, nIndex);
}


//   12  0x1002220d  u16  ReadType      FLG word
//   13  0x10022218  u8   ReadType      FLG byte
//   14  0x10022223  u32  ReadType      FLG dword
//   15  0x10022393  u8   ReadType      UUP sub-packet length
//   16  0x1002251f  u8   ReadType      PIC sub-type
//
//  netmgr.cpp:1556 already declares `ServerMessageTypeView` with exactly
//  the ReadWord / ReadDWord / ReadData members sites 1-7 need, so nothing
//  new has to be added for them; `ReadByte` is servermsg.h:340's.
// ---------------------------------------------------------------------

// ---------------------------------------------------------------------
//  FRAME, read off the prologue at 0x10021cbe
//
//    push %ebp ; mov %esp,%ebp ; mov $0x2110,%eax ; call 0x1003e740
//        -- _chkstk for 0x2110 of locals
//    push %ebx ; push %esi ; push %edi
//
//    -0x2110  char   str[0x2000]      (memset at 0x10021cce)
//    -0x0110  char   temp[0x100]
//    -0x0010  ...    the four scalar slots, see below
//
//  0x2000 + 0x100 + 0x10 = 0x2110 exactly, so there are FOUR dword slots
//  above temp and no more.  They are:
//      -0x04  nSubLen   (shared by the GRP, UPD and UUP blocks)
//      -0x08  UPD's  pIO
//      -0x0c  UUP's  pIO
//      -0x10  GRP's  pIO
//  Three separate pIO slots is why the three blocks each declare their own
//  `CAbstractIO* pIO` below rather than sharing one at function scope --
//  a single function-scope pointer would have had ONE slot.
//
//  Everything else lives in registers or in the incoming parameter home
//  slots, which /O1 reuses once the parameters are dead:
//      %esi     pMsg        (loaded 0x10021ce1, live throughout)
//      %ebx     nLen        (loaded 0x10021d0c / 0x10021d33)
//      %edi     scratch zero / scratch one / nCount
//      0x08(%ebp)  nCount (GRP), the uint16 Init/ReadData arg (UPD, UUP),
//                  the FLG word
//      0x0c(%ebp)  nSubWord (GRP), nFlags (UPD record loop), the
//                  CMessageRef (UPD, UUP)
//      0x10(%ebp)  the CMessageRef (GRP)
//      0x13(%ebp)  nSubByte (UPD, UUP), the FLG byte
//  None of that is expressible in source; it is recorded so the next
//  reader does not mistake a parameter store for a parameter write.
// ---------------------------------------------------------------------

// ---------------------------------------------------------------------
//  SIGNATURE
//
//  __cdecl, three arguments, caller-cleaned (`add $0xc,%esp` after each of
//  the three recursive calls, 0x10021e9d / 0x100220ad / 0x10022434).
//  External linkage: servermgr.h:2358 declares it and netmgr.cpp:330 calls
//  it across no TU boundary but through that declaration.
//
//  THE THIRD ARGUMENT IS A BYTE, NOT AN `int`.  0x10021cef and 0x10021d16
//  are both `cmpb $0x0,0x10(%ebp)` -- a byte compare, which an `int`
//  parameter could not produce (an int 0x100 would test false).  So
//  servermgr.h:2358's `int n` is wrong in type and should read
//  `uint8 bOut`; netmgr.cpp:330 passes the literal 1 and is unaffected
//  either way.  The header is not edited here.  Alternative spelling at this site: `LTBOOL`, but the engine
//  typedefs LTBOOL to a 32-bit type, so it would not give `cmpb`.
// ---------------------------------------------------------------------
void net_LogPacket(ServerMessage* pMsg, uint32 nLen, uint8 bOut)
{
	char         temp[0x100];
	char         str[0x2000];
	uint32       nFlags;
	uint32       nCount;
	uint32       nSubLen;
	uint16       nSubWord;
	uint8        nSubByte;
	uint16       nFlagWord;
	uint8        nFlagByte;
	uint32       nFlagLong;
	uint8        nMsgClass;
	// -0x08 / -0x0c / -0x10.  THREE SEPARATE SLOTS: the frame is 0x2110
	// and 0x2000 + 0x100 + nSubLen's dword leaves exactly three more.
	// Declaring one `CAbstractIO*` per block instead gives ONE slot --
	// VC6 /O1 merges the stack slots of disjoint lexical scopes, measured,
	// and the frame comes out 0x2108.
	CAbstractIO* pIOUpd;
	CAbstractIO* pIOUup;
	CAbstractIO* pIOGrp;

	// 0x10021cce: push 0x2000 / push 0 / push str -> 0x1003e280.
	memset(str, 0, sizeof(str));

	// 0x10021ce7  mov 0x54(%esi),%eax ; testb $0x80,(%eax)
	// -- payload byte 0, bit 7 = "this packet is guaranteed".
	if (pMsg->m_pData[0] & 0x80)
	{
		// 0x10021cef: cmpb $0x0,0x10(%ebp), then the pointer select
		// 0x10053780 "G " (bOut != 0) / 0x1005377c "g " (bOut == 0).
		strcat(str, bOut ? "G " : "g ");

		// 0x10021d11  sub $0x4,%ebx -- only on the guaranteed side.
		nLen -= 4;
	}
	else
	{
		// 0x10021d16: 0x10053778 "U " / 0x10053774 "u ".
		strcat(str, bOut ? "U " : "u ");
	}

	// 0x10021d38: the payload byte is RE-READ from m_pData, not carried
	// over from the test above -- `mov 0x54(%esi),%eax ; mov (%eax),%al ;
	// and $0x3f,%eax`.  (VC6 does not CSE across the strcat call.)
	switch (pMsg->m_pData[0] & 0x3f)
	{
		// --------------------------------------------------------- 0x10021d5f
		case 0:
			strcat(str, "TCP ");
			break;

		// ------------------------------------------------------------
		//  1 -- the engine's own packet.  0x10021d69.
		//  Sub-type in the next payload byte; the four arms are the same
		//  four the internal handler at 0x100235e8 dispatches, and its
		//  own error string "Error in SUB_ACK" names arm 0.
		// ------------------------------------------------------------
		case 1:
			strcat(str, "NM  ");

			// 0x10021d7c  push $1 ; pop %edi -- the constant 1 is parked
			// in %edi for both guards and both `nLen--`s in this arm.
			if (nLen < 1)
				goto bad;
			// 0x10021d87  movzwl 0x34 ; movzwl 0x38 ; sub ; cmp ; jl
			// -- SIGNED, because these are two uint16 members promoted to
			// int by the usual arithmetic conversions.  Not a cast.
			if (pMsg->m_Unknown34 - pMsg->m_Unknown38 < 1)
				goto bad;

			nLen--;

			// 0x10021d9e -> 0x100138d6.  The chain at 0x10021da3 is
			// `movzbl %al,%eax ; sub $0x0,%eax ; je ... ; dec ; je ...`,
			// i.e. a dense 0..3 switch that /O1 laid out as a compare
			// chain.  (/O2 gave it the 4-dword table at 0x004577e0.)
			switch (pMsg->ReadByte(0))
			{
				// --------------------------------------- 0x10021ee8
				case 0:
					strcat(str, "ACK ");

					if (nLen < 1)
						goto bad;
					if (pMsg->m_Unknown34 - pMsg->m_Unknown38 < 1)
						goto bad;

					nLen--;

					// 0x10021f1f  test %al,%al
					if (pMsg->ReadByte(0))
						strcat(str, "PRQ ");

					if (nLen < 8)
						goto bad;
					if (pMsg->m_Unknown34 - pMsg->m_Unknown38 < 8)
						goto bad;

					// 0x10021f52  xor %edi,%edi -- %edi stops being the
					// constant 1 here and becomes nCount.
					nLen -= 8;
					nCount = 0;

					((ServerMessage*)pMsg)->ReadDWord(0);
					((ServerMessage*)pMsg)->ReadDWord(0);

					// 0x10021f67: the entry test is PEELED (`cmp $4 ; jle
					// 0x100226be`) and jumps straight to the print tail --
					// VC6 threads it through the `if (nCount)` below,
					// which it knows is still zero on that edge.  The loop
					// proper is 0x10021f7a..0x10021f99, bottom-tested.
					while (pMsg->m_Unknown34 - pMsg->m_Unknown38 > 4)
					{
						// 0x10021f7d  cmp $0x4,%ebx ; jbe -- UNSIGNED.
						if (nLen <= 4)
							break;

						nCount++;
						nLen -= 4;

						((ServerMessage*)pMsg)->ReadDWord(0);
					}

					// 0x10021f9b  test %edi,%edi ; je 0x100226be
					if (nCount)
					{
						sprintf(temp, "REQ %d", nCount);
						strcat(str, temp);
					}

					goto done;

				// --------------------------------------- 0x10021ede
				case 1:
					strcat(str, "DIS ");
					break;

				// --------------------------------------- 0x10021ed4
				case 2:
					strcat(str, "PNG ");
					break;

				// ------------------------------------------ 0x10021dc4
				//  3 -- a group packet: a run of word-length-prefixed
				//  sub-packets, each carved out and traced on its own.
				// ------------------------------------------
				case 3:
					strcat(str, "GRP ");

					// 0x10021dd5  andl $0x0,0x8(%ebp)
					nCount = 0;

					// 0x10021dda  cmp $0x2,%ebx ; jb  (unsigned, entry)
					// 0x10021ea9  cmp $0x2,%ebx ; jae (unsigned, bottom)
					while (nLen >= 2)
					{
						// `< 2`, NOT `== 0`: 0x10021df3 is
						// `cmp $0x2,%eax ; jl` and the /O2 twin
						// 0x004566d9 is the same.  The UPD and UUP loops
						// below really ARE `== 0`, so this is a local
						// difference, not a blanket one.
						if (pMsg->m_Unknown34 - pMsg->m_Unknown38 < 2)
							break;

						// 0x10021dfc / 0x10021e01 -- two `dec %ebx`
						// straddling the argument push.
						nLen -= 2;

						nSubWord = ((ServerMessage*)pMsg)->ReadWord(0);

						// 0x10021e07  test %ax,%ax -- 16-bit, so the
						// variable is uint16 and not uint32.
						if (nSubWord)
						{
							// 0x10021e13  movzwl %ax,%eax ; mov %eax,-0x4(%ebp)
							nSubLen = nSubWord;

							if (nLen < nSubLen)                                     // 0x10021e16 jb
							{
								strcat(str, "BAD ");
								break;
							}
							if (pMsg->m_Unknown34 - pMsg->m_Unknown38 < (int)nSubLen) // 0x10021e2b jl
							{
								strcat(str, "BAD ");
								break;
							}

							// 0x10021e33  incl 0x8(%ebp) -- BEFORE the
							// block below, not after.
							nCount++;

							{
								CMessageRef  ref;

								// 0x10021e36  andl $0x0,0x10(%ebp) -- the
								// CMessageRef constructor, expanded.
								// 0x10021e40  msg_AllocPacket(&pIOGrp, 0x1000, 0x1000)
								// 0x10021e4c  ref.Init(...)   (0x1000ebab,
								//   out of line: /Os declines its 37 bytes)
								ref.Init(msg_AllocPacket(&pIOGrp, 0x1000, 0x1000));

								// 0x10021e51  if (pIOGrp) pIOGrp->Release()
								if (pIOGrp)
									pIOGrp->Release();

								// 0x10021e64 -> 0x10010da9.  Note the raw
								// `push 0xc(%ebp)` -- a uint16 argument is
								// passed as the dword it sits in without
								// zero-extension, which is only legal
								// because the callee's parameter is uint16.
								ref.m_pMsg->Init(nSubWord, 0x1000);

								// 0x10021e74 -> 0x100260d9
								((ServerMessage*)pMsg)->ReadData(
									ref.m_pMsg->m_pData, nSubWord);

								// 0x10021e85 / 0x10021e8c -- both 16-bit
								// stores.
								ref.m_pMsg->m_Unknown34 = nSubWord;
								ref.m_pMsg->m_Unknown38 = 1;

								// 0x10021e95  call 0x10021cbe (itself)
								net_LogPacket(ref.m_pMsg, nSubLen, 0);

								// 0x10021e9a  ~CMessageRef, expanded.
							}
						}
					}

					// 0x10021ec7: push nCount, push "%d ", then jmp to the
					// shared sprintf+strcat at 0x1002230e, which exits to
					// the print tail.
					sprintf(temp, "%d ", nCount);
					strcat(str, temp);
					goto done;

				// --------------------------------------- 0x10022681
				default:
					strcat(str, "UNK ");
					break;
			}
			break;

		case 4:                                   // 0x10021fae
			strcat(str, "NPV ");
			break;

		case 5:                                   // 0x10021fb8
			strcat(str, "ULW ");
			break;

		case 6:                                   // 0x10021fc2
			strcat(str, "LW  ");
			break;

		case 7:                                   // 0x10021fcc
			strcat(str, "COI ");
			break;

		// ------------------------------------------------------------
		//  8 -- the object update packet.  0x10021fd6.  A run of
		//  byte-length-prefixed sub-messages first, then the per-object
		//  update records.
		// ------------------------------------------------------------
		case 8:
			strcat(str, "UPD ");

			// 0x10021fe2  dec %ebx -- placed among the strcat pushes.
			nLen--;

			// 0x10021fe9  test %ebx,%ebx ; je (entry)
			// 0x100220b9  test %ebx,%ebx ; jne (bottom)
			while (nLen)
			{
				// 0x10021ff7  ... sub ; je -- `== 0`, unlike GRP's `< 2`.
				if (pMsg->m_Unknown34 - pMsg->m_Unknown38 == 0)
					break;

				nLen--;

				nSubByte = pMsg->ReadByte(0);

				// 0x10022011  test %al,%al ; mov %al,0x13(%ebp) ; je
				if (nSubByte == 0)
					break;

				// 0x1002201c  movzbl %al,%eax ; mov %eax,-0x4(%ebp)
				nSubLen = nSubByte;

				if (nLen < nSubLen)                                       // 0x1002201f jb
				{
					strcat(str, "BAD ");
					break;
				}
				if (pMsg->m_Unknown34 - pMsg->m_Unknown38 < (int)nSubLen)  // 0x10022034 jl
				{
					strcat(str, "BAD ");
					break;
				}

				{
					CMessageRef  ref;

					ref.Init(msg_AllocPacket(&pIOUpd, 0x1000, 0x1000));   // 0x1002203c
					if (pIOUpd)
						pIOUpd->Release();                                // 0x10022057

					// 0x10022063  movzbw 0x13(%ebp),%ax ; mov %ax,0x8(%ebp)
					// -- the uint8 is widened to uint16 ONCE and the
					// result reused by both Init and ReadData below.
					// The shipped widening lives in MEMORY (0x8(%ebp),
					// re-read at 0x10022076 and 0x1002208c) and ours in
					// %ebx; naming the widened value -- `nSubWord =
					// nSubByte;` then passing nSubWord at all three sites
					// -- is a MEASURED no-op, byte-identical output, so
					// the difference is the allocator's and not the
					// spelling's.
					ref.m_pMsg->Init(nSubByte, 0x1000);                // 0x10022071

					// 0x1002207c  sub -0x4(%ebp),%ebx -- AFTER Init and
					// BEFORE ReadData.  GRP has no such subtraction at
					// all; UUP has it in exactly this position.  That
					// asymmetry is in the shipped bytes of both images
					// and is source, not scheduling.
					nLen -= nSubLen;

					((ServerMessage*)pMsg)->ReadData(
						ref.m_pMsg->m_pData, nSubByte);                // 0x10022084

					ref.m_pMsg->m_Unknown34 = nSubByte;                // 0x10022095
					ref.m_pMsg->m_Unknown38 = 1;                       // 0x1002209c

					net_LogPacket(ref.m_pMsg, nSubLen, 0);             // 0x100220a5
				}
			}

			// The record loop.  Head 0x100220d6; every `continue` in it
			// jumps back there and there is no duplicated bottom test.
			// 0x100220e0  test %eax,%eax ; jle -- SIGNED  (> 0)
			// 0x100220e8  test %ebx,%ebx ; jbe -- UNSIGNED (> 0)
			while (pMsg->m_Unknown34 - pMsg->m_Unknown38 > 0 && nLen > 0)
			{
				// 0x100220f0  testb $0x4,0x100584f0 ; je PAST the block.
				// THE GUARD IS NOT NEGATED: the line is flushed when the
				// bit is SET.  Reads correctly too -- bit 2 means "expand
				// UPD", and the tail filter drops the line unless the
				// same bit is set.
				if (g_NetLogPackets & 4)
				{
					// 0x10022100 -> 0x1000e3f9, and the argument slot is
					// REUSED by the strcpy at 0x1002210b rather than
					// popped.  Cleanup batching across two cdecl calls is
					// the fixed-prototype signature servermgr.h:2372
					// records, so this site was compiled against
					// dsi_PrintString(char*), not con_Printf's varargs.
					// (Alternative spelling at this site: con_Printf(str)
					// -- same five call bytes, but it would force a
					// `pop %ecx` here and cost three bytes.)
					dsi_PrintString(str);
					strcpy(str, "      ");     // 0x100536f8, six spaces
				}

				// 0x1002211a  xor %edi,%edi -- %edi is the constant zero
				// for the rest of this loop.
				nLen--;

				// 0x10022120 -> 0x100138d6, then
				// 0x10022125  movzbl %al,%eax ; test $0x80,%al
				nFlags = ((CPacket*)pMsg)->ReadType((uint8*)0);

				if (nFlags & 0x80)
				{
					if (nLen < 1)
						goto bad;
					if (pMsg->m_Unknown34 - pMsg->m_Unknown38 < 1)
						goto bad;

					nLen--;

					// 0x10022154  xor %ecx,%ecx ; mov %al,%ch ; or %ecx,slot
					nFlags |= (uint32)((CPacket*)pMsg)->ReadType((uint8*)0) << 8;
				}

				// 0x1002215b  cmp %edi,0xc(%ebp) ; je 0x1002228f -- an
				// if/ELSE, not an early-out.  The zero-flags arm is laid
				// out AFTER the whole rest of the body (0x1002228f, past
				// the 0x1002228a fall-through jump), which is the shape
				// VC6 gives an else arm and not the one it gives a
				// `continue` guard.
				if (nFlags != 0)
				{
					if (nLen < 2)
						goto bad;
					if (pMsg->m_Unknown34 - pMsg->m_Unknown38 < 2)
						goto bad;

					nLen -= 2;

					// 0x1002218a  movzwl %ax,%eax -- the uint16 read widened
					// for the varargs push.
					sprintf(temp, "%08x ",
					        ((CPacket*)pMsg)->ReadType((uint16*)0));
					strcat(str, temp);

					// 0x100221b5  testb $0x1,0xc(%ebp)
					if (nFlags & 1)
					{
						// 0x100221bb: pushes "NEW " and jumps INTO the BAD
						// block's strcat tail at 0x100220c8, which falls
						// through to the loop head -- so this is a `continue`.
						strcat(str, "NEW ");
						continue;
					}

					// The six "(!%d)" arms below leave with `break` (out of the
					// record loop, whose exit is `goto done`), not `goto done`
					// directly: with `goto done` VC6 hosts the shared
					// sprintf+strcat tail on UUP's copy, with `break` it hosts
					// it on MI's (0x10022308..0x10022330), as shipped.
					//
					// 0x100221c5  mov 0xc(%ebp),%eax, then a chain of tests on
					// %al / %ax / %ah.  nFlags never exceeds 16 bits, so VC6
					// narrowed every test.
					if (nFlags & 0x20)                    // 0x100221c8 test $0x20,%al
					{
						sprintf(temp, "MI  (!%d)", nLen);
						strcat(str, temp);
						break;
					}

					if (nFlags & 8)                       // 0x100221d0 test $0x8,%al
					{
						strcat(str, "FLG ");

						if (nLen < 7)
							goto bad;
						if (pMsg->m_Unknown34 - pMsg->m_Unknown38 < 7)
							goto bad;

						nLen -= 7;

						nFlagWord = ((CPacket*)pMsg)->ReadType((uint16*)0);   // 0x1002220d
						nFlagByte = ((CPacket*)pMsg)->ReadType((uint8*)0);    // 0x10022218
						nFlagLong = ((CPacket*)pMsg)->ReadType((uint32*)0);   // 0x10022223

						// 0x10022239: the dword is pushed raw, the byte
						// movzbl'd and the word movzwl'd -- the argument
						// order that gives is (word, byte, dword).
						sprintf(temp, "%04hx %02hx %08x ",
						        nFlagWord, nFlagByte, nFlagLong);
						strcat(str, temp);
					}

					if (nFlags & 0x40)                    // 0x1002225d test $0x40,%al
					{
						sprintf(temp, "CLR (!%d)", nLen);
						strcat(str, temp);
						break;
					}

					if (nFlags & 0x10)                    // 0x10022265 test $0x10,%al
					{
						sprintf(temp, "SCL (!%d)", nLen);
						strcat(str, temp);
						break;
					}

					if (nFlags & 0x202)                   // 0x1002226d test $0x202,%ax
					{
						sprintf(temp, "POS (!%d)", nLen);
						strcat(str, temp);
						break;
					}

					if (nFlags & 0x404)                   // 0x10022277 test $0x404,%ax
					{
						sprintf(temp, "ROT (!%d)", nLen);
						strcat(str, temp);
						break;
					}

					if (nFlags & 0x100)                   // 0x10022281 test $0x1,%ah
					{
						sprintf(temp, "ATT (!%d)", nLen);
						strcat(str, temp);
						break;
					}

					// 0x1002228a  jmp 0x100220d6 -- fall off the end of the
					// body, back to the loop head.
				}
				else
				{
					if (nLen < 1)
						goto bad;
					if (pMsg->m_Unknown34 - pMsg->m_Unknown38 < 1)
						goto bad;

					nLen--;

					// 0x100222b4  movzbl %al,%eax ; sub %edi,%eax ; je ...
					// -- cases 0, 1, 3 and default, laid out as a compare
					// chain with the `push %ebx` for the default/REM pair
					// hoisted into it at 0x100222bf.
					// Each case carries its own strcat: VC6 merges the four
					// copies into the one at 0x100222eb and batches the
					// cleanup as `add $0x14` (a single strcat after the
					// switch gives `add $0xc` + two pops, 2 instructions long).
					switch (((CPacket*)pMsg)->ReadType((uint8*)0))
					{
						case 0:
							sprintf(temp, "SND (!%d)", nLen);
							strcat(str, temp);
							break;
						case 1:
							sprintf(temp, "STK (!%d)", nLen);
							strcat(str, temp);
							break;
						case 3:
							sprintf(temp, "REM (!%d)", nLen);
							strcat(str, temp);
							break;
						default:
							sprintf(temp, "UNK (!%d)", nLen);
							strcat(str, temp);
							break;
					}

					// 0x10022301  xor %ebx,%ebx ; jmp 0x100220d6 (head)
					nLen = 0;
				}
			}

			goto done;

		// ------------------------------------------------------------
		//  0x0a -- the unguaranteed update packet.  0x1002235d.  The same
		//  sub-packet loop as UPD, and then just the leftover count.
		// ------------------------------------------------------------
		case 0x0a:
			strcat(str, "UUP ");

			nLen--;                                       // 0x10022369

			while (nLen)                                  // 0x10022370 / 0x10022440
			{
				if (pMsg->m_Unknown34 - pMsg->m_Unknown38 == 0)   // 0x1002237e je
					break;

				nLen--;

				// 0x10022393 -> 0x100138d6.  This is read #15, so it goes
				// through the template (see THE SIXTEEN READS) even though
				// the UPD twin above uses ReadByte.  Byte-identical here;
				// the difference only shows in lithtech.exe.
				nSubByte = ((CPacket*)pMsg)->ReadType((uint8*)0);

				if (nSubByte == 0)                        // 0x10022398
					break;

				nSubLen = nSubByte;                       // 0x100223a3

				if (nLen < nSubLen)                                       // 0x100223a6 jb
				{
					strcat(str, "BAD ");
					break;
				}
				if (pMsg->m_Unknown34 - pMsg->m_Unknown38 < (int)nSubLen)  // 0x100223bb jl
				{
					strcat(str, "BAD ");
					break;
				}

				{
					CMessageRef  ref;

					ref.Init(msg_AllocPacket(&pIOUup, 0x1000, 0x1000));   // 0x100223c3
					if (pIOUup)
						pIOUup->Release();                                // 0x100223de

					ref.m_pMsg->Init(nSubByte, 0x1000);                // 0x100223f8

					nLen -= nSubLen;                                   // 0x10022403

					((ServerMessage*)pMsg)->ReadData(
						ref.m_pMsg->m_pData, nSubByte);                // 0x1002240b

					ref.m_pMsg->m_Unknown34 = nSubByte;                // 0x1002241c
					ref.m_pMsg->m_Unknown38 = 1;                       // 0x10022423

					net_LogPacket(ref.m_pMsg, nSubLen, 0);             // 0x1002242c
				}
			}

			// 0x1002245d: push nLen, push "(!%d) ", jmp to the shared
			// sprintf+strcat at 0x1002230e, which exits to the tail.
			sprintf(temp, "(!%d) ", nLen);
			strcat(str, temp);
			goto done;

		case 0x0c:                                // 0x10022468
			strcat(str, "YID ");
			break;

		// ------------------------------------------------------------
		//  0x0d -- a game message.  0x10022472.  The LAST unread payload
		//  byte is the message class, and for class 'g' the next byte
		//  READ (not the last one) is the sub-type.
		// ------------------------------------------------------------
		case 0x0d:
			strcat(str, "MSG ");

			// 0x10022485  push $1 ; pop %edi -- the constant 1 again, and
			// it stays 1 for the PIC guards below.
			if (nLen < 1)
				goto bad;
			if (pMsg->m_Unknown34 - pMsg->m_Unknown38 < 1)
				goto bad;

			nLen--;

			// 0x100224a2  mov 0x54(%esi),%eax ; dec %ebx ;
			//             movzbl (%ebx,%eax,1),%eax
			// -- indexed with the DECREMENTED count, so it is the last
			// unread byte, and the read does NOT move the cursor.
			nMsgClass = pMsg->m_pData[nLen];

			// 0x100224ac  sub $0x67,%ecx ; je   -- 0x67 == 'g'
			// 0x100224b1  sub $0x6d,%ecx ; je   -- 0x67 + 0x6d == 0xd4
			switch (nMsgClass)
			{
				// --------------------------------------- 0x100224ee
				case 0x67:
					strcat(str, "PIC ");

					// The guard is ONE positive test with the refusal as its
					// `else`, not two `goto bad` refusals: that is what puts
					// the lone "BAD " block at 0x100225ec, after the INT arm,
					// where the shipped build has it (two refusals lay it out
					// straight after the guard -- 4 bytes short).
					//
					// NO `nLen--` here -- 0x1002251b reads the sub-type
					// without touching %ebx.  Shipped in both images.
					// 0x10022527  dec %eax ; cmp $0x11 ; ja UNK ;
					//             jmp *0x1002277d(,%eax,4)
					// so the table covers 1..18 and 0 falls to UNK.
					if (nLen >= 1 && pMsg->m_Unknown34 - pMsg->m_Unknown38 >= 1)
					{
						switch (((CPacket*)pMsg)->ReadType((uint8*)0))
						{
							case 1:  strcat(str, "AMO "); break;   // 0x10022538
							case 2:  strcat(str, "HEA "); break;   // 0x10022542
							case 3:  strcat(str, "ARM "); break;   // 0x1002254c
							case 4:  strcat(str, "WEA "); break;   // 0x10022556
							case 5:  strcat(str, "WPI "); break;   // 0x10022560
							case 6:  strcat(str, "AIR "); break;   // 0x1002256a
							case 7:  strcat(str, "ROC "); break;   // 0x10022574
							case 8:  strcat(str, "OUT "); break;   // 0x1002257e
							case 9:  strcat(str, "OBJ "); break;   // 0x10022588
							case 10: strcat(str, "MOD "); break;   // 0x10022592
							case 11: strcat(str, "FAD "); break;   // 0x1002259c
							case 12: strcat(str, "INV "); break;   // 0x100225a6
							case 13: strcat(str, "MTX "); break;   // 0x100225b0
							case 14: strcat(str, "MFA "); break;   // 0x100225ba
							case 15: strcat(str, "MXH "); break;   // 0x100225c4
							case 16: strcat(str, "MXA "); break;   // 0x100225ce
							case 17: strcat(str, "WOI "); break;   // 0x100225d8
							case 18: strcat(str, "INT "); break;   // 0x100225e2
							default: strcat(str, "UNK "); break;   // 0x10022681
						}
					}
					else
						goto bad;
					break;

				// --------------------------------------- 0x100224e4
				case 0xd4:
					strcat(str, "PHU ");
					break;

				// --------------------------------------- 0x100224b6
				default:
					// push %ebx ; push %eax ; push "%2x LEN %d " --
					// so the order is (class, nLen), and the 0x18 pop at
					// 0x100224dc accounts for sprintf's four and strcat's
					// two dwords.
					sprintf(temp, "%2x LEN %d ", nMsgClass, nLen);
					strcat(str, temp);
					goto done;
			}
			break;

		case 0x0e:                                // 0x100225f6, same literal as NM's GRP
			strcat(str, "GRP ");
			break;

		case 0x0f:                                // 0x10022600
			strcat(str, "COF ");
			break;

		case 0x10:                                // 0x1002260a
			strcat(str, "CV  ");
			break;

		case 0x11:                                // 0x10022614 (the `je` arm)
			strcat(str, "SKY ");
			break;

		case 0x12:                                // 0x1002265e
			strcat(str, "ISE ");
			break;

		case 0x13:                                // 0x10022657
			strcat(str, "PF  ");
			break;

		case 0x15:                                // 0x10022650
			strcat(str, "PLL ");
			break;

		case 0x17:                                // 0x10022649
			strcat(str, "THL ");
			break;

		case 0x18:                                // 0x10022642
			strcat(str, "UL  ");
			break;

		case 0x19:                                // 0x1002263b
			strcat(str, "LMA ");
			break;

		case 0x1a:                                // 0x10022665
			strcat(str, "GL  ");
			break;

		case 0x32:                                // 0x100226ab
			strcat(str, "FD  ");
			break;

		case 0x33:                                // 0x100226a4
			strcat(str, "ST  ");
			break;

		case 0x34:                                // 0x1002269d
			strcat(str, "CFT ");
			break;

		case 0x35:                                // 0x10022696
			strcat(str, "FB  ");
			break;

		case 0x36:                                // 0x1002268f
			strcat(str, "FS  ");
			break;

		case 0x38:                                // 0x10022688
			strcat(str, "GYS ");
			break;

		default:                                  // 0x10022681
			strcat(str, "UNK ");
			break;
	}

	goto done;

bad:
	// 0x100225ec -- one shared arm; every `goto bad` above is a direct
	// branch to it.
	strcat(str, "BAD ");

done:
	// 0x100226be.  Three sequential filters over the name just built, each
	// skipped when its bit is set in the trace variable; &str[2] steps past
	// the direction pair.  The three `jz` targets are all 0x10022737, the
	// bare `leave ; ret`.
	//
	// The register restores (pop %edi/%esi/%ebx) are scheduled at
	// 0x100226c5, between the first `testb` and its `jne` -- nothing in
	// source corresponds to that.
	if (!(g_NetLogPackets & 2) && strncmp(&str[2], "UUP", 3) == 0)   // 0x100226be
		return;

	if (!(g_NetLogPackets & 4) && strncmp(&str[2], "UPD", 3) == 0)   // 0x100226e4
		return;

	if (!(g_NetLogPackets & 8) && strncmp(&str[2], "NM", 2) == 0)    // 0x10022707
		return;

	// 0x10022731 -> 0x1000e3f9.  Same prototype question as the in-loop
	// print above; alternative spelling con_Printf(str).
	dsi_PrintString(str);
}

// The transport boundary carries engine events through actual C++ types.
bool CNetMgr::accept_connection(NetConnection& connection) { return AddConnection(&connection) != LTFALSE; }
void CNetMgr::close_connection(NetConnection& connection) {
    CloseConnection(&connection);
    RemoveConnection(&connection);
}
void CNetMgr::release_buffers(NetConnection& connection) noexcept {
    TermQueue(&connection.m_GuaranteedQueue);
    TermQueue(&connection.m_SendQueue);
    TermQueue(&connection.m_RecvList);
    netbank_FreeMessages(&connection.m_SentList, &m_PacketBank);
}
void CNetMgr::flush_connection(NetConnection& connection) { sub_1002294f(&connection); }
void CNetMgr::connectionless_packet(ServerMessage& message, const nolf::platform::Endpoint& endpoint) {
    if (!m_pHandler) return;
    auto bytes = nolf::platform::ipv4_bytes(endpoint.address);
    m_pHandler->ProcessPacket(static_cast<CPacket*>(&message), bytes.data(), endpoint.port);
}
void CNetMgr::remote_disconnect(uint32 reason) {
    if (m_pHandler) m_pHandler->_v10(4, g_EmptyString, reason);
}
LTRESULT CNetMgr::GetSessionList(NetSession*& list, char* addresses) {
    list = nullptr;
    return m_pMainDriver ? m_pMainDriver->GetSessionList(&list, addresses) : LT_NOTINITIALIZED;
}
LTRESULT CNetMgr::FreeSessionList(NetSession* list) {
    while (list) { auto* next = list->m_pNext; delete list; list = next; }
    return LT_OK;
}
CBaseDriver* CNetMgr::FindDriver(char* name) {
    if (!name) return nullptr;
    for (uint32 i = 0; i < m_Drivers.m_nElements; ++i)
        if (std::strcmp(m_Drivers.m_pArray[i]->m_Name, name) == 0) return m_Drivers.m_pArray[i];
    return nullptr;
}

void CBaseMgr::_v10(int nEvent, char* pName, int n)
{
}
