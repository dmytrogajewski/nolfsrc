#include "client_reference.h"
#include <algorithm>
#include <cstring>
#include <memory>

HCLIENTREF ClientReferences::Add(LTObject& object, uint32 flags, std::string name) {
    if (!ObjectCallbackGuard(object).unchanged()) return nullptr;
    auto entry = std::make_unique<Entry>(object,flags,std::move(name));
    RemoveFor(&object);
    auto* result = entry.get();
    dl_AddHead(&list_,&result->link,result);
    object.m_ChangeFlags |= 0x80;
    entry.release(); return reinterpret_cast<HCLIENTREF>(result);
}
void ClientReferences::RemoveFor(LTObject* object) {
    auto* head = &list_.m_Head;
    for (auto* link = head->m_pNext; link != head;) {
        auto* entry = static_cast<Entry*>(link->m_pData); link = link->m_pNext;
        if (entry->object != object) continue;
        if (entry->guard.same_data()) object->m_ChangeFlags &= ~uint32(0x80);
        dl_RemoveAt(&list_,&entry->link); delete entry;
    }
}
void ClientReferences::Clear() {
    auto* head = &list_.m_Head;
    while (head->m_pNext != head) {
        auto* entry = static_cast<Entry*>(head->m_pNext->m_pData);
        if (entry->guard.same_data()) entry->object->m_ChangeFlags &= ~uint32(0x80);
        dl_RemoveAt(&list_,&entry->link); delete entry;
    }
}
const ClientReferences::Entry* ClientReferences::Find(HCLIENTREF handle) const {
    auto* head = &list_.m_Head;
    for (auto* link = head->m_pNext; link != head; link = link->m_pNext) {
        auto* entry = static_cast<Entry*>(link->m_pData);
        if (reinterpret_cast<HCLIENTREF>(entry) == handle) return entry->guard.unchanged() ? entry : nullptr;
    }
    return nullptr;
}
HCLIENTREF ClientReferences::Next(HCLIENTREF previous) const {
    auto* head = &list_.m_Head;
    const LTLink* start = head;
    if (previous) {
        const auto* found = Find(previous);
        if (!found) return nullptr;
        start = &found->link;
    }
    for (auto* link = start->m_pNext; link != head; link = link->m_pNext) {
        auto* entry = static_cast<Entry*>(link->m_pData);
        if (entry->guard.unchanged()) return reinterpret_cast<HCLIENTREF>(entry);
    }
    return nullptr;
}
bool ClientReferences::CopyName(HCLIENTREF handle, char* output, int capacity) const {
    if (!output || capacity <= 0) return false;
    output[0] = 0;
    const auto* entry = Find(handle);
    if (!entry) return false;
    const auto count = std::min(entry->name.size(),static_cast<std::size_t>(capacity-1));
    std::memcpy(output,entry->name.data(),count); output[count] = 0; return true;
}
