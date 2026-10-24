#pragma once
#include "counter.h"
#include "lthread.h"
#include <memory>

// A connected client and the network connection under it.
// A sliding rate window ({value, time} advanced per frame): constructed by
// 0x100263d2, aged by 0x100263e7; value/time is what the stats print (names
// ours).
class CRateTracker
{
public:
	CRateTracker();                          // 0x100263d2
	void Update(float fDelta);               // 0x100263e7
	void Rollover();                         // 0x10026413 (name ours)

	float m_Value;                           // +0x00
	float m_Time;                            // +0x04
	float m_Unknown08;                       // +0x08
};

// A connection's four packet queues are all LMessageList-shaped, and three
// of them are LMessageList; the awaiting-ack queue is a separate class of
// the same shape, which is what the constructor shows — one vtable address
// is loaded once and stored into three of the members, and the fourth is
// stored as its own immediate. Not derived from LMessageList: a derived
// class would construct its base first and put the zeros before the vtable,
// and the shipped order is the other way round (name ours).
// The same twelve-slot queue as LMessageList, and the same two overrides:
// its vtable (0x1004dcfc) names the base's ten bodies and its own GenAppend
// at 0x10023f72 and GenAppendList at 0x10023f87. No virtual destructor --
// slot 0 there is GenBegin.
using CNetSentQueue = LMessageList;

class NetConnection
{
public:
    // Independent of driver ownership: admission callbacks may synchronously
    // disconnect and destroy this connection.
    std::shared_ptr<bool> m_Lifetime = std::make_shared<bool>(true);
    bool m_Admitting = false;
	NetConnection();
    ~NetConnection();
    NetConnection(const NetConnection&) = delete;
    NetConnection& operator=(const NetConnection&) = delete;                         // 0x10021200
	void Term();                             // 0x1002126a (name ours)

	// 0x1002132a — has the far end gone quiet for too long? Silent while
	// the connection is still coming up (name ours).
	LTBOOL CheckAckTimeout();

	CRateTracker  m_SendBPS;                 // +0x00  the stats print fixes the
	CRateTracker  m_SendPPS;                 // +0x0c  pairings: BPS first in each
	CRateTracker  m_RecvBPS;                 // +0x18  push sequence
	CRateTracker  m_RecvPPS;                 // +0x24
	class CBaseDriver* m_pDriver;            // +0x30
	LMessageList  m_SendQueue;               // +0x34  count printed as "Q Size"
	LMessageList  m_GuaranteedQueue;         // +0x40  arrived out of order
	CNetSentQueue m_SentList;                // +0x4c  awaiting ack; resent on a timer
	LMessageList  m_RecvList;                // +0x58  arrived, not yet handed out
	uint32        m_Unknown64;               // +0x64
	uint32        m_Unknown68;               // +0x68
	uint32        m_Unknown6C;               // +0x6c
	uint32        m_Flags;                   // +0x70  bit 0 = ?, bit 2 = kill me
	union { uint32 m_RemoteAckID = 0; uint32 m_nSent; };
	uint32        m_nResent = 0;
	union { uint32 m_PacketID = 0; uint32 m_Unknown7C; };
	uint32        m_Unknown80 = 0;
	float         m_RecvWait;                // +0x84  doubles as the timeout clock
	float         m_SendWait;                // +0x88
	uint32        m_nPacketsRecv = 0, m_Unknown90 = 0;
	float         m_AckTimer;                // +0x94
	float         m_Unknown98;               // +0x98
	float         m_Ping;                    // 0x9c
	float         m_UnknownA0[3]{};
	CBaseCounter  m_Counter;                 // +0xac  constructed with mode 0
	float         m_UnknownB4;               // +0xb4  counts down per frame
    uint16        m_UnknownB8 = 0;
};

