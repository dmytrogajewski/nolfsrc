#include "engine/portable/cursor.h"
#include "engine/portable/binary.h"
#include <algorithm>
#include <bit>
#include <cstdlib>

namespace nolf {
namespace {
void valid(bool condition) { if (!condition) throw std::runtime_error("invalid native cursor resource"); }
}
std::vector<CursorEntry> decode_cursor_group(std::span<const std::byte> bytes) {
    BinaryReader input(bytes); valid(input.integer<std::uint16_t>()==0 && input.integer<std::uint16_t>()==2);
    const auto count=input.integer<std::uint16_t>(); valid(count && count<=256 && input.remaining()==std::size_t(count)*14);
    std::vector<CursorEntry> entries; entries.reserve(count);
    for (unsigned i=0;i<count;++i) {
        CursorEntry entry; entry.width=input.integer<std::uint16_t>(); entry.height=input.integer<std::uint16_t>(); entry.planes=input.integer<std::uint16_t>(); entry.depth=input.integer<std::uint16_t>(); entry.bytes=input.integer<std::uint32_t>(); entry.id=input.integer<std::uint16_t>();
        valid(entry.width && entry.width<=512 && entry.height && entry.height<=1024 && entry.planes==1 && (entry.depth==1 || entry.depth==4 || entry.depth==8 || entry.depth==24 || entry.depth==32) && entry.bytes>=44 && entry.bytes<=4*1024*1024); entries.push_back(entry);
    }
    return entries;
}
platform::CursorBitmap decode_cursor(std::span<const std::byte> bytes) {
    valid(bytes.size()<=4*1024*1024); BinaryReader input(bytes); platform::CursorBitmap result;
    result.hot_x=input.integer<std::uint16_t>(); result.hot_y=input.integer<std::uint16_t>(); const auto header=input.integer<std::uint32_t>(); valid(header==40 || header==108 || header==124);
    const auto width=std::bit_cast<std::int32_t>(input.integer<std::uint32_t>()),height=std::bit_cast<std::int32_t>(input.integer<std::uint32_t>());
    valid(width>0 && width<=512 && height!=0 && height>=-1024 && height<=1024 && height%2==0);
    result.width=static_cast<std::uint32_t>(width); result.height=static_cast<std::uint32_t>(std::abs(height)/2);
    valid(result.height && result.hot_x<result.width && result.hot_y<result.height && input.integer<std::uint16_t>()==1);
    const auto depth=input.integer<std::uint16_t>(); valid(depth==1 || depth==4 || depth==8 || depth==24 || depth==32); valid(input.integer<std::uint32_t>()==0);
    const auto image_size=input.integer<std::uint32_t>(); input.bytes(8); const auto used=input.integer<std::uint32_t>(); input.bytes(4); input.bytes(header-40);
    const auto colors=depth<=8 ? (used ? used : 1u<<depth) : used; valid(colors<=256 && (depth>8 || colors<=1u<<depth));
    std::vector<std::uint32_t> palette; palette.reserve(colors); for (unsigned i=0;i<colors;++i) palette.push_back(input.integer<std::uint32_t>()&0xffffff);
    const std::size_t stride=((std::size_t(result.width)*depth+31)/32)*4,mask_stride=((result.width+31)/32)*4;
    valid(!image_size || image_size==stride*result.height || image_size==(stride+mask_stride)*result.height);
    valid(input.remaining()==(stride+mask_stride)*result.height);
    const auto xor_image=input.bytes(stride*result.height),and_image=input.bytes(mask_stride*result.height);
    const auto count=std::size_t(result.width)*result.height; result.rgba.resize(count); std::vector<std::uint8_t> mask(count); bool alpha=false,monochrome=true;
    for (unsigned y=0;y<result.height;++y) {
        const auto row=height>0 ? result.height-1-y : y;
        for (unsigned x=0;x<result.width;++x) {
            const auto index=std::size_t(y)*result.width+x,offset=std::size_t(row)*stride; std::uint32_t color=0;
            if (depth<=8) {
                const auto byte=std::to_integer<unsigned>(xor_image[offset+(std::size_t(x)*depth)/8]); const auto p=(byte>>(8-depth-(x*depth)%8))&((1u<<depth)-1); valid(p<palette.size()); color=palette[p];
            } else {
                const auto at=offset+std::size_t(x)*(depth/8); for (unsigned channel=0;channel<depth/8;++channel) color|=std::uint32_t(std::to_integer<unsigned char>(xor_image[at+channel]))<<(channel*8);
            }
            mask[index]=(std::to_integer<unsigned char>(and_image[std::size_t(row)*mask_stride+x/8])>>(7-x%8))&1;
            result.rgba[index]=color; if (depth==32 && color>>24) alpha=true;
            if ((color&0xffffff)!=0 && (color&0xffffff)!=0xffffff) monochrome=false;
        }
    }
    if (alpha) return result;
    const auto packed_stride=(result.width+7)/8; if (monochrome) { result.mono_data.resize(std::size_t(packed_stride)*result.height); result.mono_mask.resize(result.mono_data.size()); }
    for (unsigned y=0;y<result.height;++y) for (unsigned x=0;x<result.width;++x) {
        const auto index=std::size_t(y)*result.width+x; const auto color=result.rgba[index]&0xffffff;
        if (mask[index] && color) result.color_xor=true;
        if (monochrome) {
            const auto at=std::size_t(y)*packed_stride+x/8; const auto bit=std::uint8_t(0x80>>(x%8));
            if (!mask[index]) result.mono_mask[at]|=bit;
            if (bool(mask[index]) != (color!=0xffffff)) result.mono_data[at]|=bit;
        }
        result.rgba[index]=color|(mask[index] ? 0u : 0xff000000u);
    }
    return result;
}
}
