#include "engine/client/surface_api.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace nolf {
namespace {
class NoSurfaceScope : public std::logic_error { public: using std::logic_error::logic_error; };
std::optional<PixelRect> rectangle(const LTRect* rect) {
    if (!rect) return std::nullopt;
    return PixelRect{rect->left,rect->top,rect->right,rect->bottom};
}
HLTCOLOR color(float r,float g,float b,LTBOOL transparent) {
    auto channel=[](float v) { return std::isfinite(v) ? static_cast<unsigned>(std::clamp(v,0.f,1.f)*255.f) : 0u; };
    return SETRGB(channel(r),channel(g),channel(b)) | (transparent ? COLOR_TRANSPARENCY_MASK : 0);
}
template<class F> LTRESULT result(F&& function) noexcept {
    try { function(); return LT_OK; }
    catch (const NoSurfaceScope&) { return LT_NOTINITIALIZED; }
    catch (const std::bad_alloc&) { return LT_OUTOFMEMORY; }
    catch (const std::invalid_argument&) { return LT_INVALIDPARAMS; }
    catch (const std::out_of_range&) { return LT_INVALIDPARAMS; }
    catch (...) { return LT_ERROR; }
}
template<class T,class F> T value(F&& function,T fallback={}) noexcept {
    try { return function(); } catch (...) { return fallback; }
}
}
struct ClientSurfaces::State {
    struct Record {
        std::shared_ptr<Surface> surface;
        void* user=nullptr;
    };
    static thread_local std::shared_ptr<State> current;
    std::shared_ptr<const Assets> assets;
    SurfaceDisplay& display;
    const std::thread::id owner=std::this_thread::get_id();
    std::unordered_map<HSURFACE,Record> records;
    HSURFACE screen=nullptr;
    std::size_t bytes=0,budget,live=0;
    unsigned optimized=0;
    SurfaceBlend blend=SurfaceBlend::Alpha;
    HLTCOLOR modulation=0xffffff;

