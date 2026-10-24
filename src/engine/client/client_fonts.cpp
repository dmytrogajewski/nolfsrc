#include "engine/client/client_fonts.h"
#include "engine/server/stringbank.h"
#include "engine/portable/text.h"
#include <algorithm>
#include <atomic>
#include <limits>
#include <map>
#include <thread>
#include <tuple>

namespace nolf {
namespace {
std::atomic<std::uint64_t> next_font{1};
class Inactive : public std::logic_error { public: using std::logic_error::logic_error; };
template<class F> LTRESULT operation(F&& function) noexcept {
    try { return function(); } catch (const Inactive&) { return LT_NOTINITIALIZED; }
    catch (const std::bad_alloc&) { return LT_OUTOFMEMORY; } catch (const std::invalid_argument&) { return LT_INVALIDPARAMS; }
    catch (...) { return LT_ERROR; }
}
template<class T,class F> T value(F&& function,T fallback={}) noexcept { T output=fallback; operation([&]() -> LTRESULT { output=function(); return LT_OK; }); return output; }
std::uint32_t codepoint(const char* begin,const char* end) {
    const auto size=end-begin;
    const auto first=static_cast<unsigned char>(*begin);
    if (size==1) return first<128 ? first : 0xfffd;
    std::uint32_t code=first&(size==2 ? 31 : size==3 ? 15 : 7);
    for (auto* p=begin+1;p!=end;++p) code=(code<<6)|(static_cast<unsigned char>(*p)&63);
    return code;
}
std::string string(HSTRING handle) {
    const auto text=sb_GetStringSnapshot(handle); if (!text) throw std::invalid_argument("invalid font string handle");
    if (text->size()>65536) throw std::length_error("font string exceeds layout budget");
    return *text;
}
}
struct ClientFonts::State {
    static thread_local std::shared_ptr<State> current;
    struct Record { std::shared_ptr<platform::FontFace> face; int extra=0; bool underline=false; };
    struct Placement { std::uint32_t code; int x; };
    struct Layout { int width=0,height=0,baseline=0; std::vector<Placement> glyphs; };
    using Key=std::tuple<std::uintptr_t,std::string,HLTCOLOR,HLTCOLOR,int,int>;
    struct Raster { std::shared_ptr<const Surface> surface; std::uint64_t used; };
    std::shared_ptr<platform::FontProvider> provider;
    std::shared_ptr<ClientSurfaces> surfaces;
    std::map<HLTFONT,Record,std::less<HLTFONT>> records;
    std::map<Key,Raster> cache;
    std::size_t bytes=0,budget;
    std::uint64_t clock=0;
    const std::thread::id owner=std::this_thread::get_id();
    State(std::shared_ptr<platform::FontProvider> source,std::shared_ptr<ClientSurfaces> target,std::size_t limit) : provider(std::move(source)),surfaces(std::move(target)),budget(limit) {
        if (!provider || !surfaces || !budget) throw std::invalid_argument("native font services are required");
    }
    void thread() const { if (owner!=std::this_thread::get_id()) throw std::logic_error("client fonts used from another thread"); }
    static std::shared_ptr<State> active() { const auto self=current; if (!self) throw Inactive("font SDK call outside callback scope"); self->thread(); return self; }
    Record& get(HLTFONT font) { thread(); const auto found=records.find(font); if (found==records.end()) throw std::invalid_argument("invalid native font handle"); return found->second; }
    void erase_cache(std::map<Key,Raster>::iterator item) { bytes-=item->second.surface->image().pixels().size_bytes(); cache.erase(item); }
    void invalidate(HLTFONT font) { for (auto it=cache.begin();it!=cache.end();) { if (std::get<0>(it->first)==reinterpret_cast<std::uintptr_t>(font)) { const auto dead=it++; erase_cache(dead); } else ++it; } }
    HLTFONT create(const char* family,int width,int height,bool italic,bool underline,bool bold) {
        thread(); if (records.size()>=1024) throw std::length_error("native font count exceeds budget");
        platform::FontRequest request; request.family=family && *family ? family : "sans-serif"; request.width=width; request.height=height ? height : 16; request.italic=italic; request.bold=bold; request.underline=underline;
        auto face=provider->load(request); if (!face) throw std::runtime_error("native font provider returned no face");
        const auto id=next_font.fetch_add(1,std::memory_order_relaxed); if (!id || id>std::numeric_limits<std::uintptr_t>::max()) throw std::overflow_error("font identities exhausted");
        const auto handle=reinterpret_cast<HLTFONT>(static_cast<std::uintptr_t>(id)); records.emplace(handle,Record{std::move(face),0,underline}); return handle;
    }
    Layout layout(Record& font,const std::string& text) {
        const auto metrics=font.face->metrics(); if (metrics.height<=0 || metrics.height>4096 || metrics.ascent<0 || metrics.ascent>metrics.height) throw std::runtime_error("invalid native font metrics");
        Layout result; result.height=metrics.height; result.baseline=metrics.ascent;
        std::int64_t x=0; std::uint32_t previous=0;
        for (const char* p=text.c_str();*p;) {
            const auto* end=nolf::text::next(p); const auto code=codepoint(p,end); p=end;
            const auto glyph=font.face->glyph(code,false); if (previous) x+=font.face->kerning(previous,glyph.index);
            if (x<std::numeric_limits<int>::min() || x>std::numeric_limits<int>::max()) throw std::length_error("native font layout overflow");
            result.glyphs.push_back({code,static_cast<int>(x)}); x+=glyph.advance; if (*p) x+=font.extra; previous=glyph.index;
        }
        if (x>std::numeric_limits<int>::max() || x<std::numeric_limits<int>::min()) throw std::length_error("native font string width overflow");
        result.width=static_cast<int>(std::max<std::int64_t>(x,0)); return result;
    }
    std::shared_ptr<const Surface> raster(HLTFONT font,const std::string& text,HLTCOLOR fore,HLTCOLOR back,int extra_x,int extra_y) {
        auto& record=get(font); const Key key{reinterpret_cast<std::uintptr_t>(font),text,fore,back,extra_x,extra_y};
        if (const auto found=cache.find(key);found!=cache.end()) { found->second.used=++clock; return found->second.surface; }
        const auto placed=layout(record,text); const std::int64_t width=std::int64_t(placed.width)+extra_x,height=std::int64_t(placed.height)+extra_y;
        if (width<=0 || height<=0 || width>Image::maximum_dimension || height>Image::maximum_dimension || std::uint64_t(width)*height>Image::maximum_pixels || std::uint64_t(width)*height*4>budget) throw std::length_error("native text image exceeds cache budget");
        const auto foreground=fore&COLOR_TRANSPARENCY_MASK ? 0u : 0xff000000u|(fore&0xffffff);
        const auto background=back&COLOR_TRANSPARENCY_MASK ? 0u : 0xff000000u|(back&0xffffff);
        Image image(static_cast<uint32>(width),static_cast<uint32>(height),background);
        for (const auto& placement : placed.glyphs) {
            const auto glyph=record.face->glyph(placement.code);
            if (glyph.width<0 || glyph.height<0 || glyph.width>4096 || glyph.height>4096 || glyph.coverage.size()!=std::size_t(glyph.width)*glyph.height) throw std::runtime_error("invalid native font glyph bitmap");
            const std::int64_t left=std::int64_t(placement.x)+glyph.left,top=std::int64_t(placed.baseline)-glyph.top;
            for (int y=0;y<glyph.height;++y) {
                const auto dy=top+y; if (dy<0 || dy>=height) continue;
                for (int x=0;x<glyph.width;++x) { const auto dx=left+x; if (dx>=0 && dx<width && glyph.coverage[std::size_t(y)*glyph.width+x]>=128) image.pixels()[std::size_t(dy)*width+std::size_t(dx)]=foreground; }
            }
        }
        if (record.underline && !placed.glyphs.empty()) {
            const auto metrics=record.face->metrics();
            for (int y=std::max(0,metrics.underline_y);y<std::min<std::int64_t>(height,std::int64_t(metrics.underline_y)+metrics.underline_thickness);++y)
                for (int x=0;x<std::min<std::int64_t>(width,placed.width);++x) image.pixels()[std::size_t(y)*width+x]=foreground;
        }
        auto surface=std::make_shared<Surface>(std::move(image)); const auto size=surface->image().pixels().size_bytes();
        while (bytes>budget-size || cache.size()>=256) { auto oldest=cache.begin(); for (auto it=cache.begin();it!=cache.end();++it) if (it->second.used<oldest->second.used) oldest=it; erase_cache(oldest); }
        cache.emplace(key,Raster{surface,++clock}); bytes+=size; return surface;
    }
};
thread_local std::shared_ptr<ClientFonts::State> ClientFonts::State::current;
ClientFonts::ClientFonts(std::shared_ptr<platform::FontProvider> provider,std::shared_ptr<ClientSurfaces> surfaces,std::size_t budget) : state_(std::make_shared<State>(std::move(provider),std::move(surfaces),budget)) {}
ClientFonts::~ClientFonts()=default;
ClientFonts::Scope::Scope(const ClientFonts& fonts) : previous_(State::current),current_(fonts.state_) { current_->thread(); State::current=current_; }
ClientFonts::Scope::~Scope() { State::current=std::move(previous_); }
std::size_t ClientFonts::live_fonts() const { state_->thread(); return state_->records.size(); }
std::size_t ClientFonts::cached_bytes() const { state_->thread(); return state_->bytes; }
void ClientFonts::install(ILTClient& client) {
    const auto& api=functions();
#define NOLF_FONT_API(name) client.name=api.name;
#include "client_font_members.inc"
#undef NOLF_FONT_API
}
const FontFunctions& ClientFonts::functions() {
    static const FontFunctions api{
        .CreateFont=+[](char* family,int width,int height,LTBOOL italic,LTBOOL underline,LTBOOL bold) -> HLTFONT { return value<HLTFONT>([&] { return State::active()->create(family,width,height,italic!=0,underline!=0,bold!=0); }); },
        .DeleteFont=+[](HLTFONT font) { operation([&]() -> LTRESULT { const auto self=State::active(); self->get(font); self->invalidate(font); self->records.erase(font); return LT_OK; }); },
        .SetFontExtraSpace=+[](HLTFONT font,int extra) -> LTRESULT { return operation([&]() -> LTRESULT { const auto self=State::active(); auto& record=self->get(font); if (extra < -512 || extra>512) return LT_INVALIDPARAMS; self->invalidate(font); record.extra=extra; return LT_OK; }); },
        .GetFontExtraSpace=+[](HLTFONT font,int& extra) -> LTRESULT { extra=0; return operation([&]() -> LTRESULT { const auto self=State::active(); extra=self->get(font).extra; return LT_OK; }); },
        .CreateSurfaceFromString=+[](HLTFONT font,HSTRING text,HLTCOLOR fore,HLTCOLOR back,int extra_x,int extra_y) -> HSURFACE {
            return value<HSURFACE>([&]() -> HSURFACE { const auto self=State::active(); self->get(font);
                if (!text) { const auto width=std::int64_t(1)+extra_x,height=std::int64_t(1)+extra_y; if (width<=0 || height<=0 || width>Image::maximum_dimension || height>Image::maximum_dimension || std::uint64_t(width)*height>Image::maximum_pixels) throw std::invalid_argument("invalid blank font surface size"); return self->surfaces->create_image(Image(static_cast<uint32>(width),static_cast<uint32>(height))); }
                const auto image=self->raster(font,string(text),fore,back,extra_x,extra_y); auto owned=image->image();
                for (auto& color : owned.pixels()) if (!(color>>24)) color=0xff000000;
                return self->surfaces->create_image(std::move(owned)); });
        },
        .GetStringDimensions=+[](HLTFONT font,HSTRING text,int* width,int* height) {
            if (width) *width=0;
            if (height) *height=0;
            operation([&]() -> LTRESULT { const auto self=State::active(); if (!font || !text) return LT_OK; auto& record=self->get(font); const auto placed=self->layout(record,string(text)); if (width) *width=placed.width; if (height) *height=placed.height; return LT_OK; });
        },
        .DrawStringToSurface=+[](HSURFACE destination,HLTFONT font,HSTRING text,LTRect* rectangle,HLTCOLOR fore,HLTCOLOR back) {
            operation([&]() -> LTRESULT { const auto self=State::active(); if (!font || !text || !destination) return LT_OK;
                const auto contents=string(text); self->get(font); if (contents.empty()) return LT_OK;
                const auto image=self->raster(font,contents,fore,back,0,0); self->surfaces->draw_image(destination,image,rectangle ? rectangle->left : 0,rectangle ? rectangle->top : 0); return LT_OK; });
        }
    }; return api;
}
}
