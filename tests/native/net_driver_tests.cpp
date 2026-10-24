#include "engine/server/net_driver.h"
#include "engine/server/net_packet.h"
#include "engine/server/message.h"
#include "engine/portable/wire.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <functional>
#include <memory>
#include <stdexcept>
#include <thread>

extern char* g_pBindAddress;

namespace {
using namespace std::chrono_literals;
using nolf::platform::UdpSocket;
using nolf::platform::Endpoint;
using Bytes = std::vector<std::byte>;
using Driver = std::unique_ptr<CBaseDriver>;
void require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
struct Host final : NetDriverHost {
    LTGUID guid{0x12345678, 0x1234, 0xabcd, {0,1,2,3,4,5,6,7}};
    std::vector<NetConnection*> connections;
    CNetBank sent;
    unsigned accepted = 0, closed = 0, released = 0, flushed = 0, connectionless = 0;
    uint32 reason = 0;
    bool allow = true;
    std::function<void(NetConnection&)> on_accept, on_close;
    Bytes unknown;
    Endpoint unknown_peer{};
    LTGUID application_guid() const override { return guid; }
    bool accept_connection(NetConnection& connection) override {
        ++accepted;
        if (allow) {
            connections.push_back(&connection);
            if (on_accept) on_accept(connection);
        }
        return allow;
    }
    void close_connection(NetConnection& connection) override {
        ++closed;
        const auto it = std::find(connections.begin(), connections.end(), &connection);
        require(it != connections.end(), "close callback identifies a live accepted connection");
        connections.erase(it);
        if (on_close) on_close(connection);
    }
    void release_buffers(NetConnection& connection) noexcept override {
        ++released;
        for (auto* queue : {&connection.m_SendQueue, &connection.m_GuaranteedQueue, &connection.m_RecvList})
            while (auto* node = queue->m_pTail) { queue->Remove(node); delete static_cast<CNetRecvPacket*>(node); }
        while (auto* node = connection.m_SentList.m_pTail) { connection.m_SentList.Remove(node); sent.Free(node); }
    }
    void flush_connection(NetConnection&) override { ++flushed; }
    void connectionless_packet(ServerMessage& message, const Endpoint& peer) override {
        ++connectionless;
        unknown.assign(reinterpret_cast<std::byte*>(message.m_pData), reinterpret_cast<std::byte*>(message.m_pData + message.m_DataLen));
        unknown_peer = peer;
    }
    void remote_disconnect(uint32 value) override { reason = value; }
};
const std::array<std::byte, 16> windows_guid{
    std::byte{0x78},std::byte{0x56},std::byte{0x34},std::byte{0x12}, std::byte{0x34},std::byte{0x12},std::byte{0xcd},std::byte{0xab},
    std::byte{0},std::byte{1},std::byte{2},std::byte{3},std::byte{4},std::byte{5},std::byte{6},std::byte{7}};
Bytes join_request() {
    Bytes bytes{std::byte{0}, std::byte{3}, std::byte{0}};
    bytes.insert(bytes.end(), windows_guid.begin(), windows_guid.end());
    return bytes;
}
Endpoint loopback(uint16 port) { return {"127.0.0.1", port}; }
uint16 unused_port() { UdpSocket socket; return socket.port(); }
Endpoint host_game(CBaseDriver& driver, uint32 players = 4) {
    NetHost info{};
    info.m_Port = unused_port();
    info.m_dwMaxPlayers = players;
    std::strcpy(info.m_sName, "Native NOLF session");
    require(driver.HostSession(&info) == LT_OK, "host real UDP session");
    return loopback(static_cast<uint16>(info.m_Port));
}
std::optional<nolf::platform::Datagram> pump_reply(CBaseDriver& driver, UdpSocket& socket) {
    const auto deadline = std::chrono::steady_clock::now() + 500ms;
    ServerMessage message;
    do {
        driver.ReceivePacket(&message);
        if (auto packet = socket.receive(1ms)) return packet;
    } while (std::chrono::steady_clock::now() < deadline);
    return std::nullopt;
}
void expect_reply(CBaseDriver& driver, UdpSocket& socket, uint8 type) {
    auto packet = pump_reply(driver, socket);
    require(packet && packet->bytes == Bytes{std::byte{0},std::byte{type}}, "control response uses original two-byte format");
}
void drain(UdpSocket& socket) { while (socket.receive(0ms)) {} }
void sessions_free(NetSession* list) { while (list) { auto* next = list->m_pNext; delete list; list = next; } }
void test_host_wire() {
    Host host;
    Driver driver(tcp_Create());
    driver->set_host(&host);
    require(driver->Init(), "UDP driver initializes without engine layout views");
    NetService* service = nullptr;
    require(driver->GetServiceList(&service) == LT_OK && service && !service->m_pNext &&
            service->m_dwFlags == NETDRIVER_TCPIP && driver->SelectService(service->m_handle) == 0, "typed service handle selects owner");
    delete service;
    const auto address = host_game(*driver, 1);
    UdpSocket raw;
    Bytes query{std::byte{0},std::byte{1}};
    query.insert(query.end(), windows_guid.begin(), windows_guid.end());
    query.push_back(std::byte{0x17});
    raw.send(address, query);
    auto response = pump_reply(*driver, raw);
    const std::string name = "Native NOLF session";
    Bytes expected{std::byte{0},std::byte{2}};
    const auto* text = reinterpret_cast<const std::byte*>(name.c_str());
    expected.insert(expected.end(), text, text + name.size() + 1);
    expected.push_back(std::byte{0x17});
    require(response && response->bytes == expected, "Windows discovery fixture retains GUID layout, NUL string and ping slot");
    auto wrong_guid = join_request(); wrong_guid[3] = std::byte{0xff};
    raw.send(address, wrong_guid);
    expect_reply(*driver, raw, 7);
    auto request = join_request();
    raw.send(address, request);
    expect_reply(*driver, raw, 5);
    require(host.accepted == 1 && host.connections.size() == 1 && host.flushed == 1, "host accepts and flushes one initialized peer");
    drain(raw);
    raw.send(address, request);
    expect_reply(*driver, raw, 5);
    require(host.accepted == 1, "repeated join acknowledges without creating duplicate connection");
    UdpSocket second;
    second.send(address, request);
    expect_reply(*driver, second, 6);
    require(host.accepted == 1, "full session rejects before manager callback");
    Bytes data{std::byte{0x41},std::byte{0xa3},std::byte{0x57}};
    raw.send(address, data);
    ServerMessage message;
    require(driver->ReceivePacket(&message) && message.m_pConnection == host.connections[0] &&
            message.m_DataLen == data.size() && message.m_Pos == 1 && message.m_pData[2] == 0x57, "known peer data has correct native connection and cursor");
    second.send(address, data);
    require(!driver->ReceivePacket(&message) && host.connectionless == 1 && host.unknown == data &&
            host.unknown_peer == loopback(second.port()), "unknown peer data is dispatched with a real packet base and IPv4 address");
    data.resize(50000, std::byte{0x8b});
    require(driver->SendData(data.data(), static_cast<uint32>(data.size()), 0, host.connections[0]), "native UDP send retains large packet length");
    const auto delivered = raw.receive(500ms);
    require(delivered && delivered->bytes == data, "large outgoing datagram is complete");
    raw.send(address, data);
    require(driver->ReceivePacket(&message) && message.m_DataLen == data.size() && message.m_pData[49999] == 0x8b, "incoming packet expands storage instead of truncating to historical capacity");
    std::array<std::byte, 6> disconnect{};
    nolf::wire::store(disconnect.data() + 2, uint32{0x87654321});
    second.send(address, disconnect);
    driver->ReceivePacket(&message);
    require(host.connections.size() == 1, "unknown endpoint cannot disconnect an accepted peer");
    raw.send(address, disconnect);
    driver->ReceivePacket(&message);
    require(host.connections.empty() && host.closed == 1 && host.reason == 0x87654321, "host disconnect reads full little-endian reason and closes peer");
    require(host.released >= 1, "peer destruction releases host-owned packet queues");
    host.allow = false;
    raw.send(address, request);
    expect_reply(*driver, raw, 6);
    require(host.accepted == 2 && host.closed == 1, "rejected peer never receives accepted-close notification");
    char buffer[8]{};
    require(driver->GetSessionName(buffer, sizeof(buffer)) == LT_OK && std::strcmp(buffer, "Native ") == 0, "session name copy is bounded");
    require(driver->GetSessionName(buffer, 0) == LT_INVALIDPARAMS, "zero capacity session name is rejected");
    driver->Term(); driver->Term();
    struct BindGuard { char* previous = g_pBindAddress; ~BindGuard() { g_pBindAddress = previous; } } guard;
    char bind[] = "127.0.0.1";
    g_pBindAddress = bind;
    NetHost rehost{};
    rehost.m_Port = address.port; rehost.m_dwMaxPlayers = 2;
    std::strcpy(rehost.m_sName, "Rehost");
    require(driver->HostSession(&rehost) == LT_OK && driver->HostSession(&rehost) == LT_OK, "host can restart on its existing port without leaking the old socket");
    char advertised[32]{}; unsigned short port = 0;
    require(driver->GetTcpIpAddress(advertised, sizeof(advertised), port) == LT_OK && std::strcmp(advertised, bind) == 0 && port == address.port,
        "explicit bind interface is advertised with actual host port");
}
void test_query_wire() {
    Host host;
    Driver driver(tcp_Create()); driver->set_host(&host); driver->Init();
    UdpSocket responder;
    const auto target = "127.0.0.1:" + std::to_string(responder.port());
    require(driver->StartQuery(target.c_str()) == LT_OK, "start native direct query");
    const auto request = responder.receive(500ms);
    require(request && request->bytes.size() == 19 && request->bytes[0] == std::byte{0} && request->bytes[1] == std::byte{1} &&
            std::equal(windows_guid.begin(), windows_guid.end(), request->bytes.begin() + 2) && request->bytes[18] == std::byte{0}, "outgoing discovery matches Windows byte fixture");
    Bytes answer{std::byte{0},std::byte{2},std::byte{'S'},std::byte{'1'},std::byte{0},std::byte{0}};
    UdpSocket stranger;
    stranger.send(request->peer, answer);
    driver->UpdateQuery();
    NetSession* list = nullptr;
    require(driver->GetQueryResults(&list) == LT_OK && !list, "unsolicited discovery responses are ignored");
    responder.send(request->peer, Bytes{std::byte{0},std::byte{2},std::byte{'x'}});
    driver->UpdateQuery();
    require(driver->GetQueryResults(&list) == LT_OK && !list, "truncated discovery name is ignored");
    responder.send(request->peer, answer);
    driver->UpdateQuery();
    require(driver->GetQueryResults(&list) == LT_OK && list && !list->m_pNext && std::strcmp(list->m_sName, "S1") == 0 &&
            std::strcmp(list->m_HostIP, "127.0.0.1") == 0 && list->m_HostPort == responder.port() &&
            list->m_guidInst.a == 0x0100007f && list->m_guidInst.b == responder.port(), "query results own name, normalized host and stable address ID");
    sessions_free(list);
    require(driver->EndQuery() == LT_OK && driver->EndQuery() == LT_OK && driver->UpdateQuery() == LT_NOTINITIALIZED, "query lifecycle can restart and close safely");
    require(driver->StartQuery("127.0.0.1:65536") == LT_ERROR && driver->StartQuery("127.0.0.1:0") == LT_ERROR, "invalid ports are not silently truncated");
}
void test_native_join() {
    Host server_host, client_host;
    Driver server(tcp_Create()), client(tcp_Create());
    server->set_host(&server_host); client->set_host(&client_host);
    server->Init(); client->Init();
    const auto address = host_game(*server);
    std::atomic<bool> got_data = false;
    std::jthread pump([&](std::stop_token stop) {
        ServerMessage message;
        while (!stop.stop_requested()) {
            if (server->ReceivePacket(&message) && message.m_DataLen == 3 && message.m_pData[2] == 99) got_data = true;
            std::this_thread::sleep_for(1ms);
        }
    });
    const auto target = address.address + ":" + std::to_string(address.port);
    client_host.guid.a ^= 1;
    require(client->ConnectTCP(target.c_str()) == LT_NOTSAMEGUID, "real native join rejects incompatible application GUID");
    client_host.guid.a ^= 1;
    require(client->ConnectTCP(target.c_str()) == LT_OK && client_host.connections.size() == 1, "native client completes UDP join with native server");
    std::array<uint8, 3> data{0x41, 42, 99};
    require(client->SendData(data.data(), data.size(), 0, client_host.connections[0]), "joined client sends using typed connection");
    const auto deadline = std::chrono::steady_clock::now() + 500ms;
    while (!got_data && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(1ms);
    require(got_data, "server receives joined client data on owner thread");
    client->Disconnect(client_host.connections[0], 0x1234abcd, 0);
    std::this_thread::sleep_for(20ms);
    pump.request_stop(); pump.join();
    require(server_host.connections.empty() && server_host.reason == 0x1234abcd && client_host.connections.empty(), "native peers close and preserve actual disconnect reason");
}
void test_local() {
    Host left_host, right_host;
    Driver left(ldrv_Create()), right(ldrv_Create());
    left->set_host(&left_host); right->set_host(&right_host); left->Init(); right->Init();
    left->Connect(right.get());
    require(left_host.connections.empty() && right_host.connections.empty(), "local acceptance is deferred to each owner's Update");
    left->Update(); right->Update();
    require(left_host.connections.size() == 1 && right_host.connections.size() == 1 && left_host.connections[0]->m_Flags == 1, "local endpoints receive independent real connections");
    std::array<uint8, 5> bytes{0x42,1,2,3,4};
    for (unsigned i = 0; i < 300; ++i) require(left->SendData(bytes.data(), bytes.size(), 0, left_host.connections[0]), "local packet queued");
    ServerMessage message;
    for (unsigned i = 0; i < 300; ++i) require(right->ReceivePacket(&message) && message.m_DataLen == bytes.size() &&
        message.m_pConnection == right_host.connections[0] && message.m_Pos == 1 && std::equal(bytes.begin(), bytes.end(), message.m_pData), "local queue delivers full packets in order");
    require(!right->ReceivePacket(&message), "local queue drains completely");
    left->Disconnect(left_host.connections[0], 1, 0);
    require(left_host.connections.empty() && right_host.connections.size() == 1, "peer closure does not invoke callback on sender's thread");
    right->Update();
    require(right_host.connections.empty() && left_host.closed == 1 && right_host.closed == 1, "local peer notices closure on its own owner thread");
    right_host.allow = false;
    left->Connect(right.get()); left->Update(); right->Update(); left->Update();
    require(left_host.connections.empty() && right_host.connections.empty(), "local rejection closes both mailboxes");
    left->Term(); right->Term();
    right_host.allow = true;
    left->Connect(right.get()); left->Update(); right->Update();
    require(left_host.connections.size() == 1 && right_host.connections.size() == 1, "local driver reconnects after rejection and teardown");
}
void test_local_threads() {
    Host left_host, right_host;
    Driver left(ldrv_Create()), right(ldrv_Create());
    left->set_host(&left_host); right->set_host(&right_host); left->Init(); right->Init();
    std::atomic<bool> connected = false, disconnected = false, invalid = false;
    std::atomic<unsigned> received = 0;
    std::thread::id worker_id;
    right_host.on_accept = [&](NetConnection&) {
        if (std::this_thread::get_id() != worker_id) invalid = true;
        connected = true;
    };
    right_host.on_close = [&](NetConnection&) {
        if (std::this_thread::get_id() != worker_id) invalid = true;
        disconnected = true;
    };
    std::jthread worker([&](std::stop_token stop) {
        worker_id = std::this_thread::get_id();
        ServerMessage packet;
        while (!stop.stop_requested()) {
            right->Update();
            while (right->ReceivePacket(&packet)) {
                const auto index = received.load();
                if (packet.m_DataLen != 3 || packet.m_pData[0] != 0x41 ||
                    packet.m_pData[1] != static_cast<uint8>(index) || packet.m_pData[2] != 0x99) invalid = true;
                ++received;
            }
            std::this_thread::sleep_for(1ms);
        }
    });
    left->Connect(right.get()); left->Update();
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!connected && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(1ms);
    require(connected && left_host.connections.size() == 1, "loopback connect is handed off while peer is running");
    for (unsigned i = 0; i < 1000; ++i) {
        std::array<uint8,3> bytes{0x41,static_cast<uint8>(i),0x99};
        require(left->SendData(bytes.data(), bytes.size(), 0, left_host.connections[0]), "cross-thread loopback send queues owned bytes");
    }
    while (received < 1000 && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(1ms);
    require(received == 1000 && !invalid, "running peer receives 1000 ordered packets and owns its acceptance callback");
    left->Disconnect(left_host.connections[0], 1, 0);
    while (!disconnected && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(1ms);
    worker.request_stop(); worker.join();
    require(disconnected && !invalid && right_host.connections.empty(), "cross-thread peer closure stays on the receiving thread");
}
void test_local_reentrant_close() {
    Host left_host, right_host;
    Driver left(ldrv_Create()), right(ldrv_Create());
    left->set_host(&left_host); right->set_host(&right_host); left->Init(); right->Init();
    left_host.on_accept = [&](NetConnection&) { left->Term(); };
    left->Connect(right.get()); left->Update(); right->Update();
    require(left_host.accepted == 1 && left_host.closed == 1 && left_host.connections.empty() && right_host.connections.empty(),
        "teardown requested during acceptance waits until ownership is resolved");
}

void test_udp_reentrant_close() {
    Host host;
    Driver driver(tcp_Create()); driver->set_host(&host); driver->Init();
    const auto address = host_game(*driver);
    host.on_accept = [&](NetConnection&) { driver->Term(); };
    UdpSocket raw;
    raw.send(address, join_request());
    ServerMessage packet;
    require(!driver->ReceivePacket(&packet) && host.accepted == 1 && host.closed == 1 && host.connections.empty(),
        "UDP receive stops safely when acceptance closes its socket");
}

}
int main() {
    try {
        require(nolf::platform::resolve_ipv4({"localhost", 42}) == loopback(42), "platform resolves IPv4 endpoint once");
        require(nolf::platform::ipv4_bytes("192.0.2.7") == std::array<uint8,4>{192,0,2,7}, "numeric IPv4 callback bytes");
        bool rejected = false;
        try { nolf::platform::ipv4_bytes("1.2.3.256"); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "invalid IPv4 numeric input rejected");
        test_host_wire(); test_query_wire(); test_native_join(); test_local(); test_local_threads(); test_local_reentrant_close(); test_udp_reentrant_close();
        std::cout << "native UDP discovery, join, data, disconnect, and local transport passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
