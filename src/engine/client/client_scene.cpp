#include "engine/client/client_scene.h"
#include "engine/client/client_physics.h"
#include "engine/client/client_sprites.h"
#include "engine/client/client_effects.h"
#include "engine/server/world_line_query.h"
#include "engine/client/camera_view.h"
#include "engine/server/sharedcommon.h"
#include "engine/server/commonmodel.h"
#include "engine/server/model_cache.h"
#include "engine/server/model_callback.h"
#include "engine/server/light_anim_properties.h"
#include "engine/server/objtypes.h"
#include "engine/server/object_manager.h"
#include "engine/server/container_query.h"
#include "engine/server/world_state.h"
#include "engine/server/worldbsp.h"
#include "engine/server/worldsim.h"
#include "engine/server/command_parser.h"
#include "engine/portable/asset_stream.h"
#include "ilttransform.h"
#include "ltquatbase.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <map>
#include <set>
#include <thread>
#include <cstring>
#include <numbers>
#include <limits>

namespace nolf {
namespace {
class Failure : public std::runtime_error {
public:
    Failure(LTRESULT code,const char* message) : std::runtime_error(message),code(code) {}
    LTRESULT code;
};
void check(bool condition,LTRESULT result,const char* message) { if (!condition) throw Failure(result,message); }
template<class F> LTRESULT operation(F&& f) noexcept {
    try { return f(); }
    catch (const Failure& error) { return error.code; }
    catch (const std::bad_alloc&) { return LT_OUTOFMEMORY; }
    catch (const std::invalid_argument&) { return LT_INVALIDPARAMS; }
    catch (...) { return LT_ERROR; }
}
bool finite(LTVector v) {
    for (uint32 axis=0;axis<3;++axis) if (!std::isfinite(v[axis]) || std::fabs(v[axis])>1e12f) return false;
    return true;
}
bool valid_rotation(const LTRotation& value) {
    double norm=0;
    for (float part : value.m_Quat) { if (!std::isfinite(part)) return false; norm+=double(part)*part; }
    // Keyframers add component offsets to their keys, so valid authored
    // rotations need not have unit length. Quaternion consumers normalize
    // when building a matrix; preserve the raw values for later offsets.
    return norm>0;
}
template<std::size_t N> std::string text(const char (&value)[N]) {
    const auto end=std::find(std::begin(value),std::end(value),'\0');
    check(end!=std::end(value),LT_INVALIDPARAMS,"unterminated object filename");
    return {std::begin(value),end};
}
}
struct ClientScene::State final : ClientPhysicsWorld, std::enable_shared_from_this<State> {
    struct Record {
        ClientObjectInfo info;
        ClientCamera camera;
        std::shared_ptr<CServerWorld> geometry;
        std::shared_ptr<SpriteResource> sprite;
    };
    static thread_local std::shared_ptr<State> current;
    SkyInfo sky;
    struct CallbackScope {
        std::shared_ptr<State> previous,active;
        std::shared_ptr<const void> code;
        explicit CallbackScope(State& owner) : previous(current),active(owner.shared_from_this()) { owner.thread(); code=owner.callbacks.code; current=active; }
        ~CallbackScope() { current=std::move(previous); }
    };
    bool live() const { return !retired; }
    void thread() const { check(owner==std::this_thread::get_id(),LT_ERROR,"client scene used from another thread"); }
    void ready() const { thread(); check(live(),LT_NOTINITIALIZED,"client scene retired"); }
    template<class F> LTRESULT dispatch(F&& call) { return operation([&]() -> LTRESULT { CallbackScope scope(*this); return call(); }); }
    template<class F> static LTRESULT sdk_result(F&& call) noexcept {
        const auto self=current;
        return operation([&]() -> LTRESULT {
            check(bool(self),LT_NOTINITIALIZED,"client scene SDK call outside callback scope");
            return self->dispatch([&] { self->ready(); return call(*self); });
        });
    }
    template<class T,class F> static T sdk_value(F&& call,T fallback={}) noexcept {
        T output=fallback;
        sdk_result([&](State& self) -> LTRESULT { output=call(self); return LT_OK; });
        return output;
    }
    static const SceneFunctions& functions();
    bool owns(HOBJECT object) const noexcept override {
        return owner==std::this_thread::get_id() && live() && object && records.contains(object);
    }
    Record& get(HOBJECT object) { ready(); const auto it=records.find(object); check(it!=records.end(),LT_INVALIDPARAMS,"foreign client object"); return it->second; }
    bool owns_model(HOBJECT object) const { return owns(object) && object->m_ObjectType==OT_MODEL; }
    bool owns_tracker(LTAnimTracker* tracker) const {
        if (owner!=std::this_thread::get_id() || !live()) return false;
        const auto found=trackers.find(tracker);
        return found!=trackers.end() && owns_model(found->second);
    }
    CUnknown210* spatial() noexcept override { return live() ? &index : nullptr; }
    IObjectMover* mover() noexcept override { return live() ? &movement : nullptr; }
    std::shared_ptr<const void> retain() const override { return shared_from_this(); }

    struct ModelInterface final : CommonModelLT {
        State& owner;
        explicit ModelInterface(State& state) : owner(state) {}
#define OBJECT_MODEL(method,signature,args) LTRESULT method signature override { return owner.dispatch([&]() -> LTRESULT { if (!owner.owns_model(object)) return LT_INVALIDPARAMS; return CommonModelLT::method args; }); }
        OBJECT_MODEL(GetSocket,(HOBJECT object,char* name,HMODELSOCKET& out),(object,name,out))
        OBJECT_MODEL(GetNode,(HOBJECT object,char* name,HMODELNODE& out),(object,name,out))
        OBJECT_MODEL(GetPiece,(HOBJECT object,char* name,HMODELPIECE& out),(object,name,out))
        OBJECT_MODEL(FindWeightSet,(HOBJECT object,char* name,HMODELWEIGHTSET& out),(object,name,out))
        OBJECT_MODEL(GetPieceHideStatus,(HOBJECT object,HMODELPIECE piece,LTBOOL& out),(object,piece,out))
        OBJECT_MODEL(SetPieceHideStatus,(HOBJECT object,HMODELPIECE piece,LTBOOL hidden),(object,piece,hidden))
        OBJECT_MODEL(GetNodeTransform,(HOBJECT object,HMODELNODE node,LTransform& out,LTBOOL world),(object,node,out,world))
        OBJECT_MODEL(GetSocketTransform,(HOBJECT object,HMODELSOCKET socket,LTransform& out,LTBOOL world),(object,socket,out,world))
        OBJECT_MODEL(GetInstanceHookFn,(HOBJECT object,ModelInstanceHookFn& out),(object,out))
        OBJECT_MODEL(SetInstanceHookFn,(HOBJECT object,ModelInstanceHookFn fn),(object,fn))
        OBJECT_MODEL(GetMainTracker,(HOBJECT object,LTAnimTracker*& out),(object,out))
#undef OBJECT_MODEL
#define TRACKER_MODEL(method,signature,args) LTRESULT method signature override { return owner.dispatch([&]() -> LTRESULT { if (!owner.owns_tracker(tracker)) return LT_INVALIDPARAMS; return CommonModelLT::method args; }); }
        TRACKER_MODEL(GetPlaybackState,(LTAnimTracker* tracker,uint32& flags),(tracker,flags))
        TRACKER_MODEL(GetCurAnim,(LTAnimTracker* tracker,HMODELANIM& anim),(tracker,anim))
        TRACKER_MODEL(SetCurAnim,(LTAnimTracker* tracker,HMODELANIM anim),(tracker,anim))
        TRACKER_MODEL(ResetAnim,(LTAnimTracker* tracker),(tracker))
        TRACKER_MODEL(GetLooping,(LTAnimTracker* tracker),(tracker))
        TRACKER_MODEL(SetLooping,(LTAnimTracker* tracker,LTBOOL loop),(tracker,loop))
        TRACKER_MODEL(GetPlaying,(LTAnimTracker* tracker),(tracker))
        TRACKER_MODEL(SetPlaying,(LTAnimTracker* tracker,LTBOOL playing),(tracker,playing))
        TRACKER_MODEL(GetWeightSet,(LTAnimTracker* tracker,HMODELWEIGHTSET& weight),(tracker,weight))
        TRACKER_MODEL(SetWeightSet,(LTAnimTracker* tracker,HMODELWEIGHTSET weight),(tracker,weight))
        TRACKER_MODEL(GetCurAnimLength,(LTAnimTracker* tracker,uint32& length),(tracker,length))
        TRACKER_MODEL(GetCurAnimTime,(LTAnimTracker* tracker,uint32& time),(tracker,time))
        TRACKER_MODEL(SetCurAnimTime,(LTAnimTracker* tracker,uint32 time),(tracker,time))
#undef TRACKER_MODEL
        LTRESULT UpdateMainTracker(HOBJECT object,float elapsed) override {
            return owner.dispatch([&]() -> LTRESULT {
            if (!owner.owns_model(object) || !std::isfinite(elapsed) || elapsed<0 || elapsed>1) return LT_INVALIDPARAMS;
            static_cast<ModelInstance*>(object)->m_AnimTracker.m_StringKeyCallback=State::model_key;
            return CommonModelLT::UpdateMainTracker(object,elapsed);
            });
        }
        LTRESULT AddTracker(HOBJECT object,LTAnimTracker* tracker) override {
            return owner.dispatch([&]() -> LTRESULT {
            if (!owner.owns_model(object) || !tracker) return LT_INVALIDPARAMS;
            if (owner.trackers.contains(tracker)) return LT_ALREADYEXISTS;
            if (tracker->GetModelInstance() && tracker->GetModelInstance()!=object) return LT_INVALIDPARAMS;
                auto [entry,inserted]=owner.trackers.emplace(tracker,object);
                const auto result=CommonModelLT::AddTracker(object,tracker);
                if (result!=LT_OK) owner.trackers.erase(entry);
                else tracker->m_StringKeyCallback=State::model_key;
                return result;
            });
        }
        LTRESULT RemoveTracker(HOBJECT object,LTAnimTracker* tracker) override {
            return owner.dispatch([&]() -> LTRESULT {
            if (!owner.owns_model(object) || !owner.owns_tracker(tracker) || owner.trackers.at(tracker)!=object) return LT_INVALIDPARAMS;
            const auto result=CommonModelLT::RemoveTracker(object,tracker);
            if (result==LT_OK) owner.trackers.erase(tracker);
            return result;
            });
        }
    } model_api{*this};

