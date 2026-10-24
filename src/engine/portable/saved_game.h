#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace nolf {
inline constexpr std::uint32_t saved_game_version = 1113;
inline constexpr std::uint32_t saved_game_sentinel = 0xabcdefaf;
using SavedVector = std::array<float,3>;
using SavedRotation = std::array<float,4>;
struct SavedAttachment {
    SavedVector offset{};
    SavedRotation rotation{0,0,0,1};
    std::uint16_t child = 0;
    std::int32_t node = -1;
    bool operator==(const SavedAttachment&) const = default;
};
struct SavedRelation {
    std::uint8_t type = 0;
    std::uint16_t other = 0;
    bool operator==(const SavedRelation&) const = default;
};
struct SavedAnimation {
    std::uint32_t index = 0, time = 0;
    std::uint16_t flags = 0;
    bool operator==(const SavedAnimation&) const = default;
};
struct SavedClient {
    std::uint32_t flags = 0;
    std::string name;
    bool operator==(const SavedClient&) const = default;
};
// These are serialized values, not a view of LTObject or ObjectCreateStruct.
// All object indices refer to this document; no native handle is serialized.
struct SavedObject {
    std::string class_name, name, filename;
    std::uint8_t type = 0;
    std::uint32_t flags = 0, flags2 = 0, user_flags = 0;
    SavedVector position{}, scale{1,1,1};
    SavedRotation rotation{0,0,0,1};
    std::array<std::string,2> skins;
    float light_radius = 0;
    std::uint16_t container_code = 0xffff;
    float next_update = 0;
    std::uint8_t blocking_priority = 0;
    std::optional<std::vector<std::byte>> sfx_message;
    std::array<std::uint8_t,4> color{255,255,255,255};
    SavedVector velocity{}, acceleration{}, dimensions{};
    float friction = 0, mass = 0, force_ignore_limit_squared = 0;
    std::uint32_t change_flags = 0;
    std::optional<std::uint16_t> sky_slot;
    std::optional<SavedAnimation> animation;
    std::optional<SavedClient> client;
    // Game-owned MID_SAVEOBJECT data. Its checked field boundaries are
    // interpreted by the save-message adapter, after structural validation.
    std::vector<std::byte> game_data;
    std::vector<SavedAttachment> attachments;
    std::vector<SavedRelation> relations;
    bool operator==(const SavedObject&) const = default;
};
struct SavedGame {
    // Original order: server fields 6c4,6bc,6c0,6d0,6c8,6cc,6b8.
    // Preserve exact bits until the server timing adapter consumes them.
    std::array<std::uint32_t,7> timing_words{};
    std::optional<std::vector<std::vector<std::uint16_t>>> portal_flags;
    std::vector<std::pair<std::string,std::string>> console_variables;
    std::vector<SavedObject> objects;
    bool operator==(const SavedGame&) const = default;
};
// Bounded, staged decoding: failure never publishes a partial document.
// Offsets are relative to the supplied original stream position.
SavedGame DecodeSavedGame(std::span<const std::byte>, std::uint32_t stream_origin = 0);
std::vector<std::byte> EncodeSavedGame(const SavedGame&, std::uint32_t stream_origin = 0);
}
