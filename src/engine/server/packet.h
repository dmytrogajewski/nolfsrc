#ifndef __PACKET_H__
#define __PACKET_H__
#include "ltbasedefs.h"
#include "collection.h"
#include "engine/portable/wire.h"
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <vector>

#define PACKETFLAG_READOVERFLOW 2
#define PACKETFLAG_WRITEOVERFLOW 4

// Shared packet storage. Field widths describe the wire protocol; member
// locations are deliberately owned by the compiler on each native platform.
class CPacket {
public:
    uint16 m_DataLen = 1;
    uint16 m_MaxDataLen = 0xffff;
    uint16 m_Pos = 1;
    uint32 m_Flags = 0;
    CMoArray<uint8> m_Data;

    template<class T> T ReadType(T*) {
        T value{};
        if (CanRead(sizeof(T))) {
            value = nolf::wire::load<T>(m_Data.m_pArray + m_Pos);
            m_Pos += sizeof(T);
        } else m_Flags |= PACKETFLAG_READOVERFLOW;
        return value;
    }
    template<class T> void WriteType(T value) {
        if (CanWrite(sizeof(T)) && Reserve(m_Pos + sizeof(T))) {
            nolf::wire::store(m_Data.m_pArray + m_Pos, value);
            m_Pos += sizeof(T);
            m_DataLen = std::max(m_DataLen, m_Pos);
        } else m_Flags |= PACKETFLAG_WRITEOVERFLOW;
    }
    bool WriteData(const void* input, uint32 size) {
        if (!size) return true;
        if (!input || !CanWrite(size)) {
            m_Flags |= PACKETFLAG_WRITEOVERFLOW;
            return false;
        }
        // An internal payload may be copied to its own packet. Preserve it across
        // growth and permit overlap without relying on pointer ordering.
        const auto address = reinterpret_cast<std::uintptr_t>(input);
        const auto base = reinterpret_cast<std::uintptr_t>(m_Data.m_pArray);
        const bool internal = address >= base && address - base < m_Data.GetSize();
        std::vector<uint8> copy;
        if (internal) {
            if (size > m_Data.GetSize() - (address - base)) {
                m_Flags |= PACKETFLAG_WRITEOVERFLOW;
                return false;
            }
            const auto* bytes = static_cast<const uint8*>(input);
            copy.assign(bytes, bytes + size);
            input = copy.data();
        }
        if (!Reserve(m_Pos + size)) { m_Flags |= PACKETFLAG_WRITEOVERFLOW; return false; }
        std::memmove(m_Data.m_pArray + m_Pos, input, size);
        m_Pos += size;
        m_DataLen = std::max(m_DataLen, m_Pos);
        return true;
    }
    int GetSpaceLeft() const { return std::max(0, int(m_MaxDataLen) - int(m_Pos) - 7); }
    bool CanRead(uint32 size) const {
        const uint32 limit = std::min<uint32>(m_DataLen, m_Data.GetSize());
        return m_Pos <= limit && size <= limit - m_Pos;
    }
    bool CanWrite(uint32 size) const {
        return m_Pos <= m_MaxDataLen && size <= uint32(m_MaxDataLen) - m_Pos;
    }
    bool Reserve(uint32 size) {
        return size <= m_Data.GetSize() || m_Data.NiceSetSize(size);
    }
    void ReportWriteOverrun() { m_Flags |= PACKETFLAG_WRITEOVERFLOW; }
};
#endif
