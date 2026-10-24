#include "platform/system.h"
#include <cstdlib>
#include <thread>

namespace nolf::platform {
void yield_thread() { std::this_thread::yield(); }
void sleep_for(std::chrono::milliseconds duration) { std::this_thread::sleep_for(duration); }
std::uint64_t monotonic_nanoseconds() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count());
}
UserDirectories user_directories() {
    const char* home = std::getenv("NOLF_USER_HOME");
    if (!home || !*home) return host_user_directories();
    const auto root = std::filesystem::u8path(home);
    if (!root.is_absolute()) return host_user_directories();
    return {root / "config", root / "data", root / "cache", root / "state"};
}
}
