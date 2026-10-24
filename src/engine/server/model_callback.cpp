#include "model_callback.h"
#include "command_parser.h"
#include "ltserverobj.h"
#include "ltengineobjects.h"
#include <string>
#include <vector>

void sm_ModelStringKeyCallback(LTAnimTracker* tracker, AnimKeyFrame* frame) {
    if (!tracker || !frame || !frame->m_pString) return;
    auto* object = tracker->GetModelInstance();
    if (!object || !(object->m_Flags & FLAG_MODELKEYS) || (object->m_ChangeFlags & 1) ||
        !object->m_pSData || !object->m_pSData->m_pBaseClass) return;
    auto* game = object->m_pSData->m_pBaseClass;
    ModelCallbackGuard guard(*tracker);
    // The model owning the key may be released by the first command.
    std::string script(frame->m_pString);
    CommandParser parser;
    parser.Init(script.data());
    while (parser.Next()) {
        if (!parser.m_nArgs) continue;
        ArgList args{parser.m_Args, parser.m_nArgs};
        game->EngineMessageFn(MID_MODELSTRINGKEY, &args, 0);
        if (!guard.unchanged()) return;
        if (!(object->m_Flags & FLAG_MODELKEYS) || !object->m_pSData ||
            object->m_pSData->m_pBaseClass != game) return;
    }
}

void model_UpdateTrackers(ModelInstance& object, uint32 elapsed, StringKeyCallback callback) {
    if (!elapsed || (object.m_ChangeFlags & 1)) return;
    const auto lifetime = object.m_Lifetime;
    struct Entry { LTAnimTracker* tracker; uint64 generation; };
    std::vector<Entry> pending;
    for (auto* tracker = object.m_pTrackerList; tracker; tracker = tracker->GetNext())
        pending.push_back({tracker, tracker->m_MutationGeneration});
    for (const auto& entry : pending) {
        if (!*lifetime || (object.m_ChangeFlags & 1)) return;
        LTAnimTracker** link = nullptr;
        if (!object.FindTracker(entry.tracker, &link) || entry.tracker->m_MutationGeneration != entry.generation) continue;
        entry.tracker->m_StringKeyCallback = callback;
        at_UpdateTracker(entry.tracker, elapsed);
    }
}
