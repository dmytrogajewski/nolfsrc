#pragma once
#include "engine/portable/binary.h"
#include "platform/filesystem.h"
#include "platform/timing.h"
#include <array>

namespace cursor_fixture {
// Four combinations of AND/XOR, plus a second row proving bottom-up storage.
inline std::vector<std::byte> bitmap(unsigned depth=1,bool top_down=false,bool alpha=false) {
    nolf::BinaryWriter out; out.integer<std::uint16_t>(1); out.integer<std::uint16_t>(0);
    out.integer<std::uint32_t>(40); out.integer<std::uint32_t>(8); out.integer<std::uint32_t>(top_down ? std::uint32_t(-4) : 4);
    out.integer<std::uint16_t>(1); out.integer<std::uint16_t>(depth); out.integer<std::uint32_t>(0);
    const unsigned stride=((8*depth+31)/32)*4;
    out.integer<std::uint32_t>(stride*2); out.integer<std::uint32_t>(0); out.integer<std::uint32_t>(0);
    out.integer<std::uint32_t>(depth<=8 ? 2 : 0); out.integer<std::uint32_t>(0);
    if (depth<=8) { out.integer<std::uint32_t>(0); out.integer<std::uint32_t>(0xffffff); }
    for (unsigned row=0;row<2;++row) {
        const bool first=top_down ? row==0 : row==1; std::vector<std::byte> pixels(stride);
        for (unsigned x=0;x<8;++x) {
            const bool white=first && (x==1 || x==3);
            if (depth<=8) { const auto bit=x*depth; pixels[bit/8]|=std::byte(unsigned(white)<<(8-depth-bit%8)); }
            else for (unsigned c=0;c<depth/8;++c) pixels[x*(depth/8)+c]=std::byte(c==3 ? (alpha ? 128 : 0) : white ? 255 : 0);
        }
        out.bytes(pixels);
    }
    for (unsigned row=0;row<2;++row) {
        const bool first=top_down ? row==0 : row==1;
        const std::array<std::byte,4> mask{std::byte(first ? 0x30 : 0),std::byte{},std::byte{},std::byte{}}; out.bytes(mask);
    }
    return out.data();
}
inline std::vector<std::byte> group(std::size_t bytes,unsigned depth=1) {
    nolf::BinaryWriter out; out.integer<std::uint16_t>(0); out.integer<std::uint16_t>(2); out.integer<std::uint16_t>(1);
    out.integer<std::uint16_t>(8); out.integer<std::uint16_t>(4); out.integer<std::uint16_t>(1); out.integer<std::uint16_t>(depth);
    out.integer<std::uint32_t>(bytes); out.integer<std::uint16_t>(1); return out.data();
}
struct Files {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("nolf-cursors-"+std::to_string(nolf::platform::monotonic_nanoseconds()));
    Files() {
        const auto image=bitmap(),index=group(image.size());
        nolf::platform::atomic_write(root/"resources/cres/1033/raw/1/1.bin",image);
        nolf::platform::atomic_write(root/"resources/cres/1033/raw/12/104.bin",index);
        nolf::platform::atomic_write(root/"resources/cres/1033/raw/12/pointer.bin",index);
    }
    ~Files() { std::error_code error; std::filesystem::remove_all(root,error); }
};
}
