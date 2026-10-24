#include "engine/server/objtypes.h"
#include "engine/server/object_pool.h"
#include "engine/server/object_manager.h"
#include "engine/server/object_lifetime.h"
#include "engine/server/client_update.h"
#include "engine/server/commonmodel.h"
#include "engine/server/worldbsp.h"
#include "engine/server/servermath.h"
#include "engine/server/world_state.h"
#include "engine/portable/assets.h"
#include "engine/portable/asset_stream.h"
#include <cmath>
#include <iostream>
#include <memory>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
bool near(const LTVector& a, const LTVector& b) { return a.DistSqr(b) < .00001f; }

void objects() {
    LTObject object;
    require(object.m_ObjectID == 0xffff && object.m_ObjectID2 == 0xffff &&
            object.m_Mass == 30 && object.m_ColorA == 255 && !object.m_pAttachMgr,
            "native base initializes IDs, physics, color and ownership");
    object.SetPos({10,20,30}); object.SetDims({2,3,4});
    LTVector min, max; object.GetDims(&min,&max);
    require(near(min,{8,17,26}) && near(max,{12,23,34}), "all physics accessors use the same bounds");
    LTAnimTracker** tracker_link = reinterpret_cast<LTAnimTracker**>(1);
    require(!object.FindTracker(nullptr,&tracker_link) && !tracker_link, "non-model objects expose no animation tracker");
    LTMatrix matrix; object.GetTransformMatrix(&matrix);
    LTVector origin(0,0,0), transformed; MatVMul(&transformed,&matrix,&origin);
    require(near(transformed,object.m_Pos), "actual object virtual dispatch builds its transform");
    CObjLight light; CObjCamera camera; CObjCanvas canvas; CObjLineSystem lines;
    require(light.m_ObjectType == OT_LIGHT && light.GetLightRadius(nullptr) == light.m_LightRadius &&
            camera.m_ObjectType == OT_CAMERA && canvas._v2c() == 1 && lines.m_ObjectType == OT_LINESYSTEM,
            "allocated basic types share the canonical base and light state");
    CObjContainer container; LTObject* base = &container;
    ObjectCreateStruct create; create.m_ObjectType = OT_CONTAINER;
    create.m_Pos.Init(4,5,6); create.m_ContainerCode = 37; create.m_Flags = FLAG_SOLID;
    base->_v20(nullptr,&create);
    require(container.m_ContainerCode == 37 && near(base->m_Pos,{4,5,6}) && base->m_Flags == FLAG_SOLID,
            "container creation dispatches through LTObject without a layout cast");
    LTObject child;
    {
        LTObject parent;
        obj_SetStandingOn(&child,&parent,nullptr);
        require(parent.m_StandingOnMe.m_pNext == &child.m_StandingLink, "standing objects use native links");
    }
    require(!child.m_pStandingOn && child.m_StandingLink.IsTiedOff() && (child.m_ChangeFlags & CHANGEFLAG_MOTION),
            "parent destruction detaches and marks standing objects");
    StructBank attachments; sb_InitAligned(&attachments,sizeof(Attachment),4,alignof(Attachment));
    void* memory = sb_Allocate(&attachments);
    {
        LTObject parent;
        parent.m_pAttachmentBank = &attachments;
        parent.m_pAttachments = new (memory) Attachment{};
    }
    require(sb_Allocate(&attachments) == memory, "base destruction returns full native attachment records to their bank");
    sb_Free(&attachments,memory);
}
void callbacks_and_attachments() {
    CUnknown390 manager, foreign;
    ObjectCreateStruct create; create.m_ObjectType = OT_NORMAL;
    auto allocate = [&](uint16 id) {
        LTObject* object = nullptr;
        require(om_CreateObject(&manager,&create,&object) == LT_OK,"attachment endpoint allocation");
        object->m_ObjectID = id; return object;
    };
    auto* parent = allocate(10); auto* child = allocate(11); auto* grandchild = allocate(12);
    Attachment *first = nullptr, *second = nullptr;
    require(am_CreateAttachment(&manager,parent,child,-1,nullptr,nullptr,&first) == LT_OK &&
        am_CreateAttachment(&manager,child,grandchild,-1,nullptr,nullptr,&second) == LT_OK,"native attachment chain");
    const auto handle = reinterpret_cast<HATTACHMENT>(first);
    require(am_FindAttachment(&manager,handle) == first && !am_FindAttachment(&foreign,handle) &&
        !am_FindAttachment(&manager,reinterpret_cast<HATTACHMENT>(1)),"attachment handle lookup validates ownership without dereferencing foreign values");
    Attachment* rejected = reinterpret_cast<Attachment*>(1);
    require(am_CreateAttachment(&manager,grandchild,parent,-1,nullptr,nullptr,&rejected) == LT_ERROR && !rejected && manager.m_Attachments.size() == 2,
        "attachment cycle rejected before publication");
    require(am_CreateAttachment(&manager,parent,parent,-1,nullptr,nullptr,&rejected) == LT_INVALIDPARAMS &&
        am_CreateAttachment(&foreign,parent,child,-1,nullptr,nullptr,&rejected) == LT_INVALIDPARAMS,"self and foreign-manager attachments rejected");
    LTVector invalid(std::numeric_limits<float>::quiet_NaN(),0,0);
    require(am_CreateAttachment(&manager,parent,grandchild,-1,&invalid,nullptr,&rejected) == LT_INVALIDPARAMS && !rejected,"nonfinite attachment offsets rejected");
    const auto old_lifetime = child->m_Lifetime;
    const auto old_address = reinterpret_cast<std::uintptr_t>(child);
    CUpdateContext interest; interest.MarkVisible(*child);
    require(un390_RemoveObject(&manager,child) == LT_OK && !*old_lifetime && manager.m_Attachments.empty() && !parent->m_pAttachments &&
        !am_FindAttachment(&manager,handle),"child removal retires both incoming and outgoing attachment records");
    auto* replacement = allocate(11);
    require(reinterpret_cast<std::uintptr_t>(replacement) == old_address && !interest.IsVisible(*replacement),
        "client interest rejects a new object at exactly the same storage address");
    require(*replacement->m_Lifetime && replacement->m_Lifetime != old_lifetime && !am_FindAttachment(&manager,handle),
        "object ID and storage reuse cannot revive removed endpoint lifetime");
    require(am_CreateAttachment(&manager,parent,replacement,-1,nullptr,nullptr,&first) == LT_OK,"new endpoint attachment");
    const auto parent_lifetime = parent->m_Lifetime;
    require(un390_RemoveObject(&manager,parent) == LT_OK && !*parent_lifetime && manager.m_Attachments.empty() &&
        manager.m_Allocated.contains(replacement),"parent removal frees attachments while retaining child");
    ObjectCallbackGuard original(*replacement);
    alignas(SObjData) std::byte storage[sizeof(SObjData)];
    auto* data = new (storage) SObjData{}; replacement->m_pSData = data;
    require(!original.unchanged(),"callback guard detects world-data replacement");
    ObjectCallbackGuard data_guard(*replacement);
    data->~SObjData(); data = new (storage) SObjData{};
    require(!data_guard.unchanged(),"recycled world-data address has a different generation");
    data->~SObjData(); replacement->m_pSData = nullptr;
    un390_Term(&manager);
    auto* a = allocate(20); auto* b = allocate(21); auto* c = allocate(22);
    struct Context { CUnknown390& manager; ObjectCreateStruct& create; LTObject* a; LTObject* c; unsigned calls = 0; LTObject* replacement = nullptr; } context{manager,create,a,c};
    require(object_Visit(manager.m_Lists[OT_NORMAL],[](void* pointer,LTObject&) {
        auto& context = *static_cast<Context*>(pointer);
        if (!context.calls++) {
            un390_RemoveObject(&context.manager,context.a); un390_RemoveObject(&context.manager,context.c);
            require(om_CreateObject(&context.manager,&context.create,&context.replacement) == LT_OK,"reentrant object allocation");
        }
        return true;
    },&context) && context.calls == 2 && manager.m_Allocated.size() == 2 && manager.m_Allocated.contains(b),
        "object visit tolerates removal of current/later objects and excludes recycled new generation");
}
void attachment_callback_visits() {
    CUnknown390 manager;
    ObjectCreateStruct create; create.m_ObjectType = OT_NORMAL;
    LTObject *parent = nullptr, *a = nullptr, *b = nullptr;
    for (auto** output : {&parent,&a,&b}) require(om_CreateObject(&manager,&create,output) == LT_OK,"attachment callback endpoints");
    Attachment *a_link = nullptr, *b_link = nullptr;
    require(am_CreateAttachment(&manager,parent,a,-1,nullptr,nullptr,&a_link) == LT_OK &&
        am_CreateAttachment(&manager,parent,b,-1,nullptr,nullptr,&b_link) == LT_OK,"callback attachment setup");
    struct Context { CUnknown390& manager; LTObject* parent; LTObject* a; Attachment*& a_link; unsigned calls = 0; } context{manager,parent,a,a_link};
    require(am_VisitChildren(&manager,parent,[](void* pointer,LTObject&) {
        auto& ctx = *static_cast<Context*>(pointer); ++ctx.calls;
        require(am_RemoveAttachment(&ctx.manager,ctx.parent,ctx.a_link) == LT_OK,"callback removes later attachment");
        require(am_CreateAttachment(&ctx.manager,ctx.parent,ctx.a,-1,nullptr,nullptr,&ctx.a_link) == LT_OK,"callback recycles later attachment storage");
        return true;
    },&context) && context.calls == 1 && manager.m_Attachments.size() == 2,
        "attachment visit excludes newly allocated generation at the same address");
    const auto parent_lifetime = parent->m_Lifetime;
    context.calls = 0;
    require(am_VisitChildren(&manager,parent,[](void* pointer,LTObject&) {
        auto& ctx = *static_cast<Context*>(pointer); ++ctx.calls;
        un390_RemoveObject(&ctx.manager,ctx.parent); return true;
    },&context) && context.calls == 1 && !*parent_lifetime && manager.m_Attachments.empty(),
        "attachment visit can destroy parent and all memberships during notification");
    require(om_CreateObject(&manager,&create,&parent) == LT_OK,"new parent allocation");
    require(am_CreateAttachment(&manager,parent,a,-1,nullptr,nullptr,&a_link) == LT_OK &&
        am_CreateAttachment(&manager,parent,b,-1,nullptr,nullptr,&b_link) == LT_OK,"child destruction visit setup");
    struct Remove { CUnknown390& manager; LTObject* a; LTObject* b; unsigned calls = 0; } remove{manager,a,b};
    require(am_VisitChildren(&manager,parent,[](void* pointer,LTObject&) {
        auto& ctx = *static_cast<Remove*>(pointer); ++ctx.calls;
        un390_RemoveObject(&ctx.manager,ctx.a); un390_RemoveObject(&ctx.manager,ctx.b); return true;
    },&remove) && remove.calls == 1 && manager.m_Attachments.empty() && !parent->m_pAttachments,
        "attachment visit can destroy current and later children without following freed links");
}

