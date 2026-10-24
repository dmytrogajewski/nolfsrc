#include "commonmodel.h"
#include <utility>
void at_SetTime(LTAnimTracker*,uint32,LTBOOL);

void ModelInstance::BindModel(std::shared_ptr<Model> model, bool pending, bool preserve) {
    auto previous = std::move(m_ModelOwner);
    m_ModelOwner = std::move(model);
    for (auto* tracker = m_pTrackerList; tracker; tracker = tracker->GetNext()) {
        if (!preserve || !m_ModelOwner || !tracker->GetModel()) at_Init(tracker,m_ModelOwner.get(),0);
        else {
            const auto time = tracker->m_TimeRef.m_Cur.m_Time;
            const auto index = tracker->m_TimeRef.m_Cur.m_iAnim;
            const auto key = tracker->m_CurKey;
            const bool was_pending = (tracker->m_Flags & 8) != 0;
            tracker->SetModel(m_ModelOwner.get());
            if (!pending && index >= m_ModelOwner->m_AnimArray.GetSize()) {
                tracker->m_TimeRef.m_Cur.m_iAnim = 0;
                tracker->m_TimeRef.m_Prev.m_iAnim = 0;
            }
            at_SetTime(tracker,time,LTTRUE);
            if (pending || was_pending) tracker->m_CurKey = key;
        }
        if (pending) tracker->m_Flags |= 8;
        else tracker->m_Flags &= ~8;
    }
}
