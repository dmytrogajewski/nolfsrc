#include "engine/portable/sprite.h"
#include "engine/portable/binary.h"
#include "platform/filesystem.h"
#include <algorithm>

namespace nolf {
SpriteAnimation decode_sprite(std::span<const std::byte> bytes) {
    if (bytes.size()<20 || bytes.size()>16*1024*1024) throw std::invalid_argument("invalid SPR extent");
    BinaryReader input(bytes); const auto count=input.integer<std::uint32_t>(); SpriteAnimation sprite;
    sprite.frames_per_second=input.integer<std::uint32_t>();
    const auto keyed=input.integer<std::uint32_t>(),translucent=input.integer<std::uint32_t>();
    sprite.keyed=keyed!=0; sprite.translucent=translucent!=0; sprite.color_key=input.integer<std::uint32_t>();
    if (count>65536 || std::uint64_t(count)*3>input.remaining() || !sprite.frames_per_second || sprite.frames_per_second>1000 || keyed>1 || translucent>1)
        throw std::invalid_argument("invalid SPR frame table or rate");
    sprite.frames.reserve(count);
    for (std::uint32_t i=0;i<count;++i) {
        const auto length=input.integer<std::uint16_t>();
        if (!length || length>1000) throw std::invalid_argument("invalid SPR frame name length");
        const auto name=input.bytes(length);
        if (std::find(name.begin(),name.end(),std::byte{0})!=name.end()) throw std::invalid_argument("embedded NUL in SPR frame name");
        sprite.frames.push_back(platform::asset_key({reinterpret_cast<const char*>(name.data()),name.size()}));
    }
    if (input.remaining()) throw std::invalid_argument("trailing SPR bytes");
    return sprite;
}
}
