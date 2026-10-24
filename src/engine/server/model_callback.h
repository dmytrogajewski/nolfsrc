#pragma once
#include "commonmodel.h"

// Game callbacks may reset or remove a tracker, rebind its model, or destroy
// its object. Check the owner's independent lifetime before reading either
// object or tracker after calling game code.
class ModelCallbackGuard {
public:
    explicit ModelCallbackGuard(LTAnimTracker& tracker)
        : tracker_(&tracker), instance_(tracker.GetModelInstance()),
          lifetime_(instance_ ? instance_->m_Lifetime : nullptr),
          module_(instance_ && instance_->m_pSData ? instance_->m_pSData->m_GameModuleLease : nullptr),
          generation_(tracker.m_MutationGeneration), model_(tracker.GetModel()),
          animation_(tracker.m_TimeRef.m_Cur.m_iAnim) {}
    bool unchanged() const {
        if (instance_) {
            if (!*lifetime_ || (instance_->m_ChangeFlags & 1)) return false;
            LTAnimTracker** link = nullptr;
            if (!instance_->FindTracker(tracker_, &link)) return false;
        }
        return tracker_->m_MutationGeneration == generation_ && tracker_->GetModel() == model_ &&
               tracker_->m_TimeRef.m_Cur.m_iAnim == animation_;
    }
private:
    LTAnimTracker* tracker_;
    ModelInstance* instance_;
    std::shared_ptr<bool> lifetime_;
    std::shared_ptr<const void> module_;
    uint64 generation_;
    Model* model_;
    uint16 animation_;
};

void sm_ModelStringKeyCallback(LTAnimTracker*, AnimKeyFrame*);
void model_UpdateTrackers(ModelInstance&, uint32 elapsed, StringKeyCallback);
