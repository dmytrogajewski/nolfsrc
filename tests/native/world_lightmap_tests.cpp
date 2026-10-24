#include "world_lightmap_fixture.h"
#include "engine/portable/binary.h"
#include "engine/portable/memory_stream.h"
#include "engine/portable/asset_stream.h"
#include "engine/portable/assets.h"
#include "engine/server/world_state.h"
#include <future>
#include <cmath>
#include <iostream>
#include <limits>

namespace {
void require(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
template<class F> void rejects(F&& function) { bool failed=false; try { function(); } catch (const std::exception&) { failed=true; } require(failed,"invalid world lightmap operation accepted"); }
void atlas() {
    auto source=std::make_shared<CWorldData>(); nolf_test::load_lightmap_fixture(*source,nolf_test::lightmap_fixture());
    nolf::WorldLightmaps atlas(source,nolf_test::lightmap_geometry(),8,16384);
    require(atlas.pages()==1 && atlas.revision(0)==1 && !atlas.update() && !atlas.find(42),"initial atlas packing or stable update failed");
    const auto first=*atlas.find(7),second=*atlas.find(9);
    require(first.x==1 && first.y==1 && second.x==5 && second.y==1 && first.width==2 && first.height==1,"lightmap shelf layout or border spacing");
    auto pixel=[&](const nolf::LightmapLocation& location,unsigned x=0) { return atlas.image(location.page).at(location.x+x,location.y); };
    require(pixel(first)==0xff0000ff && atlas.image(0).at(0,0)==0xff0000ff && atlas.image(0).at(3,2)==0xff0000ff,"base lightmap or repeated atlas borders wrong");
    const auto uv=atlas.coordinates(7,{100000,-100000,0}); require(std::abs(uv[0]-2.5f/8)<.00001f && std::abs(uv[1]-1.5f/8)<.00001f,"lightmap coordinates leaked into a neighboring tile");
    auto& blink=source->m_Anims[1]; blink.m_iFrames[0]=0; blink.m_iFrames[1]=1; blink.m_fPercentBetween=.5f; blink.m_fBlendPercent=.5f;
    require(atlas.update()==2 && atlas.revision(0)==2 && pixel(first)==0xff3939ff && pixel(second)==0xff3939ff,"animated layer interpolation/addition wrong");
    blink.m_fBlendPercent=0; require(atlas.update()==2 && pixel(first)==0xff0000ff,"disabled layer was not removed");
    auto& shadow=source->m_Anims[2]; shadow.m_iFrames[0]=shadow.m_iFrames[1]=0; shadow.m_vLightColor.Init(255,127,127); shadow.m_fLightRadius=1;
    require(atlas.update()==2 && pixel(first)==0xffff00ff && pixel(first,1)==0xff0000ff,"shadow mask, radius, color or texel world position wrong");
    auto thread=std::async(std::launch::async,[&] { atlas.update(); }); bool wrong=false;
    try { thread.get(); } catch (const std::logic_error&) { wrong=true; } require(wrong,"another thread updated world lightmaps");
    const auto revision=atlas.revision(0);
    const auto saved=pixel(first);
    nolf::MemoryStream bad({std::byte{0}}); require(source->LoadObjects(&bad)!=LT_OK && !atlas.update() && atlas.revision(0)==revision,"failed source reload invalidated live atlas");
    nolf_test::load_lightmap_fixture(*source,nolf_test::lightmap_fixture()); nolf_test::load_lightmap_fixture(*source,nolf_test::lightmap_fixture()); rejects([&] { atlas.update(); });
    require(pixel(first)==saved,"source replacement invalidated cached pixel snapshot");
    rejects([&] { atlas.coordinates(42,{0,0,0}); }); rejects([&] { atlas.coordinates(7,{std::numeric_limits<float>::quiet_NaN(),0,0}); });
    rejects([&] { nolf::WorldLightmaps limited(source,nolf_test::lightmap_geometry(),8,1); });
    auto duplicate=nolf_test::lightmap_geometry(); duplicate[1].key=7; rejects([&] { nolf::WorldLightmaps invalid(source,duplicate,8,16384); });
    auto invalid=nolf_test::lightmap_geometry(); invalid[0].layers[0].animation=3; rejects([&] { nolf::WorldLightmaps invalid_atlas(source,invalid,8,16384); });
    auto broken=std::make_shared<CWorldData>(); nolf_test::load_lightmap_fixture(*broken,nolf_test::lightmap_fixture(true)); auto partial=nolf_test::lightmap_geometry(); partial[0].layers={{0,0}}; partial[1].layers={{0,0},{1,0}};
    nolf::WorldLightmaps transaction(broken,std::move(partial),8,16384); const auto place=*transaction.find(7);
    const auto before=transaction.image(0).at(place.x,place.y); const auto old_revision=transaction.revision(0);
    broken->m_Anims[0].m_iFrames[0]=broken->m_Anims[0].m_iFrames[1]=0xffffffff;
    broken->m_Anims[1].m_iFrames[0]=broken->m_Anims[1].m_iFrames[1]=0;
    rejects([&] { transaction.update(); });
    require(transaction.image(0).at(place.x,place.y)==before && transaction.revision(0)==old_revision,"late decode failure partially published earlier tiles");
    source->TermArrays(); rejects([&] { atlas.update(); }); require(pixel(first)==saved,"terminating lighting storage destroyed atlas snapshot");
}
void dynamic() {
    auto source=std::make_shared<CWorldData>(); nolf_test::load_lightmap_fixture(*source,nolf_test::lightmap_fixture());
    nolf::WorldLightmaps atlas(source,nolf_test::lightmap_geometry(),8,16384);
    const auto first=*atlas.find(7);
    auto pixel=[&](unsigned x=0) { return atlas.image(first.page).at(first.x+x,first.y); };
    using Light=nolf::DynamicWorldLight;
    // A white light just in front of both tiles adds nearly its full colour.
    const auto white=Light::from({0,0,.5f},10,{255,255,255},false,false);
    require(atlas.update(std::span(&white,1),1)==2 && pixel()==0xffffffff && pixel(1)==0xffffffff,"dynamic light did not add to the lightmap");
    // The next frame without it gives the tiles their own lightmaps back.
    require(atlas.update()==2 && pixel()==0xff0000ff,"dynamic light outlived its frame");
    const auto behind=Light::from({0,0,-.5f},10,{255,255,255},false,true);
    require(!atlas.update(std::span(&behind,1),1) && pixel()==0xff0000ff,"front-only light lit a back face");
    const auto solid=Light::from({0,0,.5f},10,{0,255,0},true,false);
    require(atlas.update(std::span(&solid,1),1)==2 && pixel()==0xff00ff00,"solid light did not set its colour");
    const auto far=Light::from({100,0,.5f},10,{255,255,255},false,false);
    require(atlas.update(std::span(&far,1),1)==2 && pixel()==0xff0000ff,"out of reach light changed the lightmap");
    // A world model moved 100 along X takes the same light into its BSP's
    // space, where its texels are, and is lit by it.
    nolf::LightmapPlacement moved{0,{}}; moved.to_local.Identity(); moved.to_local.m[0][3]=-100;
    require(atlas.update(std::span(&far,1),1,std::span(&moved,1))==2 && pixel()==0xffffffff,"a moved world model's lightmap missed the light where it is");
    rejects([&] { atlas.update({},std::numeric_limits<float>::quiet_NaN()); });
}
void corpus(const char* path) {
    auto assets=std::make_shared<nolf::Assets>(); assets->mount_game(path); unsigned worlds=0; std::uint64_t pages=0,tiles=0,updated=0;
    for (const auto& name : assets->names()) if (name.ends_with(".dat")) {
        try {
            auto world=std::make_shared<CServerWorld>(); nolf::AssetStream stream(assets->locate(name)); WorldLoadRequest request; request.m_pStream=&stream;
            require(world->Load(&request)==0 && world->LoadObjects(&stream)==LT_OK,"original lightmap atlas world load failed");
            auto atlas=nolf::world_lightmaps(world); require(!atlas->update(),"unchanged original lightmap source rebuilt tiles");
            for (std::size_t page=0;page<atlas->pages();++page) require(atlas->revision(page)>0 && atlas->image(page).width()==512,"original atlas dimensions/revision wrong");
            for (const auto& animation : std::span(world->m_Anims.GetArray(),world->m_Anims.GetSize())) for (const auto& key : std::span(animation.m_pLights,animation.m_nLights)) {
                const uint32 id=uint32(key.m_n00)<<16|key.m_n02; const auto location=atlas->find(id); if (!location) continue;
                const auto& polygon=*world->m_Worlds[key.m_n00]->m_pWorldBsp->m_ppParts[key.m_n02];
                for (const auto& vertex : polygon.render_vertices()) {
                    const auto uv=atlas->coordinates(id,*vertex.m_pPos);
                    require(std::isfinite(uv[0]) && std::isfinite(uv[1]) && uv[0]>0 && uv[0]<1 && uv[1]>0 && uv[1]<1,"original lightmap UV outside atlas");
                }
                ++tiles;
            }
            for (auto& animation : std::span(world->m_Anims.GetArray(),world->m_Anims.GetSize())) if (animation.m_nFrames) {
                animation.m_iFrames[0]=0; animation.m_iFrames[1]=animation.m_nFrames-1; animation.m_fPercentBetween=.5f; animation.m_fBlendPercent=.75f;
                if (animation.m_bShadowMap) { animation.m_vLightPos=(world->m_vMin+world->m_vMax)*.5f; animation.m_vLightColor.Init(255,192,128); animation.m_fLightRadius=1024; }
            }
            updated+=atlas->update(); require(!atlas->update(),"original atlas repeated a completed animated update");
            pages+=atlas->pages(); ++worlds;
            std::weak_ptr<CServerWorld> weak=world; world.reset(); require(!weak.expired(),"lightmap cache did not retain actual world data");
            atlas.reset(); require(weak.expired(),"last lightmap release leaked its world");
        } catch (const std::exception& error) { throw std::runtime_error(name+": "+error.what()); }
    }
    require(worlds && pages && tiles,"empty original lightmap atlas corpus");
    std::cout<<"Composed "<<worlds<<" original worlds / "<<pages<<" atlas pages / "<<tiles<<" tile references / "<<updated<<" animated tile updates\n";
}
}
int main(int argc,char** argv) {
    try { atlas(); dynamic(); if (argc==2) corpus(argv[1]); else if (argc!=1) throw std::invalid_argument("usage: nolf-world-lightmap-tests [game-data]"); std::cout<<"Native world lightmap atlas checks passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
