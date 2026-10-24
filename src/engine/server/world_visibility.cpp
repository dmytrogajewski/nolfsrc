#include "world_visibility.h"
#include "worldbsp.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
[[noreturn]] void invalid(const char* reason) { throw std::runtime_error(std::string("invalid world visibility: ") + reason); }
bool finite(LTVector v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
template<class T> uint32 index_of(const T* pointer, const T* base, uint32 count) {
    const auto address = reinterpret_cast<std::uintptr_t>(pointer), start = reinterpret_cast<std::uintptr_t>(base);
    if (!pointer || !base || address < start || (address - start) % sizeof(T) || (address - start) / sizeof(T) >= count) invalid("native record index");
    return static_cast<uint32>((address - start) / sizeof(T));
}
}
std::size_t WorldVisibility::EstimateMemory(const CWorldBsp& world) {
    if ((world.m_n84 && !world.m_p80) || world.m_n74 > 1000000 || world.m_n8c > 65535 || world.m_n84 > 1000000) invalid("table budget");
    uint64 size = sizeof(WorldVisibility) + uint64(world.m_n74)*sizeof(Node) + uint64(world.m_n8c)*sizeof(Region) + uint64(world.m_n84)*sizeof(Entry);
    for (uint32 i = 0; i < world.m_n84; ++i) size += world.m_p80[i].m_nBytes;
    if (size > 512u*1024*1024) invalid("native memory budget");
    return static_cast<std::size_t>(size);
}
std::shared_ptr<const WorldVisibility> WorldVisibility::Build(const CWorldBsp& world) {
    EstimateMemory(world);
    auto result = std::make_shared<WorldVisibility>();
    if ((world.m_n74 && !world.m_n70) || (world.m_n8c && !world.m_p88) || (world.m_n84 && !world.m_p80)) invalid("missing tables");
    if (world.m_n74 > 1000000 || world.m_n8c > 65535 || world.m_n84 > 1000000) invalid("table budget");
    auto node_index = [&](const WorldPolyRef* node) {
        if (node == g_pWorldTreeFrontTerm || node == g_pWorldTreeBackTerm || !node) return terminal;
        return index_of(node,world.m_n70,world.m_n74);
    };
    result->nodes_.reserve(world.m_n74);
    for (uint32 i = 0; i < world.m_n74; ++i) {
        const auto& node = world.m_n70[i];
        if (!node.m_pOwner || !node.m_pOwner->m_pSurface || !node.m_pOwner->m_pSurface->m_pPlane) invalid("missing node plane");
        const auto& plane = *node.m_pOwner->m_pSurface->m_pPlane;
        if (!finite(plane.m_Normal) || !std::isfinite(plane.m_Dist) ||
            (node.m_x != 0xffff && node.m_x >= world.m_n8c)) invalid("plane/group index");
        result->nodes_.push_back({plane,{node_index(node.m_Children[0]),node_index(node.m_Children[1])},node.m_x});
    }
    result->root_ = node_index(world.m_n9c);
    std::vector<uint32> parents(result->nodes_.size());
    for (const auto& node : result->nodes_) for (auto child : node.children)
        if (child != terminal && ++parents[child] > 1) result->shared_nodes_ = true;
    // Validate manually constructed worlds too; the file loader already
    // checks this graph, but queries never rely on an unchecked recursion.
    std::vector<uint8> color(result->nodes_.size());
    for (uint32 start = 0; start < result->nodes_.size(); ++start) {
        if (color[start]) continue;
        std::vector<std::pair<uint32,bool>> pending{{start,false}};
        while (!pending.empty()) {
            auto [node,leaving] = pending.back(); pending.pop_back();
            if (leaving) { color[node] = 2; continue; }
            if (color[node] == 1) invalid("node cycle");
            if (color[node] == 2) continue;
            color[node] = 1; pending.push_back({node,true});
            for (auto child : result->nodes_[node].children) if (child != terminal) pending.push_back({child,false});
        }
    }
    result->entries_.resize(world.m_n84);
    for (uint32 i = 0; i < world.m_n84; ++i) {
        const auto& source = world.m_p80[i]; auto& entry = result->entries_[i]; entry.portal = source.m_n00;
        if (source.m_nBytes) {
            const auto pointer = reinterpret_cast<std::uintptr_t>(source.m_pData), base = reinterpret_cast<std::uintptr_t>(world.m_p104);
            if (!source.m_pData || !world.m_p104 || pointer < base || pointer - base > world.m_n98 || source.m_nBytes > world.m_n98 - (pointer - base)) invalid("PVS byte extent");
            const auto* bytes = static_cast<const std::byte*>(source.m_pData);
            entry.bytes.assign(bytes,bytes + source.m_nBytes);
        }
    }
    result->regions_.reserve(world.m_n8c);
    std::vector<uint8> checked(world.m_n84);
    for (uint32 i = 0; i < world.m_n8c; ++i) {
        const auto& group = world.m_p88[i]; Region region;
        if (group.m_nEntries) {
            region.first = index_of(group.m_pEntries,world.m_p80,world.m_n84); region.count = group.m_nEntries;
            if (region.count > world.m_n84 - region.first) invalid("region entry extent");
        }
        result->regions_.push_back(region);
    }
    for (const auto& region : result->regions_) for (uint32 i = 0; i < region.count; ++i) {
        const auto entry = region.first + i;
        if (i && result->entries_[entry].portal >= world.m_nChunks) invalid("portal index");
        if (!checked[entry]) { result->Decode(entry); checked[entry] = 1; }
    }
    return result;
}
void WorldVisibility::DecodeInto(uint32 entry_index, std::vector<uint8>& output) const {
    if (entry_index >= entries_.size() || output.size() != regions_.size()) invalid("decode arguments");
    const auto& bytes = entries_[entry_index].bytes;
    std::size_t bit = 0;
    auto read = [&] {
        if (bit / 8 >= bytes.size()) invalid("truncated PVS bitstream");
        const bool result = (std::to_integer<unsigned>(bytes[bit / 8]) & (1u << (bit & 7))) != 0;
        ++bit; return result;
    };
    struct Frame { uint32 node; uint8 phase; };
    std::vector<Frame> stack;
    if (root_ != terminal) stack.push_back({root_,0});
    while (!stack.empty()) {
        auto& frame = stack.back(); const auto& node = nodes_[frame.node];
        if (!frame.phase) { if (node.group != 0xffff && read()) output[node.group] = 1; ++frame.phase; }
        else if (frame.phase <= 2) {
            const auto child = node.children[frame.phase++ - 1];
            if (child != terminal && read()) stack.push_back({child,0});
        } else stack.pop_back();
    }
}
std::vector<uint8> WorldVisibility::Decode(uint32 entry) const {
    std::vector<uint8> output(regions_.size()); DecodeInto(entry,output); return output;
}
uint32 WorldVisibility::GroupAt(LTVector position) const {
    if (!finite(position)) return terminal;
    uint32 group = terminal;
    for (auto index = root_; index != terminal;) {
        const auto& node = nodes_[index];
        if (node.group != 0xffff) group = node.group;
        index = node.children[node.plane.m_Normal.Dot(position) - node.plane.m_Dist >= -.0001f ? 1 : 0];
    }
    return group;
}
WorldVisibility::Selection WorldVisibility::Select(LTVector position, std::span<const uint16> portals, const PortalFilter& filter) const {
    if (!finite(position)) invalid("nonfinite query position");
    Selection result; result.groups.resize(regions_.size());
    const auto origin = GroupAt(position);
    if (origin == terminal || !regions_[origin].count) {
        std::fill(result.groups.begin(),result.groups.end(),1); result.unrestricted = true; return result;
    }
    const auto region = regions_[origin];
    for (uint32 i = 0; i < region.count; ++i) {
        const auto entry = region.first + i;
        if (i) {
            const auto portal = entries_[entry].portal;
            if (portal >= portals.size()) invalid("query portal extent");
            if (!(portals[portal] & 1) || (filter && !filter(portal))) continue;
        }
        DecodeInto(entry,result.groups);
    }
    return result;
}
bool WorldVisibility::Intersects(const Selection& selection, LTVector center, float radius) const {
    if (selection.groups.size() != regions_.size() || !finite(center) || !std::isfinite(radius) || radius < 0) return false;
    if (selection.unrestricted) return true;
    std::vector<uint32> pending;
    std::vector<uint8> seen(shared_nodes_ ? nodes_.size() : 0);
    if (root_ != terminal) pending.push_back(root_);
    std::size_t visited = 0;
    while (!pending.empty()) {
        if (++visited > 4 * nodes_.size() + 1) invalid("sphere traversal budget");
        const auto index = pending.back(); pending.pop_back();
        if (!seen.empty()) { if (seen[index]) continue; seen[index] = 1; }
        const auto& node = nodes_[index];
        if (node.group != 0xffff && selection.groups[node.group]) return true;
        const auto distance = node.plane.m_Normal.Dot(center) - node.plane.m_Dist;
        if (node.children[0] != terminal && distance < radius + .1f) pending.push_back(node.children[0]);
        if (node.children[1] != terminal && distance > -(radius + .1f)) pending.push_back(node.children[1]);
    }
    return false;
}
std::size_t WorldVisibility::MemoryBytes() const {
    std::size_t size = sizeof(*this) + nodes_.size()*sizeof(Node) + regions_.size()*sizeof(Region) + entries_.size()*sizeof(Entry);
    for (const auto& entry : entries_) size += entry.bytes.size();
    return size;
}
