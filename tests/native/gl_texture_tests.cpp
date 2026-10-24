#include "engine/client/gl_textures.h"
#include "platform/desktop.h"
#include "platform/system.h"
#include "texture_fixture.h"
#include "rdev.h"
#include <cstdlib>
#include <future>
#include <iostream>
#include <limits>

namespace {
void require(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
template<class F> void rejects(F&& function) { bool failed=false; try { function(); } catch (const std::exception&) { failed=true; } require(failed,"invalid GPU texture request accepted"); }
struct Files {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("nolf-gl-textures-"+std::to_string(nolf::platform::monotonic_nanoseconds()));
    std::shared_ptr<nolf::Assets> assets=std::make_shared<nolf::Assets>();
    Files() { nolf::platform::atomic_write(root/"first.dtx",nolf_test::tiny_texture()); nolf::platform::atomic_write(root/"second.dtx",nolf_test::tiny_texture()); assets->mount_directory(root); }
    ~Files() { std::error_code error; std::filesystem::remove_all(root,error); }
};
struct Readback {
    void (*image)(unsigned,int,unsigned,unsigned,void*)=nullptr;
    void (*level)(unsigned,int,unsigned,int*)=nullptr;
    void (*parameter)(unsigned,unsigned,int*)=nullptr;
    unsigned (*error)()=nullptr;
    unsigned char (*is_texture)(unsigned)=nullptr;
    void (*integer)(unsigned,int*)=nullptr;
    explicit Readback(nolf::platform::Desktop& desktop) {
        image=reinterpret_cast<decltype(image)>(desktop.gl_symbol("glGetTexImage"));
        level=reinterpret_cast<decltype(level)>(desktop.gl_symbol("glGetTexLevelParameteriv"));
        parameter=reinterpret_cast<decltype(parameter)>(desktop.gl_symbol("glGetTexParameteriv"));
        error=reinterpret_cast<decltype(error)>(desktop.gl_symbol("glGetError"));
        is_texture=reinterpret_cast<decltype(is_texture)>(desktop.gl_symbol("glIsTexture"));
        integer=reinterpret_cast<decltype(integer)>(desktop.gl_symbol("glGetIntegerv"));
        require(image && level && parameter && error && is_texture && integer,"texture readback entry points unavailable");
    }
    unsigned binding() const { int value=0; integer(0x8069,&value); return static_cast<unsigned>(value); }
    int param(unsigned name) const { int value=0; parameter(0x0de1,name,&value); return value; }
    void compare(const nolf::TexturePixels& pixels,bool alpha=true) const {
        for (std::size_t i=0;i<pixels.texture.mips.size();++i) {
            const auto& expected=pixels.texture.mips[i]; int w=0,h=0;
            level(0x0de1,int(i),0x1000,&w); level(0x0de1,int(i),0x1001,&h);
            require(w==int(expected.width()) && h==int(expected.height()),"GPU mip dimensions differ");
            std::vector<std::uint32_t> actual(expected.pixels().size()); image(0x0de1,int(i),0x80e1,0x1401,actual.data());
            require(!error(),"OpenGL texture readback error");
            for (std::size_t p=0;p<actual.size();++p) require(actual[p]==(alpha ? expected.pixels()[p] : expected.pixels()[p]|0xff000000),"GPU texture pixel/channel/alpha differs");
        }
    }
};
void checks(nolf::platform::Desktop& desktop,const Readback& read) {
    Files files; nolf::ClientTextures cpu(files.assets); nolf::GlTextures gpu(40);
    auto first=cpu.load("first.dtx"),second=cpu.load("second.dtx"); const auto original=first->snapshot();
    gpu.bind(first); read.compare(*original); require(gpu.size()==1 && gpu.bytes()==20 && read.param(0x813d)==1,"GPU resource mip residency");
    const auto name=read.binding(); gpu.bind(first); require(read.binding()==name,"unchanged resource reallocated GPU texture");
    nolf::TextureSampling sampling; sampling.alpha=false; sampling.mipmaps=false; sampling.linear=false; sampling.repeat_s=false; sampling.anisotropy=1000;
    gpu.bind(first,sampling); read.compare(*original,false);
    require(read.param(0x813d)==0 && read.param(0x2801)==0x2600 && read.param(0x2800)==0x2600 && read.param(0x2802)==0x812f && read.param(0x2803)==0x2901,"sampler state not applied");
    gpu.bind(first); read.compare(*original);
    sampling={}; sampling.alpha=false; sampling.color_key=0x203040; gpu.bind(first,sampling);
    std::array<std::uint32_t,4> keyed{}; read.image(0x0de1,0,0x80e1,0x1401,keyed.data());
    require(keyed==std::array<std::uint32_t,4>{0x00203040,0xff607080,0xffa0b0c0,0xffe0f000},"RGB color key did not produce an opaque non-key/transparent key variant");
    require(first->snapshot()==original && first->snapshot()->texture.mips[0].pixels()[0]==0x10203040,"material color key mutated shared SDK texture pixels");
    gpu.bind(first); read.compare(*original);
    HTEXTURE handle=nullptr; char filename[]="first.dtx";
    require(cpu.GetTextureHandle(filename,handle,0)==LT_OK,"GPU edit handle acquisition");
    uint8* data=nullptr; long pitch=0; require(cpu.LockTexture(handle,nullptr,TLOCK_TEXTURE,data,pitch)==LT_OK,"GPU edit lock");
    *reinterpret_cast<std::uint32_t*>(data)=0xff123456; require(cpu.UnlockTexture(handle)==LT_OK,"GPU edit publish");
    gpu.bind(first); read.compare(*first->snapshot()); require(read.binding()==name,"edit discarded GPU allocation");
    const auto edited=first->snapshot(); gpu.bind(first,original,{}); read.compare(*original);
    gpu.bind(first,edited,{}); read.compare(*edited); gpu.bind(first); read.compare(*edited);
    if (g_RDCaps.nTextureUnits>=2) {
        rd_PushAttrib(RD_ATTR_ALL); struct Restore { ~Restore() { rd_PopAttrib(); } } restore;
        nolf::GlTextures units(80); nolf::TextureSampling clamp; clamp.alpha=false; clamp.repeat_s=clamp.repeat_t=false; clamp.linear=false;
        units.bind_unit(0,first,clamp); const auto unit0=read.binding(); read.compare(*edited,false);
        units.bind_unit(1,first); const auto unit1=read.binding(); read.compare(*edited);
        require(unit0!=unit1 && units.size()==2 && units.bytes()==40,"same resource on two units aliased GPU image state");
        rd_ActiveTexture(0); require(read.binding()==unit0 && read.param(0x2802)==0x812f && read.param(0x2800)==0x2600,"second unit changed first unit's sampler"); read.compare(*edited,false);
        rd_ActiveTexture(1); require(read.binding()==unit1 && read.param(0x2802)==0x2901 && read.param(0x2800)==0x2601,"first unit changed second unit's sampler");
        rejects([&] { units.bind_unit(static_cast<uint32>(g_RDCaps.nTextureUnits),first); });
        const std::array<nolf::TextureBinding,2> bindings{{{0,first,clamp},{1,first,{}}}}; units.bind_units(bindings);
        nolf::GlTextures limited(20); rejects([&] { limited.bind_units(bindings); }); require(limited.size()==0 && !limited.bytes(),"combined draw budget failure partially evicted/uploaded textures");
    }
    require(cpu.ReleaseTextureHandle(handle)==LT_OK,"GPU edit release"); gpu.bind(first); read.compare(*original);
    sampling.anisotropy=std::numeric_limits<float>::quiet_NaN(); rejects([&] { gpu.bind(first,sampling); }); rejects([&] { gpu.bind(nullptr); });
    nolf::GlTextures too_small(1); rejects([&] { too_small.bind(first); }); require(!too_small.size() && !too_small.bytes(),"failed upload retained GPU allocation");
    gpu.bind(second); require(gpu.size()==2 && gpu.bytes()==40,"GPU second resource residency");
    auto thread=std::async(std::launch::async,[&] { rejects([&] { gpu.bind(first); }); rejects([&] { gpu.clear(); }); }); thread.get();
    second.reset(); gpu.collect(); require(gpu.size()==1 && gpu.bytes()==20,"weak GPU cache retained retired resource");
    { nolf::GlTextures lru(20); lru.bind(first); lru.bind(cpu.load("second.dtx")); require(lru.size()==1 && lru.bytes()==20,"GPU LRU exceeded residency budget"); lru.bind(first); read.compare(*original); }
    const auto generation=rd_DeviceGeneration(); gpu.bind(first); const auto orphan=read.binding();
    rd_DestroyDevice(); rejects([&] { gpu.bind(first); }); require(!gpu.size() && !gpu.bytes(),"dead device retained GPU cache records");
    require(desktop.make_current(),"cannot inspect detached SDL context"); require(!read.is_texture(orphan),"device shutdown retained native texture allocation"); desktop.release_context();
    require(rd_CreateDevice(&desktop,128,128,true,0,1) && rd_DeviceGeneration()!=generation,"texture device restart failed");
    gpu.bind(first); read.compare(*original); require(gpu.size()==1 && gpu.bytes()==20,"CPU resource not restored after device restart");
    gpu.clear(); require(!gpu.size() && !gpu.bytes() && !read.error(),"GPU clear leaked texture/error");
}
void corpus(const char* data,const Readback& read) {
    auto assets=std::make_shared<nolf::Assets>(); assets->mount_game(data); nolf::ClientTextures cpu(assets); nolf::GlTextures gpu;
    unsigned textures=0,mips=0;
    for (const auto& name : assets->names()) if (name.ends_with(".dtx")) {
        const auto resource=cpu.load(name); gpu.bind(resource); const auto pixels=resource->snapshot(); read.compare(*pixels);
        ++textures; mips+=unsigned(pixels->texture.mips.size()); gpu.collect();
    }
    gpu.collect(); cpu.free_unused(); require(textures && !gpu.size() && !gpu.bytes() && !cpu.bytes(),"texture corpus resources retained");
    std::cout<<"Uploaded/read back "<<textures<<" original DTX textures / "<<mips<<" mip images exactly\n";
}
}
int main(int argc,char** argv) {
    try {
        if (argc>2) throw std::invalid_argument("usage: nolf-gl-texture-tests [game-data]");
        nolf::platform::DesktopConfig config; config.width=128; config.height=128; config.vsync=false;
        nolf::platform::Desktop desktop(config); require(rd_CreateDevice(&desktop,128,128,true,0,1),"texture device initialization failed");
        struct Device { ~Device() { rd_DestroyDevice(); } } device;
        if (const auto* expected=std::getenv("NOLF_EXPECT_VIDEO_DRIVER")) require(std::string_view(expected)==desktop.driver(),"unexpected desktop backend");
        std::cout<<"desktop="<<desktop.driver()<<" renderer="<<rd_Describe()<<'\n';
        Readback read(desktop); checks(desktop,read); if (argc==2) corpus(argv[1],read);
        std::cout<<"Native OpenGL texture checks passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
