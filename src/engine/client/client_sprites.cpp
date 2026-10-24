#include "engine/client/client_sprites.h"
#include <numeric>

namespace nolf {
SpriteResource::SpriteResource(SpriteAnimation data) : data_(std::move(data)),indices_(data_.frames.size()) {
    if (!data_.frames_per_second || data_.frames_per_second>1000 || data_.frames.size()>65536) throw std::invalid_argument("invalid native sprite animation");
    std::iota(indices_.begin(),indices_.end(),0u);
    animation_={indices_.data(),static_cast<uint32>(indices_.size()),data_.frames_per_second}; sprite_={&animation_,1};
}
ClientSprites::ClientSprites(std::shared_ptr<const Assets> assets) : assets_(std::move(assets)) {
    if (!assets_) throw std::invalid_argument("sprite cache needs assets");
}
void ClientSprites::require_owner() const { if (owner_!=std::this_thread::get_id()) throw std::logic_error("sprite cache used from another thread"); }
void ClientSprites::free_unused() { require_owner(); std::erase_if(cache_,[](const auto& entry) { return entry.second.expired(); }); }
std::shared_ptr<SpriteResource> ClientSprites::load(std::string_view filename) {
    require_owner(); const auto name=platform::asset_key(filename);
    if (const auto it=cache_.find(name);it!=cache_.end()) if (auto sprite=it->second.lock()) return sprite;
    const auto source=assets_->locate(name);
    if (source.size>16*1024*1024) throw std::invalid_argument("SPR exceeds file budget");
    std::vector<std::byte> bytes(static_cast<std::size_t>(source.size)); source.read_at(0,bytes);
    auto sprite=std::make_shared<SpriteResource>(decode_sprite(bytes));
    if (cache_.size()>=65536) { free_unused(); if (cache_.size()>=65536) throw std::bad_alloc(); }
    cache_[name]=sprite; return sprite;
}
}
