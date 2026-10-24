#pragma once
#include "engine/server/commonphysics.h"
#include <thread>

namespace nolf {
// Owned native scene state supplies identities and collision services. No
// reconstructed client-manager layout or server manager crosses this boundary.
class ClientPhysicsWorld {
public:
    virtual ~ClientPhysicsWorld()=default;
    virtual bool owns(HOBJECT) const noexcept=0;
    virtual CUnknown210* spatial() noexcept=0;
    virtual IObjectMover* mover() noexcept=0;
    // A heap-owned scene can retire during a game callback. The movement
    // call retains its state until all collision-stack frames have unwound.
    virtual std::shared_ptr<const void> retain() const { return {}; }
};
class ClientPhysics final : public PhysicsProperties<ILTClientPhysics> {
public:
    explicit ClientPhysics(ClientPhysicsWorld&);
    ClientPhysics(const ClientPhysics&)=delete;
    ClientPhysics& operator=(const ClientPhysics&)=delete;
    LTRESULT SetFrictionCoefficient(HOBJECT,float) override;
    LTRESULT SetForceIgnoreLimit(HOBJECT,float) override;
    LTRESULT SetObjectMass(HOBJECT,float) override;
    LTRESULT SetVelocity(HOBJECT,LTVector*) override;
    LTRESULT SetAcceleration(HOBJECT,LTVector*) override;
    LTRESULT GetStandingOn(HOBJECT,CollisionInfo*) override;
    LTRESULT SetObjectDims(HOBJECT,LTVector*,uint32) override;
    LTRESULT MoveObject(HOBJECT,LTVector*,uint32) override;
    LTRESULT GetGlobalForce(LTVector&) override;
    LTRESULT SetGlobalForce(LTVector&) override;
    LTRESULT GetStairHeight(float&) override;
    LTRESULT SetStairHeight(float) override;
    LTRESULT UpdateMovement(MoveInfo*) override;
    LTRESULT MovePushObjects(HOBJECT,LTVector&,HOBJECT*,uint32) override;
    LTRESULT RotatePushObjects(HOBJECT,LTRotation&,HOBJECT*,uint32) override;
private:
    bool ValidObject(HOBJECT) const override;
    bool movement_state(HOBJECT,WMMoveState&);
    ClientPhysicsWorld& world_;
    const std::thread::id owner_=std::this_thread::get_id();
    GlobalForceControl force_;
    float stair_height_=-1;
};
}
