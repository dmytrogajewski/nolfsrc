#include "object_manager.h"
#include "object_lifetime.h"
#include <cmath>
#include <unordered_set>
#include <vector>

namespace {
bool available(CUnknown390& manager, LTObject* object) {
    return object && manager.m_Allocated.contains(object) && *object->m_Lifetime &&
        (!object->m_pSData || !object->m_pSData->m_Removing);
}
bool reaches(LTObject* object, LTObject* target) {
    std::vector<LTObject*> pending{object};
    std::unordered_set<LTObject*> visited;
    while (!pending.empty()) {
        auto* next = pending.back(); pending.pop_back();
        if (next == target) return true;
        if (!visited.insert(next).second) continue;
        for (auto* attachment = next->m_pAttachments; attachment; attachment = attachment->m_pNext)
            if (attachment->m_pChild) pending.push_back(attachment->m_pChild);
    }
    return false;
}
}
Attachment* am_FindAttachment(CUnknown390* manager, HATTACHMENT handle) {
    if (!manager || !handle) return nullptr;
    auto* attachment = reinterpret_cast<Attachment*>(handle);
    const auto found = manager->m_Attachments.find(attachment);
    if (found == manager->m_Attachments.end() || !manager->m_Allocated.contains(found->second)) return nullptr;
    if (!manager->m_Allocated.contains(attachment->m_pChild)) return nullptr;
    return attachment;
}
LTRESULT am_RemoveAttachment(CUnknown390* manager, LTObject* parent, Attachment* attachment) {
    if (!manager || !parent || !attachment || !manager->m_Allocated.contains(parent)) return LT_INVALIDPARAMS;
    const auto found = manager->m_Attachments.find(attachment);
    if (found == manager->m_Attachments.end() || found->second != parent) return LT_NOTFOUND;
    auto** link = &parent->m_pAttachments;
    while (*link && *link != attachment) link = &(*link)->m_pNext;
    if (!*link) return LT_ERROR;
    *link = attachment->m_pNext;
    manager->m_Attachments.erase(found);
    attachment->~Attachment(); sb_Free(&manager->m_Bank4c,attachment);
    return LT_OK;
}
void am_DetachObject(CUnknown390* manager, LTObject* object) {
    if (!manager || !object) return;
    for (auto it = manager->m_Attachments.begin(); it != manager->m_Attachments.end();) {
        auto current = it++;
        auto* attachment = current->first;
        if (current->second == object || attachment->m_pChild == object)
            am_RemoveAttachment(manager,current->second,attachment);
    }
}
bool am_VisitChildren(CUnknown390* manager, LTObject* parent, bool (*visitor)(void*,LTObject&), void* context) {
    if (!manager || !parent || !visitor || !manager->m_Allocated.contains(parent)) return false;
    ObjectCallbackGuard parent_guard(*parent);
    struct Entry { Attachment* attachment; uint64 generation; LTObject* child; ObjectCallbackGuard guard; };
    std::vector<Entry> children;
    try {
        for (auto* record = parent->m_pAttachments; record; record = record->m_pNext) {
            if (!manager->m_Attachments.contains(record) || !record->m_pChild || !manager->m_Allocated.contains(record->m_pChild)) continue;
            children.push_back({record,record->m_Generation,record->m_pChild,ObjectCallbackGuard(*record->m_pChild)});
        }
    } catch (const std::bad_alloc&) { return false; }
    for (auto& entry : children) {
        if (!parent_guard.same_data()) break;
        const auto found = manager->m_Attachments.find(entry.attachment);
        if (found == manager->m_Attachments.end() || entry.attachment->m_Generation != entry.generation || !entry.guard.same_data()) continue;
        if (!visitor(context,*entry.child)) break;
    }
    return true;
}
LTRESULT am_CreateAttachment(CUnknown390* manager, LTObject* parent, LTObject* child, int node,
                             LTVector* offset, LTRotation* rotation, Attachment** output) {
    if (output) *output = nullptr;
    if (!manager || !available(*manager,parent) || !available(*manager,child) || node < -1 || parent == child) return LT_INVALIDPARAMS;
    if (offset && (!std::isfinite(offset->x) || !std::isfinite(offset->y) || !std::isfinite(offset->z))) return LT_INVALIDPARAMS;
    if (rotation) {
        double norm = 0;
        for (auto value : rotation->m_Quat) { if (!std::isfinite(value)) return LT_INVALIDPARAMS; norm += double(value)*value; }
        if (!norm) return LT_INVALIDPARAMS;
    }
    Attachment* record = nullptr;
    try {
        if (reaches(child,parent)) return LT_ERROR;
        auto* storage = sb_Allocate(&manager->m_Bank4c);
        if (!storage) return LT_OUTOFMEMORY;
        record = new (storage) Attachment{};
        record->m_pParent = parent; record->m_pChild = child;
        record->m_nParentID = parent->m_ObjectID; record->m_nChildID = child->m_ObjectID; record->m_iNode = node;
        record->m_Offset.m_Pos = offset ? *offset : LTVector(0,0,0);
        if (rotation) record->m_Offset.m_Rot = *rotation;
        else record->m_Offset.m_Rot.Init();
        manager->m_Attachments.emplace(record,parent);
    } catch (const std::bad_alloc&) {
        if (record) { record->~Attachment(); sb_Free(&manager->m_Bank4c,record); }
        return LT_OUTOFMEMORY;
    }
    parent->m_pAttachmentBank = &manager->m_Bank4c;
    record->m_pNext = parent->m_pAttachments; parent->m_pAttachments = record;
    if (output) *output = record;
    return LT_OK;
}
