#include "world_loader.h"
#include "worldbsp.h"
#include "iltstream.h"
#include "engine/portable/allocation.h"
#include "engine/portable/wire.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

int math_FindClosestAxisBasis(LTVector);
void w_SetNodeProjectionsQT1(void*, uint32, int);
void w_SetupPolyTexCoordsQT1(CWorldPart*);

namespace {
[[noreturn]] void invalid(const char* text) { throw std::runtime_error(std::string("invalid world geometry: ") + text); }
class Reader {
public:
    explicit Reader(ILTStream& stream) : stream(stream) {
        if (stream.GetLen(&length) != LT_OK) invalid("stream length");
    }
    uint32 remaining() {
        uint32 position = 0;
        if (stream.GetPos(&position) != LT_OK || position > length) invalid("stream position");
        return length - position;
    }
    void bytes(void* destination, uint32 size) {
        if (size > remaining() || (size && stream.Read(destination, size) != LT_OK)) invalid("truncated record");
    }
    template<class T> T value() {
        std::array<std::byte, sizeof(T)> buffer{}; bytes(buffer.data(), buffer.size());
        return nolf::wire::load<T>(buffer.data());
    }
    LTVector vector(bool require_finite = true) {
        LTVector v; v.x = value<float>(); v.y = value<float>(); v.z = value<float>();
        if (require_finite && (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)))
            throw std::runtime_error(std::string("invalid world geometry: nonfinite vector in ") + phase +
                " at byte " + std::to_string(length - remaining() - 12));
        return v;
    }
    std::string string() {
        auto size = value<uint16>();
        std::string text(size, '\0'); bytes(text.data(), size);
        if (text.find('\0') != std::string::npos) invalid("embedded string terminator");
        return text;
    }
    void charge(uint64 size) {
        if (size > 256ull * 1024 * 1024 - allocation) invalid("allocation budget");
        allocation += size;
    }
    template<class T> T* raw(uint32 count) {
        const uint64 size = uint64(count) * sizeof(T); charge(size);
        auto* result = static_cast<T*>(dalloc_z(static_cast<uint32>(size)));
        if (size && !result) throw std::bad_alloc();
        return result;
    }
    template<class T> void count(uint32 n, uint32 serialized_size) {
        if (uint64(n) * serialized_size > remaining()) invalid("record count");
        charge(uint64(n) * sizeof(T));
    }
    ILTStream& stream;
    uint32 length = 0;
    uint64 allocation = sizeof(CWorldBsp);
    const char* phase = "model header";
};
void read_world_information(Reader& reader,std::array<uint32,11>& header,std::string& information) {
    if (reader.stream.SeekTo(0)!=LT_OK) invalid("world header seek");
    for (auto& word : header) word=reader.value<uint32>();
    if (header[0]!=66) invalid("unsupported world version");
    if (header[1]>reader.length || header[2]>reader.length) invalid("world section offsets");
    const auto size=reader.value<uint32>();
    if (size>reader.remaining() || size>1024*1024) invalid("world information size");
    information.resize(size); reader.bytes(information.data(),size);
}
template<std::size_t N> void copy_name(char (&destination)[N], const std::string& source) {
    if (source.size() >= N) invalid("name length");
    std::copy(source.begin(), source.end(), destination); destination[source.size()] = 0;
}
void validate_nodes(WorldPolyRef* nodes, uint32 count) {
    std::vector<uint8> color(count);
    std::vector<uint32> height(count);
    std::vector<std::pair<uint32, bool>> stack;
    for (uint32 start = 0; start < count; ++start) {
        if (color[start]) continue;
        stack.emplace_back(start, false);
        while (!stack.empty()) {
            auto [index, leaving] = stack.back(); stack.pop_back();
            auto& node = nodes[index];
            if (leaving) {
                uint32 max_height = 0;
                for (auto* child : node.m_Children)
                    if (child != g_pWorldTreeFrontTerm && child != g_pWorldTreeBackTerm)
                        max_height = std::max(max_height, height[child - nodes]);
                height[index] = max_height + 1; color[index] = 2;
            } else {
                if (color[index] == 1) invalid("BSP cycle");
                if (color[index] == 2) continue;
                color[index] = 1; stack.emplace_back(index, true);
                for (auto* child : node.m_Children)
                    if (child != g_pWorldTreeFrontTerm && child != g_pWorldTreeBackTerm)
                        stack.emplace_back(static_cast<uint32>(child - nodes), false);
            }
        }
    }
}
WorldPolyRef* node_reference(Reader& reader, WorldPolyRef* nodes, uint32 count) {
    auto* node = w_GetTreeNodeQT1(nodes, count, reader.value<uint32>());
    if (!node) invalid("BSP child or root index");
    return node;
}
void simple_header(Reader& reader, CWorldSimpleHeader& header) {
    header.m_n00 = reader.value<uint8>(); header.m_n01 = reader.value<uint8>();
    header.m_n02 = reader.value<uint8>(); header.m_n03 = reader.value<uint8>(); header.m_n04 = reader.value<uint32>();
}
uint64 grid_allocation(const CWorldSub108& grid, uint32 bsp_nodes) {
    uint64 bytes = uint64(grid.m_nRecords) * sizeof(CSubRecord);
    for (uint32 cell = 0; cell < grid.m_nRecords; ++cell) {
        const auto& block = grid.m_pRecords[cell];
        bytes += uint64(block.m_nCount) * sizeof(WSNode);
        for (uint32 node = 0; node < block.m_nCount; ++node)
            if (block.m_pNodes[node].m_nPoly >= bsp_nodes) invalid("physics BSP node index");
    }
    return bytes;
}
}