    State(std::shared_ptr<const Assets> files,unsigned width,unsigned height,SurfaceDisplay& target,std::size_t limit)
        : assets(std::move(files)),display(target),budget(limit) { screen=add(create(width,height)); }
    void require_owner() const {
        if (owner!=std::this_thread::get_id()) throw std::logic_error("client surfaces used from another thread");
    }
    static State& active() {
        if (!current) throw NoSurfaceScope("surface SDK call outside a client callback scope");
        current->require_owner(); return *current;
    }
    Record& get(HSURFACE handle) {
        const auto it=records.find(handle);
        if (it==records.end()) throw std::invalid_argument("invalid surface handle");
        return it->second;
    }
    HSURFACE add(std::shared_ptr<Surface> surface) {
        const auto size=surface->image().pixels().size_bytes();
        if (size>budget || bytes>budget-size || records.size()>=65536) throw std::length_error("client surface budget exceeded");
        // SDK handles are opaque tokens, never addresses the game may read.
        // Surface identities are process-unique, including across services;
        // retiring a record cannot make a stale handle refer to new pixels.
        static_assert(sizeof(std::uintptr_t)>=sizeof(std::uint64_t));
        const auto handle=reinterpret_cast<HSURFACE>(static_cast<std::uintptr_t>(surface->identity()));
        records.emplace(handle,Record{std::move(surface),nullptr}); bytes+=size; ++live; return handle;
    }
    std::shared_ptr<Surface> create(unsigned width,unsigned height) {
        const auto size=std::uint64_t(width)*height*4;
        if (!width || !height || width>Image::maximum_dimension || height>Image::maximum_dimension ||
            size>budget || bytes>budget-size) throw std::invalid_argument("surface dimensions exceed client budget");
        return std::make_shared<Surface>(width,height);
    }
    std::shared_ptr<Surface> read(HSURFACE handle) {
        auto surface=get(handle).surface;
        if (handle==screen) return std::make_shared<Surface>(display.read(surface->rectangle()));
        return surface;
    }
    void erase(HSURFACE handle) {
        auto& record=get(handle);
        if (handle==screen) throw std::invalid_argument("cannot delete the screen surface");
        bytes-=record.surface->image().pixels().size_bytes(); --live;
        records.erase(handle);
    }
    SurfacePaint paint(const SurfaceDraw& style) { return {style.color_key,style.solid,style.mask.get()}; }
    SurfaceDraw style(std::optional<HLTCOLOR> key={},std::optional<HLTCOLOR> solid={},HSURFACE mask=nullptr) {
        SurfaceDraw draw; draw.color_key=key; draw.solid=solid;
        if (mask) draw.mask=read(mask);
        if (optimized) { draw.blend=blend; draw.color=modulation&0xffffff; }
        return draw;
    }
    void draw(HSURFACE destination,HSURFACE source,const LTRect* source_rect,const LTRect* dest_rect,
        int x,int y,SurfaceDraw style) {
        auto& target=get(destination);
        auto src=read(source);
        const auto from=rectangle(source_rect);
        PixelRect to;
        if (dest_rect) to=*rectangle(dest_rect);
        else if (x==0 && y==0 && source_rect==nullptr && dest_rect==nullptr) {
            // A plain blit uses the source dimensions, even if the screen or
            // target is larger. Scaling passes an explicit target rectangle.
            to=src->rectangle();
        } else {
            const auto extents=from.value_or(src->rectangle());
            const auto right=std::int64_t(x)+std::int64_t(extents.right)-extents.left;
            const auto bottom=std::int64_t(y)+std::int64_t(extents.bottom)-extents.top;
            if (right<std::numeric_limits<int>::min() || right>std::numeric_limits<int>::max() ||
                bottom<std::numeric_limits<int>::min() || bottom>std::numeric_limits<int>::max())
                throw std::invalid_argument("surface destination overflow");
            to={x,y,static_cast<int>(right),static_cast<int>(bottom)};
        }
        if (destination==screen) display.draw(src,from,to,std::move(style));
        else target.surface->blit(*src,from,to,paint(style));
    }
    void scale(HSURFACE dest,HSURFACE src,const LTRect* to,const LTRect* from,SurfaceDraw style) {
        const auto full=get(dest).surface->rectangle();
        const LTRect rect{full.left,full.top,full.right,full.bottom};
        draw(dest,src,from,to ? to : &rect,0,0,std::move(style));
    }
    void warp(HSURFACE dest,HSURFACE source,LTWarpPt* points,int count,SurfaceDraw style) {
        if (!points || count<3 || count>10) throw std::invalid_argument("invalid SDK surface warp");
        std::array<WarpPoint,10> vertices;
        for (int i=0;i<count;++i) vertices[i]={points[i].source_x,points[i].source_y,points[i].dest_x,points[i].dest_y};
        const auto span=std::span(vertices).first(count);
        auto& target=get(dest); auto src=read(source);
        if (dest==screen) display.warp(src,span,std::move(style));
        else target.surface->warp(*src,span,paint(style));
    }
    void transform(HSURFACE dest,HSURFACE source,const LTFloatPt* origin,int x,int y,float angle,float sx,float sy,SurfaceDraw style) {
        auto& target=get(dest); const auto src=read(source);
        std::optional<std::pair<double,double>> point;
        if (origin) point={{origin->x,origin->y}};
        const auto points=surface_transform(src->image().width(),src->image().height(),point,x,y,angle,sx,sy);
        if (dest==screen) display.warp(src,points,std::move(style));
        else target.surface->warp(*src,points,paint(style));
    }
};
thread_local std::shared_ptr<ClientSurfaces::State> ClientSurfaces::State::current;

ClientSurfaces::ClientSurfaces(std::shared_ptr<const Assets> assets,unsigned width,unsigned height,SurfaceDisplay& display,std::size_t budget)
    : state_(std::make_shared<State>(std::move(assets),width,height,display,budget)) {}
