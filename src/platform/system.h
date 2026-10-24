#pragma once
#include "platform/logging.h"
#include <chrono>
#include <filesystem>
#include <string_view>

namespace nolf::platform {
using Clock = std::chrono::steady_clock;
std::uint64_t monotonic_nanoseconds();
struct UserDirectories {
    std::filesystem::path config, data, cache, state;
};
// An absolute NOLF_USER_HOME holds all four as config, data, cache and state
// beneath it (portable installs, isolated tests); otherwise the host's own.
UserDirectories user_directories();
UserDirectories host_user_directories();
struct LocalDate { int year, month, day, weekday; };
LocalDate local_date();
void yield_thread();
void sleep_for(std::chrono::milliseconds);
}
