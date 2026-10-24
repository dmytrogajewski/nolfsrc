#include "engine/client/canvas_draw.h"
#include <cmath>

namespace nolf {
namespace {
template<class F> LTRESULT operation(F&& function) noexcept {
    try { return function(); } catch (const std::bad_alloc&) { return LT_OUTOFMEMORY; }
    catch (const std::invalid_argument&) { return LT_INVALIDPARAMS; } catch (...) { return LT_ERROR; }
}
}
bool canvas_state_valid(LTRState state,uint32 value) {
    const auto index=static_cast<unsigned>(state); if (index>=NUM_LTRSTATES) return false;
    const auto maximum=index<=LTRSTATE_ZWRITEENABLE ? 1u : index<=LTRSTATE_DESTBLEND ? 6u : index==LTRSTATE_TEXADDR ? 2u : 5u;
    return value<=maximum && (index<=LTRSTATE_ZWRITEENABLE || value);
}
CanvasDraw::CanvasDraw(TextureLoader load) : load_(std::move(load)) { if (!load_) throw std::invalid_argument("canvas needs a texture loader"); }
void CanvasDraw::require_owner() const { if (owner_!=std::this_thread::get_id()) throw std::logic_error("canvas used from another thread"); }
LTRESULT CanvasDraw::DrawPrimitive(LTVertex* source,uint32 count,uint32) {
    return operation([&]() -> LTRESULT {
        require_owner(); if (!source || count<3 || count>100) return LT_INVALIDPARAMS;
        if (count>262144-vertices_ || commands_.size()>=65536) return LT_OUTOFMEMORY;
        CanvasPrimitive primitive; primitive.states=states_; primitive.texture=texture_; primitive.vertices.reserve(count);
        if (texture_) primitive.pixels=texture_->snapshot();
        for (uint32 i=0;i<count;++i) {
            const auto& v=source[i];
            if (!std::isfinite(v.m_Vec.x) || !std::isfinite(v.m_Vec.y) || !std::isfinite(v.m_Vec.z) || !std::isfinite(v.m_TU) || !std::isfinite(v.m_TV)) return LT_INVALIDPARAMS;
            primitive.vertices.push_back({v.m_Vec,{v.m_Color.r,v.m_Color.g,v.m_Color.b,v.m_Color.a},v.m_TU,v.m_TV});
        }
        commands_.push_back(std::move(primitive)); vertices_+=count; return LT_OK;
    });
}
LTRESULT CanvasDraw::SetState(LTRState state,uint32 value) {
    return operation([&]() -> LTRESULT {
        require_owner(); if (!canvas_state_valid(state,value)) return LT_INVALIDPARAMS;
        states_[static_cast<unsigned>(state)]=value; return LT_OK;
    });
}
LTRESULT CanvasDraw::GetState(LTRState state,uint32& value) {
    value=0; return operation([&]() -> LTRESULT { require_owner(); const auto index=static_cast<unsigned>(state); if (index>=states_.size()) return LT_INVALIDPARAMS; value=states_[index]; return LT_OK; });
}
LTRESULT CanvasDraw::SetTexture(const char* name) {
    return operation([&]() -> LTRESULT {
        require_owner(); if (!name) { texture_.reset(); return LT_OK; }
        const auto texture=load_(name); if (!texture) return LT_NOTFOUND; texture_=texture; return LT_OK;
    });
}
LTRESULT CanvasDraw::GetTexelSize(float& u,float& v) {
    u=v=0; return operation([&]() -> LTRESULT { require_owner(); if (texture_) { const auto image=texture_->snapshot(); u=1.f/image->texture.header.width; v=1.f/image->texture.header.height; } return LT_OK; });
}
std::span<const CanvasPrimitive> CanvasDraw::commands() const { require_owner(); return commands_; }
}
