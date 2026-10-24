#pragma once
#include "engine/portable/rez_archive.h"
#include "platform/filesystem.h"
#include <memory>

namespace nolf {
std::vector<std::filesystem::path> game_resource_paths(const std::filesystem::path&);
struct AssetSource {
    std::shared_ptr<const platform::InputFile> file;
    std::uint64_t offset = 0, size = 0;
    void read_at(std::uint64_t position, std::span<std::byte> output) const;
};
struct AssetEntry { std::string name; bool directory = false; };
// Later mounts override earlier mounts, matching LithTech's resource search.
class Assets {
public:
    void mount_archive(const std::filesystem::path& path);
    void mount_directory(const std::filesystem::path& path);
    void mount_game(const std::filesystem::path& path);
    bool contains(std::string_view name) const;
    std::vector<std::byte> read(std::string_view name) const;
    std::vector<std::string> names() const;
    AssetSource locate(std::string_view name) const;
    std::vector<AssetEntry> list(std::string_view directory) const;
private:
    struct Mount { std::unique_ptr<RezArchive> archive; std::unique_ptr<platform::DirectoryIndex> directory; };
    std::vector<Mount> mounts_;
};
}
