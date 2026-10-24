#include "engine/client/surface_api.h"
#include <array>
#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
template<class F> void rejects(F&& f) { bool failed=false; try { f(); } catch (const std::exception&) { failed=true; } require(failed,"invalid client surface request accepted"); }
// A recording display checks the SDK/backend boundary. Actual optimized
// blending, upload/readback and device loss are checked with GlSurfaces.
struct Display final : nolf::SurfaceDisplay {
    std::shared_ptr<nolf::Surface> framebuffer=std::make_shared<nolf::Surface>(8,8);
    nolf::SurfaceDraw last_style;
    float last_alpha=1;
    bool reject_resize=false;
    void resize(unsigned w,unsigned h) override {
        if (reject_resize) throw std::runtime_error("fixture rejected display resize");
        framebuffer=std::make_shared<nolf::Surface>(w,h);
    }
    void draw(const std::shared_ptr<const nolf::Surface>& src,std::optional<nolf::PixelRect> from,nolf::PixelRect to,nolf::SurfaceDraw style) override {
        last_style=style; last_alpha=src->alpha(); framebuffer->blit(*src,from,to,{style.color_key,style.solid,style.mask.get()});
    }
    void warp(const std::shared_ptr<const nolf::Surface>& src,std::span<const nolf::WarpPoint> points,nolf::SurfaceDraw style) override {
        last_style=style; last_alpha=src->alpha(); framebuffer->warp(*src,points,{style.color_key,style.solid,style.mask.get()});
    }
    void clear(std::optional<nolf::PixelRect> rect,unsigned rgb) override { framebuffer->fill(rect,rgb); }
    nolf::Image read(nolf::PixelRect rect) override {
        if (rect.left<0 || rect.top<0 || rect.right>int(framebuffer->image().width()) || rect.bottom>int(framebuffer->image().height()))
            throw std::invalid_argument("fixture read outside framebuffer");
        nolf::Surface result(rect.right-rect.left,rect.bottom-rect.top);
        result.blit(*framebuffer,rect,result.rectangle()); return result.image();
    }
};
const auto& api=nolf::ClientSurfaces::functions();
unsigned pixel(HSURFACE h,unsigned x,unsigned y) { HLTCOLOR color=0xdeadbeef; require(api.GetPixel(h,x,y,&color)==LT_OK,"SDK pixel read failed"); return color; }
void ownership() {
    Display display;
    require(api.CreateSurface(1,1)==nullptr && api.GetScreenSurface()==nullptr && api.FillRect(nullptr,nullptr,0)==LT_NOTINITIALIZED,
        "SDK surface entries require an active callback scope");
    HSURFACE retired=nullptr;
    {
        nolf::ClientSurfaces service({},8,8,display,1024);
        auto scope=service.activate(); const auto screen=api.GetScreenSurface();
        require(screen && service.live_surfaces()==1 && service.allocated_bytes()==256,"screen ownership/budget");
        require(api.DeleteSurface(screen)==LT_INVALIDPARAMS,"game deleted the engine screen handle");
        const auto first=api.CreateSurface(2,2); retired=first;
        nolf::Image glyph(2,1,0); glyph.pixels()[0]=0x80ffffff; glyph.pixels()[1]=0;
        const auto image=service.create_image(glyph); require(image && api.SetPixel(first,0,0,0x0000ff)==LT_OK,"native image surface creation failed");
        auto glyph_source=std::make_shared<nolf::Surface>(std::move(glyph)); service.draw_image(first,glyph_source,0,0);
        require(pixel(first,0,0)==0x8080ff && pixel(first,1,0)==0,"native font-image draw bypassed CPU alpha compositor");
        service.draw_image(screen,glyph_source,-1,0,nolf::PixelRect{0,0,1,1}); require(display.last_style.blend==nolf::SurfaceBlend::Alpha,"native image draw inherited unrelated optimized blend state");
        require(api.DeleteSurface(image)==LT_OK,"owned native image surface did not retire");
        require(first && api.SetPixel(first,1,1,0x123456)==LT_OK,"owned surface creation");
        int user=7; api.SetSurfaceUserData(first,&user); require(api.GetSurfaceUserData(first)==&user,"native user pointer was narrowed");
        require(api.DeleteSurface(first)==LT_OK && api.DeleteSurface(first)==LT_INVALIDPARAMS,"retired handles remain invalid");
        require(api.GetSurfaceUserData(first)==nullptr && api.SetPixel(first,0,0,0)==LT_INVALIDPARAMS,"retired handles accessed freed pixels");
        for (unsigned i=0;i<70000;++i) {
            const auto h=api.CreateSurface(1,1); require(h && h!=retired,"surface handle was reused");
            require(api.DeleteSurface(h)==LT_OK,"repeated surface retirement failed");
        }
        require(service.live_surfaces()==1 && service.allocated_bytes()==256,"retired handle records leaked surface budget");
        const auto valid=api.CreateSurface(2,2);
        {
            Display other_display; nolf::ClientSurfaces other({},8,8,other_display); auto nested=other.activate();
            require(api.DeleteSurface(valid)==LT_INVALIDPARAMS && api.GetScreenSurface()!=screen,"nested clients shared opaque handles");
        }
        require(api.GetScreenSurface()==screen && api.SetPixel(valid,0,0,0xffffff)==LT_OK,"nested callbacks lost their outer client");
        auto thread=std::async(std::launch::async,[&] {
            rejects([&] { auto wrong=service.activate(); });
            require(api.GetScreenSurface()==nullptr,"another thread inherited the active client");
            Display local; nolf::ClientSurfaces owned({},8,8,local); auto active=owned.activate();
            require(api.GetScreenSurface()!=screen,"independent threads reused a client");
            return api.CreateSurface(1,1)!=nullptr;
        }); require(thread.get(),"thread-owned client failed");
        require(api.CreateSurface(0,1)==nullptr && api.CreateSurface(0xffffffff,0xffffffff)==nullptr && api.CreateSurface(20,20)==nullptr,
            "invalid or over-budget dimensions allocated surfaces");
        const auto old_bytes=service.allocated_bytes();
        display.reject_resize=true; rejects([&] { service.resize(9,9); }); display.reject_resize=false;
        unsigned w=0,h=0; api.GetSurfaceDims(screen,&w,&h);
        require(w==8 && h==8 && service.allocated_bytes()==old_bytes,"failed backend resize changed SDK screen ownership");
        service.resize(10,10); api.GetSurfaceDims(screen,&w,&h);
        require(api.GetScreenSurface()==screen && w==10 && h==10 && pixel(valid,0,0)==0xffffff,"resize invalidated screen token or offscreen pixels");
        rejects([&] { service.resize(100,100); });
        require(api.DeleteSurface(valid)==LT_OK,"offscreen survived resize but could not retire");
    }
    require(api.GetScreenSurface()==nullptr,"callback scope retained a dead active client");
    // An active callback lease owns the service state even after its host is
    // retired. The display still belongs to the surrounding engine lifetime.
    auto owner=std::make_unique<nolf::ClientSurfaces>(nullptr,8,8,display);
    auto lease=owner->activate(); const auto screen=api.GetScreenSurface(); owner.reset();
    require(screen && api.GetScreenSurface()==screen && api.FillRect(screen,nullptr,0xaabbcc)==LT_OK,"callback lease did not retain retired client state");
}
void operations() {
    Display display; nolf::ClientSurfaces service({},8,8,display); auto scope=service.activate();
    const auto source=api.CreateSurface(2,2),dest=api.CreateSurface(6,6),screen=api.GetScreenSurface();
    const unsigned colors[]{0xff0000,0x00ff00,0x0000ff,0xffffff};
    for (unsigned i=0;i<4;++i) require(api.SetPixel(source,i%2,i/2,colors[i])==LT_OK,"source pixels");
    require(api.DrawSurfaceToSurface(dest,source,nullptr,1,2)==LT_OK && pixel(dest,1,2)==colors[0] && pixel(dest,2,3)==colors[3],"plain SDK blit dimensions/origin");
    require(api.DrawSurfaceToSurface(dest,source,nullptr,std::numeric_limits<int>::max(),0)==LT_INVALIDPARAMS,"destination addition overflowed");
    require(api.DrawSurfaceToSurface(dest,source,nullptr,-1,-1)==LT_OK && pixel(dest,0,0)==0xffffff,"clipped blit lost source coordinates");
    require(api.FillRect(dest,nullptr,0x123456)==LT_OK && api.DrawSurfaceToSurfaceTransparent(dest,source,nullptr,0,0,SETRGB_T(255,0,0))==LT_OK &&
        pixel(dest,0,0)==0x123456 && pixel(dest,1,0)==0x00ff00,"SDK transparent color flag affected RGB matching");
    require(api.DrawSurfaceSolidColor(dest,source,nullptr,0,0,0xff0000,0xffff00)==LT_OK && pixel(dest,1,1)==0xffff00,"SDK solid-color blit");
    LTRect to{0,0,4,4}; require(api.ScaleSurfaceToSurface(dest,source,&to,nullptr)==LT_OK && pixel(dest,3,3)==0xffffff,"SDK scale source/destination order");
    const auto mask=api.CreateSurface(2,1); api.SetPixel(mask,0,0,0x111111); api.SetPixel(mask,1,0,0xeeeeee);
    require(api.DrawSurfaceMasked(dest,source,mask,nullptr,0,0,0xff0000)==LT_OK && pixel(dest,0,1)==0x111111 && pixel(dest,1,1)==0xeeeeee,"SDK mask ownership and tiling");
    require(api.WarpSurfaceToSurface(dest,source,nullptr,4)==LT_INVALIDPARAMS,"null warp points accepted");
    std::array<LTWarpPt,4> points{{{0,0,0,0},{1,0,2,0},{1,1,2,2},{0,1,0,2}}};
    require(api.WarpSurfaceToSurface(dest,source,points.data(),4)==LT_OK && pixel(dest,1,1)==0xffffff,"SDK warp coordinate conversion");
    api.FillRect(dest,nullptr,0);
    require(api.TransformSurfaceToSurface(dest,source,nullptr,1,1,0,1,1)==LT_OK && pixel(dest,1,1)==0xff0000,"SDK affine origin");
    require(api.TransformSurfaceToSurface(dest,source,nullptr,1,1,0,0,1)==LT_INVALIDPARAMS,"degenerate affine transform accepted");
    api.FillRect(dest,nullptr,0); LTRect inner{2,1,4,3}; api.FillRect(dest,&inner,0xffffff); LTRect border{};
    require(api.GetBorderSize(dest,0,&border)==LT_OK && border.left==2 && border.top==1 && border.right==2 && border.bottom==3,"SDK border margins");
    unsigned untouched=0xdeadbeef;
    require(api.GetPixel(dest,99,99,&untouched)==LT_INVALIDPARAMS && untouched==0xdeadbeef && api.GetPixel(dest,0,0,nullptr)==LT_INVALIDPARAMS,"bad pixel read corrupted output");
    require(api.SetSurfaceAlpha(source,2)==LT_OK,"retail surface alpha clamp"); float alpha=0;
    require(api.GetSurfaceAlpha(source,alpha)==LT_OK && alpha==1 && api.SetSurfaceAlpha(source,std::numeric_limits<float>::quiet_NaN())==LT_INVALIDPARAMS,"surface alpha validation");
    api.SetSurfaceAlpha(source,.5f);
    require(api.DrawSurfaceToSurface(screen,source,nullptr,0,0)==LT_OK && display.last_alpha==.5f,"modern screen alpha was lost outside optimized drawing");
    require(api.StartOptimized2D()==LT_OK && api.OptimizeSurface(source,SETRGB_T(255,0,0))==LT_OK &&
        api.DrawSurfaceToSurface(screen,source,nullptr,0,0)==LT_OK && display.last_alpha==.5f && !display.last_style.color_key,
        "optimization hint changed modern opaque blit semantics");
    require(api.DrawSurfaceToSurfaceTransparent(screen,source,nullptr,0,0,0xff0000)==LT_OK && display.last_style.color_key==0xff0000,
        "explicit transparent draw lost its color key");
    api.SetOptimized2DBlend(LTSURFACEBLEND_ADD); api.SetOptimized2DColor(0x808080);
    require(api.TransformSurfaceToSurface(screen,source,nullptr,0,0,0,1,1)==LT_OK && display.last_style.blend==nolf::SurfaceBlend::Add &&
        display.last_style.color==0x808080 && display.last_alpha==.5f,"affine screen draw bypassed optimized blend/color/alpha");
    require(api.OptimizeSurface(source,0)==LT_OK && api.DrawSurfaceToSurface(screen,source,nullptr,0,0)==LT_OK && !display.last_style.color_key,"opaque optimization made black transparent");
    LTSurfaceBlend mode=LTSURFACEBLEND_ALPHA; HLTCOLOR modulation=0;
    require(api.GetOptimized2DBlend(mode)==LT_OK && mode==LTSURFACEBLEND_ADD && api.GetOptimized2DColor(modulation)==LT_OK && modulation==0x808080,"optimized state queries");
    require(api.EndOptimized2D()==LT_OK && api.EndOptimized2D()==LT_INVALIDPARAMS,"unmatched optimized draw block accepted");
    display.framebuffer->fill(std::nullopt,0xabcdef); // A renderer updated the screen outside the SDK surface service.
    require(pixel(screen,2,2)==0xabcdef && api.DrawSurfaceToSurface(dest,screen,nullptr,0,0)==LT_OK && pixel(dest,4,4)==0xabcdef &&
        service.screenshot().at(7,7)==0xffabcdef,"screen readback used a stale CPU shadow instead of the renderer");
    require(api.CreateColor(-1,2,.5f,LTTRUE)==SETRGB_T(0,255,127) && api.SetupColor1(0,0,0,LTFALSE)==0,"SDK color clamp/transparency bit");
}
void corpus(const char* path) {
    auto assets=std::make_shared<nolf::Assets>(); assets->mount_game(path);
    Display display; nolf::ClientSurfaces service(assets,8,8,display); auto scope=service.activate(); unsigned count=0;
    for (auto name : assets->names()) {
        if (!name.ends_with(".pcx")) continue;
        const auto decoded=nolf::decode_pcx(assets->read(name)); const auto h=api.CreateSurfaceFromBitmap(name.data());
        require(h!=nullptr,"original bitmap could not load through the SDK"); unsigned w=0,height=0; api.GetSurfaceDims(h,&w,&height);
        require(w==decoded.width() && height==decoded.height(),"original SDK surface dimensions changed");
        for (unsigned y=0;y<height;++y) for (unsigned x=0;x<w;++x)
            require(pixel(h,x,y)==(decoded.at(x,y)&0xffffff),"original SDK surface pixel changed");
        require(api.DeleteSurface(h)==LT_OK,"original SDK bitmap release failed"); ++count;
    }
    require(count>0 && service.live_surfaces()==1,"original bitmap corpus was empty or leaked a surface");
    std::cout << "Original PCX assets loaded/read/released through SDK: " << count << '\n';
}
}
int main(int argc,char** argv) {
    try { ownership(); operations(); if (argc==2) corpus(argv[1]);
        std::cout << "Native SDK surface ownership, scoped dispatch and display boundary passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
