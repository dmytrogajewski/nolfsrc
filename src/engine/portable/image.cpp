#include "engine/portable/image.h"
#include "engine/portable/binary.h"
#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

namespace nolf {
Image::Image(std::uint32_t width,std::uint32_t height,std::uint32_t color) : width_(width),height_(height) {
    if (!width || !height || width>maximum_dimension || height>maximum_dimension ||
        std::uint64_t(width)*height>maximum_pixels) throw std::length_error("invalid image dimensions");
    pixels_.assign(std::size_t(width)*height,color);
}
Image& Image::operator=(const Image& other) {
    if (this!=&other) { Image copy(other); *this=std::move(copy); }
    return *this;
}
Image::Image(Image&& other) noexcept : width_(std::exchange(other.width_,0)),height_(std::exchange(other.height_,0)),pixels_(std::move(other.pixels_)) {}
Image& Image::operator=(Image&& other) noexcept {
    if (this!=&other) { pixels_=std::move(other.pixels_); width_=std::exchange(other.width_,0); height_=std::exchange(other.height_,0); }
    return *this;
}
std::uint32_t Image::at(std::uint32_t x,std::uint32_t y) const {
    if (x>=width_ || y>=height_) throw std::out_of_range("image pixel outside extent");
    return pixels_[std::size_t(y)*width_+x];
}
Image decode_pcx(std::span<const std::byte> bytes) {
    if (bytes.size()<128 || bytes.size()>64*1024*1024) throw std::runtime_error("invalid PCX file size");
    auto byte=[&](std::size_t offset) { return std::to_integer<std::uint8_t>(bytes[offset]); };
    auto word=[&](std::size_t offset) { BinaryReader input(bytes.subspan(offset,2)); return input.integer<std::uint16_t>(); };
    const auto encoding=byte(2),planes=byte(65);
    if (byte(0)!=10 || byte(1)!=5 || encoding>1 || byte(3)!=8 || (planes!=1 && planes!=3))
        throw std::runtime_error("unsupported PCX format");
    const auto left=word(4),top=word(6),right=word(8),bottom=word(10),pitch=word(66);
    if (right<left || bottom<top) throw std::runtime_error("inverted PCX image extent");
    const std::uint32_t width=std::uint32_t(right)-left+1,height=std::uint32_t(bottom)-top+1;
    if (pitch<width) throw std::runtime_error("PCX row pitch is smaller than the image");
    const auto row_size=std::size_t(pitch)*planes;
    std::size_t end=bytes.size();
    std::array<std::uint32_t,256> palette{};
    if (planes==1) {
        if (end<128+769 || byte(end-769)!=12) throw std::runtime_error("PCX palette is missing");
        end-=769;
        for (std::size_t i=0;i<palette.size();++i)
            palette[i]=0xff000000u|(std::uint32_t(byte(end+1+i*3))<<16)|(std::uint32_t(byte(end+2+i*3))<<8)|byte(end+3+i*3);
    }
    const auto decoded=std::uint64_t(row_size)*height;
    if (decoded>64*1024*1024 || end-128<(encoding ? (decoded+62)/63 : decoded)) throw std::runtime_error("invalid PCX decoded extent");
    Image image(width,height);
    std::vector<std::uint8_t> row(row_size);
    std::size_t cursor=128;
    unsigned run=0;
    std::uint8_t value=0;
    auto next=[&]() {
        if (cursor>=end) throw std::runtime_error("truncated PCX pixel data");
        return byte(cursor++);
    };
    for (std::uint32_t y=0;y<height;++y) {
        for (auto& pixel : row) {
            if (!run) {
                value=next(); run=1;
                if (encoding && value>=0xc0) {
                    run=value&0x3f;
                    if (!run) throw std::runtime_error("zero-length PCX run");
                    value=next();
                }
            }
            pixel=value; --run;
        }
        for (std::uint32_t x=0;x<width;++x) {
            image.pixels()[std::size_t(y)*width+x]=planes==1 ? palette[row[x]] :
                0xff000000u|(std::uint32_t(row[x])<<16)|(std::uint32_t(row[pitch+x])<<8)|row[std::size_t(pitch)*2+x];
        }
    }
    if (run) throw std::runtime_error("PCX run exceeds the decoded image");
    return image;
}
}

namespace nolf {
std::vector<std::byte> encode_bmp(const Image& image) {
    if (!image.width() || !image.height()) throw std::invalid_argument("cannot encode an empty image");
    const std::uint32_t pitch=(image.width()*3+3)&~3u;
    const std::uint32_t extent=pitch*image.height();
    BinaryWriter output; output.reserve(54+extent);
    output.integer<std::uint16_t>(0x4d42); output.integer<std::uint32_t>(54+extent);
    output.integer<std::uint32_t>(0); output.integer<std::uint32_t>(54);
    output.integer<std::uint32_t>(40); output.integer(image.width()); output.integer(image.height());
    output.integer<std::uint16_t>(1); output.integer<std::uint16_t>(24);
    output.integer<std::uint32_t>(0); output.integer(extent);
    for (unsigned i=0;i<4;++i) output.integer<std::uint32_t>(0);
    for (unsigned y=image.height();y--;) {
        for (unsigned x=0;x<image.width();++x) {
            const auto pixel=image.at(x,y);
            output.integer<std::uint8_t>(pixel&255); output.integer<std::uint8_t>((pixel>>8)&255); output.integer<std::uint8_t>((pixel>>16)&255);
        }
        for (unsigned i=image.width()*3;i<pitch;++i) output.integer<std::uint8_t>(0);
    }
    return output.data();
}
}
