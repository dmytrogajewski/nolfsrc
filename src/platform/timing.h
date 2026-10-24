#pragma once
#include "platform/system.h"
#include <optional>

namespace nolf::platform {
// A monotonic frame deadline. A late frame resets the schedule rather than
// accumulating an unbounded backlog of immediate updates.
class FrameLimiter {
public:
    explicit FrameLimiter(unsigned frequency);
    void reset() { previous_.reset(); }
    Clock::time_point deadline(Clock::time_point now);
    void wait();
private:
    Clock::duration period_;
    std::optional<Clock::time_point> previous_;
};
}
