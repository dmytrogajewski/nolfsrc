#include "engine/server/world_state.h"
#include "engine/server/object_properties.h"
#include "engine/server/ltobject_internal.h"
#include "engine/portable/assets.h"
#include "engine/portable/asset_stream.h"
#include "engine/portable/memory_stream.h"
#include "engine/portable/command_reader.h"
#include "engine/portable/wire.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
class Object : public LTObject {
public:
    Object(LTVector min, LTVector max) { m_BBoxMin = min; m_BBoxMax = max; }
};
class TrackedStream : public CBaseStream {
public:
    TrackedStream(uint32& releases, CServerWorld* world) : releases_(releases), world_(world) {}
    void Release() override { ++releases_; if (world_) world_->Term(); delete this; }
    LTRESULT Read(void*,uint32) override { return LT_ERROR; }
    LTRESULT Write(const void*,uint32) override { return LT_ERROR; }
    LTRESULT SeekTo(uint32) override { return LT_ERROR; }
    LTRESULT GetPos(uint32*) override { return LT_ERROR; }
    LTRESULT GetLen(uint32*) override { return LT_ERROR; }
    LTRESULT ErrorStatus() override { return LT_ERROR; }
private:
    uint32& releases_;
    CServerWorld* world_;
};
void spatial() {
    WorldSpatialTree tree;
    std::array<WorldLayoutNode,5> layout;
    layout[0] = {{-10,-10,-10},{10,10,10},{1,2,3,4}};
    for (uint32 i = 0; i < 4; ++i) layout[i+1] = {{i & 2 ? 0.f : -10.f,-10,i & 1 ? 0.f : -10.f}, {i & 2 ? 10.f : 0.f,10,i & 1 ? 10.f : 0.f}};
    require(tree.LoadLayout(layout), "load native quad-tree layout");
    CWorldSimpleHeader path{}; path.m_n00 = 3; path.m_n04 = 1;
    require(w_FindLayoutNode(layout,path) == &layout[4], "terrain path resolves original two-bit quad-tree child codes");
    path.m_n04 = 17; require(!w_FindLayoutNode(layout,path), "terrain path cannot exceed its serialized path storage");
    Object left({-8,-1,-8},{-6,1,-6}), right({6,-1,6},{8,1,8}), center({-1,-1,-1},{1,1,1}), outside({20,20,20},{21,21,21});
    for (auto* object : {&left,&right,&center,&outside}) tree.BumpObject(object, 0);
    require(tree.Size() == 4, "one index entry per object");
    std::vector<LTObject*> found;
    tree.QueryBox({-9,-2,-9},{-5,2,-5},[&](LTObject* o) { found.push_back(o); });
    require(found == std::vector<LTObject*>{&left}, "box query follows intersected cells and bounds");
    found.clear(); tree.QueryPoint({20,20,20},[&](LTObject* o) { found.push_back(o); });
    require(found == std::vector<LTObject*>{&outside}, "objects beyond the layout retain queryable root entries");
    found.clear(); tree.QuerySegment({-9,0,-9},{9,0,9},[&](LTObject* o) { found.push_back(o); });
    require(found.size() == 3, "segment query tests full three-axis slabs");
    auto bad = layout; bad[0].children[0] = 0;
    require(!tree.LoadLayout(bad) && tree.Size() == 4, "invalid layout preserves current index");
    left.m_BBoxMin.Init(6,2,6); left.m_BBoxMax.Init(8,3,8); tree.BumpObject(&left, 0);
    found.clear(); tree.QueryPoint({-7,0,-7},[&](LTObject* o) { found.push_back(o); });
    require(found.empty(), "moving an object removes its old cell membership");
    uint32 count = 0;
    tree.QueryBox({-100,-100,-100},{100,100,100},[&](LTObject* object) {
        ++count; tree.RemoveObject(&left); tree.RemoveObject(&right); tree.RemoveObject(&center); tree.RemoveObject(&outside);
        tree.QueryPoint({0,0,0}, [](LTObject*) { throw std::runtime_error("removed object remained in nested query"); });
        tree.BumpObject(object, 0);
    });
    require(count == 1 && tree.Size() == 1, "callbacks can remove and reinsert candidates without reviving an old query generation");
    tree.Term(); tree.Term(); require(tree.Size() == 0, "spatial teardown is repeatable");
    {
        auto object = std::make_unique<Object>(LTVector(-1,-1,-1),LTVector(1,1,1));
        tree.BumpObject(object.get(), 0);
        object.reset(); require(tree.Size() == 0, "native object destruction detaches spatial membership");
    }
    {
        auto first = std::make_unique<Object>(LTVector(-1,-1,-1),LTVector(1,1,1));
        auto second = std::make_unique<Object>(LTVector(-1,-1,-1),LTVector(1,1,1));
        tree.BumpObject(first.get(), 0); tree.BumpObject(second.get(), 0);
        uint32 visited = 0;
        tree.QueryPoint({0,0,0}, [&](LTObject*) { ++visited; first.reset(); second.reset(); });
        require(visited == 1 && tree.Size() == 0, "callbacks can destroy current and future candidates safely");
    }
    {
        WorldSpatialTree other;
        auto object = std::make_unique<Object>(LTVector(-1,-1,-1),LTVector(1,1,1));
        tree.BumpObject(object.get(),0); tree.Swap(other);
        require(tree.Size() == 0 && other.Size() == 1, "spatial state transfers without allocation during world publication");
        object.reset(); require(other.Size() == 0, "transferred memberships detach from their new owner");
    }
}
using Bytes = std::vector<std::byte>;
template<class T> void append(Bytes& bytes, T value) {
    auto at = bytes.size(); bytes.resize(at + sizeof(T)); nolf::wire::store(bytes.data() + at,value);
}
void string(Bytes& bytes, std::string_view value) { append(bytes,uint16(value.size())); for (char c : value) append(bytes,uint8(c)); }
void vec(Bytes& bytes, LTVector v) { append(bytes,v.x); append(bytes,v.y); append(bytes,v.z); }
void properties() {
    std::vector<WorldProperty> source;
    auto add = [&](std::string name, uint8 type, Bytes data) { source.push_back({std::move(name),type,0,std::move(data)}); };
    Bytes text; string(text,std::string(600,'a')); add("LongText",0,text);
    for (auto type : {uint8(1),uint8(2),uint8(7)}) {
        Bytes data; vec(data,{1,2,3}); if (type == 7) append(data,0.f);
        add("Vector" + std::to_string(type),type,data);
    }
    for (auto type : {uint8(3),uint8(4),uint8(6)}) {
        Bytes data; append(data,-42.5f); add("Number" + std::to_string(type),type,data);
    }
    add("Enabled",5,{std::byte{1}});
    ObjectProperties decoded{std::span<const WorldProperty>(source)};
    source.clear();
    require(decoded.find("LongText")->text.size() == 600 &&
            std::strlen(decoded.find("LongText")->value.m_String) == MAX_GP_STRING_LEN,
            "native properties own full source strings while generic output obeys SDK limits");
    require(decoded.find("Vector1")->value.m_Vec.DistSqr({1,2,3}) == 0 &&
            decoded.find("Vector2")->value.m_Color.DistSqr({1,2,3}) == 0 &&
            decoded.find("Vector7")->value.m_Vec.DistSqr({1,2,3}) == 0 &&
            decoded.find("Number6")->value.m_Long == -42 && decoded.find("Enabled")->value.m_Bool,
            "all original property types decode explicit serialized values");
    ObjectProperties statements("Tag first; Tag \"last value\"; Pos \"1 2 3\"; Enabled true; Huge 1e38;");
    require(statements.find("Tag")->text == "last value" && statements.find("Pos")->value.m_Vec.DistSqr({1,2,3}) == 0 &&
            statements.find("Enabled")->value.m_Bool && statements.find("Huge")->value.m_Long == INT32_MAX,
            "property statements retain last-definition precedence and bounded 32-bit numeric conversions");
    auto rejects = [](const WorldProperty& property) {
        try { ObjectProperties invalid{std::span<const WorldProperty>(&property,1)}; }
        catch (const std::exception&) { return; }
        throw std::runtime_error("invalid property record accepted");
    };
    for (std::size_t length = 0; length < text.size(); ++length)
        rejects({"Text",0,0,Bytes(text.begin(),text.begin()+length)});
    rejects({"Unknown",99,0,{}});
    Bytes number; append(number,std::numeric_limits<float>::infinity()); rejects({"Nonfinite",3,0,number});
    rejects({"Extra",5,0,{std::byte{1},std::byte{0}}});
}
Bytes empty_world() {
    Bytes bytes(44); nolf::wire::store(bytes.data(),uint32(66));
    const std::string info = "ambientlight 10 20 30; LIGHTTABLERES 1;";
    append(bytes,uint32(info.size())); for (char c : info) append(bytes,uint8(c));
    append(bytes,1000.f);
    for (uint32 i = 0; i < 2; ++i) { vec(bytes,{0,0,0}); vec(bytes,{2,2,2}); }
    append(bytes,uint32(1)); append(bytes,uint32(0)); append(bytes,uint8(0)); append(bytes,uint32(0));
    nolf::wire::store(bytes.data() + 4,uint32(bytes.size()));
    append(bytes,uint32(1));
    Bytes object; string(object,"ObjectLight"); append(object,uint32(4));
    const auto property = [&](std::string_view name, uint8 type, const Bytes& data) {
        string(object,name); append(object,type); append(object,uint32(0)); append(object,uint16(data.size())); object.insert(object.end(),data.begin(),data.end());
    };
    Bytes radius; append(radius,2.f); property("LightRadius",3,radius);
    Bytes color; vec(color,{100,0,0}); property("LightColor",2,color);
    Bytes pos; vec(pos,{0,0,0}); property("Pos",1,pos);
    Bytes rotation; vec(rotation,{0,0,0}); append(rotation,0.f); property("Rotation",7,rotation);
    append(bytes,uint16(object.size())); bytes.insert(bytes.end(),object.begin(),object.end());
    nolf::wire::store(bytes.data() + 8,uint32(bytes.size()));
    for (uint32 i = 0; i < 5; ++i) append(bytes,uint32(0));
    return bytes;
}
void world_ownership() {
    auto bytes = empty_world(); nolf::MemoryStream stream(bytes); WorldLoadRequest request; request.m_pStream = &stream;
    CServerWorld world;
    require(world.Load(&request) == 0 && world.LoadObjects(&stream) == LT_OK && world.m_bLoaded && world.ObjectDefinitions().size() == 1,
        "owned world loads object definitions and four-float Euler rotation properties");
    require(world.m_Shade.Sample({0,0,0}) == std::array<uint8,4>{30,20,110,255} && world.Lights().size() == 1,
        "case-insensitive world settings and original light records initialize native shading");
    require(world.PositionReference()->m_vMin.x == 0 && world.PositionReference()->m_vMax.x == 2 &&
        world.PositionReference()->m_vScale.x == .5f && world.m_vBoxMin.x == -100,
        "message compression uses original file bounds and keeps collision margins separate");
    HPOLY polygon = INVALID_HPOLY;
    require(world.NextPoly(&polygon) == LT_FINISHED && world.NextPoly(nullptr) == LT_INVALIDPARAMS,
        "SDK polygon iteration handles empty worlds and invalid output pointers");
    const auto* geometry = world.Geometry();
    for (std::size_t size = 0; size < bytes.size() - 20; ++size) {
        nolf::MemoryStream partial(Bytes(bytes.begin(),bytes.begin() + size)); request.m_pStream = &partial;
        require(world.Load(&request) != 0 && world.Geometry() == geometry && world.m_bLoaded,
            "truncated world sections preserve live geometry, properties and lighting");
    }
    CServerWorld inherited;
    require(inherited.InheritWorld(&world) && inherited.Geometry() != geometry, "world inheritance owns independent runtime state");
    uint32 releases = 0;
    world.m_pObjectStream = new TrackedStream(releases,nullptr);
    world.TermObjects(); world.TermObjects();
    require(releases == 1 && world.Geometry() == geometry && world.m_bLoaded, "world object-stream release is typed and leaves retained geometry available");
    world.m_pObjectStream = new TrackedStream(releases,&world);
    world.Term();
    require(releases == 2 && !world.m_pObjectStream && !world.Geometry(), "stream callbacks can request teardown without double release");
    require(inherited.m_bLoaded && inherited.ObjectDefinitions().size() == 1 && inherited.m_Shade.Size() == 27,
        "world state and lights survive source teardown");
    nolf::CommandReader commands("first \"a b\" (outer (inner)) escaped%;value;second 2\n");
    require(commands.next() == std::vector<std::string>{"first","a b","outer (inner)","escaped;value"} &&
        commands.next() == std::vector<std::string>{"second","2"} && !commands.next(), "world statements preserve original grouping, escapes and command boundaries");
}
void shading() {
    WorldShadeGrid grid;
    grid.Setup({0,0,0},{2,2,2},1,{10,20,30});
    require(grid.Size() == 27 && grid.Sample({1,1,1}) == std::array<uint8,4>{30,20,10,255}, "native shade grid initializes BGR ambient samples");
    WorldLight light; light.position.Init(0,0,0); light.radius = 2; light.color.Init(100,0,0);
    grid.AddLight(light);
    require(grid.Sample({0,0,0})[2] == 110 && grid.Sample({1,0,0})[2] == 35 && grid.Sample({.5f,0,0})[2] == 72,
        "fast lighting preserves quadratic falloff and trilinear sampling");
    require(grid.Sample({2,2,2}) == grid.Sample({100,100,100}) && grid.Sample({-100,-100,-100}) == grid.Sample({0,0,0}),
        "sampling clamps complete coordinates and neighboring cells at both boundaries");
    WorldShadeGrid flat; flat.Setup({1,1,1},{1,1,1},250,{1,2,3});
    require(flat.Size() == 1 && flat.Sample({999,999,999}) == std::array<uint8,4>{3,2,1,255}, "flat one-cell grids interpolate without neighboring allocations");
    bool rejected = false;
    try { flat.Setup({0,0,0},{1,1,1},0,{0,0,0}); } catch (const std::exception&) { rejected = true; }
    require(rejected && flat.Size() == 1, "invalid grid replacement preserves owned samples");
}
void real_worlds(const std::filesystem::path& path) {
    nolf::Assets assets; assets.mount_game(path);
    std::size_t worlds = 0, objects = 0, lights = 0;
    CServerWorld world;
    for (const auto& name : assets.names()) {
        if (!name.starts_with("worlds/") || !name.ends_with(".dat")) continue;
        nolf::AssetStream stream(assets.locate(name)); WorldLoadRequest request; request.m_pStream = &stream;
        if (world.Load(&request) != 0 || world.LoadObjects(&stream) != LT_OK) throw std::runtime_error("native world state load failed: " + name + ": " + world.LastLoadError());
        require(world.m_bLoaded && world.Geometry() && world.m_Worlds.GetSize() == world.Geometry()->models.size(), "world state owns its native geometry table");
        uint64 expected_polygons = 0, enumerated = 0;
        for (const auto& record : world.Geometry()->models) expected_polygons += record->m_pWorldBsp->m_nParts;
        HPOLY polygon = INVALID_HPOLY;
        while (world.NextPoly(&polygon) == LT_OK) { require(world.FindPoly(polygon), "enumerated polygon resolves through the native world table"); ++enumerated; }
        require(enumerated == expected_polygons, "SDK iteration visits every original polygon exactly once");
        ++worlds; objects += world.ObjectDefinitions().size(); lights += world.Lights().size();
        nolf::MemoryStream truncated(std::vector<std::byte>(12)); request.m_pStream = &truncated;
        const auto* previous = world.Geometry();
        require(world.Load(&request) != 0 && world.Geometry() == previous && world.m_bLoaded, "bad replacement retains live world state");
        if (worlds == 1) {
            CServerWorld child;
            require(child.InheritWorld(&world) && child.Geometry() != world.Geometry() &&
                child.m_Worlds[0]->m_pWorldBsp != world.m_Worlds[0]->m_pWorldBsp, "inherited world owns independent mutable geometry and lighting");
            world.Term(); require(child.m_bLoaded && child.m_Worlds.GetSize() != 0, "inherited world survives source teardown");
        }
    }
    world.Term(); world.Term();
    require(worlds != 0 && !g_WorldBytes, "all native world state and geometry allocations release between levels");
    std::cout << "loaded native state for " << worlds << " original worlds, " << objects << " objects and " << lights << " enabled object lights\n";
}
}
int main(int argc, char** argv) {
    try { spatial(); shading(); world_ownership(); properties(); if (argc == 3 && std::string_view(argv[1]) == "--data") real_worlds(argv[2]);
        std::cout << "native world ownership, spatial queries and shade grid passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
