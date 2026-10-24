#pragma once
#include "ltobject_internal.h"

class ObjectCallbackGuard {
public:
    explicit ObjectCallbackGuard(LTObject& object)
        : object_(&object), lifetime_(object.m_Lifetime), data_(object.m_pSData),
          module_(data_ ? data_->m_GameModuleLease : nullptr),
          generation_(data_ ? data_->m_Generation : 0) {}
    bool alive() const { return *lifetime_; }
    bool same_data() const {
        return alive() && object_->m_pSData == data_ && (!data_ || data_->m_Generation == generation_);
    }
    bool unchanged() const {
        return same_data() && !(object_->m_ChangeFlags & 1) && (!data_ || !data_->m_Removing);
    }
private:
    LTObject* object_;
    std::shared_ptr<const bool> lifetime_;
    SObjData* data_;
    std::shared_ptr<const void> module_;
    uint64 generation_;
};

// Snapshot object identities before invoking code that can change the list.
// Objects created during callbacks belong to the next visit.
bool object_Visit(LTList&, bool (*visitor)(void*, LTObject&), void* context);
