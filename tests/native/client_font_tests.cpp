#include "engine/client/client_fonts.h"
#include "engine/server/stringbank.h"
#include <algorithm>
#include <future>
#include <iostream>
#include <limits>

namespace {
void require(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
struct String {
    HSTRING handle;
    explicit String(std::string text) : handle(sb_CreateString(text.data())) { require(handle,"native test string creation failed"); }
    ~String() { sb_FreeString(handle); }
};
struct Display final : nolf::SurfaceDisplay {
    nolf::Surface screen{256,64};
    void resize(uint32 w,uint32 h) override { require(w==256 && h==64,"unexpected font display resize"); }
    void draw(const std::shared_ptr<const nolf::Surface>& source,std::optional<nolf::PixelRect> from,nolf::PixelRect to,nolf::SurfaceDraw style) override {
        require(style.blend==nolf::SurfaceBlend::Alpha && !style.color_key && !style.solid,"font drawing inherited unrelated display state");
        const auto rectangle=from.value_or(source->rectangle()); require(to.right-to.left==rectangle.right-rectangle.left && to.bottom-to.top==rectangle.bottom-rectangle.top,"font image was unexpectedly scaled");
        screen.composite(*source,to.left-rectangle.left,to.top-rectangle.top,to);
    }
    void warp(const std::shared_ptr<const nolf::Surface>&,std::span<const nolf::WarpPoint>,nolf::SurfaceDraw) override { throw std::runtime_error("font requested a warp"); }
    void clear(std::optional<nolf::PixelRect> rect,uint32 rgb) override { screen.fill(rect,rgb); }
    nolf::Image read(nolf::PixelRect rect) override { require(rect.left==0 && rect.top==0 && rect.right==256 && rect.bottom==64,"unexpected font readback"); return screen.image(); }
};
uint32 pixel(HSURFACE surface,uint32 x,uint32 y) { HLTCOLOR color=0; require(nolf::ClientSurfaces::functions().GetPixel(surface,x,y,&color)==LT_OK,"font surface pixel read failed"); return color; }
void checks() {
    Display display; auto surfaces=std::make_shared<nolf::ClientSurfaces>(nullptr,256,64,display); auto provider=nolf::platform::make_freetype_fonts(nolf::platform::system_font_locator());
    nolf::ClientFonts fonts(provider,surfaces,1024*1024); const auto& api=nolf::ClientFonts::functions(); const auto& images=nolf::ClientSurfaces::functions(); char family[]="monospace";
    require(!api.CreateFont(family,0,16,LTFALSE,LTFALSE,LTFALSE),"font SDK worked without owning callback scope");
    auto image_scope=surfaces->activate(); const auto active=fonts.activate(); const auto font=api.CreateFont(family,0,16,LTFALSE,LTFALSE,LTFALSE);
    require(font && fonts.live_fonts()==1,"native SDK font creation failed"); String text("AV é"),i("i"),ii("ii"),empty("");
    int width=0,height=0; api.GetStringDimensions(font,text.handle,&width,&height); require(width>0 && height==16,"font SDK dimensions failed");
    int letter=0,line=0; api.GetStringDimensions(font,i.handle,&letter,&line); require(letter>0 && line==16,"single glyph SDK dimensions failed");
    int pair=0; api.GetStringDimensions(font,ii.handle,&pair,nullptr); require(pair==letter*2 && api.SetFontExtraSpace(font,3)==LT_OK,"font pair or extra spacing setup failed"); api.GetStringDimensions(font,ii.handle,&pair,nullptr); require(pair==letter*2+3,"extra spacing counted UTF-8 bytes or trailing gap");
    int extra=0; require(api.GetFontExtraSpace(font,extra)==LT_OK && extra==3 && api.SetFontExtraSpace(font,0)==LT_OK,"font spacing read/reset failed");
    int null_width=99,null_height=99; api.GetStringDimensions(font,nullptr,&null_width,&null_height); require(!null_width && !null_height,"null string dimensions differ from original SDK");
    api.GetStringDimensions(font,empty.handle,&null_width,&null_height); require(null_width==0 && null_height==16,"empty native string cell metrics differ");
    const auto null_surface=api.CreateSurfaceFromString(font,nullptr,0xffffff,0xff0000,0,0); require(null_surface && pixel(null_surface,0,0)==0,"null string surface not a plain black pixel"); require(images.DeleteSurface(null_surface)==LT_OK,"null font surface release failed");
    require(!api.CreateSurfaceFromString(font,empty.handle,0xffffff,0,0,0),"zero-width string allocated surface");
    const auto mask=api.CreateSurfaceFromString(font,text.handle,0xffffff,0,2,3); require(mask,"native font string surface failed"); uint32 mask_width=0,mask_height=0; images.GetSurfaceDims(mask,&mask_width,&mask_height); require(mask_width==uint32(width+2) && mask_height==19,"text surface padding differs");
    unsigned ink=0,blank=0; for (uint32 y=0;y<16;++y) for (uint32 x=0;x<uint32(width);++x) { const auto color=pixel(mask,x,y); require(color==0 || color==0xffffff,"native font raster is not a monochrome mask"); if (color) ++ink; else ++blank; } require(ink && blank,"native text image has no glyphs/background");
    const auto target=images.CreateSurface(width+4,20); require(target && images.FillRect(target,nullptr,0x0000ff)==LT_OK,"native font target failed");
    LTRect at{2,1,2,1}; // Original SDK uses left/top, independently of right/bottom.
    api.DrawStringToSurface(target,font,text.handle,&at,0xff0000,COLOR_TRANSPARENCY_MASK);
    for (uint32 y=0;y<16;++y) for (uint32 x=0;x<uint32(width);++x) require(pixel(target,x+2,y+1)==(pixel(mask,x,y) ? 0xff0000 : 0x0000ff),"transparent font background, UTF-8 glyph placement or rectangle positioning differs");
    require(images.FillRect(target,nullptr,0x00ff00)==LT_OK,"font foreground transparency clear failed"); api.DrawStringToSurface(target,font,text.handle,&at,COLOR_TRANSPARENCY_MASK,0x0000ff);
    for (uint32 y=0;y<16;++y) for (uint32 x=0;x<uint32(width);++x) require(pixel(target,x+2,y+1)==(pixel(mask,x,y) ? 0x00ff00 : 0x0000ff),"transparent foreground did not preserve glyph-shaped destination holes");
    api.DrawStringToSurface(target,font,text.handle,&at,COLOR_TRANSPARENCY_MASK,COLOR_TRANSPARENCY_MASK); require(pixel(target,0,0)==0x00ff00,"fully transparent font draw changed destination");
    const auto from_transparent=api.CreateSurfaceFromString(font,text.handle,0xffffff,COLOR_TRANSPARENCY_MASK,0,0); require(from_transparent,"transparent string surface failed");
    for (uint32 y=0;y<16;++y) for (uint32 x=0;x<uint32(width);++x) require(pixel(from_transparent,x,y)==pixel(mask,x,y),"string surface lost default black pixels under transparent background");
    const auto cached=fonts.cached_bytes(); require(cached>0 && cached<=1024*1024,"font cache did not retain bounded raster images");
    api.DrawStringToSurface(target,font,text.handle,&at,0xff0000,COLOR_TRANSPARENCY_MASK); require(fonts.cached_bytes()==cached,"repeat font draw allocated duplicate raster cache entry");
    const auto underlined=api.CreateFont(family,0,16,LTFALSE,LTTRUE,LTFALSE); require(underlined,"underline font creation failed"); const auto line_mask=api.CreateSurfaceFromString(underlined,text.handle,0xffffff,0,0,0); require(line_mask,"underline image failed");
    nolf::platform::FontRequest request; request.family=family; const auto underline_y=provider->load(request)->metrics().underline_y;
    for (uint32 x=0;x<uint32(width);++x) require(pixel(line_mask,x,underline_y)==0xffffff,"native underline did not cover spaces and glyph advances");
    require(images.DeleteSurface(line_mask)==LT_OK,"underline surface did not retire"); api.DeleteFont(underlined); require(fonts.cached_bytes()==cached,"retiring one font invalidated another font's cache");
    require(api.SetFontExtraSpace(font,std::numeric_limits<int>::max())==LT_INVALIDPARAMS,"invalid font spacing accepted");
    const auto bad=reinterpret_cast<HSTRING>(1); api.GetStringDimensions(font,bad,&null_width,&null_height); require(!null_width && !null_height,"font SDK dereferenced unknown string handle");
    { nolf::ClientFonts other(provider,surfaces); const auto nested=other.activate(); require(api.GetFontExtraSpace(font,extra)==LT_INVALIDPARAMS && extra==0,"nested client font handle accepted"); }
    require(api.GetFontExtraSpace(font,extra)==LT_OK,"nested font scope did not restore owner");
    require(std::async(std::launch::async,[&] { int result=0; return api.GetFontExtraSpace(font,result); }).get()==LT_NOTINITIALIZED,"font SDK scope leaked to another thread");
    api.DeleteFont(font); require(fonts.live_fonts()==0 && !fonts.cached_bytes() && api.GetFontExtraSpace(font,extra)==LT_INVALIDPARAMS,"font retirement left handles/cache live"); const auto next=api.CreateFont(family,0,16,LTFALSE,LTFALSE,LTFALSE); require(next && next!=font,"font handle was reused"); api.DeleteFont(next);
    for (const auto handle : {mask,target,from_transparent}) require(images.DeleteSurface(handle)==LT_OK,"font image surface retirement failed");
    require(surfaces->live_surfaces()==1,"font tests left native surfaces live");
    auto owner=std::make_unique<nolf::ClientFonts>(provider,surfaces); { const auto lease=owner->activate(); const auto retained=api.CreateFont(family,0,16,LTFALSE,LTFALSE,LTFALSE); owner.reset(); api.GetStringDimensions(retained,text.handle,&width,&height); require(width>0 && height==16,"active font callback lost service state on host retirement"); api.DeleteFont(retained); }
}
}
int main() { try { checks(); std::cout<<"Native SDK fonts, Unicode layout, fore/background masks, surface images, spacing, ownership, cache and callback lifetime passed\n"; return 0; } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
