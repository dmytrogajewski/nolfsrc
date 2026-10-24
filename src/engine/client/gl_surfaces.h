#pragma once
#include "engine/client/surface_display.h"
#include <memory>

namespace nolf {
// Uses the renderer device contract, never an OS/context handle. CPU surfaces
// survive device restarts. Uploads and destruction run on the render thread;
// the device owns the GL allocations until they are released or it shuts down.
class GlSurfaces final : public SurfaceDisplay {
public:
    GlSurfaces(std::uint32_t width,std::uint32_t height,std::size_t texture_budget=256*1024*1024);
    ~GlSurfaces() override;
    GlSurfaces(const GlSurfaces&)=delete;
    GlSurfaces& operator=(const GlSurfaces&)=delete;
    void resize(std::uint32_t width,std::uint32_t height) override;
    void draw(const std::shared_ptr<const Surface>&,std::optional<PixelRect> source,PixelRect destination,SurfaceDraw = {}) override;
    void warp(const std::shared_ptr<const Surface>&,std::span<const WarpPoint>,SurfaceDraw = {}) override;
    void clear(std::optional<PixelRect>,std::uint32_t rgb) override;
    Image read(PixelRect) override;
    void collect();
    std::size_t cached_textures() const;
private:
    void polygon(const std::shared_ptr<const Surface>&,std::span<const WarpPoint>,SurfaceDraw,bool clamp_source);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
