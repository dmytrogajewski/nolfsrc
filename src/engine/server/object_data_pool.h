#pragma once
#include "ltobject_internal.h"
#include "engine/portable/record_pool.h"

class CMessageBank {
public:
    SObjData* Allocate() { return records_.Allocate(); }
    bool Free(SObjData* record) { return records_.Free(record); }
    void Term() { records_.Term(); }
    std::size_t Size() const { return records_.Size(); }
private:
    nolf::RecordPool<SObjData> records_;
public:
    StructBank& m_Bank = records_.m_Bank;
};
