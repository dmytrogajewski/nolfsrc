#include "object_lifetime.h"
#include <vector>

bool object_Visit(LTList& list, bool (*visitor)(void*, LTObject&), void* context) {
    if (!visitor) return false;
    struct Entry { LTObject* object; ObjectCallbackGuard guard; };
    std::vector<Entry> pending;
    try {
        for (auto* link = list.m_Head.m_pNext; link != &list.m_Head; link = link->m_pNext) {
            auto* object = static_cast<LTObject*>(link->m_pData);
            pending.push_back({object,ObjectCallbackGuard(*object)});
        }
    } catch (const std::bad_alloc&) { return false; }
    for (auto& entry : pending) if (entry.guard.unchanged() && !visitor(context,*entry.object)) break;
    return true;
}
