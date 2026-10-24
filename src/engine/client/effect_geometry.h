#pragma once
#include "engine/client/client_effects.h"
#include "engine/client/camera_view.h"
#include <span>

namespace nolf {
LTMatrix effect_transform(const LTObject&);
struct EffectVertex {
    LTVector position{0,0,0};
    std::array<uint8,4> color{255,255,255,255};
    float u=0,v=0;
};
// All output is in camera coordinates. Native object transforms affect local
// particle centres and line endpoints; particle size remains a screen square.
std::vector<EffectVertex> particle_vertices(const ParticleSnapshot&,const LTObject&,const CameraView&,
    uint32 texture_width,uint32 texture_height,float near_distance,LTVector light_scale);
std::vector<EffectVertex> line_vertices(std::span<const LTLine>,const LTObject&,const CameraView&,LTVector light_scale);
}
