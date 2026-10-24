#include "engine/client/game_handshake.h"
#include "engine/portable/binary.h"
#include "ltbasedefs.h"
#include "game/Shared/ClientServerShared.h"
#include "game/Shared/MsgIDs.h"
#include <stdexcept>
#include <algorithm>

namespace nolf {
GameHandshake::GameHandshake(std::string player_name, std::uint32_t nonce,
        std::uint32_t weapon_crc, std::uint32_t client_shell_crc)
    : name_(std::move(player_name)), nonce_(nonce), weapons_(weapon_crc), client_shell_(client_shell_crc) {
    if (name_.empty() || name_.size() > 15 || name_.find('\0') != name_.npos)
        throw std::runtime_error("invalid player name for game handshake");
}
std::optional<PacketBytes> GameHandshake::receive(std::span<const std::byte> payload) {
    BinaryReader input(payload);
    const auto type = input.integer<std::uint8_t>();
    BinaryWriter reply;
    auto next = phase_;
    if (type == MID_HANDSHAKE_HELLO && phase_ == Phase::Hello) {
        if (input.integer<std::uint16_t>() != GAME_HANDSHAKE_VER)
            throw std::runtime_error("incompatible NOLF game handshake version");
        reply.integer<std::uint8_t>(MID_HANDSHAKE_HELLO);
        reply.integer<std::uint16_t>(GAME_HANDSHAKE_VER); reply.integer(nonce_);
        next = Phase::Challenge;
    } else if (type == MID_HANDSHAKE_PASSWORD && phase_ == Phase::Challenge) {
        const auto mask = input.integer<std::uint32_t>() ^ nonce_;
        std::uint32_t name_key = 0;
        for (std::size_t i=0; i<std::min<std::size_t>(4,name_.size()); ++i)
            name_key |= std::uint32_t(static_cast<unsigned char>(name_[i])) << (8*i);
        reply.integer<std::uint8_t>(MID_HANDSHAKE_LETMEIN);
        reply.integer(name_key ^ mask); reply.integer(weapons_ ^ mask); reply.integer(client_shell_ ^ mask);
        next = Phase::Confirmation;
    } else if (type == MID_HANDSHAKE_DONE && phase_ == Phase::Confirmation) next = Phase::Complete;
    else throw std::runtime_error("unexpected NOLF game handshake stage");
    if (input.remaining()) throw std::runtime_error("trailing NOLF game handshake bytes");
    phase_ = next;
    if (reply.data().empty()) return {};
    return reply.data();
}
}
