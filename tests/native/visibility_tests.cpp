#include "world_visibility.h"
#include "worldbsp.h"
#include "world_loader.h"
#include "world_spatial.h"
#include "ltobject_internal.h"
#include "client_update.h"
#include "container_query.h"
#include "engine/portable/assets.h"
#include "engine/portable/asset_stream.h"
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool test,const char* why) { if (!test) throw std::runtime_error(why); }
template<class F> void rejects(F&& call) {
    bool rejected = false; try { call(); } catch (const std::exception&) { rejected = true; }
    require(rejected,"invalid visibility graph accepted");
}
struct Fixture {
    LTPlane plane;
    CWorldSurface surface;
    CWorldPart polygon{};
    std::array<LTPlane,2> child_planes;
    std::array<CWorldSurface,2> child_surfaces;
    std::array<CWorldPart,2> child_polygons{};
    std::array<CWorldGroupQT1,3> groups;
    std::array<CWorldGroupEntryQT1,3> entries;
    std::array<std::byte,3> bytes{std::byte{6},std::byte{12},std::byte{12}};
    std::array<WorldPortal,1> portals;
    CWorldBsp world;
    Fixture() {
        world.m_Flags4c = 1;
        plane.m_Normal.Init(1,0,0); plane.m_Dist = 0;
        surface.m_pPlane = &plane; polygon.m_pSurface = &surface;
        world.m_n70 = new WorldPolyRef[3]; world.m_n74 = 3; world.m_n9c = world.m_n70;
        for (unsigned i = 0; i < 3; ++i) {
            auto& node = world.m_n70[i]; node.m_pOwner = &polygon; node.m_x = i; node.m_flag = 8;
            node.m_Children[0] = static_cast<WorldPolyRef*>(g_pWorldTreeBackTerm);
            node.m_Children[1] = static_cast<WorldPolyRef*>(g_pWorldTreeFrontTerm);
            entries[i].m_nBytes = 1; entries[i].m_pData = &bytes[i];
        }
        world.m_n70[0].m_Children[0] = world.m_n70 + 1; world.m_n70[0].m_Children[1] = world.m_n70 + 2;
        child_planes[0].Init({1,0,0},-10); child_planes[1].Init({-1,0,0},-10);
        for (unsigned i = 0; i < 2; ++i) {
            child_surfaces[i].m_pPlane = &child_planes[i]; child_polygons[i].m_pSurface = &child_surfaces[i];
            world.m_n70[i+1].m_pOwner = &child_polygons[i];
        }
        groups[1].m_pEntries = entries.data(); groups[1].m_nEntries = 2;
        groups[2].m_pEntries = entries.data() + 2; groups[2].m_nEntries = 1;
        world.m_p88 = groups.data(); world.m_n8c = groups.size();
        world.m_p80 = entries.data(); world.m_n84 = entries.size();
        world.m_p104 = bytes.data(); world.m_n98 = bytes.size(); world.m_nChunks = 1; world.m_pChunks = portals.data();
    }
};
void checks() {
    Fixture f;
    const auto graph = WorldVisibility::Build(f.world);
    require(graph->GroupAt({-5,0,0}) == 1 && graph->GroupAt({5,0,0}) == 2 && graph->GroupAt({0,0,0}) == 2,"original BSP group lookup and plane epsilon");
    std::array<uint16,1> portals{};
    const auto closed = graph->Select({-5,0,0},portals);
    require(closed.groups == std::vector<uint8>({0,1,0}) && !closed.unrestricted,"closed portal PVS bits");
    require(graph->Intersects(closed,{-5,0,0},.5f) && !graph->Intersects(closed,{5,0,0},.5f) && graph->Intersects(closed,{0,0,0},1),"sphere filtering across visible BSP branches");
    portals[0] = 1;
    const auto opened = graph->Select({-5,0,0},portals);
    require(opened.groups == std::vector<uint8>({0,1,1}) && graph->Intersects(opened,{5,0,0},.5f),"open portal merges PVS sets");
    unsigned filters = 0;
    const auto filtered = graph->Select({-5,0,0},portals,[&](uint16 portal) {
        ++filters; require(portal == 0,"typed portal callback index");
        const auto nested = graph->Select({5,0,0},portals);
        require(nested.groups == std::vector<uint8>({0,0,1}),"nested PVS query has independent bit cursor");
        return false;
    });
    require(filters == 1 && filtered.groups == closed.groups,"portal filter and reentrant selection");
    rejects([&] { graph->Select({-5,0,0},{}); });
    f.entries[0].m_nBytes = 0;
    rejects([&] { WorldVisibility::Build(f.world); });
    f.entries[0].m_nBytes = 1; f.world.m_n70[1].m_x = 3;
    rejects([&] { WorldVisibility::Build(f.world); });
    f.world.m_n70[1].m_x = 1; f.world.m_n70[1].m_Children[0] = f.world.m_n70;
    rejects([&] { WorldVisibility::Build(f.world); });
    f.world.m_n70[1].m_Children[0] = static_cast<WorldPolyRef*>(g_pWorldTreeBackTerm);
    f.groups[1].m_nEntries = 0;
    const auto fallback = WorldVisibility::Build(f.world)->Select({-5,0,0},{});
    require(fallback.unrestricted && fallback.groups == std::vector<uint8>({1,1,1}),"authored region without PVS retains original unfiltered traversal");
    f.world.Term();
    require(graph->Select({-5,0,0},portals).groups == opened.groups,"immutable graph retains data independently of source geometry");
    CWorldVisQuery terminated; terminated.position.Init(-5,0,0);
    require(f.world.DoVisQuery(terminated) == LT_NOTINITIALIZED,"terminated geometry rejects new queries");
    WorldSpatialTree tree;
    require(!tree.BindVisibility(&f.world),"terminated geometry cannot be rebound as live visibility");
}
void object_queries() {
    Fixture f; f.world.m_Visibility = WorldVisibility::Build(f.world);
    auto back = std::make_unique<LTObject>(), front = std::make_unique<LTObject>();
    back->SetDims({.1f,.1f,.1f}); front->SetDims({.1f,.1f,.1f});
    back->SetPos({-5,0,0}); front->SetPos({5,0,0});
    require(obj_LinkIntoWorldTree(&f.world,back.get()) && obj_LinkIntoWorldTree(&f.world,front.get()),"native BSP object membership");
    require(back->m_Link6c.m_pData == back.get() && front->m_Link6c.m_pData == front.get(),"BSP membership retains actual object identity");
    CWorldVisQuery query; query.position.Init(-5,0,0);
    std::vector<LTObject*> found;
    query.object = [&](LTObject& object) { found.push_back(&object); };
    require(f.world.DoVisQuery(query) == LT_OK && found == std::vector<LTObject*>{back.get()},"authored PVS filters native BSP memberships");
    f.portals[0].m_Flags = 1; found.clear();
    require(f.world.DoVisQuery(query) == LT_OK && found.size() == 2,"portal changes affect following object query");
    std::array<LTObject*,3> duplicates{back.get(),back.get(),front.get()}; query.candidates = duplicates;
    found.clear(); require(f.world.DoVisQuery(query) == LT_OK && found.size() == 2,"explicit candidates are deduplicated per query");
    CUpdateContext interest;
    interest.MarkVisible(*back); require(interest.IsVisible(*back),"client interest captures native object lifetime");
    unsigned visits = 0;
    query.object = [&](LTObject&) { ++visits; back.reset(); front.reset(); };
    require(f.world.DoVisQuery(query) == LT_OK && visits == 1,"PVS callback can remove current and later objects");
    auto replacement = std::make_unique<LTObject>();
    require(!interest.IsVisible(*replacement),"new object cannot inherit a previous interest generation");
    auto doomed = std::make_unique<Fixture>(); doomed->world.m_Visibility = WorldVisibility::Build(doomed->world);
    CWorldVisQuery destroy; destroy.position.Init(-5,0,0); destroy.region = [&](const CWorldGroupQT1&) { doomed.reset(); };
    auto* raw = &doomed->world;
    require(raw->DoVisQuery(destroy) == LT_FINISHED && !doomed,"region callback can destroy source geometry and owning world");
    doomed = std::make_unique<Fixture>(); doomed->world.m_Visibility = WorldVisibility::Build(doomed->world); doomed->portals[0].m_Flags = 1;
    destroy.region = {}; destroy.portal = [&](uint16) { doomed.reset(); return true; }; raw = &doomed->world;
    require(raw->DoVisQuery(destroy) == LT_FINISHED && !doomed,"portal callback can destroy source before traversal dispatch");
    std::vector<std::unique_ptr<LTObject>> many;
    std::vector<LTObject*> candidates;
    for (unsigned i = 0; i < 6100; ++i) {
        auto object = std::make_unique<LTObject>(); object->SetPos({-5,0,0});
        candidates.push_back(object.get()); many.push_back(std::move(object));
    }
    query.candidates = candidates; visits = 0;
    query.object = [&](LTObject&) { ++visits; };
    require(f.world.DoVisQuery(query) == LT_OK && visits == candidates.size(),"visibility has no legacy 5000-object truncation");
}
void spatial_queries() {
    Fixture f; f.world.m_Visibility = WorldVisibility::Build(f.world);
    LTObject back,front; back.SetPos({-5,0,0}); front.SetPos({5,0,0}); back.SetDims({.1f,.1f,.1f}); front.SetDims({.1f,.1f,.1f});
    auto tree = std::make_unique<WorldSpatialTree>();
    tree->BumpObject(&back,0); tree->BumpObject(&front,0);
    require(tree->BindVisibility(&f.world),"bind native PVS to spatial index");
    CVisQuery query; query.m_Position.Init(-5,0,0); query.m_Radius = 20;
    std::vector<LTObject*> found;
    query.m_Fn1 = [](LTObject* object,void* pointer) { static_cast<std::vector<LTObject*>*>(pointer)->push_back(object); };
    query.m_pUser = &found; tree->FindObjects(&query);
    require(found == std::vector<LTObject*>{&back},"server spatial query applies authored visibility");
    f.portals[0].m_Flags = 1; found.clear(); tree->FindObjects(&query);
    require(found.size() == 2,"server query snapshots current portal flags");
    unsigned portal_calls = 0;
    query.m_PortalFilter = [&](uint16) {
        ++portal_calls; CVisQuery nested = query; nested.m_PortalFilter = {}; nested.m_Position.Init(5,0,0);
        std::vector<LTObject*> nested_found; nested.m_pUser = &nested_found; tree->FindObjects(&nested);
        require(nested_found == std::vector<LTObject*>{&front},"nested spatial PVS query uses its own selection");
        return true;
    };
    found.clear(); tree->FindObjects(&query); require(portal_calls == 1 && found.size() == 2,"nested query preserves outer callbacks");
    query.m_PortalFilter = {}; query.m_pUser = &tree;
    query.m_Fn1 = [](LTObject*,void* pointer) { static_cast<std::unique_ptr<WorldSpatialTree>*>(pointer)->reset(); };
    tree->FindObjects(&query);
    require(!tree,"spatial callback can destroy the index without reading freed state");
    tree = std::make_unique<WorldSpatialTree>(); tree->BumpObject(&back,0); tree->BumpObject(&front,0);
    require(tree->BindVisibility(&f.world),"rebind visibility");
    query.m_PortalFilter = [&](uint16) { tree.reset(); return true; };
    auto* raw = tree.get(); raw->FindObjects(&query);
    require(!tree,"portal callback can destroy spatial owner before dispatch");
    tree = std::make_unique<WorldSpatialTree>(); tree->BumpObject(&back,0); tree->BumpObject(&front,0);
    struct Endpoints { LTVector from{-10,0,0}, to{10,0,0}; };
    auto endpoints = std::make_unique<Endpoints>();
    unsigned segments = 0;
    tree->QuerySegment(endpoints->from,endpoints->to,[&](LTObject*) { endpoints.reset(); ++segments; });
    require(segments == 2,"segment endpoints survive callback destruction of their source");
    unsigned replaced = 0;
    tree->QueryBox({-10,-1,-1},{10,1,1},[&](LTObject*) {
        ++replaced; require(tree->LoadLayout({}),"replace layout in query callback");
    });
    require(replaced == 1,"replacing spatial layout invalidates active traversal");
    require(tree->BindVisibility(&f.world),"bind geometry before termination");
    f.world.Term(); found.clear(); query.m_pUser = &found; query.m_PortalFilter = {};
    query.m_Fn1 = [](LTObject* object,void* pointer) { static_cast<std::vector<LTObject*>*>(pointer)->push_back(object); };
    tree->FindObjects(&query); require(found.empty(),"geometry termination invalidates an existing spatial binding");
}
void container_queries() {
    CWorldBsp world; world.m_Flags4c = 1;
    LTPlane plane; plane.Init({1,0,0},0);
    CWorldSurface surface; surface.m_pPlane = &plane;
    CWorldPart polygon{}; polygon.m_pSurface = &surface;
    world.m_n70 = new WorldPolyRef[1]; world.m_n74 = 1; world.m_n9c = world.m_n70;
    auto& node = world.m_n70[0]; node.m_pOwner = &polygon;
    node.m_Children[0] = static_cast<WorldPolyRef*>(g_pWorldTreeBackTerm);
    node.m_Children[1] = static_cast<WorldPolyRef*>(g_pWorldTreeFrontTerm);
    world.m_vBoxMin.Init(-10,-10,-10); world.m_vBoxMax.Init(10,10,10);
    CObjContainer container; container.m_pWorldBsp = &world; container.SetPos({10,0,0});
    container.m_Transform230.Identity(); container.m_Transform230.m[0][3] = -10;
    require(object_IsPointInContainer(container,{9,0,0}) && !object_IsPointInContainer(container,{11,0,0}),"container containment uses native inverse transform and BSP");
    require(!object_IsPointInContainer(container,{-100,0,0}),"container bounds reject distant point");
    require(!object_IsPointInContainer(container,{std::numeric_limits<float>::quiet_NaN(),0,0}),"container rejects nonfinite position");
    LTObject other; other.m_ObjectType = OT_CONTAINER; other.m_pWorldBsp = &world;
    require(!object_IsPointInContainer(other,{-1,0,0}),"container requires the native container type");
    std::array<HOBJECT,2> output{nullptr,&other};
    PointContainerGather gather{output.data(),1,0,{9,0,0}};
    sm_GatherContainersCB(&container,&gather); sm_GatherContainersCB(&container,&gather);
    require(gather.m_nElements == 1 && output[0] == &container && output[1] == &other,"container gathering respects caller capacity");
    gather.m_pList = nullptr; sm_GatherContainersCB(&container,&gather);
    sm_GatherContainersCB(nullptr,&gather); sm_GatherContainersCB(&container,nullptr);
    container.m_Unknown54 = 1;
    require(!object_IsPointInContainer(container,{9,0,0}),"inactive containers are excluded");
}
void corpus(const std::filesystem::path& data) {
    nolf::Assets assets; assets.mount_game(data);
    std::size_t worlds = 0, models = 0, regions = 0, entries = 0;
    for (const auto& name : assets.names()) {
        if (!name.starts_with("worlds/") || !name.ends_with(".dat")) continue;
        auto* stream = new nolf::AssetStream(assets.locate(name));
        require(stream,"world stream");
        struct Release { ILTStream* stream; ~Release() { stream->Release(); } } release{stream};
        try {
            const auto geometry = w_ReadWorldGeometry(*stream);
            for (const auto& model : geometry->models) {
                const auto& graph = model->m_pWorldBsp->m_Visibility;
                require(bool(graph),"loaded visibility graph");
                regions += graph->GroupCount(); entries += graph->EntryCount(); ++models;
            }
        } catch (const std::exception& e) { throw std::runtime_error(name + ": " + e.what()); }
        ++worlds;
    }
    require(worlds > 0,"missing worlds");
    std::cout << "Visibility corpus: " << worlds << " worlds, " << models << " models, " << regions << " regions, " << entries << " PVS entries\n";
}
}
int main(int argc,char** argv) {
    try { checks(); object_queries(); spatial_queries(); container_queries(); if (argc == 3 && std::string_view(argv[1]) == "--data") corpus(argv[2]);
        else require(argc == 1,"usage: nolf-visibility-tests [--data directory]");
        std::cout << "native visibility tests passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