void geometry_membership() {
    CWorldBsp world; world.m_Flags4c = 1;
    LTPlane plane; plane.m_Normal.Init(1,0,0); plane.m_Dist = 0;
    CWorldSurface surface; surface.m_pPlane = &plane;
    CWorldPart part{}; part.m_pSurface = &surface;
    world.m_n70 = new WorldPolyRef[1]; world.m_n74 = 1;
    auto& root = world.m_n70[0]; root.m_pOwner = &part; root.m_x = 0;
    root.m_Children[0] = static_cast<WorldPolyRef*>(g_pWorldTreeBackTerm);
    root.m_Children[1] = static_cast<WorldPolyRef*>(g_pWorldTreeFrontTerm);
    world.m_n9c = &root;
    auto groups = std::make_unique<CWorldGroupQT1[]>(1); world.m_p88 = groups.get(); world.m_n8c = 1;
    LTObject object;
    object.SetPos({10,0,0});
    require(obj_LinkIntoWorldTree(&world,&object) == &root && object.m_pTreeNode78 == &root &&
            root.m_head.m_pNext == &object.m_Link6c, "sphere membership links native objects into native BSP nodes");
    object.SetPos({-10,0,0});
    require(!obj_LinkIntoWorldTree(&world,&object) && !object.m_pTreeNode78 && root.m_head.IsTiedOff(),
            "objects behind the back terminator retain no stale geometry membership");
    auto grid = std::make_unique<CObjPolyGrid>();
    grid->m_Width = grid->m_Height = 16;
    require(obj_LinkIntoWorldTree(&world,grid.get()) == &root && !grid->m_Records.IsTiedOff() &&
            !groups[0].m_Link.IsTiedOff(), "poly grids publish native dual memberships");
    obj_LinkIntoWorldTree(&world,grid.get());
    require(grid->m_Records.m_pNext->m_pNext == &grid->m_Records, "relinking a grid releases its previous records");
    world.FreeTree();
    require(!grid->m_pTreeNode78 && grid->m_Link6c.IsTiedOff(), "geometry destruction detaches live object memberships");
    groups.reset();
    grid.reset(); // Both owners can disappear before the object.
}

