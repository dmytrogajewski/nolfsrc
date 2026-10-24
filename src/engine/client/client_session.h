#pragma once
#include "server_files.h"
#include "engine/portable/sky.h"
#include <optional>

namespace nolf {
struct ClientEvent {
    enum class Kind { Identity, UnloadWorld, LoadWorld, ConsoleValue, PreloadBegin,
        PreloadFiles, PreloadEnd, ClientObject, GameMessage, EnginePacket, Sky };
    Kind kind;
    std::uint8_t id = 0;
    std::uint16_t object_id = 0xffff;
    float server_time = 0;
    std::string name, value;
    std::vector<std::uint16_t> files;
    PacketBytes bytes;
    std::optional<SkyInfo> sky;
};
// Owns the engine handshake and message framing. Applications consume owned
// events and explicitly acknowledge completed world/preload work. No game or
// renderer callback executes inside the network packet parser.
class ClientSession {
public:
    enum class Phase { Disconnected, Negotiating, LoadingWorld, Preloading, AwaitingObject, InWorld, Closed };
    explicit ClientSession(std::shared_ptr<const Assets>);
    void begin(std::span<const std::byte> client_data);
    void receive(std::span<const std::byte> packet);
    void world_loaded();
    void preload_complete();
    void send_game(std::uint8_t id, std::span<const std::byte> payload);
    void send_commands(std::uint8_t rate, std::span<const std::uint8_t> active,
        std::span<const std::uint16_t> finished_sounds = {});
    void close();
    std::vector<ClientEvent> take_events();
    std::vector<PacketBytes> take_outgoing();
    Phase phase() const { return phase_; }
    bool version_verified() const { return version_; }
    std::optional<std::uint16_t> client_id() const { return client_; }
    std::optional<std::uint16_t> object_id() const { return object_; }
    bool local() const { return local_; }
    const ServerFiles& files() const { return files_; }
private:
    void receive(std::span<const std::byte>, unsigned depth, unsigned& remaining);
    void event(ClientEvent);
    void outgoing(PacketBytes);
    ServerFiles files_;
    Phase phase_ = Phase::Disconnected;
    bool version_ = false, local_ = false, preload_started_ = false, preload_ended_ = false;
    std::optional<std::uint16_t> client_, object_;
    std::vector<ClientEvent> events_;
    std::vector<PacketBytes> outgoing_;
    std::size_t event_bytes_ = 0, outgoing_bytes_ = 0;
};
}
