#include "engine/client/surface.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace nolf {
namespace {
std::atomic<std::uint64_t> next_identity{1};
constexpr std::uint32_t rgb_mask=0xffffff;
std::uint32_t opaque(std::uint32_t color) { return 0xff000000u|(color&rgb_mask); }
PixelRect clip(PixelRect rectangle,PixelRect bounds) {
    return {std::max(rectangle.left,bounds.left),std::max(rectangle.top,bounds.top),
        std::min(rectangle.right,bounds.right),std::min(rectangle.bottom,bounds.bottom)};
}
bool empty(PixelRect rectangle) { return rectangle.right<=rectangle.left || rectangle.bottom<=rectangle.top; }
struct Samples {
    std::span<const std::uint32_t> pixels;
    std::vector<std::uint32_t> copy;
    std::uint32_t width,height;
    Samples(const Surface& source,const Surface& destination) : pixels(source.image().pixels()),width(source.image().width()),height(source.image().height()) {
        if (&source==&destination) { copy.assign(pixels.begin(),pixels.end()); pixels=copy; }
    }
    std::uint32_t at(unsigned x,unsigned y) const { return pixels[std::size_t(y)*width+x]; }
};
class Painter {
public:
    Painter(const Surface& source,const Surface& destination,SurfacePaint style) : samples(source,destination),style(style) {
        if (style.mask) {
            const auto w=style.mask->image().width(),h=style.mask->image().height();
            if (w>256 || h>256 || (w&(w-1)) || (h&(h-1))) throw std::invalid_argument("surface mask must have power-of-two dimensions at most 256");
            if (style.solid) throw std::invalid_argument("surface paint has both a mask and solid color");
            mask.emplace(*style.mask,destination);
        }
    }
    std::optional<std::uint32_t> pixel(unsigned x,unsigned y) const {
        auto color=samples.at(x,y);
        if (style.color_key && (color&rgb_mask)==(*style.color_key&rgb_mask)) return std::nullopt;
        if (style.solid) color=opaque(*style.solid);
        if (mask) color=mask->at(x&(mask->width-1),y&(mask->height-1));
        return color;
    }
    Samples samples;
    SurfacePaint style;
    std::optional<Samples> mask;
};
double cross(const WarpPoint& a,const WarpPoint& b,const WarpPoint& c) {
    return (b.dest_x-a.dest_x)*(c.dest_y-a.dest_y)-(b.dest_y-a.dest_y)*(c.dest_x-a.dest_x);
}
}
Surface::Surface(Image image) : image_(std::move(image)),identity_(next_identity.fetch_add(1,std::memory_order_relaxed)) {
    if (!image_.width() || !image_.height()) throw std::invalid_argument("cannot create an empty surface");
    if (!identity_) throw std::overflow_error("surface identity space exhausted");
}
Surface::Surface(std::uint32_t width,std::uint32_t height) : Surface(Image(width,height)) {}
void Surface::changed() {
    if (revision_==std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("surface revision space exhausted");
    ++revision_;
}
PixelRect Surface::rectangle() const { return {0,0,static_cast<int>(image_.width()),static_cast<int>(image_.height())}; }
void Surface::alpha(float alpha) {
    if (!std::isfinite(alpha) || alpha<0 || alpha>1) throw std::invalid_argument("invalid surface alpha");
    alpha_=alpha;
}
void Surface::fill(std::optional<PixelRect> requested,std::uint32_t color) {
    const auto rectangle=clip(requested.value_or(this->rectangle()),this->rectangle());
    if (empty(rectangle)) return;
    changed();
    auto pixels=image_.pixels();
    for (int y=rectangle.top;y<rectangle.bottom;++y)
        std::fill(pixels.begin()+std::size_t(y)*image_.width()+rectangle.left,
            pixels.begin()+std::size_t(y)*image_.width()+rectangle.right,opaque(color));
}
void Surface::pixel(std::uint32_t x,std::uint32_t y,std::uint32_t color) {
    image_.at(x,y); changed(); image_.pixels()[std::size_t(y)*image_.width()+x]=opaque(color);
}
std::uint32_t Surface::pixel(std::uint32_t x,std::uint32_t y) const { return image_.at(x,y)&rgb_mask; }
PixelRect Surface::border(std::uint32_t color) const {
    PixelRect result{static_cast<int>(image_.width()),static_cast<int>(image_.height()),0,0}; int right=0,bottom=0;
    bool found=false;
    for (unsigned y=0;y<image_.height();++y) for (unsigned x=0;x<image_.width();++x) {
        if ((image_.pixels()[std::size_t(y)*image_.width()+x]&rgb_mask)==(color&rgb_mask)) continue;
        found=true; result.left=std::min(result.left,static_cast<int>(x)); result.top=std::min(result.top,static_cast<int>(y));
        right=std::max(right,static_cast<int>(x)+1); bottom=std::max(bottom,static_cast<int>(y)+1);
    }
    if (!found) return {static_cast<int>(image_.width()),static_cast<int>(image_.height()),0,0};
    result.right=static_cast<int>(image_.width())-right; result.bottom=static_cast<int>(image_.height())-bottom;
    return result;
}
void Surface::blit(const Surface& source,std::optional<PixelRect> requested,PixelRect destination,SurfacePaint style) {
    const auto src=requested.value_or(source.rectangle());
    if (empty(src) || empty(destination)) throw std::invalid_argument("invalid surface blit rectangle");
    const auto dst=clip(destination,rectangle()); if (empty(dst)) return;
    Painter painter(source,*this,style);
    const double xscale=(double(src.right)-src.left)/(double(destination.right)-destination.left);
    const double yscale=(double(src.bottom)-src.top)/(double(destination.bottom)-destination.top);
    changed();
    for (int y=dst.top;y<dst.bottom;++y) {
        const auto sy=std::floor(src.top+(double(y)-destination.top+.5)*yscale);
        if (sy<0 || sy>=painter.samples.height) continue;
        for (int x=dst.left;x<dst.right;++x) {
            const auto sx=std::floor(src.left+(double(x)-destination.left+.5)*xscale);
            if (sx<0 || sx>=painter.samples.width) continue;
            if (const auto color=painter.pixel(static_cast<unsigned>(sx),static_cast<unsigned>(sy)))
                image_.pixels()[std::size_t(y)*image_.width()+x]=*color;
        }
    }
}
void Surface::composite(const Surface& source,int x,int y,std::optional<PixelRect> requested) {
    const std::int64_t right=std::int64_t(x)+source.image().width(),bottom=std::int64_t(y)+source.image().height();
    if (right>std::numeric_limits<int>::max() || bottom>std::numeric_limits<int>::max()) throw std::invalid_argument("image destination overflow");
    auto bounds=clip(PixelRect{x,y,static_cast<int>(right),static_cast<int>(bottom)},rectangle());
    if (requested) bounds=clip(bounds,*requested);
    if (empty(bounds)) return;
    Samples samples(source,*this); changed();
    for (int dy=bounds.top;dy<bounds.bottom;++dy) for (int dx=bounds.left;dx<bounds.right;++dx) {
        const auto src=samples.at(static_cast<unsigned>(std::int64_t(dx)-x),static_cast<unsigned>(std::int64_t(dy)-y));
        const unsigned alpha=src>>24; if (!alpha) continue;
        auto& destination=image_.pixels()[std::size_t(dy)*image_.width()+dx];
        if (alpha==255) { destination=src; continue; }
        const unsigned old_alpha=destination>>24,coverage=alpha*255+old_alpha*(255-alpha);
        std::uint32_t output=((coverage+127)/255)<<24;
        for (unsigned shift : {0u,8u,16u}) {
            const unsigned component=((src>>shift)&255)*alpha*255+((destination>>shift)&255)*old_alpha*(255-alpha);
            output|=((component+coverage/2)/coverage)<<shift;
        }
        destination=output;
    }
}
void Surface::warp(const Surface& source,std::span<const WarpPoint> points,SurfacePaint style) {
    if (points.size()<3 || points.size()>10) throw std::invalid_argument("surface warp needs 3..10 vertices");
    double minimum_x=image_.width(),minimum_y=image_.height(),maximum_x=0,maximum_y=0;
    double direction=0;
    for (std::size_t i=0;i<points.size();++i) {
        const auto& point=points[i];
        if (!std::isfinite(point.source_x) || !std::isfinite(point.source_y) || !std::isfinite(point.dest_x) || !std::isfinite(point.dest_y) ||
            std::abs(point.dest_x)>1e12 || std::abs(point.dest_y)>1e12 || std::abs(point.source_x)>1e12 || std::abs(point.source_y)>1e12) throw std::invalid_argument("invalid surface warp coordinate");
        const auto turn=cross(point,points[(i+1)%points.size()],points[(i+2)%points.size()]);
        if (turn) { if (direction && std::signbit(direction)!=std::signbit(turn)) throw std::invalid_argument("surface warp polygon is not convex"); direction=turn; }
        minimum_x=std::min(minimum_x,point.dest_x); minimum_y=std::min(minimum_y,point.dest_y);
        maximum_x=std::max(maximum_x,point.dest_x); maximum_y=std::max(maximum_y,point.dest_y);
    }
    if (!direction) throw std::invalid_argument("surface warp polygon has no area");
    for (std::size_t i=0;i<points.size();++i) for (const auto& point : points) {
        const auto side=cross(points[i],points[(i+1)%points.size()],point);
        if (side && std::signbit(side)!=std::signbit(direction)) throw std::invalid_argument("surface warp polygon is not convex");
    }
    const PixelRect bounds{static_cast<int>(std::clamp(std::floor(minimum_x),0.0,double(image_.width()))),
        static_cast<int>(std::clamp(std::floor(minimum_y),0.0,double(image_.height()))),
        static_cast<int>(std::clamp(std::ceil(maximum_x),0.0,double(image_.width()))),
        static_cast<int>(std::clamp(std::ceil(maximum_y),0.0,double(image_.height())))};
    if (empty(bounds)) return;
    std::array<WarpPoint,10> clamped{};
    std::copy(points.begin(),points.end(),clamped.begin());
    for (auto& point : clamped) {
        point.source_x=std::clamp(point.source_x,0.0,double(source.image().width()-1));
        point.source_y=std::clamp(point.source_y,0.0,double(source.image().height()-1));
    }
    points=std::span(clamped).first(points.size());
    Painter painter(source,*this,style); changed();
    for (std::size_t triangle=1;triangle+1<points.size();++triangle) {
        const auto& a=points[0]; const auto& b=points[triangle]; const auto& c=points[triangle+1];
        const auto area=cross(a,b,c); if (!area) continue;
        for (int y=bounds.top;y<bounds.bottom;++y) for (int x=bounds.left;x<bounds.right;++x) {
            const WarpPoint pixel{0,0,x+.5,y+.5};
            const double u=cross(pixel,b,c)/area,v=cross(a,pixel,c)/area,w=1-u-v;
            if (!std::isfinite(u) || !std::isfinite(v) || !std::isfinite(w) || u<-1e-12 || v<-1e-12 || w<-1e-12) continue;
            const auto sx=std::clamp(u*a.source_x+v*b.source_x+w*c.source_x,0.0,double(painter.samples.width-1));
            const auto sy=std::clamp(u*a.source_y+v*b.source_y+w*c.source_y,0.0,double(painter.samples.height-1));
            if (const auto color=painter.pixel(static_cast<unsigned>(std::floor(sx+.5)),static_cast<unsigned>(std::floor(sy+.5))))
                image_.pixels()[std::size_t(y)*image_.width()+x]=*color;
        }
    }
}
std::array<WarpPoint,4> surface_transform(std::uint32_t source_width,std::uint32_t source_height,
    std::optional<std::pair<double,double>> origin,int dest_x,int dest_y,double angle,double scale_x,double scale_y) {
    if (!source_width || !source_height) throw std::invalid_argument("empty surface transform");
    if (!std::isfinite(angle) || !std::isfinite(scale_x) || !std::isfinite(scale_y) || !scale_x || !scale_y)
        throw std::invalid_argument("invalid surface transform");
    const double width=source_width,height=source_height;
    const auto center=origin.value_or(std::pair{dest_x+width*.5,dest_y+height*.5});
    if (!std::isfinite(center.first) || !std::isfinite(center.second)) throw std::invalid_argument("invalid surface transform origin");
    std::array<WarpPoint,4> points{{{0,0,double(dest_x),double(dest_y)}, {width-1,0,dest_x+width,double(dest_y)},
        {width-1,height-1,dest_x+width,dest_y+height}, {0,height-1,double(dest_x),dest_y+height}}};
    const double cosine=std::cos(angle),sine=std::sin(angle);
    for (auto& point : points) {
        const auto x=(point.dest_x-center.first)*scale_x,y=(point.dest_y-center.second)*scale_y;
        point.dest_x=center.first+cosine*x-sine*y; point.dest_y=center.second+sine*x+cosine*y;
    }
    return points;
}
void Surface::transform(const Surface& source,std::optional<std::pair<double,double>> origin,int dest_x,int dest_y,
    double angle,double scale_x,double scale_y,SurfacePaint style) {
    warp(source,surface_transform(source.image().width(),source.image().height(),origin,dest_x,dest_y,angle,scale_x,scale_y),style);
}
}
