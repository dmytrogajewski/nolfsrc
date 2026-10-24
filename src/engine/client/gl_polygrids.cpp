#include "engine/client/gl_polygrids.h"
#include "rdev.h"
#include <algorithm>
#include <cmath>
namespace nolf {
namespace {
struct Frame {
    const int units=std::min(2,g_RDCaps.nTextureUnits);
    Frame() {
        rd_PushAttrib(RD_ATTR_ALL);
        for (int unit=0;unit<g_RDCaps.nTextureUnits;++unit) { rd_ActiveTexture(unit); rd_Disable(RD_TEXTURE_2D); if (unit<units) { rd_MatrixMode(RD_TEXTURE); rd_PushMatrix(); rd_LoadIdentity(); } }
        rd_ActiveTexture(0); rd_MatrixMode(RD_MODELVIEW);
    }
    ~Frame() { for (int unit=0;unit<units;++unit) { rd_ActiveTexture(unit); rd_MatrixMode(RD_TEXTURE); rd_PopMatrix(); } rd_PopAttrib(); }
};
}
std::size_t GlPolygrids::draw(const PolygridMesh& mesh,const std::shared_ptr<const TextureResource>& texture,const std::shared_ptr<const TextureResource>& environment,ClientTextures::Render& visibility,const PolygridDrawStyle& style) {
    if (owner_!=std::this_thread::get_id() || !rd_DeviceGeneration()) throw std::logic_error("polygrid drawing requires its owning device thread");
    if (mesh.vertices.size()>65000 || mesh.indices.size()%3 || mesh.indices.size()>390000 || static_cast<unsigned>(style.blend)>static_cast<unsigned>(ObjectBlend::Multiply)) throw std::invalid_argument("invalid polygrid draw request");
    for (auto i : mesh.indices) if (i>=mesh.vertices.size()) throw std::invalid_argument("polygrid index exceeds prepared geometry");
    for (const auto& v : mesh.vertices) if (!std::isfinite(v.position.x) || !std::isfinite(v.position.y) || !std::isfinite(v.position.z) || !std::isfinite(v.u) || !std::isfinite(v.v) || !std::isfinite(v.environment_u) || !std::isfinite(v.environment_v)) throw std::invalid_argument("invalid prepared polygrid vertex");
    if (mesh.indices.empty() || (style.environment_only && !environment)) return 0;
    const bool dual=environment && texture && !style.environment_only;
    if ((environment && !g_RDCaps.bCombine) || (dual && g_RDCaps.nTextureUnits<2)) throw std::logic_error("polygrid environment requires texture combine support and two units for dual texturing");
    Frame frame; rd_CullBack(false); rd_Enable(RD_DEPTH_TEST); rd_DepthFunc(RD_LEQUAL); rd_DepthMask(style.depth_write); rd_ColorMask(true);
    if (style.alpha_test) { rd_Enable(RD_ALPHA_TEST); rd_AlphaGreater(.1666f); } else rd_Disable(RD_ALPHA_TEST);
    if (style.blend==ObjectBlend::Opaque) rd_Disable(RD_BLEND);
    else { rd_Enable(RD_BLEND); if (style.blend==ObjectBlend::Alpha) rd_BlendFunc(RD_SRC_ALPHA,RD_ONE_MINUS_SRC_ALPHA); else if (style.blend==ObjectBlend::Additive) rd_BlendFunc(RD_ONE,RD_ONE); else rd_BlendFunc(RD_ZERO,RD_SRC_COLOR); }
    TextureSampling env_sampling; env_sampling.alpha=false; env_sampling.repeat_s=env_sampling.repeat_t=false;
    if (dual) {
        const std::array<TextureBinding,2> bindings{{{0,environment,env_sampling},{1,texture,style.sampling}}}; textures_.bind_units(bindings);
        visibility.drawn(environment); visibility.drawn(texture);
        rd_ActiveTexture(0); rd_Enable(RD_TEXTURE_2D); const RDCombine first{RD_OP_MODULATE,RD_ARG_TEXTURE,RD_ARG_PRIMARY,1,RD_OP_REPLACE,RD_ARG_PRIMARY,RD_ARG_PRIMARY}; rd_TexCombine(first);
        rd_ActiveTexture(1); rd_Enable(RD_TEXTURE_2D);
        const RDCombine second{style.environment_add ? RD_OP_ADD_SIGNED : RD_OP_MODULATE,RD_ARG_TEXTURE,RD_ARG_PREVIOUS,1,RD_OP_MODULATE,RD_ARG_TEXTURE,RD_ARG_PREVIOUS}; rd_TexCombine(second); rd_ActiveTexture(0);
    } else if (environment) {
        textures_.bind_unit(0,environment,env_sampling); visibility.drawn(environment); rd_Enable(RD_TEXTURE_2D);
        const RDCombine combine{RD_OP_MODULATE,RD_ARG_TEXTURE,RD_ARG_PRIMARY,1,RD_OP_REPLACE,RD_ARG_PRIMARY,RD_ARG_PRIMARY}; rd_TexCombine(combine);
    } else if (texture) { textures_.bind_unit(0,texture,style.sampling); visibility.drawn(texture); rd_Enable(RD_TEXTURE_2D); rd_TexEnv(RD_ENV_MODULATE); }
    rd_Begin(RD_TRIANGLES);
    for (auto i : mesh.indices) {
        const auto& v=mesh.vertices[i]; rd_Color4ubv(v.color.data());
        if (environment) { rd_TexCoord2f(v.environment_u,v.environment_v); if (dual) rd_MultiTexCoord2f(1,v.u,v.v); } else rd_TexCoord2f(v.u,v.v);
        rd_Vertex3fv(&v.position.x);
    }
    rd_End(); return mesh.indices.size()/3;
}
}
