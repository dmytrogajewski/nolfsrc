#pragma once
#include "engine/server/structbank.h"
#include <mutex>
#include <new>
#include <unordered_set>

namespace nolf {
// Owns constructed records as well as their storage. Destruction may free
// other records from this pool; termination rejects allocations until done.
template<class T> class RecordPool {
public:
    explicit RecordPool(uint32 cache = 32) { sb_InitAligned(&m_Bank,sizeof(T),cache,alignof(T)); }
    ~RecordPool() { Term(); }
    RecordPool(const RecordPool&) = delete;
    RecordPool& operator=(const RecordPool&) = delete;
    T* Allocate() {
        std::lock_guard lock(mutex_);
        if (terminating_ || pending_) return nullptr;
        auto* memory = sb_Allocate(&m_Bank);
        if (!memory) return nullptr;
        ++active_;
        T* record = nullptr;
        try {
            record = new (memory) T{};
            if (pending_) { record->~T(); sb_Free(&m_Bank,memory); record = nullptr; }
            else live_.insert(record);
        }
        catch (...) {
            if (record) record->~T();
            sb_Free(&m_Bank,memory);
            record = nullptr;
        }
        --active_;
        if (!active_ && pending_) Term();
        return record;
    }
    bool Free(T* record) {
        std::lock_guard lock(mutex_);
        if (!record || !live_.erase(record)) return false;
        ++active_;
        record->~T(); sb_Free(&m_Bank,record);
        --active_;
        if (!active_ && pending_) Term();
        return true;
    }
    void Term() {
        std::lock_guard lock(mutex_);
        if (terminating_) return;
        if (active_) { pending_ = true; return; }
        terminating_ = true;
        pending_ = false;
        while (!live_.empty()) Free(*live_.begin());
        sb_Term(&m_Bank);
        terminating_ = false;
    }
    std::size_t Size() const { std::lock_guard lock(mutex_); return live_.size(); }
    bool Contains(T* record) const { std::lock_guard lock(mutex_); return live_.contains(record); }
    StructBank m_Bank;
private:
    mutable std::recursive_mutex mutex_;
    std::unordered_set<T*> live_;
    uint32 active_ = 0;
    bool terminating_ = false, pending_ = false;
};
}
