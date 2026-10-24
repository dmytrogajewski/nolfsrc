#include "platform/sync.h"
namespace nolf::platform {
void Event::set() {
    { std::lock_guard lock(mutex_); signaled_ = true; }
    ready_.notify_all();
}
void Event::reset() { std::lock_guard lock(mutex_); signaled_ = false; }
bool Event::wait(std::stop_token stop) {
    std::unique_lock lock(mutex_);
    return ready_.wait(lock, stop, [&] { return signaled_; });
}
bool Event::wait_for(std::chrono::milliseconds duration) {
    std::unique_lock lock(mutex_);
    return ready_.wait_for(lock, duration, [&] { return signaled_; });
}
}