void transforms() {
    CWorldBsp authored, instance;
    authored.m_Flags4c = instance.m_Flags4c = 1;
    LTVector point(1,2,3), moved;
    authored.m_pa8 = &point; instance.m_pa8 = &moved;
    authored.m_nac = instance.m_nac = 1;
    LTPlane plane, moved_plane;
    plane.m_Normal.Init(1,0,0); plane.m_Dist = 1;
    authored.m_p68 = &plane; instance.m_p68 = &moved_plane;
    authored.m_n6c = instance.m_n6c = 1;
    authored.m_vBoxMin.Init(-1,-2,-3); authored.m_vBoxMax.Init(1,2,3);
    CObjWorldModel object; object.SetWorldBsp(&authored,&instance); object.SetPos({10,20,30});
    require(object._v30() && !object._v34(), "movable world model requires instance geometry and is not the main world");
    authored.m_nUnknown48 = WORLDFLAG_MAINWORLD;
    object.SetWorldBsp(&authored,nullptr);
    require(!object._v30() && object._v34() == WORLDFLAG_MAINWORLD,
            "main-world point and segment tests preserve the original object virtual contract");
    object.SetWorldBsp(&authored,&instance);
    require(object._v14(4) == &object.m_Links1b4[4] && !object._v14(5), "world model extra-link queries are bounded native array accesses");
    obj_RetransformWorldModel(&object);
    require(near(moved,{11,22,33}) && moved_plane.m_Dist == 11 && near(point,{1,2,3}) && plane.m_Dist == 1,
            "world transforms update instance vertices and planes while preserving authored geometry");
    LTVector local; MatVMul(&local,&object.m_Transform230,&moved);
    require(near(local,point) && near(instance.m_vBoxMin,{9,18,27}) && near(instance.m_vBoxMax,{11,22,33}),
            "native world transform inverse and transformed bounds agree");
    object.SetPos({-2,0,0}); obj_RetransformWorldModel(&object);
    require(near(moved,{-1,2,3}) && moved_plane.m_Dist == -1, "successive transforms always start from authored geometry");
}

