#include "engine/server/worldbsp.h"
#include "engine/server/world_loader.h"
#include "engine/server/world_containment.h"
#include "engine/server/world_line_query.h"
#include "engine/portable/assets.h"
#include "engine/portable/asset_stream.h"
#include "engine/portable/memory_stream.h"
#include "engine/portable/wire.h"
#include "engine/server/world_record.h"
#include <array>
#include <cmath>
#include "engine/portable/allocation.h"
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
std::unique_ptr<CWorldBsp> geometry() {
    auto world = std::make_unique<CWorldBsp>();
    world->m_nf8 = 23;
    world->m_nUnknown48 = 0x24;
    world->m_nParts = 2;
    world->m_ppParts = static_cast<CWorldPart**>(dalloc_z(2 * sizeof(CWorldPart*)));
    const auto polygon_size = world_polygon_bytes(2);
    world->m_pfc = dalloc_z(static_cast<uint32>(polygon_size * 2));
    world->m_n100 = static_cast<uint32>(polygon_size * 2);
    world->m_pa8 = dalloc_z(4 * sizeof(LTVector));
    world->m_nac = 4;
    world->m_p68 = dalloc_z(sizeof(LTPlane));
    world->m_n6c = 1;
    require(world->m_ppParts && world->m_pfc && world->m_pa8 && world->m_p68, "allocate native geometry fixture");
    auto* plane = std::construct_at(static_cast<LTPlane*>(world->m_p68));
    plane->m_Normal.Init(0,1,0); plane->m_Dist = 5;
    world->m_pTree = new CWorldSurface[1]; world->m_nSurfaces = 1;
    world->m_pTree[0].m_pPlane = plane;
    for (uint32 i = 0; i < 4; ++i) {
        auto* point = std::construct_at(static_cast<LTVector*>(world->m_pa8) + i);
        point->Init(i & 1 ? 2.f : -2.f, i & 1 ? 3.f : -3.f, i & 1 ? 4.f : -4.f);
    }
    for (uint32 i = 0; i < 2; ++i) {
        auto* part = std::construct_at(reinterpret_cast<CWorldPart*>(static_cast<std::byte*>(world->m_pfc) + i * polygon_size));
        *part = {};
        part->m_nIndex = static_cast<uint16>(i);
        part->m_nVerts = part->m_nVerts2 = 2;
        part->m_Verts = reinterpret_cast<WorldPolyVertex*>(part + 1);
        part->m_pVerts2 = part->m_Verts;
        part->m_pSurface = world->m_pTree;
        for (uint32 j = 0; j < 2; ++j) {
            auto* vertex = std::construct_at(part->m_Verts + j);
            *vertex = {};
            vertex->m_pPos = static_cast<LTVector*>(world->m_pa8) + i * 2 + j;
        }
        world->m_ppParts[i] = part;
    }
    world->m_n70 = new WorldPolyRef[2]; world->m_n74 = 2;
    for (uint32 i = 0; i < 2; ++i) {
        auto& node = world->m_n70[i];
        node.m_pOwner = world->m_ppParts[i];
        node.m_Children[0] = static_cast<WorldPolyRef*>(g_pWorldTreeBackTerm);
        node.m_Children[1] = static_cast<WorldPolyRef*>(g_pWorldTreeFrontTerm);
    }
    world->m_n70[0].m_Children[0] = &world->m_n70[1];
    world->m_n9c = &world->m_n70[0];
    world->m_vBoxMin.Init(-2,-3,-4); world->m_vBoxMax.Init(2,3,4);
    require(world->m_Array54.SetSizeInit2(3, WorldLightmapReference{}), "native lightmap references allocate actual four-byte records");
    return world;
}
void interface_and_lifetime() {
    unsigned destroyed = 0;
    ObserverNode observer;
    ObserverHook hook;
    hook.m_pFn = [](CModelSub14* owner, ObserverNode*, void*, void* state) {
        if (state && *static_cast<uint32*>(state) == 1) ++*static_cast<unsigned*>(owner->m_pOwner);
    };
    observer.m_pHooks = &hook;
    {
        auto storage = geometry();
        storage->m_pTree[0].m_Sub28.m_pOwner = &destroyed;
        storage->m_pTree[0].m_Sub28.m_pHead = &observer;
        std::unique_ptr<IWorldBsp> world(std::move(storage));
        require(world->GetRoot() == world->GetPolyRefs() && world->GetRoot()->GetPlane()->m_Dist == 5,
                "world loader and physics dispatch through one real C++ interface");
        require(world->GetWorldFlags() == 0x24 && world->IsFlagSet() && !world->IsBusy(), "native BSP virtual state queries");
        require(world->GetBoxDiagonalSqr() == 116 && !world->GetPBlock({0,0,0}), "base geometry owns actual bounds and grid dispatch");
        const HPOLY handle = (23u << 16) | 1;
        auto* nodes = world->GetPolyRefs();
        require(world->MakeHPoly(&nodes[1]) == handle && world->FindPoly(handle) == nodes[1].m_pOwner, "polygon handles retain 32-bit world and polygon indices");
        WorldPolyRef foreign;
        CWorldPart outside{}; outside.m_nIndex = 1; foreign.m_pOwner = &outside;
        require(world->MakeHPoly(&foreign) == 0xffffffff && world->MakeHPoly(nullptr) == 0xffffffff && !world->FindPoly(0xffffffff),
                "polygon lookup validates ownership and bounds");
    }
    require(destroyed == 1, "deletion through the world interface destroys owned surfaces exactly once");
}
void terrain_copy_and_teardown() {
    auto world = geometry();
    require(world->m_Array138.SetSize(1), "world owns constructed terrain sections");
    auto& terrain = world->m_Array138[0];
    terrain.m_p7c = world.get();
    require(terrain.m_Array50.SetSizeInit2(2, static_cast<CWorldPart*>(nullptr)) && terrain.m_Array68.SetSize(2), "terrain owns native part and node arrays");
    for (uint32 i = 0; i < 2; ++i) {
        terrain.m_Array50[i] = world->m_ppParts[i];
        terrain.m_Array68[i].m_pOwner = world->m_ppParts[i];
        terrain.m_Array68[i].m_Children[0] = static_cast<WorldPolyRef*>(g_pWorldTreeBackTerm);
        terrain.m_Array68[i].m_Children[1] = static_cast<WorldPolyRef*>(g_pWorldTreeFrontTerm);
    }
    terrain.m_Array68[0].m_Children[0] = &terrain.m_Array68[1];
    terrain.m_n64 = &terrain.m_Array68[0];
    std::strcpy(terrain.m_Name, "NativeTerrain");
    terrain.ComputeRadius();
    IWorldBsp& interface = terrain;
    require(interface.IsBusy() && interface.GetWorldFlags() == 0x24 && interface.GetBoxDiagonalSqr() == 116,
        "terrain dispatches through the same interface with native bounds");
    require(interface.MakeHPoly(interface.GetRoot()) == (23u << 16), "terrain delegates handles to owning world");
    auto& source_head = terrain.m_Array68[0].m_head;
    CWorldItem18Link live(LTLink_Init);
    source_head.AddAfter(&live);
    source_head.m_pNext = source_head.m_pPrev = &live;
    CWorldSimple copy(terrain);
    require(copy.GetRoot() == &copy.m_Array68[0] && copy.GetRoot() != terrain.GetRoot() &&
        copy.GetRoot()->m_Children[0] == &copy.m_Array68[1] && copy.m_Array68[1].m_Children[1] == g_pWorldTreeFrontTerm,
        "terrain copy remaps roots and children while preserving shared leaves");
    require(copy.m_Array68[0].m_head.m_pNext == &copy.m_Array68[0].m_head && source_head.m_pNext == &live,
        "terrain copy has independent object lists and leaves source links intact");
    source_head.m_pNext = source_head.m_pPrev = &source_head;
    CWorldSimple assigned;
    assigned = terrain; assigned = assigned;
    require(assigned.GetRoot() == &assigned.m_Array68[0] && assigned.GetRoot()->m_Children[0] == &assigned.m_Array68[1] &&
        std::strcmp(assigned.m_Name, terrain.m_Name) == 0, "terrain assignment preserves owned graph and metadata");
    auto* retained_root = assigned.GetRoot();
    auto& occupied = assigned.m_Array68[0].m_head;
    CWorldItem18Link occupant(LTLink_Init);
    occupied.AddAfter(&occupant);
    occupied.m_pNext = occupied.m_pPrev = &occupant;
    bool refused = false;
    try { assigned = terrain; } catch (const std::logic_error&) { refused = true; }
    require(refused && assigned.GetRoot() == retained_root && occupied.m_pNext == &occupant, "live object links prevent destructive terrain replacement");
    occupied.m_pNext = occupied.m_pPrev = &occupied;
    auto* source_root = terrain.GetRoot();
    WorldPolyRef foreign;
    terrain.m_n64 = &foreign;
    refused = false;
    try { assigned = terrain; } catch (const std::invalid_argument&) { refused = true; }
    require(refused && assigned.GetRoot() == retained_root, "invalid source graph leaves assigned world intact");
    terrain.m_n64 = source_root;
    assigned.Reset(); copy.Reset();
    require(!copy.GetPolyRefs() && !copy.GetWorldFlags() && !copy.FindPoly(0) && copy.MakeHPoly(nullptr) == 0xffffffff && copy.GetRadiusSqr() == 0,
        "terrain reset leaves a safe empty interface");
    world->Term(); world->Term();
    require(!world->GetRoot() && !world->GetPolyRefs() && !world->m_Array54.m_nElements && !world->m_Array138.m_nElements &&
        !world->GetRadiusSqr() && !world->m_pTree, "explicit world teardown releases owned containers and supports repeated shutdown");
}
void keys_and_bounds() {
    auto world = geometry();
    world->m_p88 = new CWorldGroupQT1[3]; world->m_n8c = 3;
    world->m_p90 = static_cast<CWorldPolyKey*>(dalloc_z(3 * sizeof(CWorldPolyKey))); world->m_n94 = 3;
    require(world->m_p90, "allocate serialized polygon keys");
    world->m_p90[0] = {0,0}; world->m_p90[1] = {0,1}; world->m_p90[2] = {0,1};
    world->m_p88[0].m_pRefs = world->m_p90; world->m_p88[0].m_nRefs = 2;
    world->m_p88[1].m_pRefs = world->m_p90 + 2; world->m_p88[1].m_nRefs = 1;
    std::array<std::byte,12> original;
    std::memcpy(original.data(), world->m_p90, original.size());
    CServerWorldRec record;
    record.m_pWorldBsp = world.get();
    std::array<CServerWorldRec*,1> models{&record};
    require(w_ResolvePolyKeys(*world, models), "resolve indexed world keys into native pointer storage");
    require(world->m_p88[0].m_Polygons.size() == 2 && world->m_p88[0].m_Polygons[0] == world->m_ppParts[0] &&
        world->m_p88[1].m_Polygons[0] == world->m_ppParts[1] && std::memcmp(original.data(), world->m_p90, original.size()) == 0,
        "native pointer resolution leaves every serialized key byte intact");
    auto* retained = world->m_p88[0].m_Polygons.data();
    world->m_p90[2].m_iPoly = 2;
    require(!w_ResolvePolyKeys(*world, models) && world->m_p88[0].m_Polygons.data() == retained &&
        world->m_p88[1].m_Polygons[0] == world->m_ppParts[1], "invalid later key preserves all previously resolved groups");
    world->m_p90[2] = {1,0};
    require(!w_ResolvePolyKeys(*world, models), "out-of-range model index is rejected");
    world->m_p90[2] = {0,1};
    require(w_ResolvePolyKeys(*world, models), "resolution can be repeated using retained file keys");
    wm_ResetGeometry(world.get());
    const float radius = std::sqrt(29.f);
    require(std::fabs(world->m_ppParts[0]->m_fRadius - radius - 0.2f) < 0.00001f &&
        std::fabs(world->m_p88[0].m_fRadius - radius - 5.f) < 0.00001f && world->m_p88[2].m_fRadius == 5,
        "bounding spheres use resolved polygons and handle empty groups");
}
void record_ownership_and_reentrancy() {
    struct State { CWorldBsp* world; CServerWorldRec* record; unsigned destroyed = 0; bool valid = true; };
    CServerWorldRec record;
    auto geometry_owner = geometry();
    State state{geometry_owner.get(), &record};
    ObserverHook hook;
    ObserverNode observer;
    hook.m_pFn = [](CModelSub14* owner, ObserverNode*, void*, void* destroying) {
        if (!destroying || *static_cast<uint32*>(destroying) != 1) return;
        auto& state = *static_cast<State*>(owner->m_pOwner);
        ++state.destroyed;
        state.valid = state.valid && !state.record->m_Flags && !state.record->m_pWorldBsp &&
            state.world->GetRoot() && state.world->GetRoot()->GetPlane()->m_Dist == 5;
        state.world->Term();
    };
    observer.m_pHooks = &hook;
    geometry_owner->m_pTree[0].m_Sub28.m_pHead = &observer;
    geometry_owner->m_pTree[0].m_Sub28.m_pOwner = &state;
    record.m_pWorldBsp = geometry_owner.release();
    record.m_pOriginal = record.m_pCurrent = record.m_pWorldBsp;
    record.m_Flags = 3;
    record.Term(); record.Term();
    require(state.destroyed == 1 && state.valid && !record.m_pCurrent, "owned geometry is released once and callbacks can request teardown safely");
}

