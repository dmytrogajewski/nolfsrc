#pragma once
#include "engine/client/gl_textures.h"
#include "engine/client/render_blend.h"
#include "engine/client/sprite_geometry.h"
namespace nolf {
struct SpriteDrawStyle {
    std::array<uint8,4> color{255,255,255,255};
    TextureSampling sampling;
    ObjectBlend blend=ObjectBlend::Alpha;
    bool depth_test=true,alpha_test=false;
};
// Prepared camera-space sprite polygons; the scene owns ordering, camera,
// animation, lighting and the lifetime of any clipping geometry.
class GlSprites {
public:
    explicit GlSprites(GlTextures&);
    std::size_t draw(std::span<const SpriteVertex>,const std::shared_ptr<const TextureResource>&,
        ClientTextures::Render&,const SpriteDrawStyle& = {});
private:
    GlTextures& textures_;
    const std::thread::id owner_=std::this_thread::get_id();
};
}