void manager() {
    CUnknown390 manager, other;
    ObjectCreateStruct create;
    LTObject* objects[NUM_OBJECTTYPES]{};
    for (uint32 type = 0; type < NUM_OBJECTTYPES; ++type) {
        create.m_ObjectType = static_cast<uint16>(type); create.m_Pos.Init(1,2,3);
        require(om_CreateObject(&manager,&create,objects + type) == LT_OK, "real native type factory allocates every object type");
        auto* object = objects[type];
        require(object->m_ObjectType == type && object->m_pAttachMgr == &manager &&
                object->m_pAttachmentBank == &manager.m_Bank4c && manager.m_Lists[type].m_nElements == 1 &&
                manager.m_Lists[type].m_Head.m_pNext == &object->m_Link7c,
                "all allocated objects use the manager's actual native lists and attachment bank");
    }
    auto* model = dynamic_cast<ModelInstance*>(objects[OT_MODEL]);
    require(model && static_cast<LTObject*>(model)->m_Lifetime == model->m_Lifetime,"model and base object share a single callback lifetime");
    require(model && model->m_pTrackerList == &model->m_AnimTracker && !model->_v2c(), "model factory and model accessor use the same native object");
    LTAnimTracker** link = nullptr;
    require(model->FindTracker(&model->m_AnimTracker,&link) && link == &model->m_pTrackerList,
            "model virtual dispatch returns the actual tracker list link");
    require(dynamic_cast<CObjParticleSystem*>(objects[OT_PARTICLESYSTEM]) && dynamic_cast<CObjSprite*>(objects[OT_SPRITE]),
            "particle and sprite factories create native subclasses");
    auto* sprite = static_cast<CObjSprite*>(objects[OT_SPRITE]);
    ILTSpriteControl& control = sprite->m_Control;
    uint32 count = 99;
    require(control.GetNumAnims(count) == LT_NOTINITIALIZED && !count, "unloaded sprite control reports its state through the SDK");
    uint32 frames[]{4,5,6}; SpriteAnim animation{frames,3,20}; Sprite resource{&animation,1};
    sprite->m_pSprite = &resource;
    require(control.GetNumAnims(count) == LT_OK && count == 1 && control.GetNumFrames(0,count) == LT_OK && count == 3,
            "sprite SDK control reads the allocated instance's native resource");
    require(control.SetCurPos(0,2) == LT_OK && sprite->m_FrameTime == 100, "sprite control computes native frame timing");
    uint32 anim = 99, frame = 99;
    require(control.GetCurPos(anim,frame) == LT_OK && !anim && frame == 2, "sprite SDK position round trips through native fields");
    animation.m_FrameRate = 0;
    require(control.SetCurPos(0,1) == LT_INVALIDPARAMS && sprite->m_pCurFrame == frames + 2,
            "zero frame rate preserves current playback without division by zero");
    require(control.SetFlags(SC_LOOP) == LT_OK && control.GetFlags(count) == LT_OK && count == SC_LOOP,
            "sprite flags use the actual SDK virtual interface");
    Attachment* attachment = nullptr;
    objects[OT_NORMAL]->m_ObjectID = 12;
    objects[OT_MODEL]->m_ObjectID = 13;
    require(am_CreateAttachment(&manager,objects[OT_NORMAL],objects[OT_MODEL],-1,nullptr,nullptr,&attachment) == LT_OK &&
            attachment->m_nParentID == 12 && attachment->m_nChildID == 13 && near(attachment->m_Offset.m_Pos,{0,0,0}),
            "native attachments initialize IDs and absent offsets");
    require(am_RemoveAttachment(&other,objects[OT_NORMAL],attachment) == LT_INVALIDPARAMS &&
            am_RemoveAttachment(&manager,objects[OT_NORMAL],attachment) == LT_OK && !objects[OT_NORMAL]->m_pAttachments,
            "attachments reject another manager and unlink from their real parent");
    am_CreateAttachment(&manager,objects[OT_NORMAL],objects[OT_MODEL],-1,nullptr,nullptr,&attachment);
    auto* particles = static_cast<CObjParticleSystem*>(objects[OT_PARTICLESYSTEM]);
    auto* particle = new (sb_Allocate(&manager.m_Bank14)) CParticle;
    particle->m_pNext = &particles->m_Head; particles->m_p1c8 = particles->m_p1e0 = particle;
    auto* lines = static_cast<CObjLineSystem*>(objects[OT_LINESYSTEM]);
    auto* segment = new (sb_Allocate(&manager.m_Bank30)) CLineSegment;
    segment->m_pNext = &lines->m_Head; lines->m_pCursorB = lines->m_pCursorA = segment;
    require(manager.Lock() == 1 && manager.Lock() == 2 && manager.GetLockCount() == 2 && objects[0]->m_Unknown58 == 0,
            "manager stamp sweep reaches native object fields");
    require(un390_RemoveObject(&other,objects[0]) == LT_NOTFOUND, "another manager cannot destroy an object");
    for (auto* object : objects) require(un390_RemoveObject(&manager,object) == LT_OK, "every native factory destroys its concrete type");
    require(un390_RemoveObject(&manager,objects[0]) == LT_NOTFOUND && manager.m_Allocated.empty(),
            "removed objects cannot be destroyed twice");
    require(sb_Allocate(&manager.m_Bank14) == particle && sb_Allocate(&manager.m_Bank30) == segment,
            "particle and line destructors recycle records through typed native banks");
    sb_Free(&manager.m_Bank14,particle); sb_Free(&manager.m_Bank30,segment);
    for (uint32 iteration = 0; iteration < 1000; ++iteration) {
        create.m_ObjectType = static_cast<uint16>(iteration % NUM_OBJECTTYPES);
        LTObject* object = nullptr;
        require(om_CreateObject(&manager,&create,&object) == LT_OK && object->m_pAttachments == nullptr &&
                object->m_ObjectID == 0xffff && un390_RemoveObject(&manager,object) == LT_OK,
                "recycling every native object type resets state without corrupting neighbouring slots");
    }
    create.m_ObjectType = NUM_OBJECTTYPES; LTObject* invalid = objects[0];
    require(om_CreateObject(&manager,&create,&invalid) == LT_INVALIDPARAMS && !invalid, "invalid object type allocates no list entry");
    create.m_ObjectType = OT_NORMAL;
    require(om_CreateObject(&manager,&create,&invalid) == LT_OK, "manager remains usable after a rejected request");
    un390_Term(&manager); un390_Term(&manager);
    require(manager.m_Allocated.empty() && !manager.m_Lists[OT_NORMAL].m_nElements, "manager termination destroys live objects and repeats safely");
    un390_Init(&manager,1);
    require(om_CreateObject(&manager,&create,&invalid) == LT_OK, "terminated manager can start a dedicated world");
}

