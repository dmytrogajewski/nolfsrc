#include "engine/client/canvas_draw.h"
#include "engine/client/client_scene.h"
#include "platform/system.h"
#include "texture_fixture.h"
#include <future>
#include <iostream>
#include <limits>

namespace {
void require(bool v,const char* why) { if (!v) throw std::runtime_error(why); }
void checks() {
    const auto root=std::filesystem::temp_directory_path()/("nolf-canvas-"+std::to_string(nolf::platform::monotonic_nanoseconds()));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code e; std::filesystem::remove_all(path,e); } } cleanup{root};
    nolf::platform::atomic_write(root/"tiny.dtx",nolf_test::tiny_texture()); auto assets=std::make_shared<nolf::Assets>(); assets->mount_directory(root);
    nolf::ClientTextures textures(assets); nolf::CanvasDraw draw([&](std::string_view name) { return assets->contains(name) ? textures.load(name) : nullptr; }); ILTCustomDraw& sdk=draw;
    const std::array<uint32,NUM_LTRSTATES> defaults{0,1,1,5,6,1,3,2}; uint32 value=99;
    for (unsigned i=0;i<NUM_LTRSTATES;++i) require(sdk.GetState(static_cast<LTRState>(i),value)==LT_OK && value==defaults[i],"canvas native default state differs");
    require(sdk.SetState(NUM_LTRSTATES,0)==LT_INVALIDPARAMS && sdk.SetState(LTRSTATE_SRCBLEND,0)==LT_INVALIDPARAMS && sdk.SetState(LTRSTATE_ALPHAOP,6)==LT_INVALIDPARAMS,"invalid canvas state accepted");
    require(sdk.GetState(NUM_LTRSTATES,value)==LT_INVALIDPARAMS && value==0,"invalid canvas getter did not initialize output");
    float u=9,v=9; require(sdk.GetTexelSize(u,v)==LT_OK && u==0 && v==0 && sdk.SetTexture("tiny.dtx")==LT_OK && sdk.GetTexelSize(u,v)==LT_OK && u==.5f && v==.5f,"canvas texture selection/texel size failed");
    std::array<LTVertex,4> vertices; for (unsigned i=0;i<4;++i) { auto& p=vertices[i]; p.m_Vec.Init(float(i),float(i+1),10); p.m_Color.Init(10,20,30,40); p.m_TU=.25f; p.m_TV=.5f; }
    require(sdk.DrawPrimitive(vertices.data(),4,0)==LT_OK && draw.commands().size()==1,"canvas command recording failed");
    const auto original=draw.commands()[0].pixels;
    vertices[0].m_Vec.x=999; require(draw.commands()[0].vertices[0].position.x==0 && draw.commands()[0].vertices[0].color==std::array<uint8,4>{10,20,30,40},"canvas borrowed mutable game geometry or BGRA order");
    require(sdk.SetState(LTRSTATE_ALPHABLENDENABLE,1)==LT_OK && sdk.SetTexture("missing.dtx")==LT_NOTFOUND && sdk.GetTexelSize(u,v)==LT_OK && u==.5f,"failed canvas texture selection changed state");
    HTEXTURE handle=nullptr; char name[]="tiny.dtx"; require(textures.GetTextureHandle(name,handle,0)==LT_OK,"canvas edit handle failed"); uint8* pixels=nullptr; long pitch=0;
    require(textures.LockTexture(handle,nullptr,TLOCK_TEXTURE,pixels,pitch)==LT_OK,"canvas texture edit lock failed"); *reinterpret_cast<uint32*>(pixels)=0xff00ff00; require(textures.UnlockTexture(handle)==LT_OK,"canvas texture edit failed");
    require(sdk.DrawPrimitive(vertices.data(),3,0)==LT_OK && draw.commands()[0].states[LTRSTATE_ALPHABLENDENABLE]==0 && draw.commands()[1].states[LTRSTATE_ALPHABLENDENABLE]==1 && draw.commands()[0].pixels==original && original->texture.mips[0].at(0,0)==0x10203040 && draw.commands()[1].pixels->texture.mips[0].at(0,0)==0xff00ff00,"canvas geometry/state/texture versions were not captured per primitive");
    require(textures.ReleaseTextureHandle(handle)==LT_OK,"canvas edit release failed");
    require(sdk.SetTexture(nullptr)==LT_OK && sdk.DrawPrimitive(vertices.data(),3,0)==LT_OK && !draw.commands()[2].texture && !draw.commands()[2].pixels,"untextured canvas primitive retained texture");
    const auto count=draw.commands().size(); require(sdk.DrawPrimitive(nullptr,3,0)==LT_INVALIDPARAMS && sdk.DrawPrimitive(vertices.data(),2,0)==LT_INVALIDPARAMS && sdk.DrawPrimitive(vertices.data(),101,0)==LT_INVALIDPARAMS,"invalid canvas geometry accepted");
    vertices[0].m_TU=std::numeric_limits<float>::infinity(); require(sdk.DrawPrimitive(vertices.data(),4,0)==LT_INVALIDPARAMS && draw.commands().size()==count,"invalid canvas primitive partially recorded"); vertices[0].m_TU=0;
    require(std::async(std::launch::async,[&] { return sdk.DrawPrimitive(vertices.data(),3,0); }).get()==LT_ERROR,"canvas crossed device ownership thread");
    nolf::ClientScene scene; ObjectCreateStruct input; input.m_ObjectType=OT_CANVAS; HOBJECT object=nullptr; require(scene.create(input,object)==LT_OK,"native canvas creation failed");
    const auto callback=+[](ILTCustomDraw*,HOBJECT,void*) {}; int user=1; require(scene.canvas_function(object,callback,&user)==LT_OK && scene.canvas_radius(object,10000)==LT_OK,"native canvas control failed");
    auto canvas=scene.canvas(object); require(canvas && canvas->draw==callback && canvas->user==&user && canvas->radius==10000 && object->_v2c()==10000,"native canvas metadata and virtual radius differ");
    require(scene.canvas_radius(object,-1)==LT_INVALIDPARAMS && scene.canvas_radius(object,std::numeric_limits<float>::quiet_NaN())==LT_INVALIDPARAMS,"invalid canvas visibility radius accepted");
    require(scene.remove(object)==LT_OK && !scene.canvas(object) && scene.canvas_function(object,nullptr,nullptr)==LT_INVALIDPARAMS,"retired canvas control dereferenced storage");
    nolf::CanvasDraw bounded([&](std::string_view filename) { return textures.load(filename); }); std::array<LTVertex,100> many;
    for (auto& p : many) { p.m_Vec.Init(0,0,10); p.m_Color.Init(255,255,255,255); p.m_TU=p.m_TV=0; }
    for (unsigned i=0;i<2621;++i) require(bounded.DrawPrimitive(many.data(),100,0)==LT_OK,"canvas budget rejected valid bounded commands");
    require(bounded.DrawPrimitive(many.data(),100,0)==LT_OUTOFMEMORY && bounded.commands().size()==2621,"canvas total vertex budget/atomic append failed");
}
}
int main() { try { checks(); std::cout<<"Native custom draw states, owned geometry/texture versions, canvas controls, budgets and thread ownership passed\n"; return 0; } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
