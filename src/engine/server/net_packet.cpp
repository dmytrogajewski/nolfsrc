#include "net_packet.h"
#include <new>

CNetRecvPacket::~CNetRecvPacket() { Term(); }
void CNetRecvPacket::Term() {
    ::operator delete(m_pData);
    m_pData = nullptr; m_nAlloced = 0; m_Len = 0;
}
void CNetFragRec::Clear() {
    for (auto& fragment : m_pFrags) fragment.Assign(nullptr);
    m_ID = 0; m_pConn = nullptr;
}
LTBOOL CNetFragRec::Complete() const {
    for (const auto& fragment : m_pFrags) if (!fragment.m_pMsg) return LTFALSE;
    return LTTRUE;
}
LTBOOL CNetFragRec::AddFragment(ServerMessage& source, uint8 index) {
    if (index >= 4 || m_pFrags[index].m_pMsg || source.m_Pos > source.m_DataLen || source.m_DataLen > source.m_DataSize)
        return LTFALSE;
    const uint32 header = index == 0 ? 1 : 0;
    if (source.m_DataLen - source.m_Pos < header) return LTFALSE;
    const uint32 length = source.m_DataLen - source.m_Pos - header;
    CAbstractIO* owner = nullptr;
    auto* packet = msg_AllocPacket(&owner, length + 1, 0x1000);
    if (!owner) return LTFALSE;
    CMessageRef copy;
    copy.Init(packet); owner->Release();
    if (header) copy.m_pMsg->m_pData[0] = source.ReadByte(0);
    if (!copy.m_pMsg->WriteData(source.m_pData + source.m_Pos, length)) return LTFALSE;
    m_pFrags[index].Assign(copy.m_pMsg);
    return LTTRUE;
}
CNetBank::CNetBank() { sb_InitAligned(&m_Bank, sizeof(CNetSentPacket), 32, alignof(CNetSentPacket)); }
CNetBank::~CNetBank() { Term(); }
CNetSentPacket* CNetBank::Allocate() {
    auto* memory = sb_Allocate(&m_Bank);
    if (!memory) return nullptr;
    CNetSentPacket* packet = nullptr;
    try {
        packet = new (memory) CNetSentPacket;
        active_.insert(packet);
        return packet;
    } catch (const std::bad_alloc&) {
        if (packet) packet->~CNetSentPacket();
        sb_Free(&m_Bank, memory);
        return nullptr;
    }
}
void CNetBank::Free(void* memory) {
    auto* packet = static_cast<CNetSentPacket*>(memory);
    if (!active_.erase(packet)) return;
    packet->~CNetSentPacket();
    sb_Free(&m_Bank, memory);
}
void CNetBank::Term() {
    while (!active_.empty()) Free(*active_.begin());
    sb_Term(&m_Bank);
}
