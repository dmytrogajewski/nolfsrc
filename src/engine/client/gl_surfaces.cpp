#include "engine/client/gl_surfaces.h"
#include "rdev.h"
#include "platform/logging.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace nolf {
namespace {
static_assert(std::endian::native==std::endian::little,"BGRA device pixels require little-endian words");
constexpr std::uint32_t rgb_mask=0xffffff;
void dimensions(std::uint32_t width,std::uint32_t height) {
    if (!width || !height || width>Image::maximum_dimension || height>Image::maximum_dimension) throw std::invalid_argument("invalid surface viewport");
}
struct State {
    State(unsigned width,unsigned height) {
        rd_PushAttrib(RD_ATTR_ALL);
        for (int unit=1;unit<g_RDCaps.nTextureUnits;++unit) { rd_ActiveTexture(unit); rd_Disable(RD_TEXTURE_2D); }
        rd_ActiveTexture(0);
        rd_MatrixMode(RD_TEXTURE); rd_PushMatrix(); rd_LoadIdentity();
        rd_MatrixMode(RD_PROJECTION); rd_PushMatrix(); rd_LoadIdentity(); rd_Ortho(0,width,height,0,-1,1);
        rd_MatrixMode(RD_MODELVIEW); rd_PushMatrix(); rd_LoadIdentity();
        rd_Viewport(0,0,width,height); rd_CullBack(false); rd_Disable(RD_DEPTH_TEST); rd_DepthMask(false);
        rd_Disable(RD_FOG); rd_Disable(RD_SCISSOR_TEST); rd_ColorMask(true);
        rd_Enable(RD_TEXTURE_2D); rd_Enable(RD_ALPHA_TEST); rd_AlphaGreater(0);
        rd_Enable(RD_BLEND); rd_TexEnv(RD_ENV_MODULATE);
    }
    ~State() {
        rd_MatrixMode(RD_MODELVIEW); rd_PopMatrix();
        rd_MatrixMode(RD_PROJECTION); rd_PopMatrix();
        rd_MatrixMode(RD_TEXTURE); rd_PopMatrix();
        rd_PopAttrib();
    }
};
void blend(SurfaceBlend mode) {
    constexpr RDBlend factors[][2]={{RD_SRC_ALPHA,RD_ONE_MINUS_SRC_ALPHA},{RD_ONE,RD_ZERO},{RD_ONE,RD_ONE},
        {RD_DST_COLOR,RD_ZERO},{RD_DST_COLOR,RD_SRC_COLOR},{RD_ZERO,RD_ONE_MINUS_SRC_COLOR},{RD_ONE,RD_ONE_MINUS_SRC_COLOR}};
    const auto index=static_cast<unsigned>(mode);
    if (index>=std::size(factors)) throw std::invalid_argument("invalid surface blend");
    rd_BlendFunc(factors[index][0],factors[index][1]);
}
std::uint8_t channel(std::uint32_t value,unsigned shift) { return static_cast<std::uint8_t>((value>>shift)&255); }
}
struct GlSurfaces::Impl {
    struct Texture {
        std::weak_ptr<const Surface> source,mask;
        std::uint64_t revision=0,mask_id=0,mask_revision=0,last_used=0;
        std::optional<std::uint32_t> key;
        std::uint32_t name=0;
        bool solid=false;
        std::size_t bytes=0;
    };
    std::unordered_map<std::uint64_t,Texture> textures;
    std::uint32_t width,height;
    std::size_t budget,bytes=0;
    std::uint64_t generation=0,clock=0;
    const std::thread::id owner=std::this_thread::get_id();
    Impl(unsigned width,unsigned height,std::size_t budget) : width(width),height(height),budget(budget) {}
    void require_owner() const { if (owner!=std::this_thread::get_id()) throw std::logic_error("surface renderer used from another thread"); }
    void context() {
        require_owner();
        const auto current=rd_DeviceGeneration();
        if (current!=generation) { textures.clear(); bytes=0; generation=current; }
        if (!current) throw std::logic_error("surface rendering without an active device");
    }
    void erase(decltype(textures)::iterator entry) {
        if (generation==rd_DeviceGeneration()) rd_DeleteTexture(entry->second.name);
        bytes-=entry->second.bytes; textures.erase(entry);
    }
    void release() {
        if (owner==std::this_thread::get_id() && generation && generation==rd_DeviceGeneration())
            for (const auto& [id,texture] : textures) rd_DeleteTexture(texture.name);
        textures.clear(); bytes=0;
    }
    void collect() {
        context();
        for (auto entry=textures.begin();entry!=textures.end();) {
            if (entry->second.source.expired() || (entry->second.mask_id && entry->second.mask.expired())) { auto stale=entry++; erase(stale); }
            else ++entry;
        }
    }
    std::uint32_t upload(const std::shared_ptr<const Surface>& surface,const SurfaceDraw& style) {
        if (style.mask) {
            const auto w=style.mask->image().width(),h=style.mask->image().height();
            if (w>256 || h>256 || (w&(w-1)) || (h&(h-1)) || style.solid) throw std::invalid_argument("invalid surface mask");
        }
        const auto& image=surface->image();
        if (image.width()>static_cast<unsigned>(g_RDCaps.nMaxTextureSize) || image.height()>static_cast<unsigned>(g_RDCaps.nMaxTextureSize))
            throw std::length_error("surface exceeds device texture dimensions");
        const auto size=image.pixels().size_bytes();
        if (size>budget) throw std::length_error("surface exceeds texture budget");
        auto key=style.color_key;
        if (key) *key&=rgb_mask;
        auto found=textures.find(surface->identity());
        const auto old_size=found==textures.end() ? 0 : found->second.bytes;
        while (bytes-old_size>budget-size) {
            auto oldest=textures.end();
            for (auto entry=textures.begin();entry!=textures.end();++entry) {
                if (entry->first==surface->identity()) continue;
                if (oldest==textures.end() || entry->second.last_used<oldest->second.last_used) oldest=entry;
            }
            if (oldest==textures.end()) throw std::length_error("surface texture budget exhausted");
            erase(oldest);
        }
        auto [position,inserted]=textures.try_emplace(surface->identity());
        struct Rollback {
            Impl& owner; decltype(textures)::iterator position; bool active;
            ~Rollback() { if (active) { rd_DeleteTexture(position->second.name); owner.textures.erase(position); } }
        } rollback{*this,position,inserted};
        auto& texture=position->second;
        const auto mask_id=style.mask ? style.mask->identity() : 0,mask_revision=style.mask ? style.mask->revision() : 0;
        if (texture.name && texture.revision==surface->revision() && texture.key==key && texture.mask_id==mask_id &&
            texture.mask_revision==mask_revision && texture.solid==bool(style.solid)) {
            texture.last_used=++clock; rollback.active=false; return texture.name;
        }
        std::vector<std::uint32_t> pixels;
        auto source=image.pixels();
        if (key || style.mask || style.solid) {
            pixels.assign(source.begin(),source.end());
            for (unsigned y=0;y<image.height();++y) for (unsigned x=0;x<image.width();++x) {
                auto& pixel=pixels[std::size_t(y)*image.width()+x];
                if (key && (pixel&rgb_mask)==*key) { pixel=0; continue; }
                if (style.mask) {
                    const auto& mask=style.mask->image();
                    const auto color=mask.at(x&(mask.width()-1),y&(mask.height()-1));
                    pixel=(std::uint32_t(channel(pixel,24))*channel(color,24)/255<<24)|(color&rgb_mask);
                } else if (style.solid) pixel=(pixel&0xff000000u)|rgb_mask;
            }
            source=pixels;
        }
        if (!texture.name) texture.name=rd_GenTexture();
        rd_BindTexture(texture.name); rd_TexWrap(false,false); rd_TexMaxLevel(0);
        rd_TexImage(0,RD_FMT_RGBA8,image.width(),image.height(),RD_PIX_BGRA,source.data());
        bytes=bytes-texture.bytes+size;
        texture.bytes=size; texture.source=surface; texture.mask=style.mask;
        texture.revision=surface->revision(); texture.mask_id=mask_id; texture.mask_revision=mask_revision;
        texture.key=key; texture.solid=bool(style.solid); texture.last_used=++clock;
        rollback.active=false;
        return texture.name;
    }
};
GlSurfaces::GlSurfaces(unsigned width,unsigned height,std::size_t budget) : impl_(std::make_unique<Impl>(width,height,budget)) {
    dimensions(width,height); if (!budget) throw std::invalid_argument("empty surface texture budget");
}
GlSurfaces::~GlSurfaces() { impl_->release(); }
void GlSurfaces::resize(unsigned width,unsigned height) { impl_->require_owner(); dimensions(width,height); impl_->width=width; impl_->height=height; }
void GlSurfaces::collect() { impl_->collect(); }
std::size_t GlSurfaces::cached_textures() const { impl_->require_owner(); return impl_->textures.size(); }
void GlSurfaces::draw(const std::shared_ptr<const Surface>& source,std::optional<PixelRect> requested,PixelRect destination,SurfaceDraw style) {
    if (!source) throw std::invalid_argument("missing draw surface");
    const auto src=requested.value_or(source->rectangle());
    if (src.right<=src.left || src.bottom<=src.top || destination.right<=destination.left || destination.bottom<=destination.top)
        throw std::invalid_argument("invalid surface draw rectangle");
    const double left=std::max(src.left,0),top=std::max(src.top,0);
    const double right=std::min(src.right,static_cast<int>(source->image().width())),bottom=std::min(src.bottom,static_cast<int>(source->image().height()));
    impl_->context();
    if (right<=left || bottom<=top) return;
    const double xscale=(double(destination.right)-destination.left)/(double(src.right)-src.left);
    const double yscale=(double(destination.bottom)-destination.top)/(double(src.bottom)-src.top);
    const double x0=destination.left+(left-src.left)*xscale,y0=destination.top+(top-src.top)*yscale;
    const double x1=destination.left+(right-src.left)*xscale,y1=destination.top+(bottom-src.top)*yscale;
    // Draw rectangles use texel edges. Warp coordinates use texel centers.
    const std::array<WarpPoint,4> points{{{left-.5,top-.5,x0,y0},{right-.5,top-.5,x1,y0},
        {right-.5,bottom-.5,x1,y1},{left-.5,bottom-.5,x0,y1}}};
    polygon(source,points,std::move(style),false);
}

