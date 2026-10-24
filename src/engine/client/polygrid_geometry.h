#pragma once
#include "engine/client/effect_geometry.h"
namespace nolf {
struct PolygridVertex {
    LTVector position{0,0,0};
    std::array<uint8,4> color{255,255,255,255};
    float u=0,v=0,environment_u=0,environment_v=0;
};
struct PolygridMesh {
    std::vector<PolygridVertex> vertices;
    std::vector<uint16> indices;
    LTVector center{0,0,0};
    float radius=0;
};
// Canonical owned height field; positions and reflection coordinates use the
// native object transform. The PC grid covers its full width/height in cells.
PolygridMesh polygrid_mesh(const PolygridSnapshot&,const LTObject&,LTVector camera,
    uint32 texture_width,uint32 texture_height,LTVector light_scale,bool saturate=false);
}