    struct CommonInterface final : SharedCommonLT {
        State& owner;
        explicit CommonInterface(State& state) : owner(state) { SetMath(&state.math); m_pTransformLT=&state.transform; m_pModelLT=&state.model_api; }
        LTRESULT SetObjectFilenames(HOBJECT object,ObjectCreateStruct* create) override {
            if (!create) return LT_INVALIDPARAMS;
            return owner.dispatch([&] { return owner.filenames(object,*create); });
        }
        LTRESULT GetObjectFlags(HOBJECT object,ObjFlagType type,uint32& flags) override {
            return owner.dispatch([&] { return owner.owns(object) ? SharedCommonLT::GetObjectFlags(object,type,flags) : LT_INVALIDPARAMS; });
        }
        LTRESULT SetObjectFlags(HOBJECT object,ObjFlagType type,uint32 flags) override {
            return owner.dispatch([&]() -> LTRESULT { if (!owner.owns(object)) return LT_INVALIDPARAMS;
            const auto result=SharedCommonLT::SetObjectFlags(object,type,flags);
            if (result==LT_OK) owner.records.at(object).info.dirty|=1;
            return result; });
        }
        LTRESULT GetObjectType(HOBJECT object,uint32* type) override {
            return owner.dispatch([&] { return owner.owns(object) ? SharedCommonLT::GetObjectType(object,type) : LT_INVALIDPARAMS; });
        }
        LTRESULT GetModelAnimUserDims(HOBJECT object,LTVector* dims,HMODELANIM anim) override {
            return owner.dispatch([&] { return owner.owns_model(object) ? SharedCommonLT::GetModelAnimUserDims(object,dims,anim) : LT_INVALIDPARAMS; });
        }
        LTRESULT GetAttachments(HOBJECT object,HOBJECT* list,uint32 capacity,uint32& size,uint32& total) override {
            size=total=0; return owner.dispatch([&] {
            return owner.owns(object) ? SharedCommonLT::GetAttachments(object,list,capacity,size,total) : LT_INVALIDPARAMS; });
        }
        LTRESULT GetAttachmentObjects(HATTACHMENT handle,HOBJECT& parent,HOBJECT& child) override {
            parent=child=nullptr; return owner.dispatch([&]() -> LTRESULT {
            if (!owner.live()) return LT_NOTINITIALIZED;
            const auto* record=am_FindAttachment(&owner.objects,handle); if (!record) return LT_INVALIDPARAMS;
            parent=record->m_pParent; child=record->m_pChild; return LT_OK; });
        }
        LTRESULT GetAttachmentTransform(HATTACHMENT attachment,LTransform& out,LTBOOL world) override {
            return operation([&] { CallbackScope scope(owner); return SharedCommonLT::GetAttachmentTransform(attachment,out,world); });
        }
        LTRESULT GetAttachedModelNodeTransform(HATTACHMENT attachment,HMODELNODE node,LTransform& out) override {
            return operation([&] { CallbackScope scope(owner); return SharedCommonLT::GetAttachedModelNodeTransform(attachment,node,out); });
        }
        LTRESULT GetAttachedModelSocketTransform(HATTACHMENT attachment,HMODELSOCKET socket,LTransform& out) override {
            return operation([&] { CallbackScope scope(owner); return SharedCommonLT::GetAttachedModelSocketTransform(attachment,socket,out); });
        }
        LTRESULT GetPointStatus(LTVector* point) override {
            return owner.dispatch([&]() -> LTRESULT {
            if (!point || !finite(*point)) return LT_INVALIDPARAMS;
            if (!owner.world || !owner.live()) return LT_NOTINWORLD;
            auto* geometry=owner.world->FindFlagged10();
            return geometry && bsp_PointInside(geometry->GetRoot(),point) ? LT_INSIDE : LT_OUTSIDE; });
        }
        LTRESULT GetPointShade(LTVector* point,LTVector* color) override {
            return owner.dispatch([&]() -> LTRESULT {
            if (!point || !color || !finite(*point)) return LT_INVALIDPARAMS;
            if (!owner.world || !owner.live()) return LT_NOTINWORLD;
            const auto shade=owner.world->m_Shade.Sample(*point); color->Init(shade[2],shade[1],shade[0]); return LT_OK; });
        }
        LTRESULT GetPolyTextureFlags(HPOLY polygon,uint32* flags) override {
            return owner.dispatch([&]() -> LTRESULT { if (!flags) return LT_INVALIDPARAMS;
            if (!owner.world || !owner.live()) return LT_NOTINWORLD;
            const auto* poly=owner.world->FindPoly(polygon); if (!poly) return LT_INVALIDPARAMS;
            *flags=poly->m_pParent->m_nTextureFlags; return LT_OK; });
        }
        LTRESULT CreateMessage(ILTMessage*& message) override {
            message=nullptr; return owner.dispatch([&]() -> LTRESULT { if (!owner.live()) return LT_NOTINITIALIZED;
            CAbstractIO* packet=nullptr; msg_AllocPacket(&packet,MAX_PACKET_LEN,MAX_PACKET_LEN);
            if (!packet) return LT_OUTOFMEMORY;
            auto* owned=static_cast<ServerMessage*>(packet); message=&owned->m_Write; return LT_OK; });
        }
    } common_api{*this};

    struct LightInterface final : ILTLightAnim {
        State& owner;
        explicit LightInterface(State& state) : owner(state) {}
        LTRESULT FindLightAnim(const char* name,HLIGHTANIM& handle) override {
            handle=INVALID_LIGHT_ANIM;
            return owner.dispatch([&]() -> LTRESULT {
                owner.ready(); if (!name) return LT_INVALIDPARAMS;
                return owner.world && owner.world->FindLightAnim(name,&handle) ? LT_OK : LT_NOTFOUND;
            });
        }
        LightAnim* find(HLIGHTANIM handle) {
            owner.ready(); return owner.world && handle<owner.world->m_Anims.GetSize() ? &owner.world->m_Anims[handle] : nullptr;
        }
        LTRESULT GetNumFrames(HLIGHTANIM handle,uint32& count) override {
            count=0; return owner.dispatch([&]() -> LTRESULT {
                const auto* animation=find(handle); if (!animation) return LT_INVALIDPARAMS;
                count=animation->m_nFrames; return LT_OK;
            });
        }
        LTRESULT GetLightAnimInfo(HLIGHTANIM handle,LAInfo& info) override {
            info={}; info.m_iFrames[0]=info.m_iFrames[1]=LIGHTANIMFRAME_NONE;
            return owner.dispatch([&]() -> LTRESULT {
                auto* animation=find(handle); if (!animation) return LT_INVALIDPARAMS;
                la_GetInfo(animation,&info); return LT_OK;
            });
        }
        LTRESULT SetLightAnimInfo(HLIGHTANIM handle,LAInfo& info) override {
            return owner.dispatch([&]() -> LTRESULT {
                auto* animation=find(handle); if (!animation) return LT_INVALIDPARAMS;
                for (auto frame : info.m_iFrames) if (frame!=LIGHTANIMFRAME_NONE && frame>=animation->m_nFrames) return LT_INVALIDPARAMS;
                for (auto amount : {info.m_fPercentBetween,info.m_fBlendPercent})
                    if (!std::isfinite(amount) || amount<0 || amount>1) return LT_INVALIDPARAMS;
                LAInfo accepted; la_GetInfo(animation,&accepted);
                std::copy_n(info.m_iFrames,2,accepted.m_iFrames);
                accepted.m_fPercentBetween=info.m_fPercentBetween; accepted.m_fBlendPercent=info.m_fBlendPercent;
                if (animation->m_bShadowMap) {
                    if (!finite(info.m_vLightPos) || !finite(info.m_vLightColor) || !std::isfinite(info.m_fLightRadius) || info.m_fLightRadius<0 || info.m_fLightRadius>1e8f) return LT_INVALIDPARAMS;
                    for (uint32 axis=0;axis<3;++axis) if (std::fabs(info.m_vLightPos[axis])>1e8f || info.m_vLightColor[axis]<0 || info.m_vLightColor[axis]>255) return LT_INVALIDPARAMS;
                    accepted.m_vLightPos=info.m_vLightPos; accepted.m_vLightColor=info.m_vLightColor; accepted.m_fLightRadius=info.m_fLightRadius;
                }
                la_SetInfo(animation,&accepted); return LT_OK;
            });
        }
    } lighting_api{*this};

