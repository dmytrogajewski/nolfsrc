#include "servermgr.h"
#include "servercommon.h"
#include "servermodel.h"
#include <climits>
#include <limits>

namespace {
bool live(CServerMgr* manager, LTObject* object) {
    return manager && object && manager->m_Unknown390.m_Allocated.contains(object) && *object->m_Lifetime &&
        object->m_pSData && !object->m_pSData->m_Removing;
}
}
LTRESULT si_CreateAttachment(HOBJECT parent, HOBJECT child, char* socket_name, LTVector* offset,
                             LTRotation* rotation, HATTACHMENT* output) {
    if (output) *output = nullptr;
    if (!g_pServerMgr) return LT_NOTINITIALIZED;
    if (!live(g_pServerMgr,parent) || !live(g_pServerMgr,child)) return LT_INVALIDPARAMS;
    int node = -1;
    if (parent->m_ObjectType == OT_MODEL && socket_name) {
        auto* model = static_cast<ModelInstance*>(parent)->m_AnimTracker.GetModel();
        if (!model) return LT_INVALIDDATA;
        uint32 found = 0;
        if (model->FindSocket(socket_name,&found)) {
            if (found > uint32(INT_MAX)) return LT_INVALIDDATA;
            node = static_cast<int>(found);
        } else {
            if (!model->FindNode(socket_name,&found)) return LT_NODENOTFOUND;
            const auto biased = uint64(found) + model->m_Sockets.GetSize();
            if (biased > INT_MAX) return LT_INVALIDDATA;
            node = static_cast<int>(biased);
        }
    }
    Attachment* attachment = nullptr;
    auto result = am_CreateAttachment(&g_pServerMgr->m_Unknown390,parent,child,node,offset,rotation,&attachment);
    if (result != LT_OK) return result;
    sm_SetObjectChangeFlag(g_pServerMgr,parent,0x100);
    if (output) *output = reinterpret_cast<HATTACHMENT>(attachment);
    return LT_OK;
}
LTRESULT si_RemoveAttachment(HATTACHMENT handle) {
    if (!g_pServerMgr) return LT_NOTINITIALIZED;
    auto* attachment = am_FindAttachment(&g_pServerMgr->m_Unknown390,handle);
    if (!attachment) return LT_INVALIDPARAMS;
    auto* parent = attachment->m_pParent;
    const auto result = am_RemoveAttachment(&g_pServerMgr->m_Unknown390,parent,attachment);
    if (result == LT_OK && live(g_pServerMgr,parent)) sm_SetObjectChangeFlag(g_pServerMgr,parent,0x100);
    return result;
}
LTRESULT si_FindAttachment(HOBJECT parent, HOBJECT child, HATTACHMENT* output) {
    if (output) *output = nullptr;
    if (!g_pServerMgr) return LT_NOTINITIALIZED;
    if (!output || !live(g_pServerMgr,parent) || !live(g_pServerMgr,child)) return LT_INVALIDPARAMS;
    for (auto* attachment = parent->m_pAttachments; attachment; attachment = attachment->m_pNext) {
        if (attachment->m_pChild == child) { *output = reinterpret_cast<HATTACHMENT>(attachment); return LT_OK; }
    }
    return LT_NOTFOUND;
}
LTRESULT ServerCommonLT::GetAttachmentObjects(HATTACHMENT handle, HOBJECT& parent, HOBJECT& child) {
    parent = child = nullptr;
    if (!m_pServerMgr) return LT_NOTINITIALIZED;
    auto* attachment = am_FindAttachment(&m_pServerMgr->m_Unknown390,handle);
    if (!attachment) return LT_INVALIDPARAMS;
    parent = attachment->m_pParent; child = attachment->m_pChild;
    return LT_OK;
}
void sm_RemoveObjectAttachments(CServerMgr* manager, LTObject* object) {
    if (!manager) return;
    auto& attachments = manager->m_Unknown390.m_Attachments;
    for (auto it = attachments.begin(); it != attachments.end();) {
        const auto current = it++;
        auto* record = current->first;
        auto* parent = current->second;
        if (parent != object && record->m_pChild != object) continue;
        if (parent != object && live(manager,parent)) sm_SetObjectChangeFlag(manager,parent,0x100);
        am_RemoveAttachment(&manager->m_Unknown390,parent,record);
    }
}
