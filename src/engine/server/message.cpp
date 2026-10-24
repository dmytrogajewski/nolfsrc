#include "message.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <memory>
#include <vector>

void CAbstractIO::AddRef() { ++m_nRefCount; }
void CAbstractIO::Release() {
    assert(m_nRefCount > 0);
    if (--m_nRefCount == 0) _v0c();
}
void CAbstractIO::_v0c() { delete this; }

ServerMessage::ServerMessage() { Init(0x1000, 0x1000); }
void ServerMessage::Init(uint16 maxLen, uint16 growSize) {
    m_Write.m_pOwner = this;
    m_pConnection = nullptr;
    m_DataLen = m_Pos = 1; // Byte zero is reserved for the packet id.
    m_MaxDataLen = std::max<uint16>(1, maxLen);
    m_Flags = 0;
    m_Data.SetCacheSize(growSize);
    if (!Reserve(m_MaxDataLen)) m_Flags |= PACKETFLAG_WRITEOVERFLOW;
    else std::memset(m_pData, 0, m_DataSize);
}
void* msg_AllocPacket(CAbstractIO** output, uint32 maxLen, uint32 growSize) {
    if (!output) return nullptr;
    *output = nullptr;
    if (maxLen == 0 || maxLen > 0xffff || growSize > 0xffff) return output;
    auto* msg = new(std::nothrow) ServerMessage;
    if (!msg) return output;
    msg->Init(uint16(maxLen), uint16(growSize));
    if (msg->m_Flags & PACKETFLAG_WRITEOVERFLOW) { delete msg; return output; }
    msg->AddRef();
    *output = msg;
    return output;
}
LTRESULT CMessageRef::CopyFrom(const ServerMessage& source, uint32 offset) {
    if (source.m_DataLen < offset || source.m_DataLen > source.m_DataSize) return LT_INVALIDPARAMS;
    try {
        auto copy = std::make_unique<ServerMessage>();
        const auto length = source.m_DataLen - offset;
        copy->Init(static_cast<uint16>(std::max<uint32>(1, length)), 0x1000);
        if (copy->m_Flags & PACKETFLAG_WRITEOVERFLOW) return LT_OUTOFMEMORY;
        if (length) std::memcpy(copy->m_pData, source.m_pData + offset, length);
        copy->m_DataLen = static_cast<uint16>(length);
        Assign(copy.release());
        return LT_OK;
    } catch (const std::bad_alloc&) { return LT_OUTOFMEMORY; }
}
bool ServerMessage::ReadData(void* output, uint32 size) {
    if (!size) return true;
    if (!output || !CanRead(size)) {
        m_Flags |= PACKETFLAG_READOVERFLOW;
        if (output) std::memset(output, 0, size);
        return false;
    }
    std::memmove(output, m_pData + m_Pos, size);
    m_Pos += size;
    return true;
}
bool ServerMessage::WriteData(const void* input, uint32 size) { return CPacket::WriteData(input, size); }
uint8 ServerMessage::ReadByteFromEnd(uint8) {
    if (m_Pos >= m_DataLen || m_DataLen > m_DataSize) return 0;
    return m_pData[--m_DataLen];
}
char* ServerMessage::ReadStringInPlace() {
    if (!CanRead(1)) { m_Flags |= PACKETFLAG_READOVERFLOW; return nullptr; }
    auto* start = reinterpret_cast<char*>(m_pData + m_Pos);
    const auto size = std::min<uint32>(m_DataLen, m_DataSize) - m_Pos;
    auto* end = static_cast<char*>(std::memchr(start, 0, size));
    if (!end) { m_Flags |= PACKETFLAG_READOVERFLOW; return nullptr; }
    m_Pos += uint16(end - start + 1);
    return start;
}
ServerMessage* CMsgCursor::Init(ServerMessage* next) {
    if (next) next->AddRef();
    if (m_pMsg) m_pMsg->Release();
    return m_pMsg = next;
}
void lmsg_AfterRead(LMessageImpl* msg) {
    auto& packet = *msg->m_pOwner;
    if (packet.m_Pos >= packet.m_DataLen && (packet.m_Flags & 1)) packet.m_Pos = 1;
}
LTRESULT LMessageImpl::Verify() const {
    return m_pOwner && !(m_pOwner->m_Flags & PACKETFLAG_WRITEOVERFLOW) ? LT_OK : LT_NOTINITIALIZED;
}
