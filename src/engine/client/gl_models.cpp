#include "engine/client/gl_models.h"
#include "rdev.h"
#include <cmath>

namespace nolf {
namespace {
struct State {
    State() {
        rd_PushAttrib(RD_ATTR_ALL);
        for (int unit=1;unit<g_RDCaps.nTextureUnits;++unit) { rd_ActiveTexture(unit); rd_Disable(RD_TEXTURE_2D); }
        rd_ActiveTexture(0); rd_MatrixMode(RD_TEXTURE); rd_PushMatrix(); rd_LoadIdentity();
    }
    ~State() { rd_MatrixMode(RD_TEXTURE); rd_PopMatrix(); rd_PopAttrib(); }
};
}
GlModels::GlModels(GlTextures& textures) : textures_(textures) {}
std::size_t GlModels::draw(const ModelPose& pose,const ModelPieceData& mesh,
    const std::shared_ptr<const TextureResource>& skin,ClientTextures::Render& render,const ModelDrawStyle& style,std::span<const LTVector> prepared) {
    if (owner_!=std::this_thread::get_id()) throw std::logic_error("model drawing used from another thread");
    if (!rd_DeviceGeneration()) throw std::logic_error("model drawing requires an active device");
    if (!pose.model || mesh.m_pModel!=pose.model.get() || pose.nodes.size()!=pose.model->m_Nodes.GetSize()) throw std::invalid_argument("render mesh does not belong to model pose");
    const auto owned=prepared.empty() ? model_SkinVertices(pose,mesh) : std::vector<LTVector>{};
    const std::span<const LTVector> positions=prepared.empty() ? std::span<const LTVector>(owned) : prepared;
    if (positions.size()!=mesh.vertices().size()) throw std::invalid_argument("prepared model positions do not match mesh");
    for (const auto& point : positions) if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) throw std::invalid_argument("nonfinite prepared model position");
    const auto triangles=mesh.triangles();
    if (!style.vertex_colors.empty() && style.vertex_colors.size()!=positions.size()) throw std::invalid_argument("model lighting does not match vertices");
    if (!style.vertex_uvs.empty() && style.vertex_uvs.size()!=positions.size()) throw std::invalid_argument("model texture coordinates do not match vertices");
    if (!style.normals.empty() && style.normals.size()!=positions.size()) throw std::invalid_argument("model normals do not match vertices");
    if (static_cast<unsigned>(style.blend)>static_cast<unsigned>(ModelBlend::Multiply)) throw std::invalid_argument("invalid model blend");
    for (const auto& triangle : triangles) for (unsigned i=0;i<3;++i)
        if (triangle.indices[i]>=positions.size() || !std::isfinite(triangle.uv[i][0]) || !std::isfinite(triangle.uv[i][1]))
            throw std::invalid_argument("invalid model triangle");
    if (triangles.empty()) return 0;
    State state;
    if (skin) { textures_.bind(skin,style.sampling); render.drawn(skin); rd_Enable(RD_TEXTURE_2D); rd_TexEnv(RD_ENV_MODULATE); }
    else rd_Disable(RD_TEXTURE_2D);
    rd_CullBack(style.cull);
    if (style.depth_test) rd_Enable(RD_DEPTH_TEST); else rd_Disable(RD_DEPTH_TEST);
    rd_DepthFunc(RD_LEQUAL); rd_DepthMask(style.depth_write); rd_ColorMask(true);
    // Chroma-keyed models keep texels more than half opaque, as drawn in
    // the original's chroma-key set.
    if (style.alpha_test) { rd_Enable(RD_ALPHA_TEST); rd_AlphaGreater(.5f); } else rd_Disable(RD_ALPHA_TEST);
    switch (style.blend) {
    case ModelBlend::Opaque: rd_Disable(RD_BLEND); break;
    case ModelBlend::Alpha: rd_Enable(RD_BLEND); rd_BlendFunc(RD_SRC_ALPHA,RD_ONE_MINUS_SRC_ALPHA); break;
    case ModelBlend::Additive: rd_Enable(RD_BLEND); rd_BlendFunc(RD_ONE,RD_ONE); break;
    case ModelBlend::Multiply: rd_Enable(RD_BLEND); rd_BlendFunc(RD_ZERO,RD_SRC_COLOR); break;
    }
    // Everything that can fail or call game code precedes Begin/End.
    rd_Begin(style.wireframe ? RD_LINES : RD_TRIANGLES);
    for (const auto& triangle : triangles) for (unsigned i=0;i<3;++i) {
        const auto emit=[&](unsigned corner) {
            const auto vertex=triangle.indices[corner];
            const auto& color=style.vertex_colors.empty() ? style.color : style.vertex_colors[vertex];
            rd_Color4ub(color[0],color[1],color[2],color[3]);
            if (style.vertex_uvs.empty()) rd_TexCoord2f(triangle.uv[corner][0],triangle.uv[corner][1]);
            else rd_TexCoord2f(style.vertex_uvs[vertex][0],style.vertex_uvs[vertex][1]);
            if (!style.normals.empty()) { const auto& normal=style.normals[vertex]; rd_Normal3f(normal.x,normal.y,normal.z); }
            const auto& position=positions[vertex]; rd_Vertex3f(position.x,position.y,position.z);
        };
        emit(i); if (style.wireframe) emit((i+1)%3);
    }
    rd_End(); return triangles.size();
}
}