void wb_ReadSimpleHeader(ILTStream* stream, CWorldSimpleHeader* header) {
    if (!stream || !header) return;
    Reader reader(*stream); simple_header(reader, *header);
}

static void read_simple(CWorldSimple&, CWorldBsp*, ILTStream*, uint32);

std::unique_ptr<CWorldBsp> w_ReadWorldModel(ILTStream& stream, bool current) {
    Reader reader(stream);
    auto world = std::make_unique<CWorldBsp>();
    for (int i = 0; i < 8; ++i) reader.value<uint32>();
    world->m_nUnknown48 = reader.value<uint32>(); world->m_ndc = reader.value<uint32>();
    copy_name(world->m_Name, reader.string());
    const auto points = reader.value<uint32>(), planes = reader.value<uint32>(), surfaces = reader.value<uint32>();
    const auto portals = reader.value<uint32>(), polies = reader.value<uint32>(), groups = reader.value<uint32>();
    reader.value<uint32>();
    const auto pool = reader.value<uint32>(), entries = reader.value<uint32>(), nodes = reader.value<uint32>();
    const auto keys = reader.value<uint32>(), edges = reader.value<uint32>();
    world->m_vBoxMin = reader.vector(); world->m_vBoxMax = reader.vector(); world->m_ve8 = reader.vector();
    if (points > 65536 || polies > 65535 || portals > 65535 || groups > 65535 ||
        world->m_vBoxMin.x > world->m_vBoxMax.x || world->m_vBoxMin.y > world->m_vBoxMax.y ||
        world->m_vBoxMin.z > world->m_vBoxMax.z) invalid("model bounds or index capacity");
    const uint64 disk_minimum = uint64(points) * 24 + uint64(planes) * 16 + uint64(surfaces) * 53 +
        uint64(portals) * 32 + uint64(polies) * 24 + uint64(groups) * 10 + uint64(nodes) * 14 +
        uint64(edges) * 4 + uint64(keys) * 4 + uint64(entries) * 4 + pool;
    if (disk_minimum > reader.remaining()) invalid("model table counts");
    const auto string_bytes = reader.value<uint32>(), strings = reader.value<uint32>();
    if (strings > string_bytes || string_bytes > reader.remaining()) invalid("texture names");
    world->m_pb8 = reader.raw<char>(string_bytes);
    world->m_pbc = reader.raw<char*>(strings); world->m_nc0 = strings;
    uint32 name_offset = 0;
    for (uint32 i = 0; i < strings; ++i) {
        static_cast<char**>(world->m_pbc)[i] = static_cast<char*>(world->m_pb8) + name_offset;
        char c;
        do {
            if (name_offset >= string_bytes) invalid("unterminated texture name");
            c = reader.value<uint8>(); static_cast<char*>(world->m_pb8)[name_offset++] = c;
        } while (c);
    }
    std::vector<std::pair<uint8, uint8>> vertex_counts;
    vertex_counts.reserve(polies);
    uint64 polygon_bytes = 0;
    for (uint32 i = 0; i < polies; ++i) {
        const auto first = reader.value<uint8>(), second = reader.value<uint8>();
        if (first < 3) invalid("polygon vertex count");
        vertex_counts.emplace_back(first, second);
        polygon_bytes += world_polygon_bytes(uint32(first) + second);
    }
    if (polygon_bytes > std::numeric_limits<uint32>::max()) invalid("polygon allocation");
    world->m_pfc = reader.raw<std::byte>(static_cast<uint32>(polygon_bytes)); world->m_n100 = static_cast<uint32>(polygon_bytes);
    world->m_ppParts = reader.raw<CWorldPart*>(polies); world->m_nParts = polies;
    world->m_pa8 = reader.raw<LTVector>(points); world->m_nac = points;
    world->m_p68 = reader.raw<LTPlane>(planes); world->m_n6c = planes;
    reader.count<CWorldSurface>(surfaces, 53); world->m_pTree = new CWorldSurface[surfaces]; world->m_nSurfaces = surfaces;
    reader.count<CWorldGroupQT1>(groups, 10); world->m_p88 = new CWorldGroupQT1[groups]; world->m_n8c = groups;
    world->m_p80 = reader.raw<CWorldGroupEntryQT1>(entries); world->m_n84 = entries;
    world->m_p90 = reader.raw<CWorldPolyKey>(keys); world->m_n94 = keys;
    world->m_p104 = reader.raw<std::byte>(pool); world->m_n98 = pool;
    world->m_pChunks = reader.raw<CWorldChunk>(portals); world->m_nChunks = portals;
    reader.count<WorldPolyRef>(nodes, 14); world->m_n70 = new WorldPolyRef[nodes]; world->m_n74 = nodes;
    reader.count<CWorldEdgeQT1>(edges, 4);
    if (!world->m_Array54.SetSize(edges)) throw std::bad_alloc();
    uint32 polygon_offset = 0;
    for (uint32 i = 0; i < polies; ++i) {
        auto* poly = ::new (static_cast<std::byte*>(world->m_pfc) + polygon_offset) CWorldPart{};
        poly->m_Verts = reinterpret_cast<WorldPolyVertex*>(poly + 1); poly->m_nIndex = static_cast<uint16>(i);
        poly->m_nVerts = vertex_counts[i].first; poly->m_nVerts2 = vertex_counts[i].second;
        world->m_ppParts[i] = poly;
        polygon_offset += static_cast<uint32>(world_polygon_bytes(poly->m_nVerts + poly->m_nVerts2));
    }
    uint32 entry_offset = 0, pool_offset = 0, key_offset = 0;
    for (uint32 i = 0; i < groups; ++i) {
        auto& group = world->m_p88[i];
        const auto count = reader.value<uint16>();
        if (count == 0xffff) {
            const auto prior = reader.value<uint16>();
            if (prior >= i) invalid("forward group alias");
            group.m_pEntries = world->m_p88[prior].m_pEntries; group.m_nEntries = world->m_p88[prior].m_nEntries;
        } else {
            if (count > entries - entry_offset) invalid("group entry span");
            group.m_nEntries = count; group.m_pEntries = count ? world->m_p80 + entry_offset : nullptr;
            for (uint32 j = 0; j < count; ++j) {
                auto& entry = world->m_p80[entry_offset++];
                entry.m_n00 = reader.value<uint16>(); entry.m_nBytes = reader.value<uint16>();
                if (entry.m_nBytes > pool - pool_offset) invalid("group byte span");
                entry.m_pData = entry.m_nBytes ? static_cast<std::byte*>(world->m_p104) + pool_offset : nullptr;
                reader.bytes(entry.m_pData, entry.m_nBytes); pool_offset += entry.m_nBytes;
            }
        }
        group.m_nRefs = reader.value<uint32>();
        if (group.m_nRefs > keys - key_offset) invalid("group polygon key span");
        group.m_pRefs = group.m_nRefs ? world->m_p90 + key_offset : nullptr;
        for (uint32 j = 0; j < group.m_nRefs; ++j) {
            auto& key = world->m_p90[key_offset++]; key.m_iModel = reader.value<uint16>(); key.m_iPoly = reader.value<uint16>();
        }
        group.m_n28 = reader.value<uint32>();
    }
    reader.phase = "planes";
    for (uint32 i = 0; i < planes; ++i) {
        auto& plane = static_cast<LTPlane*>(world->m_p68)[i];
        plane.m_Normal = reader.vector(); plane.m_Dist = reader.value<float>();
        if (!std::isfinite(plane.m_Dist)) invalid("plane distance");
    }
    reader.phase = "surface texture vectors";
    for (uint32 i = 0; i < surfaces; ++i) {
        auto& surface = world->m_pTree[i];
        surface.m_vO = reader.vector(); surface.m_vP = reader.vector(); surface.m_vQ = reader.vector();
        surface.m_n3e = reader.value<uint16>();
        const auto plane = reader.value<uint32>();
        if (plane >= planes) invalid("surface plane index");
        surface.m_pPlane = static_cast<LTPlane*>(world->m_p68) + plane;
        surface.m_nAxis = math_FindClosestAxisBasis(surface.m_pPlane->m_Normal);
        surface.m_nFlags = reader.value<uint32>(); surface.m_n44 = reader.value<uint8>();
        for (int component = 0; component < 3; ++component) reader.value<uint8>();
        if (reader.value<uint8>() == 1) {
            auto class_name = reader.string(); auto properties = reader.string();
            reader.charge(sizeof(WorldSurfaceProperties) + class_name.size() + properties.size() + 2);
            world->m_SurfaceProperties.push_back({i, std::move(class_name), std::move(properties)});
        }
        surface.m_nTextureFlags = reader.value<uint16>(); surface.m_nFirstPoly = 0xffff;
    }
    reader.phase = "polygon data";
    uint32 edge_offset = 0;
    for (uint32 i = 0; i < polies; ++i) {
        auto& poly = *world->m_ppParts[i];
        // Retail m12s04 contains NaNs in this opaque polygon metadata. It
        // does not define positions, planes or texture vectors.
        poly.m_v34 = reader.vector(false); poly.m_n48 = static_cast<uint8>(reader.value<uint16>());
        poly.m_n49 = static_cast<uint8>(reader.value<uint16>()); poly.m_nEdges = reader.value<uint16>();
        if (poly.m_nEdges > edges - edge_offset) invalid("polygon edge span");
        poly.m_pEdges = poly.m_nEdges ? world->m_Array54.GetArray() + edge_offset : nullptr;
        for (uint32 j = 0; j < poly.m_nEdges; ++j) {
            auto& reference = world->m_Array54[edge_offset++]; reference.animation = reader.value<uint16>(); reference.polygon = reader.value<uint16>();
        }
        const auto surface = reader.value<uint32>();
        if (surface >= surfaces) invalid("polygon surface index");
        poly.m_pSurface = world->m_pTree + surface;
        for (uint32 j = 0; j < uint32(poly.m_nVerts) + poly.m_nVerts2; ++j) {
            const auto point = reader.value<uint16>();
            if (point >= points) invalid("polygon point index");
            auto& vertex = poly.m_Verts[j]; vertex = {};
            vertex.m_pPos = static_cast<LTVector*>(world->m_pa8) + point;
            vertex.m_r = reader.value<uint8>(); vertex.m_g = reader.value<uint8>(); vertex.m_b = reader.value<uint8>(); vertex.m_a = 0xff;
        }
        if (poly.m_nVerts2) poly.m_pVerts2 = poly.m_Verts + poly.m_nVerts;
        else { poly.m_pVerts2 = poly.m_Verts; poly.m_nVerts2 = poly.m_nVerts; }
        poly.m_nNextOnSurface = poly.m_pSurface->m_nFirstPoly; poly.m_pSurface->m_nFirstPoly = static_cast<uint16>(i);
    }
    for (uint32 i = 0; i < nodes; ++i) {
        auto& node = world->m_n70[i]; node.m_flag = 8;
        const auto poly = reader.value<uint32>();
        if (poly >= polies) invalid("BSP polygon index");
        node.m_pOwner = world->m_ppParts[poly]; node.m_x = reader.value<uint16>();
        for (auto& child : node.m_Children) child = node_reference(reader, world->m_n70, nodes);
    }
    validate_nodes(world->m_n70, nodes); w_SetNodeProjectionsQT1(world->m_n70, nodes, current);
    reader.phase = "portals";
    for (uint32 i = 0; i < portals; ++i) {
        auto& portal = world->m_pChunks[i]; portal.m_nIndex = static_cast<uint16>(i);
        const auto name = reader.string(); portal.m_pName = reader.raw<char>(static_cast<uint32>(name.size() + 1));
        std::memcpy(portal.m_pName, name.data(), name.size());
        reader.value<uint32>(); portal.m_n04 = reader.value<uint16>(); portal.m_v0c = reader.vector(); portal.m_v18 = reader.vector();
    }
    reader.phase = "points";
    for (uint32 i = 0; i < points; ++i) {
        static_cast<LTVector*>(world->m_pa8)[i] = reader.vector(); reader.vector(false);
    }
    const char* grid_error = nullptr;
    if (!world->m_Sub108.Load(&stream, &grid_error)) invalid(grid_error);
    reader.charge(grid_allocation(world->m_Sub108, nodes));
    world->m_n9c = node_reference(reader, world->m_n70, nodes);
    const auto subworlds = reader.value<uint32>(); reader.count<CWorldSimple>(subworlds, 64);
    if (!world->m_Array138.SetSize(subworlds)) throw std::bad_alloc();
    for (uint32 i = 0; i < subworlds; ++i) {
        read_simple(world->m_Array138[i], world.get(), &stream, i);
        const auto& terrain = world->m_Array138[i];
        reader.charge(uint64(terrain.m_Array50.GetSize()) * sizeof(CWorldPart*) +
            uint64(terrain.m_Array68.GetSize()) * sizeof(WorldPolyRef) + grid_allocation(terrain.m_Sub80, terrain.m_Array68.GetSize()));
    }
    for (uint32 i = 0; i < polies; ++i) w_SetupPolyTexCoordsQT1(world->m_ppParts[i]);
    wm_ResetGeometry(world.get()); world->Recalc();
    reader.phase = "visibility";
    reader.charge(WorldVisibility::EstimateMemory(*world));
    world->m_Visibility = WorldVisibility::Build(*world);
    if (stream.ErrorStatus() != LT_OK) invalid("stream error");
    world->m_nBytes = static_cast<uint32>(reader.allocation); g_WorldBytes += world->m_nBytes;
    return world;
}

