#include "engine/portable/sprite.h"
#include "engine/portable/binary.h"
#include "engine/portable/wire.h"
#include "engine/client/client_scene.h"
#include "engine/client/client_sprites.h"
#include "engine/server/objtypes.h"
#include "platform/system.h"
#include <cstring>
#include <future>
#include <iostream>

namespace {
void require(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
template<class F> void rejects(F&& function) { bool failed=false; try { function(); } catch (const std::exception&) { failed=true; } require(failed,"invalid sprite operation accepted"); }
std::vector<std::byte> fixture(unsigned count=3,unsigned rate=30) {
    nolf::BinaryWriter out;
    for (std::uint32_t value : {count,rate,1u,1u,0x123456u}) out.integer(value);
    for (unsigned i=0;i<count;++i) {
        const auto name="Tex\\Frame"+std::to_string(i)+".DTX";
        out.integer(static_cast<std::uint16_t>(name.size())); out.bytes(std::as_bytes(std::span(name.data(),name.size())));
    }
    return out.data();
}
void parser() {
    auto file=fixture(); const auto sprite=nolf::decode_sprite(file);
    require(sprite.frames_per_second==30 && sprite.keyed && sprite.translucent && sprite.color_key==0x123456 && sprite.frames==std::vector<std::string>{"tex/frame0.dtx","tex/frame1.dtx","tex/frame2.dtx"},"sprite metadata/frame name decoding");
    for (std::size_t i=0;i<file.size();++i) rejects([&] { nolf::decode_sprite(std::span(file).first(i)); });
    for (unsigned rate : {0u,1001u,0xffffffffu}) rejects([&] { nolf::decode_sprite(fixture(1,rate)); });
    auto count=file; nolf::wire::store(count.data(),std::uint32_t(65537)); rejects([&] { nolf::decode_sprite(count); });
    auto boolean=file; nolf::wire::store(boolean.data()+8,std::uint32_t(2)); rejects([&] { nolf::decode_sprite(boolean); });
    auto length=file; nolf::wire::store(length.data()+20,std::uint16_t(1001)); rejects([&] { nolf::decode_sprite(length); });
    auto nul=file; nul[23]=std::byte{0}; rejects([&] { nolf::decode_sprite(nul); });
    auto escape=file; escape[22]=std::byte{'/'}; rejects([&] { nolf::decode_sprite(escape); });
    file.push_back(std::byte{0}); rejects([&] { nolf::decode_sprite(file); });
    require(nolf::decode_sprite(fixture(0,1)).frames.empty(),"empty animation metadata rejected");
}
struct Files {
    const std::filesystem::path root=std::filesystem::temp_directory_path()/("nolf-sprites-"+std::to_string(nolf::platform::monotonic_nanoseconds()));
    std::shared_ptr<nolf::Assets> assets=std::make_shared<nolf::Assets>();
    Files() {
        nolf::platform::atomic_write(root/"Test.SPR",fixture()); nolf::platform::atomic_write(root/"Other.SPR",fixture(4,15));
        nolf::platform::atomic_write(root/"Empty.SPR",fixture(0,1)); nolf::platform::atomic_write(root/"Broken.SPR",{}); assets->mount_directory(root);
    }
    ~Files() { std::error_code error; std::filesystem::remove_all(root,error); }
};
HOBJECT create(nolf::ClientScene& scene,const char* filename) {
    ObjectCreateStruct input; input.m_ObjectType=OT_SPRITE; std::strcpy(input.m_Filename,filename); HOBJECT object=nullptr;
    require(scene.create(input,object)==LT_OK && object,"native sprite allocation failed"); return object;
}
uint32 position(ILTSpriteControl& control) { uint32 animation=99,frame=99; require(control.GetCurPos(animation,frame)==LT_OK && !animation,"native sprite current position failed"); return frame; }
void scene() {
    Files files; nolf::ClientSprites cache(files.assets); auto cached=cache.load("TEST.SPR"); require(cache.load("test.spr")==cached,"sprite cache did not canonicalize names");
    nolf::ClientScene scene(files.assets),foreign(files.assets); auto object=create(scene,"Test.SPR");
    const auto retained=scene.sprite(object); require(retained && retained->animation().frames.size()==3,"scene did not retain sprite resource");
    ILTSpriteControl* control=nullptr; require(scene.sprite_control(object,control)==LT_OK && control,"actual SDK sprite facade absent");
    uint32 count=99; require(control->GetNumAnims(count)==LT_OK && count==1 && control->GetNumFrames(0,count)==LT_OK && count==3 && !position(*control),"sprite SDK resource counters");
    require(scene.update(.034f)==LT_OK && position(*control)==1,"quantized sprite playback");
    require(scene.update(.066f)==LT_OK && position(*control)==0,"looping sprite playback");
    require(control->SetCurPos(0,2)==LT_OK && control->SetFlags(0)==LT_OK && scene.update(1)==LT_OK && position(*control)==2,"paused sprite advanced");
    require(control->SetCurPos(0,0)==LT_OK && control->SetFlags(SC_PLAY)==LT_OK && scene.update(1)==LT_OK && position(*control)==2,"non-looping sprite did not clamp to final frame");
    require(control->SetCurPos(1,0)==LT_INVALIDPARAMS && control->SetCurPos(0,3)==LT_INVALIDPARAMS,"invalid frame changed animation");
    auto scope=scene.activate(); const auto& sdk=nolf::ClientScene::functions();
    require(sdk.ClipSprite(object,INVALID_HPOLY)==LT_OK && sdk.ClipSprite(object,0)==LT_INVALIDPARAMS,"sprite clipping polygon ownership");
    ILTSpriteControl* other=control; require(foreign.sprite_control(object,other)==LT_INVALIDPARAMS && !other && !foreign.sprite(object),"foreign scene accepted sprite control/resource");
    ObjectCreateStruct input; std::strcpy(input.m_Filename,"Broken.SPR");
    require(scene.filenames(object,input)==LT_INVALIDSPRITEFILE && scene.sprite(object)==retained && position(*control)==2,"failed sprite replacement changed live animation");
    std::strcpy(input.m_Filename,"Other.SPR");
    require(scene.filenames(object,input)==LT_OK && control->GetNumFrames(0,count)==LT_OK && count==4 && !position(*control),"sprite filename replacement/reset");
    require(retained->animation().frames.size()==3,"replacement invalidated retained render resource");
    std::strcpy(input.m_Filename,"Empty.SPR"); require(scene.filenames(object,input)==LT_OK && scene.update(1)==LT_OK && control->GetNumFrames(0,count)==LT_OK && !count,"empty sprite playback unsafe");
    auto thread=std::async(std::launch::async,[&] { rejects([&] { cache.load("test.spr"); }); rejects([&] { scene.sprite(object); }); }); thread.get();
    require(scene.remove(object)==LT_OK && !scene.sprite(object),"removed sprite retained native record");
    require(scene.sprite_control(object,other)==LT_INVALIDPARAMS && !other,"removed object exposed embedded control");
    // A large frame number at 30 FPS must agree with the quantized updater.
    std::vector<uint32> frames(200); SpriteAnim anim{frames.data(),200,30}; Sprite sprite{&anim,1}; CObjSprite native;
    native.m_pSprite=&sprite; require(native.m_Control.SetCurPos(0,100)==LT_OK && native.m_FrameTime==3300,"sprite seek disagreed with frame period");
    native.UpdateAnimation(0); require(position(native.m_Control)==100,"zero elapsed changed sought frame");
    native.UpdateAnimation(0xffffffffu); require(position(native.m_Control)==((std::uint64_t(3300)+0xffffffffu)%6600)/33,"large delta overflowed sprite clock");
}
void corpus(const char* data) {
    auto assets=std::make_shared<nolf::Assets>(); assets->mount_game(data); nolf::ClientScene scene(assets);
    unsigned sprites=0,frames=0,missing=0;
    for (const auto& name : assets->names()) if (name.ends_with(".spr")) {
        auto object=create(scene,name.c_str()); const auto resource=scene.sprite(object); const auto& animation=resource->animation();
        ILTSpriteControl* control=nullptr; require(scene.sprite_control(object,control)==LT_OK && control,"original sprite facade unavailable");
        const auto count=unsigned(animation.frames.size()); uint32 actual=0;
        require(control->GetNumFrames(0,actual)==LT_OK && actual==count,"original frame count mismatch");
        for (const auto& frame : animation.frames) { if (!assets->contains(frame)) { ++missing; std::cout<<name<<" missing authored frame "<<frame<<'\n'; } }
        if (count) {
            const auto period=1000/animation.frames_per_second; const auto duration=period*count;
            require(scene.update(1)==LT_OK && position(*control)==(1000%duration)/period,"original sprite loop timing mismatch");
            require(control->SetCurPos(0,count-1)==LT_OK && control->SetFlags(0)==LT_OK && scene.update(1)==LT_OK && position(*control)==count-1,"original sprite pause/seek mismatch");
            require(control->SetCurPos(0,0)==LT_OK && control->SetFlags(SC_PLAY)==LT_OK,"original playback control failed");
            for (unsigned i=0;i<duration/1000+1;++i) require(scene.update(1)==LT_OK,"original sprite advance failed");
            require(position(*control)==count-1,"original sprite final-frame clamp mismatch");
        }
        require(scene.remove(object)==LT_OK,"original sprite retirement failed"); ++sprites; frames+=count;
    }
    require(sprites,"empty original sprite corpus");
    std::cout<<"Loaded/played "<<sprites<<" original sprites / "<<frames<<" frames / "<<missing<<" authored missing frame assets\n";
}
}
int main(int argc,char** argv) {
    try { parser(); scene(); if (argc==2) corpus(argv[1]); else if (argc!=1) throw std::invalid_argument("usage: nolf-sprite-tests [game-data]"); std::cout<<"Native sprite checks passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
