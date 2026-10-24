#include "engine/server/net_manager.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

uint16 net_Checksum(uint8*, uint16);

namespace {
using namespace std::chrono_literals;
void require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
class Handler final : public CBaseMgr {
public:
    std::vector<NetConnection*> connections;
    unsigned accepted = 0, closed = 0;
    bool allow = true;
    std::function<void(NetConnection*)> on_accept;
    LTBOOL _v04(NetConnection* connection, uint32) override {
        ++accepted;
        if (!allow) return LTFALSE;
        connections.push_back(connection);
        if (const auto hook = on_accept) hook(connection);
        return LTTRUE;
    }
    void _v08(NetConnection* connection) override {
        const auto it = std::find(connections.begin(), connections.end(), connection);
        require(it != connections.end(), "manager closes a registered connection once");
        connections.erase(it); ++closed;
    }
    void ProcessPacket(CPacket*, uint8*, uint16) override {}
};
struct Pair {
    Handler left_handler, right_handler;
    CNetMgr left, right;
    float time = 0;
    Pair() {
        left.SetNetName(const_cast<char*>("left")); right.SetNetName(const_cast<char*>("right"));
        left.m_pHandler = &left_handler; right.m_pHandler = &right_handler;
        LTGUID guid{0x12345678, 12, 34, {0,1,2,3,4,5,6,7}};
        left.SetGuid(&guid); right.SetGuid(&guid);
    }
    void update() {
        time += 0.02f;
        left.Update(const_cast<char*>("left: "), time, 0);
        right.Update(const_cast<char*>("right: "), time, 0);
    }
};
void send(CNetMgr& manager, NetConnection* connection, uint16 sequence, uint32 size, bool reliable = true) {
    ServerMessage message;
    if (size > 4000) message.Init(static_cast<uint16>(size+64),0x1000);
    message.m_pData[0] = 10;
    message.WriteWord(sequence);
    std::vector<uint8> bytes(size, static_cast<uint8>(sequence));
    require(message.WriteData(bytes.data(), size), "build game packet payload");
    const auto length = message.m_DataLen, position = message.m_Pos;
    require(manager.SendToConnection(&message, connection, reliable ? 0x80 : 0), "manager sends game packet through actual driver");
    require(message.m_DataLen == length && message.m_Pos == position && message.m_pData[0] == 10, "send restores caller's packet cursor, length and ID");
}
uint16 receive(CNetMgr& manager, uint32 size) {
    ServerMessage message;
    require(manager.GetPacket(&message), "manager delivers game packet");
    require((message.m_pData[0] & 0x3f) == 10 && message.m_DataLen == size + 3 && message.m_Pos == 1,
            "transport framing is stripped from delivered game packet");
    const auto sequence = message.ReadWord(nullptr);
    for (uint32 i = 0; i < size; ++i) require(message.ReadByte(0) == static_cast<uint8>(sequence), "delivered game payload is complete");
    return sequence;
}
void local() {
    Pair pair;
    auto* left = pair.left.AddDriver(const_cast<char*>("local"));
    auto* right = pair.right.AddDriver(const_cast<char*>("local"));
    require(left && right, "manager owns native local drivers");
    left->Connect(right);
    pair.update();
    require(pair.left_handler.connections.size() == 1 && pair.right_handler.connections.size() == 1, "local manager callbacks register both endpoints");
    for (uint16 i = 0; i < 30; ++i) send(pair.left, pair.left_handler.connections[0], i, 1000, i % 2 == 0);
    for (uint16 i = 0; i < 30; ++i) require(receive(pair.right, 1000) == i, "local manager preserves game message order");
    ServerMessage empty;
    require(!pair.right.GetPacket(&empty), "local manager queues drain completely");
    pair.left.Disconnect(pair.left_handler.connections[0], 1, 0);
    pair.update();
    require(pair.left_handler.connections.empty() && pair.right_handler.connections.empty(), "manager detaches native local endpoints");
}
void malformed() {
    Pair pair;
    auto* left = pair.left.AddDriver(const_cast<char*>("local"));
    auto* right = pair.right.AddDriver(const_cast<char*>("local"));
    left->Connect(right); pair.update();
    auto* connection = pair.left_handler.connections[0];
    connection->m_Flags = 0;
    send(pair.left, connection, 99, 100);
    require(connection->m_SendQueue.m_nMessages == 1, "remote reliable packet remains retained until acknowledged");
    const auto inject = [&](std::vector<uint8> bytes) {
        ServerMessage packet;
        if (!bytes.empty()) std::memcpy(packet.m_pData, bytes.data(), bytes.size());
        packet.m_DataLen = static_cast<uint16>(bytes.size()); packet.m_Pos = 1;
        packet.m_pConnection = connection;
        return pair.left.TranslatePacket(&packet, connection, 1);
    };
    require(!inject({1,0,0}) && connection->m_SendQueue.m_nMessages == 1, "truncated acknowledgement cannot release pending game data");
    require(!inject({1,0,0,99,0,0,0,0,0,0,0}) && connection->m_SendQueue.m_nMessages == 1, "acknowledgement cannot claim an unsent sequence number");
    require(!inject({1,3,0,0}) && !connection->m_RecvList.m_nMessages, "zero-length grouped packet is rejected");
    require(!inject({1,3,5,0,10}) && !connection->m_RecvList.m_nMessages, "truncated group is rejected before queue mutation");
    require(!inject({1,4,0x3f,0,0,0,0}) && !pair.left.m_QueueC4.m_nMessages, "invalid fragment index does not allocate reassembly state");
    require(!inject({0x8a}) && !inject({}), "missing packet header and guaranteed trailer are bounded");
    uint32 random = 0x12345678;
    for (unsigned i = 0; i < 5000; ++i) {
        random = random * 1664525 + 1013904223;
        std::vector<uint8> bytes(2 + (random & 63));
        bytes[0] = 1;
        const std::array<uint8,4> subtypes{0,2,3,4};
        bytes[1] = subtypes[(random >> 8) & 3];
        for (std::size_t n = 2; n < bytes.size(); ++n) { random = random * 1664525 + 1013904223; bytes[n] = static_cast<uint8>(random >> 24); }
        if (bytes[1] == 3 && bytes.size() >= 4) bytes[2] = bytes[3] = 0xff;
        require(!inject(std::move(bytes)), "internal transport packets never escape as game messages");
    }
    connection->m_Flags = 1;
}
void udp() {
    Pair pair;
    auto* left = pair.left.AddDriver(const_cast<char*>("internet"));
    auto* right = pair.right.AddDriver(const_cast<char*>("internet"));
    require(left && right, "manager owns native UDP drivers");
    NetHost info{};
    { nolf::platform::UdpSocket unused; info.m_Port = unused.port(); }
    info.m_dwMaxPlayers = 4;
    std::strcpy(info.m_sName, "Manager integration");
    require(left->HostSession(&info) == LT_OK, "manager's UDP driver hosts game");
    const auto address = "127.0.0.1:" + std::to_string(info.m_Port);
    pair.left_handler.on_accept = [&](NetConnection* connection) {
        require(connection->m_Admitting, "admission has explicit pending state");
        send(pair.left,connection,123,1000);
        require(connection->m_SendQueue.m_nMessages == 1, "initial reliable game message is owned until transport admission finishes");
    };
    std::jthread accept([&](std::stop_token stop) {
        ServerMessage message;
        while (!stop.stop_requested()) { pair.left.GetPacket(&message); std::this_thread::sleep_for(1ms); }
    });
    const auto joined = right->ConnectTCP(address.c_str());
    accept.request_stop(); accept.join();
    require(joined == LT_OK && pair.left_handler.connections.size() == 1 && pair.right_handler.connections.size() == 1, "two real managers complete native UDP handshake");
    require(receive(pair.right,1000) == 123, "reliable game message queued by admission arrives after the transport greeting");
    // Guaranteed packets cross the fragment threshold and must reassemble
    // before being delivered in sequence by the manager.
    for (uint16 i = 0; i < 8; ++i) send(pair.right, pair.right_handler.connections[0], i, 3000);
    for (uint16 i = 0; i < 8; ++i) require(receive(pair.left, 3000) == i, "UDP manager reassembles guaranteed game packets in order");
    for (auto size : {20000u,65000u}) {
        send(pair.right,pair.right_handler.connections[0],321,size);
        require(receive(pair.left,size) == 321,"large reliable payloads retain all four fragments beyond the old fixed 4096-byte fragment buffer");
    }
    // Let acknowledgement traffic settle and verify retained send buffers
    // return to the manager rather than leaking across disconnects.
    ServerMessage message;
    for (unsigned i = 0; i < 40; ++i) {
        pair.update();
        while (pair.left.GetPacket(&message)) {}
        while (pair.right.GetPacket(&message)) {}
    }
    require(!pair.right_handler.connections[0]->m_SendQueue.m_nMessages, "acknowledgements release retained guaranteed packets");
    pair.right.Disconnect(pair.right_handler.connections[0], 7, 0);
    pair.left.GetPacket(&message);
    require(pair.left_handler.connections.empty() && pair.right_handler.connections.empty(), "native manager disconnect removes peer and pending fragments");
}
void admission_disconnect() {
    Pair pair;
    auto* driver = pair.left.AddDriver(const_cast<char*>("internet"));
    NetHost host{};
    { nolf::platform::UdpSocket unused; host.m_Port = unused.port(); }
    host.m_dwMaxPlayers = 2;
    std::strcpy(host.m_sName,"Admission cancellation");
    require(driver && driver->HostSession(&host) == LT_OK,"host admission cancellation fixture");
    pair.left_handler.on_accept = [&](NetConnection* connection) { pair.left.Disconnect(connection,6,0); };
    nolf::platform::UdpSocket peer;
    const std::array<std::byte,19> request{
        std::byte{0},std::byte{3},std::byte{0},
        std::byte{0x78},std::byte{0x56},std::byte{0x34},std::byte{0x12},
        std::byte{12},std::byte{0},std::byte{34},std::byte{0},
        std::byte{0},std::byte{1},std::byte{2},std::byte{3},std::byte{4},std::byte{5},std::byte{6},std::byte{7}};
    peer.send({"127.0.0.1",static_cast<uint16>(host.m_Port)},request);
    ServerMessage message;
    const auto deadline = std::chrono::steady_clock::now()+1s;
    while (!pair.left_handler.closed && std::chrono::steady_clock::now() < deadline) {
        pair.left.GetPacket(&message);
        std::this_thread::sleep_for(1ms);
    }
    require(pair.left_handler.accepted == 1 && pair.left_handler.closed == 1 &&
        pair.left_handler.connections.empty() && pair.left.m_Connections.GetSize() == 0,
        "disconnect during admission retires exactly that connection without removing another array element");
    require(!pair.left.GetPacket(&message),"packet pump remains valid after admission destroys its connection");
}
void initialization() {
    CNetMgr manager;
    require(manager.Init() == LT_OK && manager.FindDriver(const_cast<char*>("local")) &&
        manager.FindDriver(const_cast<char*>("internet")) && manager.m_Drivers.GetSize() == 2,
        "default initialization supplies the real native local and internet adapters");
    require(manager.Init() == LT_OK && manager.m_Drivers.GetSize() == 2,"network initialization replaces its adapters on restart");
}
void reordered_acknowledgements() {
    Pair pair;
    auto* left = pair.left.AddDriver(const_cast<char*>("local"));
    auto* right = pair.right.AddDriver(const_cast<char*>("local"));
    left->Connect(right); pair.update();
    auto* connection = pair.left_handler.connections.at(0);
    connection->m_Flags = 0;
    const auto inject = [&](uint32 sequence) {
        ServerMessage message;
        message.m_pData[0] = 0x8a;
        message.WriteWord(static_cast<uint16>(sequence)); message.WriteDWord(sequence); message.m_Pos = 1;
        return pair.left.TranslatePacket(&message,connection,1);
    };
    const auto ack = [&](uint32 received,uint32 sent) {
        ServerMessage message; message.m_pData[0] = 1;
        message.WriteByte(0); message.WriteByte(0); message.WriteDWord(received); message.WriteDWord(sent); message.m_Pos = 1;
        require(!pair.left.TranslatePacket(&message,connection,1),"transport ACK remains internal");
    };
    for (uint32 sequence : {3u,1u,2u}) require(!inject(sequence),"future reliable packet waits for its predecessor");
    require(connection->m_GuaranteedQueue.m_nMessages == 3,"out-of-order payloads have independent retained owners");
    ServerMessage requested;
    pair.left.WriteAckPacket(connection,&requested);
    require(requested.m_DataLen == 15 && nolf::wire::load<uint32>(requested.m_pData+11) == 0,
        "negative acknowledgement requests only the missing predecessor, not queued frames");
    ack(0xffffffff,1);
    require(connection->m_GuaranteedQueue.m_nMessages == 3 && connection->m_Unknown80 >= 4,
        "stale ACK cannot discard received frames or move the known remote frontier backwards");
    require(inject(0),"missing predecessor becomes visible immediately");
    for (uint16 sequence : {1,2,3}) require(receive(pair.left,0) == sequence,"queued reliable payloads resolve in order after a reordered ACK");
    for (uint16 sequence : {0,1,2}) send(pair.left,connection,sequence,10);
    for (uint32 sequence : {0u,1u,2u}) require(pair.left.FindQueuedPacket(&connection->m_SendQueue,sequence),
        "resend lookup can find every retained packet in the circular queue");
    ack(1,4);
    require(connection->m_SendQueue.m_nMessages == 1 && pair.left.FindQueuedPacket(&connection->m_SendQueue,2),
        "acknowledgement retires the complete acknowledged prefix and preserves later frames");
}
}
int main() {
    try {
        std::array<uint8,4> checksum{0xaa,0x34,0x12,0xff};
        require(net_Checksum(checksum.data() + 1, 3) == 0x1233, "checksum retains Windows word order and signed odd-byte behavior on unaligned input");
        initialization(); local(); malformed(); udp(); admission_disconnect(); reordered_acknowledgements(); std::cout << "native managers deliver ordered local and fragmented reliable UDP game packets\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
