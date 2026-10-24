#include "engine/client/model_shadows.h"
#include "engine/server/world_node.h"
#include "engine/server/world_polygon.h"
#include "rdev.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace nolf {
namespace {
constexpr std::size_t max_receivers=256;
constexpr uint32 surface_invisible=0x4,surface_sky=0x10;
LTVector normalized(LTVector v) { const auto length=v.Mag(); return length>0 ? v/length : v; }
LTVector cross(LTVector a,LTVector b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
LTVector transform(const LTMatrix& m,LTVector p) {
    return {m.m[0][0]*p.x+m.m[0][1]*p.y+m.m[0][2]*p.z+m.m[0][3],m.m[1][0]*p.x+m.m[1][1]*p.y+m.m[1][2]*p.z+m.m[1][3],m.m[2][0]*p.x+m.m[2][1]*p.y+m.m[2][2]*p.z+m.m[2][3]};
}
LTVector rotate(const LTMatrix& m,LTVector p) {
    return {m.m[0][0]*p.x+m.m[0][1]*p.y+m.m[0][2]*p.z,m.m[1][0]*p.x+m.m[1][1]*p.y+m.m[1][2]*p.z,m.m[2][0]*p.x+m.m[2][1]*p.y+m.m[2][2]*p.z};
}
LTMatrix multiply(const LTMatrix& a,const LTMatrix& b) {
    LTMatrix r;
    for (int i=0;i<4;++i) for (int j=0;j<4;++j) r.m[i][j]=a.m[i][0]*b.m[0][j]+a.m[i][1]*b.m[1][j]+a.m[i][2]*b.m[2][j]+a.m[i][3]*b.m[3][j];
    return r;
}
struct Plane { LTVector normal; float distance; };
struct Receiver { const CWorldPart* polygon; const ShadowReceiver* owner; };
// 16x16 white, alpha falling off as 1 - (d/7.5)^3 from the centre (d3d.ren's
// 0x10039210).
uint32_t blob_texture() {
    static uint32_t texture=0; static uint64_t generation=0;
    if (texture && generation==rd_DeviceGeneration()) return texture;
    std::array<uint8_t,16*16*4> pixels{};
    for (int y=0;y<16;++y) for (int x=0;x<16;++x) {
        const float fx=x-7.5f,fy=y-7.5f,t=std::min(std::sqrt(fx*fx+fy*fy),7.5f)/7.5f;
        auto* p=&pixels[(y*16+x)*4]; p[0]=p[1]=p[2]=255;
        p[3]=uint8_t(std::clamp(int((1-t*t*t)*255.9f),0,255));
    }
    texture=rd_GenTexture(); rd_BindTexture(texture);
    rd_TexFilter(RD_LINEAR,RD_LINEAR); rd_TexWrap(false,false); rd_TexMaxLevel(0);
    rd_TexImage(0,RD_FMT_RGBA8,16,16,RD_PIX_RGBA,pixels.data());
    generation=rd_DeviceGeneration();
    return texture;
}
// Walk a BSP along the light's segment (in its space), collecting the
// polygons that face the camera, face up the segment enough to take a
// shadow and lie near enough to it.
void collect(const ShadowReceiver& owner,LTVector start,LTVector end,LTVector camera,float radius,std::vector<Receiver>& out) {
    auto* node=owner.bsp->GetRoot(); if (!node) return;
    if (owner.to_local) { start=transform(*owner.to_local,start); end=transform(*owner.to_local,end); camera=transform(*owner.to_local,camera); }
    const auto direction=normalized(end-start);
    struct Segment { WorldPolyRef* node; LTVector start,end; };
    std::vector<Segment> stack; stack.reserve(64);
    for (;;) {
        if (!node || (node->m_flag&3)) {
            if (stack.empty()) return;
            node=stack.back().node; start=stack.back().start; end=stack.back().end; stack.pop_back(); continue;
        }
        const auto* polygon=node->m_pOwner;
        if (!polygon || !polygon->m_pSurface || !polygon->m_pSurface->m_pPlane) return;
        const auto& plane=*polygon->m_pSurface->m_pPlane;
        const float d1=plane.m_Normal.Dot(start)-plane.m_Dist,d2=plane.m_Normal.Dot(end)-plane.m_Dist;
        if (d1>radius && d2>radius) { node=node->m_Children[1]; continue; }
        if (d1<-radius && d2<-radius) { node=node->m_Children[0]; continue; }
        const int side=d1>0 ? 1 : 0;
        const float t=std::abs(d1-d2)>.00001f ? d1/(d1-d2) : 0;
        const auto middle=start+(end-start)*t;
        if (out.size()<max_receivers && plane.m_Normal.Dot(camera)-plane.m_Dist>1 && plane.m_Normal.Dot(direction)<.7f &&
            !(polygon->m_pSurface->m_nFlags&(surface_invisible|surface_sky)) && (middle-polygon->m_vCentre).Mag()<polygon->m_fRadius+radius &&
            std::none_of(out.begin(),out.end(),[&](const Receiver& r) { return r.polygon==polygon; }))
            out.push_back({polygon,&owner});
        if (stack.size()>=512) { node=nullptr; continue; }
        if (t>0 && t<1) { stack.push_back({node->m_Children[side==0],middle,end}); end=middle; }
        else stack.push_back({node->m_Children[side==0],start,end});
        node=node->m_Children[side];
    }
}
// Keep where dot(n,p) - d > 0 (d3d.ren's 0x10047d80).
std::size_t clip(const LTVector* in,std::size_t count,const Plane& plane,LTVector* out) {
    std::size_t n=0;
    for (std::size_t i=0;i<count;++i) {
        const auto& a=in[i]; const auto& b=in[(i+1)%count];
        const float da=plane.normal.Dot(a)-plane.distance,db=plane.normal.Dot(b)-plane.distance;
        if (da>0) out[n++]=a;
        if ((da>0)!=(db>0)) out[n++]=a+(b-a)*(da/(da-db));
    }
    return n;
}
}
void model_DrawShadow(const Model& model,LTVector position,std::span<const ShadowReceiver> receivers,LTVector camera,const ModelShadowSettings& settings) {
    if (settings.max<=0 || !model.m_Unknown0d4) return;
    const float project=model.m_f0d8,light_distance=model.m_f0dc,half_h=model.m_f0e0,half_w=model.m_f0e4;
    if (!(half_h>0) || !(half_w>0)) return;
    // The one direction d3d.ren fills in: straight down.
    const LTVector down{0,-1,0};
    const auto start=position-down*light_distance;
    const auto direction=normalized(position+model.m_Vec0e8-start);
    const auto end=start+direction*(light_distance+project);
    std::vector<Receiver> found; const float radius=std::max(half_h,half_w);
    for (const auto& receiver : receivers) if (receiver.bsp) collect(receiver,start,end,camera,radius,found);
    if (found.empty()) return;

    // The volume: below the model, above the far cap, and four sides through
    // the light and the blob's corners.
    const auto normal=direction;
    const auto hint=std::abs(normal.y)>.9f ? LTVector(1,0,0) : LTVector(0,1,0);
    const auto right=normalized(cross(hint,normal)),up=cross(right,normal);
    const float far_distance=direction.Dot(end);
    const auto c0=end-up*half_h,c1=end+up*half_h,c2=end+right*half_w,c3=end-right*half_w;
    const auto corner=end-up*half_h-right*half_w;
    std::array<Plane,6> planes;
    planes[0]={normal,normal.Dot(position)};
    planes[1]={normal*-1.f,(normal*-1.f).Dot(position+normal*project)};
    planes[2].normal=normalized(cross(right,c0-start)); planes[4].normal=normalized(cross(c1-start,right));
    planes[3].normal=normalized(cross(up,c2-start)); planes[5].normal=normalized(cross(c3-start,up));
    for (int i=2;i<6;++i) planes[i].distance=planes[i].normal.Dot(start);

    // Texture coordinates: the light's projection onto the far cap's plane,
    // then the blob's basis there, scaled to 0..1.
    LTMatrix shadow,basis,scale;
    const float k=direction.Dot(start)-far_distance;
    for (int r=0;r<3;++r) {
        for (int c=0;c<3;++c) shadow.m[r][c]=(r==c ? k : 0.f)-start[r]*direction[c];
        shadow.m[r][3]=start[r]*far_distance;
    }
    shadow.m[3][0]=-direction.x; shadow.m[3][1]=-direction.y; shadow.m[3][2]=-direction.z; shadow.m[3][3]=k+far_distance;
    for (auto& row : basis.m) for (auto& v : row) v=0;
    basis.m[0][0]=up.x; basis.m[0][1]=up.y; basis.m[0][2]=up.z; basis.m[0][3]=-up.Dot(corner);
    basis.m[1][0]=right.x; basis.m[1][1]=right.y; basis.m[1][2]=right.z; basis.m[1][3]=-right.Dot(corner);
    basis.m[2][2]=basis.m[3][3]=1;
    for (auto& row : scale.m) for (auto& v : row) v=0;
    scale.m[0][0]=1/(2*half_h); scale.m[1][1]=1/(2*half_w); scale.m[2][2]=scale.m[3][3]=1;
    const auto texture=multiply(multiply(scale,basis),shadow);

    rd_PushAttrib(RD_ATTR_COLOR|RD_ATTR_DEPTH|RD_ATTR_ENABLE|RD_ATTR_TEXTURE);
    // Only the blob: no other unit, no culling of the receivers' windings.
    for (int unit=1;unit<g_RDCaps.nTextureUnits;++unit) { rd_ActiveTexture(unit); rd_Disable(RD_TEXTURE_2D); }
    rd_ActiveTexture(0); rd_Enable(RD_TEXTURE_2D); rd_BindTexture(blob_texture()); rd_TexEnv(RD_ENV_MODULATE); rd_CullBack(false);
    rd_Enable(RD_BLEND); rd_BlendFunc(RD_SRC_ALPHA,RD_ONE_MINUS_SRC_ALPHA); rd_Disable(RD_ALPHA_TEST);
    rd_Enable(RD_DEPTH_TEST); rd_DepthFunc(RD_LEQUAL); rd_DepthMask(false);
    rd_Color4ub(0,0,0,uint8_t(std::clamp(settings.alpha,0.f,255.f)));
    rd_MatrixMode(RD_TEXTURE); rd_PushMatrix(); rd_LoadIdentity(); rd_MatrixMode(RD_MODELVIEW);
    std::array<LTVector,64> a,b;
    for (const auto& receiver : found) {
        const auto* polygon=receiver.polygon; const auto vertices=std::size_t(polygon->m_nVerts);
        if (vertices<3 || vertices>32) continue;
        auto lift=polygon->m_pSurface->m_pPlane->m_Normal*settings.offset;
        if (receiver.owner->to_world) lift=rotate(*receiver.owner->to_world,lift);
        std::size_t n=0;
        for (std::size_t v=0;v<vertices;++v) {
            auto p=*polygon->m_Verts[v].m_pPos;
            if (receiver.owner->to_world) p=transform(*receiver.owner->to_world,p);
            a[n++]=p+lift;
        }
        auto* in=a.data(); auto* out=b.data();
        for (std::size_t plane=0;plane<planes.size() && n>=3;++plane) { n=clip(in,n,planes[plane],out); std::swap(in,out); }
        if (n<3) continue;
        rd_Begin(RD_TRIANGLE_FAN);
        for (std::size_t v=0;v<n;++v) {
            const auto& p=in[v];
            rd_TexCoord4f(texture.m[0][0]*p.x+texture.m[0][1]*p.y+texture.m[0][2]*p.z+texture.m[0][3],
                          texture.m[1][0]*p.x+texture.m[1][1]*p.y+texture.m[1][2]*p.z+texture.m[1][3],0,
                          texture.m[3][0]*p.x+texture.m[3][1]*p.y+texture.m[3][2]*p.z+texture.m[3][3]);
            rd_Vertex3f(p.x,p.y,p.z);
        }
        rd_End();
    }
    rd_MatrixMode(RD_TEXTURE); rd_PopMatrix(); rd_MatrixMode(RD_MODELVIEW);
    rd_PopAttrib();
}
}
