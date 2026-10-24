#include "save_runtime.h"
#include "save_message.h"
#include "save_objects.h"
#include "servermgr.h"
#include "commonmodel.h"
#include "engine/portable/memory_stream.h"
#include "platform/user_files.h"
#include "platform/filesystem.h"
#include "platform/system.h"
#include <bit>
#include <unordered_set>

namespace nolf::save {
namespace {
SavedVector vector(LTVector value) { return {value.x,value.y,value.z}; }
SavedRotation rotation(const LTRotation& value) { return {value.m_Quat[0],value.m_Quat[1],value.m_Quat[2],value.m_Quat[3]}; }
bool always_loaded(const ClassDef* definition) {
    std::unordered_set<const ClassDef*> seen;
    for (; definition; definition = definition->m_ParentClass) {
        require(seen.insert(definition).second,LT_INVALIDOBJECTDLL,"cyclic saved class hierarchy");
        if (definition->m_ClassFlags & CF_ALWAYSLOAD) return true;
    }
    return false;
}
SavedObject snapshot(CServerMgr& manager, LTObject& object) {
    auto* data = object.m_pSData;
    require(data && data->m_pClass && data->m_pBaseClass,LT_INVALIDPARAMS,"saved object has no game class");
    SavedObject result;
    result.class_name = data->m_pClass->m_ClassName;
    result.type = object.m_ObjectType; result.flags = object.m_Flags; result.flags2 = object.m_Flags2;
    result.position = vector(object.m_Pos); result.scale = vector(object.m_Scale); result.rotation = rotation(object.m_Rotation);
    result.user_flags = object.m_UserFlags;
    result.name = data->m_hIdentifier ? dhash_GetKey(data->m_hIdentifier,0) : "";
    if (object.m_ObjectType == OT_WORLDMODEL || object.m_ObjectType == OT_CONTAINER) {
        require(object.m_pWorldBsp,LT_INVALIDDATA,"saved world model has no geometry");
        result.filename = object.m_pWorldBsp->m_Name;
    } else result.filename = fs_GetFileName(&manager.m_FileMgr,data->m_pModelFile);
    result.skins[0] = fs_GetFileName(&manager.m_FileMgr,data->m_SkinFiles[0]);
    if (object.m_ObjectType == OT_MODEL) {
        auto* model = dynamic_cast<ModelInstance*>(&object);
        require(model,LT_INVALIDDATA,"saved model type mismatch");
        result.skins[1] = fs_GetFileName(&manager.m_FileMgr,data->m_SkinFiles[1]);
        const auto& tracker = model->m_AnimTracker;
        result.animation = SavedAnimation{tracker.m_TimeRef.m_Cur.m_iAnim,tracker.m_TimeRef.m_Cur.m_Time,tracker.m_Flags};
    }
    if (object.m_ObjectType == OT_LIGHT) result.light_radius = object.m_LightRadius;
    if (object.m_ObjectType == OT_CONTAINER) {
        const auto* container = dynamic_cast<CObjContainer*>(&object);
        require(container,LT_INVALIDDATA,"saved container type mismatch"); result.container_code = container->m_ContainerCode;
    }
    result.next_update = data->m_NextUpdate; result.blocking_priority = object.m_BlockingPriority;
    if (const auto* message = data->m_SFXMessage.m_pMsg) {
        require(message->m_DataLen <= message->m_DataSize,LT_INVALIDDATA,"saved SFX extent");
        const auto* first = reinterpret_cast<const std::byte*>(message->m_pData);
        result.sfx_message.emplace(first,first + message->m_DataLen);
    }
    result.color = {object.m_ColorR,object.m_ColorG,object.m_ColorB,object.m_ColorA};
    result.velocity = vector(object.m_Velocity); result.acceleration = vector(object.m_Acceleration);
    result.friction = object.m_FrictionCoefficient; result.mass = object.m_Mass; result.force_ignore_limit_squared = object.m_ForceIgnoreLimitSqr;
    result.dimensions = vector(object.m_Dims); result.change_flags = object.m_ChangeFlags;
    if (result.change_flags & 0x100) {
        result.sky_slot = 0;
        for (uint16 i = 0; i < 30; ++i) if (manager.m_SkyObjects[i] == object.m_ObjectID) result.sky_slot = i;
    }
    if (const auto* client = data->m_pOwningClient) result.client = SavedClient{client->m_ClientFlags,client->m_Name};
    else if (auto handle = manager.m_ClientReferences.Next(nullptr); handle) {
        do {
            const auto* entry = manager.m_ClientReferences.Find(handle);
            if (entry && entry->object == &object) { result.client = SavedClient{entry->flags,entry->name}; break; }
            handle = manager.m_ClientReferences.Next(handle);
        } while (handle);
    }
    return result;
}
}
SavedGame capture(CServerMgr& manager, const ObjectList& list, uint32 parameter, uint32 flags) {
    require(manager.m_Lifetime->alive && !manager.m_Terminating && !manager.m_EndingWorld,LT_NOTINITIALIZED,"server is shutting down");
    ServerCallbackGuard server(manager.m_Lifetime);
    struct Actor { LTObject* object; BaseClass* game; ObjectCallbackGuard guard; };
    std::vector<LTObject*> pointers; std::vector<Actor> actors;
    std::unordered_map<LTObject*,uint16> indices;
    std::unordered_set<const ObjectLink*> links;
    for (auto* link = list.m_pFirstLink; link; link = link->m_pNext) {
        require(links.insert(link).second && links.size() <= 65535,LT_INVALIDPARAMS,"cyclic or oversized save list");
        auto* object = link->m_hObject;
        require(object && manager.m_Unknown390.m_Allocated.contains(object),LT_INVALIDPARAMS,"foreign saved object");
        require(ObjectCallbackGuard(*object).same_data() && object->m_pSData && object->m_pSData->m_pClass,LT_INVALIDPARAMS,"unavailable saved object");
        if (always_loaded(object->m_pSData->m_pClass) || indices.contains(object)) continue;
        indices.emplace(object,static_cast<uint16>(pointers.size())); pointers.push_back(object);
        actors.push_back({object,object->m_pSData->m_pBaseClass,ObjectCallbackGuard(*object)});
    }
    SavedGame result;
    result.timing_words = {manager.m_Unknown6c4,std::bit_cast<uint32>(manager.m_Unknown6bc),std::bit_cast<uint32>(manager.m_Unknown6c0),
        static_cast<uint32>(manager.m_Unknown6d0),std::bit_cast<uint32>(manager.m_Unknown6c8),std::bit_cast<uint32>(manager.m_Time),std::bit_cast<uint32>(manager.m_Unknown6b8)};
    if (flags & 2) {
        result.portal_flags.emplace(manager.m_World.m_Worlds.GetSize());
        for (uint32 i = 0; i < manager.m_World.m_Worlds.GetSize(); ++i) {
            const auto* geometry = manager.m_World.m_Worlds[i]->m_pWorldBsp;
            auto& portal_flags = (*result.portal_flags)[i]; portal_flags.reserve(geometry->m_nChunks);
            for (uint32 p = 0; p < geometry->m_nChunks; ++p) portal_flags.push_back(geometry->m_pChunks[p].m_Flags);
        }
    }
    if (flags & 1) for (void* iterator = dhash_GetFirst(manager.m_ConsoleState.m_hVarHash); iterator;) {
        const auto* variable = static_cast<const LTCommandVar*>(dhash_GetData(dhash_GetNext(&iterator)));
        result.console_variables.emplace_back(variable->m_pName,variable->m_pStringVal);
    }
    result.objects.reserve(actors.size());
    auto resources = manager.m_ClassMgr.m_Resources;
    SaveObjectReferences references(pointers,*manager.m_World.PositionReference(),[resources](int code,va_list* arguments) {
        return nolf::format_resource(resources,uint32(code),arguments);
    });
    uint32 payload_bytes = 0;
    for (std::size_t i = 0; i < actors.size(); ++i) {
        const auto& actor = actors[i];
        // A save callback may remove other listed objects (KeyFramer::Save
        // removes its keys): removal waits for the end of the frame, so they
        // are saved as the original saved them, and references to them as none.
        require(server.unchanged() && actor.guard.same_data(),LT_ERROR,"world changed during save");
        // Match the original callback order: later actors can observe earlier
        // callbacks, while this actor's engine fields precede its own payload.
        result.objects.push_back(snapshot(manager,*actor.object));
        MemoryStream payload({},true,byte_budget-payload_bytes);
        SaveMessage message(payload,SaveMessageDirection::Write,&references);
        manager.m_ClassMgr.message(actor.game,MID_SAVEOBJECT,static_cast<ILTMessage*>(&message),static_cast<float>(parameter));
        require(server.unchanged() && actor.guard.same_data(),LT_ERROR,"saved actor destroyed during callback");
        require(message.Verify() == LT_OK,LT_INVALIDFILE,"game save callback failed");
        const auto bytes = payload.data(); payload_bytes += static_cast<uint32>(bytes.size());
        result.objects[i].game_data.assign(bytes.begin(),bytes.end());
    }
    // Attachments and links describe the graph after all save callbacks.
    for (std::size_t i = 0; i < actors.size(); ++i) {
        const auto& actor = actors[i];
        require(server.unchanged() && actor.guard.same_data(),LT_ERROR,"saved graph changed during callback");
        auto& saved = result.objects[i];
        for (auto* attachment = actor.object->m_pAttachments; attachment; attachment = attachment->m_pNext) {
            const auto owned = manager.m_Unknown390.m_Attachments.find(attachment);
            require(owned != manager.m_Unknown390.m_Attachments.end() && owned->second == actor.object,LT_INVALIDDATA,"foreign saved attachment");
            const auto child = indices.find(attachment->m_pChild);
            if (child != indices.end()) saved.attachments.push_back({vector(attachment->m_Offset.m_Pos),rotation(attachment->m_Offset.m_Rot),child->second,attachment->m_iNode});
        }
        auto* head = &actor.object->m_pSData->m_Links;
        for (auto* link = head->m_pNext; link != head; link = link->m_pNext) {
            const auto* relation = static_cast<const InterObjectLink*>(link->m_pData);
            if (relation->m_pOwner != actor.object || relation->sound || relation->m_Type > 1) continue;
            const auto other = indices.find(relation->m_pLinked);
            if (other != indices.end()) saved.relations.push_back({static_cast<uint8>(relation->m_Type),other->second});
        }
    }
    return result;
}
SavedGame read(ILTStream& stream) {
    uint32 position = 0, length = 0;
    require(stream.GetPos(&position) == LT_OK && stream.GetLen(&length) == LT_OK && position <= length && length-position <= byte_budget,
        LT_INVALIDFILE,"save stream extent");
    std::vector<std::byte> bytes(length-position);
    require(stream.Read(bytes.data(),static_cast<uint32>(bytes.size())) == LT_OK,LT_INVALIDFILE,"save stream read");
    require(bytes.size() >= 4,LT_INVALIDFILE,"truncated save version");
    require(wire::load<uint32>(bytes.data()) == saved_game_version,LT_INVALIDVERSION,"unsupported save version");
    return DecodeSavedGame(bytes,position);
}
}
LTRESULT sav_SaveObjects(CServerMgr* manager, ILTStream* stream, ObjectList* list, uint32 parameter, uint32 flags) {
    if (!manager || !stream || !list) return LT_INVALIDPARAMS;
    try {
        uint32 origin = 0;
        nolf::save::require(stream->GetPos(&origin) == LT_OK,LT_ERROR,"save output position");
        const auto document = nolf::save::capture(*manager,*list,parameter,flags);
        const auto bytes = nolf::EncodeSavedGame(document,origin);
        return stream->Write(bytes.data(),static_cast<uint32>(bytes.size()));
    } catch (const nolf::save::Failure& failure) { nolf::platform::log("save",failure.what()); return failure.code; }
      catch (const std::bad_alloc&) { return LT_OUTOFMEMORY; }
      catch (const std::exception& error) { nolf::platform::log("save",error.what()); return LT_INVALIDFILE; }
}
LTRESULT sav_SaveFile(CServerMgr* manager, const char* filename, ObjectList* list, uint32 parameter, uint32 flags) {
    if (!manager || !filename || !*filename || !list) return LT_INVALIDPARAMS;
    try {
        // A game name ("Save\\Slot01.sav"), possibly from a client: never a host path.
        const auto destination = nolf::platform::UserFiles{}.path(filename);
        const auto document = nolf::save::capture(*manager,*list,parameter,flags);
        const auto bytes = nolf::EncodeSavedGame(document);
        nolf::platform::atomic_write(destination,bytes); return LT_OK;
    } catch (const nolf::save::Failure& failure) { nolf::platform::log("save",failure.what()); return failure.code; }
      catch (const std::bad_alloc&) { return LT_OUTOFMEMORY; }
      catch (const std::exception& error) { nolf::platform::log("save",error.what()); return LT_ERROR; }
}
