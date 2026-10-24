#pragma once
#include "engine/client/gl_textures.h"
#include "engine/server/model_geometry.h"
#include "engine/client/render_blend.h"

namespace nolf {
using ModelBlend=ObjectBlend;
struct ModelDrawStyle {
    std::array<std::uint8_t,4> color{255,255,255,255};
    std::span<const std::array<std::uint8_t,4>> vertex_colors;
    // Per-vertex texture coordinates in place of the triangles' (chrome).
    std::span<const std::array<float,2>> vertex_uvs;
    // World-space vertex normals, for a lighting program the caller has in use.
    std::span<const LTVector> normals;
    TextureSampling sampling;
    ModelBlend blend=ModelBlend::Opaque;
    bool cull=true,wireframe=false,alpha_test=false,depth_test=true,depth_write=true;
};
// Draws canonical world-space model geometry through the device API. Camera,
// lighting, object visibility and pass ordering belong to the scene renderer.
class GlModels {
public:
    explicit GlModels(GlTextures&);
    std::size_t draw(const ModelPose&,const ModelPieceData&,
        const std::shared_ptr<const TextureResource>&,ClientTextures::Render&,const ModelDrawStyle& = {},
        std::span<const LTVector> prepared_positions = {});
private:
    GlTextures& textures_;
    const std::thread::id owner_=std::this_thread::get_id();
};
}
