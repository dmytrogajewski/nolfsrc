#include "engine/client/sprite_geometry.h"
#include "engine/client/client_sprites.h"
#include "engine/client/client_textures.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <iostream>
#include <numbers>

namespace {
void require(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
void near(float actual,float expected) { require(std::abs(actual-expected)<.0002f,"native sprite position/UV differs"); }
template<class F> void rejects(F&& function) { bool failed=false; try { function(); } catch (const std::exception&) { failed=true; } require(failed,"invalid sprite geometry accepted"); }
void checks() {
    LTRotation identity; identity.Init(); nolf::CameraView camera({0,0,0},identity,{0,0,128,128},1,1);
    nolf::SpriteGeometryRequest request; request.width=8; request.height=4; request.position.Init(10,0,100); request.scale.Init(.5,.25,1);
    auto vertices=nolf::sprite_vertices(request,camera); require(vertices.size()==4,"native billboard quad missing");
    near(vertices[0].position.x,6); near(vertices[0].position.y,1); near(vertices[0].position.z,100); near(vertices[0].u,.25); near(vertices[0].v,0); near(vertices[2].u,.75); near(vertices[2].v,1);
    request.flags=FLAG_SPRITEBIAS; vertices=nolf::sprite_vertices(request,camera); near(vertices[0].position.x,4.8); near(vertices[0].position.y,.8); near(vertices[0].position.z,80);
    request.flags=FLAG_GLOWSPRITE; request.position.Init(0,0,10); vertices=nolf::sprite_vertices(request,camera); near(vertices[0].position.x,-.4);
    request.position.z=500; vertices=nolf::sprite_vertices(request,camera); near(vertices[0].position.x,-8);
    request.position.z=7; request.flags=0; require(nolf::sprite_vertices(request,camera).empty(),"billboard behind near plane drew");
    request.flags=FLAG_REALLYCLOSE; request.position.z=1; nolf::CameraView displaced({100,200,300},identity,{0,0,128,128},1,1);
    vertices=nolf::sprite_vertices(request,displaced); near(vertices[0].position.x,-4); near(vertices[0].position.z,1);
    request.flags=FLAG_ROTATEABLESPRITE; request.width=request.height=2; request.scale.Init(1,1,1); request.position.Init(0,0,100);
    request.rotation=LTRotation(0,0,std::sqrt(.5f),std::sqrt(.5f)); vertices=nolf::sprite_vertices(request,camera); near(vertices[0].position.x,-2); near(vertices[0].position.y,-2);
    request.rotation.Init();
    LTPlane plane; plane.m_Normal.Init(0,0,-1); plane.m_Dist=-100; CWorldSurface surface; surface.m_pPlane=&plane;
    std::array<LTVector,4> points{{{-1,-1,100},{1,-1,100},{1,1,100},{-1,1,100}}}; std::array<WorldPolyVertex,4> boundary{};
    for (unsigned i=0;i<4;++i) boundary[i].m_pPos=&points[i];
    CWorldPart polygon{}; polygon.m_pSurface=&surface; polygon.m_Verts=polygon.m_pVerts2=boundary.data(); polygon.m_nVerts=4;
    request.clip=&polygon; vertices=nolf::sprite_vertices(request,camera); require(vertices.size()==4,"rotatable decal clipping lost quad");
    for (const auto& vertex : vertices) { require(std::abs(vertex.position.x)<=1.0001 && std::abs(vertex.position.y)<=1.0001,"decal escaped convex clipping boundary"); near(vertex.u,.5f+vertex.position.x/4); near(vertex.v,.5f-vertex.position.y/4); }
    std::reverse(boundary.begin(),boundary.end()); const auto reversed=nolf::sprite_vertices(request,camera); require(reversed.size()==4,"clipper winding changed clipped sprite visibility");
    nolf::CameraView behind({0,0,200},identity,{0,0,128,128},1,1); require(nolf::sprite_vertices(request,behind).empty(),"decal camera behind clipping plane drew");
    request.flags=0; vertices=nolf::sprite_vertices(request,camera); near(vertices[0].position.x,-2); // Billboard ignores decal clipper.
    request.width=0; rejects([&] { nolf::sprite_vertices(request,camera); }); request.width=2;
    request.flags=FLAG_ROTATEABLESPRITE; request.rotation.m_Quat[3]=0; rejects([&] { nolf::sprite_vertices(request,camera); });
    request.rotation.Init(); request.position.x=std::numeric_limits<float>::infinity(); rejects([&] { nolf::sprite_vertices(request,camera); });
}
void corpus(const char* directory) {
    auto assets=std::make_shared<nolf::Assets>(); assets->mount_game(directory); nolf::ClientSprites sprites(assets); nolf::ClientTextures textures(assets);
    LTRotation identity; identity.Init(); nolf::CameraView camera({0,0,0},identity,{0,0,128,128},1,1); unsigned count=0,frames=0,missing=0;
    for (const auto& name : assets->names()) if (name.ends_with(".spr")) {
        const auto sprite=sprites.load(name);
        for (const auto& frame : sprite->animation().frames) {
            if (!assets->contains(frame)) { ++missing; continue; }
            const auto texture=textures.load(frame); const auto& header=texture->snapshot()->texture.header;
            nolf::SpriteGeometryRequest request; request.width=header.width; request.height=header.height; request.position.Init(0,0,1000); request.scale.Init(.1,.1,.1);
            for (auto flags : {uint32(0),uint32(FLAG_ROTATEABLESPRITE),uint32(FLAG_GLOWSPRITE|FLAG_SPRITEBIAS),uint32(FLAG_REALLYCLOSE)}) {
                request.flags=flags; const auto vertices=nolf::sprite_vertices(request,camera); require(vertices.size()==4,"original sprite dimensions produced empty geometry");
                for (const auto& vertex : vertices) require(std::isfinite(vertex.position.x) && std::isfinite(vertex.position.y) && std::isfinite(vertex.position.z),"original sprite geometry nonfinite");
            }
            ++frames;
        }
        ++count;
    }
    require(count && frames,"empty original sprite geometry corpus"); std::cout<<"Prepared "<<count<<" original sprites / "<<frames<<" available frames / "<<missing<<" missing frames\n";
}
}
int main(int argc,char** argv) {
    try { checks(); if (argc==2) corpus(argv[1]); else if (argc!=1) throw std::invalid_argument("usage: nolf-sprite-geometry-tests [game-data]"); std::cout<<"Native sprite geometry checks passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
