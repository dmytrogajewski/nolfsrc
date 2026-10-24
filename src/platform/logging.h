#pragma once
#include <cstddef>
#include <filesystem>
#include <string_view>

namespace nolf::platform {
void log(std::string_view subsystem, std::string_view message);
// The callback crosses the native module boundary using byte spans only.
using LogSink = void (*)(const char* subsystem, std::size_t subsystem_size, const char* message, std::size_t message_size);
void set_log_sink(LogSink sink) noexcept;
// Appends and flushes each line. Empty disables the file; the default is in
// the user's state directory. The host owns destination and synchronization.
void set_log_file(std::filesystem::path path);
}
