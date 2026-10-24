#pragma once
#include "engine/client/client_textures.h"
#include "iltcustomdraw.h"
#include <functional>
#include <span>

namespace nolf {
bool canvas_state_valid(LTRState,uint32);
struct CanvasVertex {
    LTVector position{0,0,0};
    std::array<uint8,4> color{255,255,255,255};
    float u=0,v=0;
};
struct CanvasPrimitive {
    std::vector<CanvasVertex> vertices;
    std::array<uint32,NUM_LTRSTATES> states;
    std::shared_ptr<const TextureResource> texture;
    std::shared_ptr<const TexturePixels> pixels;
};
// The game's custom drawing callback records an owned command list. GPU
// submission happens after the callback and its object lifetime checks.
class CanvasDraw final : public ILTCustomDraw {
public:
    using TextureLoader=std::function<std::shared_ptr<const TextureResource>(std::string_view)>;
    explicit CanvasDraw(TextureLoader);
    LTRESULT DrawPrimitive(LTVertex*,uint32,uint32) override;
    LTRESULT SetState(LTRState,uint32) override;
    LTRESULT GetState(LTRState,uint32&) override;
    LTRESULT SetTexture(const char*) override;
    LTRESULT GetTexelSize(float&,float&) override;
    std::span<const CanvasPrimitive> commands() const;
private:
    void require_owner() const;
    TextureLoader load_;
    std::array<uint32,NUM_LTRSTATES> states_{0,1,1,LTBLEND_SRCALPHA,LTBLEND_INVSRCALPHA,LTTEXADDR_WRAP,LTOP_MODULATE,LTOP_SELECTDIFFUSE};
    std::shared_ptr<const TextureResource> texture_;
    std::vector<CanvasPrimitive> commands_;
    std::size_t vertices_=0;
    const std::thread::id owner_=std::this_thread::get_id();
};
}
