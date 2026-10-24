#include "engine/client/client_physics.h"
#include <cmath>
#include <span>
#include <vector>

void wm_RotateWorldModel(WMMoveState*,LTRotation*,int);
namespace nolf {
namespace {
bool scalar(float value,float minimum=0) { return std::isfinite(value) && value>=minimum && value<=1e12f; }
bool finite(LTVector v) { return scalar(v.x,-1e12f) && scalar(v.y,-1e12f) && scalar(v.z,-1e12f); }
bool rotation(const LTRotation& v) {
    double norm=0;
    for (float component : v.m_Quat) { if (!std::isfinite(component)) return false; norm+=double(component)*component; }
    return std::abs(norm-1)<.001;
}
template<class F> LTRESULT guarded(F&& call) noexcept {
    try { return call(); }
    catch (const std::bad_alloc&) { return LT_OUTOFMEMORY; }
    catch (...) { return LT_ERROR; }
}
}
ClientPhysics::ClientPhysics(ClientPhysicsWorld& world) : world_(world) { m_ClientServerType=ClientType; }
bool ClientPhysics::ValidObject(HOBJECT object) const { return owner_==std::this_thread::get_id() && object && world_.owns(object); }
bool ClientPhysics::movement_state(HOBJECT object,WMMoveState& state) {
    auto* spatial=world_.spatial(); auto* mover=world_.mover();
    if (!spatial || !mover || mover->IsServer()) return false;
    state.Init(spatial,mover,object,object->m_BlockingPriority); return true;
}
LTRESULT ClientPhysics::SetFrictionCoefficient(HOBJECT object,float value) {
    return scalar(value) ? PhysicsProperties::SetFrictionCoefficient(object,value) : LT_INVALIDPARAMS;
}
LTRESULT ClientPhysics::SetForceIgnoreLimit(HOBJECT object,float value) {
    return scalar(value) ? PhysicsProperties::SetForceIgnoreLimit(object,value) : LT_INVALIDPARAMS;
}
LTRESULT ClientPhysics::SetObjectMass(HOBJECT object,float value) {
    return scalar(value) ? PhysicsProperties::SetObjectMass(object,value) : LT_INVALIDPARAMS;
}
LTRESULT ClientPhysics::SetVelocity(HOBJECT object,LTVector* velocity) {
    if (!ValidObject(object) || !velocity || !finite(*velocity)) return LT_INVALIDPARAMS;
    object->m_Velocity=*velocity; object->m_ChangeFlags|=CHANGEFLAG_MOTION; return LT_OK;
}
LTRESULT ClientPhysics::SetAcceleration(HOBJECT object,LTVector* acceleration) {
    if (!ValidObject(object) || !acceleration || !finite(*acceleration)) return LT_INVALIDPARAMS;
    object->m_Acceleration=*acceleration; object->m_ChangeFlags|=CHANGEFLAG_MOTION; return LT_OK;
}
LTRESULT ClientPhysics::GetStandingOn(HOBJECT object,CollisionInfo* info) {
    if (!ValidObject(object) || !info) return LT_INVALIDPARAMS;
    *info=CollisionInfo{}; info->m_hPoly=INVALID_HPOLY;
    return PhysicsProperties::GetStandingOn(object,info);
}
LTRESULT ClientPhysics::SetObjectDims(HOBJECT object,LTVector* dimensions,uint32 flags) {
    if (!ValidObject(object) || !dimensions || !finite(*dimensions) || dimensions->x<0 || dimensions->y<0 || dimensions->z<0 ||
        object->m_ObjectType==OT_WORLDMODEL || object->m_ObjectType==OT_CONTAINER || flags&~SETDIMS_PUSHOBJECTS) return LT_INVALIDPARAMS;
    return guarded([&] {
        const auto keep=world_.retain();
        WMMoveState state; if (!movement_state(object,state)) return LT_NOTINITIALIZED;
        auto accepted=*dimensions;
        if (wm_SetObjectDims(&state,&accepted,flags&SETDIMS_PUSHOBJECTS,1)) return LT_OK;
        *dimensions=accepted; return LT_ERROR;
    });
}
LTRESULT ClientPhysics::MoveObject(HOBJECT object,LTVector* position,uint32 flags) {
    if (!ValidObject(object) || !position || !finite(*position) || flags&~MOVEOBJECT_TELEPORT) return LT_INVALIDPARAMS;
    return guarded([&] {
        const auto keep=world_.retain();
        WMMoveState state; if (!movement_state(object,state)) return LT_NOTINITIALIZED;
        // The client collides without the server's touch/carry bits. Explicit
        // teleport uses the shared worker's destination flag (the retail9).
        wm_MoveObject(&state,*position,(flags&MOVEOBJECT_TELEPORT) ? 9 : 1); return LT_OK;
    });
}
LTRESULT ClientPhysics::GetGlobalForce(LTVector& force) {
    if (owner_!=std::this_thread::get_id()) return LT_ERROR;
    force=force_.m_Force; return LT_OK;
}
LTRESULT ClientPhysics::SetGlobalForce(LTVector& force) {
    if (owner_!=std::this_thread::get_id()) return LT_ERROR;
    if (!finite(force)) return LT_INVALIDPARAMS;
    force_.Set(&force); return LT_OK;
}
LTRESULT ClientPhysics::GetStairHeight(float& height) {
    if (owner_!=std::this_thread::get_id()) return LT_ERROR;
    height=stair_height_; return LT_OK;
}
LTRESULT ClientPhysics::SetStairHeight(float height) {
    if (owner_!=std::this_thread::get_id()) return LT_ERROR;
    if (!scalar(height,-1)) return LT_INVALIDPARAMS;
    stair_height_=height; return LT_OK;
}
LTRESULT ClientPhysics::UpdateMovement(MoveInfo* info) {
    if (!info || !ValidObject(info->m_hObject) || !std::isfinite(info->m_dt) || info->m_dt<0 || info->m_dt>1) return LT_INVALIDPARAMS;
    auto* object=info->m_hObject;
    info->m_Offset.Init();
    if (!info->m_dt || !(object->m_ChangeFlags&CHANGEFLAG_MOTION)) return LT_OK;
    // Per-call requests avoid retaining object/stack pointers in the world's
    // force state and preserve the actual shared movement integrator.
    auto request=force_;
    request.m_pObj=object; request.m_fFrameTime=info->m_dt; request.m_nObjFlags=object->m_Flags;
    request.m_pVelocity=&object->m_Velocity; request.m_pAcceleration=&object->m_Acceleration;
    phys_ComputeFrameMove(&request); info->m_Offset=request.m_vFrameMove; return LT_OK;
}
LTRESULT ClientPhysics::MovePushObjects(HOBJECT object,LTVector& position,HOBJECT* objects,uint32 count) {
    if (!ValidObject(object) || !finite(position) || (count && !objects) || count>65536) return LT_INVALIDPARAMS;
    return guarded([&] {
        const auto keep=world_.retain();
        auto owned=std::make_shared<std::vector<MoveObjectReference>>();
        owned->reserve(count);
        for (auto* other : std::span(objects,count)) {
            if (!ValidObject(other)) return LT_INVALIDPARAMS;
            if (other!=object) owned->emplace_back(other);
        }
        WMMoveState state; if (!movement_state(object,state)) return LT_NOTINITIALIZED;
        state.m_PushObjects=std::move(owned);
        wm_MoveObject(&state,position,0x15); return LT_OK;
    });
}
LTRESULT ClientPhysics::RotatePushObjects(HOBJECT object,LTRotation& orientation,HOBJECT* objects,uint32 count) {
    if (!ValidObject(object) || !rotation(orientation) || (count && !objects) || count>65536 ||
        (object->m_ObjectType!=OT_WORLDMODEL && object->m_ObjectType!=OT_CONTAINER)) return LT_INVALIDPARAMS;
    return guarded([&] {
        const auto keep=world_.retain();
        auto owned=std::make_shared<std::vector<MoveObjectReference>>();
        owned->reserve(count);
        for (auto* other : std::span(objects,count)) {
            if (!ValidObject(other)) return LT_INVALIDPARAMS;
            if (other!=object) owned->emplace_back(other);
        }
        WMMoveState state; if (!movement_state(object,state)) return LT_NOTINITIALIZED;
        state.m_PushObjects=std::move(owned);
        wm_RotateWorldModel(&state,&orientation,1); return LT_OK;
    });
}
}