    struct Mover final : IObjectMover {
        State& owner;
        CollisionInfo* collision=nullptr;
        explicit Mover(State& state) : owner(state) {}
        void SetObjectChangeFlags(LTObject* object,uint32 flags) override { if (owner.owns(object)) owner.records.at(object).info.dirty|=flags; }
        CollisionInfo** GetCollisionInfo() override { return &collision; }
        void TouchNotify(LTObject* object,HOBJECT other,LTVector* stop,float force) override {
            CallbackScope scope(owner);
            if (!owner.owns(object) || !owner.owns(other)) return;
            const auto callback=owner.callbacks.touch;
            if (callback) { auto info=collision ? *collision : CollisionInfo{}; info.m_hObject=other; if (stop) info.m_vStopVel=*stop; callback(object,info,force); }
        }
        void Attach(LTObject* object,LTObject* container) override {
            if (owner.owns(object) && owner.owns(container)) owner.container_links[object].insert(container);
        }
        void Detach(LTObject* object) override { owner.container_links.erase(object); }
        void UpdateAttachments(WMMoveState* state) override { if (state && owner.owns(state->m_pObj)) owner.follow(state->m_pObj); }
        // Blocking priorities stay on the server (a door's is set there), so
        // a client copy of a brush ranks no higher than the player standing
        // on it: a brush moves what is in its way, as the server's lift
        // does the player, or the lift is held back and the player sinks
        // through its floor. Nothing else pushes a brush or another brush.
        int ObjectBlocks(WMMoveState* state,LTObject*,LTObject* other) override {
            const auto brush=[](const LTObject* object) { return object->m_ObjectType==OT_WORLDMODEL || object->m_ObjectType==OT_CONTAINER; };
            return owner.owns(other) && other->_v30() &&
                (state->m_nUnknown0C>other->m_BlockingPriority || (brush(state->m_pObj) && !brush(other)));
        }
        uint32 Crush(LTObject*,void*) override { return 0; } // Crushing is decided by the authoritative server.
        void CheckMaxPos(WMMoveState*,LTVector* value) override { check(value && finite(*value),LT_INVALIDPARAMS,"invalid client movement position"); }
        bool IsServer() const override { return false; }
        LTBOOL CanPlace(LTObject* object) override {
            return !(object->m_Flags&(FLAG_SOLID|FLAG_TOUCH_NOTIFY|FLAG_CONTAINER)) && !object->m_pAttachments && object->m_StandingOnMe.IsTiedOff();
        }
        const char* GetObjectClassName(LTObject* object) override {
            const auto found=owner.records.find(object);
            return found==owner.records.end() ? "ClientObject" : found->second.info.name.c_str();
        }
        ILTPhysics* Physics() override { return &owner.physics_api; }
    } movement{*this};

    const std::thread::id owner=std::this_thread::get_id();
    std::shared_ptr<const Assets> assets;
    CUnknown390 objects;
    CUnknown210 index;
    ModelCache models;
    ClientSprites sprites;
    ClientEffects effects;
    ILTMath math;
    ILTTransform transform;
    ClientPhysics physics_api{*this};
    std::map<HOBJECT,Record,std::less<HOBJECT>> records;
    std::map<LTAnimTracker*,HOBJECT,std::less<LTAnimTracker*>> trackers;
    std::map<uint16,HOBJECT> server_objects;
    std::map<HOBJECT,std::set<HOBJECT,std::less<HOBJECT>>,std::less<HOBJECT>> container_links;
    std::shared_ptr<CServerWorld> world;
    HOBJECT world_object=nullptr;
    std::string world_path;
    ClientSceneEvents callbacks;
    uint32 viewport_width=640,viewport_height=480;
    uint64 event_generation=0;
    double animation_remainder=0;
    bool retired=false,clearing=false,clearing_world=false,transition=false,following=false;
    std::vector<MoveObjectReference> attachment_pending;