static void read_simple(CWorldSimple& target, CWorldBsp* parent, ILTStream* stream, uint32 index) {
        Reader reader(*stream); CWorldSimple next; next.m_p7c = parent;
        copy_name(next.m_Name, "#$#" + std::to_string(index) + " " + parent->m_Name);
        const auto nodes = reader.value<uint32>(); reader.count<WorldPolyRef>(nodes, 12);
        if (!next.m_Array68.SetSize(nodes)) throw std::bad_alloc();
        for (uint32 i = 0; i < nodes; ++i) {
            auto& node = next.m_Array68[i]; node.m_flag = 8;
            const auto poly = reader.value<uint32>();
            if (poly >= parent->m_nParts) invalid("terrain polygon index");
            node.m_pOwner = parent->m_ppParts[poly];
            for (auto& child : node.m_Children) child = node_reference(reader, next.m_Array68.GetArray(), nodes);
        }
        validate_nodes(next.m_Array68.GetArray(), nodes);
        next.m_n64 = node_reference(reader, next.m_Array68.GetArray(), nodes);
        const auto polies = reader.value<uint32>(); reader.count<CWorldPart*>(polies, 4);
        if (!next.m_Array50.SetSize(polies)) throw std::bad_alloc();
        for (uint32 i = 0; i < polies; ++i) {
            const auto poly = reader.value<uint32>();
            if (poly >= parent->m_nParts) invalid("terrain polygon index");
            next.m_Array50[i] = parent->m_ppParts[poly];
        }
        const char* grid_error = nullptr;
        if (!next.m_Sub80.Load(stream, &grid_error)) invalid(grid_error);
        simple_header(reader, next.m_Header48); next.m_vCenter = reader.vector();
        w_SetNodeProjectionsQT1(next.m_Array68.GetArray(), nodes, 1); next.ComputeRadius();
        if (stream->ErrorStatus() != LT_OK) invalid("terrain stream error");
        target.Swap(next);
}
LTBOOL CWorldSimple::Load(CWorldBsp* parent, ILTStream* stream, uint32 index) {
    if (!parent || !stream) return LTFALSE;
    for (uint32 i = 0; i < m_Array68.GetSize(); ++i)
        if (m_Array68[i].m_head.m_pNext != &m_Array68[i].m_head) return LTFALSE;
    try { read_simple(*this, parent, stream, index); return LTTRUE; }
    catch (const std::exception&) { return LTFALSE; }
}

