#include "engine/client/gl_world.h"
#include "engine/portable/asset_stream.h"
#include "engine/server/world_state.h"
#include "engine/server/object_properties.h"
#include "engine/server/ltobject_internal.h"
#include "platform/desktop.h"
#include "platform/system.h"
#include "world_render_fixture.h"
#include "rdev.h"
#include <future>
#include <iostream>
#include <numbers>

namespace {
void require(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
template<class F> void rejects(F&& function) { bool failed=false; try { function(); } catch (const std::exception&) { failed=true; } require(failed,"invalid native world render accepted"); }
void near(uint32 color,uint32 expected) { for (unsigned shift : {0u,8u,16u}) require(std::abs(int(color>>shift&255)-int(expected>>shift&255))<=2,"native BSP surface/lightmap pixel differs"); }
struct Files {
    const std::filesystem::path root=std::filesystem::temp_directory_path()/("nolf-gl-world-"+std::to_string(nolf::platform::monotonic_nanoseconds()));
    std::shared_ptr<nolf::Assets> assets=std::make_shared<nolf::Assets>();
    Files() {
        std::vector<std::byte> bytes(168); nolf::wire::store(bytes.data()+4,int32(-5));
        for (unsigned offset : {8u,10u,12u}) nolf::wire::store(bytes.data()+offset,uint16(1));
        bytes[26]=std::byte{3}; nolf::wire::store(bytes.data()+164,uint32(0x00804020));
        nolf::platform::atomic_write(root/"color.dtx",bytes); assets->mount_directory(root);
    }
    ~Files() { std::error_code error; std::filesystem::remove_all(root,error); }
};
void checks(nolf::platform::Desktop& desktop) {
    Files files; auto textures=std::make_shared<nolf::ClientTextures>(files.assets); nolf::GlTextures gpu;
    auto world=std::make_shared<CServerWorld>(); nolf::MemoryStream stream(nolf_test::render_world_fixture()); WorldLoadRequest request; request.m_pStream=&stream;
    require(!world->Load(&request) && world->LoadObjects(&stream)==LT_OK,"render world fixture failed to load");
    nolf::GlWorld renderer(world,textures,files.assets,gpu); LTRotation rotation; rotation.Init();
    nolf::CameraView camera({0,0,0},rotation,{0,0,128,128},std::numbers::pi_v<float>/2,std::numbers::pi_v<float>/2);
    auto clear=[] { rd_Disable(RD_SCISSOR_TEST); rd_ColorMask(true); rd_DepthMask(true); rd_ClearColor(0,1,0,1); rd_Clear(RD_CLEAR_COLOR|RD_CLEAR_DEPTH); };
    auto pixel=[] { uint32 value=0; rd_ReadPixels(64,64,1,1,RD_PIX_BGRA,&value); return value; };
    auto draw=[&](nolf::WorldRenderOptions options={}) { auto render=textures->begin_render(); auto result=renderer.render_static(camera,render,options); render.finish(); return result; };
    clear(); auto stats=draw(); require(stats.regions==1 && stats.candidates==1 && stats.polygons==1 && stats.triangles==2 && stats.lightmapped==1,"native PVS polygon/lightmap submission failed"); near(pixel(),0x000020);
    {
        CObjWorldModel brush; brush.m_pWorldBsp=world->m_Worlds[0]->m_pWorldBsp;
        auto draw_brush=[&] { auto render=textures->begin_render(); auto stats=renderer.render_brush(brush,camera,render); render.finish(); return stats; };
        clear(); require(draw_brush().lightmapped==1,"brush lost source lightmap mapping"); near(pixel(),0x000020);
        {
            auto render=textures->begin_render(); renderer.render_static(camera,render);
            world->m_Anims[1].m_iFrames[0]=world->m_Anims[1].m_iFrames[1]=0;
            clear(); renderer.render_brush(brush,camera,render); near(pixel(),0x000020); render.finish();
        }
        clear(); draw_brush(); near(pixel(),0x800020);
        world->m_Anims[1].m_iFrames[0]=world->m_Anims[1].m_iFrames[1]=0xffffffff;
        clear(); draw_brush(); near(pixel(),0x000020);
        {
            auto render=textures->begin_render(); renderer.render_static(camera,render);
            auto& surface=*brush.m_pWorldBsp->PolygonReferences()[0].m_pOwner->m_pSurface;
            surface.m_nFlags|=16; renderer.render_sky_brush(brush,camera,render); near(pixel(),0x000020); surface.m_nFlags&=~16u;
            render.finish();
        }
        brush.m_ColorB=128; clear(); draw_brush(); near(pixel(),0x000010); brush.m_ColorB=255;
        brush.m_Transform1f0.m[0][3]=1000; brush.m_Transform230.m[0][3]=-1000;
        clear(); require(draw_brush().culled==1,"translated brush used untransformed bounds"); near(pixel(),0x00ff00);
        LTVector pivot{0,0,10},position{0,0,10}; LTRotation turn(0,1,0,0);
        wm_BuildTransforms(&pivot,&position,&turn,&brush.m_Transform1f0,&brush.m_Transform230);
        clear(); require(draw_brush().culled==1,"rotated solid brush used world-space camera for local facing"); near(pixel(),0x00ff00);
        brush.m_ColorA=128; clear(); stats=draw_brush(); require(stats.polygons==1 && !stats.lightmapped,"translucent brush failed two-sided BSP pass"); near(pixel(),0x20a004);
        brush.m_ColorA=255; brush.m_Transform1f0.Identity(); brush.m_Transform230.Identity(); brush.m_Flags2=FLAG2_CHROMAKEY;
        clear(); draw_brush(); near(pixel(),0x00ff00); brush.m_Flags2=0;
        CWorldBsp foreign; brush.m_pWorldBsp=&foreign;
        { auto render=textures->begin_render(); rejects([&] { renderer.render_brush(brush,camera,render); }); }
        brush.m_pWorldBsp=nullptr;
    }
    nolf::WorldRenderOptions options; options.lightmaps=false; clear(); stats=draw(options); require(!stats.lightmapped,"disabled lightmaps still submitted"); near(pixel(),0x404008);
    options.lightmaps=true; options.saturate=true; clear(); draw(options); near(pixel(),0x000040);
    options.saturate=false; options.light_scale.Init(.5,1,1); clear(); draw(options); near(pixel(),0x000020);
    world->m_Anims[1].m_iFrames[0]=world->m_Anims[1].m_iFrames[1]=0; clear(); draw(); near(pixel(),0x800020);
    auto& bsp=*world->m_Worlds[0]->m_pWorldBsp; auto& surface=bsp.m_pTree[0];
    surface.m_nFlags|=4; clear(); require(!draw().polygons,"invisible world surface drew"); near(pixel(),0x00ff00); surface.m_nFlags&=~4u;
    surface.m_nFlags|=16; clear(); require(draw().sky==1,"sky aperture not identified"); near(pixel(),0x00ff00); surface.m_nFlags&=~16u;
    surface.m_pPlane->m_Normal.z=1; surface.m_pPlane->m_Dist=10;
    clear(); require(draw().culled==1,"back-facing BSP surface drew"); near(pixel(),0x00ff00);
    surface.m_pPlane->m_Normal.z=-1; surface.m_pPlane->m_Dist=-10;
    *static_cast<uint8*>(bsp.m_p80[0].m_pData)=0; bsp.m_Visibility=WorldVisibility::Build(bsp);
    clear(); require(!draw().polygons,"hidden PVS group drew"); near(pixel(),0x00ff00);
    options={}; options.draw_all=true; clear(); require(draw(options).polygons==1,"draw-all did not bypass PVS"); near(pixel(),0x800020);
    *static_cast<uint8*>(bsp.m_p80[0].m_pData)=1; bsp.m_Visibility=WorldVisibility::Build(bsp);
    auto get_error=reinterpret_cast<unsigned (*)()>(desktop.gl_symbol("glGetError"));
    auto integer=reinterpret_cast<void (*)(unsigned,int*)>(desktop.gl_symbol("glGetIntegerv")); require(get_error && integer,"world GL inspection unavailable");
    rd_MatrixMode(RD_PROJECTION); draw(); int mode=0; integer(0x0ba0,&mode); require(mode==0x1701 && !get_error(),"world pass did not restore GL state");
    { auto render=textures->begin_render(); auto thread=std::async(std::launch::async,[&] { renderer.render_static(camera,render); }); bool wrong=false;
      try { thread.get(); } catch (const std::logic_error&) { wrong=true; } require(wrong,"another thread rendered native BSP"); }
    const auto generation=rd_DeviceGeneration(); rd_DestroyDevice(); { auto render=textures->begin_render(); rejects([&] { renderer.render_static(camera,render); }); }
    require(rd_CreateDevice(&desktop,128,128,true,0,1) && rd_DeviceGeneration()!=generation,"world device restart failed"); clear(); draw(); near(pixel(),0x800020);
    world->Term(); { auto render=textures->begin_render(); rejects([&] { renderer.render_static(camera,render); }); }
    std::cout<<"Native BSP pixels, PVS exclusion, vertex lighting, animated lightmaps, sky apertures and context restart passed\n";
}
void corpus(const char* directory,const char* snapshot,nolf::platform::Desktop& desktop) {
    auto assets=std::make_shared<nolf::Assets>(); assets->mount_game(directory);
    auto textures=std::make_shared<nolf::ClientTextures>(assets); nolf::GlTextures gpu;
    unsigned worlds=0; std::size_t polygons=0,triangles=0,lightmapped=0,candidates=0,missing=0,regions=0;
    auto error=reinterpret_cast<unsigned (*)()>(desktop.gl_symbol("glGetError")); require(error,"world GL inspection unavailable");
    for (const auto& name : assets->names()) if (name.ends_with(".dat")) {
        try {
            auto world=std::make_shared<CServerWorld>(); nolf::AssetStream stream(assets->locate(name)); WorldLoadRequest request; request.m_pStream=&stream;
            require(!world->Load(&request) && world->LoadObjects(&stream)==LT_OK,"original render world load failed");
            nolf::GlWorld renderer(world,textures,assets,gpu); LTVector position=(world->m_vMin+world->m_vMax)*.5f;
            for (const auto& object : world->ObjectDefinitions()) if (object.class_name=="GameStartPoint") {
                ObjectProperties properties(object.properties); if (const auto* pos=properties.find("Pos")) position=pos->value.m_Vec; break;
            }
            for (unsigned direction=0;direction<4;++direction) {
                const float angle=direction*std::numbers::pi_v<float>/2; LTRotation rotation(0,std::sin(angle/2),0,std::cos(angle/2));
                nolf::CameraView camera(position,rotation,{0,0,128,128},std::numbers::pi_v<float>/2,std::numbers::pi_v<float>/2);
                rd_Disable(RD_SCISSOR_TEST); rd_ColorMask(true); rd_DepthMask(true); rd_ClearColor(0,0,0,1); rd_Clear(RD_CLEAR_COLOR|RD_CLEAR_DEPTH);
                auto render=textures->begin_render(); auto stats=renderer.render_static(camera,render); render.finish();
                polygons+=stats.polygons; triangles+=stats.triangles; lightmapped+=stats.lightmapped; candidates+=stats.candidates; missing+=stats.missing_materials; regions+=stats.regions;
                require(!error(),"original native BSP draw left GL error");
                if (snapshot && name=="worlds/m01s01.dat" && direction==0) {
                    nolf::Image image(128,128); rd_ReadPixels(0,0,128,128,RD_PIX_BGRA,image.pixels().data()); nolf::platform::atomic_write(snapshot,nolf::encode_bmp(image));
                }
            }
            for (const auto& material : renderer.missing_materials()) std::cout<<name<<": missing material "<<material<<'\n';
            ++worlds;
        } catch (const std::exception& error) { throw std::runtime_error(name+": "+error.what()); }
    }
    require(worlds && polygons && lightmapped,"empty original native BSP render corpus");
    std::cout<<"Rendered "<<worlds<<" original worlds / "<<polygons<<" polygons / "<<triangles<<" triangles / "<<lightmapped<<" lightmapped / "<<candidates<<" PVS candidates / "<<regions<<" regions / "<<missing<<" missing-material submissions\n";
}
void brush_corpus(const char* directory,nolf::platform::Desktop& desktop) {
    auto assets=std::make_shared<nolf::Assets>(); assets->mount_game(directory); auto textures=std::make_shared<nolf::ClientTextures>(assets); nolf::GlTextures gpu;
    std::size_t worlds=0,models=0,terrains=0,polygons=0,triangles=0,lightmapped=0,deduplicated=0,sky_polygons=0;
    const auto error=reinterpret_cast<unsigned (*)()>(desktop.gl_symbol("glGetError")); require(error,"brush GL inspection unavailable");
    for (const auto& name : assets->names()) if (name.ends_with(".dat")) {
        try {
            auto world=std::make_shared<CServerWorld>(); nolf::AssetStream stream(assets->locate(name)); WorldLoadRequest request; request.m_pStream=&stream;
            require(!world->Load(&request) && world->LoadObjects(&stream)==LT_OK,"original brush world load failed"); nolf::GlWorld renderer(world,textures,assets,gpu);
            auto render=textures->begin_render();
            for (auto* record : std::span(world->m_Worlds.GetArray(),world->m_Worlds.GetSize())) {
                auto draw=[&](IWorldBsp& source,bool terrain) {
                    CObjWorldModel brush; brush.m_pWorldBsp=&source; brush.m_Flags=FLAG_VISIBLE;
                    if (!terrain) {
                        const auto pivot=record->m_pWorldBsp->m_ve8; LTVector origin=pivot,position=pivot+LTVector(100,-50,25); LTRotation rotation(0,std::sin(.2f),0,std::cos(.2f));
                        wm_BuildTransforms(&origin,&position,&rotation,&brush.m_Transform1f0,&brush.m_Transform230);
                    }
                    const auto half=(source.m_vBoxMax-source.m_vBoxMin)*.5f,mid=(source.m_vBoxMax+source.m_vBoxMin)*.5f;
                    LTVector center{0,0,0}; for (unsigned row=0;row<3;++row) { center[row]=brush.m_Transform1f0.m[row][3]; for (unsigned column=0;column<3;++column) center[row]+=brush.m_Transform1f0.m[row][column]*mid[column]; }
                    const float distance=std::max(30.,std::hypot(double(half.x),double(half.y),double(half.z))*3+30);
                    // Every polygon is submitted with culling disabled. Keep
                    // this bulk geometry check small to bound software overdraw;
                    // the pixel fixtures above use the full 128x128 viewport.
                    LTRotation rotation(0,1,0,0); nolf::CameraView camera(center+LTVector(0,0,distance),rotation,{0,0,16,16},std::numbers::pi_v<float>/2,std::numbers::pi_v<float>/2,7,std::max(10000.f,distance*5+100));
                    nolf::WorldRenderOptions options; options.draw_all=true; options.cull=false;
                    const auto stats=renderer.render_brush(brush,camera,render,options);
                    polygons+=stats.polygons; triangles+=stats.triangles; lightmapped+=stats.lightmapped;
                    sky_polygons+=renderer.render_sky_brush(brush,camera,render,options).polygons;
                    if (terrain && !deduplicated) {
                        renderer.render_static(camera,render,options);
                        const auto after=renderer.render_brush(brush,camera,render,options,true);
                        require(after.polygons<=stats.polygons,"static terrain deduplication added geometry"); deduplicated+=stats.polygons-after.polygons;
                    }
                    require(!error(),"original transformed brush draw left GL error"); if (terrain) ++terrains; else ++models;
                };
                draw(*record->m_pWorldBsp,false);
                for (auto& terrain : std::span(record->m_pWorldBsp->m_Array138.GetArray(),record->m_pWorldBsp->m_Array138.GetSize())) draw(terrain,true);
            }
            render.finish(); ++worlds;
        } catch (const std::exception& error) { throw std::runtime_error(name+": "+error.what()); }
    }
    require(worlds && models && terrains && polygons && lightmapped && deduplicated,"original brush corpus empty or static terrain duplication unresolved");
    std::cout<<"Rendered "<<worlds<<" original worlds / "<<models<<" transformed BSP models / "<<terrains<<" terrain subsets / "<<polygons<<" polygons / "<<triangles<<" triangles / "<<lightmapped<<" lightmapped / "<<deduplicated<<" static terrain duplicates excluded / "<<sky_polygons<<" sky-pass polygons\n";
}
}
int main(int argc,char** argv) {
    try {
        if (argc>3) throw std::invalid_argument("usage: nolf-gl-world-tests [game-data [snapshot.bmp]] or --brushes game-data");
        nolf::platform::DesktopConfig config; config.width=config.height=128; config.vsync=false; nolf::platform::Desktop desktop(config);
        require(rd_CreateDevice(&desktop,128,128,true,0,1),"world GPU device failed"); struct Device { ~Device() { rd_DestroyDevice(); } } device;
        std::cout<<"desktop="<<desktop.driver()<<" renderer="<<rd_Describe()<<'\n'; checks(desktop);
        if (argc==3 && std::string_view(argv[1])=="--brushes") brush_corpus(argv[2],desktop);
        else if (argc>=2) corpus(argv[1],argc==3 ? argv[2] : nullptr,desktop);
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
