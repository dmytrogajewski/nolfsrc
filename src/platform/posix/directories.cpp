#include "platform/system.h"
#include <cstdlib>
#include <stdexcept>
namespace nolf::platform {
static std::filesystem::path environment_path(const char* name, const std::filesystem::path& fallback) {
    const char* value = std::getenv(name);
    if (value && std::filesystem::path(value).is_absolute()) return value;
    return fallback;
}
UserDirectories host_user_directories() {
    const auto home = environment_path("HOME", {});
    if (home.empty()) throw std::runtime_error("cannot determine the user's home directory");
    return {
        environment_path("XDG_CONFIG_HOME", home / ".config") / "nolf",
        environment_path("XDG_DATA_HOME", home / ".local/share") / "nolf",
        environment_path("XDG_CACHE_HOME", home / ".cache") / "nolf",
        environment_path("XDG_STATE_HOME", home / ".local/state") / "nolf"
    };
}
}
