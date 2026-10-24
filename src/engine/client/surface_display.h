#pragma once
#include "engine/client/surface.h"
#include <memory>

namespace nolf {
enum class SurfaceBlend { Alpha,Solid,Add,Multiply,Multiply2,Mask,MaskAdd };
struct SurfaceDraw {
    std::optional<std::uint32_t> color_key,solid;
    std::shared_ptr<const Surface> mask;
    SurfaceBlend blend=SurfaceBlend::Alpha;
    std::uint32_t color=0xffffff;
    bool linear=false;
};
// The client owns this display. Surface SDK calls use this contract instead of
// reaching into a window, graphics context, or renderer implementation.
class SurfaceDisplay {
public:
    virtual ~SurfaceDisplay()=default;
    virtual void resize(std::uint32_t width,std::uint32_t height)=0;
    virtual void draw(const std::shared_ptr<const Surface>&,std::optional<PixelRect>,PixelRect,SurfaceDraw = {})=0;
    virtual void warp(const std::shared_ptr<const Surface>&,std::span<const WarpPoint>,SurfaceDraw = {})=0;
    virtual void clear(std::optional<PixelRect>,std::uint32_t rgb)=0;
    virtual Image read(PixelRect)=0;
};
}
