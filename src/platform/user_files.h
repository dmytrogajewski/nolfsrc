#pragma once
#include "platform/filesystem.h"

namespace nolf::platform {
// Writable game files have logical, case-insensitive names just like assets.
// The default root is the user's data directory; tests can supply an isolated root.
class UserFiles {
public:
    UserFiles();
    explicit UserFiles(std::filesystem::path root);
    std::filesystem::path path(std::string_view logical) const;
    bool file_exists(std::string_view logical) const;
    bool directory_exists(std::string_view logical) const;
    void create_directory(std::string_view logical) const;
    std::vector<std::byte> read(std::string_view logical, std::size_t maximum = 64*1024*1024) const;
    void write(std::string_view logical, std::span<const std::byte>) const;
private:
    std::filesystem::path root_;
};
}
