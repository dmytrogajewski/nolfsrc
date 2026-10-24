#pragma once
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stop_token>

namespace nolf::platform {
using RecursiveMutex = std::recursive_mutex;
// Manual-reset event: set wakes all waiters and stays set until reset.
class Event {
public:
    explicit Event(bool signaled = false) : signaled_(signaled) {}
    void set();
    void reset();
    bool wait(std::stop_token stop = {});
    bool wait_for(std::chrono::milliseconds duration);
private:
    std::mutex mutex_;
    std::condition_variable_any ready_;
    bool signaled_;
};
}