WorldFileInformation w_ReadWorldInformation(ILTStream& stream) {
    Reader reader(stream); WorldFileInformation information;
    read_world_information(reader,information.header,information.information); return information;
}
std::unique_ptr<WorldGeometryFile> w_ReadWorldGeometry(ILTStream& stream) {
    Reader reader(stream);
    auto world = std::make_unique<WorldGeometryFile>();
    read_world_information(reader,world->header,world->information);
    world->distance = reader.value<float>();
    if (!std::isfinite(world->distance)) invalid("world distance");
    world->min = reader.vector(); world->max = reader.vector();
    const auto tree_min = reader.vector(), tree_max = reader.vector();
    for (auto& word : world->layout_metadata) word = reader.value<uint32>();
    uint8 current_byte = 0, bit = 8;
    const auto load_node = [&](auto&& self, LTVector min, LTVector max, uint32 depth) -> uint32 {
        if (depth > 32 || world->layout.size() >= 1000000 ||
            min.x > max.x || min.y > max.y || min.z > max.z) invalid("spatial tree bounds or depth");
        if (bit == 8) { current_byte = reader.value<uint8>(); bit = 0; }
        const bool children = (current_byte & (1u << bit++)) != 0;
        const auto index = static_cast<uint32>(world->layout.size());
        world->layout.push_back({min, max});
        if (children) {
            const auto center = min * .5f + max * .5f;
            for (uint32 child = 0; child < 4; ++child) {
                auto child_min = min, child_max = max;
                if (child & 2) child_min.x = center.x; else child_max.x = center.x;
                if (child & 1) child_min.z = center.z; else child_max.z = center.z;
                const auto child_index = self(self, child_min, child_max, depth + 1);
                world->layout[index].children[child] = child_index;
            }
        }
        return index;
    };
    load_node(load_node, tree_min, tree_max, 0);
    const auto count = reader.value<uint32>();
    if (count > 65535 || uint64(count) * 144 > reader.remaining()) invalid("world model count");
    uint64 allocated = 0;
    std::vector<CServerWorldRec*> model_table;
    for (uint32 i = 0; i < count; ++i) {
        const auto next = reader.value<uint32>();
        uint32 start = 0;
        if (stream.GetPos(&start) != LT_OK || next < start || next > reader.length) invalid("world model offset");
        auto record = std::make_unique<CServerWorldRec>();
        auto geometry = w_ReadWorldModel(stream, true);
        uint32 end = 0;
        if (stream.GetPos(&end) != LT_OK || end > next) invalid("overlapping world models");
        geometry->m_nf8 = static_cast<uint16>(i);
        allocated += geometry->m_nBytes;
        record->m_pWorldBsp = geometry.release(); record->m_Flags = 1;
        if (record->m_pWorldBsp->m_nUnknown48 & 2) {
            if (stream.SeekTo(start) != LT_OK) invalid("original geometry seek");
            auto original = w_ReadWorldModel(stream, false); original->m_nf8 = static_cast<uint16>(i);
            allocated += original->m_nBytes;
            record->m_pOriginal = original.release(); record->m_Flags |= 2;
        }
        if (allocated > 1024ull * 1024 * 1024) invalid("world geometry allocation budget");
        record->m_pCurrent = record->m_pOriginal ? record->m_pOriginal : record->m_pWorldBsp;
        model_table.push_back(record.get()); world->models.push_back(std::move(record));
        if (stream.SeekTo(next) != LT_OK) invalid("next world model seek");
    }
    for (const auto& record : world->models) {
        if (!w_ResolvePolyKeys(*record->m_pWorldBsp, model_table)) invalid("world polygon key");
        wm_ResetGeometry(record->m_pWorldBsp);
        if (record->m_pOriginal) {
            if (!w_ResolvePolyKeys(*record->m_pOriginal, model_table)) invalid("original world polygon key");
            wm_ResetGeometry(record->m_pOriginal);
        }
    }
    return world;
}
const WorldLayoutNode* w_FindLayoutNode(std::span<const WorldLayoutNode> layout, const CWorldSimpleHeader& header) {
    if (layout.empty() || header.m_n04 > 16) return nullptr;
    const std::array<uint8,4> path{header.m_n00,header.m_n01,header.m_n02,header.m_n03};
    uint32 node = 0;
    for (uint32 depth = 0; depth < header.m_n04; ++depth) {
        node = layout[node].children[(path[depth / 4] >> ((depth % 4) * 2)) & 3];
        if (node >= layout.size()) return nullptr;
    }
    return &layout[node];
}
