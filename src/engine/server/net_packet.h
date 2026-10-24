#pragma once
#include "message.h"
#include "lthread.h"
#include "structbank.h"
#include <unordered_set>

class NetConnection;
// These are real queue elements; shared links are inherited rather than
// reconstructed as integer words at historical offsets.
class CNetRecvPacket : public LThreadMessage {
public:
    CNetRecvPacket() = default;
    ~CNetRecvPacket();
    CNetRecvPacket(const CNetRecvPacket&) = delete;
    CNetRecvPacket& operator=(const CNetRecvPacket&) = delete;
    void Term();
    uint8* m_pData = nullptr;
    int m_nAlloced = 0;
    uint32 m_Len = 0, m_Unknown14 = 0, m_ID = 0, m_bPending = 0;
};
class CNetSentPacket : public LThreadMessage {
public:
    float m_ResendTimer = 0;
    ServerMessage m_Packet;
};
class CNetFragRec : public LThreadMessage {
public:
    void Clear();
    void Term() { Clear(); }
    LTBOOL AddFragment(ServerMessage&, uint8 index);
    LTBOOL Complete() const;
    CMessageRef m_pFrags[4];
    union { uint32 m_ID = 0; uint32 m_PacketID; };
    union { NetConnection* m_pConn = nullptr; NetConnection* m_pConnection; };
};
using CNetPending = CNetFragRec;

class CNetBank {
public:
    CNetBank();
    ~CNetBank();
    CNetBank(const CNetBank&) = delete;
    CNetBank& operator=(const CNetBank&) = delete;
    CNetSentPacket* Allocate();
    void Free(void*);
    void Term();
    StructBank m_Bank;
private:
    std::unordered_set<CNetSentPacket*> active_;
};
