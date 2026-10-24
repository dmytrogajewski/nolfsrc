#pragma once
#include "world_lifetime.h"
#include "ltbasedefs.h"
#include "ltplane.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

class CWorldBsp;
// Immutable native topology and owned encoded PVS data. Selection has no
// global cursor/visit marks and remains valid across nested world queries.
class WorldVisibility {
public:
    struct Selection { std::vector<uint8> groups; bool unrestricted = false; };
    using PortalFilter = std::function<bool(uint16)>;
    static std::shared_ptr<const WorldVisibility> Build(const CWorldBsp&);
    static std::size_t EstimateMemory(const CWorldBsp&);
    uint32 GroupAt(LTVector) const;
    Selection Select(LTVector, std::span<const uint16> portal_flags, const PortalFilter& = {}) const;
    bool Intersects(const Selection&, LTVector center, float radius) const;
    std::size_t GroupCount() const { return regions_.size(); }
    std::size_t EntryCount() const { return entries_.size(); }
    std::size_t MemoryBytes() const;
    // Exposed for file validation/corpus checks, independently of live objects.
    std::vector<uint8> Decode(uint32 entry) const;
private:
    static constexpr uint32 terminal = UINT32_MAX;
    struct Node { LTPlane plane; std::array<uint32,2> children; uint16 group; };
    struct Region { uint32 first = 0; uint16 count = 0; };
    struct Entry { uint16 portal = 0; std::vector<std::byte> bytes; };
    std::vector<Node> nodes_;
    std::vector<Region> regions_;
    std::vector<Entry> entries_;
    uint32 root_ = terminal;
    bool shared_nodes_ = false;
    void DecodeInto(uint32 entry, std::vector<uint8>&) const;
};



class LTObject;
struct CWorldGroupQT1;
struct CWorldVisQuery {
    LTVector position{0,0,0};
    std::span<LTObject* const> candidates;
    std::function<void(LTObject&)> object;
    std::function<void(const CWorldGroupQT1&)> region;
    WorldVisibility::PortalFilter portal;
};