    explicit State(std::shared_ptr<const Assets> files) : assets(files ? std::move(files) : std::make_shared<Assets>()),models(assets),sprites(assets),effects(assets) { index.Init(&objects); }
    ~State() {
        const auto retired_callbacks=std::move(callbacks); index.Term();
        // Secondary trackers belong to game storage. Normal removal detaches
        // them while the game is alive; retirement never calls through them.
        trackers.clear(); effects.clear(); un390_Term(&objects); records.clear(); world.reset();
    }
    void retire() {
        const auto retired_callbacks=std::move(callbacks);
        retired=true; ++event_generation; movement.Retire();
        for (const auto& [object,record] : records) object->m_ChangeFlags|=1;
    }
    static void model_key(LTAnimTracker* tracker,AnimKeyFrame* frame) {
        const auto self=current;
        if (!self || !frame || !frame->m_pString || !self->owns_tracker(tracker)) return;
        auto* object=self->trackers.at(tracker);
        if (!(self->records.at(object).info.client_flags&CF_NOTIFYMODELKEYS)) return;
        ModelCallbackGuard guard(*tracker);
        const auto generation=self->event_generation;
        std::string script(frame->m_pString); CommandParser parser; parser.Init(script.data());
        while (parser.Next()) {
            if (!parser.m_nArgs) continue;
            const auto callback=self->callbacks.model_key;
            if (callback) { ArgList args{parser.m_Args,parser.m_nArgs}; callback(object,args); }
            if (!self->live() || self->event_generation!=generation || !guard.unchanged() || !self->owns(object) || !(self->records.at(object).info.client_flags&CF_NOTIFYMODELKEYS)) return;
        }
    }
    LTRESULT create(const ObjectCreateStruct& input,HOBJECT& output,ClientObjectLifetime lifetime,std::shared_ptr<CServerWorld> geometry={});
    LTRESULT remove(HOBJECT,bool notify=true);
    LTRESULT filenames(HOBJECT,const ObjectCreateStruct&);
    LTRESULT place(HOBJECT,const LTVector*,const LTRotation*,bool);
    LTRESULT scale(HOBJECT,LTVector);
    LTRESULT color(HOBJECT,float,float,float,float);
    LTRESULT camera(HOBJECT,const ClientCamera&);
    ClientCamera camera(HOBJECT);
    void follow(HOBJECT);
    void clear_objects(bool world_only);
    LTRESULT load_world(std::string_view);
    void unload_world();
};
thread_local std::shared_ptr<ClientScene::State> ClientScene::State::current;

LTRESULT ClientScene::State::create(const ObjectCreateStruct& input,HOBJECT& output,ClientObjectLifetime lifetime,std::shared_ptr<CServerWorld> geometry) {
    ready(); output=nullptr;
    check(!clearing && !(clearing_world && (lifetime==ClientObjectLifetime::World || input.m_ObjectType==OT_WORLDMODEL || input.m_ObjectType==OT_CONTAINER)),LT_NOTINITIALIZED,"client objects are being cleared");
    check(records.size()<65536,LT_OUTOFMEMORY,"client object capacity exceeded");
    check(input.m_ObjectType<NUM_OBJECTTYPES && finite(input.m_Pos) && finite(input.m_Scale) && valid_rotation(input.m_Rotation),LT_INVALIDPARAMS,"invalid native object creation");
    Record record; record.info.name=text(input.m_Name); record.info.filename=text(input.m_Filename); record.info.lifetime=lifetime;
    for (uint32 skin=0;skin<MAX_MODEL_TEXTURES;++skin) { record.info.skins[skin]=text(input.m_SkinNames[skin]); if (!record.info.skins[skin].empty()) record.info.skins[skin]=platform::asset_key(record.info.skins[skin]); }
    std::shared_ptr<Model> model;
    if (input.m_ObjectType==OT_MODEL) {
        check(!record.info.filename.empty(),LT_INVALIDMODELFILE,"model object needs an ABC file");
        record.info.filename=platform::asset_key(record.info.filename);
        try { model=models.Load(record.info.filename); } catch (const std::exception&) { throw Failure(LT_INVALIDMODELFILE,"client model load failed"); }
    }
    if (input.m_ObjectType==OT_SPRITE && !record.info.filename.empty()) {
        record.info.filename=platform::asset_key(record.info.filename);
        try { record.sprite=sprites.load(record.info.filename); }
        catch (const std::bad_alloc&) { throw; }
        catch (const std::exception&) { throw Failure(LT_INVALIDSPRITEFILE,"client sprite load failed"); }
    }
    if (input.m_ObjectType==OT_WORLDMODEL || input.m_ObjectType==OT_CONTAINER) {
        record.geometry=geometry ? std::move(geometry) : world;
        check(bool(record.geometry),LT_NOTINWORLD,"world model created without a world");
        record.info.lifetime=ClientObjectLifetime::World;
    }
    ObjectCreateStruct create=input; LTObject* object=nullptr;
    const auto result=om_CreateObject(&objects,&create,&object); if (result!=LT_OK) return result;
    try {
        if (model) {
            auto* instance=static_cast<ModelInstance*>(object); instance->BindModel(std::move(model),false,false);
            instance->m_AnimTracker.m_StringKeyCallback=State::model_key;
            if (instance->m_AnimTracker.GetModel()->m_AnimArray.GetSize()) object->SetDims(instance->m_AnimTracker.GetModel()->m_AnimArray[0].m_vDims);
        }
        if (record.sprite) {
            auto* sprite=static_cast<CObjSprite*>(object); sprite->m_pSprite=&record.sprite->sprite_;
            sprite->m_pCurAnim=&record.sprite->animation_;
            sprite->m_pCurFrame=record.sprite->indices_.empty() ? nullptr : record.sprite->indices_.data();
        }
        if (record.geometry) {
            check(record.geometry->AttachWorldModel(object,create.m_Filename)!=0,LT_MISSINGWORLDMODEL,"missing native brush model");
            auto* bsp=object->m_pWorldBsp;
            if (!bsp->IsBusy()) { object->SetPos(static_cast<CWorldBsp*>(bsp)->m_ve8); object->SetDims((bsp->m_vBoxMax-bsp->m_vBoxMin)*.5f); }
            obj_RetransformWorldModelDims(object);
        }
        record.camera.rectangle={0,0,int(viewport_width),int(viewport_height)};
        records.emplace(object,std::move(record));
        effects.add_object(object);
        if (object->m_ObjectType==OT_MODEL) trackers.emplace(&static_cast<ModelInstance*>(object)->m_AnimTracker,object);
        index.BumpObject(object,0); output=object; return LT_OK;
    } catch (...) {
        if (object->m_ObjectType==OT_MODEL) trackers.erase(&static_cast<ModelInstance*>(object)->m_AnimTracker);
        // The metadata keeps brush geometry alive through native destruction.
        effects.forget(object); un390_RemoveObject(&objects,object); records.erase(object); throw;
    }
}
LTRESULT ClientScene::State::remove(HOBJECT object,bool notify) {
    get(object); auto& record=records.at(object);
    if (record.info.removing) return LT_NOCHANGE;
    const auto callback=notify && (record.info.client_flags&CF_NOTIFYREMOVE) ? callbacks.removed : std::function<void(HOBJECT)>{};
    record.info.removing=true; object->m_ChangeFlags|=1;
    if (record.info.server_id) {
        server_objects.erase(*record.info.server_id);
        std::replace(sky.objects.begin(),sky.objects.end(),*record.info.server_id,uint16(0xffff));
    }
    std::exception_ptr error;
    try { if (callback) callback(object); } catch (...) { error=std::current_exception(); }
    if (object->m_ObjectType==OT_MODEL) {
        auto* model=static_cast<ModelInstance*>(object);
        while (live() && model->m_pTrackerList && model->m_pTrackerList->GetNext()) {
            auto* tracker=model->m_pTrackerList->GetNext();
            model_api.CommonModelLT::RemoveTracker(object,tracker); trackers.erase(tracker);
        }
        trackers.erase(&model->m_AnimTracker);
    }
    container_links.erase(object); for (auto& [child,parents] : container_links) parents.erase(object);
    if (world_object==object) world_object=nullptr;
    effects.forget(object); un390_RemoveObject(&objects,object); records.erase(object);
    if (error) std::rethrow_exception(error);
    return LT_OK;
}
LTRESULT ClientScene::State::filenames(HOBJECT object,const ObjectCreateStruct& input) {
    auto& record=get(object);
    if (object->m_ObjectType==OT_SPRITE) {
        auto filename=platform::asset_key(text(input.m_Filename)); std::shared_ptr<SpriteResource> resource;
        try { resource=sprites.load(filename); }
        catch (const std::bad_alloc&) { throw; }
        catch (const std::exception&) { return LT_INVALIDSPRITEFILE; }
        auto* sprite=static_cast<CObjSprite*>(object); sprite->m_pSprite=&resource->sprite_; sprite->m_pCurAnim=&resource->animation_;
        sprite->m_pCurFrame=resource->indices_.empty() ? nullptr : resource->indices_.data(); sprite->m_FrameTime=0; sprite->m_Unknown1b8=SC_PLAY|SC_LOOP;
        record.sprite=std::move(resource); record.info.filename=std::move(filename); record.info.dirty|=1; return LT_OK;
    }
    check(object->m_ObjectType==OT_MODEL,LT_UNSUPPORTED,"filename replacement requires a model or sprite");
    auto filename=platform::asset_key(text(input.m_Filename));
    std::array<std::string,MAX_MODEL_TEXTURES> skins;
    for (uint32 skin=0;skin<MAX_MODEL_TEXTURES;++skin) { skins[skin]=text(input.m_SkinNames[skin]); if (!skins[skin].empty()) skins[skin]=platform::asset_key(skins[skin]); }
    std::shared_ptr<Model> model;
    try { model=models.Load(filename); } catch (const std::exception&) { return LT_INVALIDMODELFILE; }
    static_cast<ModelInstance*>(object)->BindModel(std::move(model),false,true);
    record.info.filename=std::move(filename); record.info.skins=std::move(skins); record.info.dirty|=1; return LT_OK;
}
void ClientScene::State::follow(HOBJECT object) {
    get(object);
    check(attachment_pending.size()<65536,LT_ERROR,"client attachment update budget exceeded");
    attachment_pending.emplace_back(object);
    if (following) return;
    following=true;
    struct Reset { State& owner; ~Reset() { owner.attachment_pending.clear(); owner.following=false; } } reset{*this};
    for (std::size_t next=0;next<attachment_pending.size() && live();++next) {
    const auto entry=attachment_pending[next];
    if (!entry.guard.unchanged() || !owns(entry.object)) continue;
    object=entry.object; ObjectCallbackGuard parent(*object);
    struct Context { State& owner; HOBJECT parent_object; ObjectCallbackGuard& parent; } context{*this,object,parent};
    am_VisitChildren(&objects,object,[](void* pointer,LTObject& child) {
        auto& context=*static_cast<Context*>(pointer); auto& self=context.owner;
        if (!self.live() || !context.parent.unchanged()) return false;
        ObjectCallbackGuard child_guard(child);
        Attachment* attachment=nullptr;
        for (auto* record=context.parent_object->m_pAttachments; record; record=record->m_pNext)
            if (record->m_pChild==&child) { attachment=record; break; }
        if (!attachment) return true;
        const auto generation=attachment->m_Generation;
        LTransform transform;
        const auto result=self.common_api.GetAttachmentTransform(reinterpret_cast<HATTACHMENT>(attachment),transform,LTTRUE);
        if (!self.live() || !context.parent.unchanged()) return false;
        if (!child_guard.unchanged()) return true;
        check(result==LT_OK,result,"client attachment transform failed");
        const auto* current=am_FindAttachment(&self.objects,reinterpret_cast<HATTACHMENT>(attachment));
        if (!current || current->m_Generation!=generation) return true;
        child.m_Rotation=transform.m_Rot;
        WMMoveState move; move.Init(&self.index,&self.movement,&child,child.m_BlockingPriority);
        wm_MoveObject(&move,transform.m_Pos,0x0d);
        if (!self.live() || !context.parent.unchanged()) return false;
        // A brush is drawn and hit through its transform, which the move
        // rebuilds only on its collision path: a non-solid one (a car's
        // windows and panels while it drives) or a turn alone leaves it where
        // it was. As the original client places any world model.
        if (child_guard.unchanged() && (child.m_ObjectType==OT_WORLDMODEL || child.m_ObjectType==OT_CONTAINER)) {
            obj_RetransformWorldModelDims(&child); self.index.BumpObject(&child,0); self.records.at(&child).info.dirty|=1;
        }
        return true;
    },&context);
    }
}
void ClientScene::State::clear_objects(bool world_only) {
    auto& flag=world_only ? clearing_world : clearing; const auto before=flag; flag=true;
    struct Reset { bool& target; bool before; ~Reset() { target=before; } } reset{flag,before};
    std::vector<MoveObjectReference> selected;
    for (const auto& [object,record] : records) if (!world_only || record.info.lifetime==ClientObjectLifetime::World) selected.emplace_back(object);
    std::exception_ptr error;
    for (const auto& entry : selected) {
        if (!live()) break;
        if (entry.guard.alive() && records.contains(entry.object) && !records.at(entry.object).info.removing)
            try { remove(entry.object); } catch (...) { if (!error) error=std::current_exception(); }
    }
    if (world_only) {
        for (const auto& [object,record] : records) if (object->m_ObjectType==OT_SPRITE) static_cast<CObjSprite*>(object)->m_ClipPoly=INVALID_HPOLY;
    }
    sky=SkyInfo{};
    if (error) std::rethrow_exception(error);
}
LTRESULT ClientScene::State::load_world(std::string_view name) {
    ready(); check(!transition && !clearing,LT_ERROR,"recursive client world transition");
    auto filename=platform::asset_key(name); if (!filename.ends_with(".dat")) filename+=".dat";
    auto loaded=std::make_shared<CServerWorld>(); AssetStream stream(assets->locate(filename)); WorldLoadRequest request; request.m_pStream=&stream;
    check(loaded->Load(&request)==0,LT_INVALIDWORLDFILE,"client world load failed");
    check(loaded->LoadObjects(&stream)==LT_OK,LT_INVALIDWORLDFILE,"client baked lighting load failed");
    auto* main=loaded->FindFlagged10(); check(main!=nullptr,LT_INVALIDWORLDFILE,"missing main physics geometry");
    transition=true; struct Reset { bool& flag; ~Reset() { flag=false; } } reset{transition};
    clear_objects(true); check(live(),LT_NOTINITIALIZED,"scene retired during world unload");
    world.reset(); world_path.clear(); world_object=nullptr; index.BindVisibility(nullptr);
    check(index.LoadLayout(loaded->Geometry()->layout),LT_INVALIDWORLDFILE,"invalid client world layout");
    ObjectCreateStruct create; create.m_ObjectType=OT_WORLDMODEL; create.m_Flags=FLAG_SOLID;
    check(std::strlen(main->m_Name)<sizeof(create.m_Filename),LT_INVALIDWORLDFILE,"oversized world model name"); std::strcpy(create.m_Filename,main->m_Name);
    HOBJECT object=nullptr;
    const auto result=this->create(create,object,ClientObjectLifetime::World,loaded); if (result!=LT_OK) return result;
    try { check(index.BindVisibility(loaded->FindFlagged20()),LT_INVALIDWORLDFILE,"invalid client world visibility"); }
    catch (...) { remove(object,false); index.BindVisibility(nullptr); throw; }
    // Terrain sections are world models the server never sends (its update
    // skips simple BSPs): the client makes its own, as the server's
    // PrecacheModels does, so the player stands on them.
    for (auto* record : std::span(loaded->m_Worlds.GetArray(),loaded->m_Worlds.GetSize())) {
        if (!(record->m_pWorldBsp->m_nUnknown48&8)) continue;
        for (uint32 section=0;section<record->m_pWorldBsp->m_Array138.GetSize();++section) {
            ObjectCreateStruct terrain; terrain.m_ObjectType=OT_WORLDMODEL; terrain.m_Flags=FLAG_SOLID|FLAG_RAYHIT;
            // The server's fs_GetTableName spelling, which AttachWorldModel reads.
            const auto written=std::snprintf(terrain.m_Filename,sizeof(terrain.m_Filename),"#$#%u %s",unsigned(section),record->m_pWorldBsp->m_Name);
            check(written>0 && std::size_t(written)<sizeof(terrain.m_Filename),LT_INVALIDWORLDFILE,"oversized terrain name");
            HOBJECT piece=nullptr; const auto made=this->create(terrain,piece,ClientObjectLifetime::World,loaded); if (made!=LT_OK) return made;
        }
    }
    world=std::move(loaded); world_object=object; records.at(object).info.main_world=true; world_path=std::move(filename); return LT_OK;
}
void ClientScene::State::unload_world() {
    ready(); check(!transition,LT_ERROR,"recursive client world transition");
    transition=true; struct Reset { bool& flag; ~Reset() { flag=false; } } reset{transition};
    clear_objects(true); if (!live()) return;
    world_object=nullptr; world.reset(); world_path.clear(); index.BindVisibility(nullptr);
}
LTRESULT ClientScene::State::place(HOBJECT object,const LTVector* position,const LTRotation* rotation,bool force) {
    get(object);
    check(!position || finite(*position),LT_INVALIDPARAMS,"invalid object position");
    check(!rotation || valid_rotation(*rotation),LT_INVALIDPARAMS,"invalid object rotation");
    const bool moving=position && (force || object->m_Pos.DistSqr(*position)>=.001f);
    if (!moving && !rotation) return LT_OK;
    if (moving) object->SetPos(*position);
    if (rotation) object->m_Rotation=*rotation;
    if (object->m_ObjectType==OT_WORLDMODEL || object->m_ObjectType==OT_CONTAINER) {
        // An opening window's box-physics glass must rotate its collision
        // extents along with its drawn geometry and attachment transform.
        if (rotation) obj_RetransformWorldModelDims(object);
        else obj_RetransformWorldModel(object);
    }
    index.BumpObject(object,0); records.at(object).info.dirty|=1; follow(object); return LT_OK;
}
LTRESULT ClientScene::State::scale(HOBJECT object,LTVector scale) {
    get(object); check(finite(scale),LT_INVALIDPARAMS,"invalid object scale");
    object->m_Scale=scale;
    if (object->m_ObjectType==OT_WORLDMODEL || object->m_ObjectType==OT_CONTAINER) obj_RetransformWorldModel(object);
    index.BumpObject(object,0); records.at(object).info.dirty|=1; return LT_OK;
}
LTRESULT ClientScene::State::color(HOBJECT object,float r,float g,float b,float a) {
    get(object);
    for (float value : {r,g,b,a}) check(std::isfinite(value),LT_INVALIDPARAMS,"nonfinite object color");
    const auto byte=[](float value) { return static_cast<uint8>(std::clamp(value,0.f,1.f)*255); };
    object->m_ColorR=byte(r); object->m_ColorG=byte(g); object->m_ColorB=byte(b); object->m_ColorA=byte(a);
    records.at(object).info.dirty|=1; return LT_OK;
}
LTRESULT ClientScene::State::camera(HOBJECT object,const ClientCamera& camera) {
    auto& record=get(object); check(object->m_ObjectType==OT_CAMERA,LT_INVALIDPARAMS,"camera method on non-camera");
    check(std::isfinite(camera.fov_x) && std::isfinite(camera.fov_y) && camera.fov_x>0 && camera.fov_y>0 && camera.fov_x<std::numbers::pi_v<float> && camera.fov_y<std::numbers::pi_v<float> && finite(camera.light_add),LT_INVALIDPARAMS,"invalid camera projection");
    const auto width=std::int64_t(camera.rectangle.right)-camera.rectangle.left,height=std::int64_t(camera.rectangle.bottom)-camera.rectangle.top;
    check(width>0 && height>0 && width<=16384 && height<=16384,LT_INVALIDPARAMS,"invalid camera rectangle");
    record.camera=camera; auto* native=static_cast<CObjCamera*>(object);
    native->m_fFovX=camera.fov_x; native->m_fFovY=camera.fov_y; native->m_v1c4=camera.light_add; return LT_OK;
}
ClientCamera ClientScene::State::camera(HOBJECT object) {
    auto& record=get(object); check(object->m_ObjectType==OT_CAMERA,LT_INVALIDPARAMS,"camera method on non-camera");
    auto result=record.camera;
    if (result.fullscreen) result.rectangle={0,0,int(viewport_width),int(viewport_height)};
    return result;
}
const SceneFunctions& ClientScene::State::functions() {
    static const SceneFunctions api{
        .CreateObject=+[](ObjectCreateStruct* input) -> HLOCALOBJ {
            return sdk_value<HLOCALOBJ>([&](State& self) -> HLOCALOBJ {
                if (!input) return nullptr;
                HOBJECT object=nullptr; return self.create(*input,object,ClientObjectLifetime::Client)==LT_OK ? object : nullptr;
            });
        },
        .GetObjectPos=+[](HLOCALOBJ object,LTVector* position) {
            if (position) { position->Init(); sdk_result([&](State& self) -> LTRESULT { self.get(object); *position=object->m_Pos; return LT_OK; }); }
        },
        .GetObjectRotation=+[](HLOCALOBJ object,LTRotation* rotation) {
            if (rotation) { rotation->Init(); sdk_result([&](State& self) -> LTRESULT { self.get(object); *rotation=object->m_Rotation; return LT_OK; }); }
        },
        .SetObjectRotation=+[](HLOCALOBJ object,LTRotation* rotation) {
            if (rotation) sdk_result([&](State& self) { return self.place(object,nullptr,rotation,false); });
        },
        .SetObjectPosAndRotation=+[](HLOCALOBJ object,LTVector* position,LTRotation* rotation) {
            if (position && rotation) sdk_result([&](State& self) { return self.place(object,position,rotation,true); });
        },
        .GetObjectScale=+[](HLOCALOBJ object,LTVector* scale) -> LTRESULT {
            if (!scale) return LT_INVALIDPARAMS;
            scale->Init(); return sdk_result([&](State& self) -> LTRESULT { self.get(object); *scale=object->m_Scale; return LT_OK; });
        },
        .SetObjectScale=+[](HLOCALOBJ object,LTVector* scale) -> LTRESULT {
            if (!scale) return LT_INVALIDPARAMS;
            return sdk_result([&](State& self) { return self.scale(object,*scale); });
        },
        .GetObjectColor=+[](HLOCALOBJ object,float* r,float* g,float* b,float* a) {
            for (auto* channel : {r,g,b,a}) if (channel) *channel=0;
            sdk_result([&](State& self) -> LTRESULT {
                self.get(object);
                if (r) *r=object->m_ColorR/255.f;
                if (g) *g=object->m_ColorG/255.f;
                if (b) *b=object->m_ColorB/255.f;
                if (a) *a=object->m_ColorA/255.f;
                return LT_OK;
            });
        },
        .SetObjectColor=+[](HLOCALOBJ object,float r,float g,float b,float a) {
            sdk_result([&](State& self) { return self.color(object,r,g,b,a); });
        },
        .GetObjectUserFlags=+[](HLOCALOBJ object,uint32* flags) -> LTRESULT {
            if (!flags) return LT_INVALIDPARAMS;
            *flags=0; return sdk_result([&](State& self) -> LTRESULT { self.get(object); *flags=object->m_UserFlags; return LT_OK; });
        },
        .SetObjectUserFlags=+[](HLOCALOBJ object,uint32 flags) -> LTRESULT {
            return sdk_result([&](State& self) -> LTRESULT {
                auto& record=self.get(object); if (record.info.server_id) return LT_ERROR;
                object->m_UserFlags=flags; record.info.dirty|=1; return LT_OK;
            });
        },
        .GetObjectClientFlags=+[](HLOCALOBJ object) -> uint32 { return sdk_value<uint32>([&](State& self) { return self.get(object).info.client_flags; }); },
        .SetObjectClientFlags=+[](HLOCALOBJ object,uint32 flags) { sdk_result([&](State& self) -> LTRESULT { self.get(object).info.client_flags=flags; return LT_OK; }); },
        .GetObjectUserData=+[](HLOCALOBJ object) -> void* { return sdk_value<void*>([&](State& self) { return self.get(object).info.user; }); },
        .SetObjectUserData=+[](HLOCALOBJ object,void* user) { sdk_result([&](State& self) -> LTRESULT { self.get(object).info.user=user; return LT_OK; }); },
        .Get3DCameraPt=+[](HLOCALOBJ object,int x,int y,LTVector* out) -> LTRESULT {
            if (!out) return LT_INVALIDPARAMS;
            out->Init(); return sdk_result([&](State& self) -> LTRESULT {
                const auto camera=self.camera(object); const auto rect=camera.rectangle;
                if (x<rect.left || x>=rect.right || y<rect.top || y>=rect.bottom) return LT_OUTSIDE;
                *out=CameraView(object->m_Pos,object->m_Rotation,rect,camera.fov_x,camera.fov_y).pixel_point(x,y); return LT_OK;
            });
        },
        .GetCameraFOV=+[](HLOCALOBJ object,float* x,float* y) {
            if (x) *x=0;
            if (y) *y=0;
            sdk_result([&](State& self) -> LTRESULT { const auto camera=self.camera(object); if (x) *x=camera.fov_x; if (y) *y=camera.fov_y; return LT_OK; });
        },
        .SetCameraFOV=+[](HLOCALOBJ object,float x,float y) {
            sdk_result([&](State& self) -> LTRESULT {
                if (!std::isfinite(x) || !std::isfinite(y)) return LT_INVALIDPARAMS;
                auto camera=self.camera(object); constexpr float minimum=std::numbers::pi_v<float>*.01f,maximum=std::numbers::pi_v<float>*.99f;
                camera.fov_x=std::clamp(x,minimum,maximum); camera.fov_y=std::clamp(y,minimum,maximum); return self.camera(object,camera);
            });
        },
        .GetCameraRect=+[](HLOCALOBJ object,LTBOOL* fullscreen,int* left,int* top,int* right,int* bottom) {
            if (fullscreen) *fullscreen=LTFALSE;
            for (auto* side : {left,top,right,bottom}) if (side) *side=0;
            sdk_result([&](State& self) -> LTRESULT {
                const auto camera=self.camera(object); if (fullscreen) *fullscreen=camera.fullscreen ? LTTRUE : LTFALSE;
                if (left) *left=camera.rectangle.left;
                if (top) *top=camera.rectangle.top;
                if (right) *right=camera.rectangle.right;
                if (bottom) *bottom=camera.rectangle.bottom;
                return LT_OK;
            });
        },
        .SetCameraRect=+[](HLOCALOBJ object,LTBOOL fullscreen,int left,int top,int right,int bottom) {
            sdk_result([&](State& self) { auto camera=self.camera(object); camera.fullscreen=fullscreen!=LTFALSE;
                camera.rectangle=camera.fullscreen ? LTRect{0,0,int(self.viewport_width),int(self.viewport_height)} : LTRect{left,top,right,bottom}; return self.camera(object,camera); });
        },
        .GetCameraLightAdd=+[](HLOCALOBJ object,LTVector* light) -> LTBOOL {
            if (!light) return LTFALSE;
            light->Init(); return sdk_value<LTBOOL>([&](State& self) -> LTBOOL { *light=self.camera(object).light_add; return LTTRUE; });
        },
        .SetCameraLightAdd=+[](HLOCALOBJ object,LTVector* light) -> LTBOOL {
            if (!light) return LTFALSE;
            return sdk_value<LTBOOL>([&](State& self) -> LTBOOL { auto camera=self.camera(object); camera.light_add=*light; return self.camera(object,camera)==LT_OK ? LTTRUE : LTFALSE; });
        },
        .GetNextModelNode=+[](HLOCALOBJ object,HMODELNODE current,HMODELNODE* next) -> LTRESULT {
            if (!next) return LT_INVALIDPARAMS;
            *next=INVALID_MODEL_NODE; return sdk_result([&](State& self) -> LTRESULT {
                if (!self.owns_model(object)) return LT_INVALIDPARAMS;
                return ci_GetNextModelNode(object,current,next);
            });
        },
        .GetModelNodeName=+[](HLOCALOBJ object,HMODELNODE node,char* buffer,uint32 capacity) -> LTRESULT {
            if (!buffer || !capacity) return LT_INVALIDPARAMS;
            buffer[0]='\0'; return sdk_result([&](State& self) -> LTRESULT {
                if (!self.owns_model(object)) return LT_INVALIDPARAMS;
                return ci_GetModelNodeName(object,node,buffer,capacity);
            });
        },
        .ClipSprite=+[](HLOCALOBJ object,HPOLY polygon) -> LTRESULT {
            return sdk_result([&](State& self) -> LTRESULT {
                self.get(object); if (object->m_ObjectType!=OT_SPRITE) return LT_INVALIDPARAMS;
                if (polygon!=INVALID_HPOLY && (!self.world || !self.world->FindPoly(polygon))) return LT_INVALIDPARAMS;
                static_cast<CObjSprite*>(object)->m_ClipPoly=polygon; return LT_OK;
            });
        },
        .GetSkyDef=+[](SkyDef* output) -> LTRESULT {
            return sdk_result([&](State& self) -> LTRESULT { if (!output) return LT_INVALIDPARAMS; *output=self.sky.definition; return LT_OK; });
        },
        .SetupParticleSystem=+[](HLOCALOBJ object,char* texture,float gravity,uint32 flags,float radius) -> LTRESULT {
            return sdk_result([&](State& self) { return self.effects.setup(object,texture,gravity,flags,radius); });
        },
        .SetSoftwarePSColor=+[](HLOCALOBJ object,float r,float g,float b) -> LTRESULT {
            return sdk_result([&](State& self) { return self.effects.software_color(object,r,g,b); });
        },
        .AddParticles=+[](HLOCALOBJ object,uint32 count,LTVector* p0,LTVector* p1,LTVector* v0,LTVector* v1,LTVector* c0,LTVector* c1,float t0,float t1) {
            sdk_result([&](State& self) -> LTRESULT { if (!p0 || !p1 || !v0 || !v1 || !c0 || !c1) return LT_INVALIDPARAMS; self.effects.add_particles(object,count,*p0,*p1,*v0,*v1,*c0,*c1,t0,t1); return LT_OK; });
        },
        .GetParticles=+[](HLOCALOBJ object,LTParticle** head,LTParticle** tail) -> LTBOOL {
            if (head) *head=nullptr;
            if (tail) *tail=nullptr;
            return sdk_value<LTBOOL>([&](State& self) { return head && tail && self.effects.particles(object,*head,*tail) ? LTTRUE : LTFALSE; });
        },
        .GetParticlePos=+[](HLOCALOBJ object,LTParticle* particle,LTVector* position) {
            if (position) { position->Init(); sdk_result([&](State& self) -> LTRESULT { *position=self.effects.particle_position(object,particle); return LT_OK; }); }
        },
        .SetParticlePos=+[](HLOCALOBJ object,LTParticle* particle,LTVector* position) {
            if (position) sdk_result([&](State& self) -> LTRESULT { self.effects.particle_position(object,particle,*position); return LT_OK; });
        },
        .GetParticleLifetime=+[](HLOCALOBJ object,LTParticle* particle,float& lifetime) -> LTRESULT {
            lifetime=0; return sdk_result([&](State& self) -> LTRESULT { lifetime=self.effects.particle_lifetime(object,particle,false); return LT_OK; });
        },
        .GetParticleTotalLifetime=+[](HLOCALOBJ object,LTParticle* particle,float& lifetime) -> LTRESULT {
            lifetime=0; return sdk_result([&](State& self) -> LTRESULT { lifetime=self.effects.particle_lifetime(object,particle,true); return LT_OK; });
        },
        .RemoveParticle=+[](HLOCALOBJ object,LTParticle* particle) {
            sdk_result([&](State& self) -> LTRESULT { self.effects.remove_particle(object,particle); return LT_OK; });
        },
        .OptimizeParticles=+[](HLOCALOBJ object) -> LTRESULT {
            return sdk_result([&](State& self) -> LTRESULT { self.effects.optimize(object); return LT_OK; });
        },
        .GetNextLine=+[](HLOCALOBJ object,HLTLINE previous) -> HLTLINE {
            return sdk_value<HLTLINE>([&](State& self) { return self.effects.next_line(object,previous); });
        },
        .GetLineInfo=+[](HLTLINE line,LTLine* output) {
            if (output) { *output=LTLine{}; for (auto& point : output->m_Points) point.m_Pos.Init(); sdk_result([&](State& self) -> LTRESULT { *output=self.effects.line(line); return LT_OK; }); }
        },
        .SetLineInfo=+[](HLTLINE line,LTLine* input) {
            if (input) sdk_result([&](State& self) -> LTRESULT { self.effects.line(line,*input); return LT_OK; });
        },
        .AddLine=+[](HLOCALOBJ object,LTLine* line) -> HLTLINE {
            return sdk_value<HLTLINE>([&](State& self) { return line ? self.effects.add_line(object,*line) : nullptr; });
        },
        .RemoveLine=+[](HLOCALOBJ object,HLTLINE line) {
            sdk_result([&](State& self) -> LTRESULT { self.effects.remove_line(object,line); return LT_OK; });
        },
        .IntersectSegment=+[](ClientIntersectQuery* query,ClientIntersectInfo* hit) -> LTBOOL {
            if (hit) *hit=ClientIntersectInfo{};
            return sdk_value<LTBOOL>([&](State& self) { return query && hit ? w_IntersectSegment(query,hit,&self.index,0) : LTFALSE; });
        },
        .SetupPolyGrid=+[](HLOCALOBJ object,uint32 width,uint32 height,LTBOOL half) -> LTBOOL {
            return sdk_value<LTBOOL>([&](State& self) { return self.effects.setup_grid(object,width,height,half!=0) ? LTTRUE : LTFALSE; });
        },
        .SetPolyGridTexture=+[](HLOCALOBJ object,char* name) -> LTRESULT {
            return sdk_result([&](State& self) { return self.effects.grid_texture(object,name); });
        },
        .SetPolyGridEnvMap=+[](HLOCALOBJ object,char* name) -> LTRESULT {
            return sdk_result([&](State& self) { return self.effects.grid_environment(object,name); });
        },
        .GetPolyGridTextureInfo=+[](HLOCALOBJ object,float* x_pan,float* y_pan,float* x_scale,float* y_scale) -> LTRESULT {
            for (auto* value : {x_pan,y_pan,x_scale,y_scale}) if (value) *value=0;
            return sdk_result([&](State& self) -> LTRESULT { const auto values=self.effects.grid_texture_info(object); const std::array<float*,4> output{x_pan,y_pan,x_scale,y_scale}; for (unsigned i=0;i<4;++i) if (output[i]) *output[i]=values[i]; return LT_OK; });
        },
        .SetPolyGridTextureInfo=+[](HLOCALOBJ object,float x_pan,float y_pan,float x_scale,float y_scale) -> LTRESULT {
            return sdk_result([&](State& self) -> LTRESULT { self.effects.grid_texture_info(object,x_pan,y_pan,x_scale,y_scale); return LT_OK; });
        },
        .GetPolyGridInfo=+[](HLOCALOBJ object,char** data,uint32* width,uint32* height,PGColor** colors) -> LTRESULT {
            if (data) *data=nullptr;
            if (width) *width=0;
            if (height) *height=0;
            if (colors) *colors=nullptr;
            return sdk_result([&](State& self) -> LTRESULT { if (!data || !width || !height || !colors) return LT_INVALIDPARAMS; self.effects.grid_info(object,*data,*width,*height,*colors); return LT_OK; });
        },
        .FitPolyGrid=+[](HLOCALOBJ object,LTVector* min,LTVector* max,LTVector* position,LTVector* scale) -> LTRESULT {
            return sdk_result([&](State& self) -> LTRESULT { if (!min || !max) return LT_INVALIDPARAMS; self.effects.fit_grid(object,*min,*max,position,scale); return LT_OK; });
        }
    };
    return api;
}

ClientScene::ClientScene(std::shared_ptr<const Assets> files) : state_(std::make_shared<State>(std::move(files))) {}
ClientScene::~ClientScene() { state_->retire(); }
ClientScene::Scope::Scope(const ClientScene& scene) : previous_(State::current),current_(scene.state_) {
    current_->ready(); code_=current_->callbacks.code; State::current=current_;
}
ClientScene::Scope::~Scope() { State::current=std::move(previous_); }
const SceneFunctions& ClientScene::functions() { return State::functions(); }
void ClientScene::install(ILTClient& client) const {
    state_->ready(); const auto& api=functions();
#define NOLF_SCENE_API(name) client.name=api.name;
#include "client_scene_members.inc"
#undef NOLF_SCENE_API
}
std::shared_ptr<const void> ClientScene::retain() const { state_->thread(); return state_; }
void ClientScene::events(ClientSceneEvents events) { state_->ready(); std::swap(state_->callbacks,events); ++state_->event_generation; }
bool ClientScene::contains(HOBJECT object) const { return state_->owns(object); }
std::size_t ClientScene::size() const { state_->ready(); return state_->records.size(); }
std::optional<ClientObjectInfo> ClientScene::info(HOBJECT object) const { state_->thread(); if (!state_->owns(object)) return {}; return state_->records.at(object).info; }
std::shared_ptr<const SpriteResource> ClientScene::sprite(HOBJECT object) const {
    state_->thread(); if (!state_->owns(object)) return {}; return state_->records.at(object).sprite;
}
LTRESULT ClientScene::sprite_control(HOBJECT object,ILTSpriteControl*& output) {
    output=nullptr; const auto self=state_;
    return self->dispatch([&]() -> LTRESULT {
        self->get(object); if (object->m_ObjectType!=OT_SPRITE) return LT_INVALIDPARAMS;
        output=&static_cast<CObjSprite*>(object)->m_Control; return LT_OK;
    });
}
std::vector<MoveObjectReference> ClientScene::snapshot() const {
    state_->ready(); std::vector<MoveObjectReference> result; result.reserve(state_->records.size());
    for (const auto& [object,record] : state_->records) if (!record.info.removing) result.emplace_back(object);
    return result;
}
LTRESULT ClientScene::create(const ObjectCreateStruct& create,HOBJECT& output,ClientObjectLifetime lifetime) {
    output=nullptr; const auto self=state_; return self->dispatch([&] { return self->create(create,output,lifetime); });
}
LTRESULT ClientScene::remove(HOBJECT object) { const auto self=state_; return self->dispatch([&] { return self->remove(object); }); }
LTRESULT ClientScene::clear() {
    const auto self=state_;
    return self->dispatch([&]() -> LTRESULT { self->ready(); self->clear_objects(false); if (self->live()) { self->world.reset(); self->world_path.clear(); self->index.BindVisibility(nullptr); } return LT_OK; });
}
LTRESULT ClientScene::filenames(HOBJECT object,const ObjectCreateStruct& create) {
    const auto self=state_; return self->dispatch([&] { return self->filenames(object,create); });
}
LTRESULT ClientScene::position(HOBJECT object,LTVector position,bool force) {
    const auto self=state_; return self->dispatch([&] { return self->place(object,&position,nullptr,force); });
}
LTRESULT ClientScene::rotation(HOBJECT object,LTRotation rotation) {
    const auto self=state_; return self->dispatch([&] { return self->place(object,nullptr,&rotation,false); });
}
LTRESULT ClientScene::scale(HOBJECT object,LTVector scale) {
    const auto self=state_; return self->dispatch([&] { return self->scale(object,scale); });
}
LTRESULT ClientScene::color(HOBJECT object,float r,float g,float b,float a) {
    const auto self=state_; return self->dispatch([&] { return self->color(object,r,g,b,a); });
}
LTRESULT ClientScene::client_flags(HOBJECT object,uint32 flags) { const auto self=state_; return operation([&]() -> LTRESULT { self->get(object).info.client_flags=flags; return LT_OK; }); }
LTRESULT ClientScene::user_data(HOBJECT object,void* data) { const auto self=state_; return operation([&]() -> LTRESULT { self->get(object).info.user=data; return LT_OK; }); }
LTRESULT ClientScene::server_id(HOBJECT object,std::optional<uint16> id) {
    const auto self=state_; return operation([&]() -> LTRESULT { auto& record=self->get(object); check(!id || *id!=0xffff,LT_INVALIDPARAMS,"reserved server object id");
        check(!id || (!self->clearing && !self->clearing_world && !record.info.removing),LT_NOTINITIALIZED,"server identity assigned during client object teardown");
        if (id) { auto [entry,inserted]=self->server_objects.emplace(*id,object); check(inserted || entry->second==object,LT_ALREADYEXISTS,"duplicate server object id"); }
        if (record.info.server_id && record.info.server_id!=id) {
            self->server_objects.erase(*record.info.server_id);
            std::replace(self->sky.objects.begin(),self->sky.objects.end(),*record.info.server_id,uint16(0xffff));
        }
        record.info.server_id=id; object->m_ObjectID=id.value_or(0xffff); if (id) record.info.lifetime=ClientObjectLifetime::World; return LT_OK; });
}
HOBJECT ClientScene::find_server_object(uint16 id) const { state_->ready(); const auto it=state_->server_objects.find(id); return it==state_->server_objects.end() ? nullptr : it->second; }
std::optional<uint16> ClientScene::server_id(HOBJECT object) const { state_->ready(); return state_->get(object).info.server_id; }
LTRESULT ClientScene::camera(HOBJECT object,const ClientCamera& camera) {
    const auto self=state_; return self->dispatch([&] { return self->camera(object,camera); });
}
std::optional<ClientCamera> ClientScene::camera(HOBJECT object) const {
    state_->thread(); if (!state_->owns(object) || object->m_ObjectType!=OT_CAMERA) return {};
    return state_->camera(object);
}
LTRESULT ClientScene::viewport(uint32 width,uint32 height) { const auto self=state_; return operation([&]() -> LTRESULT { self->ready(); check(width && height && width<=16384 && height<=16384,LT_INVALIDPARAMS,"invalid scene viewport"); self->viewport_width=width; self->viewport_height=height; return LT_OK; }); }
LTRESULT ClientScene::attach(HOBJECT parent,HOBJECT child,int socket,LTVector offset,LTRotation rotation,HATTACHMENT& output) {
    output=nullptr; const auto self=state_; return self->dispatch([&]() -> LTRESULT { self->get(parent); self->get(child);
        check(finite(offset) && valid_rotation(rotation),LT_INVALIDPARAMS,"invalid attachment transform");
        Attachment* attachment=nullptr; const auto result=am_CreateAttachment(&self->objects,parent,child,socket,&offset,&rotation,&attachment);
        if (result!=LT_OK) return result;
        const auto generation=attachment->m_Generation;
        try { self->follow(parent); }
        catch (...) {
            const auto* current=am_FindAttachment(&self->objects,reinterpret_cast<HATTACHMENT>(attachment));
            if (current && current->m_Generation==generation) am_RemoveAttachment(&self->objects,parent,attachment);
            throw;
        }
        const auto* current=self->live() ? am_FindAttachment(&self->objects,reinterpret_cast<HATTACHMENT>(attachment)) : nullptr;
        if (!current || current->m_Generation!=generation) return LT_NOCHANGE;
        output=reinterpret_cast<HATTACHMENT>(attachment); return LT_OK; });
}
LTRESULT ClientScene::detach(HATTACHMENT attachment) { const auto self=state_; return operation([&]() -> LTRESULT { self->ready(); auto* record=am_FindAttachment(&self->objects,attachment); return record ? am_RemoveAttachment(&self->objects,record->m_pParent,record) : LT_INVALIDPARAMS; }); }
LTRESULT ClientScene::process_attachments(HOBJECT object) { const auto self=state_; return self->dispatch([&]() -> LTRESULT { self->follow(object); return LT_OK; }); }
LTRESULT ClientScene::node_control(HOBJECT object,NodeControlFn function,void* user) {
    const auto self=state_; return self->dispatch([&]() -> LTRESULT {
        self->get(object); check(self->owns_model(object),LT_INVALIDPARAMS,"node control requires a model");
        auto* model=static_cast<ModelInstance*>(object); model->m_Unknown1fc=function; model->m_Unknown200=user; return LT_OK;
    });
}
std::vector<HOBJECT> ClientScene::containers(HOBJECT object) const {
    state_->ready(); if (!state_->owns(object)) return {}; const auto found=state_->container_links.find(object);
    return found==state_->container_links.end() ? std::vector<HOBJECT>{} : std::vector<HOBJECT>(found->second.begin(),found->second.end());
}
uint32 ClientScene::point_containers(LTVector point,HOBJECT* output,uint32 capacity) const {
    state_->ready(); if (!finite(point) || (capacity && !output)) return 0; uint32 count=0;
    state_->index.QueryPoint(point,[&](LTObject* object) { if (count<capacity && state_->owns(object) && object_IsPointInContainer(*object,point)) output[count++]=object; }); return count;
}
LTRESULT ClientScene::load_world(std::string_view name) { const auto self=state_; return self->dispatch([&] { return self->load_world(name); }); }
LTRESULT ClientScene::unload_world() { const auto self=state_; return self->dispatch([&]() -> LTRESULT { self->unload_world(); return LT_OK; }); }
std::string ClientScene::world_name() const { state_->ready(); return state_->world_path; }
HOBJECT ClientScene::main_world() const { state_->ready(); return state_->world_object; }
std::shared_ptr<const CServerWorld> ClientScene::world_data() const { state_->ready(); return state_->world; }
LTRESULT ClientScene::portal_flags(uint32 world,uint32 portal,uint16 flags) {
    const auto self=state_; return self->dispatch([&]() -> LTRESULT {
        if (!self->world || world>=self->world->m_Worlds.GetSize()) return LT_INVALIDPARAMS;
        auto* bsp=self->world->m_Worlds[world]->m_pWorldBsp; if (!bsp || portal>=bsp->m_nChunks) return LT_INVALIDPARAMS;
        bsp->m_pChunks[portal].m_Flags=flags; return LT_OK;
    });
}
SkyInfo ClientScene::sky() const { state_->ready(); return state_->sky; }
LTRESULT ClientScene::sky(const SkyInfo& value) {
    const auto self=state_; return self->dispatch([&]() -> LTRESULT {
        self->ready(); if (!valid_sky(value.definition)) return LT_INVALIDPARAMS; self->sky=value; return LT_OK;
    });
}
std::vector<MoveObjectReference> ClientScene::sky_objects() const {
    const auto self=state_; self->ready(); std::vector<MoveObjectReference> result; result.reserve(maximum_sky_objects);
    for (auto id : self->sky.objects) if (id!=0xffff) {
        const auto object=find_server_object(id);
        if (object && self->owns(object) && !self->records.at(object).info.removing) result.emplace_back(object);
    }
    return result;
}
std::optional<WorldPolygonGeometry> ClientScene::polygon_geometry(HPOLY handle,LTVector near_point) const {
    const auto self=state_; self->ready();
    if (!finite(near_point)) throw std::invalid_argument("nonfinite polygon reference point");
    if (!self->world) return {};
    const auto* polygon=self->world->FindPoly(handle); if (!polygon) return {};
    const auto* source=self->world->m_Worlds[handle>>16]->m_pWorldBsp;
    LTMatrix identity; identity.Identity();
    if (source->m_nUnknown48&(4|0x10|0x20)) return world_PolygonGeometry(*polygon,identity,identity);
    std::optional<WorldPolygonGeometry> nearest; double distance=std::numeric_limits<double>::infinity();
    for (const auto& [object,record] : self->records) {
        if (record.info.removing || object->m_pWorldBsp!=source || (object->m_ObjectType!=OT_WORLDMODEL && object->m_ObjectType!=OT_CONTAINER)) continue;
        const auto* brush=static_cast<CObjWorldModel*>(object);
        auto geometry=world_PolygonGeometry(*polygon,brush->m_Transform1f0,brush->m_Transform230);
        const auto separation=world_PolygonDistanceSquared(geometry,near_point);
        if (separation<distance) { distance=separation; nearest=std::move(geometry); }
    }
    return nearest ? std::move(nearest) : std::optional(world_PolygonGeometry(*polygon,identity,identity));
}
std::vector<MoveObjectReference> ClientScene::visible(LTVector position,float radius) const {
    const auto self=state_; self->ready();
    check(finite(position) && std::isfinite(radius) && radius>=0 && radius<=1e12f,LT_INVALIDPARAMS,"invalid client visibility query");
    std::vector<MoveObjectReference> result;
    struct Query { State& owner; std::vector<MoveObjectReference>& result; } context{*self,result};
    CVisQuery query; query.m_Position=position; query.m_Radius=radius; query.m_pUser=&context;
    query.m_Fn1=[](LTObject* object,void* user) {
        auto& context=*static_cast<Query*>(user);
        if (context.owner.owns(object) && !context.owner.records.at(object).info.removing) context.result.emplace_back(object);
    };
    self->index.FindObjects(&query); return result;
}
LTRESULT ClientScene::update(float elapsed) {
    const auto self=state_; return self->dispatch([&]() -> LTRESULT { self->ready(); check(std::isfinite(elapsed) && elapsed>=0 && elapsed<=1,LT_INVALIDPARAMS,"invalid scene timestep");
        const auto milliseconds=self->animation_remainder+double(elapsed)*1000; const auto advance=static_cast<uint32>(milliseconds); self->animation_remainder=milliseconds-advance;
        std::vector<MoveObjectReference> pending,parents;
        for (const auto& [object,record] : self->records) {
            if (record.info.removing) continue;
            if (object->m_pAttachments) parents.emplace_back(object);
            if (object->m_ObjectType==OT_MODEL) pending.emplace_back(object);
            if (object->m_ObjectType==OT_SPRITE) static_cast<CObjSprite*>(object)->UpdateAnimation(advance);
        }
        self->effects.update(elapsed,advance,[&](LTVector position) -> std::optional<float> {
            IntersectQuery query; IntersectInfo hit; query.m_From=position; query.m_To=position-LTVector(0,400,0);
            if (!w_IntersectSegment(&query,&hit,&self->index,0)) return {};
            return hit.m_Plane.m_Normal.y*hit.m_Plane.m_Dist-position.y;
        });
        for (const auto& object : pending) { if (!self->live()) break; if (!object.guard.unchanged()) continue;
            model_UpdateTrackers(*static_cast<ModelInstance*>(object.object),advance,State::model_key);
        }
        // As the original client, attachments follow their parents every
        // frame: an animation moves a socket without moving its model.
        for (const auto& object : parents) { if (!self->live()) break; if (object.guard.unchanged()) self->follow(object.object); }
        return LT_OK; });
}
ILTClientPhysics& ClientScene::physics() { state_->ready(); return state_->physics_api; }
void ClientScene::free_unused_models() { state_->ready(); state_->models.Prune(); state_->sprites.free_unused(); }
ILTModel& ClientScene::model() { state_->ready(); return state_->model_api; }
ILTCommon& ClientScene::common() { state_->ready(); return state_->common_api; }
ILTMath& ClientScene::math() { state_->ready(); return state_->math; }
ILTTransform& ClientScene::transform() { state_->ready(); return state_->transform; }
ILTLightAnim& ClientScene::lighting() { state_->ready(); return state_->lighting_api; }
ClientEffects& ClientScene::effects() { state_->ready(); return state_->effects; }
std::optional<ClientCanvas> ClientScene::canvas(HOBJECT object) const {
    state_->ready(); if (!state_->owns(object) || object->m_ObjectType!=OT_CANVAS) return {};
    const auto* canvas=static_cast<CObjCanvas*>(object); return ClientCanvas{canvas->m_DrawFn,canvas->m_DrawUser,canvas->m_VisRadius};
}
LTRESULT ClientScene::canvas_function(HOBJECT object,CanvasDrawFn function,void* user) {
    const auto self=state_; return self->dispatch([&]() -> LTRESULT {
        self->get(object); if (object->m_ObjectType!=OT_CANVAS) return LT_INVALIDPARAMS;
        auto* canvas=static_cast<CObjCanvas*>(object); canvas->m_DrawFn=function; canvas->m_DrawUser=user; return LT_OK;
    });
}
LTRESULT ClientScene::canvas_radius(HOBJECT object,float radius) {
    const auto self=state_; return self->dispatch([&]() -> LTRESULT {
        self->get(object); if (object->m_ObjectType!=OT_CANVAS || !std::isfinite(radius) || radius<0 || radius>1e12f) return LT_INVALIDPARAMS;
        static_cast<CObjCanvas*>(object)->m_VisRadius=radius; return LT_OK;
    });
}
}
