#pragma once
#include "ltbasetypes.h"
#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace nolf {
inline constexpr std::size_t maximum_sky_objects=30;
struct SkyInfo {
    SkyDef definition{{-10,-10,-10},{10,10,10},{0,0,0},{0,0,0}};
    std::array<uint16,maximum_sky_objects> objects;
    SkyInfo() { objects.fill(0xffff); }
};
bool valid_sky(const SkyDef&);
// Engine packet 0x11 body, with twelve LE float32 values and a LE uint16
// object count/ID list. No native SkyDef record is read from wire memory.
SkyInfo decode_sky(std::span<const std::byte>);
std::vector<std::byte> encode_sky(const SkyInfo&);
LTVector sky_view_position(const SkyDef&,LTVector world_min,LTVector world_max,LTVector camera);
}
