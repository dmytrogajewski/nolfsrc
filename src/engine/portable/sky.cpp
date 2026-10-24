#include "engine/portable/sky.h"
#include "engine/portable/binary.h"
#include <cmath>
#include <limits>

namespace nolf {
namespace {
bool finite(LTVector value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }
}
bool valid_sky(const SkyDef& sky) { return finite(sky.m_Min) && finite(sky.m_Max) && finite(sky.m_ViewMin) && finite(sky.m_ViewMax); }
SkyInfo decode_sky(std::span<const std::byte> bytes) {
    BinaryReader input(bytes); SkyInfo sky;
    for (auto* vector : {&sky.definition.m_Min,&sky.definition.m_Max,&sky.definition.m_ViewMin,&sky.definition.m_ViewMax})
        for (unsigned axis=0;axis<3;++axis) (*vector)[axis]=input.real();
    const auto count=input.integer<uint16>();
    if (!valid_sky(sky.definition) || count>maximum_sky_objects || input.remaining()!=std::size_t(count)*2) throw std::invalid_argument("invalid sky packet definition or object extent");
    for (unsigned i=0;i<count;++i) sky.objects[i]=input.integer<uint16>();
    return sky;
}
std::vector<std::byte> encode_sky(const SkyInfo& sky) {
    if (!valid_sky(sky.definition)) throw std::invalid_argument("nonfinite sky definition");
    BinaryWriter output; output.reserve(48+2+maximum_sky_objects*2);
    for (const auto* vector : {&sky.definition.m_Min,&sky.definition.m_Max,&sky.definition.m_ViewMin,&sky.definition.m_ViewMax})
        for (unsigned axis=0;axis<3;++axis) output.real((*vector)[axis]);
    output.integer(uint16(maximum_sky_objects)); for (auto object : sky.objects) output.integer(object); return output.data();
}
LTVector sky_view_position(const SkyDef& sky,LTVector minimum,LTVector maximum,LTVector camera) {
    if (!valid_sky(sky) || !finite(minimum) || !finite(maximum) || !finite(camera)) throw std::invalid_argument("nonfinite sky camera inputs");
    LTVector output{0,0,0}; const bool volume=minimum.x!=maximum.x && minimum.y!=maximum.y && minimum.z!=maximum.z;
    for (unsigned axis=0;axis<3;++axis) {
        if (minimum[axis]>maximum[axis]) throw std::invalid_argument("inverted sky world bounds");
        const double fraction=volume ? (double(camera[axis])-minimum[axis])/(double(maximum[axis])-minimum[axis]) : .5;
        const double value=double(sky.m_ViewMin[axis])+(double(sky.m_ViewMax[axis])-sky.m_ViewMin[axis])*fraction;
        if (!std::isfinite(value) || std::abs(value)>std::numeric_limits<float>::max()) throw std::invalid_argument("sky camera position overflow");
        output[axis]=static_cast<float>(value);
    }
    return output;
}
}
