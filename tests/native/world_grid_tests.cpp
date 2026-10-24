#include "engine/server/world_grid.h"
#include "engine/server/world_node.h"
#include "engine/server/world_polygon.h"
#include "engine/server/world_clip.h"
#include <array>
#include "engine/portable/memory_stream.h"
#include "engine/portable/wire.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
using Bytes = std::vector<std::byte>;
void require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
template<class T> void append(Bytes& bytes, T value) {
    const auto start = bytes.size(); bytes.resize(start + sizeof(T));
    nolf::wire::store(bytes.data() + start, value);
}
Bytes header(int32 x = 2, int32 y = 1, int32 z = 1) {
    Bytes bytes;
    append(bytes, x); append(bytes, y); append(bytes, z);
    for (float value : {10.f,20.f,30.f,-10.f,-20.f,-30.f}) append(bytes, value);
    return bytes;
}
Bytes fixture() {
    auto bytes = header();
    append(bytes, uint16{1}); append(bytes, uint16{0});
    append(bytes, uint16{7}); append(bytes, uint16{0xfffe}); append(bytes, uint16{0xffff});
    append(bytes, uint16{0}); append(bytes, uint16{0xffff});
    return bytes;
}
bool load(CWorldSub108& grid, const Bytes& bytes) {
    nolf::MemoryStream stream(bytes);
    return grid.Load(&stream) != LTFALSE;
}
void grid_tests() {
    CWorldSub108 grid;
    require(load(grid, fixture()), "load original six-byte physics node format");
    require(grid.m_nRecords == 2 && grid.m_nSliceStride == 2 && grid.m_pRecords[0].m_nCount == 1 &&
        grid.m_pRecords[0].m_pNodes[0].m_nPoly == 7 && grid.m_pRecords[0].m_pNodes[0].m_nChild[1] == 0xffff, "native cells preserve wire words and own typed nodes");
    require(grid.GetPointStatus({-11,-100,-100}) == &grid.m_pRecords[0] &&
        grid.GetPointStatus({std::numeric_limits<float>::max(),1000,1000}) == &grid.m_pRecords[1], "point lookup clamps without overflowing integer conversions");
    require(!grid.GetPointStatus({std::numeric_limits<float>::quiet_NaN(),0,0}), "NaN position does not index grid storage");
    auto* original = grid.m_pRecords;
    {
        CWorldSub108 copy(grid);
        require(copy.m_pRecords != original && copy.m_pRecords[0].m_pNodes != original[0].m_pNodes, "copied world owns independent cell and node arrays");
        copy.m_pRecords[0].m_pNodes[0].m_nPoly = 99;
        require(original[0].m_pNodes[0].m_nPoly == 7, "copied nodes do not alias the source");
        CWorldSub108 assigned;
        assigned = copy; assigned = assigned;
        auto* storage = assigned.m_pRecords;
        CWorldSub108 moved(std::move(assigned));
        require(moved.m_pRecords == storage && !assigned.m_pRecords && !assigned.m_nRecords, "grid move transfers ownership and empties source");
        assigned = std::move(moved);
        require(assigned.m_pRecords == storage && !moved.m_pRecords, "grid move assignment preserves owned nodes");
    }
    const auto complete = fixture();
    for (std::size_t size = 0; size < complete.size(); ++size) {
        Bytes truncated(complete.begin(), complete.begin() + size);
        require(!load(grid, truncated) && grid.m_pRecords == original && original[0].m_pNodes[0].m_nPoly == 7, "truncated grid preserves previous world and releases staged allocations");
    }
    for (auto dimensions : {header(-1), header(0x7fffffff,0x7fffffff,0x7fffffff), header(1000000)})
        require(!load(grid, dimensions) && grid.m_pRecords == original, "invalid cell counts fail before allocation");
    auto zero_cell = fixture(); nolf::wire::store(zero_cell.data() + 12, 0.f);
    require(!load(grid, zero_cell), "nonempty grid requires positive cell dimensions");
    auto nan = fixture(); nolf::wire::store(nan.data() + 24, std::numeric_limits<float>::infinity());
    require(!load(grid, nan), "nonfinite grid origin is rejected");
    auto bad_root = fixture(); nolf::wire::store(bad_root.data() + 38, uint16{1});
    require(!load(grid, bad_root), "cell root must name a node or reserved leaf");
    auto bad_child = fixture(); nolf::wire::store(bad_child.data() + 42, uint16{1});
    require(!load(grid, bad_child), "child index cannot leave its cell");
    auto cycle = fixture(); nolf::wire::store(cycle.data() + 42, uint16{0});
    require(!load(grid, cycle), "cyclic physics tree is rejected before simulation");
    auto deep = header(1);
    append(deep, uint16{1025}); append(deep, uint16{0});
    for (uint16 index = 0; index < 1025; ++index) {
        append(deep, uint16{0}); append(deep, index == 1024 ? uint16{0xfffe} : uint16(index + 1)); append(deep, uint16{0xffff});
    }
    require(load(grid, deep), "valid deep physics trees use growing traversal storage");
    auto flat = header(1);
    for (std::size_t offset : {12u,16u,20u}) nolf::wire::store(flat.data() + offset, 0.f);
    append(flat, uint16{0}); append(flat, uint16{0xffff});
    require(load(grid, flat) && grid.GetPointStatus({100,-200,300}) == grid.m_pRecords,
        "single-cell terrain axes can have zero width and never divide by zero");
    require(load(grid, header(0)) && !grid.m_pRecords && !grid.m_nRecords && !grid.GetPointStatus({0,0,0}), "empty grid replaces and releases previous storage");
    grid.Term(); grid.Term();
}
void polygon_tests() {
    LTPlane plane;
    plane.m_Normal.Init(0,1,0); plane.m_Dist = 42;
    CWorldSurface surface;
    surface.m_pPlane = &plane;
    surface.m_nAxis = 0;
    require(surface.m_Sub28.m_pOwner == &surface, "surface observer retains complete native owner pointer");
    CWorldPart part{};
    part.m_pSurface = &surface;
    WorldPolyRef node;
    node.m_pOwner = &part;
    require(node.GetPlane() == &plane && node.m_pOwner->m_pParent == &surface, "loader, BSP traversal and physics use one node/polygon/surface chain");
    WorldPolyVertex vertices[5]{};
    part.m_Verts = vertices;
    part.m_pVerts2 = vertices;
    part.m_nVerts = 3; part.m_nVerts2 = 3;
    require(part.GetVertexCount() == 3, "shared alternate vertex run is counted once");
    part.m_pVerts2 = vertices + 3; part.m_nVerts2 = 2;
    require(part.GetVertexCount() == 5 && world_polygon_bytes(5) >= sizeof(part) + sizeof(vertices), "polygon allocation accounts for every native vertex record");
}
void sweep_queries_and_clipping() {
    CWorldSub108 grid; require(load(grid,fixture()),"multi-cell query fixture");
    grid.m_pRecords[1].m_nCount = 1; grid.m_pRecords[1].m_nIndex = 0;
    delete[] grid.m_pRecords[1].m_pNodes;
    grid.m_pRecords[1].m_pNodes = new WSNode[1]{grid.m_pRecords[0].m_pNodes[0]};
    LTPlane plane; plane.m_Normal.Init(0,1,0); plane.m_Dist = 0;
    CWorldSurface surface{}; surface.m_pPlane = &plane;
    CWorldPart polygon{}; polygon.m_pParent = &surface;
    std::array<WorldPolyRef,8> refs; refs[7].m_pOwner = &polygon;
    auto query = grid.QueryBox({-9,-1,-1},{9,1,1},refs);
    require(query.multiple_cells && query.nodes.size() == 1 && query.nodes[0]->m_nPoly == 7,"swept query crosses cells and deduplicates shared native polygon references");
    query = grid.QueryBox({-9,5,-1},{9,6,1},refs);
    require(query.multiple_cells && query.nodes.empty(),"swept query prunes native trees outside the box");
    query = grid.QueryBox({-9,-1,-1},{-7,1,1},refs);
    require(!query.multiple_cells && query.nodes.empty(),"single-cell move keeps the native tree traversal path");
    const auto extreme = std::numeric_limits<float>::max();
    query = grid.QueryBox({-extreme,-extreme,-extreme},{extreme,extreme,extreme},refs);
    require(query.multiple_cells && query.nodes.size() == 1,"finite extreme sweep bounds cannot overflow cell or plane arithmetic");
    std::vector<LTVector> positions; std::vector<WorldPolyVertex> vertices(96);
    positions.reserve(vertices.size());
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const double angle = 6.283185307179586*double(i)/vertices.size();
        positions.emplace_back(float(std::cos(angle)*10),0,float(std::sin(angle)*10));
    }
    for (std::size_t i = 0; i < vertices.size(); ++i) vertices[i].m_pPos = &positions[i];
    polygon.m_nVertices = static_cast<uint16>(vertices.size()); polygon.m_Vertices = vertices.data();
    const std::array<AxisClipPlane,4> planes{{{0,-1,1},{0,1,-1},{2,-1,1},{2,1,-1}}};
    auto clipped = w_ClipPolygon(polygon,planes);
    require(clipped.size() == 4,"large convex polygons clip without historical 32/40-vertex buffers");
    for (auto point : clipped) require(std::fabs(point.x) <= 1.0001f && std::fabs(point.z) <= 1.0001f && point.y == 0,"clipped vertices lie inside the actual query rectangle");
    require(w_PolygonTouchesBox(polygon,{-1,-1,-1},{1,1,1}) && !w_PolygonTouchesBox(polygon,{20,-1,-1},{22,1,1}),"large polygon rejection uses finite geometry instead of an unconditional blocking fallback");
    polygon.m_nVertices = 0;
    require(w_ClipPolygon(polygon,planes).empty(),"empty polygon clipping never reads a previous vertex");
}
}
int main() {
    try {
        grid_tests(); polygon_tests(); sweep_queries_and_clipping();
        std::cout << "native physics grid bounds, decoding, ownership, and shared polygon types passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
