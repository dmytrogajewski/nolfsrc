#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "platform/filesystem.h"

namespace nolf {
struct RezEntry {
    std::string name;
    std::uint32_t offset, size;
};
class RezArchive {
public:
    explicit RezArchive(std::filesystem::path path);
    const std::unordered_map<std::string, RezEntry>& entries() const { return entries_; }
    bool contains(std::string_view name) const;
    std::vector<std::byte> read(std::string_view name) const;
    std::shared_ptr<const platform::InputFile> file() const { return file_; }
private:
    std::vector<std::byte> read_at(std::uint64_t offset, std::uint64_t length) const;
    void directory(std::uint32_t offset, std::uint32_t size, const std::string& prefix, unsigned depth);
    std::filesystem::path path_;
    std::shared_ptr<const platform::InputFile> file_;
    std::uint64_t size_{};
    std::unordered_map<std::string, RezEntry> entries_;
    std::unordered_set<std::uint32_t> directories_;
};
}
