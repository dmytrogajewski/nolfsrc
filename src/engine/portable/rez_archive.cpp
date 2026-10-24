#include "engine/portable/rez_archive.h"
#include "engine/portable/binary.h"
#include "platform/filesystem.h"
#include <algorithm>
#include <stdexcept>

namespace nolf {
RezArchive::RezArchive(std::filesystem::path path)
    : path_(std::move(path)), file_(std::make_shared<platform::InputFile>(path_)), size_(file_->size()) {
    const auto header = read_at(0, 139);
    const std::string title(reinterpret_cast<const char*>(header.data()), 127);
    if (title.find("RezMgr") == title.npos) throw std::runtime_error("not a RezMgr archive: " + path_.string());
    BinaryReader reader{std::span(header).subspan(127)};
    if (reader.integer<std::uint32_t>() != 1) throw std::runtime_error("unsupported REZ version");
    const auto root = reader.integer<std::uint32_t>();
    const auto root_size = reader.integer<std::uint32_t>();
    directory(root, root_size, "", 0);
}
std::vector<std::byte> RezArchive::read_at(std::uint64_t offset, std::uint64_t length) const {
    if (offset > size_ || length > size_ - offset) throw std::runtime_error("REZ extent outside archive: " + path_.string());
    std::vector<std::byte> data(static_cast<std::size_t>(length));
    file_->read_at(offset, data);
    return data;
}
void RezArchive::directory(std::uint32_t offset, std::uint32_t size, const std::string& prefix, unsigned depth) {
    if (depth > 64 || !directories_.insert(offset).second) throw std::runtime_error("cyclic or excessively deep REZ directory");
    // Directory metadata is small; a corrupt length must not allocate an entire archive.
    if (size > 64 * 1024 * 1024) throw std::runtime_error("REZ directory metadata is too large");
    auto data = read_at(offset, size);
    BinaryReader reader(data);
    struct Child { std::uint32_t offset, size; std::string name; };
    std::vector<Child> children;
    while (reader.remaining()) {
        const auto type = reader.integer<std::uint32_t>();
        const auto position = reader.integer<std::uint32_t>();
        const auto length = reader.integer<std::uint32_t>();
        reader.integer<std::uint32_t>(); // timestamp
        if (type == 1) {
            auto name = reader.cstring();
            if (name.find_first_of("/\\") != name.npos) throw std::runtime_error("invalid REZ directory component");
            children.push_back({position, length, prefix + platform::asset_key(name) + '/'});
        } else if (type == 0) {
            reader.integer<std::uint32_t>(); // resource ID
            auto tag = reader.bytes(4);
            const auto keys = reader.integer<std::uint32_t>();
            auto name = reader.cstring();
            reader.bytes(1); // description terminator
            if (keys > reader.remaining() / 4) throw std::runtime_error("truncated REZ resource keys");
            reader.bytes(static_cast<std::size_t>(keys) * 4);
            std::string extension;
            for (auto byte : tag) { if (byte == std::byte{}) break; extension += static_cast<char>(byte); }
            std::reverse(extension.begin(), extension.end());
            if (!extension.empty()) name += '.' + extension;
            const auto key = platform::asset_key(prefix + name);
            if (position > size_ || length > size_ - position) throw std::runtime_error("REZ resource outside archive");
            if (!entries_.emplace(key, RezEntry{key, position, length}).second) throw std::runtime_error("duplicate REZ asset: " + key);
        } else {
            throw std::runtime_error("unknown REZ directory record");
        }
    }
    for (const auto& child : children) directory(child.offset, child.size, child.name, depth + 1);
}
bool RezArchive::contains(std::string_view name) const { return entries_.contains(platform::asset_key(name)); }
std::vector<std::byte> RezArchive::read(std::string_view name) const {
    const auto it = entries_.find(platform::asset_key(name));
    if (it == entries_.end()) throw std::runtime_error("missing REZ asset: " + std::string(name));
    return read_at(it->second.offset, it->second.size);
}
}
