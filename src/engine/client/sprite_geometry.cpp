#include "engine/client/sprite_geometry.h"
#include "engine/portable/image.h"
#include "ltquatbase.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace nolf {
namespace {
bool finite(LTVector value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }
float dot(LTVector axis,LTVector point,LTVector origin) {
    const double value=double(axis.x)*(double(point.x)-origin.x)+double(axis.y)*(double(point.y)-origin.y)+double(axis.z)*(double(point.z)-origin.z);
    if (!std::isfinite(value) || std::abs(value)>std::numeric_limits<float>::max()) throw std::invalid_argument("sprite camera coordinates overflow");
    return static_cast<float>(value);
}
std::vector<SpriteVertex> clip(std::vector<SpriteVertex> vertices,const LTPlane& plane,std::span<const LTVector> boundary,const CameraView& camera) {
    if (!finite(plane.m_Normal) || !std::isfinite(plane.m_Dist)) throw std::invalid_argument("invalid sprite clipping plane");
    if (dot(plane.m_Normal,camera.position(),{0,0,0})-plane.m_Dist<=.01f) return {};
    if (boundary.size()<3 || boundary.size()>512) throw std::invalid_argument("invalid sprite clipping polygon");
    std::array<double,3> center{};
    for (const auto& vertex : boundary) {
        if (!finite(vertex)) throw std::invalid_argument("invalid sprite clip vertex");
        for (unsigned axis=0;axis<3;++axis) center[axis]+=double(vertex[axis])/boundary.size();
    }
    std::vector<SpriteVertex> next; next.reserve(boundary.size()+4); vertices.reserve(boundary.size()+4);
    for (std::size_t edge=0;edge<boundary.size() && !vertices.empty();++edge) {
        const auto a=boundary[edge],b=boundary[(edge+1)%boundary.size()];
        const std::array<double,3> d{double(b.x)-a.x,double(b.y)-a.y,double(b.z)-a.z};
        const auto n=plane.m_Normal;
        std::array<double,3> normal{d[1]*n.z-d[2]*n.y,d[2]*n.x-d[0]*n.z,d[0]*n.y-d[1]*n.x};
        double length=0,side=0;
        for (unsigned axis=0;axis<3;++axis) { length+=normal[axis]*normal[axis]; side+=normal[axis]*(center[axis]-a[axis]); }
        if (length<1e-20) continue;
        const double scale=(side<0 ? -1. : 1.)/std::sqrt(length); for (auto& value : normal) value*=scale;
        auto distance=[&](const SpriteVertex& vertex) { double value=0; for (unsigned axis=0;axis<3;++axis) value+=normal[axis]*(double(vertex.position[axis])-a[axis]); return value; };
        next.clear(); auto prior=vertices.back(); auto previous=distance(prior);
        for (const auto& current : vertices) {
            const auto now=distance(current); const bool inside=now>=0,was_inside=previous>=0;
            if (inside!=was_inside) {
                const double fraction=std::clamp(previous/(previous-now),0.,1.); SpriteVertex intersection;
                for (unsigned axis=0;axis<3;++axis) intersection.position[axis]=static_cast<float>(double(prior.position[axis])+(double(current.position[axis])-prior.position[axis])*fraction);
                intersection.u=static_cast<float>(double(prior.u)+(double(current.u)-prior.u)*fraction);
                intersection.v=static_cast<float>(double(prior.v)+(double(current.v)-prior.v)*fraction); next.push_back(intersection);
            }
            if (inside) next.push_back(current);
            if (next.size()>boundary.size()+4) throw std::length_error("sprite clipping exceeded convex polygon budget");
            prior=current; previous=now;
        }
        vertices.swap(next);
    }
    return vertices.size()<3 ? std::vector<SpriteVertex>{} : vertices;
}
}
std::vector<SpriteVertex> sprite_vertices(const SpriteGeometryRequest& request,const CameraView& camera) {
    if (!finite(request.position) || !finite(request.scale) || !request.width || !request.height || request.width>Image::maximum_dimension || request.height>Image::maximum_dimension)
        throw std::invalid_argument("invalid native sprite geometry");
    const bool rotated=request.flags&FLAG_ROTATEABLESPRITE,close=(request.flags&FLAG_REALLYCLOSE) && !rotated;
    const float u0=request.width>4 ? 2.f/request.width : 0,u1=1-u0,v0=request.height>4 ? 2.f/request.height : 0,v1=1-v0;
    std::vector<SpriteVertex> result; result.reserve(4);
    const std::array<std::array<float,2>,4> corners{{{-1,1},{1,1},{1,-1},{-1,-1}}};
    const std::array<std::array<float,2>,4> uv{{{u0,v0},{u1,v0},{u1,v1},{u0,v1}}};
    if (rotated) {
        double norm=0; for (auto part : request.rotation.m_Quat) { if (!std::isfinite(part)) throw std::invalid_argument("invalid sprite rotation"); norm+=double(part)*part; }
        if (std::abs(norm-1)>.001) throw std::invalid_argument("sprite rotation is not normalized");
        LTVector right{0,0,0},up{0,0,0},forward{0,0,0}; quat_GetVectors(request.rotation.m_Quat,&right.x,&up.x,&forward.x);
        for (unsigned i=0;i<4;++i) result.push_back({request.position+right*(corners[i][0]*request.width*request.scale.x)+up*(corners[i][1]*request.height*request.scale.y),uv[i][0],uv[i][1]});
        for (const auto& vertex : result) if (!finite(vertex.position)) throw std::invalid_argument("native rotated sprite geometry overflow");
        if (request.clip_geometry) result=clip(std::move(result),request.clip_geometry->plane,request.clip_geometry->points,camera);
        else if (request.clip) {
            const auto* plane=request.clip->m_pSurface ? request.clip->m_pSurface->m_pPlane : nullptr;
            if (!plane) throw std::invalid_argument("missing sprite clipping plane");
            const auto source=request.clip->render_vertices(); std::vector<LTVector> boundary; boundary.reserve(source.size());
            for (const auto& vertex : source) { if (!vertex.m_pPos) throw std::invalid_argument("missing sprite clip vertex"); boundary.push_back(*vertex.m_pPos); }
            result=clip(std::move(result),*plane,boundary,camera);
        }
        for (auto& vertex : result) {
            const auto point=vertex.position;
            vertex.position.Init(dot(camera.right(),point,camera.position()),dot(camera.up(),point,camera.position()),dot(camera.forward(),point,camera.position()));
        }
    } else {
        LTVector center=request.position;
        if (!close) center.Init(dot(camera.right(),request.position,camera.position()),dot(camera.up(),request.position,camera.position()),dot(camera.forward(),request.position,camera.position()));
        if (center.z<=(close ? .01f : 7.f)) return {};
        const auto glow=(request.flags&FLAG_GLOWSPRITE) ? .1f+1.9f*std::clamp((center.z-10)/490,0.f,1.f) : 1.f;
        // Sprite bias (billboards only): drawn 20 units nearer, never nearer
        // than 7, the same size on screen -- the quad scaled towards the eye.
        float bias=1;
        if (request.flags&FLAG_SPRITEBIAS) { auto z=center.z-20; if (z<7) z=std::min(7.f,center.z); bias=z/center.z; }
        for (unsigned i=0;i<4;++i) result.push_back({(center+LTVector(corners[i][0]*request.width*request.scale.x*glow,corners[i][1]*request.height*request.scale.y*glow,0))*bias,uv[i][0],uv[i][1]});
    }
    for (const auto& vertex : result) if (!finite(vertex.position)) throw std::invalid_argument("native sprite geometry overflow");
    return result;
}
}
