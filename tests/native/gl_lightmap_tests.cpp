#include "engine/client/gl_lightmaps.h"
#include "engine/portable/asset_stream.h"
#include "engine/portable/assets.h"
#include "engine/server/world_state.h"
#include "platform/desktop.h"
#include "world_lightmap_fixture.h"
#include "rdev.h"
#include <future>
#include <iostream>

namespace {
void require(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
template<class F> void rejects(F&& function) { bool failed=false; try { function(); } catch (const std::exception&) { failed=true; } require(failed,"invalid GPU lightmap operation accepted"); }
void setup(unsigned side) {
    rd_Viewport(0,0,side,side); rd_Disable(RD_SCISSOR_TEST); rd_Disable(RD_BLEND); rd_Disable(RD_ALPHA_TEST); rd_Disable(RD_DEPTH_TEST); rd_Disable(RD_FOG); rd_CullBack(false);
    for (int unit=1;unit<g_RDCaps.nTextureUnits;++unit) { rd_ActiveTexture(unit); rd_Disable(RD_TEXTURE_2D); }
    rd_ActiveTexture(0); rd_Enable(RD_TEXTURE_2D); rd_TexEnv(RD_ENV_REPLACE);
    rd_MatrixMode(RD_TEXTURE); rd_LoadIdentity(); rd_MatrixMode(RD_PROJECTION); rd_LoadIdentity(); rd_Ortho(0,side,side,0,-1,1); rd_MatrixMode(RD_MODELVIEW); rd_LoadIdentity();
}
void compare(nolf::GlLightmaps& gpu,const nolf::WorldLightmaps& atlas,std::size_t page) {
    const auto& image=atlas.image(page); const auto side=image.width(),block=std::min(side,256u); setup(block); gpu.bind(page,false);
    // The compositor may constrain the drawable to 640x480. Read full-size
    // atlas texels in smaller squares rather than requesting a taller window.
    std::vector<uint32> pixels(std::size_t(block)*block);
    for (unsigned y=0;y<side;y+=block) for (unsigned x=0;x<side;x+=block) {
        const float left=float(x)/side,top=float(y)/side,right=float(x+block)/side,bottom=float(y+block)/side;
        rd_Begin(RD_QUADS);
        rd_TexCoord2f(left,top); rd_Vertex2f(0,0); rd_TexCoord2f(right,top); rd_Vertex2f(block,0);
        rd_TexCoord2f(right,bottom); rd_Vertex2f(block,block); rd_TexCoord2f(left,bottom); rd_Vertex2f(0,block); rd_End();
        rd_ReadPixels(0,0,block,block,RD_PIX_BGRA,pixels.data());
        for (unsigned row=0;row<block;++row)
            require(std::equal(pixels.begin()+row*block,pixels.begin()+(row+1)*block,image.pixels().begin()+(y+row)*side+x),"GPU lightmap readback differs from CPU atlas");
    }
}
void checks(nolf::platform::Desktop& desktop) {
    auto source=std::make_shared<CWorldData>(); nolf_test::load_lightmap_fixture(*source,nolf_test::lightmap_fixture());
    auto atlas=std::make_shared<nolf::WorldLightmaps>(source,nolf_test::lightmap_geometry(),4,16384);
    require(atlas->pages()==2,"two-page residency fixture failed");
    std::weak_ptr<const nolf::WorldLightmaps> weak=atlas;
    nolf::GlLightmaps gpu(atlas,64); compare(gpu,*atlas,0); compare(gpu,*atlas,1); compare(gpu,*atlas,0);
    require(gpu.bytes()==64 && gpu.size()==1,"GPU atlas page eviction exceeded residency budget");
    source->m_Anims[1].m_iFrames[0]=source->m_Anims[1].m_iFrames[1]=0; atlas->update(); compare(gpu,*atlas,0); compare(gpu,*atlas,1);
    rejects([&] { gpu.bind(2); });
    nolf::GlLightmaps limited(atlas,63); rejects([&] { limited.bind(0); }); require(!limited.bytes() && !limited.size(),"failed page budget allocated a GPU texture");
    auto thread=std::async(std::launch::async,[&] { gpu.bind(0); }); bool wrong=false;
    try { thread.get(); } catch (const std::logic_error&) { wrong=true; } require(wrong,"another thread bound a lightmap");
    auto error=reinterpret_cast<unsigned (*)()>(desktop.gl_symbol("glGetError")); require(error && !error(),"lightmap adapter left GL error");
    const auto generation=rd_DeviceGeneration(); rd_DestroyDevice(); rejects([&] { gpu.bind(0); });
    require(rd_CreateDevice(&desktop,640,480,true,0,1) && rd_DeviceGeneration()!=generation,"lightmap device restart failed");
    compare(gpu,*atlas,0); compare(gpu,*atlas,1); require(gpu.bytes()==64 && gpu.size()==1 && !error(),"lightmap restart retained stale GPU residency");
    atlas.reset(); require(!weak.expired(),"GPU atlas lost its CPU snapshot owner"); gpu.clear(); require(!gpu.size() && !gpu.bytes(),"GPU atlas explicit clear failed");
    std::cout<<"Native GPU lightmap pixels, animated revision uploads, bounded eviction and context restart passed\n";
}
void corpus(const char* directory,nolf::platform::Desktop& desktop) {
    auto assets=std::make_shared<nolf::Assets>(); assets->mount_game(directory); unsigned worlds=0; std::size_t pages=0;
    auto error=reinterpret_cast<unsigned (*)()>(desktop.gl_symbol("glGetError")); require(error,"GL error inspection unavailable");
    for (const auto& name : assets->names()) if (name.ends_with(".dat")) {
        try {
            auto world=std::make_shared<CServerWorld>(); nolf::AssetStream stream(assets->locate(name)); WorldLoadRequest request; request.m_pStream=&stream;
            require(!world->Load(&request) && world->LoadObjects(&stream)==LT_OK,"original GPU lightmap world load failed");
            std::shared_ptr<nolf::WorldLightmaps> atlas=nolf::world_lightmaps(world); nolf::GlLightmaps gpu(atlas,512*512*4);
            for (std::size_t page=0;page<atlas->pages();++page) { compare(gpu,*atlas,page); ++pages; }
            require(!error(),"original GPU lightmap draw left GL error"); ++worlds;
        } catch (const std::exception& error) { throw std::runtime_error(name+": "+error.what()); }
    }
    require(worlds && pages,"empty original GPU atlas corpus");
    std::cout<<"Compared exact GPU pixels for "<<worlds<<" original worlds / "<<pages<<" lightmap atlas pages\n";
}
}
int main(int argc,char** argv) {
    try {
        if (argc>2) throw std::invalid_argument("usage: nolf-gl-lightmap-tests [game-data]");
        nolf::platform::DesktopConfig config; config.width=640; config.height=480; config.vsync=false; nolf::platform::Desktop desktop(config);
        require(rd_CreateDevice(&desktop,640,480,true,0,1),"lightmap GPU device failed"); struct Device { ~Device() { rd_DestroyDevice(); } } device;
        std::cout<<"desktop="<<desktop.driver()<<" renderer="<<rd_Describe()<<'\n'; checks(desktop); if (argc==2) corpus(argv[1],desktop); return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
