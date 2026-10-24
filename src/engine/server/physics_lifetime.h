#pragma once
#include "world_move.h"
#include "object_lifetime.h"

// Callback code may remove either endpoint, reuse its storage, or destroy
// the simulation owner. These tokens remain valid independently of all three.
class MoveCallbackGuard {
public:
    MoveCallbackGuard(IObjectMover& source, LTObject& object)
        : source_(source.Lifetime()), object_(object) {}
    bool unchanged() const { return *source_ && object_.unchanged(); }
private:
    std::shared_ptr<const bool> source_;
    ObjectCallbackGuard object_;
};

class CollisionScope {
public:
    explicit CollisionScope(IObjectMover& source)
        : source_(&source), lifetime_(source.Lifetime()), previous_(*source.GetCollisionInfo()) {
        *source.GetCollisionInfo() = &current_;
    }
    ~CollisionScope() { if (*lifetime_) *source_->GetCollisionInfo() = previous_; }
private:
    IObjectMover* source_;
    std::shared_ptr<const bool> lifetime_;
    CollisionInfo* previous_;
    CollisionInfo current_{};
};

class MovePairGuard {
public:
    MovePairGuard(IObjectMover& source, LTObject& first, LTObject& second)
        : first_(source,first), second_(second) {}
    bool unchanged() const { return first_.unchanged() && second_.unchanged(); }
private:
    MoveCallbackGuard first_;
    ObjectCallbackGuard second_;
};

struct StandingObject {
    LTObject* m_pObj;
    LTVector m_vOffset;
    ObjectCallbackGuard guard;
    StandingObject(LTObject* object, LTVector offset) : m_pObj(object), m_vOffset(offset), guard(*object) {}
};

class MovingObjectScope {
public:
    explicit MovingObjectScope(LTObject& object) : object_(&object), guard_(object) { object.m_ChangeFlags |= 2; }
    ~MovingObjectScope() { if (guard_.same_data()) object_->m_ChangeFlags &= ~2u; }
private:
    LTObject* object_;
    ObjectCallbackGuard guard_;
};
