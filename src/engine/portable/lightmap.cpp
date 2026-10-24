#include "engine/portable/lightmap.h"
#include "engine/portable/binary.h"
#include <algorithm>
#include <cmath>

namespace nolf {
namespace {
void extent(std::size_t texels) { if (!texels || texels>1024) throw std::invalid_argument("invalid lightmap texel count"); }
int fraction(float value) {
    if (!std::isfinite(value) || value<0 || value>1) throw std::invalid_argument("invalid lightmap fraction");
    return static_cast<int>(value*64);
}
}
std::vector<std::uint16_t> decode_lightmap555(std::span<const std::byte> data,std::size_t texels) {
    extent(texels);
    if (data.size()>texels*3) throw std::invalid_argument("lightmap stream exceeds encoding bounds");
    BinaryReader input(data); std::vector<std::uint16_t> output; output.reserve(texels);
    while (input.remaining()) {
        const auto encoded=input.integer<std::uint16_t>();
        const unsigned run=(encoded&0x8000) ? input.integer<std::uint8_t>() : 1;
        if (!run || run>texels-output.size()) throw std::invalid_argument("invalid lightmap run");
        output.insert(output.end(),run,encoded&0x7fff);
    }
    if (output.size()!=texels) throw std::invalid_argument("incomplete lightmap stream");
    return output;
}
std::vector<std::uint8_t> decode_shadow_map(std::span<const std::byte> data,std::size_t texels) {
    extent(texels);
    if (data.size()>texels*2+1) throw std::invalid_argument("shadow stream exceeds encoding bounds");
    std::vector<std::uint8_t> output; output.reserve(texels); std::uint8_t value=0;
    for (const auto byte : data) {
        const auto run=std::to_integer<unsigned>(byte);
        if (run>texels-output.size()) throw std::invalid_argument("invalid shadow run");
        output.insert(output.end(),run,value); value^=255;
    }
    if (output.size()!=texels) throw std::invalid_argument("incomplete shadow stream");
    return output;
}
Image lightmap_image(std::uint32_t width,std::uint32_t height,std::span<const std::uint16_t> texels) {
    extent(std::uint64_t(width)*height);
    if (texels.size()!=std::uint64_t(width)*height) throw std::invalid_argument("lightmap dimensions differ from texels");
    Image image(width,height);
    for (std::size_t i=0;i<texels.size();++i) {
        const auto pixel=texels[i]; if (pixel&0x8000) throw std::invalid_argument("invalid unpacked 555 lightmap color");
        image.pixels()[i]=0xff000000u|(((pixel>>10)&31)*255/31<<16)|(((pixel>>5)&31)*255/31<<8)|((pixel&31)*255/31);
    }
    return image;
}
std::uint16_t interpolate_lightmap555(std::uint16_t first,std::uint16_t second,float percent) {
    const auto step=fraction(percent);
    if ((first|second)&0x8000) throw std::invalid_argument("invalid unpacked lightmap color");
    if (percent<1.f/32) return first;
    if (percent>31.f/32) return second;
    std::uint16_t pixel=0;
    for (unsigned shift : {0u,5u,10u}) {
        const int a=(first>>shift)&31,b=(second>>shift)&31;
        pixel|=static_cast<std::uint16_t>(std::clamp(a+((b-a)*step>>6),0,31)<<shift);
    }
    return pixel;
}
std::uint16_t add_lightmap555(std::uint16_t base,std::uint16_t layer,float blend) {
    const auto step=fraction(blend);
    if ((base|layer)&0x8000) throw std::invalid_argument("invalid unpacked lightmap color");
    std::uint16_t pixel=0;
    for (unsigned shift : {0u,5u,10u}) pixel|=static_cast<std::uint16_t>(std::min(31,int((base>>shift)&31)+(int((layer>>shift)&31)*step>>6))<<shift);
    return pixel;
}
}
