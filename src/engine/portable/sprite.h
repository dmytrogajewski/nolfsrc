#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace nolf {
struct SpriteAnimation {
    std::uint32_t frames_per_second=1,color_key=0;
    bool keyed=false,translucent=false;
    std::vector<std::string> frames;
};
// NOLF SPR contains one animation: five LE dwords and length-prefixed names.
// Frame assets are resolved separately, so an unused missing frame does not
// prevent parsing the animation metadata.
SpriteAnimation decode_sprite(std::span<const std::byte>);
}
