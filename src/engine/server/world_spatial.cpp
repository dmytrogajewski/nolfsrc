#include "world_spatial.h"
#include "ltobject_internal.h"
#include "worldbsp.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace {
bool finite(const LTVector& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool bounds(const LTVector& a, const LTVector& b) { return finite(a) && finite(b) && a.x <= b.x && a.y <= b.y && a.z <= b.z; }
bool overlap(const LTVector& a, const LTVector& b, const LTVector& c, const LTVector& d) {
    return a.x <= d.x && b.x >= c.x && a.y <= d.y && b.y >= c.y && a.z <= d.z && b.z >= c.z;
}
bool contains(const WorldLayoutNode& node, const LTVector& min, const LTVector& max) {
    return min.x >= node.min.x && min.y >= node.min.y && min.z >= node.min.z &&
        max.x <= node.max.x && max.y <= node.max.y && max.z <= node.max.z;
}
bool segment(const LTVector& from, const LTVector& to, const LTVector& min, const LTVector& max) {
    double first = 0, last = 1;
    for (uint32 axis = 0; axis < 3; ++axis) {
        const double delta = double(to[axis]) - from[axis];
        if (!delta) { if (from[axis] < min[axis] || from[axis] > max[axis]) return false; continue; }
        double a = (double(min[axis]) - from[axis]) / delta, b = (double(max[axis]) - from[axis]) / delta;
        if (a > b) std::swap(a, b);
        first = std::max(first, a); last = std::min(last, b);
        if (first > last) return false;
    }
    return true;
}
}
struct CUnknown210::Impl {
    CWorldBsp* visibility_world = nullptr;
    std::shared_ptr<const WorldVisibility> visibility;
    std::shared_ptr<WorldGeometryLifetime> visibility_lifetime;
    uint64 visibility_generation = 0;
    struct Entry { LTVector min, max; uint32 node; uint64 serial; };
    std::vector<WorldLayoutNode> layout;
    std::vector<std::unordered_set<LTObject*>> lists;
    std::unordered_map<LTObject*, Entry> objects;
    uint64 next_serial = 1;
    uint32 node_for(const LTVector& min, const LTVector& max) const {
        uint32 node = 0;
        for (;;) {
            uint32 child = 0xffffffff;
            for (auto index : layout[node].children)
                if (index != 0xffffffff && contains(layout[index], min, max)) { child = index; break; }
            if (child == 0xffffffff) return node;
            node = child;
        }
    }
};
CUnknown210::CUnknown210() : impl_(std::make_unique<Impl>()) { if (!LoadLayout({})) throw std::bad_alloc(); }
CUnknown210::~CUnknown210() { lifetime_->alive = false; Term(); }
SpatialMembership::~SpatialMembership() { if (tree_) tree_->RemoveObject(object_); }
SpatialMembership& SpatialMembership::operator=(const SpatialMembership& other) {
    if (this != &other && tree_) tree_->RemoveObject(object_);
    return *this;
}
bool CUnknown210::LoadLayout(std::span<const WorldLayoutNode> layout) {
    try {
        auto next = std::make_unique<Impl>(); next->next_serial = impl_->next_serial;
        if (layout.size() > 1000000) return false;
        next->layout.assign(layout.begin(), layout.end());
        next->visibility_world = impl_->visibility_world; next->visibility = impl_->visibility;
        next->visibility_lifetime = impl_->visibility_lifetime; next->visibility_generation = impl_->visibility_generation;
        if (next->layout.empty()) next->layout.push_back({{0,0,0},{0,0,0}});
        std::vector<uint8> parents(next->layout.size());
        for (uint32 i = 0; i < next->layout.size(); ++i) {
            const auto& node = next->layout[i];
            if (!bounds(node.min, node.max)) return false;
            for (auto child : node.children) {
                if (child == 0xffffffff) continue;
                if (child <= i || child >= next->layout.size() || ++parents[child] != 1 ||
                    !contains(node, next->layout[child].min, next->layout[child].max)) return false;
            }
        }
        for (uint32 i = 1; i < parents.size(); ++i) if (parents[i] != 1) return false;
        next->lists.resize(next->layout.size());
        for (auto [object, entry] : impl_->objects) {
            entry.node = next->node_for(entry.min, entry.max);
            next->objects.emplace(object, entry); next->lists[entry.node].insert(object);
        }
        impl_.swap(next); ++lifetime_->epoch; return true;
    } catch (const std::bad_alloc&) { return false; }
}
void CUnknown210::Init(CUnknown390*) { Term(); }
void CUnknown210::Swap(CUnknown210& other) noexcept {
    if (this == &other) return;
    ++lifetime_->epoch; ++other.lifetime_->epoch;
    impl_.swap(other.impl_);
    for (auto& [object, entry] : impl_->objects) object->m_SpatialMembership.tree_ = this;
    for (auto& [object, entry] : other.impl_->objects) object->m_SpatialMembership.tree_ = &other;
}
void CUnknown210::Term() {
    ++lifetime_->epoch;
    for (auto& [object, entry] : impl_->objects) { object->m_SpatialMembership.tree_ = nullptr; object->m_SpatialMembership.object_ = nullptr; }
    impl_->objects.clear(); for (auto& list : impl_->lists) list.clear();
    impl_->visibility_world = nullptr; impl_->visibility.reset(); impl_->visibility_lifetime.reset();
}
void CUnknown210::RemoveObject(LTObject* object) {
    auto found = impl_->objects.find(object);
    if (found == impl_->objects.end()) return;
    impl_->lists[found->second.node].erase(object); impl_->objects.erase(found);
    object->m_SpatialMembership.tree_ = nullptr; object->m_SpatialMembership.object_ = nullptr;
}
void CUnknown210::BumpObject(LTObject* object, int) {
    if (!object) return;
    if (object->m_SpatialMembership.tree_ && object->m_SpatialMembership.tree_ != this)
        object->m_SpatialMembership.tree_->RemoveObject(object);
    if (!bounds(object->m_BBoxMin, object->m_BBoxMax)) { RemoveObject(object); return; }
    const auto node = impl_->node_for(object->m_BBoxMin, object->m_BBoxMax);
    auto found = impl_->objects.find(object);
    if (found == impl_->objects.end()) {
        auto [entry, inserted] = impl_->objects.emplace(object, Impl::Entry{object->m_BBoxMin, object->m_BBoxMax, node, impl_->next_serial++});
        try { impl_->lists[node].insert(object); } catch (...) { impl_->objects.erase(entry); throw; }
    } else {
        if (found->second.node != node) {
            impl_->lists[node].insert(object); impl_->lists[found->second.node].erase(object);
        }
        found->second.min = object->m_BBoxMin; found->second.max = object->m_BBoxMax; found->second.node = node;
    }
    object->m_SpatialMembership.tree_ = this; object->m_SpatialMembership.object_ = object;
}
std::size_t CUnknown210::Size() const { return impl_->objects.size(); }
void CUnknown210::QueryBox(const LTVector& min, const LTVector& max, const Callback& callback) {
    if (!callback || !bounds(min, max)) return;
    std::vector<std::pair<LTObject*, uint64>> matches;
    const auto lifetime = lifetime_;
    const auto epoch = lifetime->epoch;
    std::vector<uint32> pending{0};
    while (!pending.empty()) {
        const auto node = pending.back(); pending.pop_back();
        for (auto* object : impl_->lists[node]) {
            const auto& entry = impl_->objects.at(object);
            if (overlap(min, max, entry.min, entry.max)) matches.emplace_back(object, entry.serial);
        }
        for (auto child : impl_->layout[node].children)
            if (child != 0xffffffff && overlap(min, max, impl_->layout[child].min, impl_->layout[child].max)) pending.push_back(child);
    }
    for (const auto [object, serial] : matches) {
        if (!lifetime->alive || lifetime->epoch != epoch) break;
        auto found = impl_->objects.find(object);
        if (found != impl_->objects.end() && found->second.serial == serial) callback(object);
    }
}
void CUnknown210::QueryPoint(const LTVector& point, const Callback& callback) { QueryBox(point, point, callback); }
void CUnknown210::QuerySegment(const LTVector& from, const LTVector& to, const Callback& callback) {
    if (!callback || !finite(from) || !finite(to)) return;
    const auto start = from, end = to;
    const LTVector min(std::min(start.x,end.x),std::min(start.y,end.y),std::min(start.z,end.z));
    const LTVector max(std::max(start.x,end.x),std::max(start.y,end.y),std::max(start.z,end.z));
    QueryBox(min, max, [&](LTObject* object) {
        const auto& entry = impl_->objects.at(object);
        if (segment(start, end, entry.min, entry.max)) callback(object);
    });
}
void CUnknown210::FindObjectsInBox(LTVector* min, LTVector* max, int (*callback)(LTObject*, void*), void* user, int) {
    if (min && max && callback) QueryBox(*min, *max, [&](LTObject* object) { callback(object, user); });
}
void CUnknown210::FindPointContainers(LTVector* point, int (*callback)(LTObject*, void*), void* user, int) {
    if (point && callback) QueryPoint(*point, [&](LTObject* object) { callback(object, user); });
}
bool CUnknown210::Contains(LTObject* object) const { return impl_->objects.contains(object); }
bool CUnknown210::BindVisibility(CWorldBsp* world) {
    if (!world) {
        impl_->visibility_world = nullptr; impl_->visibility.reset(); impl_->visibility_lifetime.reset();
        ++lifetime_->epoch; return true;
    }
    try {
        if (!world->m_GeometryLifetime->alive || !world->m_GeometryLifetime->available) return false;
        auto graph = world->m_Visibility ? world->m_Visibility : WorldVisibility::Build(*world);
        impl_->visibility_world = world; impl_->visibility = std::move(graph);
        impl_->visibility_lifetime = world->m_GeometryLifetime;
        impl_->visibility_generation = world->m_GeometryLifetime->generation;
        ++lifetime_->epoch; return true;
    } catch (const std::exception&) { return false; }
}
void CUnknown210::FindObjects(CVisQuery* input) {
    if (!input || !input->m_Fn1 || !std::isfinite(input->m_Radius) || input->m_Radius < 0) return;
    const auto query = *input;
    const auto lifetime = lifetime_; const auto epoch = lifetime->epoch;
    const auto graph = impl_->visibility;
    const auto geometry_lifetime = impl_->visibility_lifetime;
    const auto geometry_generation = impl_->visibility_generation;
    auto valid = [&] {
        return lifetime->alive && lifetime->epoch == epoch && (!graph ||
            geometry_lifetime->Matches(geometry_generation));
    };
    WorldVisibility::Selection selection;
    if (graph) {
        if (!valid()) return;
        const auto* world = impl_->visibility_world;
        std::vector<uint16> portals(world->m_nChunks);
        for (uint32 i = 0; i < world->m_nChunks; ++i) portals[i] = world->m_pChunks[i].m_Flags;
        selection = graph->Select(query.m_Position,portals,[&](uint16 portal) {
            return valid() && (!query.m_PortalFilter || query.m_PortalFilter(portal));
        });
        if (!valid()) return;
    }
    QueryBox(query.m_Position - query.m_Radius,query.m_Position + query.m_Radius,[&](LTObject* object) {
        if (valid() && (!graph || graph->Intersects(selection,object->m_Pos,object->m_Radius))) query.m_Fn1(object,query.m_pUser);
    });
}