void GlSurfaces::warp(const std::shared_ptr<const Surface>& source,std::span<const WarpPoint> points,SurfaceDraw style) {
    polygon(source,points,std::move(style),true);
}
void GlSurfaces::polygon(const std::shared_ptr<const Surface>& source,std::span<const WarpPoint> points,SurfaceDraw style,bool clamp_source) {
    if (!source || points.size()<3 || points.size()>10) throw std::invalid_argument("invalid surface draw polygon");
    double winding=0;
    for (std::size_t i=0;i<points.size();++i) {
        const auto& a=points[i]; const auto& b=points[(i+1)%points.size()];
        if (!std::isfinite(a.source_x) || !std::isfinite(a.source_y) || !std::isfinite(a.dest_x) || !std::isfinite(a.dest_y) ||
            std::abs(a.source_x)>1e12 || std::abs(a.source_y)>1e12 || std::abs(a.dest_x)>1e12 || std::abs(a.dest_y)>1e12)
            throw std::invalid_argument("invalid surface draw coordinate");
        for (const auto& c : points) {
            const auto side=(b.dest_x-a.dest_x)*(c.dest_y-a.dest_y)-(b.dest_y-a.dest_y)*(c.dest_x-a.dest_x);
            if (side) { if (winding && std::signbit(side)!=std::signbit(winding)) throw std::invalid_argument("surface draw polygon is not convex"); winding=side; }
        }
    }
    if (!winding) throw std::invalid_argument("surface draw polygon has no area");
    impl_->context(); State state(impl_->width,impl_->height);
    blend(style.blend);
    const auto texture=impl_->upload(source,style);
    rd_BindTexture(texture); rd_TexFilter(style.linear ? RD_LINEAR : RD_NEAREST,style.linear ? RD_LINEAR : RD_NEAREST);
    const auto solid=style.solid.value_or(0xffffff);
    rd_Color4f(channel(style.color,16)/255.0f*(channel(solid,16)/255.0f),channel(style.color,8)/255.0f*(channel(solid,8)/255.0f),
        channel(style.color,0)/255.0f*(channel(solid,0)/255.0f),source->alpha());
    rd_Begin(RD_TRIANGLE_FAN);
    for (const auto& point : points) {
        const auto source_x=clamp_source ? std::clamp(point.source_x,0.0,double(source->image().width()-1)) : point.source_x;
        const auto source_y=clamp_source ? std::clamp(point.source_y,0.0,double(source->image().height()-1)) : point.source_y;
        rd_TexCoord2f(static_cast<float>((source_x+.5)/source->image().width()),static_cast<float>((source_y+.5)/source->image().height()));
        rd_Vertex2f(static_cast<float>(point.dest_x),static_cast<float>(point.dest_y));
    }
    rd_End();
}
void GlSurfaces::clear(std::optional<PixelRect> requested,std::uint32_t color) {
    impl_->context();
    const auto rectangle=requested.value_or(PixelRect{0,0,static_cast<int>(impl_->width),static_cast<int>(impl_->height)});
    const auto left=std::clamp(rectangle.left,0,static_cast<int>(impl_->width)),top=std::clamp(rectangle.top,0,static_cast<int>(impl_->height));
    const auto right=std::clamp(rectangle.right,0,static_cast<int>(impl_->width)),bottom=std::clamp(rectangle.bottom,0,static_cast<int>(impl_->height));
    if (right<=left || bottom<=top) return;
    rd_PushAttrib(RD_ATTR_COLOR|RD_ATTR_SCISSOR);
    rd_ColorMask(true); rd_Enable(RD_SCISSOR_TEST); rd_Scissor(left,top,right-left,bottom-top);
    rd_ClearColor(channel(color,16)/255.0f,channel(color,8)/255.0f,channel(color,0)/255.0f,1); rd_Clear(RD_CLEAR_COLOR);
    rd_PopAttrib();
}
Image GlSurfaces::read(PixelRect rectangle) {
    impl_->context();
    if (rectangle.left<0 || rectangle.top<0 || rectangle.right>static_cast<int>(impl_->width) || rectangle.bottom>static_cast<int>(impl_->height) ||
        rectangle.right<=rectangle.left || rectangle.bottom<=rectangle.top) throw std::invalid_argument("invalid surface readback extent");
    Image image(rectangle.right-rectangle.left,rectangle.bottom-rectangle.top);
    rd_ReadPixels(rectangle.left,rectangle.top,image.width(),image.height(),RD_PIX_BGRA,image.pixels().data()); return image;
}
}
