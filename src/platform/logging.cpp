#include "platform/logging.h"
#include "platform/system.h"
#include <atomic>
#include <fstream>
#include <iostream>
#include <mutex>

namespace nolf::platform {
namespace {
std::atomic<LogSink> forwarded_log{nullptr};
struct Logger {
    std::mutex mutex;
    bool configured = false;
    std::ofstream file;
};
Logger& logger() { static Logger state; return state; }
void open_log(Logger& state, const std::filesystem::path& path) {
    state.file.close(); state.file.clear(); state.configured = true;
    if (path.empty()) return;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    state.file.open(path, std::ios::binary | std::ios::app);
}
}
void set_log_file(std::filesystem::path path) {
    auto& state = logger();
    std::lock_guard lock(state.mutex);
    open_log(state, path);
}
void set_log_sink(LogSink sink) noexcept { forwarded_log = sink; }
void log(std::string_view subsystem, std::string_view message) {
    if (const auto sink = forwarded_log.load()) {
        sink(subsystem.data(), subsystem.size(), message.data(), message.size());
        return;
    }
    auto& state = logger();
    std::lock_guard lock(state.mutex);
    const auto line = '[' + std::string(subsystem) + "] " + std::string(message) + '\n';
    std::clog << line;
    if (!state.configured) {
        try { open_log(state, user_directories().state / "modern.log"); }
        catch (...) { state.configured = true; }
    }
    if (state.file.is_open()) {
        state.file << line;
        state.file.flush();
    }
}
}
