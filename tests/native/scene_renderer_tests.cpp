#include "engine/client/scene_renderer.h"
#include "engine/client/shadow_maps.h"
#include "engine/client/pixel_lighting.h"
#include "engine/server/commonmodel.h"
#include "engine/portable/wire.h"
#include "engine/portable/binary.h"
#include "platform/desktop.h"
#include "platform/system.h"
#include "model_fixture.h"
#include "world_render_fixture.h"
#include "engine/server/world_state.h"
#include "engine/server/objtypes.h"
#include "engine/client/client_sprites.h"
#include "engine/client/client_effects.h"
#include "engine/client/canvas_draw.h"
#include "engine/client/gl_polygrids.h"
#include "rdev.h"
#include <algorithm>
#include <cstring>
#include <future>
#include <iostream>

namespace {
void require(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
template<class F> void rejects(F&& function) { bool failed=false; try { function(); } catch (const std::exception&) { failed=true; } require(failed,"invalid model scene request accepted"); }
void near(unsigned color,unsigned expected) {
    for (unsigned shift : {0u,8u,16u}) require(std::abs(int((color>>shift)&255)-int((expected>>shift)&255))<=2,"native model scene pixel differs");
}
struct Files {
    const std::filesystem::path root=std::filesystem::temp_directory_path()/("nolf-scene-renderer-"+std::to_string(nolf::platform::monotonic_nanoseconds()));
    std::shared_ptr<nolf::Assets> assets=std::make_shared<nolf::Assets>();
    Files() {
        nolf::platform::atomic_write(root/"triangle.abc",nolf_test::tiny_model({},"start",true));
        std::vector<std::byte> bytes(168); nolf::wire::store(bytes.data()+4,std::int32_t(-5));
        for (unsigned offset : {8u,10u,12u}) nolf::wire::store(bytes.data()+offset,std::uint16_t(1));
        bytes[26]=std::byte{3}; nolf::wire::store(bytes.data()+164,std::uint32_t(0x00804020));
        nolf::platform::atomic_write(root/"color.dtx",bytes);
        nolf::wire::store(bytes.data()+16,uint32(1)); nolf::platform::atomic_write(root/"fullbright.dtx",bytes); nolf::wire::store(bytes.data()+16,uint32(0));
        nolf::wire::store(bytes.data()+164,std::uint32_t(0x00204080)); nolf::platform::atomic_write(root/"other.dtx",bytes);
        nolf::BinaryWriter sprite;
        for (std::uint32_t value : {2u,2u,0u,0u,0u}) sprite.integer(value);
        for (const std::string name : {"color.dtx","other.dtx"}) {
            sprite.integer(static_cast<std::uint16_t>(name.size())); sprite.bytes(std::as_bytes(std::span(name.data(),name.size())));
        }
        nolf::platform::atomic_write(root/"animated.spr",sprite.data());
        nolf::BinaryWriter keyed; for (uint32 value : {1u,1u,1u,1u,0x804020u}) keyed.integer(value);
        const std::string key_frame="color.dtx"; keyed.integer(uint16(key_frame.size())); keyed.bytes(std::as_bytes(std::span(key_frame.data(),key_frame.size())));
        nolf::platform::atomic_write(root/"keyed.spr",keyed.data());
        nolf::platform::atomic_write(root/"world.dat",nolf_test::render_world_fixture());
        nolf::platform::atomic_write(root/"move.dat",nolf_test::render_world_fixture(true)); assets->mount_directory(root);
    }
    ~Files() { std::error_code error; std::filesystem::remove_all(root,error); }
};
void clear() { rd_Disable(RD_SCISSOR_TEST); rd_DepthMask(true); rd_ColorMask(true); rd_ClearColor(0,0,1,1); rd_Clear(RD_CLEAR_COLOR|RD_CLEAR_DEPTH); }
unsigned pixel() { unsigned value=0; rd_ReadPixels(60,61,1,1,RD_PIX_BGRA,&value); return value&0xffffff; }
HOBJECT model(nolf::ClientScene& scene) {
    ObjectCreateStruct create; create.m_ObjectType=OT_MODEL; create.m_Pos.Init(-1,0,0);
    create.m_Flags=FLAG_VISIBLE|FLAG_NOLIGHT; std::strcpy(create.m_Filename,"triangle.abc"); std::strcpy(create.m_SkinName,"color.dtx");
    HOBJECT object=nullptr; require(scene.create(create,object)==LT_OK,"scene model fixture failed"); return object;
}
void effect_scene(nolf::platform::Desktop& desktop) {
    Files files; auto scene=std::make_shared<nolf::ClientScene>(files.assets); auto textures=std::make_shared<nolf::ClientTextures>(files.assets);
    nolf::SceneRenderer renderer(scene,textures,files.assets); require(scene->viewport(128,128)==LT_OK,"effect viewport failed");
    ObjectCreateStruct create; create.m_ObjectType=OT_CAMERA; HOBJECT camera=nullptr; require(scene->create(create,camera)==LT_OK,"effect camera failed");
    create=ObjectCreateStruct{}; create.m_ObjectType=OT_PARTICLESYSTEM; create.m_Pos.Init(0,0,20); create.m_Flags=FLAG_VISIBLE; HOBJECT system=nullptr;
    require(scene->create(create,system)==LT_OK,"particle render system failed"); auto& effects=scene->effects();
    require(effects.setup(system,"animated.spr",0,PS_DUMB,2)==LT_OK,"particle texture setup failed"); auto* particle=effects.add_particle(system,{0,0,0},{0,0,0},{255,255,255},1);
    auto sample=[](int x=64,int y=64) { uint32 color=0; rd_ReadPixels(x,y,1,1,RD_PIX_BGRA,&color); return color; };
    HTEXTURE handle=nullptr; char filename[]="color.dtx"; require(textures->GetTextureHandle(filename,handle,0)==LT_OK,"effect texture visibility handle failed");
    clear(); auto stats=renderer.render(camera); near(sample(),0x804020); require(stats.effects.systems==1 && stats.effects.particles==1 && !stats.pending_objects,"native particle scene pass missed quad"); require(textures->WasTextureDrawnLastFrame(handle)==LT_YES,"particle texture visibility not published");
    require(scene->update(.5f)==LT_OK,"particle sprite animation failed"); clear(); renderer.render(camera); near(sample(),0x204080);
    require(effects.setup(system,"color.dtx",0,PS_DUMB,2)==LT_OK,"particle static texture failed"); particle->m_Alpha=.5f; clear(); renderer.render(camera); near(sample(),0x402090);
    system->m_Flags2=FLAG2_ADDITIVE; clear(); renderer.render(camera); near(sample(),0x8040ff); system->m_Flags2=FLAG2_MULTIPLY; clear(); renderer.render(camera); near(sample(),0x000020); system->m_Flags2=0; particle->m_Alpha=1;
    require(effects.setup(system,"keyed.spr",0,PS_DUMB,2)==LT_OK,"keyed particle texture failed"); clear(); renderer.render(camera); near(sample(),0x0000ff);
    require(effects.setup(system,"",0,PS_DUMB,2)==LT_OK,"untextured particle setup failed"); particle->m_Color.Init(255,0,0); clear(); renderer.render(camera); near(sample(),0xff0000);
    effects.particle_position(system,particle,{0,0,-30}); clear(); stats=renderer.render(camera); near(sample(),0x0000ff); require(!stats.effects.particles && stats.effects.culled==1,"behind-camera particle submitted"); effects.particle_position(system,particle,{0,0,0});
    require(scene->load_world("world.dat")==LT_OK,"effect depth world failed"); clear(); renderer.render(camera); near(sample(),0x000020);
    { std::array<HOBJECT,1> list{system}; clear(); const auto result=renderer.render_objects(camera,list); near(sample(),0xff0000); require(result.effects.particles==1 && !result.world.polygons,"explicit particle list rendered an implicit world"); }
    system->m_Flags|=FLAG_REALLYCLOSE; require(scene->position(camera,{100000,0,0},true)==LT_OK && scene->position(system,{0,0,3},true)==LT_OK,"close particle setup failed"); clear(); stats=renderer.render(camera); near(sample(),0xff0000); require(stats.effects.particles==1,"camera-relative particle rejected by world visibility");
    require(scene->position(camera,{0,0,0},true)==LT_OK && scene->unload_world()==LT_OK,"particle depth world cleanup failed"); system->m_Flags&=~FLAG_REALLYCLOSE;
    // Local centres follow full object rotation and scale; size remains a screen square.
    effects.particle_position(system,particle,{10,0,0}); require(scene->position(system,{0,-20,20},true)==LT_OK && scene->scale(system,{2,3,4})==LT_OK && scene->rotation(system,{0,0,std::sqrt(.5f),std::sqrt(.5f)})==LT_OK,"particle relative transform failed");
    clear(); renderer.render(camera); near(sample(),0xff0000);
    const auto triangle=model(*scene); auto* instance=static_cast<ModelInstance*>(triangle); auto& tri=instance->m_ModelOwner->m_Pieces[0]->m_Array10[0]; std::swap(tri.indices[0],tri.indices[1]);
    nolf::SceneRenderOptions options; options.models.hook_user=scene.get(); options.models.hook=+[](ModelHookData*,void* user) { for (const auto& entry : static_cast<nolf::ClientScene*>(user)->snapshot()) if (entry.object->m_ObjectType==OT_PARTICLESYSTEM) static_cast<nolf::ClientScene*>(user)->remove(entry.object); };
    clear(); stats=renderer.render(camera,options); require(stats.effects.removed==1 && !scene->contains(system),"model hook left a freed particle system in draw queue"); require(scene->remove(triangle)==LT_OK,"effect hook model cleanup failed");
    create=ObjectCreateStruct{}; create.m_ObjectType=OT_LINESYSTEM; create.m_Pos.Init(0,0,20); create.m_Flags=FLAG_VISIBLE; HOBJECT line_system=nullptr; require(scene->create(create,line_system)==LT_OK,"line render system failed");
    LTLine line{}; line.m_Points[0]={{-10,0,0},1,0,0,1}; line.m_Points[1]={{10,0,0},1,0,0,1}; const auto line_handle=effects.add_line(line_system,line);
    auto line_pixel=[&] { for (int y=62;y<=65;++y) { const auto color=sample(64,y)&0xffffff; if (color!=0x0000ff) return color; } return uint32(0x0000ff); };
    clear(); stats=renderer.render(camera); near(line_pixel(),0xff0000); require(stats.effects.lines==1 && stats.effects.systems==1 && !stats.pending_objects,"line scene pass missed canonical endpoints");
    require(scene->color(line_system,0,1,0,.5f)==LT_OK,"line alpha setup failed"); clear(); renderer.render(camera); near(line_pixel(),0x7f0080); // Endpoint RGB is independent of object RGB.
    line.m_Points[1].r=0; line.m_Points[1].g=1; effects.line(line_handle,line); require(scene->color(line_system,1,1,1,1)==LT_OK,"line alpha reset failed"); clear(); renderer.render(camera); const auto gradient=line_pixel(); require(((gradient>>16)&255)>100 && ((gradient>>8)&255)>100,"line endpoint colors did not interpolate");
    require(scene->rotation(line_system,{0,0,std::sqrt(.5f),std::sqrt(.5f)})==LT_OK,"line rotation failed"); clear(); stats=renderer.render(camera); require(stats.effects.lines==1,"rotated local line was omitted");
    auto integer=reinterpret_cast<void (*)(unsigned,int*)>(desktop.gl_symbol("glGetIntegerv")); require(integer,"effect GL state inspection unavailable"); rd_MatrixMode(RD_TEXTURE); rd_Disable(RD_DEPTH_TEST); clear(); renderer.render(camera); int mode=0; integer(0x0ba0,&mode); require(mode==0x1702,"effect drawing failed to restore matrix state");
    require(textures->ReleaseTextureHandle(handle)==LT_OK && scene->remove(line_system)==LT_OK,"effect scene cleanup failed");
    std::cout<<"Native particle/line pixels, local transforms, animation, keyed/untextured materials, blends, world depth, view models and callback removal passed\n";
}
struct CanvasContext {
    nolf::ClientScene* scene=nullptr;
    nolf::ClientTextures* textures=nullptr;
    std::unique_ptr<nolf::SceneRenderer>* renderer=nullptr;
    unsigned mode=0,color_op=0,alpha_op=0,calls=0;
    float z=20;
    HOBJECT remove=nullptr;
};
void polygrid_scene(nolf::platform::Desktop& desktop) {
    Files files; auto scene=std::make_shared<nolf::ClientScene>(files.assets); auto textures=std::make_shared<nolf::ClientTextures>(files.assets);
    nolf::SceneRenderer renderer(scene,textures,files.assets); require(scene->viewport(128,128)==LT_OK,"grid viewport failed");
    ObjectCreateStruct create; create.m_ObjectType=OT_CAMERA; HOBJECT camera=nullptr; require(scene->create(create,camera)==LT_OK,"grid camera failed");
    create=ObjectCreateStruct{}; create.m_ObjectType=OT_POLYGRID; create.m_Flags=FLAG_VISIBLE; create.m_Pos.Init(0,0,20); create.m_Scale.Init(4,4,4); create.m_Rotation.Init(std::sqrt(.5f),0,0,std::sqrt(.5f)); HOBJECT object=nullptr;
    require(scene->create(create,object)==LT_OK,"grid object failed"); auto& effects=scene->effects();
    require(effects.setup_grid(object,2,2,false) && effects.grid_texture(object,"animated.spr")==LT_OK,"grid buffers/material failed");
    char* heights=nullptr; uint32 width=0,height=0; PGColor* palette=nullptr; effects.grid_info(object,heights,width,height,palette); for (unsigned i=0;i<256;++i) palette[i]={255,255,255,255};
    auto sample=[] { uint32 color=0; rd_ReadPixels(64,64,1,1,RD_PIX_BGRA,&color); return color; };
    clear(); auto stats=renderer.render(camera); near(sample(),0x804020); require(stats.effects.polygrids==1 && stats.effects.triangles==2 && !stats.pending_objects,"grid scene missed native prepared mesh");
    require(scene->update(.5f)==LT_OK,"grid animation advance failed"); clear(); renderer.render(camera); near(sample(),0x204080);
    require(effects.grid_texture(object,"animated.spr")==LT_OK && effects.grid_environment(object,"other.dtx")==LT_OK,"grid environment setup failed");
    clear(); renderer.render(camera); near(sample(),0x101010);
    require(scene->color(object,0,0,0,.5f)==LT_OK,"grid fade setup failed"); clear(); renderer.render(camera); near(sample(),0x080888);
    require(scene->color(object,0,0,0,1)==LT_OK,"grid fade reset failed"); object->m_Flags|=FLAG_ENVIRONMENTMAPONLY; clear(); renderer.render(camera); near(sample(),0x204080); object->m_Flags&=~FLAG_ENVIRONMENTMAPONLY;
    require(effects.grid_environment(object,"color.dtx")==LT_OK,"aliased environment setup failed"); clear(); renderer.render(camera); near(sample(),0x401004);
    nolf::SceneRenderOptions options; options.world.environment_add=true; clear(); renderer.render(camera,options); near(sample(),0x800000);
    require(effects.grid_environment(object,nullptr)==LT_OK && effects.grid_texture(object,"keyed.spr")==LT_OK,"keyed grid setup failed"); clear(); renderer.render(camera); near(sample(),0x0000ff);
    require(effects.grid_texture(object,"animated.spr")==LT_OK && effects.setup_grid(object,2,2,true),"half grid setup failed"); clear(); stats=renderer.render(camera); require(stats.effects.polygrids==1 && stats.effects.triangles==1,"half grid submitted wrong triangle count");
    require(effects.setup_grid(object,2,2,false),"full grid restoration failed"); effects.grid_info(object,heights,width,height,palette); for (unsigned i=0;i<256;++i) palette[i]={255,255,255,255};
    object->m_Flags|=FLAG_UNSIGNED; for (unsigned i=0;i<4;++i) heights[i]=char(128); clear(); renderer.render(camera); near(sample(),0x804020); object->m_Flags&=~FLAG_UNSIGNED; for (unsigned i=0;i<4;++i) heights[i]=0;
    require(scene->load_world("world.dat")==LT_OK,"grid depth world failed"); clear(); renderer.render(camera); near(sample(),0x000020);
    object->m_Flags|=FLAG_REALLYCLOSE; require(scene->position(object,{0,0,3},true)==LT_OK && scene->position(camera,{100000,0,0},true)==LT_OK,"close grid setup failed"); clear(); stats=renderer.render(camera); near(sample(),0x804020); require(stats.effects.polygrids==1,"close grid rejected by main world visibility");
    object->m_Flags&=~FLAG_REALLYCLOSE; require(scene->unload_world()==LT_OK && scene->position(object,{0,0,20},true)==LT_OK && scene->position(camera,{0,0,0},true)==LT_OK,"grid world cleanup failed");
    require(effects.grid_environment(object,"other.dtx")==LT_OK,"grid state fixture failed");
    auto floats=reinterpret_cast<void (*)(unsigned,float*)>(desktop.gl_symbol("glGetFloatv")); auto integer=reinterpret_cast<void (*)(unsigned,int*)>(desktop.gl_symbol("glGetIntegerv")); require(floats && integer,"grid GL state inspection unavailable");
    std::array<float,16> matrix{}; matrix[0]=2; matrix[5]=matrix[10]=matrix[15]=1; matrix[12]=.25f; rd_ActiveTexture(1); rd_MatrixMode(RD_TEXTURE); rd_LoadMatrix(matrix.data());
    clear(); renderer.render(camera); std::array<float,16> actual{}; floats(0x0ba8,actual.data()); int unit=0,mode=0; integer(0x84e0,&unit); integer(0x0ba0,&mode); require(actual==matrix && unit==0x84c1 && mode==0x1702,"grid draw failed to restore texture-unit/matrix state"); rd_LoadIdentity(); rd_ActiveTexture(0); rd_MatrixMode(RD_MODELVIEW);
    const auto target=model(*scene); auto* model_object=static_cast<ModelInstance*>(target); auto& triangle=model_object->m_ModelOwner->m_Pieces[0]->m_Array10[0]; std::swap(triangle.indices[0],triangle.indices[1]);
    options={}; options.models.hook_user=scene.get(); options.models.hook=+[](ModelHookData*,void* context) { auto& scene=*static_cast<nolf::ClientScene*>(context); for (const auto& entry : scene.snapshot()) if (entry.object->m_ObjectType==OT_POLYGRID) scene.remove(entry.object); };
    clear(); stats=renderer.render(camera,options); require(stats.effects.removed==1 && !scene->contains(object),"model callback removal left stale grid buffers in draw queue");
    std::cout<<"Native polygrid pixels, material animation, reflection/base maps, fade, aliased texture units, half meshes, unsigned heights, close depth and callback removal passed\n";
}
void canvas_callback(ILTCustomDraw* draw,HOBJECT object,void* pointer) {
    auto& context=*static_cast<CanvasContext*>(pointer); ++context.calls;
    std::array<LTVertex,4> vertices; const std::array<LTVector,4> points{{{-8,8,context.z},{8,8,context.z},{8,-8,context.z},{-8,-8,context.z}}};
    for (unsigned i=0;i<4;++i) { vertices[i].m_Vec=points[i]; vertices[i].m_Color.Init(255,0,0,255); vertices[i].m_TU=vertices[i].m_TV=.5f; }
    if (context.mode==1 || context.mode==2 || context.mode==3 || context.mode==7) {
        require(draw->SetTexture("color.dtx")==LT_OK,"canvas texture selection failed"); float u=0,v=0; require(draw->GetTexelSize(u,v)==LT_OK && u==1 && v==1,"canvas GPU texel size differs");
        for (auto& vertex : vertices) vertex.m_Color.Init(context.mode==2 ? 64 : 255,context.mode==2 ? 64 : 255,context.mode==2 ? 64 : 255,context.mode==3 ? 128 : 255);
    }
    if (context.color_op) require(draw->SetState(LTRSTATE_COLOROP,context.color_op)==LT_OK,"canvas color state failed");
    if (context.alpha_op) require(draw->SetState(LTRSTATE_ALPHAOP,context.alpha_op)==LT_OK,"canvas alpha state failed");
    if (context.mode==3) require(draw->SetState(LTRSTATE_ALPHABLENDENABLE,1)==LT_OK,"canvas blending state failed");
    if (context.mode==4) require(draw->SetState(LTRSTATE_ZREADENABLE,0)==LT_OK && draw->SetState(LTRSTATE_ZWRITEENABLE,0)==LT_OK,"canvas depth state failed");
    if (context.mode==7) {
        for (unsigned i : {1u,2u}) vertices[i].m_Vec.x=0;
        require(draw->DrawPrimitive(vertices.data(),4,0)==LT_OK,"canvas first texture version failed");
        HTEXTURE handle=nullptr; char name[]="color.dtx"; uint8* pixels=nullptr; long pitch=0;
        require(context.textures->GetTextureHandle(name,handle,0)==LT_OK && context.textures->LockTexture(handle,nullptr,TLOCK_TEXTURE,pixels,pitch)==LT_OK,"canvas edit selection failed");
        *reinterpret_cast<uint32*>(pixels)=0x00204080; require(context.textures->UnlockTexture(handle)==LT_OK,"canvas edit publication failed");
        for (unsigned i=0;i<4;++i) vertices[i].m_Vec.x=i==0 || i==3 ? 0 : 8;
        require(draw->DrawPrimitive(vertices.data(),4,0)==LT_OK && context.textures->ReleaseTextureHandle(handle)==LT_OK,"canvas second texture version failed"); return;
    }
    require(draw->DrawPrimitive(vertices.data(),4,0)==LT_OK,"canvas primitive recording failed");
    if (context.mode==5) require(context.scene->remove(object)==LT_OK,"canvas self removal failed");
    if (context.mode==6) context.renderer->reset();
    if (context.remove) { require(context.scene->remove(context.remove)==LT_OK,"canvas later model removal failed"); context.remove=nullptr; }
}
void canvas_scene(nolf::platform::Desktop& desktop) {
    Files files; auto scene=std::make_shared<nolf::ClientScene>(files.assets); auto textures=std::make_shared<nolf::ClientTextures>(files.assets);
    auto renderer=std::make_unique<nolf::SceneRenderer>(scene,textures,files.assets); require(scene->viewport(128,128)==LT_OK,"canvas viewport failed");
    ObjectCreateStruct create; create.m_ObjectType=OT_CAMERA; HOBJECT camera=nullptr; require(scene->create(create,camera)==LT_OK,"canvas camera failed");
    create=ObjectCreateStruct{}; create.m_ObjectType=OT_CANVAS; create.m_Flags=FLAG_VISIBLE; create.m_Pos.Init(1000,1000,1000); HOBJECT object=nullptr;
    require(scene->create(create,object)==LT_OK && scene->canvas_radius(object,10000)==LT_OK,"canvas object failed"); CanvasContext context{scene.get(),textures.get(),&renderer};
    require(scene->canvas_function(object,canvas_callback,&context)==LT_OK,"native canvas callback failed");
    auto sample=[](int x=64,int y=64) { uint32 color=0; rd_ReadPixels(x,y,1,1,RD_PIX_BGRA,&color); return color; };
    clear(); auto stats=renderer->render(camera); near(sample(),0xff0000); require(stats.effects.canvases==1 && stats.effects.triangles==2 && !stats.pending_objects && context.calls==1,"canvas callback did not draw world-space primitive");
    context.mode=1; clear(); renderer->render(camera); near(sample(),0x804020);
    context.mode=2; const std::array<unsigned,5> color_values{0x804020,0x404040,0x201008,0xc08060,0x400000};
    for (unsigned op=1;op<=5;++op) { context.color_op=op; clear(); renderer->render(camera); near(sample(),color_values[op-1]); }
    context.mode=3; context.color_op=LTOP_SELECTTEXTURE; const std::array<unsigned,5> alpha_values{0x0000ff,0x402090,0x0000ff,0x402090,0x0000ff};
    for (unsigned op=1;op<=5;++op) { context.alpha_op=op; clear(); renderer->render(camera); near(sample(),alpha_values[op-1]); }
    context={scene.get(),textures.get(),&renderer}; require(scene->load_world("world.dat")==LT_OK,"canvas depth world failed");
    clear(); renderer->render(camera); near(sample(),0x000020); context.mode=4; clear(); renderer->render(camera); near(sample(),0xff0000);
    require(scene->unload_world()==LT_OK && scene->position(object,{0,0,0},true)==LT_OK,"canvas depth cleanup failed");
    const auto model_object=model(*scene); auto* instance=static_cast<ModelInstance*>(model_object); auto& triangle=instance->m_ModelOwner->m_Pieces[0]->m_Array10[0]; std::swap(triangle.indices[0],triangle.indices[1]);
    clear(); renderer->render(camera); near(pixel(),0xff0000); require(scene->client_flags(object,CF_SOLIDCANVAS)==LT_OK,"solid canvas flag failed");
    clear(); renderer->render(camera); near(pixel(),0x804020); context.remove=model_object; clear(); stats=renderer->render(camera); near(pixel(),0xff0000); require(stats.models.removed==1,"solid canvas removal left stale model queue entry");
    require(scene->client_flags(object,0)==LT_OK,"canvas flags reset failed"); context.mode=7; context.z=10;
    clear(); renderer->render(camera); near(sample(40),0x804020); near(sample(88),0x204080);
    object->m_Flags|=FLAG_REALLYCLOSE; context.mode=0; context.z=3; require(scene->position(camera,{100000,0,0},true)==LT_OK,"close canvas camera failed");
    clear(); stats=renderer->render(camera); near(sample(),0xff0000); require(stats.effects.canvases==1,"really-close canvas failed camera-space near depth"); object->m_Flags&=~FLAG_REALLYCLOSE; context.z=20;
    require(scene->position(camera,{0,0,0},true)==LT_OK,"canvas camera reset failed");
    auto integer=reinterpret_cast<void (*)(unsigned,int*)>(desktop.gl_symbol("glGetIntegerv")); require(integer,"canvas state inspection unavailable"); rd_MatrixMode(RD_TEXTURE); rd_Disable(RD_DEPTH_TEST); clear(); renderer->render(camera); int mode=0; integer(0x0ba0,&mode); require(mode==0x1702,"canvas adapter did not restore caller matrix state");
    context.mode=5; clear(); stats=renderer->render(camera); near(sample(),0x0000ff); require(stats.effects.removed==1 && !scene->contains(object),"self-removed canvas submitted recorded GPU commands");
    require(scene->create(create,object)==LT_OK && scene->canvas_radius(object,10000)==LT_OK && scene->canvas_function(object,canvas_callback,&context)==LT_OK,"retiring canvas setup failed"); context.mode=6;
    clear(); stats=renderer->render(camera); near(sample(),0x0000ff); require(!renderer && stats.models.retired,"canvas callback used retired renderer or submitted stale commands");
    std::cout<<"Native canvas pixels, five color/alpha operations, world-space geometry, depth, solid ordering, texture versions and callback retirement passed\n";
}
void checks(nolf::platform::Desktop& desktop) {
    Files files; auto scene=std::make_shared<nolf::ClientScene>(files.assets); auto textures=std::make_shared<nolf::ClientTextures>(files.assets);
    auto renderer=std::make_unique<nolf::SceneRenderer>(scene,textures,files.assets);
    require(scene->viewport(128,128)==LT_OK,"scene drawable resize"); ObjectCreateStruct input; input.m_ObjectType=OT_CAMERA; HOBJECT camera=nullptr;
    require(scene->create(input,camera)==LT_OK,"native render camera creation");
    auto object=model(*scene); auto* instance=static_cast<ModelInstance*>(object);
    instance->m_ModelOwner->m_f0a8=0;
    // The fixture's opposite winding also exercises the native culling path.
    auto& triangle=instance->m_ModelOwner->m_Pieces[0]->m_Array10[0]; std::swap(triangle.indices[0],triangle.indices[1]);
    std::array<HOBJECT,1> list{object}; clear(); auto report=renderer->render_models(camera,list); near(pixel(),0x804020);
    require(report.models==1 && report.pieces==1 && report.triangles==1,"scene model pass did not dispatch canonical mesh");
    require(scene->position(object,{1000,0,0},true)==LT_OK,"move model outside view failed"); clear(); report=renderer->render_models(camera,list); near(pixel(),0x0000ff);
    require(report.culled==1 && !report.models && scene->position(object,{-1,0,0},true)==LT_OK,"evaluated model bounds did not cull outside geometry");
    nolf::ModelRenderOptions options; options.light_scale.Init(.5f,.25f,1); clear(); renderer->render_models(camera,list,options); near(pixel(),0x401020);
    ObjectCreateStruct replacement; std::strcpy(replacement.m_Filename,"triangle.abc"); std::strcpy(replacement.m_SkinName,"animated.spr");
    require(scene->filenames(object,replacement)==LT_OK,"animated model skin replacement failed"); options={}; options.time=.6;
    clear(); renderer->render_models(camera,list,options); near(pixel(),0x204080);
    std::strcpy(replacement.m_SkinName,"color.dtx"); require(scene->filenames(object,replacement)==LT_OK,"model skin restoration failed");
    std::strcpy(replacement.m_SkinName,"fullbright.dtx"); require(scene->filenames(object,replacement)==LT_OK,"fullbright skin fixture failed");
    clear(); renderer->render_models(camera,list); near(pixel(),0x804020);
    instance->m_Flags2|=FLAG2_CHROMAKEY; clear(); renderer->render_models(camera,list); near(pixel(),0x0000ff); instance->m_Flags2&=~FLAG2_CHROMAKEY;
    std::strcpy(replacement.m_SkinName,"color.dtx"); require(scene->filenames(object,replacement)==LT_OK,"fullbright skin restoration failed");
    auto behind=model(*scene); require(scene->position(behind,{-1,0,10},true)==LT_OK && scene->color(behind,0,1,0,.5f)==LT_OK && scene->color(object,1,0,0,.5f)==LT_OK,"translucent scene setup failed");
    { std::array<HOBJECT,2> unordered{object,behind}; clear(); options={}; report=renderer->render_models(camera,unordered,options); near(pixel(),0x401040); require(report.models==2,"transparent model ordering lost an object"); }
    require(scene->remove(behind)==LT_OK && scene->color(object,1,1,1,1)==LT_OK,"transparent fixture cleanup failed");
    options={}; options.hook=+[](ModelHookData* data,void*) { data->m_ObjectColor->Init(0,255,0); };
    clear(); renderer->render_models(camera,list,options); near(pixel(),0x004000);
    options.hook=+[](ModelHookData* data,void*) { data->m_Flags&=~MHF_USETEXTURE; data->m_ObjectColor->Init(255,0,0); };
    clear(); renderer->render_models(camera,list,options); near(pixel(),0xff0000);
    require(scene->model().SetPieceHideStatus(object,0,LTTRUE)==LT_OK,"SDK hide-piece binding failed"); clear(); report=renderer->render_models(camera,list); near(pixel(),0x0000ff); require(!report.models && !report.triangles,"hidden piece still counted as drawn");
    require(scene->model().SetPieceHideStatus(object,0,LTFALSE)==LT_OK,"SDK unhide-piece binding failed");
    instance->m_Flags|=FLAG_REALLYCLOSE; require(scene->position(camera,{100,200,300},true)==LT_OK,"move real camera");
    clear(); report=renderer->render_models(camera,list); near(pixel(),0x804020); require(report.models==1,"camera-relative model did not render");
    instance->m_Flags&=~FLAG_REALLYCLOSE; require(scene->position(camera,{0,0,0},true)==LT_OK,"reset camera");
    auto integer=reinterpret_cast<void (*)(unsigned,int*)>(desktop.gl_symbol("glGetIntegerv")); require(integer,"GL state inspection unavailable");
    rd_MatrixMode(RD_TEXTURE); rd_Disable(RD_DEPTH_TEST); clear(); renderer->render_models(camera,list); int mode=0; integer(0x0ba0,&mode); require(mode==0x1702,"scene pass failed to restore caller matrix mode");
    { std::array<HOBJECT,1> wrong{camera}; rejects([&] { renderer->render_models(camera,wrong); }); }
    auto thread=std::async(std::launch::async,[&] { renderer->render_models(camera,list); });
    bool rejected=false; try { thread.get(); } catch (const std::logic_error&) { rejected=true; } require(rejected,"foreign thread entered scene renderer");
    options={}; options.hook_user=scene.get(); options.hook=+[](ModelHookData* data,void* user) { static_cast<nolf::ClientScene*>(user)->remove(data->m_hObject); };
    clear(); report=renderer->render_models(camera,list,options); near(pixel(),0x0000ff); require(report.removed==1 && !scene->contains(object),"model hook removal left stale draw data");
    object=model(*scene); list[0]=object;
    require(scene->node_control(object,+[](HOBJECT object,HMODELNODE,LTMatrix*,void* scene) { static_cast<nolf::ClientScene*>(scene)->remove(object); },scene.get())==LT_OK,"scene node callback setup");
    clear(); report=renderer->render_models(camera,list); near(pixel(),0x0000ff); require(report.removed==1,"node callback removal left stale model pose");
    object=model(*scene); list[0]=object;
    options={}; options.hook_user=&renderer; options.hook=+[](ModelHookData*,void* owner) { static_cast<std::unique_ptr<nolf::SceneRenderer>*>(owner)->reset(); };
    report=renderer->render_models(camera,list,options); require(!renderer && report.retired && !report.models,"renderer retirement during game hook used dead host");
    require(scene->remove(object)==LT_OK,"final fixture removal failed");
    std::cout<<"Native scene camera/models/skins/hooks/hidden pieces/view model rendering and callback retirement passed\n";
}
void world_scene() {
    Files files; auto scene=std::make_shared<nolf::ClientScene>(files.assets); auto textures=std::make_shared<nolf::ClientTextures>(files.assets);
    nolf::SceneRenderer renderer(scene,textures,files.assets); require(scene->viewport(128,128)==LT_OK && scene->load_world("world.dat")==LT_OK,"scene render world fixture failed");
    ObjectCreateStruct create; create.m_ObjectType=OT_CAMERA; HOBJECT camera=nullptr; require(scene->create(create,camera)==LT_OK,"world scene camera failed");
    const auto object=model(*scene); auto* instance=static_cast<ModelInstance*>(object);
    auto& triangle=instance->m_ModelOwner->m_Pieces[0]->m_Array10[0]; std::swap(triangle.indices[0],triangle.indices[1]);
    ObjectCreateStruct replacement; std::strcpy(replacement.m_Filename,"triangle.abc"); std::strcpy(replacement.m_SkinName,"other.dtx");
    require(scene->filenames(object,replacement)==LT_OK && scene->position(object,{-1,0,-1},true)==LT_OK,"world model placement/skin failed");
    HTEXTURE wall=nullptr,skin=nullptr; char wall_name[]="color.dtx",skin_name[]="other.dtx";
    require(textures->GetTextureHandle(wall_name,wall,0)==LT_OK && textures->GetTextureHandle(skin_name,skin,0)==LT_OK,"scene texture visibility handles failed");
    clear(); auto stats=renderer.render(camera); require(stats.world.polygons==1 && stats.models.models==1 && !stats.pending_objects,"composed scene missed world or model pass"); near(pixel(),0x204080);
    require(textures->WasTextureDrawnLastFrame(wall)==LT_YES && textures->WasTextureDrawnLastFrame(skin)==LT_YES,"model completion lost world texture visibility");
    auto world=std::const_pointer_cast<CServerWorld>(scene->world_data()); world->m_Shade.Setup(world->m_vMin,world->m_vMax,250,{255,0,0}); instance->m_Flags&=~FLAG_NOLIGHT;
    LTVector point{0,0,0},shade{0,0,0}; require(scene->common().GetPointShade(&point,&shade)==LT_OK && shade.x==255 && shade.y==0 && shade.z==0,"SDK point shade did not expose RGB channels");
    clear(); renderer.render(camera); near(pixel(),0x200000);
    instance->m_Flags|=FLAG_NOLIGHT; require(scene->position(object,{-1,0,5},true)==LT_OK,"place model behind world wall failed");
    clear(); renderer.render(camera); near(pixel(),0x000020);
    { std::array<HOBJECT,2> listed{object,camera}; clear(); const auto explicit_objects=renderer.render_objects(camera,listed);
      require(!explicit_objects.world.polygons && explicit_objects.models.models==1 && !explicit_objects.pending_objects,"explicit object list rendered an implicit world or rejected its camera entry"); near(pixel(),0x204080); }
    instance->m_Flags|=FLAG_REALLYCLOSE; require(scene->position(camera,{100000,0,0},true)==LT_OK && scene->position(object,{-1,0,0},true)==LT_OK,"view model world/PVS fixture failed");
    clear(); stats=renderer.render(camera); near(pixel(),0x204080); require(stats.models.models==1,"camera-relative view model was rejected by world PVS/distance");
    require(scene->unload_world()==LT_OK && scene->position(camera,{0,0,0},true)==LT_OK,"composed scene unload failed");
    clear(); stats=renderer.render(camera); require(!stats.world.polygons && stats.models.models==1,"unloaded world renderer was reused"); near(pixel(),0x204080);
    require(textures->WasTextureDrawnLastFrame(wall)==LT_NO && textures->WasTextureDrawnLastFrame(skin)==LT_YES,"unload retained old world texture visibility");
    require(textures->ReleaseTextureHandle(wall)==LT_OK && textures->ReleaseTextureHandle(skin)==LT_OK,"scene render texture handle release failed");
    std::cout<<"Composed native world/model depth, RGB lighting, shared texture visibility and view-model PVS bypass passed\n";
}
// A model between the camera and the world's wall, a light off to its left:
// with shadow maps on, the wall right of the model darkens where its shadow
// falls, and nothing else changes.
void shadow_scene() {
    if (!nolf::ShadowMaps::available()) { std::cout<<"Shadow maps: the device has no programmable extension; skipped\n"; return; }
    Files files; auto scene=std::make_shared<nolf::ClientScene>(files.assets); auto textures=std::make_shared<nolf::ClientTextures>(files.assets);
    nolf::SceneRenderer renderer(scene,textures,files.assets); require(scene->viewport(128,128)==LT_OK && scene->load_world("world.dat")==LT_OK,"shadow world fixture failed");
    ObjectCreateStruct create; create.m_ObjectType=OT_CAMERA; HOBJECT camera=nullptr; require(scene->create(create,camera)==LT_OK,"shadow camera failed");
    const auto object=model(*scene); auto* instance=static_cast<ModelInstance*>(object);
    auto& triangle=instance->m_ModelOwner->m_Pieces[0]->m_Array10[0]; std::swap(triangle.indices[0],triangle.indices[1]);
    require(scene->position(object,{-1,0,-1},true)==LT_OK,"shadow caster placement failed");
    create=ObjectCreateStruct{}; create.m_ObjectType=OT_LIGHT; create.m_Flags=FLAG_VISIBLE|FLAG_ONLYLIGHTOBJECTS; create.m_Pos.Init(-6,.3f,8);
    HOBJECT light=nullptr; require(scene->create(create,light)==LT_OK,"shadow light failed");
    light->m_LightRadius=100; light->m_ColorR=light->m_ColorG=light->m_ColorB=255;
    auto brightness=[](int x0,int x1) { double sum=0; for (int y=20;y<108;++y) for (int x=x0;x<x1;++x) { uint32 c=0; rd_ReadPixels(x,y,1,1,RD_PIX_BGRA,&c); sum+=(c&255)+((c>>8)&255)+((c>>16)&255); } return sum; };
    using Mode=nolf::ModelRenderOptions::ShadowMapMode;
    const auto frame=[&](Mode mode,bool caster) {
        nolf::SceneRenderOptions options; options.models.shadow_maps=mode; options.models.shadows.max=0;
        if (caster) instance->m_Flags|=FLAG_SHADOW; else instance->m_Flags&=~FLAG_SHADOW;
        clear(); return renderer.render(camera,options);
    };
    frame(Mode::Off,false); const double left=brightness(0,56),right=brightness(72,128);
    // Enhanced: every solid model casts, as deep as its light is bright.
    instance->m_Flags&=~FLAG_NOLIGHT;
    frame(Mode::Off,false); const double lit_left=brightness(0,56),lit_right=brightness(72,128);
    frame(Mode::Enhanced,false);
    require(std::abs(brightness(0,56)-lit_left)<lit_left*.02,"a shadow darkened the side away from it");
    require(brightness(72,128)<lit_right*.99,"an enhanced shadow map did not darken the wall behind its caster");
    // The original's projected shadows: only a model asking for one casts it.
    instance->m_Flags|=FLAG_NOLIGHT;
    frame(Mode::Projected,false); require(std::abs(brightness(72,128)-right)<right*.01,"a model without a shadow request cast a projected shadow");
    const auto model_data=instance->m_ModelOwner; const auto enabled=model_data->m_Unknown0d4; const_cast<Model&>(*model_data).m_Unknown0d4=1;
    frame(Mode::Projected,true);
    require(std::abs(brightness(0,56)-left)<left*.02 && brightness(72,128)<right*.99,"the projected shadow did not fall on the wall behind its caster");
    const_cast<Model&>(*model_data).m_Unknown0d4=enabled; instance->m_Flags&=~FLAG_SHADOW;
    require(scene->remove(light)==LT_OK && scene->unload_world()==LT_OK,"shadow scene cleanup failed");
    // Enhanced lighting's normals: unit length and turned the way the bind
    // pose's face, whichever way the triangle is wound.
    const auto pose=model_EvaluatePose(*instance); require(pose.has_value(),"normal fixture pose failed");
    const auto* mesh=instance->m_ModelOwner->m_Pieces[0]->GetPieceData(0); const auto skinned=model_SkinVertices(*pose,*mesh);
    const auto normals=nolf::model_SmoothNormals(*mesh,skinned,pose->object); require(normals.size()==skinned.size(),"normals do not match vertices");
    for (std::size_t i=0;i<normals.size();++i) require(std::abs(normals[i].Mag()-1)<1e-3f && normals[i].Dot(mesh->vertices()[i].normal)>0,"smoothed normal is not a unit vector facing the bind pose's way");
    std::cout<<"Projected and enhanced shadow maps darken the receiving wall behind their caster and nothing else; smoothed normals face the bind pose's way\n";
}
void sprite_scene() {
    Files files; auto scene=std::make_shared<nolf::ClientScene>(files.assets); auto textures=std::make_shared<nolf::ClientTextures>(files.assets);
    nolf::SceneRenderer renderer(scene,textures,files.assets); require(scene->viewport(128,128)==LT_OK,"sprite viewport failed");
    ObjectCreateStruct create; create.m_ObjectType=OT_CAMERA; HOBJECT camera=nullptr; require(scene->create(create,camera)==LT_OK,"sprite camera failed");
    create=ObjectCreateStruct{}; create.m_ObjectType=OT_SPRITE; create.m_Pos.Init(0,0,20); create.m_Scale.Init(4,4,4); create.m_Flags=FLAG_VISIBLE|FLAG_NOLIGHT; std::strcpy(create.m_Filename,"animated.spr");
    HOBJECT object=nullptr; require(scene->create(create,object)==LT_OK,"sprite object failed"); auto* sprite=static_cast<CObjSprite*>(object);
    auto sample=[](unsigned x=64) { uint32 color=0; rd_ReadPixels(x,64,1,1,RD_PIX_BGRA,&color); return color; };
    clear(); auto stats=renderer.render(camera); near(sample(),0x804020); require(stats.sprites.sprites==1 && stats.sprites.triangles==2 && !stats.pending_objects,"scene sprite pass missed canonical billboard");
    require(scene->update(.5f)==LT_OK,"sprite animation advance failed"); clear(); renderer.render(camera); near(sample(),0x204080);
    ILTSpriteControl* control=nullptr; require(scene->sprite_control(object,control)==LT_OK && control->SetCurPos(0,0)==LT_OK && control->SetFlags(0)==LT_OK,"sprite SDK seek/pause failed");
    require(scene->color(object,1,1,1,.5f)==LT_OK,"sprite alpha setup failed"); clear(); renderer.render(camera); near(sample(),0x402090);
    const auto translucent=model(*scene); auto* triangle_model=static_cast<ModelInstance*>(translucent);
    auto& triangle=triangle_model->m_ModelOwner->m_Pieces[0]->m_Array10[0]; std::swap(triangle.indices[0],triangle.indices[1]);
    require(scene->color(translucent,0,1,0,.5f)==LT_OK,"mixed transparent model color failed");
    clear(); stats=renderer.render(camera); near(pixel(),0x203048); require(stats.models.models==1 && stats.sprites.sprites==1,"mixed transparent model/sprite ordering dropped an object");
    require(scene->remove(translucent)==LT_OK,"mixed transparency fixture removal failed");
    sprite->m_Flags2=FLAG2_ADDITIVE; clear(); renderer.render(camera); near(sample(),0x8040ff);
    sprite->m_Flags2=FLAG2_MULTIPLY; clear(); renderer.render(camera); near(sample(),0x000020); sprite->m_Flags2=0;
    require(scene->color(object,1,1,1,1)==LT_OK,"sprite alpha restore failed");
    ObjectCreateStruct replacement; std::strcpy(replacement.m_Filename,"keyed.spr"); require(scene->filenames(object,replacement)==LT_OK,"keyed sprite replacement failed");
    clear(); renderer.render(camera); near(sample(),0x0000ff);
    std::strcpy(replacement.m_Filename,"animated.spr"); require(scene->filenames(object,replacement)==LT_OK && scene->load_world("world.dat")==LT_OK,"sprite clipping world failed");
    sprite->m_Flags|=FLAG_ROTATEABLESPRITE; require(scene->position(object,{0,0,9},true)==LT_OK,"place decal in front of surface failed");
    auto world=std::const_pointer_cast<CServerWorld>(scene->world_data()); auto* polygon=world->FindPoly(HPOLY(0));
    require(polygon,"sprite clipper fixture missing");
    for (auto& vertex : std::span(polygon->m_Verts,polygon->m_nVerts)) { vertex.m_pPos->x*=.1f; vertex.m_pPos->y*=.1f; }
    { const auto active=scene->activate(); require(nolf::ClientScene::functions().ClipSprite(object,0)==LT_OK,"SDK decal clipping setup failed"); }
    clear(); renderer.render(camera); near(sample(),0x804020); near(sample(85),0x0000ff);
    { const auto active=scene->activate(); require(nolf::ClientScene::functions().ClipSprite(object,INVALID_HPOLY)==LT_OK,"SDK decal clipping reset failed"); }
    clear(); renderer.render(camera); near(sample(85),0x804020);
    for (auto& vertex : std::span(polygon->m_Verts,polygon->m_nVerts)) { vertex.m_pPos->x*=10; vertex.m_pPos->y*=10; }
    sprite->m_Flags&=~FLAG_ROTATEABLESPRITE; require(scene->position(object,{0,0,20},true)==LT_OK,"place sprite behind world wall failed");
    clear(); renderer.render(camera); near(sample(),0x000020);
    sprite->m_Flags|=FLAG_SPRITE_NOZ; clear(); renderer.render(camera); near(sample(),0x804020); sprite->m_Flags&=~FLAG_SPRITE_NOZ;
    sprite->m_Flags|=FLAG_SPRITEBIAS; clear(); renderer.render(camera); near(sample(),0x804020); sprite->m_Flags&=~FLAG_SPRITEBIAS;
    { const auto active=scene->activate(); require(nolf::ClientScene::functions().ClipSprite(object,0)==LT_OK,"persistent sprite clip setup failed"); }
    require(scene->unload_world()==LT_OK && sprite->m_ClipPoly==INVALID_HPOLY,"persistent sprite retained a polygon from the unloaded world");
    sprite->m_Flags|=FLAG_REALLYCLOSE; require(scene->position(camera,{100000,0,0},true)==LT_OK && scene->position(object,{0,0,1},true)==LT_OK,"camera-relative sprite setup failed");
    clear(); stats=renderer.render(camera); near(sample(),0x804020); require(stats.sprites.sprites==1,"camera-relative sprite failed PVS bypass or near-depth rendering");
    std::cout<<"Native sprite SDK animation, RGB keying, blends, polygon clipping, depth/no-Z/bias and camera-relative rendering passed\n";
}
void brush_scene() {
    Files files; auto scene=std::make_shared<nolf::ClientScene>(files.assets); auto textures=std::make_shared<nolf::ClientTextures>(files.assets);
    nolf::SceneRenderer renderer(scene,textures,files.assets); require(scene->viewport(128,128)==LT_OK && scene->load_world("world.dat")==LT_OK,"brush scene world failed");
    ObjectCreateStruct create; create.m_ObjectType=OT_CAMERA; HOBJECT camera=nullptr; require(scene->create(create,camera)==LT_OK,"brush camera failed");
    create=ObjectCreateStruct{}; create.m_ObjectType=OT_WORLDMODEL; create.m_Flags=FLAG_VISIBLE; std::strcpy(create.m_Filename,"Main"); HOBJECT brush=nullptr;
    require(scene->create(create,brush)==LT_OK && scene->color(brush,1,1,0,1)==LT_OK,"canonical brush creation/color failed");
    auto sample=[] { uint32 color=0; rd_ReadPixels(64,64,1,1,RD_PIX_BGRA,&color); return color; };
    clear(); auto stats=renderer.render(camera); require(stats.world.polygons==1 && stats.brushes.objects==1 && stats.brushes.lightmapped==1 && !stats.pending_objects,"composed scene missed brush pass"); near(sample(),0x000000);
    require(scene->position(brush,{0,0,20},true)==LT_OK,"SDK brush movement failed"); clear(); renderer.render(camera); near(sample(),0x000020);
    nolf::SceneRenderOptions options; options.draw_world=false; clear(); stats=renderer.render(camera,options); near(sample(),0x000000); require(!stats.world.polygons && stats.brushes.objects==1,"brush-only render reused static world pass");
    require(scene->position(brush,{1000,0,20},true)==LT_OK,"move brush outside frustum failed"); clear(); stats=renderer.render(camera,options); near(sample(),0x0000ff); require(!stats.brushes.objects && stats.brushes.culled,"brush culling used collision bounds");
    require(scene->position(brush,{0,0,9},true)==LT_OK && scene->color(brush,1,1,1,.5f)==LT_OK,"translucent brush setup failed");
    clear(); stats=renderer.render(camera); near(sample(),0x202014); require(stats.brushes.objects==1 && !stats.brushes.lightmapped,"translucent brush failed scene ordering or vertex lighting");
    require(scene->unload_world()==LT_OK && !scene->contains(brush),"world unload retained brush object"); clear(); stats=renderer.render(camera); require(!stats.brushes.objects && !stats.world.polygons,"retired brush geometry drew after unload");
    std::cout<<"Composed brush transforms, tint, static depth, transparency, frustum culling and unload passed\n";
}
void sky_scene() {
    Files files; auto scene=std::make_shared<nolf::ClientScene>(files.assets); auto textures=std::make_shared<nolf::ClientTextures>(files.assets);
    nolf::SceneRenderer renderer(scene,textures,files.assets); require(scene->viewport(128,128)==LT_OK && scene->load_world("world.dat")==LT_OK,"sky scene world failed");
    ObjectCreateStruct create; create.m_ObjectType=OT_CAMERA; HOBJECT camera=nullptr; require(scene->create(create,camera)==LT_OK,"sky camera failed");
    create=ObjectCreateStruct{}; create.m_ObjectType=OT_WORLDMODEL; create.m_Flags=FLAG_VISIBLE; create.m_Flags2=FLAG2_SKYOBJECT; std::strcpy(create.m_Filename,"Main"); HOBJECT object=nullptr;
    require(scene->create(create,object)==LT_OK && scene->server_id(object,42)==LT_OK && scene->color(object,1,1,1,.1f)==LT_OK,"sky brush creation failed");
    nolf::SkyInfo sky; sky.objects[0]=42; require(scene->sky(sky)==LT_OK,"sky list publication failed");
    auto world=std::const_pointer_cast<CServerWorld>(scene->world_data()); auto& surface=*world->FindPoly(HPOLY(0))->m_pSurface;
    surface.m_nFlags|=16;
    auto sample=[] { uint32 color=0; rd_ReadPixels(64,64,1,1,RD_PIX_BGRA,&color); return color; };
    clear(); auto stats=renderer.render(camera); near(sample(),0x404008); require(stats.world.sky==1 && stats.sky.objects==1 && !stats.pending_objects,"sky aperture did not render ordered sky brush");
    surface.m_nFlags&=~16u; clear(); stats=renderer.render(camera); near(sample(),0x000020); require(!stats.sky.objects,"sky rendered without visible apertures");
    // Partial geometry covers the center while an aperture in the same PVS
    // still selects the sky. Background depth must preserve that foreground.
    surface.m_nFlags|=16; require(scene->color(object,1,0,0,1)==LT_OK,"sky tint setup failed");
    const auto foreground=model(*scene); auto* model_object=static_cast<ModelInstance*>(foreground); auto& triangle=model_object->m_ModelOwner->m_Pieces[0]->m_Array10[0]; std::swap(triangle.indices[0],triangle.indices[1]);
    clear(); renderer.render(camera); near(pixel(),0x804020); near(sample(),0x400000);
    require(scene->remove(foreground)==LT_OK,"sky foreground removal failed");
    const auto moon=model(*scene); auto* moon_model=static_cast<ModelInstance*>(moon); auto& moon_triangle=moon_model->m_ModelOwner->m_Pieces[0]->m_Array10[0]; moon_triangle.indices[0]=1; moon_triangle.indices[1]=0;
    moon->m_Flags2|=FLAG2_SKYOBJECT; require(scene->server_id(moon,43)==LT_OK,"sky model ID failed"); sky.objects[1]=43; require(scene->sky(sky)==LT_OK,"sky model layer publication failed");
    clear(); stats=renderer.render(camera); near(pixel(),0x804020); require(stats.sky.objects==2 && !stats.models.models && !stats.pending_objects,"sky model did not reuse canonical model pass or leaked into world pass");
    std::swap(sky.objects[0],sky.objects[1]); require(scene->sky(sky)==LT_OK,"sky slot reorder failed"); clear(); renderer.render(camera); near(pixel(),0x400000);
    std::swap(sky.objects[0],sky.objects[1]); sky.objects[2]=43; require(scene->sky(sky)==LT_OK,"repeated sky slot publication failed"); clear(); stats=renderer.render(camera); require(stats.sky.objects==3,"repeated sky slots were deduplicated");
    create=ObjectCreateStruct{}; create.m_ObjectType=OT_SPRITE; create.m_Pos.Init(0,0,20); create.m_Scale.Init(4,4,4); create.m_Flags=FLAG_VISIBLE|FLAG_NOLIGHT|FLAG_SPRITE_NOZ; create.m_Flags2=FLAG2_SKYOBJECT; std::strcpy(create.m_Filename,"animated.spr"); HOBJECT cloud=nullptr;
    require(scene->create(create,cloud)==LT_OK && scene->server_id(cloud,44)==LT_OK && scene->color(cloud,0,1,0,1)==LT_OK,"sky sprite creation failed");
    sky.objects[2]=44; require(scene->sky(sky)==LT_OK,"sky sprite slot publication failed"); clear(); stats=renderer.render(camera); near(sample(),0x004000); require(stats.sky.objects==3 && !stats.sprites.sprites,"sky sprite failed ordered background rendering");
    require(scene->position(camera,{100000,0,0},true)==LT_OK,"sky parallax fixture setup failed");
    clear(); stats=renderer.render(camera); near(sample(),0x004000); require(stats.sky.objects==3,"sky objects used main camera world position instead of their view box");
    nolf::SceneRenderOptions options; options.models.hook_user=scene.get(); options.models.hook=+[](ModelHookData*,void* context) { auto* scene=static_cast<nolf::ClientScene*>(context); const auto object=scene->find_server_object(44); if (object) scene->remove(object); };
    clear(); stats=renderer.render(camera,options); require(stats.sky.removed==1 && !scene->contains(cloud),"sky model callback removal left a stale later sky sprite");
    create=ObjectCreateStruct{}; create.m_ObjectType=OT_PARTICLESYSTEM; create.m_Pos.Init(0,0,20); create.m_Flags=FLAG_VISIBLE; create.m_Flags2=FLAG2_SKYOBJECT; HOBJECT dust=nullptr;
    require(scene->create(create,dust)==LT_OK && scene->server_id(dust,45)==LT_OK && scene->effects().setup(dust,"",0,PS_DUMB,10)==LT_OK,"sky particle setup failed");
    scene->effects().add_particle(dust,{0,0,0},{0,0,0},{0,0,255},1); sky.objects[2]=45; require(scene->sky(sky)==LT_OK,"sky particle layer publication failed");
    clear(); stats=renderer.render(camera); near(sample(),0x0000ff); require(stats.sky.objects==3 && !stats.effects.systems,"sky particles did not use view-box camera and ordered sky queue");
    auto retiring=std::make_unique<nolf::SceneRenderer>(scene,textures,files.assets); options.models.hook_user=&retiring;
    options.models.hook=+[](ModelHookData*,void* owner) { static_cast<std::unique_ptr<nolf::SceneRenderer>*>(owner)->reset(); };
    stats=retiring->render(camera,options); require(!retiring && stats.models.retired,"sky model callback retirement used destroyed renderer state");
    require(scene->unload_world()==LT_OK,"sky scene unload failed"); clear(); stats=renderer.render(camera); near(sample(),0x0000ff); require(!stats.sky.objects,"old sky rendered after level unload");
    std::cout<<"Native sky brush/model/sprite order, repeated slots, callback removal, foreground depth, view-box camera and unload passed\n";
}
void moving_decal_scene() {
    Files files; auto scene=std::make_shared<nolf::ClientScene>(files.assets); auto textures=std::make_shared<nolf::ClientTextures>(files.assets);
    nolf::SceneRenderer renderer(scene,textures,files.assets); require(scene->viewport(128,128)==LT_OK && scene->load_world("move.dat")==LT_OK,"moving decal world failed");
    ObjectCreateStruct create; create.m_ObjectType=OT_CAMERA; HOBJECT camera=nullptr; require(scene->create(create,camera)==LT_OK && scene->position(camera,{100,0,0},true)==LT_OK,"moving decal camera failed");
    create=ObjectCreateStruct{}; create.m_ObjectType=OT_WORLDMODEL; create.m_Flags=FLAG_VISIBLE|FLAG_BOXPHYSICS; std::strcpy(create.m_Filename,"Door"); HOBJECT first=nullptr,second=nullptr;
    require(scene->create(create,first)==LT_OK && scene->create(create,second)==LT_OK && scene->position(first,{100,0,10},true)==LT_OK && scene->position(second,{200,0,10},true)==LT_OK,"moving duplicate brush setup failed");
    require(scene->color(first,0,1,0,1)==LT_OK && scene->color(second,0,1,0,1)==LT_OK,"moving brush tint failed");
    create=ObjectCreateStruct{}; create.m_ObjectType=OT_SPRITE; create.m_Flags=FLAG_VISIBLE|FLAG_NOLIGHT|FLAG_ROTATEABLESPRITE; create.m_Pos.Init(100,0,9); create.m_Scale.Init(4,4,4); std::strcpy(create.m_Filename,"animated.spr"); HOBJECT decal=nullptr;
    require(scene->create(create,decal)==LT_OK,"moving decal sprite failed"); { const auto active=scene->activate(); require(nolf::ClientScene::functions().ClipSprite(decal,HPOLY(1u<<16))==LT_OK,"moving decal HPOLY setup failed"); }
    auto sample=[] { uint32 color=0; rd_ReadPixels(64,64,1,1,RD_PIX_BGRA,&color); return color; };
    clear(); auto stats=renderer.render(camera); near(sample(),0x804020); require(stats.sprites.sprites==1,"translated visual brush clipped decal against source coordinates");
    require(scene->position(camera,{200,0,0},true)==LT_OK && scene->position(decal,{200,0,9},true)==LT_OK,"second brush decal setup failed");
    clear(); stats=renderer.render(camera); near(sample(),0x804020); require(stats.sprites.sprites==1,"shared source geometry chose wrong brush instance for decal");
    const LTRotation turn(0,std::sqrt(.5f),0,std::sqrt(.5f));
    require(scene->rotation(first,turn)==LT_OK && scene->position(camera,{90,0,10},true)==LT_OK && scene->rotation(camera,turn)==LT_OK &&
        scene->position(decal,{99,0,10},true)==LT_OK && scene->rotation(decal,turn)==LT_OK,"rotated moving decal setup failed");
    clear(); stats=renderer.render(camera); near(sample(),0x804020); require(stats.sprites.sprites==1,"rotated visual brush used untransformed clipping plane");
    std::cout<<"Decal clipping follows translated/rotated visual brushes and duplicate geometry with box physics enabled\n";
}
void sprite_corpus(const char* directory,nolf::platform::Desktop& desktop) {
    auto assets=std::make_shared<nolf::Assets>(); assets->mount_game(directory); auto scene=std::make_shared<nolf::ClientScene>(assets); auto textures=std::make_shared<nolf::ClientTextures>(assets);
    nolf::SceneRenderer renderer(scene,textures,assets); require(scene->viewport(128,128)==LT_OK,"sprite corpus viewport failed");
    ObjectCreateStruct create; create.m_ObjectType=OT_CAMERA; HOBJECT camera=nullptr; require(scene->create(create,camera)==LT_OK,"sprite corpus camera failed");
    unsigned sprites=0,frames=0,missing=0; auto error=reinterpret_cast<unsigned (*)()>(desktop.gl_symbol("glGetError")); require(error,"sprite corpus GL inspection unavailable");
    for (const auto& name : assets->names()) if (name.ends_with(".spr")) {
        create=ObjectCreateStruct{}; create.m_ObjectType=OT_SPRITE; create.m_Pos.Init(0,0,1000); create.m_Scale.Init(.1,.1,.1); create.m_Flags=FLAG_VISIBLE|FLAG_NOLIGHT;
        require(name.size()<sizeof(create.m_Filename),"original sprite filename exceeds SDK extent"); std::strcpy(create.m_Filename,name.c_str()); HOBJECT object=nullptr;
        require(scene->create(create,object)==LT_OK,"original native sprite creation failed"); const auto resource=scene->sprite(object); ILTSpriteControl* control=nullptr;
        require(scene->sprite_control(object,control)==LT_OK && control->SetFlags(0)==LT_OK,"original sprite SDK control failed");
        for (uint32 frame=0;frame<resource->animation().frames.size();++frame) {
            require(control->SetCurPos(0,frame)==LT_OK,"original sprite SDK frame selection failed"); clear(); const auto stats=renderer.render(camera);
            if (assets->contains(resource->animation().frames[frame])) { require(stats.sprites.sprites==1 && stats.sprites.triangles==2,"original frame missed native GPU sprite pass"); ++frames; }
            else { require(stats.sprites.missing==1 && !stats.sprites.sprites,"missing authored sprite frame produced geometry"); ++missing; }
            require(!error(),"original native sprite draw left GL error");
        }
        require(scene->remove(object)==LT_OK,"original sprite removal failed"); ++sprites;
    }
    require(sprites && frames,"empty original native sprite GPU corpus");
    std::cout<<"Rendered "<<sprites<<" original sprites / "<<frames<<" available SDK-selected frames / "<<missing<<" authored missing frames\n";
    unsigned systems=0,particle_frames=0,particle_missing=0;
    nolf::ClientSprites resources(assets);
    for (const auto& name : assets->names()) if (name.ends_with(".spr")) {
        const auto resource=resources.load(name); const auto& animation=resource->animation();
        create=ObjectCreateStruct{}; create.m_ObjectType=OT_PARTICLESYSTEM; create.m_Pos.Init(0,0,1000); create.m_Flags=FLAG_VISIBLE; HOBJECT object=nullptr;
        require(scene->create(create,object)==LT_OK && scene->effects().setup(object,name.c_str(),0,PS_DUMB,100)==LT_OK,"original particle material setup failed");
        scene->effects().add_particle(object,{0,0,0},{0,0,0},{255,255,255},1);
        for (uint32 frame=0;frame<animation.frames.size();++frame) {
            const auto snapshot=scene->effects().snapshot_particles(object); require(snapshot.texture==animation.frames[frame],"original particle animation frame differs");
            clear(); const auto stats=renderer.render(camera);
            if (assets->contains(animation.frames[frame])) { require(stats.effects.systems==1 && stats.effects.particles==1,"original particle frame missed native GPU pass"); ++particle_frames; }
            else { require(stats.effects.missing==1 && !stats.effects.systems,"missing authored particle frame produced geometry"); ++particle_missing; }
            require(!error(),"original native particle draw left GL error"); scene->effects().update(0,1000/animation.frames_per_second,{});
        }
        require(scene->remove(object)==LT_OK,"original particle removal failed"); ++systems;
    }
    require(systems==sprites && particle_frames==frames && particle_missing==missing,"particle/sprite material corpus differs");
    std::cout<<"Rendered "<<systems<<" particle systems / "<<particle_frames<<" engine-advanced original material frames / "<<particle_missing<<" authored missing frames\n";
    unsigned grids=0,grid_frames=0,grid_missing=0;
    for (const auto& name : assets->names()) if (name.ends_with(".spr")) {
        const auto resource=resources.load(name); const auto& animation=resource->animation();
        create=ObjectCreateStruct{}; create.m_ObjectType=OT_POLYGRID; create.m_Pos.Init(0,0,1000); create.m_Scale.Init(400,.01f,400); create.m_Rotation.Init(std::sqrt(.5f),0,0,std::sqrt(.5f)); create.m_Flags=FLAG_VISIBLE; HOBJECT object=nullptr;
        require(scene->create(create,object)==LT_OK && scene->effects().setup_grid(object,2,2,false) && scene->effects().grid_texture(object,name.c_str())==LT_OK,"original grid material setup failed");
        char* data=nullptr; uint32 width=0,height=0; PGColor* colors=nullptr; scene->effects().grid_info(object,data,width,height,colors); for (unsigned i=0;i<256;++i) colors[i]={255,255,255,255};
        for (uint32 frame=0;frame<animation.frames.size();++frame) {
            const auto snapshot=scene->effects().snapshot_grid(object); require(snapshot.texture==animation.frames[frame],"original grid animation frame differs");
            clear(); const auto stats=renderer.render(camera);
            if (assets->contains(animation.frames[frame])) { require(stats.effects.polygrids==1 && stats.effects.triangles==2,"original grid material frame missed native GPU pass"); ++grid_frames; }
            else { require(stats.effects.missing==1 && !stats.effects.polygrids,"missing grid material produced geometry"); ++grid_missing; }
            require(!error(),"original native grid draw left GL error"); scene->effects().update(0,1000/animation.frames_per_second,{});
        }
        require(scene->remove(object)==LT_OK,"original grid removal failed"); ++grids;
    }
    require(grids==sprites && grid_frames==frames && grid_missing==missing,"grid/sprite material corpus differs");
    std::cout<<"Rendered "<<grids<<" polygrid SPR materials / "<<grid_frames<<" engine-advanced original frames / "<<grid_missing<<" authored missing frames\n";
}
}
int main(int argc,char** argv) {
    try {
        if (argc>2) throw std::invalid_argument("usage: nolf-scene-renderer-tests [game-data]");
        nolf::platform::DesktopConfig config; config.width=128; config.height=128; config.vsync=false;
        nolf::platform::Desktop desktop(config); require(rd_CreateDevice(&desktop,128,128,true,0,1),"scene device failed");
        struct Device { ~Device() { rd_DestroyDevice(); } } device;
        std::cout<<"desktop="<<desktop.driver()<<" renderer="<<rd_Describe()<<'\n'; checks(desktop); world_scene(); shadow_scene(); sprite_scene(); brush_scene(); sky_scene(); moving_decal_scene(); effect_scene(desktop); canvas_scene(desktop); polygrid_scene(desktop); if (argc==2) sprite_corpus(argv[1],desktop); return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
