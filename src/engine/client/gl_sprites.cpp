#include "engine/client/gl_sprites.h"
#include "rdev.h"
#include <cmath>

namespace nolf {
namespace {
struct Frame {
    Frame() {
        rd_PushAttrib(RD_ATTR_ALL);
        for (int unit=1;unit<g_RDCaps.nTextureUnits;++unit) { rd_ActiveTexture(unit); rd_Disable(RD_TEXTURE_2D); }
        rd_ActiveTexture(0); rd_MatrixMode(RD_TEXTURE); rd_PushMatrix(); rd_LoadIdentity();
        rd_MatrixMode(RD_MODELVIEW); rd_PushMatrix(); rd_LoadIdentity();
    }
    ~Frame() { rd_MatrixMode(RD_MODELVIEW); rd_PopMatrix(); rd_MatrixMode(RD_TEXTURE); rd_PopMatrix(); rd_PopAttrib(); }
};
}
GlSprites::GlSprites(GlTextures& textures) : textures_(textures) {}
std::size_t GlSprites::draw(std::span<const SpriteVertex> vertices,const std::shared_ptr<const TextureResource>& skin,ClientTextures::Render& render,const SpriteDrawStyle& style) {
    if (owner_!=std::this_thread::get_id()) throw std::logic_error("sprite drawing used from another thread");
    if (!rd_DeviceGeneration()) throw std::logic_error("sprite drawing requires an active device");
    if (vertices.size()>1024 || !skin || static_cast<unsigned>(style.blend)>static_cast<unsigned>(ObjectBlend::Multiply)) throw std::invalid_argument("invalid sprite draw request");
    for (const auto& vertex : vertices) if (!std::isfinite(vertex.position.x) || !std::isfinite(vertex.position.y) || !std::isfinite(vertex.position.z) || !std::isfinite(vertex.u) || !std::isfinite(vertex.v)) throw std::invalid_argument("nonfinite sprite draw vertex");
    if (vertices.size()<3) return 0;
    Frame frame; textures_.bind(skin,style.sampling); render.drawn(skin);
    rd_Enable(RD_TEXTURE_2D); rd_TexEnv(RD_ENV_MODULATE); rd_CullBack(false); rd_DepthMask(false); rd_DepthFunc(RD_LEQUAL); rd_ColorMask(true);
    if (style.depth_test) rd_Enable(RD_DEPTH_TEST); else rd_Disable(RD_DEPTH_TEST);
    if (style.alpha_test) { rd_Enable(RD_ALPHA_TEST); rd_AlphaGreater(.1666f); } else rd_Disable(RD_ALPHA_TEST);
    if (style.blend==ObjectBlend::Opaque) rd_Disable(RD_BLEND);
    else {
        rd_Enable(RD_BLEND);
        if (style.blend==ObjectBlend::Alpha) rd_BlendFunc(RD_SRC_ALPHA,RD_ONE_MINUS_SRC_ALPHA);
        else if (style.blend==ObjectBlend::Additive) rd_BlendFunc(RD_ONE,RD_ONE);
        else rd_BlendFunc(RD_ZERO,RD_SRC_COLOR);
    }
    rd_Color4ubv(style.color.data()); rd_Begin(RD_TRIANGLE_FAN);
    for (const auto& vertex : vertices) { rd_TexCoord2f(vertex.u,vertex.v); rd_Vertex3f(vertex.position.x,vertex.position.y,vertex.position.z); }
    rd_End(); return vertices.size()-2;
}
}
