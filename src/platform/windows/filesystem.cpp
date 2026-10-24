#include "platform/filesystem.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <system_error>

namespace nolf::platform::detail {
void commit_file(const std::filesystem::path& temporary, const std::filesystem::path& destination) {
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::system_error(GetLastError(), std::system_category(), "open temporary save");
    const bool flushed = FlushFileBuffers(file);
    const auto error = GetLastError();
    CloseHandle(file);
    if (!flushed) throw std::system_error(error, std::system_category(), "flush temporary save");
    if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::system_error(GetLastError(), std::system_category(), "replace save");
}
}