ClientSurfaces::~ClientSurfaces()=default;
ClientSurfaces::Scope::Scope(const ClientSurfaces& service) : previous_(State::current),current_(service.state_) {
    current_->require_owner(); State::current=current_;
}
ClientSurfaces::Scope::~Scope() { State::current=std::move(previous_); }
void ClientSurfaces::resize(unsigned width,unsigned height) {
    state_->require_owner();
    auto& target=state_->get(state_->screen);
    // Budget against the replacement, rather than simultaneously charging
    // both screen images. Existing offscreen surfaces remain live.
    if (!width || !height || width>Image::maximum_dimension || height>Image::maximum_dimension)
        throw std::invalid_argument("invalid screen dimensions");
    const auto requested=std::uint64_t(width)*height*4;
    const auto old_bytes=target.surface->image().pixels().size_bytes();
    if (requested>state_->budget || state_->bytes-old_bytes>state_->budget-requested)
        throw std::length_error("client screen exceeds surface budget");
    auto replacement=std::make_shared<Surface>(width,height);
    const auto old=target.surface->image().pixels().size_bytes(),size=replacement->image().pixels().size_bytes();
    if (size>state_->budget || state_->bytes-old>state_->budget-size) throw std::length_error("client screen exceeds surface budget");
    state_->display.resize(width,height);
    state_->bytes=state_->bytes-old+size; target.surface=std::move(replacement);
}
Image ClientSurfaces::screenshot() const {
    state_->require_owner(); return state_->display.read(state_->get(state_->screen).surface->rectangle());
}
std::size_t ClientSurfaces::live_surfaces() const { state_->require_owner(); return state_->live; }
std::size_t ClientSurfaces::allocated_bytes() const { state_->require_owner(); return state_->bytes; }
HSURFACE ClientSurfaces::create_image(Image image) {
    const auto self=state_; self->require_owner(); return self->add(std::make_shared<Surface>(std::move(image)));
}
void ClientSurfaces::draw_image(HSURFACE handle,const std::shared_ptr<const Surface>& source,int x,int y,std::optional<PixelRect> clip) {
    const auto self=state_; self->require_owner(); if (!source) throw std::invalid_argument("missing native image source");
    auto& target=self->get(handle);
    if (handle!=self->screen) { target.surface->composite(*source,x,y,clip); return; }
    const std::int64_t right=std::int64_t(x)+source->image().width(),bottom=std::int64_t(y)+source->image().height();
    if (right>std::numeric_limits<int>::max() || bottom>std::numeric_limits<int>::max()) throw std::invalid_argument("native image destination overflow");
    auto bounds=target.surface->rectangle();
    if (clip) { bounds.left=std::max(bounds.left,clip->left); bounds.top=std::max(bounds.top,clip->top); bounds.right=std::min(bounds.right,clip->right); bounds.bottom=std::min(bounds.bottom,clip->bottom); }
    const PixelRect to{std::max(x,bounds.left),std::max(y,bounds.top),std::min(static_cast<int>(right),bounds.right),std::min(static_cast<int>(bottom),bounds.bottom)};
    if (to.left>=to.right || to.top>=to.bottom) return;
    const PixelRect from{static_cast<int>(std::int64_t(to.left)-x),static_cast<int>(std::int64_t(to.top)-y),static_cast<int>(std::int64_t(to.right)-x),static_cast<int>(std::int64_t(to.bottom)-y)};
    self->display.draw(source,from,to,SurfaceDraw{});
}
void ClientSurfaces::install(ILTClient& client) const {
    state_->require_owner(); const auto& table=functions();
#define NOLF_SURFACE_API(name) client.name=table.name;
#include "surface_api_members.inc"
#undef NOLF_SURFACE_API
}
const SurfaceFunctions& ClientSurfaces::functions() {
    static const auto table=[] {
        SurfaceFunctions api;
        api.CreateColor=api.SetupColor1=api.SetupColor2=color;
        api.DeleteColor=[](HLTCOLOR) {};
        api.GetScreenSurface=[] { return value<HSURFACE>([] { return State::active().screen; }); };
        api.CreateSurface=[](uint32 w,uint32 h) { return value<HSURFACE>([&] { auto& s=State::active(); return s.add(s.create(w,h)); }); };
        api.CreateSurfaceFromBitmap=[](char* name) { return value<HSURFACE>([&] {
            auto& state=State::active();
            if (!name || !state.assets) throw std::invalid_argument("missing surface asset");
            const auto source=state.assets->locate(name);
            if (source.size>64*1024*1024) throw std::length_error("bitmap file exceeds image budget");
            return state.add(std::make_shared<Surface>(decode_pcx(state.assets->read(name))));
        }); };
        api.DeleteSurface=[](HSURFACE h) { return result([&] { State::active().erase(h); }); };
        api.GetSurfaceUserData=[](HSURFACE h) { return value<void*>([&] { return State::active().get(h).user; }); };
        api.SetSurfaceUserData=[](HSURFACE h,void* user) { result([&] { State::active().get(h).user=user; }); };
        api.GetSurfaceDims=[](HSURFACE h,uint32* w,uint32* height) {
            if (w) *w=0;
            if (height) *height=0;
            result([&] { const auto& image=State::active().get(h).surface->image(); if (w) *w=image.width(); if (height) *height=image.height(); });
        };
        api.GetPixel=[](HSURFACE h,uint32 x,uint32 y,HLTCOLOR* output) { return result([&] {
            if (!output) throw std::invalid_argument("null pixel output");
            auto& s=State::active(); const auto& image=s.get(h).surface->image();
            if (x>=image.width() || y>=image.height()) throw std::invalid_argument("pixel outside surface");
            const auto pixel=h==s.screen ? s.display.read({int(x),int(y),int(x)+1,int(y)+1}).at(0,0)&0xffffff : image.at(x,y)&0xffffff;
            *output=pixel;
        }); };
        api.SetPixel=[](HSURFACE h,uint32 x,uint32 y,HLTCOLOR rgb) { return result([&] {
            auto& state=State::active(); auto& surface=state.get(h).surface;
            if (x>=surface->image().width() || y>=surface->image().height()) throw std::invalid_argument("pixel outside surface");
            if (h==state.screen) state.display.clear(PixelRect{int(x),int(y),int(x)+1,int(y)+1},rgb);
            else surface->pixel(x,y,rgb);
        }); };
        api.FillRect=[](HSURFACE h,LTRect* rect,HLTCOLOR rgb) { return result([&] {
            auto& state=State::active(); auto& target=state.get(h);
            if (h==state.screen) state.display.clear(rectangle(rect),rgb);
            else target.surface->fill(rectangle(rect),rgb);
        }); };
        api.GetBorderSize=[](HSURFACE h,HLTCOLOR rgb,LTRect* output) { return result([&] {
            if (!output) throw std::invalid_argument("null border output");
            const auto border=State::active().read(h)->border(rgb);
            *output={border.left,border.top,border.right,border.bottom};
        }); };
        // As in the modern renderer's mr_OptimizeSurface, uploads and color
        // keys are selected per draw. The cache tracks edits automatically;
        // this SDK hint never changes an opaque blit into a keyed blit.
        api.OptimizeSurface=[](HSURFACE h,HLTCOLOR) { return result([&] { State::active().get(h); }); };
        api.UnoptimizeSurface=[](HSURFACE h) { return result([&] { State::active().get(h); }); };
        api.SetSurfaceAlpha=[](HSURFACE h,float alpha) { return result([&] {
            if (!std::isfinite(alpha)) throw std::invalid_argument("nonfinite surface alpha");
            State::active().get(h).surface->alpha(std::clamp(alpha,0.f,1.f));
        }); };
        api.GetSurfaceAlpha=[](HSURFACE h,float& alpha) { return result([&] { alpha=State::active().get(h).surface->alpha(); }); };
        api.DrawSurfaceToSurface=[](HSURFACE dest,HSURFACE src,LTRect* from,int x,int y) { return result([&] {
            auto& s=State::active(); s.draw(dest,src,from,nullptr,x,y,s.style());
        }); };
        api.DrawSurfaceToSurfaceTransparent=[](HSURFACE dest,HSURFACE src,LTRect* from,int x,int y,HLTCOLOR key) { return result([&] {
            auto& s=State::active(); s.draw(dest,src,from,nullptr,x,y,s.style(key));
        }); };
        api.DrawSurfaceSolidColor=[](HSURFACE dest,HSURFACE src,LTRect* from,int x,int y,HLTCOLOR key,HLTCOLOR solid) { return result([&] {
            auto& s=State::active(); s.draw(dest,src,from,nullptr,x,y,s.style(key,solid));
        }); };
        api.DrawSurfaceMasked=[](HSURFACE dest,HSURFACE src,HSURFACE mask,LTRect* from,int x,int y,HLTCOLOR key) { return result([&] {
            auto& s=State::active(); s.get(mask); s.draw(dest,src,from,nullptr,x,y,s.style(key,{},mask));
        }); };
        api.ScaleSurfaceToSurface=[](HSURFACE dest,HSURFACE src,LTRect* to,LTRect* from) { return result([&] {
            auto& s=State::active(); s.scale(dest,src,to,from,s.style());
        }); };
        api.ScaleSurfaceToSurfaceTransparent=[](HSURFACE dest,HSURFACE src,LTRect* to,LTRect* from,HLTCOLOR key) { return result([&] {
            auto& s=State::active(); s.scale(dest,src,to,from,s.style(key));
        }); };
        api.ScaleSurfaceToSurfaceSolidColor=[](HSURFACE dest,HSURFACE src,LTRect* to,LTRect* from,HLTCOLOR key,HLTCOLOR solid) { return result([&] {
            auto& s=State::active(); s.scale(dest,src,to,from,s.style(key,solid));
        }); };
        api.WarpSurfaceToSurface=[](HSURFACE dest,HSURFACE src,LTWarpPt* vertices,int count) { return result([&] {
            auto& s=State::active(); s.warp(dest,src,vertices,count,s.style());
        }); };
        api.WarpSurfaceToSurfaceTransparent=[](HSURFACE dest,HSURFACE src,LTWarpPt* vertices,int count,HLTCOLOR key) { return result([&] {
            auto& s=State::active(); s.warp(dest,src,vertices,count,s.style(key));
        }); };
        api.WarpSurfaceToSurfaceSolidColor=[](HSURFACE dest,HSURFACE src,LTWarpPt* vertices,int count,HLTCOLOR key,HLTCOLOR solid) { return result([&] {
            auto& s=State::active(); s.warp(dest,src,vertices,count,s.style(key,solid));
        }); };
        api.TransformSurfaceToSurface=[](HSURFACE dest,HSURFACE src,LTFloatPt* origin,int x,int y,float angle,float sx,float sy) { return result([&] {
            auto& s=State::active(); s.transform(dest,src,origin,x,y,angle,sx,sy,s.style());
        }); };
        api.TransformSurfaceToSurfaceTransparent=[](HSURFACE dest,HSURFACE src,LTFloatPt* origin,int x,int y,float angle,float sx,float sy,HLTCOLOR key) { return result([&] {
            auto& s=State::active(); s.transform(dest,src,origin,x,y,angle,sx,sy,s.style(key));
        }); };
        api.DrawBitmapToSurface=[](HSURFACE dest,char* name,LTRect* from,int x,int y) -> LTBOOL {
            const auto& table=ClientSurfaces::functions(); const auto source=table.CreateSurfaceFromBitmap(name);
            if (!source) return LTFALSE;
            const auto status=table.DrawSurfaceToSurface(dest,source,from,x,y); table.DeleteSurface(source);
            return status==LT_OK ? LTTRUE : LTFALSE;
        };
        api.StartOptimized2D=[] { return result([] {
            auto& s=State::active(); if (s.optimized>=64) throw std::invalid_argument("optimized surface scope overflow"); ++s.optimized;
        }); };
        api.EndOptimized2D=[] { return result([] {
            auto& s=State::active(); if (!s.optimized) throw std::invalid_argument("unmatched optimized surface scope"); --s.optimized;
        }); };
        api.SetOptimized2DBlend=[](LTSurfaceBlend blend) { return result([&] {
            const auto mode=static_cast<unsigned>(blend);
            if (mode>static_cast<unsigned>(SurfaceBlend::MaskAdd)) throw std::invalid_argument("invalid surface blend");
            State::active().blend=static_cast<SurfaceBlend>(mode);
        }); };
        api.GetOptimized2DBlend=[](LTSurfaceBlend& blend) { return result([&] { blend=static_cast<LTSurfaceBlend>(State::active().blend); }); };
        api.SetOptimized2DColor=[](HLTCOLOR rgb) { return result([&] { State::active().modulation=rgb&0xffffff; }); };
        api.GetOptimized2DColor=[](HLTCOLOR& rgb) { return result([&] { rgb=State::active().modulation; }); };
        return api;
    }();
    return table;
}
}
