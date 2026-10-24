#include "save_runtime.h"
#include "save_message.h"
#include "save_objects.h"
#include "servermgr.h"
#include "console_checkpoint.h"
#include "commonmodel.h"
#include "servermodel.h"
#include "engine/portable/file_stream.h"
#include "engine/portable/memory_stream.h"
#include "platform/system.h"
#include "platform/user_files.h"
#include <algorithm>
#include <bit>
#include <cstring>
#include <memory>

float time_GetTime();
LTRESULT sm_CheckObjectIntegrityQ474(CServerMgr*);
LTBOOL at_SetAnim(LTAnimTracker*,uint32,LTBOOL);
void at_SetTime(LTAnimTracker*,uint32,LTBOOL);

namespace nolf::save {
namespace {
LTVector vector(const SavedVector& value) { return {value[0],value[1],value[2]}; }
LTRotation rotation(const SavedRotation& value) { return {value[0],value[1],value[2],value[3]}; }
template<std::size_t N> void text(char (&output)[N],const std::string& input) {
    require(input.size() < N,LT_CANTRESTOREOBJECT,"saved object name exceeds SDK buffer");
    std::memcpy(output,input.data(),input.size()); output[input.size()] = 0;
}
std::array<uint32,7> timing(const CServerMgr& manager) {
    return {manager.m_Unknown6c4,std::bit_cast<uint32>(manager.m_Unknown6bc),std::bit_cast<uint32>(manager.m_Unknown6c0),
        static_cast<uint32>(manager.m_Unknown6d0),std::bit_cast<uint32>(manager.m_Unknown6c8),std::bit_cast<uint32>(manager.m_Time),std::bit_cast<uint32>(manager.m_Unknown6b8)};
}
void timing(CServerMgr& manager,const std::array<uint32,7>& words) {
    manager.m_Unknown6c4 = words[0]; manager.m_Unknown6bc = std::bit_cast<float>(words[1]); manager.m_Unknown6c0 = std::bit_cast<float>(words[2]);
    manager.m_Unknown6d0 = std::bit_cast<int32>(words[3]); manager.m_Unknown6c8 = std::bit_cast<float>(words[4]);
    manager.m_Time = std::bit_cast<float>(words[5]); manager.m_Unknown6b8 = std::bit_cast<float>(words[6]);
}
struct Actor {
    LTObject* object;
    ObjectCallbackGuard guard;
};
class Restoration {
public:
    explicit Restoration(CServerMgr& manager) : manager(manager), server(manager.m_Lifetime), console(manager.m_ConsoleState), before_time(timing(manager)) {
        std::copy(std::begin(manager.m_SkyObjects),std::end(manager.m_SkyObjects),before_sky.begin());
        for (uint32 i = 0; i < manager.m_World.m_Worlds.GetSize(); ++i) {
            const auto* geometry = manager.m_World.m_Worlds[i]->m_pWorldBsp;
            auto& flags = before_portals.emplace_back(); flags.reserve(geometry->m_nChunks);
            for (uint32 p = 0; p < geometry->m_nChunks; ++p) flags.push_back(geometry->m_pChunks[p].m_Flags);
        }
    }
    ~Restoration() {
        if (!committed && server.unchanged()) {
            for (auto iterator = actors.rbegin(); iterator != actors.rend() && server.unchanged(); ++iterator) {
                if (!iterator->guard.same_data() || !iterator->object->m_pSData || !iterator->object->m_pSData->m_pBaseClass) continue;
                try { sm_RemoveObjectFromWorld(&manager,iterator->object->m_pSData->m_pBaseClass); }
                catch (const std::exception& error) { platform::log("restore rollback",error.what()); }
            }
            if (server.unchanged()) {
                timing(manager,before_time);
                std::copy(before_sky.begin(),before_sky.end(),std::begin(manager.m_SkyObjects));
                for (uint32 i = 0; i < before_portals.size(); ++i) {
                    auto* geometry = manager.m_World.m_Worlds[i]->m_pWorldBsp;
                    for (uint32 p = 0; p < before_portals[i].size(); ++p) geometry->m_pChunks[p].m_Flags = before_portals[i][p];
                }
                console.Restore(); sm_MarkSkyChanged(&manager);
            }
        }
        if (server.alive()) manager.m_Restoring = false;
    }
    CServerMgr& manager;
    ServerCallbackGuard server;
    ConsoleCheckpoint console;
    std::array<uint32,7> before_time;
    std::array<uint16,30> before_sky;
    std::vector<std::vector<uint16>> before_portals;
    std::vector<Actor> actors;
    bool committed = false;
};
ObjectCreateStruct creation(const SavedObject& saved) {
    ObjectCreateStruct result;
    result.m_ObjectType = saved.type; result.m_ContainerCode = saved.container_code;
    result.m_Flags = saved.flags; result.m_Flags2 = saved.flags2;
    result.m_Pos = vector(saved.position); result.m_Scale = vector(saved.scale); result.m_Rotation = rotation(saved.rotation);
    result.m_NextUpdate = saved.next_update;
    text(result.m_ClassName,saved.class_name); text(result.m_Name,saved.name); text(result.m_Filename,saved.filename);
    text(result.m_SkinNames[0],saved.skins[0]); text(result.m_SkinNames[1],saved.skins[1]);
    return result;
}
void state(CServerMgr& manager,LTObject& object,const SavedObject& saved) {
    auto* data = object.m_pSData;
    object.m_UserFlags = saved.user_flags; object.m_BlockingPriority = saved.blocking_priority;
    object.m_ColorR = saved.color[0]; object.m_ColorG = saved.color[1]; object.m_ColorB = saved.color[2]; object.m_ColorA = saved.color[3];
    object.m_Velocity = vector(saved.velocity); object.m_Acceleration = vector(saved.acceleration);
    object.m_FrictionCoefficient = saved.friction; object.m_Mass = saved.mass; object.m_ForceIgnoreLimitSqr = saved.force_ignore_limit_squared;
    if (saved.type == OT_LIGHT) object.m_LightRadius = saved.light_radius;
    if (saved.sfx_message) {
        auto message = std::make_unique<ServerMessage>();
        const auto count = static_cast<uint16>(saved.sfx_message->size());
        message->Init(std::max<uint16>(1,count),4096);
        require(!(message->m_Flags & PACKETFLAG_WRITEOVERFLOW),LT_OUTOFMEMORY,"restored SFX allocation");
        if (count) std::memcpy(message->m_pData,saved.sfx_message->data(),count);
        message->m_DataLen = count; message->m_Pos = 1;
        data->m_SFXMessage.Assign(message.release());
    }
    if (saved.animation) {
        auto* model = dynamic_cast<ModelInstance*>(&object);
        require(model,LT_CANTRESTOREOBJECT,"restored model type mismatch");
        auto& tracker = model->m_AnimTracker;
        require(at_SetAnim(&tracker,saved.animation->index,LTFALSE),LT_CANTRESTOREOBJECT,"restored animation unavailable");
        at_SetTime(&tracker,saved.animation->time,LTFALSE); tracker.m_Flags = saved.animation->flags;
    }
    object.SetDims(vector(saved.dimensions));
    if (saved.sky_slot) manager.m_SkyObjects[*saved.sky_slot] = object.m_ObjectID;
}
}
LTRESULT apply(CServerMgr& manager,const SavedGame& document,uint32 parameter,uint32 flags) {
    require(manager.m_Lifetime->alive && !manager.m_Restoring && !manager.m_Terminating && !manager.m_EndingWorld,
        LT_NOTINITIALIZED,"restore already active or server is shutting down");
    if (!document.objects.empty()) require(manager.m_bWorldLoaded && manager.m_pUnknown6a8,LT_NOTINWORLD,"restore requires a loaded server world");
    std::vector<ClassDef*> definitions; definitions.reserve(document.objects.size());
    std::vector<ObjectCreateStruct> creates; creates.reserve(document.objects.size());
    for (const auto& object : document.objects) {
        auto* definition = manager.m_ClassMgr.definition(object.class_name);
        require(definition && !(definition->m_ClassFlags & CF_NORUNTIME),LT_CANTRESTOREOBJECT,"restored game class unavailable");
        definitions.push_back(definition); creates.push_back(creation(object));
        for (auto dimension : object.dimensions) require(dimension >= 0,LT_INVALIDFILE,"negative saved object dimensions");
    }
    if (document.portal_flags) {
        require(document.portal_flags->size() == manager.m_World.m_Worlds.GetSize(),LT_NOTINITIALIZED,"saved world model count");
        for (uint32 i = 0; i < document.portal_flags->size(); ++i)
            require((*document.portal_flags)[i].size() == manager.m_World.m_Worlds[i]->m_pWorldBsp->m_nChunks,LT_NOTINITIALIZED,"saved portal count");
    }
    Restoration operation(manager);
    operation.actors.reserve(document.objects.size());
    manager.m_Restoring = true;
    if (flags & RESTOREOBJECTS_RESTORETIME) timing(manager,document.timing_words);
    manager.m_Unknown6b8 = manager.m_Unknown6c0 - (time_GetTime() + manager.m_Unknown6b8);
    if (document.portal_flags) for (uint32 i = 0; i < document.portal_flags->size(); ++i) {
        auto* geometry = manager.m_World.m_Worlds[i]->m_pWorldBsp;
        for (uint32 p = 0; p < (*document.portal_flags)[i].size(); ++p) geometry->m_pChunks[p].m_Flags = (*document.portal_flags)[i][p];
    }
    for (const auto& [name,value] : document.console_variables) {
        require(manager.m_ConsoleState.m_Initialized,LT_NOTINITIALIZED,"restore console unavailable");
        con_SetVar(&manager.m_ConsoleState,name.c_str(),value.c_str());
        require(operation.server.unchanged(),LT_ERROR,"world changed during console restore");
        auto* variable = reinterpret_cast<LTCommandVar*>(con_FindVar(&manager.m_ConsoleState,name.c_str()));
        require(variable && variable->m_pStringVal && value == variable->m_pStringVal,LT_ERROR,"console restore failed");
    }
    // All native objects exist before any game-owned references are decoded.
    std::vector<LTObject*> pointers; pointers.reserve(document.objects.size());
    for (std::size_t i = 0; i < document.objects.size(); ++i) {
        require(operation.server.unchanged(),LT_ERROR,"world changed during restore");
        auto* game = sm_AllocateObject(&manager,definitions[i]);
        require(game,LT_CANTRESTOREOBJECT,"restored game object allocation");
        LTObject* object = nullptr;
        try {
            require(manager.m_ClassMgr.message(game,MID_PRECREATE,&creates[i],PRECREATE_SAVEGAME) != 0,LT_CANTRESTOREOBJECT,"game refused restored object");
            require(operation.server.unchanged(),LT_ERROR,"world changed during restore precreate");
            require(creates[i].m_ObjectType == document.objects[i].type,LT_CANTRESTOREOBJECT,"restore precreate changed object type");
            const auto result = sm_CreateObjectInWorld(&manager,game,definitions[i],&creates[i],0xffff,PRECREATE_SAVEGAME,&object);
            require(operation.server.unchanged(),LT_ERROR,"world changed during restored object creation");
            require(result == LT_OK && object, result != LT_OK ? result : LT_CANTRESTOREOBJECT,"restored object creation failed");
        } catch (...) {
            if (operation.server.unchanged() && !object) manager.m_ClassMgr.destroy(game);
            throw;
        }
        operation.actors.push_back({object,ObjectCallbackGuard(*object)}); pointers.push_back(object);
        auto& actor = operation.actors.back();
        if (!actor.guard.unchanged()) continue;
        const auto& saved = document.objects[i]; state(manager,*object,saved);
        WMMoveState movement{};
        movement.m_pContainer = &manager.m_Unknown210; movement.m_pUnknown04 = manager.m_pUnknown6a8;
        movement.m_pObj = object; movement.m_nUnknown0C = object->m_BlockingPriority;
        wm_SetObjectDims(&movement,&object->m_Dims,0,0);
        require(operation.server.unchanged(),LT_ERROR,"world changed while restoring dimensions");
        if (!actor.guard.unchanged()) continue;
        sm_SetObjectState(&manager,object,saved.change_flags & 0x38);
        require(operation.server.unchanged(),LT_ERROR,"world changed while restoring activation");
        if (!actor.guard.same_data()) continue;
        object->m_ChangeFlags = (saved.change_flags & ~uint32(0x80)) | (object->m_ChangeFlags & 1u) | 4u;
        if (!actor.guard.unchanged()) continue;
        wm_MoveObject(&movement,vector(saved.position),0x0a);
        require(operation.server.unchanged(),LT_ERROR,"world changed while restoring position");
        if (!actor.guard.unchanged()) continue;
        if (saved.client) require(manager.m_ClientReferences.Add(*object,saved.client->flags,saved.client->name),LT_CANTRESTOREOBJECT,"restored client reference");
    }
    auto resources = manager.m_ClassMgr.m_Resources;
    SaveObjectReferences references(pointers,*manager.m_World.PositionReference(),[resources](int code,va_list* arguments) { return format_resource(resources,uint32(code),arguments); });
    for (std::size_t i = 0; i < operation.actors.size(); ++i) {
        require(operation.server.unchanged(),LT_ERROR,"world changed during game restore");
        const auto& actor = operation.actors[i];
        if (!actor.guard.unchanged()) continue;
        MemoryStream payload(document.objects[i].game_data);
        SaveMessage message(payload,SaveMessageDirection::Read,&references);
        manager.m_ClassMgr.message(actor.object->m_pSData->m_pBaseClass,MID_LOADOBJECT,static_cast<ILTMessage*>(&message),static_cast<float>(parameter));
        require(operation.server.unchanged(),LT_ERROR,"world changed during load callback");
        require(message.Verify() == LT_OK && payload.GetPos() == payload.GetLen(),LT_INVALIDFILE,"game load callback consumed a different field layout");
    }
    for (std::size_t i = 0; i < operation.actors.size(); ++i) {
        const auto& parent = operation.actors[i]; if (!parent.guard.unchanged()) continue;
        for (const auto& saved : document.objects[i].attachments) {
            const auto& child = operation.actors[saved.child]; if (!child.guard.unchanged()) continue;
            auto offset = vector(saved.offset); auto orientation = rotation(saved.rotation);
            require(am_CreateAttachment(&manager.m_Unknown390,parent.object,child.object,saved.node,&offset,&orientation,nullptr) == LT_OK,
                LT_CANTRESTOREOBJECT,"restored attachment rejected");
            sm_SetObjectChangeFlag(&manager,parent.object,CF_ATTACHMENTS);
        }
        for (const auto& saved : document.objects[i].relations) {
            const auto& other = operation.actors[saved.other]; if (!other.guard.unchanged()) continue;
            require(manager.m_InterObjectLinks.Create(parent.object,other.object,saved.type),LT_CANTRESTOREOBJECT,"restored inter-object link rejected");
        }
    }
    require(sm_CheckObjectIntegrityQ474(&manager) == LT_OK,LT_CANTRESTOREOBJECT,"restored object table integrity");
    sm_MarkSkyChanged(&manager); operation.committed = true; return LT_OK;
}
}
LTRESULT sav_RestoreObjects(CServerMgr* manager, ILTStream* stream, uint32 parameter, uint32 flags) {
    if (!manager || !stream) return LT_INVALIDPARAMS;
    try { const auto document = nolf::save::read(*stream); return nolf::save::apply(*manager,document,parameter,flags); }
    catch (const nolf::save::Failure& failure) { nolf::platform::log("restore",failure.what()); return failure.code; }
    catch (const std::bad_alloc&) { return LT_OUTOFMEMORY; }
    catch (const std::exception& error) { nolf::platform::log("restore",error.what()); return LT_INVALIDFILE; }
}
LTRESULT sav_RestoreFile(CServerMgr* manager, const char* filename, uint32 parameter, uint32 flags) {
    if (!manager || !filename || !*filename) return LT_INVALIDPARAMS;
    try {
        // The game names saves as it does for the menus' existence checks.
        auto* stream = nolf::OpenFileStream(nolf::platform::UserFiles{}.path(filename),nolf::platform::FileAccess::Read);
        if (!stream) return LT_ERROR;
        const auto document = [&] { std::unique_ptr<nolf::FileStream> owner(stream); return nolf::save::read(*owner); }();
        return nolf::save::apply(*manager,document,parameter,flags);
    } catch (const nolf::save::Failure& failure) { nolf::platform::log("restore",std::string(filename)+": "+failure.what()); return failure.code; }
      catch (const std::bad_alloc&) { return LT_OUTOFMEMORY; }
      catch (const std::exception& error) { nolf::platform::log("restore",error.what()); return LT_INVALIDFILE; }
}
