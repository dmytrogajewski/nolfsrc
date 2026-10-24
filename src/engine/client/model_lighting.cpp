#include "engine/client/model_lighting.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace nolf {
namespace {
bool finite(LTVector value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }
std::uint8_t channel(float value) { return static_cast<std::uint8_t>(std::clamp(value,0.f,255.f)); }
// The inverse of an affine placement (rotation, scale and translation).
LTMatrix inverse_affine(const LTMatrix& m) {
    const double a=m.m[0][0],b=m.m[0][1],c=m.m[0][2],d=m.m[1][0],e=m.m[1][1],f=m.m[1][2],g=m.m[2][0],h=m.m[2][1],i=m.m[2][2];
    const double determinant=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
    if (!std::isfinite(determinant) || std::abs(determinant)<1e-12) throw std::invalid_argument("model lighting frame is singular");
    const double k=1/determinant;
    LTMatrix r; r.Identity();
    r.m[0][0]=float((e*i-f*h)*k); r.m[0][1]=float((c*h-b*i)*k); r.m[0][2]=float((b*f-c*e)*k);
    r.m[1][0]=float((f*g-d*i)*k); r.m[1][1]=float((a*i-c*g)*k); r.m[1][2]=float((c*d-a*f)*k);
    r.m[2][0]=float((d*h-e*g)*k); r.m[2][1]=float((b*g-a*h)*k); r.m[2][2]=float((a*e-b*d)*k);
    for (unsigned row=0;row<3;++row)
        r.m[row][3]=-(r.m[row][0]*m.m[0][3]+r.m[row][1]*m.m[1][3]+r.m[row][2]*m.m[2][3]);
    return r;
}
LTVector apply(const LTMatrix& m,LTVector p) {
    return {m.m[0][0]*p.x+m.m[0][1]*p.y+m.m[0][2]*p.z+m.m[0][3],
            m.m[1][0]*p.x+m.m[1][1]*p.y+m.m[1][2]*p.z+m.m[1][3],
            m.m[2][0]*p.x+m.m[2][1]*p.y+m.m[2][2]*p.z+m.m[2][3]};
}
}
ModelLighting::ModelLighting(LTVector ambient,LTVector scale,LTVector add,std::uint8_t alpha) : ambient_(ambient),scale_(scale),add_(add),alpha_(alpha) {
    if (!finite(ambient) || !finite(scale) || !finite(add)) throw std::invalid_argument("nonfinite model lighting");
}
ModelLighting::ModelLighting(const LTMatrix& frame,LTVector ambient,LTVector scale,LTVector add,std::uint8_t alpha)
    : ModelLighting(ambient,scale,add,alpha) { to_model_=inverse_affine(frame); }
ModelLighting ModelLighting::unlit(LTVector scale,LTVector add,std::uint8_t alpha) {
    ModelLighting result({255,255,255},scale,add,alpha); result.unlit_=true; return result;
}
void ModelLighting::push(LTVector world_position,float radius,LTVector color) {
    auto& light=lights_[count_++];
    light.position=apply(to_model_,world_position);
    const auto length=light.position.Mag();
    light.direction=length>0 ? light.position/length : LTVector(0,0,0);
    light.squared_radius=radius*radius; light.inverse_squared=1/light.squared_radius;
    light.color=color;
}
void ModelLighting::add(const ModelLightSource& source,LTVector position,float reach,std::size_t limit) {
    if (unlit_ || count_>=std::min(limit,max_lights) || !(source.radius>0) || !finite(source.position) || !finite(source.color)) return;
    if (source.position.Dist(position)-reach>=source.radius) return;
    push(source.position,source.radius,source.color);
}
void ModelLighting::add(const WorldLight& source,LTVector position,float reach,std::size_t limit) {
    if (unlit_ || count_>=std::min(limit,max_lights) || !(source.radius>0)) return;
    const auto offset=position-source.position; const auto distance=offset.Mag();
    if (distance-reach>=source.radius) return;
    auto color=source.color;
    // A spot light reaches models inside its cone, its colour moving from the
    // centre's to the edge's.
    if (source.fov_cos>-1 && source.fov_cos<1 && distance!=0) {
        const auto cosine=source.direction.Dot(offset)/distance;
        if (cosine<source.fov_cos) return;
        const auto t=(1-(.5f*cosine+.5f))/(1-(.5f*source.fov_cos+.5f));
        color=color*(1-t)+source.outer_color*t;
    }
    push(source.position,source.radius,color);
}
void ModelLighting::sun(LTVector direction,LTVector color,float amount) {
    if (unlit_ || !finite(direction) || !finite(color) || !std::isfinite(amount) || !(amount>0)) return;
    // Towards the sun, in the vertices' space (rotation only).
    const auto& m=to_model_;
    sun_direction_={-(m.m[0][0]*direction.x+m.m[0][1]*direction.y+m.m[0][2]*direction.z),
                    -(m.m[1][0]*direction.x+m.m[1][1]*direction.y+m.m[1][2]*direction.z),
                    -(m.m[2][0]*direction.x+m.m[2][1]*direction.y+m.m[2][2]*direction.z)};
    sun_color_=color; sun_amount_=std::min(amount,1.f);
}
std::array<std::uint8_t,4> ModelLighting::flat() const {
    return {channel(ambient_.x*scale_.x+add_.x),channel(ambient_.y*scale_.y+add_.y),channel(ambient_.z*scale_.z+add_.z),alpha_};
}
void ModelLighting::light(std::span<const ModelVertex> vertices,std::vector<std::array<std::uint8_t,4>>& colors) const {
    colors.resize(vertices.size());
    for (std::size_t v=0;v<vertices.size();++v) {
        const auto& vertex=vertices[v]; auto color=ambient_;
        if (sun_amount_>0) {
            const auto facing=vertex.normal.Dot(sun_direction_)*sun_amount_;
            if (facing>0) color+=(sun_color_-color)*facing;
        }
        for (std::size_t i=0;i<count_;++i) {
            const auto& light=lights_[i];
            const auto facing=vertex.normal.Dot(light.direction); if (facing<=0) continue;
            const auto squared=(vertex.position-light.position).MagSqr(); if (squared>=light.squared_radius) continue;
            color+=light.color*(facing*(1-squared*light.inverse_squared));
        }
        colors[v]={channel(color.x*scale_.x+add_.x),channel(color.y*scale_.y+add_.y),channel(color.z*scale_.z+add_.z),alpha_};
    }
}
}
