#pragma once
#include "engine/client/polygrid_geometry.h"
#include "engine/client/gl_textures.h"
#include "engine/client/render_blend.h"
namespace nolf {
struct PolygridDrawStyle {
    TextureSampling sampling;
    ObjectBlend blend=ObjectBlend::Opaque;
    bool depth_write=true,alpha_test=false,environment_only=false,environment_add=false;
};
class GlPolygrids {
public:
    explicit GlPolygrids(GlTextures& textures) : textures_(textures) {}
    std::size_t draw(const PolygridMesh&,const std::shared_ptr<const TextureResource>&,
        const std::shared_ptr<const TextureResource>& environment,ClientTextures::Render&,const PolygridDrawStyle& = {});
private:
    GlTextures& textures_;
    const std::thread::id owner_=std::this_thread::get_id();
};
}
