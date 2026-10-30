#pragma once
#include "ltbasedefs.h"
#include <algorithm>
#include <cmath>
#include <utility>
#include <cstddef>

LTRESULT sound_NormalizeFilename(char*,std::size_t);
LTRESULT sound_NormalizeRequest(PlaySoundInfo&);
inline std::pair<uint16,uint8> sound_QuantizeRange(float outer, float inner) {
    if (!std::isfinite(outer) || outer <= 0) return {0,0};
    const auto range = static_cast<uint16>(std::min(outer,65535.f));
    if (!range || !std::isfinite(inner)) return {range,0};
    return {range,static_cast<uint8>(std::clamp(double(inner) * 255 / range,0.,255.))};
}
inline uint16 sound_ElapsedMilliseconds(float now, float start) {
    const auto elapsed = std::max(0.,(double(now) - start) * 1000 + .5);
    return std::isfinite(elapsed) ? static_cast<uint16>(std::fmod(elapsed,65536.)) : 0;
}