void original_models(const std::filesystem::path& path) {
    nolf::Assets assets; assets.mount_game(path);
    CServerWorld world; CUnknown390 manager;
    uint64 worlds = 0, models = 0, terrain = 0, shadowed = 0;
    ObjectCreateStruct create; create.m_ObjectType = OT_WORLDMODEL;
    for (const auto& name : assets.names()) {
        if (!name.starts_with("worlds/") || !name.ends_with(".dat")) continue;
        nolf::AssetStream stream(assets.locate(name)); WorldLoadRequest request; request.m_pStream = &stream;
        if (world.Load(&request)) throw std::runtime_error("object world load failed: " + name + ": " + world.LastLoadError());
        for (const auto& record : world.Geometry()->models) {
            auto* geometry = record->m_pWorldBsp;
            LTObject* object = nullptr;
            require(om_CreateObject(&manager,&create,&object) == LT_OK, "original world model has a native engine object");
            auto* selected = world.FindWorldModel(geometry->m_Name);
            if (!selected || !world.AttachWorldModel(object,geometry->m_Name) || object->m_pWorldBsp != selected->m_pWorldBsp || object->m_pOriginalBsp != selected->m_pOriginal)
                throw std::runtime_error("native model attachment mismatch: " + name + ": " + geometry->m_Name);
            // Retail levels contain duplicate brush-model names. Name lookup
            // preserves the original first-match rule, including case folding.
            if (selected != record.get()) {
                un390_RemoveObject(&manager,object); ++shadowed; continue;
            }
            object->SetPos(geometry->m_ve8);
            object->SetDims((geometry->m_vBoxMax - geometry->m_vBoxMin) * .5f);
            obj_RetransformWorldModel(object);
            if (record->m_pOriginal && geometry->m_nac) {
                const auto source = static_cast<LTVector*>(geometry->m_pa8)[0];
                const auto transformed = static_cast<LTVector*>(record->m_pOriginal->m_pa8)[0];
                require(near(source,transformed), "native instance transforms preserve authored vertices at the original origin");
            }
            world.m_Tree.BumpObject(object,0);
            bool found = false;
            world.m_Tree.QueryPoint(object->m_Pos,[&](LTObject* candidate) { if (candidate == object) found = true; });
            require(found, "original world model is spatially queryable through the actual allocated object's bounds");
            static_cast<CObjWorldModel*>(object)->LinkIntoTree(object);
            un390_RemoveObject(&manager,object);
            require(!world.m_Tree.Size(), "destroying original world model detaches native world membership");
            ++models;
            for (uint32 index = 0; index < geometry->m_Array138.GetSize(); ++index) {
                require(om_CreateObject(&manager,&create,&object) == LT_OK, "original terrain has a native engine object");
                auto terrain_name = "#$#" + std::to_string(index) + " " + geometry->m_Name;
                require(world.AttachWorldModel(object,terrain_name.data()) && object->m_pWorldBsp == &geometry->m_Array138[index] &&
                        object->m_pWorldBsp->IsBusy(), "original terrain names attach native terrain geometry and layout bounds");
                world.m_Tree.BumpObject(object,0);
                un390_RemoveObject(&manager,object);
                require(!world.m_Tree.Size(), "destroying original terrain releases its spatial membership");
                ++terrain;
            }
        }
        world.Term(); ++worlds;
        require(manager.m_Allocated.empty() && !g_WorldBytes, "engine objects and geometry release between original levels");
    }
    require(worlds && models, "original world model test used local assets");
    std::cout << "constructed native objects for " << models << " original model names and " << terrain << " terrain sections across " << worlds << " worlds; verified " << shadowed << " shadowed model names\n";
}
}
int main(int argc, char** argv) {
    try {
        objects(); callbacks_and_attachments(); attachment_callback_visits(); geometry_membership(); transforms(); manager();
        if (argc == 3 && std::string_view(argv[1]) == "--data") original_models(argv[2]);
        std::cout << "native object lifetime, factories and geometry tests passed\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
