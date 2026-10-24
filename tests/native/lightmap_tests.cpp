#include "engine/portable/lightmap.h"
#include "engine/portable/assets.h"
#include "engine/portable/asset_stream.h"
#include "engine/portable/wire.h"
#include "engine/server/world_state.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>

namespace {
using Bytes=std::vector<std::byte>;
void require(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
template<class F> void rejects(F&& function) { bool failed=false; try { function(); } catch (const std::exception&) { failed=true; } require(failed,"invalid lightmap accepted"); }
Bytes words(std::initializer_list<std::uint16_t> values) {
    Bytes result; for (auto value : values) { const auto at=result.size(); result.resize(at+2); nolf::wire::store(result.data()+at,value); } return result;
}
void decoders() {
    const auto raw=words({0x7c00,0x03e0,0x001f}); const auto pixels=nolf::decode_lightmap555(raw,3);
    require(pixels==std::vector<std::uint16_t>{0x7c00,0x03e0,0x001f},"555 lightmap channels changed");
    const auto image=nolf::lightmap_image(3,1,pixels);
    require(image.at(0,0)==0xffff0000 && image.at(1,0)==0xff00ff00 && image.at(2,0)==0xff0000ff,"555 color expansion/channel order");
    auto runs=words({0xfc00}); runs.push_back(std::byte{3});
    require(nolf::decode_lightmap555(runs,3)==std::vector<std::uint16_t>(3,0x7c00),"555 repeated color run");
    for (std::size_t i=0;i<raw.size();++i) rejects([&] { nolf::decode_lightmap555(std::span(raw).first(i),3); });
    runs[2]=std::byte{0}; rejects([&] { nolf::decode_lightmap555(runs,3); });
    runs[2]=std::byte{4}; rejects([&] { nolf::decode_lightmap555(runs,3); });
    rejects([&] { nolf::decode_lightmap555(raw,2); }); rejects([&] { nolf::decode_lightmap555(raw,0); }); rejects([&] { nolf::decode_lightmap555(raw,1025); });
    require(nolf::decode_shadow_map(Bytes{std::byte{0},std::byte{2},std::byte{2}},4)==std::vector<std::uint8_t>{255,255,0,0},"shadow map initial phase/alternating runs");
    require(nolf::decode_shadow_map(Bytes{std::byte{255},std::byte{0},std::byte{45}},300)==std::vector<std::uint8_t>(300,0),"shadow long run continuation");
    rejects([&] { nolf::decode_shadow_map(Bytes{std::byte{3}},2); }); rejects([&] { nolf::decode_shadow_map({},2); });
    rejects([&] { nolf::lightmap_image(3,2,pixels); }); rejects([&] { nolf::lightmap_image(0xffffffff,0xffffffff,pixels); });
    require(nolf::interpolate_lightmap555(0,0x7fff,.5f)==0x3def && nolf::interpolate_lightmap555(0x7fff,0,.5f)==0x3def,"555 interpolation signed rounding");
    require(nolf::interpolate_lightmap555(7,31,.01f)==7 && nolf::interpolate_lightmap555(7,31,.99f)==31,"555 frame snapping thresholds");
    require(nolf::add_lightmap555(std::uint16_t(31<<10|10<<5),std::uint16_t(31<<10|20<<5|31),.5f)==std::uint16_t(31<<10|20<<5|15),"555 layer saturation/scaling");
    rejects([&] { nolf::interpolate_lightmap555(0,0,-.1f); }); rejects([&] { nolf::add_lightmap555(0,0,std::numeric_limits<float>::quiet_NaN()); });
    rejects([&] { nolf::interpolate_lightmap555(0x8000,0,.5f); });
}
void polygon_views() {
    std::array<WorldPolyVertex,7> vertices{}; std::array<WorldLightmapReference,2> layers{{{2,7},{3,9}}};
    CWorldPart polygon{}; polygon.m_Verts=vertices.data(); polygon.m_nVerts=3;
    polygon.m_pVerts2=vertices.data()+3; polygon.m_nVerts2=4; polygon.m_pEdges=layers.data(); polygon.m_nEdges=2;
    require(polygon.GetVertexCount()==7 && polygon.render_vertices().size()==4 && polygon.render_vertices().data()==vertices.data()+3 &&
        polygon.render_vertices(false).size()==3 && polygon.lightmap_references()[1].animation==3 && polygon.lightmap_references()[1].polygon==9,"native render/fixed-run/lightmap reference views differ");
    LTPlane plane; plane.m_Normal.Init(0,1,0); plane.m_Dist=0; CWorldSurface surface; surface.m_pPlane=&plane; surface.m_nAxis=0;
    LTVector right,up; surface.GetBasisVectors(&right,&up);
    require(right.DistSqr({1,0,0})<.0001f && up.DistSqr({0,0,-1})<.0001f,"native lightmap basis orientation changed");
}
void corpus(const char* path) {
    nolf::Assets assets; assets.mount_game(path); unsigned worlds=0; std::uint64_t references=0,fixed=0,frames=0,shadow=0,texels=0,borders=0;
    std::map<float,unsigned> grids;
    for (const auto& name : assets.names()) if (name.ends_with(".dat")) {
        try {
            CServerWorld world; nolf::AssetStream stream(assets.locate(name)); WorldLoadRequest request; request.m_pStream=&stream;
            require(world.Load(&request)==0 && world.LoadObjects(&stream)==LT_OK,"original lightmap world load failed");
            const auto grid=world.Geometry()->lightmap_grid_size(); require(std::isfinite(grid) && grid>0,"invalid original lightmap grid"); ++grids[grid];
            for (uint32 model=0;model<world.m_Worlds.GetSize();++model) {
                const auto* bsp=world.m_Worlds[model]->m_pWorldBsp;
                for (uint32 index=0;index<bsp->m_nParts;++index) {
                    const auto& polygon=*bsp->m_ppParts[index]; const auto vertices=polygon.render_vertices();
                    require(vertices.size()>=3,"invalid native fixed vertex run");
                    if (vertices.data()!=polygon.m_Verts) { ++fixed; require(vertices[0].m_pPos==polygon.m_Verts[0].m_pPos,"fixed run starts at a different boundary vertex"); }
                    for (const auto& vertex : vertices) {
                        const auto delta=*vertex.m_pPos-polygon.m_pSurface->m_vO;
                        require(std::abs(vertex.m_fU-delta.Dot(polygon.m_pSurface->m_vP))<.01f && std::abs(vertex.m_fV-delta.Dot(polygon.m_pSurface->m_vQ))<.01f,"loaded native texture UV differs from surface basis");
                    }
                    for (const auto& reference : polygon.lightmap_references()) {
                        require(reference.animation<world.m_Anims.GetSize(),"lightmap animation reference out of range");
                        const auto& animation=world.m_Anims[reference.animation]; require(reference.polygon<animation.m_nLights,"lightmap polygon reference out of range");
                        const auto key=animation.m_pLights[reference.polygon]; require(key.m_n00==model && key.m_n02==index,"lightmap layer does not map back to its native polygon"); ++references;
                    }
                    if (!polygon.lightmap_references().empty()) {
                        LTVector right,up; polygon.m_pSurface->GetBasisVectors(&right,&up);
                        bool outside_tile=false;
                        for (const auto& vertex : vertices) {
                            const auto delta=*vertex.m_pPos-polygon.m_v34; const auto u=delta.Dot(right)/grid,v=delta.Dot(up)/grid;
                            require(std::isfinite(u) && std::isfinite(v) && u>=-.51f && v>=-.51f,"native lightmap basis does not fit authored tile origin");
                            outside_tile|=u>polygon.m_n48-1+.02f || v>polygon.m_n49-1+.02f;
                        }
                        borders+=outside_tile;
                    }
                }
            }
            for (const auto& animation : std::span(world.m_Anims.GetArray(),world.m_Anims.GetSize())) for (uint32 layer=0;layer<animation.m_nLights;++layer) {
                const auto key=animation.m_pLights[layer]; require(key.m_n00<world.m_Worlds.GetSize(),"lightmap world key out of range");
                const auto* bsp=world.m_Worlds[key.m_n00]->m_pWorldBsp; require(key.m_n02<bsp->m_nParts,"lightmap polygon key out of range");
                const auto& polygon=*bsp->m_ppParts[key.m_n02]; const auto count=std::size_t(polygon.m_n48)*polygon.m_n49;
                for (uint32 frame=0;frame<animation.m_nFrames;++frame) {
                    const auto& entry=animation.m_ppFrames[frame][layer]; const auto bytes=std::span(static_cast<const std::byte*>(entry.m_pData),entry.m_nSize);
                    if (animation.m_bShadowMap) { const auto values=nolf::decode_shadow_map(bytes,count); require(values.size()==count,"original shadow extent changed"); ++shadow; }
                    else { const auto values=nolf::decode_lightmap555(bytes,count); const auto image=nolf::lightmap_image(polygon.m_n48,polygon.m_n49,values); require(image.pixels().size()==count,"original lightmap image dimensions changed"); }
                    ++frames; texels+=count;
                }
            }
            ++worlds;
        } catch (const std::exception& error) { throw std::runtime_error(name+": "+error.what()); }
    }
    require(worlds && frames,"empty original lightmap corpus");
    for (const auto [grid,count] : grids) std::cout<<"Lightmap grid "<<grid<<": "<<count<<" worlds\n";
    std::cout<<"Decoded "<<worlds<<" worlds / "<<references<<" polygon-animation refs / "<<fixed<<" fixed vertex runs / "<<frames<<" lightmap frames ("<<shadow<<" shadow) / "<<texels<<" texels / "<<borders<<" polygons extending tile edges\n";
}
}
int main(int argc,char** argv) {
    try { decoders(); polygon_views(); if (argc==2) corpus(argv[1]); else if (argc!=1) throw std::invalid_argument("usage: nolf-lightmap-tests [game-data]"); std::cout<<"Native lightmap checks passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
