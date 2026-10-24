#include "servermgr.h"
#include "de_error.h"
#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

LTBOOL CServerMgr::GrowClientObjInfos(uint32 count) {
    if (count <= m_Unknown8b8) return LTTRUE;
    if (count > 4095) return LTFALSE;
    const auto capacity = count + 40;
    if (!client_GrowObjectInfos(m_ClientList,capacity)) return LTFALSE;
    m_Unknown8b8 = capacity;
    return LTTRUE;
}

LTRESULT sub_100332e8(CServerMgr* server, ObjectCreateStruct* create,
                    ClassDef* definition, LTObject* object, BaseClass* game, SObjData** output) {
    *output = nullptr;
    auto* data = server->m_MessageBank.Allocate();
    if (!data) return LT_OUTOFMEMORY;
    data->m_pClass = definition;
    data->m_pBaseClass = game;
    data->m_GameModuleLease = server->m_ClassMgr.lease();
    data->m_NextUpdate = create->m_NextUpdate;
    data->m_DeactivationTime = data->m_UpdateTimer = create->m_fDeactivationTime;
    data->m_CreateFlags = create->m_CreateFlags & OCS_AUTOLOAD;
    if (*create->m_Name) {
        data->m_hIdentifier = dhash_AddElement(static_cast<CHashTable*>(server->m_hHash8c0), create->m_Name,
                                             static_cast<uint32>(std::strlen(create->m_Name) + 1));
        if (!data->m_hIdentifier) { server->m_MessageBank.Free(data); return LT_OUTOFMEMORY; }
        dhash_SetData(data->m_hIdentifier, object);
    }
    data->m_Links.TieOff();
    dl_AddHead(&server->m_List8dc, &data->m_StateLink, object);
    *output = data;
    return LT_OK;
}

void CServerMgr::NotifyCommandChanges(Client* client, uint8* commands, int count) {
    if (!client || !commands || count <= 0 || !m_ClassMgr.m_pServerShell) return;
    struct Context { CServerMgr* manager; ServerCallbackGuard guard; } context{this,ServerCallbackGuard(m_Lifetime)};
    client_DispatchCommands(*client,std::span<const uint8>(commands,static_cast<std::size_t>(count)),
        [](void* pointer, HCLIENT handle, uint8 command, bool active) {
            auto& context = *static_cast<Context*>(pointer);
            if (!context.guard.unchanged() || !context.manager->m_ClassMgr.m_pServerShell) return;
            context.manager->m_ClassMgr.shell([&](IServerShell& shell) {
                if (active) shell.OnCommandOn(handle,command);
                else shell.OnCommandOff(handle,command);
            });
        },&context);
}

int sm_CreateNewID(CServerMgr* server, void** output) {
    *output = nullptr;
    if (server->m_Unknown8bc + 1 >= 0xfff) return LT_ERROR;
    auto* storage = g_LinkNodeBank.Allocate();
    if (!storage) return LT_OUTOFMEMORY;
    auto* record = new (storage) CObjectIDRec;
    CObjectMapEntry entry{};
    if (!server->m_Unknown8fc.Append(entry)) { sb_Free(&g_LinkNodeBank.m_Bank, record); return LT_OUTOFMEMORY; }
    record->m_nIndex = server->m_Unknown8bc++;
    dl_Insert(&server->m_Link8c4, record);
    *output = record;
    return LT_OK;
}
void* sm_FindObjectIDRec(CServerMgr* server, uint16 id) {
    auto* head = &server->m_Link8c4;
    for (auto* node = head->m_pNext; node != head; node = node->m_pNext) {
        auto* record = static_cast<CObjectIDRec*>(node);
        if ((record->m_nIndex & ~0x40000000u) == id) return record;
    }
    return nullptr;
}

LTRESULT sm_CheckObjectIntegrityQ474(CServerMgr* server) {
    auto* head = &server->m_List8dc.m_Head;
    for (auto* link = head->m_pNext; link != head; link = link->m_pNext) {
        auto* object = static_cast<LTObject*>(link->m_pData);
        if (object->m_ObjectID >= server->m_Unknown8fc.GetSize()) return LT_ERROR;
        const auto& entry = server->m_Unknown8fc[object->m_ObjectID];
        if (entry.m_nFlags != 1 || entry.m_pRecord != object) return LT_ERROR;
    }
    return LT_OK;
}
