#pragma once
#include "engine/portable/image.h"
#include <array>
#include <string>

namespace nolf {
enum class TextureFormat : std::uint8_t { Rgba=3,Dxt1=4,Dxt3=5,Dxt5=6,IndexedRgba=7 };
struct TextureHeader {
    std::uint32_t width=0,height=0,flags=0,user_flags=0;
    std::uint16_t mip_count=0,declared_sections=0;
    std::uint8_t storage_format=0;
    TextureFormat format=TextureFormat::Rgba;
    std::array<std::uint8_t,12> extra{};
    float detail_scale=1;
    std::int16_t detail_angle=0;
    std::string commands;
};
struct TextureSection {
    std::string type,name;
    std::vector<std::byte> data;
};
struct Texture {
    TextureHeader header;
    std::vector<Image> mips;
    std::vector<TextureSection> sections;
};
// Version -5 DTX, with owned top-down pixels and file metadata. Alpha is
// preserved; render materials decide when opacity/fullbright rules apply.
Texture decode_dtx(std::span<const std::byte>);
}
