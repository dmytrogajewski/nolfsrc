#pragma once
#include "platform/filesystem.h"
#include <cstdint>

namespace nolf {
// Portable replacement for LoadString and FindResource. The importer writes
// raw resource payloads and UTF-8 strings without retaining executable code.
class Resources {
public:
    explicit Resources(const std::filesystem::path& data);
    std::string string(std::string_view module, std::uint32_t id, std::uint32_t language = 1033) const;
    std::vector<std::byte> raw(std::string_view module, std::string_view type, std::string_view id,
                               std::uint32_t language = 1033) const;
private:
    std::vector<std::byte> get(std::string_view module, const std::string& suffix, std::uint32_t language) const;
    platform::DirectoryIndex files_;
};
}
