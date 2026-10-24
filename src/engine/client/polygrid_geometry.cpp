#include "engine/client/polygrid_geometry.h"
#include "engine/server/world_polygon_geometry.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace nolf {
namespace {
bool finite(LTVector v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
uint8 channel(double value) { if (!std::isfinite(value)) throw std::invalid_argument("nonfinite polygrid color"); return uint8(std::clamp(value,0.,255.)); }
float coordinate(double value) { if (!std::isfinite(value) || std::abs(value)>std::numeric_limits<float>::max()) throw std::invalid_argument("polygrid coordinate overflow"); return static_cast<float>(value); }
}
PolygridMesh polygrid_mesh(const PolygridSnapshot& grid,const LTObject& object,LTVector camera,uint32 width,uint32 height,LTVector light_scale,bool saturate) {
    const std::uint64_t points=std::uint64_t(grid.width)*grid.height;
    if (grid.width<2 || grid.height<2 || points>65000 || grid.heights.size()!=points || grid.indices.size()%3 || grid.indices.size()>std::size_t(grid.width-1)*(grid.height-1)*6 || width>16384 || height>16384 || !finite(camera) || !finite(light_scale)) throw std::invalid_argument("invalid native polygrid geometry");
    for (auto value : {grid.x_pan,grid.y_pan,grid.x_scale,grid.y_scale}) if (!std::isfinite(value)) throw std::invalid_argument("nonfinite grid texture transform");
    for (auto index : grid.indices) if (index>=points) throw std::invalid_argument("polygrid index exceeds height field");
    for (const auto& c : grid.colors) for (float v : {c.x,c.y,c.z,c.a}) if (!std::isfinite(v)) throw std::invalid_argument("nonfinite polygrid palette");
    auto transform=effect_transform(object);
    for (unsigned row=0;row<3;++row) { transform.m[row][0]*=float(grid.width)/float(grid.width-1); transform.m[row][2]*=float(grid.height)/float(grid.height-1); }
    const LTVector x_axis{transform.m[0][0],transform.m[1][0],transform.m[2][0]},up{transform.m[0][1],transform.m[1][1],transform.m[2][1]},z_axis{transform.m[0][2],transform.m[1][2],transform.m[2][2]};
    const double up_squared=double(up.x)*up.x+double(up.y)*up.y+double(up.z)*up.z;
    const auto length=[](LTVector v) { return std::hypot(double(v.x),double(v.y),double(v.z)); };
    const double x_length=length(x_axis),z_length=length(z_axis),kx=x_length ? up_squared/(256*x_length) : 0,kz=z_length ? up_squared/(256*z_length) : 0;
    const auto sample=[&](std::size_t i) { const auto value=grid.heights[i]; return object.m_Flags&FLAG_UNSIGNED ? int(std::bit_cast<uint8>(value))-128 : int(value); };
    const double u_texel=width ? 1./width : 0,v_texel=height ? 1./height : 0;
    const double u_scale=(grid.x_scale ? 1./grid.x_scale : 1)*u_texel*object.m_Scale.x*grid.width/(grid.width-1),v_scale=(grid.y_scale ? 1./grid.y_scale : 1)*v_texel*object.m_Scale.z*grid.height/(grid.height-1);
    PolygridMesh result; result.indices=grid.indices; result.vertices.reserve(points); LTVector low{0,0,0},high{0,0,0};
    for (uint32 row=0;row<grid.height;++row) for (uint32 col=0;col<grid.width;++col) {
        const auto index=std::size_t(row)*grid.width+col; const auto value=sample(index); const auto palette=(object.m_Flags&FLAG_UNSIGNED) ? std::bit_cast<uint8>(grid.heights[index]) : value+128;
        const auto& c=grid.colors[palette]; const double ramp=saturate ? 2 : 1;
        PolygridVertex vertex; vertex.position=world_TransformPoint(transform,{float(col)-(grid.width-1)*.5f,float(value),float(row)-(grid.height-1)*.5f});
        vertex.color={channel(double(c.x)*ramp*light_scale.x),channel(double(c.y)*ramp*light_scale.y),channel(double(c.z)*ramp*light_scale.z),channel(double(c.a)*object.m_ColorA/255)};
        vertex.u=coordinate((col+grid.x_pan*u_texel)*u_scale); vertex.v=coordinate((row+grid.y_pan*v_texel)*v_scale);
        const double gx=(sample(col ? index-1 : index)-sample(col+1<grid.width ? index+1 : index))*kx,gz=(sample(row ? index-grid.width : index)-sample(row+1<grid.height ? index+grid.width : index))*kz;
        std::array<double,3> normal; double normal_squared=0;
        for (unsigned axis=0;axis<3;++axis) { normal[axis]=up[axis]+x_axis[axis]*gx+z_axis[axis]*gz; normal_squared+=normal[axis]*normal[axis]; }
        if (normal_squared>0) { const auto divisor=std::sqrt(normal_squared); for (auto& n : normal) n/=divisor; }
        std::array<double,3> direction; double dot=0;
        for (unsigned axis=0;axis<3;++axis) { direction[axis]=double(vertex.position[axis])-camera[axis]; dot+=direction[axis]*normal[axis]; }
        double reflected_squared=0; for (unsigned axis=0;axis<3;++axis) { direction[axis]-=normal[axis]*(2*dot); reflected_squared+=direction[axis]*direction[axis]; }
        if (reflected_squared>0) { const auto divisor=std::sqrt(reflected_squared); for (auto& r : direction) r/=divisor; }
        const auto s=normal[1]*normal[1]>.54439 || normal[0]*normal[0]<normal[2]*normal[2] ? direction[0] : direction[2];
        vertex.environment_u=coordinate(-.5*s+.5); vertex.environment_v=coordinate(-.5*direction[1]+.5);
        for (unsigned axis=0;axis<3;++axis) { low[axis]=index ? std::min(low[axis],vertex.position[axis]) : vertex.position[axis]; high[axis]=index ? std::max(high[axis],vertex.position[axis]) : vertex.position[axis]; }
        result.vertices.push_back(vertex);
    }
    result.center=(low+high)*.5f; double radius=0;
    for (const auto& vertex : result.vertices) radius=std::max(radius,length(vertex.position-result.center));
    result.radius=coordinate(radius); return result;
}
}