using Bytes = std::vector<std::byte>;
template<class T> void append(Bytes& bytes, T value) {
    const auto at = bytes.size(); bytes.resize(at + sizeof(T));
    nolf::wire::store(bytes.data() + at, value);
}
void text(Bytes& bytes, std::string_view value) {
    append(bytes, static_cast<uint16>(value.size()));
    for (char c : value) append(bytes, uint8(c));
}
void vec(Bytes& bytes, LTVector value = {0,0,0}) {
    append(bytes, value.x); append(bytes, value.y); append(bytes, value.z);
}
struct ModelFixture { Bytes bytes; std::size_t counts, child, surface; };
ModelFixture model_fixture() {
    ModelFixture fixture;
    auto& bytes = fixture.bytes;
    for (uint32 i = 0; i < 8; ++i) append(bytes, uint32(0));
    append(bytes, uint32(4)); append(bytes, uint32(0)); text(bytes, "Main");
    fixture.counts = bytes.size();
    for (uint32 count : {3u,1u,1u,0u,1u,0u,0u,0u,0u,1u,0u,0u}) append(bytes, count);
    vec(bytes, {-1,0,-1}); vec(bytes, {1,0,1}); vec(bytes);
    append(bytes, uint32(0)); append(bytes, uint32(0));
    append(bytes, uint8(3)); append(bytes, uint8(0));
    vec(bytes, {0,1,0}); append(bytes, 0.f);
    vec(bytes); vec(bytes, {1,0,0}); vec(bytes, {0,0,1});
    append(bytes, uint16(0)); append(bytes, uint32(0)); append(bytes, uint32(0));
    for (uint32 i = 0; i < 4; ++i) append(bytes, uint8(0));
    append(bytes, uint8(1)); text(bytes, "Solid"); text(bytes, "foo bar"); append(bytes, uint16(0));
    vec(bytes); append(bytes, uint16(16)); append(bytes, uint16(16)); append(bytes, uint16(0));
    fixture.surface = bytes.size(); append(bytes, uint32(0));
    for (uint16 i = 0; i < 3; ++i) {
        append(bytes, i); append(bytes, uint8(10 + i)); append(bytes, uint8(20)); append(bytes, uint8(30));
    }
    append(bytes, uint32(0)); append(bytes, uint16(0xffff));
    fixture.child = bytes.size(); append(bytes, uint32(0xffffffff)); append(bytes, uint32(0xfffffffe));
    for (auto point : {LTVector(-1,0,-1),LTVector(0,0,1),LTVector(1,0,-1)}) { vec(bytes, point); vec(bytes); }
    append(bytes, uint32(0)); append(bytes, uint32(0)); append(bytes, uint32(0));
    vec(bytes, {1,1,1}); vec(bytes);
    append(bytes, uint32(0)); append(bytes, uint32(0));
    return fixture;
}
void loader_checks() {
    const auto fixture = model_fixture();
    const auto original_bytes = g_WorldBytes;
    {
        nolf::MemoryStream stream(fixture.bytes);
        auto world = w_ReadWorldModel(stream, true);
        require(world->m_nParts == 1 && world->GetRoot()->m_nProject == 2 && world->m_ppParts[0]->m_Verts[2].m_r == 12 &&
            world->m_ppParts[0]->m_Verts[0].m_fU == -1 && world->m_ppParts[0]->m_Verts[0].m_fV == -1,
            "bounded native loader resolves typed geometry, colors, projections and texture coordinates");
        require(world->m_SurfaceProperties.size() == 1 && world->m_SurfaceProperties[0].class_name == "Solid" &&
            world->m_SurfaceProperties[0].properties == "foo bar", "surface class properties survive geometry decoding");
        require(g_WorldBytes > original_bytes, "successful geometry publishes live allocation accounting");
    }
    require(g_WorldBytes == original_bytes, "geometry destruction restores allocation accounting");
    for (std::size_t size = 0; size < fixture.bytes.size(); ++size) {
        bool rejected = false;
        try {
            nolf::MemoryStream stream(Bytes(fixture.bytes.begin(), fixture.bytes.begin() + size));
            w_ReadWorldModel(stream, true);
        } catch (const std::exception&) { rejected = true; }
        require(rejected && g_WorldBytes == original_bytes, "truncated model releases staged geometry without corrupting accounting");
    }
    for (auto [offset, value] : {std::pair{fixture.counts, uint32(0xffffffff)},
        std::pair{fixture.surface,uint32(1)}, std::pair{fixture.child,uint32(0)}, std::pair{fixture.child,uint32(1)}}) {
        auto corrupt = fixture.bytes; nolf::wire::store(corrupt.data() + offset, value);
        bool rejected = false;
        try { nolf::MemoryStream stream(std::move(corrupt)); w_ReadWorldModel(stream, true); }
        catch (const std::exception&) { rejected = true; }
        require(rejected && g_WorldBytes == original_bytes, "invalid allocation counts, references and BSP cycles fail without leaked geometry");
    }
}
void deep_containment() {
    LTPlane plane; plane.m_Normal.Init(1,0,0); plane.m_Dist = 0;
    CWorldSurface surface; surface.m_pPlane = &plane;
    CWorldPart polygon{}; polygon.m_pSurface = &surface;
    std::vector<WorldPolyRef> nodes(5000);
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        nodes[i].m_pOwner = &polygon; nodes[i].m_Flags = 8;
        nodes[i].m_Children[0] = static_cast<WorldPolyRef*>(g_pWorldTreeFrontTerm);
        nodes[i].m_Children[1] = i + 1 < nodes.size() ? &nodes[i + 1] : static_cast<WorldPolyRef*>(g_pWorldTreeFrontTerm);
    }
    LTVector center(0,0,0), front(1,0,0);
    require(bsp_PointInside(nodes.data(), &front) == 1 && bsp_SphereInside(nodes.data(), &center, 1) == 1,
        "native containment traverses and unwinds 5000 levels without fixed stack limits");
    nodes.back().m_Children[0] = static_cast<WorldPolyRef*>(g_pWorldTreeBackTerm);
    require(bsp_SphereInside(nodes.data(), &center, 1) == 2 && bsp_PointInside(nodes.data(), &front) == 1,
        "deep sphere straddle and point containment preserve original leaf semantics");
}
void line_queries() {
    nolf::MemoryStream stream(model_fixture().bytes);
    auto world = w_ReadWorldModel(stream, true);
    world->m_pTree[0].m_nFlags = 1; world->m_nf8 = 23;
    LTVector from(0,1,0), to(0,-1,0), hit(9,9,9);
    LTPlane plane;
    require(bsp_IntersectLine(world->GetRoot(), &from, &to, &hit, &plane) == world->GetRoot() &&
        hit.MagSqr() == 0 && plane.m_Normal.y == 1, "native line query returns exact triangle crossing and plane");
    require(!bsp_IntersectLine(world->GetRoot(), &to, &from, &hit, &plane), "line query preserves one-sided polygon behavior");
    LTVector outside_from(5,1,5), outside_to(5,-1,5);
    require(!bsp_IntersectLine(world->GetRoot(), &outside_from, &outside_to, &hit, &plane), "plane crossing outside polygon is rejected");
    LTVector nonfinite(std::numeric_limits<float>::quiet_NaN(),0,0);
    require(!w_PointInPoly(world->m_ppParts[0], &nonfinite) && !bsp_IntersectLine(world->GetRoot(), &nonfinite, &to, &hit, &plane),
        "nonfinite query points cannot produce polygon hits");
    delete[] world->m_n70; world->m_n70 = new WorldPolyRef[5000]; world->m_n74 = 5000;
    for (uint32 i = 0; i < world->m_n74; ++i) {
        auto& node = world->m_n70[i]; node.m_pOwner = world->m_ppParts[0]; node.m_Flags = 8;
        node.m_Children[0] = static_cast<WorldPolyRef*>(g_pWorldTreeBackTerm);
        node.m_Children[1] = i + 1 < world->m_n74 ? world->m_n70 + i + 1 : static_cast<WorldPolyRef*>(g_pWorldTreeFrontTerm);
    }
    struct Filter { uint32 count = 0; HPOLY handle = 0; } filter;
    IntersectQuery intersect;
    intersect.m_pUserData = &filter;
    intersect.m_PolyFilterFn = [](HPOLY handle, void* user) -> LTBOOL {
        auto& filter = *static_cast<Filter*>(user); ++filter.count; filter.handle = handle; return LTFALSE;
    };
    CLineQuery query;
    query.m_pStart = &from; query.m_pEnd = &to; query.m_pHit = &hit;
    query.m_pQuery = &intersect; query.m_pWorldBsp = world.get();
    require(!bspq_IntersectLine(world->m_n70, &query) && filter.count == 5000 && filter.handle == (23u << 16),
        "iterative line traversal unwinds 5000 rejected crossings and passes native polygon handles to SDK filters");
    query.m_pQuery = nullptr;
    require(bspq_IntersectLine(world->m_n70, &query) && query.m_pPoly == world->m_n70 + 4999,
        "line traversal reports nearest accepted crossing in original traversal order");
}
void world_file_checks() {
    Bytes prefix(44); nolf::wire::store(prefix.data(),uint32(66));
    const std::string information="WorldName = Native menu metadata\n";
    append(prefix,static_cast<uint32>(information.size()));
    for (auto character : information) prefix.push_back(std::byte(character));
    nolf::MemoryStream metadata(prefix); const auto original_bytes=g_WorldBytes;
    const auto info=w_ReadWorldInformation(metadata);
    uint32 position=0; require(metadata.GetPos(&position)==LT_OK && position==prefix.size() && info.information==information && g_WorldBytes==original_bytes,"world menu information loaded geometry or lost prefix contents");
    for (std::size_t size=0;size<prefix.size();++size) {
        bool rejected=false;
        try { nolf::MemoryStream truncated(Bytes(prefix.begin(),prefix.begin()+size)); w_ReadWorldInformation(truncated); }
        catch (const std::exception&) { rejected=true; }
        require(rejected,"truncated world information prefix accepted");
    }
    for (std::size_t offset : {std::size_t(0),std::size_t(4),std::size_t(8),std::size_t(44)}) {
        auto invalid=prefix; nolf::wire::store(invalid.data()+offset,uint32(0xffffffff)); bool rejected=false;
        try { nolf::MemoryStream corrupt(std::move(invalid)); w_ReadWorldInformation(corrupt); }
        catch (const std::exception&) { rejected=true; }
        require(rejected,"invalid world menu version, offset or extent accepted");
    }
    auto model = model_fixture().bytes;
    Bytes bytes(44);
    nolf::wire::store(bytes.data(), uint32(66));
    append(bytes, uint32(0)); append(bytes, 1000.f);
    for (uint32 i = 0; i < 2; ++i) { vec(bytes, {-1,-1,-1}); vec(bytes, {1,1,1}); }
    append(bytes, uint32(1)); append(bytes, uint32(0)); append(bytes, uint8(0)); append(bytes, uint32(1));
    const auto next_position = bytes.size(); append(bytes, uint32(0));
    bytes.insert(bytes.end(), model.begin(), model.end());
    for (auto offset : {std::size_t(4), std::size_t(8), next_position}) nolf::wire::store(bytes.data() + offset, static_cast<uint32>(bytes.size()));
    nolf::MemoryStream stream(bytes);
    auto world = w_ReadWorldGeometry(stream);
    require(world->models.size() == 1 && world->layout.size() == 1 && world->models[0]->m_pCurrent == world->models[0]->m_pWorldBsp,
        "complete world reader owns native geometry and resolved model state");
    for (auto offset : {std::size_t(0), std::size_t(4), next_position}) {
        auto corrupt = bytes; nolf::wire::store(corrupt.data() + offset, uint32(0xffffffff));
        bool rejected = false;
        try { nolf::MemoryStream invalid_stream(std::move(corrupt)); w_ReadWorldGeometry(invalid_stream); }
        catch (const std::exception&) { rejected = true; }
        require(rejected, "unsupported versions and invalid absolute section/model offsets are rejected");
    }
}
void real_worlds(const std::filesystem::path& path) {
    nolf::Assets assets; assets.mount_game(path);
    std::size_t worlds = 0, models = 0, polies = 0, terrain = 0;
    const auto original_bytes = g_WorldBytes;
    for (const auto& filename : assets.names()) {
        if (!filename.starts_with("worlds/") || !filename.ends_with(".dat")) continue;
        try {
            nolf::AssetStream stream(assets.locate(filename));
            const auto info=w_ReadWorldInformation(stream);
            auto world = w_ReadWorldGeometry(stream);
            require(info.header==world->header && info.information==world->information,"lightweight original world metadata differs from full geometry load");
            ++worlds; models += world->models.size();
            for (const auto& record : world->models) {
                polies += record->m_pWorldBsp->m_nParts; terrain += record->m_pWorldBsp->m_Array138.GetSize();
            }
        } catch (const std::exception& error) { throw std::runtime_error(filename + ": " + error.what()); }
        require(g_WorldBytes == original_bytes, "original world geometry releases completely between levels");
    }
    require(worlds != 0, "no original worlds were found");
    std::cout << "decoded " << worlds << " original worlds, " << models << " models, " << polies << " polygons, " << terrain << " terrain sections\n";
}
}
int main(int argc, char** argv) {
    try {
        static_assert(sizeof(CWorldEdgeQT1) == 4);
        interface_and_lifetime(); terrain_copy_and_teardown(); keys_and_bounds(); record_ownership_and_reentrancy();
        loader_checks(); deep_containment(); line_queries(); world_file_checks();
        if (argc == 3 && std::string_view(argv[1]) == "--data") real_worlds(argv[2]);
        std::cout << "native BSP interface, handles and polymorphic geometry ownership passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
