#pragma once
#include "engine/portable/assets.h"
#include <map>
#include <span>

namespace nolf {
using PacketBytes = std::vector<std::byte>;
struct ServerFile {
    std::uint16_t id;
    std::uint32_t size;
    std::string name;
    AssetSource source;
};
// Server IDs are session-local wire values. They resolve to owned native asset
// sources, independently of directory mounts and the server's file manager.
class ServerFiles {
public:
    explicit ServerFiles(std::shared_ptr<const Assets>);
    std::vector<PacketBytes> announce(std::span<const std::byte> payload);
    const ServerFile* find(std::uint16_t) const;
    const ServerFile& require(std::uint16_t) const;
    void clear() { files_.clear(); name_bytes_ = 0; }
    std::size_t size() const { return files_.size(); }
private:
    std::shared_ptr<const Assets> assets_;
    std::map<std::uint16_t,ServerFile> files_;
    std::size_t name_bytes_ = 0;
};
}
