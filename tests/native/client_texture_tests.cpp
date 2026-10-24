#include "engine/client/client_textures.h"
#include "engine/portable/wire.h"
#include "platform/system.h"
#include "texture_fixture.h"
#include <future>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
template<class F> void rejects(F&& function) { bool failed=false; try { function(); } catch (const std::exception&) { failed=true; } require(failed,"invalid texture operation accepted"); }
struct Files {
    const std::filesystem::path root=std::filesystem::temp_directory_path()/("nolf-client-textures-"+std::to_string(nolf::platform::monotonic_nanoseconds()));
    std::shared_ptr<nolf::Assets> assets=std::make_shared<nolf::Assets>();
    Files() {
        const auto file=nolf_test::tiny_texture();
        nolf::platform::atomic_write(root/"Tex"/"Sample.DTX",file);
        nolf::platform::atomic_write(root/"Tex"/"Broken.DTX",std::span(file).first(170));
        assets->mount_directory(root);
    }
    ~Files() { std::error_code error; std::filesystem::remove_all(root,error); }
};
void service() {
    Files files; nolf::ClientTextures cache(files.assets); ILTTexMod& sdk=cache;
    const auto resource=cache.load("TEX\\SAMPLE.DTX"); const auto original=resource->snapshot();
    const auto baseline=cache.bytes();
    require(resource==cache.load("tex/sample.dtx") && resource->name()=="tex/sample.dtx" && resource->revision()==1 && baseline>20,"canonical texture cache identity");
    char filename[]="tex/sample.dtx",missing[]="missing.dtx",broken[]="tex/broken.dtx",traversal[]="../outside.dtx";
    HTEXTURE handle=nullptr,second=nullptr;
    require(sdk.GetTextureHandle(filename,handle,THANDLE_BUMPMAP)==LT_OK && handle,"SDK edit allocation");
    require(sdk.GetTextureHandle(filename,second,THANDLE_BUMPMAP)==LT_OK && second==handle,"duplicate edit did not retain identity");
    require(sdk.GetTextureHandle(filename,second,0)==LT_INVALIDPARAMS && !second,"flags changed while handle is held");
    require(sdk.GetTextureHandle(filename,second,2)==LT_INVALIDPARAMS && sdk.GetTextureHandle(nullptr,second,0)==LT_INVALIDPARAMS &&
        sdk.GetTextureHandle(missing,second,0)==LT_NOTFOUND && sdk.GetTextureHandle(broken,second,0)!=LT_OK &&
        sdk.GetTextureHandle(traversal,second,0)==LT_INVALIDPARAMS,"invalid texture input accepted");
    TextureInfo info{}; require(sdk.GetTextureInfo(handle,info)==LT_OK && info.m_Width==2 && info.m_Height==2,"texture dimensions");
    require(sdk.WasTextureDrawnLastFrame(handle)==LT_NO,"undrawn texture reported visible");
    std::uint64_t render_token=0;
    { auto render=cache.begin_render(); render_token=render.token(); require(render_token!=0,"active render has no identity"); render.drawn(resource); require(sdk.WasTextureDrawnLastFrame(handle)==LT_NO,"incomplete render changed preceding visibility"); render.finish(); rejects([&] { render.finish(); }); rejects([&] { render.token(); }); }
    require(sdk.WasTextureDrawnLastFrame(handle)==LT_YES,"completed render lost visibility");
    { auto render=cache.begin_render(); require(render.token()!=render_token,"new render reused a retired transaction identity"); rejects([&] { auto nested=cache.begin_render(); }); }
    require(sdk.WasTextureDrawnLastFrame(handle)==LT_YES,"aborted render changed visibility");
    { auto render=cache.begin_render(); render.finish(); }
    require(sdk.WasTextureDrawnLastFrame(handle)==LT_NO,"next completed render retained invisible texture");
    uint8* data=nullptr; long pitch=0; const LTRect rect{1,1,2,2};
    require(sdk.LockTexture(handle,&rect,TLOCK_BUMPMAP,data,pitch)==LT_OK && pitch==2 && *data==0xd0,"bump height did not originate from alpha or rectangle origin");
    *data=17;
    require(sdk.ReleaseTextureHandle(handle)==LT_OK && sdk.ReleaseTextureHandle(handle)==LT_ERROR,"last locked handle was retired");
    uint8* rejected=reinterpret_cast<uint8*>(1); long rejected_pitch=1;
    require(sdk.LockTexture(handle,nullptr,TLOCK_TEXTURE,rejected,rejected_pitch)==LT_ERROR && !rejected && !rejected_pitch,"double lock exposed another pointer");
    require(sdk.UnlockTexture(handle)==LT_OK && resource->revision()==2 && resource->snapshot()->bumpmap[3]==17,"bump edit did not publish");
    require(original->bumpmap.empty() && original->texture.mips[1].at(0,0)==0x12345678,"edit changed retained source snapshot");
    for (const LTRect invalid : {LTRect{-1,0,1,1},LTRect{0,-1,1,1},LTRect{0,0,3,1},LTRect{0,0,1,3},LTRect{1,1,1,2}})
        require(sdk.LockTexture(handle,&invalid,TLOCK_TEXTURE,data,pitch)==LT_INVALIDPARAMS && !data && !pitch,"invalid texture rectangle accepted");
    require(sdk.LockTexture(handle,nullptr,2,data,pitch)==LT_INVALIDPARAMS,"invalid lock type accepted");
    require(sdk.LockTexture(handle,&rect,TLOCK_TEXTURE,data,pitch)==LT_OK && pitch==8 && *reinterpret_cast<std::uint32_t*>(data)==0xd0e0f000,"color lock rectangle/pitch");
    *reinterpret_cast<std::uint32_t*>(data)=0xffffffff;
    require(resource->snapshot()->texture.mips[0].at(1,1)==0xd0e0f000,"unpublished editing memory reached renderer");
    require(sdk.UnlockTexture(handle)==LT_OK && sdk.UnlockTexture(handle)==LT_NOTINITIALIZED,"lock state not tracked");
    auto edited=resource->snapshot();
    require(edited->texture.mips[0].at(1,1)==0xffffffff && edited->texture.mips[1].at(0,0)==0x7b87939f && resource->revision()==3,"edit or regenerated mip pixel wrong");
    auto thread=std::async(std::launch::async,[&] {
        TextureInfo invalid{99,99};
        require(sdk.GetTextureInfo(handle,invalid)!=LT_OK && !invalid.m_Width && !invalid.m_Height,"cross-thread SDK texture access");
        require(sdk.ReleaseTextureHandle(handle)!=LT_OK,"cross-thread release changed ownership");
        rejects([&] { cache.load(filename); }); rejects([&] { resource->snapshot(); });
    }); thread.get();
    nolf::ClientTextures foreign(files.assets);
    require(foreign.ReleaseTextureHandle(handle)==LT_INVALIDPARAMS,"foreign cache accepted handle");
    { auto render=foreign.begin_render(); rejects([&] { render.drawn(resource); }); }
    require(sdk.ReleaseTextureHandle(handle)==LT_OK && resource->snapshot()==original && resource->revision()==4,"last release failed to restore disk pixels");
    require(edited->texture.mips[0].at(1,1)==0xffffffff,"release invalidated retained edited snapshot");
    require(sdk.ReleaseTextureHandle(handle)==LT_INVALIDPARAMS && sdk.GetTextureInfo(handle,info)==LT_INVALIDPARAMS && !info.m_Width && !info.m_Height,"retired handle became live");
    require(sdk.GetTextureHandle(filename,second,0)==LT_OK && second!=handle,"reacquisition reused stale handle");
    require(sdk.LockTexture(second,nullptr,TLOCK_BUMPMAP,data,pitch)==LT_NOTINITIALIZED,"unrequested bump plane allocated");
    require(sdk.ReleaseTextureHandle(second)==LT_OK,"new handle retirement failed");
    edited.reset(); require(cache.bytes()==baseline,"editing storage leaked after all leases retired");
    nolf::ClientTextures limited(files.assets,baseline+16);
    const auto limited_resource=limited.load(filename); const auto before=limited_resource->snapshot();
    require(limited.GetTextureHandle(filename,second,0)==LT_OK && limited.LockTexture(second,nullptr,TLOCK_TEXTURE,data,pitch)==LT_OK,"bounded edit setup");
    *reinterpret_cast<std::uint32_t*>(data)=0;
    require(limited.UnlockTexture(second)==LT_OUTOFMEMORY && limited_resource->snapshot()==before && limited_resource->revision()==1 && limited.ReleaseTextureHandle(second)==LT_ERROR,"failed publish changed pixels or released locked storage");
    nolf::ClientTextures too_small(files.assets,1); rejects([&] { too_small.load(filename); }); require(!too_small.bytes(),"failed load leaked budget");
}
void lifetime() {
    Files files; std::shared_ptr<const nolf::TextureResource> retained;
    {
        auto cache=std::make_unique<nolf::ClientTextures>(files.assets); retained=cache->load("tex/sample.dtx");
        auto render=cache->begin_render(); render.drawn(retained);
        HTEXTURE handle=nullptr; char filename[]="tex/sample.dtx";
        require(cache->GetTextureHandle(filename,handle,0)==LT_OK,"retirement setup failed");
        uint8* data=nullptr; long pitch=0; require(cache->LockTexture(handle,nullptr,TLOCK_TEXTURE,data,pitch)==LT_OK,"retirement lock");
        *reinterpret_cast<std::uint32_t*>(data)=0; require(cache->UnlockTexture(handle)==LT_OK,"retirement unlock");
        cache.reset(); rejects([&] { render.drawn(retained); }); rejects([&] { render.finish(); });
        require(retained->snapshot()->texture.mips[0].at(0,0)==0x10203040,"service retirement did not restore retained resource");
    }
    require(retained->name()=="tex/sample.dtx","cache retirement invalidated retained identity");
}
void corpus(const char* data) {
    auto assets=std::make_shared<nolf::Assets>(); assets->mount_game(data); nolf::ClientTextures cache(assets);
    unsigned count=0;
    for (auto name : assets->names()) if (name.ends_with(".dtx")) {
        const auto texture=cache.load(name); const auto original=texture->snapshot();
        HTEXTURE handle=nullptr; require(cache.GetTextureHandle(name.data(),handle,THANDLE_BUMPMAP)==LT_OK,"original texture SDK acquisition failed");
        const auto& image=original->texture.mips[0]; const auto pixel=image.at(image.width()-1,image.height()-1);
        const LTRect rect{int(image.width()-1),int(image.height()-1),int(image.width()),int(image.height())};
        uint8* pointer=nullptr; long pitch=0;
        require(cache.LockTexture(handle,&rect,TLOCK_BUMPMAP,pointer,pitch)==LT_OK && *pointer==pixel>>24 && pitch==long(image.width()),"original bump height/pitch mismatch");
        require(cache.UnlockTexture(handle)==LT_OK,"original bump publish failed");
        require(cache.LockTexture(handle,&rect,TLOCK_TEXTURE,pointer,pitch)==LT_OK && *reinterpret_cast<std::uint32_t*>(pointer)==pixel && pitch==long(image.width()*4),"original color lock mismatch");
        *reinterpret_cast<std::uint32_t*>(pointer)^=0xffffff;
        require(cache.UnlockTexture(handle)==LT_OK && texture->snapshot()->texture.mips[0].at(image.width()-1,image.height()-1)==(pixel^0xffffff),"original texture edit publish failed");
        { auto render=cache.begin_render(); render.drawn(texture); render.finish(); }
        require(cache.WasTextureDrawnLastFrame(handle)==LT_YES && cache.ReleaseTextureHandle(handle)==LT_OK && texture->snapshot()==original,"original texture visibility/restoration failed");
        ++count;
    }
    cache.free_unused(); require(count && !cache.bytes(),"weak texture cache retained corpus pixel allocations");
    std::cout<<"Loaded, edited and restored "<<count<<" original textures through SDK\n";
}
}
int main(int argc,char** argv) {
    try { service(); lifetime(); if (argc==2) corpus(argv[1]); else if (argc!=1) throw std::invalid_argument("usage: nolf-client-texture-tests [game-data]"); std::cout<<"Native client textures passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
