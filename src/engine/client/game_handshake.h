#pragma once
#include "engine/client/server_files.h"
#include <optional>
#include <string>

namespace nolf {
// Game authentication is independent of transport/world readiness. The nonce
// is an explicit wire value, never an address truncated to 32 bits.
class GameHandshake {
public:
    enum class Phase { Hello, Challenge, Confirmation, Complete };
    GameHandshake(std::string player_name, std::uint32_t nonce,
                  std::uint32_t weapon_crc, std::uint32_t client_shell_crc);
    std::optional<PacketBytes> receive(std::span<const std::byte>);
    Phase phase() const { return phase_; }
private:
    std::string name_;
    std::uint32_t nonce_, weapons_, client_shell_;
    Phase phase_ = Phase::Hello;
};
}
