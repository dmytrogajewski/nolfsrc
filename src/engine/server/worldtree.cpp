#include "worldbsp.h"
#include "world_visibility.h"
#include "object_lifetime.h"
#include <unordered_set>

LTRESULT CWorldBsp::DoVisQuery(const CWorldVisQuery& input) {
    if (m_Terminating || !m_GeometryLifetime->available) return LT_NOTINITIALIZED;
    try {
        const auto query = input;
        const auto lifetime = m_GeometryLifetime;
        const auto generation = lifetime->generation;
        const auto valid = [&] { return lifetime->Matches(generation); };
        const auto graph = m_Visibility ? m_Visibility : WorldVisibility::Build(*this);
        std::vector<uint16> portals(m_nChunks);
        for (uint32 i = 0; i < m_nChunks; ++i) portals[i] = m_pChunks[i].m_Flags;
        struct Entry { LTObject* object; ObjectCallbackGuard guard; };
        std::vector<Entry> candidates;
        std::unordered_set<LTObject*> seen;
        auto add = [&](LTObject* object) {
            if (object && seen.insert(object).second) candidates.push_back({object,ObjectCallbackGuard(*object)});
        };
        if (!query.candidates.empty()) for (auto* object : query.candidates) add(object);
        else for (uint32 i = 0; i < m_n74; ++i) {
            auto& head = m_n70[i].m_head;
            for (auto* link = head.m_pNext; link != &head; link = link->m_pNext) add(static_cast<LTObject*>(link->m_pData));
        }
        const auto selected = graph->Select(query.position,portals,[&](uint16 portal) {
            return valid() && (!query.portal || query.portal(portal));
        });
        if (!valid()) return LT_FINISHED;
        if (query.region) for (uint32 i = 0; i < selected.groups.size(); ++i) if (selected.groups[i]) {
            query.region(m_p88[i]);
            if (!valid()) return LT_FINISHED;
        }
        if (query.object) for (auto& entry : candidates) {
            if (entry.guard.unchanged() && graph->Intersects(selected,entry.object->m_Pos,entry.object->m_Radius)) query.object(*entry.object);
            if (!valid()) return LT_FINISHED;
        }
        return LT_OK;
    } catch (const std::bad_alloc&) { return LT_OUTOFMEMORY; }
      catch (const std::runtime_error&) { return LT_INVALIDDATA; }
}
