#include "engine/portable/texture.h"
#include "engine/portable/binary.h"
#include "engine/portable/wire.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace nolf {
namespace {
constexpr std::uint64_t budget=256ull*1024*1024;
std::uint8_t byte(std::byte value) { return std::to_integer<std::uint8_t>(value); }
std::string text(std::span<const std::byte> bytes) {
    const auto end=std::find(bytes.begin(),bytes.end(),std::byte{0});
    return {reinterpret_cast<const char*>(bytes.data()),static_cast<std::size_t>(end-bytes.begin())};
}
std::uint32_t rgb565(std::uint16_t encoded) {
    const auto r=(encoded>>11)&31u,g=(encoded>>5)&63u,b=encoded&31u;
    return 0xff000000u|((r<<3|r>>2)<<16)|((g<<2|g>>4)<<8)|(b<<3|b>>2);
}
std::uint32_t mix(std::uint32_t a,std::uint32_t b,unsigned first,unsigned second,unsigned divisor) {
    std::uint32_t output=0xff000000;
    for (unsigned shift : {0u,8u,16u}) output|=((((a>>shift)&255)*first+((b>>shift)&255)*second)/divisor)<<shift;
    return output;
}
void decode_blocks(Image& image,std::span<const std::byte> data,TextureFormat format) {
    const auto block_size=format==TextureFormat::Dxt1 ? 8u : 16u;
    std::size_t offset=0;
    for (std::uint32_t y=0;y<image.height();y+=4) for (std::uint32_t x=0;x<image.width();x+=4,offset+=block_size) {
        const auto block=data.subspan(offset,block_size);
        const auto colors=block.subspan(format==TextureFormat::Dxt1 ? 0 : 8);
        const auto first=wire::load<std::uint16_t>(colors.data()),second=wire::load<std::uint16_t>(colors.data()+2);
        std::array<std::uint32_t,4> palette{rgb565(first),rgb565(second),0,0};
        if (first>second || format!=TextureFormat::Dxt1) {
            palette[2]=mix(palette[0],palette[1],2,1,3); palette[3]=mix(palette[0],palette[1],1,2,3);
        } else palette[2]=mix(palette[0],palette[1],1,1,2);
        const auto indices=wire::load<std::uint32_t>(colors.data()+4);
        std::array<std::uint8_t,8> alpha{}; std::uint64_t alpha_indices=0;
        if (format==TextureFormat::Dxt5) {
            alpha[0]=byte(block[0]); alpha[1]=byte(block[1]);
            if (alpha[0]>alpha[1]) for (unsigned i=1;i<=6;++i) alpha[i+1]=((7-i)*alpha[0]+i*alpha[1])/7;
            else {
                for (unsigned i=1;i<=4;++i) alpha[i+1]=((5-i)*alpha[0]+i*alpha[1])/5;
                alpha[7]=255;
            }
            for (unsigned i=0;i<6;++i) alpha_indices|=std::uint64_t(byte(block[i+2]))<<(8*i);
        }
        for (unsigned i=0;i<16;++i) {
            if (x+i%4>=image.width() || y+i/4>=image.height()) continue;
            auto pixel=palette[(indices>>(2*i))&3];
            if (format==TextureFormat::Dxt3) pixel=(pixel&0xffffffu)|(std::uint32_t((byte(block[i/2])>>((i%2)*4))&15)*17u<<24);
            if (format==TextureFormat::Dxt5) pixel=(pixel&0xffffffu)|(std::uint32_t(alpha[(alpha_indices>>(3*i))&7])<<24);
            image.pixels()[std::size_t(y+i/4)*image.width()+x+i%4]=pixel;
        }
    }
}
}
Texture decode_dtx(std::span<const std::byte> bytes) {
    if (bytes.size()<164 || bytes.size()>budget) throw std::runtime_error("invalid DTX file size");
    BinaryReader input(bytes); Texture texture; auto& header=texture.header;
    if (input.integer<std::uint32_t>()!=0 || input.integer<std::uint32_t>()!=0xfffffffbu) throw std::runtime_error("unsupported DTX resource or version");
    header.width=input.integer<std::uint16_t>(); header.height=input.integer<std::uint16_t>();
    header.mip_count=input.integer<std::uint16_t>(); header.declared_sections=input.integer<std::uint16_t>();
    header.flags=input.integer<std::uint32_t>(); header.user_flags=input.integer<std::uint32_t>();
    for (auto& value : header.extra) value=input.integer<std::uint8_t>();
    header.commands=text(input.bytes(128)); header.storage_format=header.extra[2];
    const auto format=header.storage_format ? header.storage_format : 3;
    if (format!=3 && format!=4 && format!=5 && format!=6 && format!=7) throw std::runtime_error("unsupported DTX pixel format");
    header.format=static_cast<TextureFormat>(format);
    header.detail_scale=wire::load<float>(reinterpret_cast<const std::byte*>(header.extra.data()+6))+1.f;
    header.detail_angle=wire::load<std::int16_t>(reinterpret_cast<const std::byte*>(header.extra.data()+10));
    if (!std::isfinite(header.detail_scale) || !header.mip_count || header.mip_count>15) throw std::runtime_error("invalid DTX metadata or mip count");
    struct Level { std::uint32_t width,height; std::span<const std::byte> bytes; };
    std::vector<Level> levels; std::uint64_t allocated=0;
    levels.reserve(header.mip_count); texture.mips.reserve(header.mip_count);
    auto width=header.width,height=header.height;
    for (unsigned mip=0;mip<header.mip_count;++mip) {
        if (!width || !height || width>Image::maximum_dimension || height>Image::maximum_dimension ||
            std::uint64_t(width)*height>Image::maximum_pixels) throw std::runtime_error("invalid DTX mip dimensions");
        const auto pixels=std::uint64_t(width)*height;
        allocated+=pixels*4;
        if (allocated>budget) throw std::runtime_error("DTX pixel budget exceeded");
        const auto size=format==3 ? pixels*4 : format==7 ? pixels : std::uint64_t((width+3)/4)*((height+3)/4)*(format==4 ? 8 : 16);
        levels.push_back({width,height,input.bytes(static_cast<std::size_t>(size))}); width>>=1; height>>=1;
    }
    // Older files have an unreliable section count. The fixed flag controls
    // whether that field is meaningful, matching the engine's loader.
    if (header.flags&8) {
        if (std::uint64_t(header.declared_sections)*32>input.remaining()) throw std::runtime_error("truncated DTX section table");
        allocated+=std::uint64_t(header.declared_sections)*sizeof(TextureSection);
        if (allocated>budget) throw std::runtime_error("DTX section budget exceeded");
        texture.sections.reserve(header.declared_sections);
        for (unsigned section=0;section<header.declared_sections;++section) {
            const auto fields=input.bytes(28); const auto size=input.integer<std::uint32_t>();
            const auto payload=input.bytes(size); allocated+=size;
            if (allocated>budget) throw std::runtime_error("DTX section budget exceeded");
            texture.sections.push_back({text(fields.first(15)),text(fields.subspan(15,10)),{payload.begin(),payload.end()}});
        }
        if (input.remaining()) throw std::runtime_error("unexpected trailing DTX data");
    }
    std::array<std::uint32_t,256> palette{};
    if (format==7) {
        const TextureSection* found=nullptr;
        for (const auto& section : texture.sections) if (section.type=="PALLETE32") {
            if (found) throw std::runtime_error("ambiguous DTX palette");
            found=&section;
        }
        if (!found || found->data.size()<palette.size()*4) throw std::runtime_error("missing DTX palette");
        // PALLETE32 stores packed ARGB words, unlike the older byte-ordered
        // palette format. Independent indexed-reader implementation:
        // https://github.com/haekb/godot-dtx-reader/blob/master/Models/DTX.gd
        for (std::size_t i=0;i<palette.size();++i) palette[i]=wire::load<std::uint32_t>(found->data.data()+i*4);
    }
    for (const auto& level : levels) {
        Image image(level.width,level.height);
        if (format==3) for (std::size_t i=0;i<image.pixels().size();++i) image.pixels()[i]=wire::load<std::uint32_t>(level.bytes.data()+i*4);
        else if (format==7) for (std::size_t i=0;i<image.pixels().size();++i) image.pixels()[i]=palette[byte(level.bytes[i])];
        else decode_blocks(image,level.bytes,header.format);
        texture.mips.push_back(std::move(image));
    }
    return texture;
}
}
