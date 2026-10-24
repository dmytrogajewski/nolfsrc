#pragma once
#include "engine/portable/image.h"
#include <optional>
#include <span>
#include <array>
#include <utility>

namespace nolf {
struct PixelRect { int left,top,right,bottom; };
struct WarpPoint { double source_x,source_y,dest_x,dest_y; };
std::array<WarpPoint,4> surface_transform(std::uint32_t width,std::uint32_t height,
    std::optional<std::pair<double,double>> origin,int dest_x,int dest_y,double angle,double scale_x,double scale_y);
class Surface;
struct SurfacePaint {
    std::optional<std::uint32_t> color_key;
    std::optional<std::uint32_t> solid;
    const Surface* mask=nullptr;
};
// Engine-owned CPU image, independent of the GPU/device lifetime. The renderer
// observes revisions; edits invalidate uploaded textures without retaining a
// driver handle in game-facing surface data.
class Surface {
public:
    explicit Surface(Image);
    Surface(std::uint32_t width,std::uint32_t height);
    Surface(const Surface&)=delete;
    Surface& operator=(const Surface&)=delete;
    const Image& image() const { return image_; }
    std::uint64_t revision() const { return revision_; }
    std::uint64_t identity() const { return identity_; }
    float alpha() const { return alpha_; }
    void alpha(float);
    PixelRect rectangle() const;
    void fill(std::optional<PixelRect>,std::uint32_t rgb);
    void pixel(std::uint32_t x,std::uint32_t y,std::uint32_t rgb);
    std::uint32_t pixel(std::uint32_t x,std::uint32_t y) const;
    PixelRect border(std::uint32_t rgb) const; // left/top/right/bottom margins
    void blit(const Surface&,std::optional<PixelRect> source,PixelRect destination,SurfacePaint = {});
    // Straight-alpha image composition for native glyphs and decoded frames.
    // Legacy SDK surface blits retain their existing copy/color-key semantics.
    void composite(const Surface&,int x,int y,std::optional<PixelRect> clip = {});
    void warp(const Surface&,std::span<const WarpPoint>,SurfacePaint = {});
    void transform(const Surface&,std::optional<std::pair<double,double>> origin,int dest_x,int dest_y,
        double angle,double scale_x,double scale_y,SurfacePaint = {});
private:
    Image image_;
    const std::uint64_t identity_;
    std::uint64_t revision_=1;
    float alpha_=1;
    void changed();
};
}
