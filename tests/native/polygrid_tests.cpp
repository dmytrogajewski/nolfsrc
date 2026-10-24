#include "engine/client/client_scene.h"
#include "engine/client/client_effects.h"
#include "engine/client/polygrid_geometry.h"
#include "engine/server/objtypes.h"
#include "engine/portable/binary.h"
#include "platform/system.h"
#include "texture_fixture.h"
#include <algorithm>
#include <cmath>
#include <future>
#include <iostream>
#include <limits>

namespace {
void require(bool v,const char* why) { if (!v) throw std::runtime_error(why); }
void near(float a,float b) { require(std::fabs(a-b)<1e-5f,"polygrid fit/texture data differs"); }
void checks() {
    const auto root=std::filesystem::temp_directory_path()/("nolf-polygrid-"+std::to_string(nolf::platform::monotonic_nanoseconds()));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code error; std::filesystem::remove_all(path,error); } } cleanup{root};
    nolf::platform::atomic_write(root/"first.dtx",nolf_test::tiny_texture()); nolf::platform::atomic_write(root/"second.dtx",nolf_test::tiny_texture());
    nolf::BinaryWriter sprite; for (uint32 value : {2u,2u,0u,0u,0u}) sprite.integer(value);
    for (const std::string name : {"first.dtx","second.dtx"}) { sprite.integer(uint16(name.size())); sprite.bytes(std::as_bytes(std::span(name.data(),name.size()))); }
    nolf::platform::atomic_write(root/"grid.spr",sprite.data()); auto assets=std::make_shared<nolf::Assets>(); assets->mount_directory(root);
    nolf::ClientScene scene(assets),foreign; const auto active=scene.activate(); const auto& sdk=nolf::ClientScene::functions(); ObjectCreateStruct input; input.m_ObjectType=OT_POLYGRID; HOBJECT grid=nullptr,other=nullptr;
    require(scene.create(input,grid)==LT_OK && foreign.create(input,other)==LT_OK,"polygrid creation failed"); auto& effects=scene.effects();
    char* data=reinterpret_cast<char*>(1); PGColor* colors=nullptr; uint32 width=1,height=1;
    require(sdk.GetPolyGridInfo(grid,&data,&width,&height,&colors)==LT_OK && !data && !width && !height && colors,"unconfigured grid exposed buffers");
    require(sdk.SetupPolyGrid(grid,2,3,LTFALSE) && sdk.GetPolyGridInfo(grid,&data,&width,&height,&colors)==LT_OK && data && width==2 && height==3,"polygrid setup/buffer access failed");
    const auto indices=std::vector<uint16>{3,1,0,2,3,0,5,3,2,4,5,2}; require(effects.snapshot_grid(grid).indices==indices,"full grid triangulation differs from source format");
    for (unsigned i=0;i<6;++i) require(data[i]==0,"new grid heights were not zeroed"); data[0]=char(-128); data[5]=127; colors[0]={10,20,30,40}; colors[255]={100,110,120,130};
    const auto saved=effects.snapshot_grid(grid); require(saved.heights[0]==-128 && saved.heights[5]==127 && saved.colors[255].a==130,"SDK signed heights/float palette not retained");
    auto* old_data=data; require(!sdk.SetupPolyGrid(grid,1,3,LTFALSE) && !sdk.SetupPolyGrid(grid,0xffffffff,0xffffffff,LTFALSE),"invalid grid dimensions accepted"); require(sdk.GetPolyGridInfo(grid,&data,&width,&height,&colors)==LT_OK && data==old_data && width==2 && data[0]==char(-128),"failed setup discarded valid grid state");
    require(sdk.SetupPolyGrid(grid,2,3,LTTRUE) && effects.snapshot_grid(grid).indices==std::vector<uint16>{3,1,0,5,3,2},"half grid retained extra triangles");
    char spr[]="grid.spr",dtx[]="first.dtx",missing[]="missing.dtx";
    require(sdk.SetPolyGridTexture(grid,spr)==LT_OK && sdk.SetPolyGridTexture(grid,dtx)==LT_INVALIDPARAMS,"grid base material did not require sprite");
    require(sdk.SetPolyGridEnvMap(grid,dtx)==LT_OK && sdk.SetPolyGridEnvMap(grid,spr)==LT_INVALIDPARAMS && sdk.SetPolyGridEnvMap(grid,missing)==LT_NOTFOUND,"grid environment did not require available DTX");
    auto snapshot=effects.snapshot_grid(grid); require(snapshot.texture=="first.dtx" && snapshot.environment=="first.dtx","grid material snapshot differs");
    require(scene.update(.5f)==LT_OK && effects.snapshot_grid(grid).texture=="second.dtx","engine did not advance grid sprite");
    require(sdk.SetPolyGridEnvMap(grid,nullptr)==LT_OK && effects.snapshot_grid(grid).environment.empty(),"environment disable failed");
    float xp=9,yp=9,xs=9,ys=9; require(sdk.GetPolyGridTextureInfo(grid,&xp,&yp,&xs,&ys)==LT_OK && xp==0 && yp==0 && xs==1 && ys==1,"grid texture defaults differ");
    require(sdk.SetPolyGridTextureInfo(grid,2,3,4,5)==LT_OK && sdk.GetPolyGridTextureInfo(grid,&xp,&yp,&xs,&ys)==LT_OK && xp==2 && yp==3 && xs==4 && ys==5,"grid panning/scale SDK failed");
    require(sdk.SetPolyGridTextureInfo(grid,std::numeric_limits<float>::quiet_NaN(),0,0,0)==LT_INVALIDPARAMS && effects.grid_texture_info(grid)==std::array<float,4>{2,3,4,5},"invalid grid pan partially committed");
    LTVector min{-10,-127,-30},max{10,127,30},position{9,9,9},scale{9,9,9}; require(sdk.FitPolyGrid(grid,&min,&max,&position,&scale)==LT_OK,"grid fit failed"); near(position.Mag(),0); near(scale.x,10); near(scale.y,1); near(scale.z,20);
    require(sdk.GetPolyGridInfo(other,&data,&width,&height,&colors)==LT_INVALIDPARAMS && !data && !width && !height && !colors,"grid SDK dereferenced another scene");
    require(std::async(std::launch::async,[&] { return sdk.SetupPolyGrid(grid,2,2,LTFALSE); }).get()==LTFALSE,"grid SDK scope crossed thread");
    require(sdk.SetupPolyGrid(grid,2,32500,LTFALSE),"maximum valid grid rejected"); snapshot=effects.snapshot_grid(grid); require(snapshot.heights.size()==65000 && snapshot.indices.size()==194994 && *std::max_element(snapshot.indices.begin(),snapshot.indices.end())==64999,"maximum grid truncated 16-bit indices");
    require(!sdk.SetupPolyGrid(grid,2,32501,LTFALSE),"oversized grid accepted");
    require(scene.remove(grid)==LT_OK && saved.heights[0]==-128 && saved.colors[0].x==10,"owned grid snapshot invalidated on resize/destruction"); require(sdk.GetPolyGridInfo(grid,&data,&width,&height,&colors)==LT_INVALIDPARAMS,"retired grid remained queryable");
}
void geometry() {
    nolf::PolygridSnapshot grid; grid.width=2; grid.height=3; grid.heights.resize(6); grid.indices={3,1,0,2,3,0,5,3,2,4,5,2}; grid.x_pan=2; grid.y_pan=4; grid.x_scale=2; grid.y_scale=5;
    for (auto& color : grid.colors) color={100,80,60,128};
    CObjPolyGrid object; object.m_Scale.Init(10,1,20); object.m_ColorR=object.m_ColorG=object.m_ColorB=0; object.m_ColorA=128;
    auto mesh=nolf::polygrid_mesh(grid,object,{0,10,0},4,8,{.5f,2,1});
    require(mesh.vertices.size()==6 && mesh.indices==grid.indices,"prepared grid topology differs"); near(mesh.vertices[0].position.x,-10); near(mesh.vertices[0].position.z,-30); near(mesh.vertices[5].position.x,10); near(mesh.vertices[5].position.z,30); near(mesh.radius,std::sqrt(1000.f)); near(mesh.center.Mag(),0);
    near(mesh.vertices[0].u,1.25f); near(mesh.vertices[1].u,3.75f); near(mesh.vertices[0].v,.375f); near(mesh.vertices[5].v,1.875f);
    require(mesh.vertices[0].color==std::array<uint8,4>{50,160,60,64},"grid palette/alpha/global scale inherited object RGB");
    near(mesh.vertices[0].environment_u,.5f+5/std::sqrt(1100.f)); near(mesh.vertices[0].environment_v,.5f-5/std::sqrt(1100.f));
    grid.heights[0]=-128; mesh=nolf::polygrid_mesh(grid,object,{0,10,0},0,0,{1,1,1}); near(mesh.vertices[0].position.y,-128);
    object.m_Flags|=FLAG_UNSIGNED; mesh=nolf::polygrid_mesh(grid,object,{0,10,0},0,0,{1,1,1}); near(mesh.vertices[0].position.y,0);
    grid.heights[0]=0; grid.colors[0]={255,0,0,255}; mesh=nolf::polygrid_mesh(grid,object,{0,10,0},0,0,{1,1,1},true); near(mesh.vertices[0].position.y,-128); require(mesh.vertices[0].color==std::array<uint8,4>{255,0,0,128},"unsigned grid palette index or saturation differs");
    object.m_Rotation.Init(0,0,std::sqrt(.5f),std::sqrt(.5f)); object.m_Pos.Init(1,2,3); mesh=nolf::polygrid_mesh(grid,object,{0,10,0},0,0,{1,1,1}); near(mesh.vertices[0].position.x,129); near(mesh.vertices[0].position.y,-8); near(mesh.vertices[0].position.z,-27);
    auto rejects=[&] { bool failed=false; try { nolf::polygrid_mesh(grid,object,{0,0,0},0,0,{1,1,1}); } catch (const std::invalid_argument&) { failed=true; } require(failed,"malformed grid mesh accepted"); };
    grid.indices[0]=6; rejects(); grid.indices[0]=3; grid.colors[0].x=std::numeric_limits<float>::infinity(); rejects();
}
}
int main() { try { checks(); geometry(); std::cout<<"Native polygrid typed storage, exact indices, material animation, palettes, native extents/UV/reflections, bounds, resize rollback and SDK ownership passed\n"; return 0; } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
