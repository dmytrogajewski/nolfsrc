#pragma once
#include "engine/client/effect_geometry.h"
#include "engine/client/gl_textures.h"
#include "engine/client/render_blend.h"

namespace nolf {
struct EffectDrawStyle {
    TextureSampling sampling;
    ObjectBlend blend=ObjectBlend::Alpha;
    bool depth_test=true,alpha_test=false;
};
// Device adapter for prepared per-vertex colored line segments and quads.
class GlEffects {
public:
    explicit GlEffects(GlTextures& textures) : textures_(textures) {}
    std::size_t draw(std::span<const EffectVertex>,bool quads,const std::shared_ptr<const TextureResource>&,
        ClientTextures::Render&,const EffectDrawStyle& = {});
private:
    GlTextures& textures_;
    const std::thread::id owner_=std::this_thread::get_id();
};
}
