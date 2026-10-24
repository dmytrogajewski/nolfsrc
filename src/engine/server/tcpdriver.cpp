// The historical "internet" driver uses UDP. Only the platform adapter
// knows socket handles, address structures or host resolver APIs.
#include "net_driver.h"
#include "message.h"
#include "engine/portable/wire.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstring>
#include <limits>
#include <memory>
#include <string_view>
#include <thread>

extern int g_JoinPort;
extern char* g_pBindAddress;
extern char* g_pLocalAddress;

namespace {
using nolf::platform::Endpoint;
using nolf::platform::UdpSocket;
using Bytes = std::vector<std::byte>;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
constexpr uint16 default_port = 27888;
constexpr std::size_t max_query_records = 4096;

Endpoint parse_address(std::string_view text, uint16 default_value = default_port) {
    if (text.empty() || text.size() > 255 || text.find('\0') != text.npos)
        throw std::invalid_argument("invalid network address");
    uint16 port = default_value;
    if (const auto colon = text.find(':'); colon != text.npos) {
        const auto number = text.substr(colon + 1);
        unsigned value = 0;
        const auto [end, error] = std::from_chars(number.data(), number.data() + number.size(), value);
        if (number.empty() || error != std::errc{} || end != number.data() + number.size() || !value || value > 65535)
            throw std::invalid_argument("invalid network port");
        port = static_cast<uint16>(value);
        text = text.substr(0, colon);
    }
    if (text == "*") return {"255.255.255.255", port};
    return nolf::platform::resolve_ipv4({std::string(text), port});
}
Endpoint bind_address(uint16 port) {
    return {g_pBindAddress && *g_pBindAddress ? g_pBindAddress : "0.0.0.0", port};
}
std::array<std::byte, 16> guid_bytes(const LTGUID& guid) {
    std::array<std::byte, 16> bytes{};
    nolf::wire::store(bytes.data(), guid.a);
    nolf::wire::store(bytes.data() + 4, guid.b);
    nolf::wire::store(bytes.data() + 6, guid.c);
    std::memcpy(bytes.data() + 8, guid.d, 8);
    return bytes;
}
bool matches_guid(std::span<const std::byte> packet, std::size_t offset, const LTGUID& guid) {
    const auto expected = guid_bytes(guid);
    return packet.size() >= offset + expected.size() &&
        std::equal(expected.begin(), expected.end(), packet.begin() + offset);
}
bool put_packet(ServerMessage& message, const Bytes& bytes) {
    if (bytes.empty() || bytes.size() > 65535 || !message.Reserve(static_cast<uint32>(bytes.size()))) return false;
    std::memcpy(message.m_pData, bytes.data(), bytes.size());
    message.m_DataLen = static_cast<uint16>(bytes.size());
    message.m_Pos = 1;
    message.m_Flags = 0;
    message.m_pConnection = nullptr;
    return true;
}
void free_sessions(NetSession* list) {
    while (list) { auto* next = list->m_pNext; delete list; list = next; }
}

class TCPDriver final : public CBaseDriver {
    struct Peer : NetConnection {
        Endpoint address;
        bool ready = false;
    };
    struct QueryRecord {
        Endpoint address;
        std::string name;
        Clock::time_point received = Clock::now();
        std::array<Clock::time_point, 32> sent{};
        uint32 sequence = 0;
        uint32 ping_ms = 0;
        bool broadcast = false;
        bool responded = false;
    };
public:
    TCPDriver() { std::strcpy(m_Name, "internet"); m_DriverFlags = NETDRIVER_TCPIP; }
    ~TCPDriver() override { Term(); }
    LTBOOL Init() override { return LTTRUE; }
    void Update() override {}
    void Term() override {
        EndQuery();
        while (!peers_.empty()) {
            auto peer = std::move(peers_.back());
            peers_.pop_back();
            send_disconnect(*peer, 0);
            if (host()) host()->close_connection(*peer);
        }
        socket_.reset();
        hosting_ = false;
        name_.clear();
    }
    LTRESULT GetServiceList(NetService** list) override {
        if (!list) return LT_INVALIDPARAMS;
        auto service = std::make_unique<NetService>();
        *service = {};
        service->m_handle = &service_;
        service->m_dwFlags = NETDRIVER_TCPIP;
        std::strcpy(service->m_sName, "Internet TCP/IP");
        service->m_pNext = *list;
        *list = service.release();
        return LT_OK;
    }
    int SelectService(HNETSERVICE service) override { return service == &service_ ? 0 : 1; }
    LTRESULT HostSession(NetHost* info) override {
        if (!info || !host() || info->m_Port > 65535 || !std::memchr(info->m_sName, 0, sizeof(info->m_sName))) return LT_INVALIDPARAMS;
        std::string name(info->m_sName);
        Term();
        try {
            auto socket = std::make_unique<UdpSocket>(bind_address(info->m_Port ? static_cast<uint16>(info->m_Port) : default_port));
            socket_ = std::move(socket);
            name_ = std::move(name);
            max_players_ = info->m_dwMaxPlayers;
            hosting_ = true;
            return LT_OK;
        } catch (const std::exception&) { return LT_CANTBINDTOPORT; }
    }
    LTRESULT UpdateSessionName(const char* name) override {
        if (!name) return LT_INVALIDPARAMS;
        const std::string_view text(name);
        if (text.size() >= MAX_NETSESSIONNAME_LEN) return LT_INVALIDPARAMS;
        name_ = text;
        return LT_OK;
    }
    LTRESULT GetSessionName(char* name, uint32 capacity) override {
        if (!name || !capacity) return LT_INVALIDPARAMS;
        const auto count = std::min<std::size_t>(name_.size(), capacity - 1);
        std::memcpy(name, name_.data(), count);
        name[count] = 0;
        return LT_OK;
    }
    LTRESULT GetTcpIpAddress(char* address, uint32 capacity, unsigned short& port) override {
        if (!address || !capacity) return LT_INVALIDPARAMS;
        address[0] = 0;
        port = NO_HOST_PORT;
        try {
            auto text = g_pLocalAddress && *g_pLocalAddress ?
                nolf::platform::resolve_ipv4({g_pLocalAddress, 0}).address :
                (g_pBindAddress && *g_pBindAddress ? nolf::platform::resolve_ipv4({g_pBindAddress, 0}).address : nolf::platform::local_ipv4_address());
            if (text == "0.0.0.0") text = nolf::platform::local_ipv4_address();
            if (text.size() >= capacity) return LT_ERROR;
            std::memcpy(address, text.c_str(), text.size() + 1);
            if (hosting_ && socket_) port = socket_->port();
            return LT_OK;
        } catch (const std::exception&) { return LT_ERROR; }
    }
    LTRESULT ConnectTCP(const char* address) override {
        if (!address || !host()) return LT_INVALIDPARAMS;
        try { return join(parse_address(address)); }
        catch (const std::exception&) { return LT_ERROR; }
    }
    LTRESULT JoinSession(NetSession* session) override {
        if (!session || !std::memchr(session->m_HostIP, 0, sizeof(session->m_HostIP)) ||
            !session->m_HostPort || session->m_HostPort > 65535 || !host()) return LT_INVALIDPARAMS;
        try { return join(nolf::platform::resolve_ipv4({session->m_HostIP, static_cast<uint16>(session->m_HostPort)})); }
        catch (const std::exception&) { return LT_ERROR; }
    }
    LTRESULT SendTo(const void* data, uint32 length, const char* address, uint32 port) override {
        if (!socket_) return LT_NOTINITIALIZED;
        if ((!data && length) || !address || !port || port > 65535) return LT_INVALIDPARAMS;
        try {
            auto endpoint = parse_address(address);
            endpoint.port = static_cast<uint16>(port);
            return send(*socket_, endpoint, {static_cast<const std::byte*>(data), length}) ? LT_OK : LT_ERROR;
        } catch (const std::exception&) { return LT_ERROR; }
    }
    void Disconnect(NetConnection* connection, uint32 reason, uint32) override {
        const auto it = std::find_if(peers_.begin(), peers_.end(), [=](const auto& peer) { return peer.get() == connection; });
        if (it == peers_.end()) return;
        auto peer = std::move(*it);
        peers_.erase(it);
        send_disconnect(*peer, reason);
        if (host()) host()->close_connection(*peer);
    }
    LTBOOL SendData(void* data, uint32 length, uint32, NetConnection* connection) override {
        if ((!data && length) || !socket_ || length > 65507) return LTFALSE;
        for (const auto& peer : peers_) if (peer.get() == connection) {
            if (!peer->ready) return LTFALSE; // manager retains packets queued during acceptance
            return send(*socket_, peer->address, {static_cast<const std::byte*>(data), length});
        }
        return LTFALSE;
    }
    LTBOOL ReceivePacket(ServerMessage* message) override {
        if (!socket_ || !message || !host()) return LTFALSE;
        // Bound each pump so a busy socket cannot starve the simulation.
        for (unsigned n = 0; n < 256 && socket_; ++n) {
            std::optional<nolf::platform::Datagram> datagram;
            try { datagram = socket_->receive(0ms); }
            catch (const std::exception&) { return LTFALSE; }
            if (!datagram) break;
            if (datagram->bytes.empty()) continue;
            if (datagram->bytes[0] == std::byte{0}) { control(*datagram); continue; }
            if (!put_packet(*message, datagram->bytes)) continue;
            if (auto* peer = find_peer(datagram->peer)) {
                message->m_pConnection = peer;
                return LTTRUE;
            }
            host()->connectionless_packet(*message, datagram->peer);
        }
        return LTFALSE;
    }
    uint32 GetRecvSize() override { return 28; }
    LTRESULT StartQuery(const char* addresses) override {
        if (!addresses || !host()) return LT_INVALIDPARAMS;
        EndQuery();
        try {
            auto socket = std::make_unique<UdpSocket>(bind_address(0), true);
            std::vector<QueryRecord> records;
            std::string_view text(addresses);
            // CommandParser separates addresses with semicolons; accept whitespace
            // and quoted address tokens used by the console as well.
            while (!text.empty()) {
                const auto begin = text.find_first_not_of(" \t\r\n;\"");
                if (begin == text.npos) break;
                text.remove_prefix(begin);
                const auto end = text.find_first_of(" \t\r\n;\"");
                const auto token = text.substr(0, end);
                QueryRecord record;
                record.address = parse_address(token);
                record.broadcast = record.address.address == "255.255.255.255";
                if (records.size() >= max_query_records) return LT_INVALIDPARAMS;
                if (std::none_of(records.begin(), records.end(), [&](const auto& r) { return r.address == record.address; }))
                    records.push_back(std::move(record));
                if (end == text.npos) break;
                text.remove_prefix(end + 1);
            }
            query_ = std::move(records);
            query_socket_ = std::move(socket);
            last_query_ = Clock::now() - 2s;
            return UpdateQuery();
        } catch (const std::exception&) { EndQuery(); return LT_ERROR; }
    }
    LTRESULT UpdateQuery() override {
        if (!query_socket_ || !host()) return LT_NOTINITIALIZED;
        const auto now = Clock::now();
        if (now - last_query_ >= 1s) {
            const auto guid = guid_bytes(host()->application_guid());
            for (auto& record : query_) {
                Bytes request{std::byte{0}, std::byte{1}};
                request.insert(request.end(), guid.begin(), guid.end());
                const uint8 slot = record.broadcast ? 255 : static_cast<uint8>(record.sequence++ & 31);
                if (slot < 32) record.sent[slot] = now;
                request.push_back(std::byte{slot});
                send(*query_socket_, record.address, request);
            }
            last_query_ = now;
        }
        for (unsigned n = 0; n < 256; ++n) {
            std::optional<nolf::platform::Datagram> datagram;
            try { datagram = query_socket_->receive(0ms); }
            catch (const std::exception&) { return LT_ERROR; }
            if (!datagram) break;
            const auto& bytes = datagram->bytes;
            if (bytes.size() < 4 || bytes[0] != std::byte{0} || bytes[1] != std::byte{2}) continue;
            const auto end = std::find(bytes.begin() + 2, bytes.end(), std::byte{0});
            if (end == bytes.end() || end + 1 == bytes.end() || end - bytes.begin() - 2 >= MAX_NETSESSIONNAME_LEN) continue;
            auto record = std::find_if(query_.begin(), query_.end(), [&](const auto& r) { return r.address == datagram->peer; });
            if (record == query_.end()) {
                // An unsolicited reply is only meaningful while doing broadcast discovery.
                if (query_.size() >= max_query_records || std::none_of(query_.begin(), query_.end(), [&](const auto& r) {
                    return r.broadcast && r.address.port == datagram->peer.port;
                })) continue;
                query_.push_back(QueryRecord{});
                record = query_.end() - 1;
                record->address = datagram->peer;
            }
            record->name.assign(reinterpret_cast<const char*>(bytes.data() + 2), static_cast<std::size_t>(end - bytes.begin() - 2));
            record->responded = true;
            record->received = now;
            const auto slot = std::to_integer<uint8>(*(end + 1));
            if (slot < 32 && record->sent[slot] != Clock::time_point{})
                record->ping_ms = static_cast<uint32>(std::max<std::int64_t>(0, std::chrono::duration_cast<std::chrono::milliseconds>(now - record->sent[slot]).count()));
        }
        for (auto& record : query_) if (now - record.received > 30s) { record.responded = false; record.name.clear(); }
        return LT_OK;
    }
    LTRESULT GetQueryResults(NetSession** list) override {
        if (!list) return LT_INVALIDPARAMS;
        *list = nullptr;
        try {
            for (const auto& record : query_) if (record.responded) {
                auto session = std::make_unique<NetSession>();
                session->m_dwFlags = 0;
                session->m_guidApp = host()->application_guid();
                session->m_guidInst = {};
                // A stable address-derived ID replaces the historical truncated pointer.
                const auto ip = nolf::platform::ipv4_bytes(record.address.address);
                session->m_guidInst.a = uint32(ip[0]) | uint32(ip[1]) << 8 | uint32(ip[2]) << 16 | uint32(ip[3]) << 24;
                session->m_guidInst.b = record.address.port;
                session->m_Ping = record.ping_ms;
                std::memcpy(session->m_sName, record.name.c_str(), record.name.size() + 1);
                std::memcpy(session->m_HostIP, record.address.address.c_str(), record.address.address.size() + 1);
                session->m_HostPort = record.address.port;
                session->m_pNext = *list;
                *list = session.release();
            }
        } catch (...) { free_sessions(*list); *list = nullptr; return LT_ERROR; }
        return LT_OK;
    }
    LTRESULT GetSessionList(NetSession** list, const char* addresses) override {
        if (!list) return LT_INVALIDPARAMS;
        *list = nullptr;
        const auto result = StartQuery(addresses);
        if (result != LT_OK) return result;
        const auto deadline = Clock::now() + 3500ms;
        do {
            if (UpdateQuery() != LT_OK) { EndQuery(); return LT_ERROR; }
            std::this_thread::sleep_for(1ms);
        } while (Clock::now() < deadline);
        const auto results = GetQueryResults(list);
        EndQuery();
        return results;
    }
    LTRESULT EndQuery() override { query_socket_.reset(); query_.clear(); return LT_OK; }
private:
    static bool send(UdpSocket& socket, const Endpoint& to, std::span<const std::byte> bytes) {
        try { socket.send(to, bytes); return true; }
        catch (const std::exception&) { return false; }
    }
    void reply(const Endpoint& to, uint8 type, unsigned count = 1) {
        if (!socket_) return;
        const std::array bytes{std::byte{0}, std::byte{type}};
        while (count--) send(*socket_, to, bytes);
    }
    Peer* find_peer(const Endpoint& address) {
        for (const auto& peer : peers_) if (peer->address == address) return peer.get();
        return nullptr;
    }
    void send_disconnect(const Peer& peer, uint32 reason) {
        if (!socket_) return;
        std::array<std::byte, 6> bytes{};
        nolf::wire::store(bytes.data() + 2, reason);
        for (unsigned i = 0; i < 4; ++i) send(*socket_, peer.address, bytes);
    }
    LTRESULT join(const Endpoint& address) {
        if (g_JoinPort < 0 || g_JoinPort > 65535) return LT_INVALIDPARAMS;
        std::unique_ptr<UdpSocket> socket;
        try { socket = std::make_unique<UdpSocket>(bind_address(static_cast<uint16>(g_JoinPort))); }
        catch (const std::exception&) { return LT_CANTBINDTOPORT; }
        const auto guid = guid_bytes(host()->application_guid());
        Bytes request{std::byte{0}, std::byte{3}, std::byte{0}};
        request.insert(request.end(), guid.begin(), guid.end());
        Term();
        const auto deadline = Clock::now() + 10s;
        auto last_send = Clock::now() - 1s;
        do {
            const auto now = Clock::now();
            if (now - last_send >= 300ms) {
                if (!send(*socket, address, request)) return LT_ERROR;
                last_send = now;
            }
            const auto packet = socket->receive(1ms);
            if (!packet || packet->peer != address || packet->bytes.size() < 2 || packet->bytes[0] != std::byte{0}) continue;
            const auto type = std::to_integer<uint8>(packet->bytes[1]);
            if (type == 6) return LT_REJECTED;
            if (type == 7) return LT_NOTSAMEGUID;
            if (type != 5) continue;
            auto peer = std::make_unique<Peer>();
            peer->address = address;
            peer->m_pDriver = this;
            peer->ready = true;
            socket_ = std::move(socket);
            // Register before notifying the manager: acceptance may send immediately.
            auto* raw = peer.get();
            peers_.push_back(std::move(peer));
            const bool accepted = host()->accept_connection(*raw);
            const auto it = std::find_if(peers_.begin(), peers_.end(), [=](const auto& item) { return item.get() == raw; });
            // Acceptance can synchronously disconnect or tear down the driver.
            if (it == peers_.end()) return LT_REJECTED;
            if (!accepted) {
                auto rejected = std::move(*it);
                peers_.erase(it);
                send_disconnect(*rejected, 0);
                socket_.reset();
                return LT_REJECTED;
            }
            return LT_OK;
        } while (Clock::now() < deadline);
        return LT_TIMEOUT;
    }
    void control(const nolf::platform::Datagram& packet) {
        const auto& bytes = packet.bytes;
        if (bytes.size() < 2) return;
        const auto type = std::to_integer<uint8>(bytes[1]);
        if (type == 0) {
            if (bytes.size() < 6) return;
            for (auto it = peers_.begin(); it != peers_.end(); ++it) if ((*it)->address == packet.peer) {
                auto peer = std::move(*it);
                peers_.erase(it);
                host()->remote_disconnect(nolf::wire::load<uint32>(bytes.data() + 2));
                host()->close_connection(*peer);
                return;
            }
        }
        if (!hosting_) return;
        if (type == 1) {
            if (bytes.size() < 19 || !matches_guid(bytes, 2, host()->application_guid())) return;
            Bytes response{std::byte{0}, std::byte{2}};
            const auto* begin = reinterpret_cast<const std::byte*>(name_.c_str());
            response.insert(response.end(), begin, begin + name_.size() + 1);
            response.push_back(bytes[18]);
            send(*socket_, packet.peer, response);
        } else if (type == 3) {
            if (bytes.size() < 19) return;
            if (!matches_guid(bytes, 3, host()->application_guid())) { reply(packet.peer, 7); return; }
            if (find_peer(packet.peer)) { reply(packet.peer, 5); return; }
            if (peers_.size() >= max_players_) { reply(packet.peer, 6); return; }
            auto peer = std::make_unique<Peer>();
            peer->address = packet.peer;
            peer->m_pDriver = this;
            peers_.push_back(std::move(peer));
            auto* raw = peers_.back().get();
            const bool accepted = host()->accept_connection(*raw);
            const auto it = std::find_if(peers_.begin(), peers_.end(), [=](const auto& item) { return item.get() == raw; });
            if (it == peers_.end()) return;
            if (!accepted) {
                peers_.erase(it);
                reply(packet.peer, 6);
                return;
            }
            reply(packet.peer, 5, 5);
            raw->ready = true;
            host()->flush_connection(*raw);
        }
    }
    std::unique_ptr<UdpSocket> socket_, query_socket_;
    std::vector<std::unique_ptr<Peer>> peers_;
    std::vector<QueryRecord> query_;
    Clock::time_point last_query_{};
    std::string name_;
    uint32 max_players_ = 0;
    bool hosting_ = false;
    CNetService service_{};
};
}
CBaseDriver* tcp_Create() { return new TCPDriver; }
