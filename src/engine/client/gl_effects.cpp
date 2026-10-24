#include "engine/client/gl_effects.h"
#include "rdev.h"
#include <cmath>

namespace nolf {
namespace {
struct Frame {
    Frame() {
        rd_PushAttrib(RD_ATTR_ALL);
        for (int unit=1;unit<g_RDCaps.nTextureUnits;++unit) { rd_ActiveTexture(unit); rd_Disable(RD_TEXTURE_2D); }
        rd_ActiveTexture(0); rd_MatrixMode(RD_TEXTURE); rd_PushMatrix(); rd_LoadIdentity(); rd_MatrixMode(RD_MODELVIEW); rd_PushMatrix(); rd_LoadIdentity();
    }
    ~Frame() { rd_MatrixMode(RD_MODELVIEW); rd_PopMatrix(); rd_MatrixMode(RD_TEXTURE); rd_PopMatrix(); rd_PopAttrib(); }
};
}
std::size_t GlEffects::draw(std::span<const EffectVertex> vertices,bool quads,const std::shared_ptr<const TextureResource>& texture,ClientTextures::Render& render,const EffectDrawStyle& style) {
    if (owner_!=std::this_thread::get_id() || !rd_DeviceGeneration()) throw std::logic_error("effect drawing requires its owning device thread");
    const auto count=quads ? 4u : 2u;
    if (vertices.size()%count || vertices.size()/count>262144 || static_cast<unsigned>(style.blend)>static_cast<unsigned>(ObjectBlend::Multiply)) throw std::invalid_argument("invalid effect draw request");
    for (const auto& vertex : vertices) if (!std::isfinite(vertex.position.x) || !std::isfinite(vertex.position.y) || !std::isfinite(vertex.position.z) || !std::isfinite(vertex.u) || !std::isfinite(vertex.v)) throw std::invalid_argument("nonfinite effect draw vertex");
    if (vertices.empty()) return 0;
    Frame frame;
    if (texture) { textures_.bind(texture,style.sampling); render.drawn(texture); rd_Enable(RD_TEXTURE_2D); rd_TexEnv(RD_ENV_MODULATE); } else rd_Disable(RD_TEXTURE_2D);
    rd_CullBack(false); rd_DepthMask(false); rd_DepthFunc(RD_LEQUAL); rd_ColorMask(true);
    if (style.depth_test) rd_Enable(RD_DEPTH_TEST); else rd_Disable(RD_DEPTH_TEST);
    if (style.alpha_test) { rd_Enable(RD_ALPHA_TEST); rd_AlphaGreater(.1666f); } else rd_Disable(RD_ALPHA_TEST);
    if (style.blend==ObjectBlend::Opaque) rd_Disable(RD_BLEND);
    else { rd_Enable(RD_BLEND); if (style.blend==ObjectBlend::Alpha) rd_BlendFunc(RD_SRC_ALPHA,RD_ONE_MINUS_SRC_ALPHA); else if (style.blend==ObjectBlend::Additive) rd_BlendFunc(RD_ONE,RD_ONE); else rd_BlendFunc(RD_ZERO,RD_SRC_COLOR); }
    rd_Begin(quads ? RD_QUADS : RD_LINES);
    for (const auto& vertex : vertices) { rd_Color4ubv(vertex.color.data()); rd_TexCoord2f(vertex.u,vertex.v); rd_Vertex3fv(&vertex.position.x); }
    rd_End(); return vertices.size()/count;
}
}
