#pragma once
#include "engine/portable/assets.h"
#include "engine/portable/sprite.h"
#include "engine/server/sprite_data.h"
#include <thread>
#include <unordered_map>

namespace nolf {
class ClientScene;
class SpriteResource {
public:
    explicit SpriteResource(SpriteAnimation);
    SpriteResource(const SpriteResource&)=delete;
    SpriteResource& operator=(const SpriteResource&)=delete;
    const SpriteAnimation& animation() const { return data_; }
private:
    friend class ClientScene;
    SpriteAnimation data_;
    std::vector<uint32> indices_;
    SpriteAnim animation_;
    Sprite sprite_;
};
class ClientSprites {
public:
    explicit ClientSprites(std::shared_ptr<const Assets>);
    std::shared_ptr<SpriteResource> load(std::string_view);
    void free_unused();
private:
    const std::thread::id owner_=std::this_thread::get_id();
    std::shared_ptr<const Assets> assets_;
    std::unordered_map<std::string,std::weak_ptr<SpriteResource>> cache_;
    void require_owner() const;
};
}
