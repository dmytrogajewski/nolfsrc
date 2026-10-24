#include "engine/server/world_polygon_geometry.h"
#include "engine/client/client_scene.h"
#include "engine/server/world_state.h"
#include "platform/system.h"
#include "world_render_fixture.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

namespace {
void require(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
void near(double value,double expected) { require(std::abs(value-expected)<.0001,"owned polygon transform or distance differs"); }
template<class F> void rejects(F&& function) { bool failed=false; try { function(); } catch (const std::exception&) { failed=true; } require(failed,"invalid polygon geometry accepted"); }
void geometry() {
    LTPlane plane{{0,0,-1},-10}; CWorldSurface surface; surface.m_pPlane=&plane;
    std::array<LTVector,4> points{{{-1,-1,10},{1,-1,10},{1,1,10},{-1,1,10}}}; std::array<WorldPolyVertex,4> vertices{};
    for (unsigned i=0;i<4;++i) vertices[i].m_pPos=&points[i];
    CWorldPart polygon{}; polygon.m_pSurface=&surface; polygon.m_Verts=polygon.m_pVerts2=vertices.data(); polygon.m_nVerts=4;
    LTMatrix forward,inverse; forward.Identity(); inverse.Identity();
    forward.m[0][0]=2; forward.m[1][1]=3; forward.m[2][2]=4; forward.m[0][3]=100; forward.m[2][3]=-5;
    inverse.m[0][0]=.5; inverse.m[1][1]=1.f/3; inverse.m[2][2]=.25; inverse.m[0][3]=-50; inverse.m[2][3]=1.25;
    auto snapshot=world_PolygonGeometry(polygon,forward,inverse); require(snapshot.points.size()==4,"polygon snapshot lost vertices");
    near(snapshot.points[0].x,98); near(snapshot.points[0].y,-3); near(snapshot.points[0].z,35); near(snapshot.plane.m_Normal.z,-1); near(snapshot.plane.m_Dist,-35);
    near(world_PolygonDistanceSquared(snapshot,{100,0,37}),4); near(world_PolygonDistanceSquared(snapshot,{105,0,37}),13); near(world_PolygonDistanceSquared(snapshot,{105,6,37}),22);
    std::reverse(snapshot.points.begin(),snapshot.points.end()); near(world_PolygonDistanceSquared(snapshot,{100,0,37}),4);
    points[0].Init(999,999,999); near(snapshot.points.back().x,98);
    inverse.m[2][2]=0; rejects([&] { world_PolygonGeometry(polygon,forward,inverse); }); inverse.Identity(); forward.m[0][0]=std::numeric_limits<float>::quiet_NaN(); rejects([&] { world_PolygonGeometry(polygon,forward,inverse); });
    rejects([&] { world_PolygonDistanceSquared(snapshot,{std::numeric_limits<float>::infinity(),0,0}); });
}
void scene() {
    const auto root=std::filesystem::temp_directory_path()/("nolf-polygon-geometry-"+std::to_string(nolf::platform::monotonic_nanoseconds()));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code error; std::filesystem::remove_all(path,error); } } cleanup{root};
    nolf::platform::atomic_write(root/"move.dat",nolf_test::render_world_fixture(true)); auto assets=std::make_shared<nolf::Assets>(); assets->mount_directory(root); nolf::ClientScene scene(assets);
    require(scene.load_world("move.dat")==LT_OK && scene.world_data()->m_Worlds.GetSize()==2,"dynamic brush fixture failed");
    // This renderer fixture normally has a non-solid lightmapped wall.
    { auto geometry=std::const_pointer_cast<CServerWorld>(scene.world_data()); auto* polygon=geometry->FindPoly(HPOLY(0)); polygon->m_pSurface->m_nFlags|=1; std::reverse(polygon->m_Verts,polygon->m_Verts+polygon->m_nVerts); }
    const auto active=scene.activate(); const auto& api=nolf::ClientScene::functions(); ClientIntersectQuery query; ClientIntersectInfo hit;
    query.m_From.Init(0,0,0); query.m_To.Init(0,0,20); query.m_Flags=INTERSECT_HPOLY;
    require(api.IntersectSegment(&query,&hit) && hit.m_hObject==scene.main_world() && hit.m_hPoly==0,"client segment SDK did not hit canonical main world"); near(hit.m_Point.z,10);
    ObjectCreateStruct create; create.m_ObjectType=OT_WORLDMODEL; create.m_Flags=FLAG_VISIBLE|FLAG_BOXPHYSICS; std::strcpy(create.m_Filename,"Door"); HOBJECT first=nullptr,second=nullptr;
    require(scene.create(create,first)==LT_OK && scene.create(create,second)==LT_OK && scene.position(first,{100,0,10},true)==LT_OK && scene.position(second,{200,0,10},true)==LT_OK,"duplicate source brush creation/move failed");
    auto first_geometry=scene.polygon_geometry(HPOLY(1u<<16),{100,0,9}),second_geometry=scene.polygon_geometry(HPOLY(1u<<16),{200,0,9});
    require(first_geometry && second_geometry,"dynamic polygon geometry missing"); near(first_geometry->points[0].x,90); near(second_geometry->points[0].x,190);
    LTRotation rotation(0,std::sqrt(.5f),0,std::sqrt(.5f)); require(scene.rotation(first,rotation)==LT_OK,"rotate source brush failed");
    const auto rotated=scene.polygon_geometry(HPOLY(1u<<16),{99,0,10}); require(bool(rotated),"rotated clipping geometry missing"); near(rotated->plane.m_Normal.x,-1); near(rotated->plane.m_Dist,-100);
    require(!scene.polygon_geometry(INVALID_HPOLY,{0,0,0}),"invalid polygon handle resolved");
    query.m_FilterFn=+[](HOBJECT,void* context) -> LTBOOL { static_cast<nolf::ClientScene*>(context)->unload_world(); return LTTRUE; }; query.m_pUserData=&scene;
    require(!api.IntersectSegment(&query,&hit) && scene.world_name().empty() && hit.m_hPoly==INVALID_HPOLY,"world unload during client ray filter retained a stale hit");
    require(!scene.polygon_geometry(0,{0,0,0}),"unloaded polygon remained queryable"); near(first_geometry->points[0].x,90);
}
}
int main() { try { geometry(); scene(); std::cout<<"Owned polygon transforms, inverse-transpose planes, closest duplicate brush selection and unload lifetime passed\n"; return 0; } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; } }
