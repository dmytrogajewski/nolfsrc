#include "platform/timing.h"
#include <stdexcept>
#include <thread>

namespace nolf::platform {
FrameLimiter::FrameLimiter(unsigned frequency) {
    if (!frequency || frequency > 10000) throw std::invalid_argument("Invalid frame frequency");
    period_ = std::chrono::duration_cast<Clock::duration>(std::chrono::nanoseconds(1000000000 / frequency));
}
Clock::time_point FrameLimiter::deadline(Clock::time_point now) {
    if (!previous_ || now - *previous_ > period_ * 2) {
        previous_ = now;
        return now;
    }
    *previous_ += period_;
    return now < *previous_ ? *previous_ : now;
}
void FrameLimiter::wait() { std::this_thread::sleep_until(deadline(Clock::now())); }
}
