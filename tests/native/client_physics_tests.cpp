#include "engine/client/client_physics.h"
#include "engine/server/object_manager.h"
#include "engine/server/objtypes.h"
#include "engine/server/world_state.h"
#include "engine/server/worldbsp.h"
#include "engine/server/world_clip.h"
#include "engine/portable/asset_stream.h"
#include "engine/portable/wire.h"
#include "platform/logging.h"
#include "physics_test_world.h"
#include "ltserverobj.h"
#include <barrier>
#include <future>
#include <functional>
#include <iostream>
#include <limits>
#include <cstring>
#include <atomic>
#include <stdexcept>

namespace {
std::atomic<unsigned> solver_errors{0};
void diagnostic(const char* subsystem,std::size_t subsystem_size,const char* text,std::size_t size) {
    const std::string_view message(text,size);
    if (message.find("physics recursed infinitely")!=std::string_view::npos || message.find("doesn't have PBlocks")!=std::string_view::npos) ++solver_errors;
    std::clog << '[' << std::string_view(subsystem,subsystem_size) << "] " << message << '\n';
}
void require(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
bool close(LTVector a,LTVector b) { return a.DistSqr(b)<.0001f; }
struct World final : nolf::ClientPhysicsWorld {
    struct Mover final : IObjectMover {
        World& world;
        CollisionInfo* info=nullptr;
        explicit Mover(World& owner) : world(owner) {}
        void SetObjectChangeFlags(LTObject* object,uint32 flags) override { object->m_ChangeFlags|=flags; }
        CollisionInfo** GetCollisionInfo() override { return &info; }
        void TouchNotify(LTObject* first,HOBJECT second,LTVector*,float) override { if (world.touch) world.touch(first,second); }
        void Attach(LTObject*,LTObject*) override {}
        void Detach(LTObject*) override {}
        void UpdateAttachments(WMMoveState*) override {}
        int ObjectBlocks(WMMoveState* state,LTObject*,LTObject* other) override { return state->m_nUnknown0C>other->m_BlockingPriority && other->_v30(); }
        uint32 Crush(LTObject*,void*) override { return 0; }
        void CheckMaxPos(WMMoveState*,LTVector*) override {}
        bool IsServer() const override { return false; }
        LTBOOL CanPlace(LTObject*) override { return LTFALSE; }
        const char* GetObjectClassName(LTObject*) override { return world.label.c_str(); }
        ILTPhysics* Physics() override { return &world.physics; }
    } mover_service{*this};
    CUnknown390 objects;
    CUnknown210 index;
    nolf::ClientPhysics physics{*this};
    std::function<void(LTObject*,LTObject*)> touch;
    std::string label="ClientPhysicsFixture";
    bool available=true;
    World() { index.Init(&objects); }
    ~World() { index.Term(); }
    bool owns(HOBJECT object) const noexcept override { return objects.m_Allocated.contains(object); }
    CUnknown210* spatial() noexcept override { return available ? &index : nullptr; }
    IObjectMover* mover() noexcept override { return available ? &mover_service : nullptr; }
    LTObject* create(uint8 type=OT_NORMAL,LTVector pos={0,0,0}) {
        ObjectCreateStruct create; create.m_ObjectType=type; create.m_Pos=pos; create.m_Flags=FLAG_SOLID|FLAG_TOUCH_NOTIFY;
        LTObject* out=nullptr; require(om_CreateObject(&objects,&create,&out)==LT_OK,"native client physics fixture object allocation");
        index.BumpObject(out,0); return out;
    }
};
void properties() {
    World world; ILTClientPhysics& api=world.physics; auto* body=world.create(); LTObject foreign;
    float value=0; LTVector v;
    require(api.GetObjectMass(body,value)==LT_OK && value==30 && api.SetObjectMass(body,42)==LT_OK && body->m_Mass==42,"actual client physics virtual property dispatch");
    require(api.GetObjectMass(&foreign,value)==LT_INVALIDPARAMS && api.SetVelocity(reinterpret_cast<HOBJECT>(1),&v)==LT_INVALIDPARAMS,"foreign physics handles dereferenced");
    const float nan=std::numeric_limits<float>::quiet_NaN();
    require(api.SetObjectMass(body,nan)==LT_INVALIDPARAMS && body->m_Mass==42 && api.SetFrictionCoefficient(body,-1)==LT_INVALIDPARAMS &&
        api.SetForceIgnoreLimit(body,std::numeric_limits<float>::infinity())==LT_INVALIDPARAMS,"invalid physics scalars changed state");
    require(api.GetStairHeight(value)==LT_OK && value==-1 && api.SetStairHeight(18)==LT_OK && api.GetStairHeight(value)==LT_OK && value==18 &&
        api.SetStairHeight(nan)==LT_INVALIDPARAMS,"native stair state required by original shell creation");
    require(api.GetGlobalForce(v)==LT_OK && close(v,{0,-2000,0}),"default native client gravity");
    LTVector gravity{0,-1000,0}; require(api.SetGlobalForce(gravity)==LT_OK,"client gravity update");
    LTVector velocity{10,0,0},acceleration{4,0,0}; api.SetVelocity(body,&velocity); api.SetAcceleration(body,&acceleration);
    MoveInfo move{body,.5f,{99,99,99}};
    require(api.UpdateMovement(&move)==LT_OK && close(move.m_Offset,{5.25f,0,0}) && close(body->m_Velocity,{12,0,0}),
        "client update did not use the retail shared quarter-acceleration position term");
    body->m_ChangeFlags&=~CHANGEFLAG_MOTION; move.m_Offset.Init(99,99,99);
    require(api.UpdateMovement(&move)==LT_OK && close(move.m_Offset,{0,0,0}),"inactive client movement retained a stale output offset");
    move.m_dt=nan; move.m_Offset.Init(99,99,99);
    require(api.UpdateMovement(&move)==LT_INVALIDPARAMS && close(move.m_Offset,{99,99,99}),"invalid timestep committed a movement result");
    CollisionInfo collision{}; collision.m_hObject=&foreign; collision.m_hPoly=123;
    require(api.GetStandingOn(body,&collision)==LT_OK && !collision.m_hObject && collision.m_hPoly==INVALID_HPOLY,"empty standing contact left stale handles");
    auto wrong=std::async(std::launch::async,[&] { LTVector force; return api.SetVelocity(body,&velocity)==LT_INVALIDPARAMS && api.GetGlobalForce(force)==LT_ERROR; });
    require(wrong.get(),"client physics allowed access from a foreign engine thread");
    world.available=false; LTVector dimensions{2,3,4};
    require(api.SetObjectDims(body,&dimensions,0)==LT_NOTINITIALIZED && api.MoveObject(body,&velocity,0)==LT_NOTINITIALIZED,"missing scene reported successful physics");
}
void collisions() {
    World world; auto* body=world.create(OT_NORMAL,{-10,0,0}); auto* blocker=world.create();
    blocker->m_BlockingPriority=255; blocker->SetDims({2,5,5}); world.index.BumpObject(blocker,0);
    LTVector dims{2,2,2}; require(world.physics.SetObjectDims(body,&dims,0)==LT_OK,"client dimensions did not reach shared collision worker");
    LTVector target{10,0,0};
    require(world.physics.MoveObject(body,&target,0)==LT_OK && body->m_Pos.x<0 && blocker->m_Pos.x==0,"client box movement passed through a higher-priority solid");
    blocker->m_Flags|=FLAG_CLIENTNONSOLID; body->SetPos({-10,0,0}); world.index.BumpObject(body,0);
    require(world.physics.MoveObject(body,&target,0)==LT_OK && close(body->m_Pos,target),"client-only nonsolid flag was ignored by shared worker");
    blocker->m_Flags&=~FLAG_CLIENTNONSOLID; body->SetPos({-10,0,0}); world.index.BumpObject(body,0);
    require(world.physics.MovePushObjects(body,target,nullptr,0)==LT_OK && close(body->m_Pos,target),"empty explicit push list queried every world object");
    body->SetPos({-10,0,0}); world.index.BumpObject(body,0); HOBJECT named[]{blocker};
    require(world.physics.MovePushObjects(body,target,named,1)==LT_OK && body->m_Pos.x<0,"explicit push list lost its solid blocker");
    HOBJECT invalid[]{reinterpret_cast<HOBJECT>(1)}; const auto before=body->m_Pos;
    require(world.physics.MovePushObjects(body,target,invalid,1)==LT_INVALIDPARAMS && close(body->m_Pos,before),"foreign explicit push entry changed scene before validation");
    require(world.physics.RotatePushObjects(body,body->m_Rotation,nullptr,0)==LT_INVALIDPARAMS,"rotation accepted a non-world-model object");
}
void floor_and_threads(std::barrier<>* rendezvous=nullptr,int identity=0) {
    TestPhysicsWorld geometry(true); World world;
    auto* floor=world.create(OT_WORLDMODEL); floor->SetWorldBsp(&geometry,&geometry); floor->SetDims({1000,1024,1000}); floor->m_BlockingPriority=255;
    world.index.BumpObject(floor,0);
    auto* body=world.create(OT_NORMAL,{0,200,0}); LTVector dims{2,4,2}; require(world.physics.SetObjectDims(body,&dims,0)==LT_OK,"floor fixture client body dimensions");
    if (rendezvous) {
        g_fBoxRadius=float(identity); g_vMoveStart.Init(float(identity),0,0); rendezvous->arrive_and_wait();
        require(g_fBoxRadius==identity && g_vMoveStart.x==identity,"shared collision scratch leaked between client engine threads");
    }
    for (unsigned iteration=0;iteration<100;++iteration) {
        body->SetPos({0,200,0}); world.index.BumpObject(body,0); LTVector target{0,-800,0};
        require(world.physics.MoveObject(body,&target,0)==LT_OK && body->m_Pos.y>-297 && body->m_Pos.y<-295,"native client long fall crossed original-style BSP floor");
        CollisionInfo standing{}; require(world.physics.GetStandingOn(body,&standing)==LT_OK && standing.m_hObject==floor && standing.m_hPoly!=INVALID_HPOLY,
            "native client floor contact did not preserve object/polygon identity");
    }
}
void restricted_callbacks() {
    World world; auto* body=world.create(OT_NORMAL,{-100,0,0}); body->m_BlockingPriority=200;
    std::vector<HOBJECT> named;
    for (float x : {0.f,20.f,40.f}) {
        auto* object=world.create(OT_NORMAL,{x,0,0}); object->m_Flags=FLAG_TOUCH_NOTIFY; named.push_back(object);
    }
    const auto candidates=named; bool called=false; LTObject* replacement=nullptr;
    world.touch=[&](LTObject* first,LTObject* second) {
        if (called) return;
        called=true;
        auto it=std::find_if(candidates.begin(),candidates.end(),[&](auto* candidate) { return candidate!=first && candidate!=second; });
        require(it!=candidates.end(),"callback removal fixture needs a later candidate");
        require(un390_RemoveObject(&world.objects,*it)==LT_OK,"callback could not retire supplied push candidate");
        named.clear(); named.shrink_to_fit(); // The caller also retires its original list storage.
        replacement=world.create(OT_NORMAL,{10000,0,0});
    };
    LTVector end{100,0,0};
    require(world.physics.MovePushObjects(body,end,named.data(),static_cast<uint32>(named.size()))==LT_OK,"push callback move failed");
    require(called && replacement,"explicit client push fixture did not issue its contact callback");
    require(replacement->m_Pos.x==10000,"push callback visited a recycled candidate");
    require(!world.mover_service.info,"push callback retained transient collision state");
}
void original_worlds(const char* data,std::string_view filter={}) {
    nolf::Assets assets; assets.mount_game(data);
    std::size_t worlds=0,starts=0,sweeps=0,contacts=0;
    for (auto name : assets.names()) {
        if (!name.starts_with("worlds/") || !name.ends_with(".dat")) continue;
        if (!filter.empty() && name!=filter) continue;
        CServerWorld geometry_owner; nolf::AssetStream stream(assets.locate(name)); WorldLoadRequest request; request.m_pStream=&stream;
        require(geometry_owner.Load(&request)==0,"original client physics world could not load");
        auto* geometry=geometry_owner.FindFlagged10(); require(geometry,"original world has no main physics BSP");
        World scene; auto* main=scene.create(OT_WORLDMODEL,geometry->m_ve8);
        require(geometry_owner.AttachWorldModel(main,geometry->m_Name)!=0,"original client physics world model could not attach");
        main->m_BlockingPriority=255; main->SetDims((geometry->m_vBoxMax-geometry->m_vBoxMin)*.5f);
        obj_RetransformWorldModel(main); scene.index.BumpObject(main,0);
        for (const auto& definition : geometry_owner.ObjectDefinitions()) {
            if (definition.class_name!="GameStartPoint") continue;
            for (const auto& property : definition.properties) {
                if (property.name!="Pos" || property.type!=PT_VECTOR || property.data.size()!=12) continue;
                LTVector start;
                for (uint32 axis=0;axis<3;++axis) start[axis]=nolf::wire::load<float>(property.data.data()+axis*4);
                ++starts; start.y+=16;
                auto end=start-LTVector{0,4096,0}; LTVector floor; LTPlane plane;
                auto* hit=bsp_IntersectLine(geometry->GetRoot(),&start,&end,&floor,&plane);
                if (!hit || plane.m_Normal.y<.5f || start.y-floor.y<8) continue;
                LTVector dimensions{2,4,2},minimum=start-dimensions,maximum=start+dimensions;
                if (ws_GetBoxStatus(geometry->GetRoot(),&minimum,&maximum)) continue;
                for (uint32 stairs : {uint32(0),uint32(FLAG_STAIRSTEP)}) {
                    scene.label=name+" start("+std::to_string(start.x)+","+std::to_string(start.y)+","+std::to_string(start.z)+") stairs="+std::to_string(stairs);
                    auto* body=scene.create(OT_NORMAL,start); body->m_Flags=FLAG_SOLID|stairs;
                    require(scene.physics.SetObjectDims(body,&dimensions,0)==LT_OK,"original client sweep dimensions failed");
                    auto destination=end; require(scene.physics.MoveObject(body,&destination,0)==LT_OK,"original client BSP sweep failed");
                    const auto result=body->m_Pos;
                    const bool vertical=std::fabs(result.x-start.x)<.1f && std::fabs(result.z-start.z)<.1f;
                    const auto* contact=body->m_pStandingOnPoly ? body->m_pStandingOnPoly->GetPlane() : nullptr;
                    const auto clearance=contact ? contact->m_Normal.Dot(result)-contact->m_Dist : 0;
                    const auto support=contact ? phys_SupportRadius(dimensions,body->m_Flags,body->m_Flags2,contact->m_Normal) : 0;
                    auto core_min=body->m_BBoxMin+.2f,core_max=body->m_BBoxMax-.2f;
                    bool overlap=false;
                    if (ws_GetBoxStatus(geometry->GetRoot(),&core_min,&core_max))
                        for (uint32 part=0;part<geometry->m_nParts && !overlap;++part) {
                            const auto* face=geometry->m_ppParts[part];
                            if (face->m_pParent->m_nFlags&WBSP_SOLID) overlap=w_PolygonTouchesBox(*face,core_min,core_max);
                        }
                    if (!std::isfinite(result.x) || !std::isfinite(result.y) || !std::isfinite(result.z) || result.y>start.y+.5f || overlap ||
                        (vertical && result.y<floor.y-.15f) || (contact && (clearance<support-.15f || clearance>support+1)))
                        throw std::runtime_error("original native client BSP sweep mismatch: "+name);
                    if (contact) ++contacts;
                    require(un390_RemoveObject(&scene.objects,body)==LT_OK,"original client sweep object release failed"); ++sweeps;
                }
            }
        }
        ++worlds;
    }
    require(worlds && sweeps,"original client physics corpus produced no sweeps");
    std::cout << "Original native CLIENT physics: " << worlds << " worlds, " << starts << " starts, " << sweeps << " sweeps, " << contacts << " supporting polygons\n";
}
}
int main(int argc,char** argv) {
    nolf::platform::set_log_sink(diagnostic);
    struct ResetLog { ~ResetLog() { nolf::platform::set_log_sink(nullptr); } } reset;
    try { properties(); collisions(); floor_and_threads(); restricted_callbacks();
        std::barrier rendezvous(2);
        auto a=std::async(std::launch::async,[&] { floor_and_threads(&rendezvous,17); });
        auto b=std::async(std::launch::async,[&] { floor_and_threads(&rendezvous,31); }); a.get(); b.get();
        if (argc>=2) original_worlds(argv[1],argc>=3 ? argv[2] : "");
        require(solver_errors==0,"native client physics exhausted its solver or lacked collision geometry");
        std::cout << "Native ILTClientPhysics dispatch, owned scene handles, shared collision/integration and parallel engine scratch passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
