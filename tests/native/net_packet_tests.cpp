#include "engine/server/net_packet.h"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
}
int main() {
    try {
        CNetBank bank;
        LMessageList sent;
        std::array<uint8, 4000> payload{};
        for (uint32 i = 0; i < 40; ++i) {
            auto* record = bank.Allocate();
            require(record, "native sent packet allocation");
            payload.fill(static_cast<uint8>(i));
            require(record->m_Packet.WriteData(payload.data(), payload.size()), "banked packet retains complete payload");
            sent.Append(record);
        }
        require(sent.m_nMessages == 40, "native network records share real queue links");
        uint32 count = 0;
        std::array<bool, 40> seen{};
        while (auto* node = sent.m_pTail) {
            auto* record = static_cast<CNetSentPacket*>(node);
            require(record->m_Packet.m_DataLen == 4001, "queued packet length survives pool growth");
            const auto tag = record->m_Packet.m_pData[1];
            require(tag < seen.size() && !seen[tag], "queued packet payloads remain distinct");
            seen[tag] = true;
            for (uint32 i = 2; i < record->m_Packet.m_DataLen; ++i)
                require(record->m_Packet.m_pData[i] == record->m_Packet.m_pData[1], "queued packets own independent storage");
            sent.Remove(node);
            bank.Free(record);
            ++count;
        }
        require(count == 40 && !sent.m_nMessages, "sent packet queue drains through typed destruction");
        require(bank.Allocate(), "sent packet storage can be reused");
        bank.Term(); bank.Term();
        require(bank.Allocate(), "sent packet bank can restart");
        {
            CNetRecvPacket receive;
            receive.m_pData = static_cast<uint8*>(::operator new(5000));
            receive.m_nAlloced = 5000; receive.m_Len = 4999;
            std::memset(receive.m_pData, 0x55, receive.m_Len);
            LMessageList queue;
            queue.Append(&receive);
            require(static_cast<CNetRecvPacket*>(queue.m_pTail)->m_pData[4998] == 0x55, "receive storage survives native queue operations");
            queue.Remove(&receive);
            receive.Term(); receive.Term();
            require(!receive.m_pData && !receive.m_nAlloced, "receive cleanup is repeatable");
        }
        CNetFragRec fragments;
        require(!fragments.Complete(), "new split packet is incomplete");
        for (uint8 index = 0; index < 4; ++index) {
            ServerMessage incoming;
            if (index == 0) incoming.m_Write.WriteByte(0x42);
            incoming.m_Write.WriteByte(uint8(0x20 + index));
            incoming.m_Write.WriteByte(uint8(0x30 + index));
            incoming.m_Pos = 1;
            require(fragments.AddFragment(incoming, index), "four-part transport fragment accepted");
            const auto* part = fragments.m_pFrags[index].m_pMsg;
            require(part && part->m_DataLen == 3 && part->m_pData[1] == 0x20 + index && part->m_pData[2] == 0x30 + index,
                    "fragment preserves legacy payload boundaries");
            if (index == 0) require(part->m_pData[0] == 0x42, "first fragment retains original packet type");
            require(!fragments.AddFragment(incoming, index), "duplicate fragment rejected");
            require(fragments.Complete() == (index == 3), "completion requires all four fragments");
        }
        ServerMessage malformed;
        require(!fragments.AddFragment(malformed, 255), "out-of-range fragment index rejected");
        fragments.Clear();
        require(!fragments.Complete() && !fragments.m_pFrags[0].m_pMsg, "fragment reset releases retained messages");
        require(!fragments.AddFragment(malformed, 0), "truncated fragment header rejected");
        malformed.m_Pos = 20;
        require(!fragments.AddFragment(malformed, 1), "fragment read cursor is bounded");
        std::cout << "native network packet storage, queue links, and fragment ownership passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
