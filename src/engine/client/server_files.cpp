#include "server_files.h"
#include "engine/portable/binary.h"
#include <stdexcept>

namespace nolf {
ServerFiles::ServerFiles(std::shared_ptr<const Assets> assets) : assets_(std::move(assets)) {
    if (!assets_) throw std::invalid_argument("client asset catalogue is required");
}
std::vector<PacketBytes> ServerFiles::announce(std::span<const std::byte> payload) {
    if (payload.size() > 65534) throw std::runtime_error("oversized server file announcement");
    BinaryReader input(payload);
    std::map<std::uint16_t,ServerFile> staged;
    std::vector<PacketBytes> replies;
    BinaryWriter reply;
    reply.integer<std::uint8_t>(0x36);
    unsigned records = 0;
    std::size_t staged_names = 0;
    while (input.remaining()) {
        if (++records > 5000) throw std::runtime_error("too many announced files in one packet");
        const auto id = input.integer<std::uint16_t>();
        const auto size = input.integer<std::uint32_t>();
        const auto name = platform::asset_key(input.cstring());
        if (id > 0x7fff || name.size() > 4095) throw std::runtime_error("invalid server file identity");
        const auto existing = files_.find(id);
        const auto pending = staged.find(id);
        const auto* previous = existing != files_.end() ? &existing->second : pending != staged.end() ? &pending->second : nullptr;
        if (previous && (previous->name != name || previous->size != size))
            throw std::runtime_error("server reused an active file identifier");
        if (!previous) {
            if (name.size() > 16*1024*1024-name_bytes_-staged_names) throw std::runtime_error("server file catalogue budget exceeded");
            AssetSource source;
            if (assets_->contains(name)) {
                source = assets_->locate(name);
                if (source.size != size) throw std::runtime_error("client/server asset size mismatch: "+name);
            }
            staged.emplace(id,ServerFile{id,size,name,std::move(source)});
            staged_names += name.size();
        }
        // The original NOLF client has no download consumer on this path.
        // Missing assets retain metadata and fail when actually required.
        reply.integer<std::uint16_t>(id | 0x8000);
        if (reply.data().size() >= 4093) {
            replies.push_back(reply.data());
            reply = BinaryWriter{}; reply.integer<std::uint8_t>(0x36);
        }
    }
    if (reply.data().size() > 1) replies.push_back(reply.data());
    files_.merge(staged); // node transfer commits without allocating
    name_bytes_ += staged_names;
    return replies;
}
const ServerFile* ServerFiles::find(std::uint16_t id) const {
    const auto found = files_.find(id);
    return found == files_.end() ? nullptr : &found->second;
}
const ServerFile& ServerFiles::require(std::uint16_t id) const {
    const auto* file = find(id);
    if (!file) throw std::runtime_error("unannounced server file ID "+std::to_string(id));
    if (!file->source.file) throw std::runtime_error("missing client asset: "+file->name);
    return *file;
}
}
