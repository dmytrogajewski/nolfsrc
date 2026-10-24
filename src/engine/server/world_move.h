#pragma once
#include "ltobject_internal.h"
#include "iltphysics.h"
#include "object_lifetime.h"
#include <vector>
#include <algorithm>
#include <cmath>

class CUnknown210;
class CServerMgr;
struct WMMoveState;
struct MoveObjectReference {
    LTObject* object;
    ObjectCallbackGuard guard;
    explicit MoveObjectReference(LTObject* value) : object(value),guard(*value) {}
};

inline float phys_SupportRadius(const LTVector& dimensions, uint32 flags, uint32 flags2, const LTVector& normal) {
    if (flags & FLAG_POINTCOLLIDE) return 0;
    if (flags2 & FLAG2_PLAYERCOLLIDE)
        return std::fabs(normal.y)*dimensions.y + std::min(dimensions.x,dimensions.z)*std::hypot(normal.x,normal.z);
    return std::fabs(normal.x)*dimensions.x + std::fabs(normal.y)*dimensions.y + std::fabs(normal.z)*dimensions.z;
}

// Physics calls this domain interface on both the server and client. Native
// records and virtual dispatch share one declaration; serialized offsets do
// not describe runtime objects.
class IObjectMover {
public:
    IObjectMover() = default;
    IObjectMover(const IObjectMover&) = delete;
    IObjectMover& operator=(const IObjectMover&) = delete;
    virtual ~IObjectMover() { Retire(); }
    void Retire() noexcept { *lifetime_ = false; }
    std::shared_ptr<const bool> Lifetime() const { return lifetime_; }
    virtual void SetObjectChangeFlags(LTObject*, uint32) = 0;
    virtual CollisionInfo** GetCollisionInfo() = 0;
    virtual void TouchNotify(LTObject*, HOBJECT, LTVector*, float) = 0;
    virtual void Attach(LTObject*, LTObject*) = 0;
    virtual void Detach(LTObject*) = 0;
    virtual void UpdateAttachments(WMMoveState*) = 0;
    virtual int ObjectBlocks(WMMoveState*, LTObject*, LTObject*) = 0;
    virtual uint32 Crush(LTObject*, void*) = 0;
    virtual void CheckMaxPos(WMMoveState*, LTVector*) = 0;
    virtual bool IsServer() const = 0;
    virtual LTBOOL CanPlace(LTObject*) = 0;
    virtual const char* GetObjectClassName(LTObject*) = 0;
    virtual ILTPhysics* Physics() = 0;
private:
    std::shared_ptr<bool> lifetime_ = std::make_shared<bool>(true);
};

struct WMMoveState {
    CUnknown210* m_pContainer = nullptr;
    IObjectMover* m_pUnknown04 = nullptr;
    LTObject* m_pObj = nullptr;
    uint32 m_nUnknown0C = 0;
    LTObject** m_pPushList = nullptr;
    uint32 m_nPushCount = 0;
    // Native callers retain both the explicit list and each object identity
    // across substeps and callbacks. An empty list still restricts the query.
    std::shared_ptr<const std::vector<MoveObjectReference>> m_PushObjects;
    bool m_Server = false;
    LTVector* m_pOrigPos = nullptr;
    LTVector m_vPos{}, m_vMove{}, m_vMid{};
    float m_fRadius = 0;
    LTVector m_vBoxMin{}, m_vBoxMax{};
    LTMatrix* m_pRotation = nullptr;
    uint32 m_nUnknown64 = 0;

    void Setup();
    void Init(CUnknown210*, IObjectMover*, LTObject*, uint32 priority);
    void InitFrom(WMMoveState*, LTObject*);
};

class ServerObjectMover final : public IObjectMover {
public:
    explicit ServerObjectMover(CServerMgr* owner) : m_pMgr(owner) {}
    void SetObjectChangeFlags(LTObject*, uint32) override;
    CollisionInfo** GetCollisionInfo() override;
    void TouchNotify(LTObject*, HOBJECT, LTVector*, float) override;
    void Attach(LTObject*, LTObject*) override;
    void Detach(LTObject*) override;
    void UpdateAttachments(WMMoveState*) override;
    int ObjectBlocks(WMMoveState*, LTObject*, LTObject*) override;
    uint32 Crush(LTObject*, void*) override;
    void CheckMaxPos(WMMoveState*, LTVector*) override;
    bool IsServer() const override { return true; }
    LTBOOL CanPlace(LTObject*) override;
    const char* GetObjectClassName(LTObject*) override;
    ILTPhysics* Physics() override;
private:
    CServerMgr* m_pMgr;
};
