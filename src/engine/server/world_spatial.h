#pragma once
#include "world_loader.h"
#include "world_visibility.h"
#include <functional>
#include <memory>

class LTObject;
class CUnknown390;
class CUnknown210;
class SpatialMembership {
public:
    SpatialMembership() = default;
    SpatialMembership(const SpatialMembership&) {}
    SpatialMembership& operator=(const SpatialMembership&);
    ~SpatialMembership();
private:
    friend class CUnknown210;
    CUnknown210* tree_ = nullptr;
    LTObject* object_ = nullptr;
};
struct LTLink;
struct CVisQuery {
    LTVector m_Position{0,0,0};
    float m_Radius = 0;
    void (*m_Fn1)(LTObject*, void*) = nullptr;
    void* m_pUser = nullptr;
    WorldVisibility::PortalFilter m_PortalFilter;
};

// The world owns its index; native objects detach through their membership.
// Query callbacks may move/remove objects, replace the layout or run queries.
class CUnknown210 final {
public:
    using Callback = std::function<void(LTObject*)>;
    CUnknown210();
    ~CUnknown210();
    CUnknown210(const CUnknown210&) = delete;
    CUnknown210& operator=(const CUnknown210&) = delete;
    bool LoadLayout(std::span<const WorldLayoutNode>);
    void Swap(CUnknown210&) noexcept;
    void Init(CUnknown390*);
    void Term();
    void BumpObject(LTObject*, int flags);
    void RemoveObject(LTObject*);
    bool Contains(LTObject*) const;
    bool BindVisibility(CWorldBsp*);
    std::size_t Size() const;
    void QueryBox(const LTVector& min, const LTVector& max, const Callback&);
    void QueryPoint(const LTVector&, const Callback&);
    void QuerySegment(const LTVector& from, const LTVector& to, const Callback&);
    void FindObjectsInBox(LTVector*, LTVector*, int (*)(LTObject*, void*), void*, int);
    void FindPointContainers(LTVector*, int (*)(LTObject*, void*), void*, int);
    void FindObjects(CVisQuery*);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    struct QueryLifetime { uint64 epoch = 0; bool alive = true; };
    std::shared_ptr<QueryLifetime> lifetime_ = std::make_shared<QueryLifetime>();
};
using WorldSpatialTree = CUnknown210;
