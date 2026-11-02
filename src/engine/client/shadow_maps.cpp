#include "engine/client/shadow_maps.h"
#include "engine/client/gl_programs.h"
#include "engine/server/world_node.h"
#include "engine/server/world_polygon.h"
#include "rdev.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_set>

namespace nolf {
namespace {
// The units the atlases are sampled from, clear of the fixed pipeline's four.
constexpr int shadow_unit=7,world_unit=6;
constexpr std::size_t max_receivers=1024;
constexpr uint32 surface_translucent=0x8,surface_invisible=0x4,surface_sky=0x10;
struct Matrix {
    std::array<std::array<double,4>,4> m{};
    static Matrix identity() { Matrix r; for (int i=0;i<4;++i) r.m[i][i]=1; return r; }
    Matrix operator*(const Matrix& b) const {
        Matrix r;
        for (int i=0;i<4;++i) for (int j=0;j<4;++j) for (int k=0;k<4;++k) r.m[i][j]+=m[i][k]*b.m[k][j];
        return r;
    }
    std::array<float,16> columns() const {
        std::array<float,16> r{}; for (int i=0;i<4;++i) for (int j=0;j<4;++j) r[j*4+i]=float(m[i][j]); return r;
    }
};
LTVector normalized(LTVector v) { const auto length=v.Mag(); return length>0 ? v/length : v; }
LTVector cross(LTVector a,LTVector b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
LTVector transform(const LTMatrix& m,LTVector p) {
    return {m.m[0][0]*p.x+m.m[0][1]*p.y+m.m[0][2]*p.z+m.m[0][3],m.m[1][0]*p.x+m.m[1][1]*p.y+m.m[1][2]*p.z+m.m[1][3],m.m[2][0]*p.x+m.m[2][1]*p.y+m.m[2][2]*p.z+m.m[2][3]};
}
LTVector rotate(const LTMatrix& m,LTVector p) {
    return {m.m[0][0]*p.x+m.m[0][1]*p.y+m.m[0][2]*p.z,m.m[1][0]*p.x+m.m[1][1]*p.y+m.m[1][2]*p.z,m.m[2][0]*p.x+m.m[2][1]*p.y+m.m[2][2]*p.z};
}
// gluLookAt along a direction.
Matrix look(LTVector eye,LTVector forward) {
    const auto f=normalized(forward);
    const auto hint=std::abs(f.y)<.99f ? LTVector(0,1,0) : LTVector(1,0,0);
    const auto s=normalized(cross(f,hint)),u=cross(s,f);
    Matrix r=Matrix::identity();
    const std::array<LTVector,3> rows{s,u,f*-1.f};
    for (int i=0;i<3;++i) { r.m[i][0]=rows[i].x; r.m[i][1]=rows[i].y; r.m[i][2]=rows[i].z; r.m[i][3]=-rows[i].Dot(eye); }
    return r;
}
Matrix ortho(double half,double near,double far) {
    Matrix r=Matrix::identity();
    r.m[0][0]=r.m[1][1]=1/half; r.m[2][2]=-2/(far-near); r.m[2][3]=-(far+near)/(far-near);
    return r;
}
Matrix perspective(double fov,double near,double far) {
    Matrix r; const double f=1/std::tan(fov/2);
    r.m[0][0]=r.m[1][1]=f; r.m[2][2]=(far+near)/(near-far); r.m[2][3]=2*far*near/(near-far); r.m[3][2]=-1;
    return r;
}
constexpr const char* receiver_vertex=R"(#version 120
uniform mat4 u_shadow;
varying vec4 v_shadow;
varying vec3 v_world;
varying vec3 v_normal;
void main() {
    v_world=gl_Vertex.xyz; v_normal=gl_Normal; v_shadow=u_shadow*gl_Vertex;
    vec4 eye=gl_ModelViewMatrix*gl_Vertex;
    gl_FogFragCoord=abs(eye.z);
    gl_Position=gl_ProjectionMatrix*eye;
}
)";
// A shadow darkens what it falls on by how much of the light the caster
// keeps from it: 9 filtered depth comparisons, as much as the surface is the
// world the light reaches there (9 more, against the world's depth), faded
// towards the end of the caster's reach, off where the surface turns from
// the light, and, when weighted, scaled by the light's brightness there.
constexpr const char* receiver_fragment=R"(#version 120
uniform sampler2DShadow u_map;
uniform sampler2DShadow u_world;
uniform vec4 u_tile;
uniform vec4 u_light;
uniform vec4 u_caster;
uniform vec3 u_color;
uniform vec4 u_spot;
uniform float u_strength;
uniform float u_texel;
uniform float u_soft;
uniform float u_weighted;
uniform float u_fog;
varying vec4 v_shadow;
varying vec3 v_world;
varying vec3 v_normal;
void main() {
    vec3 n=normalize(v_normal);
    vec3 l; float atten=1.0; float along;
    if (u_light.w>0.0) {
        vec3 d=u_light.xyz-v_world; float distance=length(d); l=d/max(distance,0.001);
        atten=clamp(1.0-distance/u_light.w,0.0,1.0);
        if (u_spot.w>-1.0 && dot(-l,u_spot.xyz)<u_spot.w) discard;
        along=dot(v_world-u_caster.xyz,-l);
    } else { l=-u_light.xyz; along=dot(v_world-u_caster.xyz,u_light.xyz); }
    float facing=dot(n,l);
    if (facing<=0.0) discard;
    vec3 c=v_shadow.xyz/v_shadow.w;
    if (c.x<u_tile.x || c.y<u_tile.y || c.x>u_tile.z || c.y>u_tile.w || c.z>=1.0) discard;
    float lit=0.0;
    for (int y=-1;y<=1;++y) for (int x=-1;x<=1;++x)
        lit+=shadow2D(u_map,vec3(c.xy+vec2(float(x),float(y))*u_texel*u_soft,c.z-0.0015)).r;
    lit/=9.0;
    if (lit>=0.999) discard;
    float seen=0.0;
    for (int y=-1;y<=1;++y) for (int x=-1;x<=1;++x)
        seen+=shadow2D(u_world,vec3(c.xy+vec2(float(x),float(y))*u_texel,c.z-0.003)).r;
    seen/=9.0;
    if (seen<=0.001) discard;
    float k=seen*u_strength*(1.0-smoothstep(0.7,1.0,max(along,0.0)/max(u_caster.w,1.0)))*smoothstep(0.0,0.2,facing);
    if (u_weighted>0.5) k*=clamp(atten*atten*dot(u_color,vec3(0.299,0.587,0.114))*3.0,0.0,1.0);
    if (u_fog>0.5) k*=clamp((gl_Fog.end-gl_FogFragCoord)*gl_Fog.scale,0.0,1.0);
    gl_FragColor=vec4(vec3(1.0-clamp((1.0-lit)*k,0.0,0.9)),1.0);
}
)";
// The polygons of a BSP within radius of a segment, in the receiver's space.
void collect(const ShadowReceiver& owner,LTVector start,LTVector end,float radius,std::vector<const CWorldPart*>& out,std::unordered_set<const CWorldPart*>& seen) {
    auto* root=owner.bsp ? owner.bsp->GetRoot() : nullptr; if (!root) return;
    if (owner.to_local) { start=transform(*owner.to_local,start); end=transform(*owner.to_local,end); }
    const auto segment=end-start; const auto length_squared=segment.MagSqr();
    std::vector<WorldPolyRef*> stack{root};
    while (!stack.empty() && out.size()<max_receivers) {
        auto* node=stack.back(); stack.pop_back();
        if (!node || (node->m_flag&3)) continue;
        const auto* polygon=node->m_pOwner;
        if (!polygon || !polygon->m_pSurface || !polygon->m_pSurface->m_pPlane) continue;
        const auto& plane=*polygon->m_pSurface->m_pPlane;
        const float d1=plane.m_Normal.Dot(start)-plane.m_Dist,d2=plane.m_Normal.Dot(end)-plane.m_Dist;
        if (!(d1>radius && d2>radius) && stack.size()<4096) stack.push_back(node->m_Children[0]);
        if (!(d1<-radius && d2<-radius) && stack.size()<4096) stack.push_back(node->m_Children[1]);
        if (polygon->m_pSurface->m_nFlags&(surface_invisible|surface_sky)) continue;
        const float t=length_squared>0 ? std::clamp((polygon->m_vCentre-start).Dot(segment)/length_squared,0.f,1.f) : 0.f;
        if ((start+segment*t-polygon->m_vCentre).Mag()>=polygon->m_fRadius+radius) continue;
        if (seen.insert(polygon).second) out.push_back(polygon);
    }
}
// A polygon's corners in world space; false when it has none to draw.
bool corners(const ShadowReceiver& owner,const CWorldPart& polygon,std::array<LTVector,64>& points,std::size_t& count) {
    count=std::size_t(polygon.m_nVerts); if (count<3 || count>points.size()) return false;
    for (std::size_t v=0;v<count;++v) { const auto p=*polygon.m_Verts[v].m_pPos; points[v]=owner.to_world ? transform(*owner.to_world,p) : p; }
    return true;
}
}
struct ShadowMaps::State {
    struct Shadow {
        std::array<float,16> matrix; std::array<float,4> tile;
        ShadowLight light; LTVector center; float radius,reach,strength;
    };
    GlProgram receiver{"shadow receiver",receiver_vertex,receiver_fragment};
    uint32 target=0,world=0; uint64 generation=0;
    int size=0,tile=0,per_side=0;
    ShadowSettings settings;
    std::vector<Shadow> shadows;
    void release() {
        if (generation==rd_DeviceGeneration()) for (auto handle : {target,world}) if (handle) rd_DeleteDepthTarget(handle);
        target=world=0;
    }
};
ShadowMaps::ShadowMaps() : state_(std::make_unique<State>()) {}
ShadowMaps::~ShadowMaps() { state_->release(); }
bool ShadowMaps::available() { return rd_DeviceGeneration() && g_RDCaps.bShaders && g_RDCaps.bDepthTargets && g_RDCaps.nShaderUnits>std::max(shadow_unit,world_unit); }
std::size_t ShadowMaps::count() const { return state_->shadows.size(); }
bool ShadowMaps::begin(const ShadowSettings& settings) {
    auto& s=*state_; s.shadows.clear();
    if (!available() || settings.tile<16 || settings.tiles<1) return false;
    const int per_side=int(std::ceil(std::sqrt(double(std::min(settings.tiles,256)))));
    int tile=std::min(settings.tile,4096);
    while (per_side*tile>g_RDCaps.nMaxTextureSize && tile>16) tile/=2;
    const int size=per_side*tile;
    if (s.generation!=rd_DeviceGeneration()) { s.target=s.world=0; s.generation=rd_DeviceGeneration(); }
    if (!s.target || !s.world || size!=s.size) {
        s.release(); s.target=rd_CreateDepthTarget(size,size); s.world=rd_CreateDepthTarget(size,size); s.generation=rd_DeviceGeneration();
        if (!s.target || !s.world) { s.release(); return false; }
    }
    s.size=size; s.tile=tile; s.per_side=per_side; s.settings=settings; s.settings.tiles=std::min(settings.tiles,per_side*per_side);
    return true;
}
bool ShadowMaps::add(const ShadowCaster& caster,const ShadowLight& light,std::span<const ShadowReceiver> occluders) {
    auto& s=*state_;
    if (!s.target || int(s.shadows.size())>=s.settings.tiles || (caster.meshes.empty() && caster.triangles.size()<3) || !(caster.radius>0) || !(caster.strength>0)) return false;
    const double radius=caster.radius*1.05+1;
    Matrix view,projection;
    if (light.sun) {
        const auto forward=normalized(light.direction); if (forward.MagSqr()<.5f) return false;
        view=look(caster.center-forward*float(radius+64),forward);
        projection=ortho(radius,1,64+2*radius+caster.reach);
    } else {
        const auto offset=caster.center-light.position; const double distance=offset.Mag();
        if (distance<=radius*1.05) return false;
        view=look(light.position,offset);
        const double near=std::max(1.,distance-radius-8);
        double far=distance+radius+caster.reach;
        if (light.radius>0) far=std::min(far,std::max(double(light.radius),distance+radius));
        projection=perspective(2*std::asin(std::min(.995,radius/distance)),near,std::max(far,near+1));
    }
    const int index=int(s.shadows.size()),column=index%s.per_side,row=index/s.per_side;
    const int x=column*s.tile,y=row*s.tile;
    // rd_Viewport counts rows from the top; the texture's from the bottom.
    const double gl_y=s.size-y-s.tile,scale=.5*s.tile/s.size;
    Matrix atlas=Matrix::identity();
    atlas.m[0][0]=scale; atlas.m[0][3]=(x+.5*s.tile)/s.size;
    atlas.m[1][1]=scale; atlas.m[1][3]=(gl_y+.5*s.tile)/s.size;
    atlas.m[2][2]=.5; atlas.m[2][3]=.5;
    const double margin=(1.5*s.settings.softness+1)/s.size;
    State::Shadow shadow{(atlas*projection*view).columns(),
        {float(double(x)/s.size+margin),float(gl_y/s.size+margin),float(double(x+s.tile)/s.size-margin),float((gl_y+s.tile)/s.size-margin)},
        light,caster.center,float(radius),caster.reach,std::clamp(caster.strength,0.f,1.f)};

    rd_PushAttrib(RD_ATTR_ALL);
    rd_MatrixMode(RD_PROJECTION); rd_PushMatrix(); rd_LoadMatrix(projection.columns().data());
    rd_MatrixMode(RD_MODELVIEW); rd_PushMatrix(); rd_LoadMatrix(view.columns().data());
    if (g_RDCaps.bNearDepth) rd_NearDepth(0,0,0,0);
    if (g_RDCaps.bDepthClamp) rd_Disable(RD_DEPTH_CLAMP);
    rd_BindDepthTarget(s.target);
    rd_Viewport(x,y,s.tile,s.tile); rd_DepthRange(0,1);
    rd_Enable(RD_SCISSOR_TEST); rd_Scissor(x,y,s.tile,s.tile);
    rd_ColorMask(false); rd_DepthMask(true); rd_Enable(RD_DEPTH_TEST); rd_DepthFunc(RD_LEQUAL);
    rd_Disable(RD_BLEND); rd_Disable(RD_ALPHA_TEST); rd_Disable(RD_FOG); rd_CullBack(false);
    for (int unit=0;unit<g_RDCaps.nTextureUnits;++unit) { rd_ActiveTexture(unit); rd_Disable(RD_TEXTURE_2D); }
    rd_ActiveTexture(0);
    rd_Clear(RD_CLEAR_DEPTH);
    rd_PolygonOffset(1.5f,4);
    rd_Begin(RD_TRIANGLES);
    for (const auto& mesh : caster.meshes) {
        if (!mesh.data) continue;
        for (const auto& triangle : mesh.data->triangles()) for (unsigned corner=0;corner<3;++corner) {
            const auto vertex=triangle.indices[corner]; if (vertex>=mesh.positions.size()) continue;
            const auto& p=mesh.positions[vertex]; rd_Vertex3f(p.x,p.y,p.z);
        }
    }
    for (std::size_t i=0;i+2<caster.triangles.size();i+=3) for (unsigned corner=0;corner<3;++corner) {
        const auto& p=caster.triangles[i+corner]; rd_Vertex3f(p.x,p.y,p.z);
    }
    rd_End();
    // What the light sees of the world from there, which a shadow falls on
    // only where it is nearest: from the light (the sun's tile start) to the
    // end of the shadow's reach, as wide as the shadow gets.
    rd_BindDepthTarget(s.world); rd_Viewport(x,y,s.tile,s.tile); rd_Scissor(x,y,s.tile,s.tile); rd_Clear(RD_CLEAR_DEPTH);
    {
        const auto forward=light.sun ? normalized(light.direction) : normalized(caster.center-light.position);
        const auto from=light.sun ? caster.center-forward*float(radius+64) : light.position;
        const float distance=light.sun ? 0.f : (caster.center-light.position).Mag();
        const float width=float(radius)*(light.sun || distance<=0 ? 1.f : 1+(float(radius)+caster.reach)/distance);
        const auto to=caster.center+forward*(float(radius)+caster.reach);
        std::vector<const CWorldPart*> found; std::unordered_set<const CWorldPart*> seen; std::array<LTVector,64> points;
        for (const auto& occluder : occluders) {
            if (!occluder.bsp || occluder.bsp==caster.own) continue;
            found.clear(); seen.clear(); collect(occluder,from,to,width,found,seen);
            for (const auto* polygon : found) {
                std::size_t count=0;
                if ((polygon->m_pSurface->m_nFlags&surface_translucent) || !corners(occluder,*polygon,points,count)) continue;
                rd_Begin(RD_TRIANGLE_FAN);
                for (std::size_t v=0;v<count;++v) rd_Vertex3f(points[v].x,points[v].y,points[v].z);
                rd_End();
            }
        }
    }
    rd_PolygonOffset(0,0);
    rd_BindDepthTarget(0);
    rd_MatrixMode(RD_MODELVIEW); rd_PopMatrix(); rd_MatrixMode(RD_PROJECTION); rd_PopMatrix(); rd_MatrixMode(RD_MODELVIEW);
    rd_PopAttrib();
    s.shadows.push_back(shadow);
    return true;
}
std::size_t ShadowMaps::draw(std::span<const ShadowReceiver> receivers,LTVector camera,bool fog) {
    auto& s=*state_;
    if (s.shadows.empty() || !s.target || s.generation!=rd_DeviceGeneration()) return 0;
    rd_PushAttrib(RD_ATTR_ALL);
    FixedPipeline fixed;
    if (!s.receiver.use()) { rd_PopAttrib(); return 0; }
    for (int unit=0;unit<g_RDCaps.nTextureUnits;++unit) { rd_ActiveTexture(unit); rd_Disable(RD_TEXTURE_2D); }
    rd_ActiveTexture(shadow_unit); rd_BindTexture(rd_DepthTargetTexture(s.target));
    rd_ActiveTexture(world_unit); rd_BindTexture(rd_DepthTargetTexture(s.world)); rd_ActiveTexture(0);
    rd_Enable(RD_BLEND); rd_BlendFunc(RD_ZERO,RD_SRC_COLOR); rd_Disable(RD_ALPHA_TEST);
    rd_Enable(RD_DEPTH_TEST); rd_DepthFunc(RD_LEQUAL); rd_DepthMask(false); rd_ColorMask(true); rd_CullBack(false);
    rd_PolygonOffset(-1,-4);
    rd_Uniform1i(s.receiver.uniform("u_map"),shadow_unit); rd_Uniform1i(s.receiver.uniform("u_world"),world_unit);
    rd_Uniform1f(s.receiver.uniform("u_texel"),1.f/s.size);
    rd_Uniform1f(s.receiver.uniform("u_soft"),s.settings.softness);
    rd_Uniform1f(s.receiver.uniform("u_weighted"),s.settings.weighted ? 1.f : 0.f);
    rd_Uniform1f(s.receiver.uniform("u_fog"),fog ? 1.f : 0.f);
    std::size_t drawn=0; std::vector<const CWorldPart*> found; std::unordered_set<const CWorldPart*> seen;
    std::array<LTVector,64> points;
    for (const auto& shadow : s.shadows) {
        const auto& light=shadow.light;
        const auto direction=light.sun ? normalized(light.direction) : normalized(shadow.center-light.position);
        const float distance=light.sun ? 0 : (shadow.center-light.position).Mag();
        // The shadow's volume: from the caster away from the light, widening
        // with a point light's spread.
        const float spread=light.sun || distance<=0 ? 1 : 1+(shadow.radius+shadow.reach)/distance;
        const auto end=shadow.center+direction*(shadow.radius+shadow.reach);
        rd_Uniform1f(s.receiver.uniform("u_strength"),std::clamp(s.settings.strength,0.f,1.f)*shadow.strength);
        rd_UniformMatrix4(s.receiver.uniform("u_shadow"),shadow.matrix.data());
        rd_Uniform4f(s.receiver.uniform("u_tile"),shadow.tile[0],shadow.tile[1],shadow.tile[2],shadow.tile[3]);
        if (light.sun) rd_Uniform4f(s.receiver.uniform("u_light"),direction.x,direction.y,direction.z,0);
        else rd_Uniform4f(s.receiver.uniform("u_light"),light.position.x,light.position.y,light.position.z,std::max(light.radius,1.f));
        rd_Uniform4f(s.receiver.uniform("u_caster"),shadow.center.x,shadow.center.y,shadow.center.z,shadow.radius+shadow.reach);
        rd_Uniform3f(s.receiver.uniform("u_color"),light.color.x/255,light.color.y/255,light.color.z/255);
        rd_Uniform4f(s.receiver.uniform("u_spot"),light.direction.x,light.direction.y,light.direction.z,light.cone);
        for (const auto& receiver : receivers) {
            found.clear(); seen.clear();
            collect(receiver,shadow.center,end,shadow.radius*spread,found,seen);
            for (const auto* polygon : found) {
                const auto count=std::size_t(polygon->m_nVerts); if (count<3 || count>points.size()) continue;
                auto normal=polygon->m_pSurface->m_pPlane->m_Normal; if (receiver.to_world) normal=normalized(rotate(*receiver.to_world,normal));
                for (std::size_t v=0;v<count;++v) { auto p=*polygon->m_Verts[v].m_pPos; points[v]=receiver.to_world ? transform(*receiver.to_world,p) : p; }
                if (normal.Dot(camera-points[0])<=0) continue;
                const auto toward=light.sun ? direction*-1.f : light.position-points[0];
                if (normal.Dot(toward)<=0) continue;
                rd_Begin(RD_TRIANGLE_FAN);
                for (std::size_t v=0;v<count;++v) { rd_Normal3f(normal.x,normal.y,normal.z); rd_Vertex3f(points[v].x,points[v].y,points[v].z); }
                rd_End(); ++drawn;
            }
        }
    }
    rd_PolygonOffset(0,0);
    rd_ActiveTexture(shadow_unit); rd_BindTexture(0); rd_ActiveTexture(world_unit); rd_BindTexture(0); rd_ActiveTexture(0);
    rd_PopAttrib();
    return drawn;
}
}
