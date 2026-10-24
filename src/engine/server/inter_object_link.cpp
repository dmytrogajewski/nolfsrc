#include "inter_object_link.h"
#include "sound_instance.h"
#include <limits>

namespace {
bool available(LTObject* object) { return object && *object->m_Lifetime && object->m_pSData && !object->m_pSData->m_Removing; }
}
bool InterObjectLinks::Create(LTObject* owner, LTObject* linked, uint32 type) {
    if (owner == linked || type > 1 || !available(owner) || !available(linked) || next_id_ == UINT64_MAX) return false;
    for (const auto& [id, record] : records_)
        if (record->m_Type == type && record->m_pOwner == owner && record->m_pLinked == linked) return true;
    auto record = std::make_unique<InterObjectLink>();
    record->m_Type = type; record->m_pOwner = owner; record->m_pLinked = linked;
    const auto it = records_.emplace(next_id_++, std::move(record)).first;
    dl_Insert(&owner->m_pSData->m_Links,&it->second->owner_link);
    dl_Insert(&linked->m_pSData->m_Links,&it->second->linked_link);
    return true;
}
bool InterObjectLinks::Attach(LTObject* owner, SoundInstance* sound) {
    if (!available(owner) || !sound || !(sound->m_PlaySoundFlags & PLAYSOUND_ATTACHED) || next_id_ == UINT64_MAX) return false;
    for (const auto& [id, record] : records_) if (record->sound == sound) return false;
    auto record = std::make_unique<InterObjectLink>();
    record->m_Type = 2; record->m_pOwner = owner; record->sound = sound;
    const auto it = records_.emplace(next_id_++, std::move(record)).first;
    dl_Insert(&owner->m_pSData->m_Links,&it->second->owner_link);
    sound->m_pObject = owner;
    return true;
}
void InterObjectLinks::Erase(Iterator it, bool kill_sound) {
    auto& record = *it->second;
    record.owner_link.Remove(); record.linked_link.Remove();
    if (record.sound) {
        if (record.sound->m_pObject == record.m_pOwner) record.sound->m_pObject = nullptr;
        if (kill_sound && !(record.sound->m_PlaySoundFlags & PLAYSOUND_GETHANDLE)) {
            record.sound->m_fTimer = 0; record.sound->m_bKill = LTTRUE;
        }
    }
    records_.erase(it);
}
void InterObjectLinks::Break(LTObject* owner, LTObject* linked, bool all) {
    for (auto it = records_.begin(); it != records_.end();) {
        auto current = it++;
        if (current->second->m_Type == 0 && current->second->m_pOwner == owner && current->second->m_pLinked == linked) {
            Erase(current,false); if (!all) return;
        }
    }
}
void InterObjectLinks::Detach(LTObject* owner, SoundInstance* sound, bool all) {
    for (auto it = records_.begin(); it != records_.end();) {
        auto current = it++;
        if (current->second->sound == sound && current->second->m_pOwner == owner) {
            Erase(current,false); if (!all) return;
        }
    }
}
void InterObjectLinks::BreakFor(LTObject* object, uint32 type, Notify notify, void* context) {
    const auto cutoff = next_id_ - 1;
    std::uint64_t cursor = 0;
    // Retire each record before game code runs. Look up by a monotonic key
    // again after callbacks: they may break other links, clear the service,
    // remove endpoints, or allocate new records at recycled addresses.
    while (true) {
        auto it = records_.upper_bound(cursor);
        if (it == records_.end() || it->first > cutoff) break;
        cursor = it->first;
        const auto& record = *it->second;
        if (record.m_Type != type || (record.m_pOwner != object && record.m_pLinked != object)) continue;
        auto* owner = record.m_pOwner;
        auto* linked = record.m_pLinked;
        Erase(it,true);
        if (notify && type != 2) notify(context,owner,linked);
    }
}
void InterObjectLinks::Clear() { while (!records_.empty()) Erase(records_.begin(),false); }
bool InterObjectLinks::VisitContainers(LTObject* object, bool (*visitor)(void*, LTObject*), void* context) {
    if (!visitor) return false;
    const auto cutoff = next_id_ - 1;
    std::uint64_t cursor = 0;
    while (true) {
        const auto it = records_.upper_bound(cursor);
        if (it == records_.end() || it->first > cutoff) return true;
        cursor = it->first;
        auto& record = *it->second;
        if (record.m_Type == INTERLINK_CONTAINER && record.m_pLinked == object && !visitor(context,record.m_pOwner)) return false;
    }
}
