#include "client_session.h"
#include "engine/portable/binary.h"
#include <cmath>
#include <array>
#include <stdexcept>
#include <utility>

namespace nolf {
namespace {
constexpr std::size_t queue_limit = 4*1024*1024;
void finished(BinaryReader& input) { if (input.remaining()) throw std::runtime_error("unexpected server packet tail"); }
}
ClientSession::ClientSession(std::shared_ptr<const Assets> assets) : files_(std::move(assets)) {}
void ClientSession::event(ClientEvent event) {
    const auto size = sizeof(event)+event.name.size()+event.value.size()+event.files.size()*2+event.bytes.size();
    if (events_.size() >= 4096 || size > queue_limit-event_bytes_) throw std::runtime_error("client event queue full");
    events_.push_back(std::move(event)); event_bytes_ += size;
}
void ClientSession::outgoing(PacketBytes packet) {
    const auto size = packet.size();
    if (size > 65529 || outgoing_.size() >= 4096 || size > queue_limit-outgoing_bytes_)
        throw std::runtime_error("client send queue full");
    outgoing_.push_back(std::move(packet)); outgoing_bytes_ += size;
}
void ClientSession::begin(std::span<const std::byte> data) {
    if (phase_ != Phase::Disconnected || data.size() > 65526) throw std::logic_error("invalid client session start");
    BinaryWriter packet;
    packet.integer<std::uint8_t>(5); packet.integer<std::uint16_t>(data.size()); packet.bytes(data);
    outgoing(packet.data()); phase_ = Phase::Negotiating;
}
void ClientSession::receive(std::span<const std::byte> packet) {
    unsigned remaining = 4096;
    receive(packet,0,remaining);
}
void ClientSession::receive(std::span<const std::byte> packet, unsigned depth, unsigned& remaining) {
    if (phase_ == Phase::Disconnected || phase_ == Phase::Closed || packet.empty() || packet.size() > 65535 || depth > 8 || !remaining)
        throw std::runtime_error("invalid server packet or session state");
    --remaining;
    const auto id = std::to_integer<std::uint8_t>(packet[0]) & 0x3f;
    BinaryReader input(packet.subspan(1));
    switch (id) {
    case 4: {
        const auto version = input.integer<std::uint32_t>(); finished(input);
        if (version != 6) throw std::runtime_error("incompatible engine protocol version");
        version_ = true; return;
    }
    case 12: {
        const auto client = input.integer<std::uint16_t>();
        const auto local = input.integer<std::uint8_t>(); finished(input);
        if (!version_ || client == 0xffff || local > 1 || (client_ && (*client_ != client || local_ != bool(local))))
            throw std::runtime_error("invalid server client identity");
        ClientEvent value{ClientEvent::Kind::Identity}; value.object_id = client;
        event(std::move(value)); client_ = client; local_ = local; return;
    }
    case 0x32:
        for (auto& reply : files_.announce(packet.subspan(1))) outgoing(std::move(reply));
        return;
    case 5: {
        finished(input);
        if (!version_ || !client_) throw std::runtime_error("world transition before client negotiation");
        event({ClientEvent::Kind::UnloadWorld});
        object_.reset(); preload_started_ = preload_ended_ = false; phase_ = Phase::Negotiating; return;
    }
    case 6: {
        const auto time = input.real(); const auto file = input.integer<std::uint16_t>(); finished(input);
        if (!version_ || !client_ || phase_ != Phase::Negotiating || !std::isfinite(time) || time < 0)
            throw std::runtime_error("invalid world startup request");
        const auto& source = files_.require(file);
        ClientEvent value{ClientEvent::Kind::LoadWorld}; value.server_time = time;
        value.name = source.name; value.files.push_back(file);
        event(std::move(value)); phase_ = Phase::LoadingWorld; return;
    }
    case 0x10: {
        auto name = input.cstring(), value = input.cstring(); finished(input);
        if (name.empty() || name.size() > 4096 || value.size() > 16384) throw std::runtime_error("invalid server console value");
        ClientEvent item{ClientEvent::Kind::ConsoleValue}; item.name = std::move(name); item.value = std::move(value);
        event(std::move(item)); return;
    }
    case 0x11: {
        if (!version_ || !client_) throw std::runtime_error("sky definition before client negotiation");
        auto sky=decode_sky(packet.subspan(1)); ClientEvent item{ClientEvent::Kind::Sky}; item.id=id; item.sky=std::move(sky);
        event(std::move(item)); return;
    }
    case 0x15: {
        const auto kind = input.integer<std::uint8_t>();
        const bool batch = phase_ == Phase::Preloading || (local_ && phase_ == Phase::LoadingWorld);
        if (kind > 5 || (kind < 2 && (!batch || preload_ended_)) ||
            (kind >= 2 && !batch && phase_ != Phase::LoadingWorld && phase_ != Phase::AwaitingObject && phase_ != Phase::InWorld))
            throw std::runtime_error("invalid preload sequence");
        ClientEvent item{kind == 0 ? ClientEvent::Kind::PreloadBegin : kind == 1 ? ClientEvent::Kind::PreloadEnd : ClientEvent::Kind::PreloadFiles};
        item.id = kind;
        if (kind < 2) {
            finished(input);
            if ((kind == 0 && preload_started_) || (kind == 1 && !preload_started_)) throw std::runtime_error("invalid preload boundary");
        } else {
            if (input.remaining()%2) throw std::runtime_error("invalid preload file list");
            while (input.remaining()) {
                const auto file = input.integer<std::uint16_t>();
                files_.require(file); item.files.push_back(file);
            }
        }
        event(std::move(item));
        if (kind == 0) preload_started_ = true;
        if (kind == 1) preload_ended_ = true;
        return;
    }
    case 7: {
        const auto object = input.integer<std::uint16_t>(); finished(input);
        const bool loading_local = local_ && (phase_ == Phase::LoadingWorld || phase_ == Phase::Preloading);
        if (phase_ != Phase::AwaitingObject && phase_ != Phase::InWorld && !loading_local) throw std::runtime_error("client object before world readiness");
        ClientEvent item{ClientEvent::Kind::ClientObject}; item.object_id = object; event(std::move(item));
        if (object == 0xffff) { object_.reset(); if (!loading_local) phase_ = Phase::AwaitingObject; }
        else { object_ = object; if (!loading_local) phase_ = Phase::InWorld; }
        return;
    }
    case 13: {
        if (!input.remaining()) throw std::runtime_error("missing game message identifier");
        ClientEvent item{ClientEvent::Kind::GameMessage}; item.id = std::to_integer<std::uint8_t>(packet.back());
        item.bytes.assign(packet.begin()+1,packet.end()-1); event(std::move(item)); return;
    }
    case 8: case 10: case 14: {
        std::vector<std::span<const std::byte>> messages;
        while (input.remaining()) {
            const auto length = input.integer<std::uint8_t>();
            if (!length) break;
            messages.push_back(input.bytes(length));
        }
        if (id == 14) finished(input);
        const auto body = input.bytes(input.remaining());
        for (auto message : messages) receive(message,depth+1,remaining);
        if (id != 14 && !body.empty()) {
            ClientEvent item{ClientEvent::Kind::EnginePacket}; item.id = id;
            item.bytes.assign(body.begin(),body.end()); event(std::move(item));
        }
        return;
    }
    default: {
        ClientEvent item{ClientEvent::Kind::EnginePacket}; item.id = id;
        item.bytes.assign(packet.begin()+1,packet.end()); event(std::move(item)); return;
    }
    }
}
void ClientSession::world_loaded() {
    if (phase_ != Phase::LoadingWorld) throw std::logic_error("no client world load to acknowledge");
    outgoing({std::byte{9},std::byte{0}}); phase_ = Phase::Preloading;
}
void ClientSession::preload_complete() {
    if (phase_ != Phase::Preloading || !preload_started_ || !preload_ended_)
        throw std::logic_error("client preload is incomplete");
    outgoing({std::byte{9},std::byte{1}}); phase_ = object_ ? Phase::InWorld : Phase::AwaitingObject;
}
void ClientSession::send_game(std::uint8_t id, std::span<const std::byte> payload) {
    // Any identified client may talk to the game: a single-player client asks
    // for its first world this way.
    if (!version_ || !client_ || phase_ == Phase::Closed || phase_ == Phase::Disconnected || payload.size() > 65527)
        throw std::logic_error("game message before client identification");
    PacketBytes packet{std::byte{11}};
    packet.insert(packet.end(),payload.begin(),payload.end()); packet.push_back(std::byte{id}); outgoing(std::move(packet));
}
void ClientSession::send_commands(std::uint8_t rate, std::span<const std::uint8_t> active,
        std::span<const std::uint16_t> sounds) {
    if (phase_ != Phase::InWorld || active.size() > 255 || sounds.size() > (65529-258)/2)
        throw std::logic_error("invalid client command update");
    std::array<bool,255> states{};
    unsigned count = 0;
    for (auto command : active) {
        if (command >= states.size()) throw std::invalid_argument("client command ID exceeds engine range");
        if (!states[command]) { states[command] = true; ++count; }
    }
    BinaryWriter packet; packet.integer<std::uint8_t>(7); packet.integer(rate); packet.integer<std::uint8_t>(count);
    for (unsigned command = 0; command < states.size(); ++command) if (states[command]) packet.integer<std::uint8_t>(command);
    for (auto sound : sounds) packet.integer(sound);
    outgoing(packet.data());
}
void ClientSession::close() {
    outgoing_.clear(); outgoing_bytes_ = 0; events_.clear(); event_bytes_ = 0;
    files_.clear(); client_.reset(); object_.reset(); phase_ = Phase::Closed;
}
std::vector<ClientEvent> ClientSession::take_events() { event_bytes_ = 0; return std::exchange(events_,{}); }
std::vector<PacketBytes> ClientSession::take_outgoing() { outgoing_bytes_ = 0; return std::exchange(outgoing_,{}); }
}
