#include "platform/system.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <stdexcept>

namespace nolf::platform {
static std::filesystem::path known_directory(REFKNOWNFOLDERID id) {
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &path)))
        throw std::runtime_error("cannot determine Windows user data directory");
    std::filesystem::path result(path);
    CoTaskMemFree(path);
    return result / L"NOLF";
}
UserDirectories host_user_directories() {
    const auto roaming = known_directory(FOLDERID_RoamingAppData);
    const auto local = known_directory(FOLDERID_LocalAppData);
    return {roaming / L"Config", roaming / L"Data", local / L"Cache", local / L"State"};
}
}
