#include "engine/client/gl_canvas.h"
#include "rdev.h"
#include <cmath>

namespace nolf {
namespace {
struct Frame {
    Frame() {
        rd_PushAttrib(RD_ATTR_ALL);
        for (int unit=1;unit<g_RDCaps.nTextureUnits;++unit) { rd_ActiveTexture(unit); rd_Disable(RD_TEXTURE_2D); }
        rd_ActiveTexture(0); rd_MatrixMode(RD_TEXTURE); rd_PushMatrix(); rd_LoadIdentity(); rd_MatrixMode(RD_MODELVIEW);
        rd_CullBack(false); rd_DepthFunc(RD_LEQUAL); rd_ColorMask(true); rd_Disable(RD_ALPHA_TEST);
    }
    ~Frame() { rd_MatrixMode(RD_TEXTURE); rd_PopMatrix(); rd_PopAttrib(); }
};
RDBlend blend(uint32 value) { static constexpr RDBlend values[]{RD_ZERO,RD_ONE,RD_SRC_COLOR,RD_ONE_MINUS_SRC_COLOR,RD_SRC_ALPHA,RD_ONE_MINUS_SRC_ALPHA}; return values[value-1]; }
RDCombineOp operation(uint32 value) { static constexpr RDCombineOp values[]{RD_OP_REPLACE,RD_OP_REPLACE,RD_OP_MODULATE,RD_OP_ADD,RD_OP_ADD_SIGNED}; return values[value-1]; }
}
std::size_t GlCanvas::draw(std::span<const CanvasPrimitive> commands,ClientTextures::Render& visibility) {
    if (owner_!=std::this_thread::get_id() || !rd_DeviceGeneration() || !g_RDCaps.bCombine) throw std::logic_error("canvas drawing requires its owning device with texture combine support");
    if (commands.size()>65536) throw std::invalid_argument("canvas command budget exceeded");
    std::size_t total=0;
    for (const auto& command : commands) {
        if (command.vertices.size()<3 || command.vertices.size()>100 || command.vertices.size()>262144-total) throw std::invalid_argument("invalid canvas primitive size");
        total+=command.vertices.size();
        if (bool(command.texture)!=bool(command.pixels)) throw std::invalid_argument("canvas texture snapshot is incomplete");
        for (unsigned i=0;i<NUM_LTRSTATES;++i) if (!canvas_state_valid(static_cast<LTRState>(i),command.states[i])) throw std::invalid_argument("invalid canvas draw state");
        for (const auto& v : command.vertices) if (!std::isfinite(v.position.x) || !std::isfinite(v.position.y) || !std::isfinite(v.position.z) || !std::isfinite(v.u) || !std::isfinite(v.v)) throw std::invalid_argument("nonfinite canvas vertex");
    }
    Frame frame; std::size_t triangles=0;
    for (const auto& command : commands) {
        const auto& state=command.states;
        if (state[LTRSTATE_ALPHABLENDENABLE]) rd_Enable(RD_BLEND); else rd_Disable(RD_BLEND);
        if (state[LTRSTATE_ZREADENABLE]) rd_Enable(RD_DEPTH_TEST); else rd_Disable(RD_DEPTH_TEST);
        rd_DepthMask(state[LTRSTATE_ZWRITEENABLE]!=0); rd_BlendFunc(blend(state[LTRSTATE_SRCBLEND]),blend(state[LTRSTATE_DESTBLEND]));
        if (command.texture) {
            TextureSampling sampling; sampling.repeat_s=sampling.repeat_t=state[LTRSTATE_TEXADDR]==LTTEXADDR_WRAP;
            textures_.bind(command.texture,command.pixels,sampling); visibility.drawn(command.texture); rd_Enable(RD_TEXTURE_2D);
            const auto color=state[LTRSTATE_COLOROP],alpha=state[LTRSTATE_ALPHAOP];
            const RDCombine combine{operation(color),color==LTOP_SELECTDIFFUSE ? RD_ARG_PRIMARY : RD_ARG_TEXTURE,RD_ARG_PRIMARY,1,
                operation(alpha),alpha==LTOP_SELECTDIFFUSE ? RD_ARG_PRIMARY : RD_ARG_TEXTURE,RD_ARG_PRIMARY}; rd_TexCombine(combine);
        } else rd_Disable(RD_TEXTURE_2D);
        rd_Begin(RD_TRIANGLE_FAN);
        for (const auto& vertex : command.vertices) { rd_Color4ubv(vertex.color.data()); rd_TexCoord2f(vertex.u,vertex.v); rd_Vertex3fv(&vertex.position.x); }
        rd_End(); triangles+=command.vertices.size()-2;
    }
    return triangles;
}
}
