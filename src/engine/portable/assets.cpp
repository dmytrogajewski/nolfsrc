#include "engine/portable/assets.h"
#include <set>
#include <map>
#include <limits>
#include <sstream>
#include <stdexcept>
namespace nolf {
void AssetSource::read_at(std::uint64_t position, std::span<std::byte> output) const {
    if (!file || offset > file->size() || size > file->size() - offset || position > size || output.size() > size - position)
        throw std::runtime_error("asset read outside extent");
    file->read_at(offset + position, output);
}
AssetSource Assets::locate(std::string_view name) const {
    const auto key = platform::asset_key(name);
    for (auto it = mounts_.rbegin(); it != mounts_.rend(); ++it) {
        if (it->archive) {
            const auto found = it->archive->entries().find(key);
            if (found != it->archive->entries().end())
                return {it->archive->file(), found->second.offset, found->second.size};
        }
        if (it->directory) {
            auto path = it->directory->resolve(key);
            if (!path.empty()) {
                auto file = std::make_shared<platform::InputFile>(std::move(path));
                return {file, 0, file->size()};
            }
        }
    }
    throw std::runtime_error("asset not found: " + key);
}
void Assets::mount_archive(const std::filesystem::path& path) {
    mounts_.push_back({std::make_unique<RezArchive>(path), {}});
}
void Assets::mount_directory(const std::filesystem::path& path) {
    mounts_.push_back({{}, std::make_unique<platform::DirectoryIndex>(path)});
}
std::vector<std::filesystem::path> game_resource_paths(const std::filesystem::path& path) {
    const auto data = platform::read_file(path / "archives.txt");
    std::istringstream input(std::string(reinterpret_cast<const char*>(data.data()), data.size()));
    std::string archive;
    std::vector<std::filesystem::path> paths;
    while (std::getline(input, archive)) {
        if (!archive.empty() && archive.back() == '\r') archive.pop_back();
        if (archive.empty()) continue;
        const auto key = platform::asset_key(archive);
        if (key.find('/') != key.npos || !key.ends_with(".rez")) throw std::runtime_error("invalid archive name in archives.txt");
        paths.push_back(path / key);
    }
    if (paths.empty()) throw std::runtime_error("game archive list is empty");
    // The modern build's data edits override the archives, as the engine's
    // custom/ rez directory does; the data directory itself comes last.
    if (std::filesystem::is_directory(path / "custom")) paths.push_back(path / "custom");
    paths.push_back(path);
    return paths;
}
void Assets::mount_game(const std::filesystem::path& path) {
    auto paths = game_resource_paths(path);
    std::vector<Mount> staged;
    for (const auto& entry : paths) {
        if (std::filesystem::is_directory(entry)) staged.push_back({{}, std::make_unique<platform::DirectoryIndex>(entry)});
        else staged.push_back({std::make_unique<RezArchive>(entry), {}});
    }
    for (auto& mount : staged) mounts_.push_back(std::move(mount));
}

bool Assets::contains(std::string_view name) const {
    for (auto it = mounts_.rbegin(); it != mounts_.rend(); ++it) {
        if (it->archive && it->archive->contains(name)) return true;
        if (it->directory && !it->directory->resolve(name).empty()) return true;
    }
    return false;
}
std::vector<std::byte> Assets::read(std::string_view name) const {
    const auto source = locate(name);
    if (source.size > std::numeric_limits<std::size_t>::max()) throw std::length_error("asset exceeds address space");
    std::vector<std::byte> bytes(static_cast<std::size_t>(source.size));
    source.read_at(0, bytes);
    return bytes;
}

std::vector<std::string> Assets::names() const {
    std::set<std::string> names;
    for (const auto& mount : mounts_) if (mount.archive)
        for (const auto& [key, entry] : mount.archive->entries()) names.insert(key);
    for (const auto& mount : mounts_) if (mount.directory)
        for (const auto& key : mount.directory->names()) names.insert(key);
    return {names.begin(), names.end()};
}
std::vector<AssetEntry> Assets::list(std::string_view directory) const {
    std::string prefix;
    if (!directory.empty() && directory != "." && directory != "./" && directory != ".\\")
        prefix = platform::asset_key(directory) + '/';
    std::map<std::string, bool> entries;
    auto add = [&](std::string_view name, bool isDirectory) {
        if (!name.starts_with(prefix) || name.size() == prefix.size()) return;
        auto child = name.substr(prefix.size());
        const auto slash = child.find('/');
        if (slash != child.npos) { child = child.substr(0, slash); isDirectory = true; }
        entries[std::string(child)] = isDirectory;
    };
    for (const auto& mount : mounts_) {
        if (mount.archive) for (const auto& [name, entry] : mount.archive->entries()) add(name, false);
        if (mount.directory) for (const auto& [name, entry] : mount.directory->entries()) add(name, entry.directory);
    }
    std::vector<AssetEntry> result;
    for (auto& [name, directoryEntry] : entries) result.push_back({name, directoryEntry});
    return result;
}
}
