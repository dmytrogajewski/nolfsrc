#include "engine/client/effect_geometry.h"
#include "engine/server/world_polygon_geometry.h"
#include "engine/server/objtypes.h"
#include <algorithm>
#include <cmath>

namespace nolf {
namespace {
bool finite(LTVector value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }
uint8 channel(float value) { if (!std::isfinite(value)) throw std::invalid_argument("nonfinite effect color"); return uint8(std::clamp(value,0.f,255.f)); }
}
LTMatrix effect_transform(const LTObject& object) {
    auto position=object.m_Pos,scale=object.m_Scale; auto rotation=object.m_Rotation; LTMatrix result;
    if (!finite(position) || !finite(scale)) throw std::invalid_argument("nonfinite effect transform");
    double norm=0; for (auto part : rotation.m_Quat) norm+=double(part)*part;
    if (!std::isfinite(norm) || std::abs(norm-1)>.001) throw std::invalid_argument("invalid effect rotation");
    mat_SetupTransform(&position,&rotation,&scale,&result); return result;
}
namespace {
LTVector camera_point(const LTMatrix& transform,LTVector point,const CameraView& camera) {
    const auto p=world_TransformPoint(transform,point)-camera.position();
    const LTVector result{p.Dot(camera.right()),p.Dot(camera.up()),p.Dot(camera.forward())};
    if (!finite(result)) throw std::invalid_argument("effect camera coordinates overflow");
    return result;
}
}
std::vector<EffectVertex> particle_vertices(const ParticleSnapshot& source,const LTObject& object,const CameraView& camera,uint32 width,uint32 height,float near_distance,LTVector light_scale) {
    if (source.particles.size()>262144 || width>16384 || height>16384 || !finite(light_scale) || !std::isfinite(near_distance) || near_distance<=0) throw std::invalid_argument("invalid particle geometry request");
    const auto transform=effect_transform(object); const auto rectangle=camera.rectangle();
    const float aspect=float(rectangle.right-rectangle.left)/float(rectangle.bottom-rectangle.top)*camera.projection()[0]/camera.projection()[5];
    const float u0=width>4 ? 2.f/width : 0,v0=height>4 ? 2.f/height : 0;
    const std::array<std::array<float,2>,4> corners{{{-1,1},{1,1},{1,-1},{-1,-1}}},uv{{{u0,v0},{1-u0,v0},{1-u0,1-v0},{u0,1-v0}}};
    std::vector<EffectVertex> result; result.reserve(source.particles.size()*4);
    for (const auto& particle : source.particles) {
        if (!finite(particle.position) || !finite(particle.color) || !std::isfinite(particle.alpha) || !std::isfinite(particle.size) || std::fabs(particle.size)>1e12f) throw std::invalid_argument("invalid particle vertex");
        const auto center=camera_point(transform,particle.position,camera); if (center.z<=near_distance || particle.size<=0) continue;
        const float half_x=particle.size*2,half_y=half_x*aspect;
        const auto world_center=world_TransformPoint(transform,particle.position);
        if (!camera.visible(world_center,std::hypot(half_x,half_y))) continue;
        const std::array<uint8,4> color{channel(particle.color.x*object.m_ColorR/255*light_scale.x),channel(particle.color.y*object.m_ColorG/255*light_scale.y),channel(particle.color.z*object.m_ColorB/255*light_scale.z),channel(particle.alpha*object.m_ColorA)};
        for (unsigned i=0;i<4;++i) { const auto position=center+LTVector(corners[i][0]*half_x,corners[i][1]*half_y,0); if (!finite(position)) throw std::invalid_argument("particle extent overflow"); result.push_back({position,color,uv[i][0],uv[i][1]}); }
    }
    return result;
}
std::vector<EffectVertex> line_vertices(std::span<const LTLine> lines,const LTObject& object,const CameraView& camera,LTVector light_scale) {
    if (lines.size()>262144 || !finite(light_scale)) throw std::invalid_argument("invalid line geometry request");
    const auto transform=effect_transform(object); std::vector<EffectVertex> result; result.reserve(lines.size()*2);
    for (const auto& line : lines) for (const auto& point : line.m_Points) result.push_back({camera_point(transform,point.m_Pos,camera),{channel(point.r*255*light_scale.x),channel(point.g*255*light_scale.y),channel(point.b*255*light_scale.z),channel(point.a*object.m_ColorA)},0,0});
    return result;
}
}
