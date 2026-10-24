#include "platform/filesystem.h"
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <system_error>

namespace nolf::platform::detail {
void commit_file(const std::filesystem::path& temporary, const std::filesystem::path& destination) {
    const int file = open(temporary.c_str(), O_RDONLY | O_CLOEXEC);
    if (file < 0) throw std::system_error(errno, std::generic_category(), "open temporary save");
    const int status = fsync(file), saved_errno = errno;
    close(file);
    if (status < 0) throw std::system_error(saved_errno, std::generic_category(), "flush temporary save");
    std::filesystem::rename(temporary, destination);
    auto parent = destination.parent_path();
    if (parent.empty()) parent = ".";
    const int directory = open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory < 0) throw std::system_error(errno, std::generic_category(), "open save directory");
    const int result = fsync(directory), directory_errno = errno;
    close(directory);
    if (result < 0) throw std::system_error(directory_errno, std::generic_category(), "flush save directory");
}
}
