#pragma once
#include "engine/portable/image.h"

namespace nolf {
// Retail world lightmap/shadow streams, bounded to one polygon's texels.
std::vector<std::uint16_t> decode_lightmap555(std::span<const std::byte>,std::size_t texels);
std::vector<std::uint8_t> decode_shadow_map(std::span<const std::byte>,std::size_t texels);
Image lightmap_image(std::uint32_t width,std::uint32_t height,std::span<const std::uint16_t>);
std::uint16_t interpolate_lightmap555(std::uint16_t,std::uint16_t,float percent);
std::uint16_t add_lightmap555(std::uint16_t base,std::uint16_t layer,float blend);
}
