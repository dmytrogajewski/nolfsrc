#include "engine/client/gl_models.h"
#include "engine/server/model_loading.h"
#include "engine/server/model_cache.h"
#include "engine/portable/memory_stream.h"
#include "engine/portable/wire.h"
#include "platform/desktop.h"
#include "platform/system.h"
#include "model_fixture.h"
#include "rdev.h"
#include <future>
#include <iostream>

namespace {
void require(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
template<class F> void rejects(F&& function) { bool failed=false; try { function(); } catch (const std::exception&) { failed=true; } require(failed,"invalid model draw accepted"); }
void near(unsigned color,unsigned expected) {
    for (unsigned shift : {0u,8u,16u}) require(std::abs(int((color>>shift)&255)-int((expected>>shift)&255))<=2,"rasterized native model color differs");
}
struct Files {
    const std::filesystem::path root=std::filesystem::temp_directory_path()/("nolf-gl-models-"+std::to_string(nolf::platform::monotonic_nanoseconds()));
    std::shared_ptr<nolf::Assets> assets=std::make_shared<nolf::Assets>();
    Files() {
        std::vector<std::byte> bytes(168); nolf::wire::store(bytes.data()+4,std::int32_t(-5));
        for (unsigned offset : {8u,10u,12u}) nolf::wire::store(bytes.data()+offset,std::uint16_t(1));
        bytes[26]=std::byte{3}; nolf::wire::store(bytes.data()+164,std::uint32_t(0x00112233));
        nolf::platform::atomic_write(root/"color.dtx",bytes); assets->mount_directory(root);
    }
    ~Files() { std::error_code error; std::filesystem::remove_all(root,error); }
};
void checks(nolf::platform::Desktop& desktop) {
    auto model=std::make_shared<Model>(&g_DefAlloc,&g_DefAlloc); nolf::MemoryStream stream(nolf_test::tiny_model({},"start",true));
    require(model->Load(&stream,&g_DefAlloc)==LT_OK,"render fixture header failed");
    CModelLoadRequest request; request.m_pStream=&stream; request.m_bUnknown10=LTFALSE;
    require(model->LoadChildModels(&request)==LT_OK,"render fixture geometry failed");
    ModelInstance instance; instance.BindModel(model,false,false); instance.SetPos({-1,0,0});
    const auto pose=model_EvaluatePose(instance); require(bool(pose),"render pose evaluation failed");
    auto& mesh=*model->m_Pieces[0];
    Files files; nolf::ClientTextures textures(files.assets); nolf::GlTextures gpu; nolf::GlModels renderer(gpu);
    rd_Viewport(0,0,128,128); rd_MatrixMode(RD_PROJECTION); rd_LoadIdentity(); rd_Ortho(-2,2,-2,2,-100,100);
    rd_MatrixMode(RD_MODELVIEW); rd_LoadIdentity(); rd_Disable(RD_FOG); rd_Disable(RD_SCISSOR_TEST);
    auto clear=[] { rd_DepthMask(true); rd_ColorMask(true); rd_ClearColor(0,0,1,1); rd_Clear(RD_CLEAR_COLOR|RD_CLEAR_DEPTH); };
    auto pixel=[] { unsigned value=0; rd_ReadPixels(40,56,1,1,RD_PIX_BGRA,&value); return value&0xffffff; };
    auto draw=[&](const std::shared_ptr<const nolf::TextureResource>& skin,nolf::ModelDrawStyle style) {
        auto render=textures.begin_render(); require(renderer.draw(*pose,mesh,skin,render,style)==1,"native triangle not submitted"); render.finish();
    };
    nolf::ModelDrawStyle style; style.cull=false; style.color={255,0,0,255}; clear(); draw({},style); near(pixel(),0xff0000);
    style.cull=true; clear(); draw({},style); near(pixel(),0x0000ff);
    std::swap(mesh.m_Array10[0].indices[0],mesh.m_Array10[0].indices[1]); clear(); draw({},style); near(pixel(),0xff0000);
    std::swap(mesh.m_Array10[0].indices[0],mesh.m_Array10[0].indices[1]); style.cull=false;
    style.wireframe=true; clear(); draw({},style); near(pixel(),0x0000ff);
    std::array<unsigned,128*128> lines{}; rd_ReadPixels(0,0,128,128,RD_PIX_BGRA,lines.data());
    require(std::any_of(lines.begin(),lines.end(),[](unsigned pixel) { return (pixel&0xffffff)==0xff0000; }),"wireframe failed to rasterize edges"); style.wireframe=false;
    style.color[3]=128; style.blend=nolf::ModelBlend::Alpha; style.depth_write=false; clear(); draw({},style); near(pixel(),0x80007f);
    const auto skin=textures.load("color.dtx"); style={}; style.cull=false; style.sampling.alpha=false;
    clear(); draw(skin,style); near(pixel(),0x112233);
    style.sampling.alpha=true; style.alpha_test=true; clear(); draw(skin,style); near(pixel(),0x0000ff);
    HTEXTURE handle=nullptr; char filename[]="color.dtx"; require(textures.GetTextureHandle(filename,handle,0)==LT_OK,"model skin SDK handle failed");
    uint8* data=nullptr; long pitch=0; require(textures.LockTexture(handle,nullptr,TLOCK_TEXTURE,data,pitch)==LT_OK,"model skin SDK lock failed");
    *reinterpret_cast<std::uint32_t*>(data)=0xff00ff00; require(textures.UnlockTexture(handle)==LT_OK,"model skin SDK update failed");
    clear(); draw(skin,style); near(pixel(),0x00ff00);
    require(textures.WasTextureDrawnLastFrame(handle)==LT_YES,"model submission did not update texture visibility");
    require(textures.ReleaseTextureHandle(handle)==LT_OK,"model skin release failed"); clear(); draw(skin,style); near(pixel(),0x0000ff);
    std::array<std::array<std::uint8_t,4>,3> lighting{{{255,255,0,255},{255,255,0,255},{255,255,0,255}}};
    style.vertex_colors=lighting; style.alpha_test=false; clear(); draw({},style); near(pixel(),0xffff00);
    auto integer=reinterpret_cast<void (*)(unsigned,int*)>(desktop.gl_symbol("glGetIntegerv"));
    auto get_error=reinterpret_cast<unsigned (*)()>(desktop.gl_symbol("glGetError")); require(integer && get_error,"GL inspection unavailable");
    rd_Disable(RD_DEPTH_TEST); rd_Enable(RD_BLEND); rd_MatrixMode(RD_PROJECTION); draw({},style);
    int mode=0; integer(0x0ba0,&mode); require(mode==0x1701,"model draw failed to restore matrix mode");
    auto enabled=reinterpret_cast<unsigned char (*)(unsigned)>(desktop.gl_symbol("glIsEnabled")); require(enabled && !enabled(0x0b71) && enabled(0x0be2),"model draw failed to restore depth/blend state");
    {
        auto render=textures.begin_render();
        auto thread=std::async(std::launch::async,[&] { renderer.draw(*pose,mesh,skin,render,style); });
        bool wrong_thread=false; try { thread.get(); } catch (const std::logic_error&) { wrong_thread=true; } require(wrong_thread,"foreign thread drew a model");
    }
    { auto render=textures.begin_render(); style.vertex_colors=std::span(lighting).first(2); rejects([&] { renderer.draw(*pose,mesh,{},render,style); }); }
    require(!get_error(),"native model draw left GL error");
    const auto generation=rd_DeviceGeneration(); rd_DestroyDevice();
    { auto render=textures.begin_render(); style.vertex_colors={}; rejects([&] { renderer.draw(*pose,mesh,skin,render,style); }); }
    require(rd_CreateDevice(&desktop,128,128,true,0,1) && rd_DeviceGeneration()!=generation,"model device restart failed");
    rd_MatrixMode(RD_PROJECTION); rd_LoadIdentity(); rd_Ortho(-2,2,-2,2,-100,100); rd_MatrixMode(RD_MODELVIEW); rd_LoadIdentity();
    style.sampling.alpha=false; clear(); draw(skin,style); near(pixel(),0x112233);
    std::cout<<"Canonical model vertices rasterized with skin edits, alpha, lighting colors, state restoration and device restart\n";
}
void corpus(const char* path,nolf::platform::Desktop& desktop) {
    auto assets=std::make_shared<nolf::Assets>(); assets->mount_game(path); ModelCache models(assets);
    nolf::ClientTextures textures(assets); nolf::GlTextures gpu; nolf::GlModels renderer(gpu);
    std::shared_ptr<const nolf::TextureResource> skin;
    for (const auto& name : assets->names()) if (name.ends_with(".dtx")) { skin=textures.load(name); break; }
    require(bool(skin),"no original skin texture");
    auto get_error=reinterpret_cast<unsigned (*)()>(desktop.gl_symbol("glGetError")); require(get_error,"GL error check unavailable");
    auto render=textures.begin_render(); nolf::ModelDrawStyle style; style.cull=false; style.depth_test=false; style.depth_write=false; style.sampling.alpha=false;
    unsigned count=0; std::size_t lods=0,triangles=0;
    for (const auto& name : assets->names()) if (name.ends_with(".abc")) {
        auto model=models.Load(name); ModelInstance instance; instance.BindModel(model,false,false);
        auto pose=model_EvaluatePose(instance); require(bool(pose),"original GPU model pose failed");
        for (auto* piece : std::span(model->m_Pieces.GetArray(),model->m_Pieces.GetSize())) for (uint32 i=0;i<=piece->m_Array34.GetSize();++i) {
            const auto* mesh=piece->GetPieceData(i); const auto vertices=model_SkinVertices(*pose,*mesh);
            double extent=1;
            for (const auto& vertex : vertices) for (unsigned axis=0;axis<3;++axis) extent=std::max(extent,std::abs(double(vertex[axis]))+1);
            rd_MatrixMode(RD_PROJECTION); rd_LoadIdentity(); rd_Ortho(-extent,extent,-extent,extent,-extent,extent);
            rd_MatrixMode(RD_MODELVIEW); rd_LoadIdentity();
            triangles+=renderer.draw(*pose,*mesh,skin,render,style); ++lods;
            require(!get_error(),"original native model GPU draw left error");
        }
        ++count;
    }
    render.finish(); require(count && triangles,"empty original GPU model corpus");
    std::cout<<"Drew "<<count<<" original models, "<<lods<<" mesh LODs, "<<triangles<<" triangles through native GPU adapter\n";
}
}
int main(int argc,char** argv) {
    try {
        if (argc>2) throw std::invalid_argument("usage: nolf-gl-model-tests [game-data]");
        nolf::platform::DesktopConfig config; config.width=128; config.height=128; config.vsync=false;
        nolf::platform::Desktop desktop(config); require(rd_CreateDevice(&desktop,128,128,true,0,1),"model GPU device failed");
        struct Device { ~Device() { rd_DestroyDevice(); } } device;
        std::cout<<"desktop="<<desktop.driver()<<" renderer="<<rd_Describe()<<'\n'; checks(desktop); if (argc==2) corpus(argv[1],desktop); return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
