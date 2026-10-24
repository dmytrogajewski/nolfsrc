#include "engine/client/gl_surfaces.h"
#include "engine/client/surface_api.h"
#ifdef NOLF_TEST_SYSTEM_FONTS
#include "engine/client/client_fonts.h"
#include "engine/server/stringbank.h"
#endif
#include "engine/portable/assets.h"
#include "platform/desktop.h"
#include "rdev.h"
#include <array>
#include <cmath>
#include <cstdlib>
#include <string_view>
#include <future>
#include <iostream>
#include <stdexcept>

static void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F action) { bool rejected=false; try { action(); } catch (const std::exception&) { rejected=true; } require(rejected,"invalid GPU surface operation accepted"); }
static unsigned rgb(const nolf::Image& image,unsigned x,unsigned y) { return image.at(x,y)&0xffffff; }
static void near(unsigned pixel,unsigned expected) {
    for (unsigned shift : {0u,8u,16u}) if (std::abs(int((pixel>>shift)&255)-int((expected>>shift)&255))>2) throw std::runtime_error("OpenGL surface pixel differs from expected blend");
}
static void sdk_surface_checks(nolf::GlSurfaces& renderer,unsigned width,unsigned height) {
    nolf::ClientSurfaces surfaces({},width,height,renderer);
    auto active=surfaces.activate(); const auto& sdk=nolf::ClientSurfaces::functions();
    const auto screen=sdk.GetScreenSurface(),source=sdk.CreateSurface(1,1),copy=sdk.CreateSurface(4,4);
    require(screen && source && copy,"SDK GPU surface creation failed");
    require(sdk.SetPixel(source,0,0,0xff0000)==LT_OK && sdk.FillRect(screen,nullptr,0x0000ff)==LT_OK,"SDK GPU pixel/clear failed");
    require(sdk.OptimizeSurface(source,0)==LT_OK && sdk.SetSurfaceAlpha(source,.5f)==LT_OK && sdk.StartOptimized2D()==LT_OK,"SDK GPU optimized setup failed");
    LTRect rectangle{0,0,16,16};
    require(sdk.ScaleSurfaceToSurface(screen,source,&rectangle,nullptr)==LT_OK,"SDK GPU scaled blit failed");
    HLTCOLOR pixel=0; require(sdk.GetPixel(screen,4,4,&pixel)==LT_OK,"SDK GPU framebuffer pixel read failed"); near(pixel,0x800080);
    require(sdk.DrawSurfaceToSurface(copy,screen,nullptr,0,0)==LT_OK && sdk.GetPixel(copy,2,2,&pixel)==LT_OK,"SDK screen-to-surface readback failed"); near(pixel,0x800080);
    require(sdk.SetSurfaceAlpha(source,1)==LT_OK && sdk.SetPixel(source,0,0,0x808080)==LT_OK,"SDK GPU source mutation failed");
    constexpr std::array<unsigned,7> expected{0x808080,0x808080,0xc0c0c0,0x202020,0x404040,0x202020,0xa0a0a0};
    for (unsigned mode=0;mode<expected.size();++mode) {
        require(sdk.FillRect(screen,nullptr,0x404040)==LT_OK && sdk.SetOptimized2DBlend(static_cast<LTSurfaceBlend>(mode))==LT_OK &&
            sdk.ScaleSurfaceToSurface(screen,source,&rectangle,nullptr)==LT_OK && sdk.GetPixel(screen,4,4,&pixel)==LT_OK,"SDK optimized blend dispatch failed");
        near(pixel,expected[mode]);
    }
    require(sdk.SetOptimized2DBlend(LTSURFACEBLEND_ALPHA)==LT_OK && sdk.SetPixel(source,0,0,0xff0000)==LT_OK && sdk.FillRect(screen,nullptr,0x0000ff)==LT_OK &&
        sdk.SetSurfaceAlpha(source,.5f)==LT_OK && sdk.TransformSurfaceToSurface(screen,source,nullptr,2,2,0,8,8)==LT_OK &&
        sdk.GetPixel(screen,3,3,&pixel)==LT_OK,"SDK GPU affine draw failed"); near(pixel,0x800080);
    require(sdk.EndOptimized2D()==LT_OK,"SDK optimized block did not end");
    nolf::Image rgba(3,1,0); rgba.pixels()[0]=0xffff0000; rgba.pixels()[1]=0; rgba.pixels()[2]=0x80ffffff; const auto native_image=surfaces.create_image(rgba);
    require(native_image && sdk.FillRect(screen,nullptr,0x0000ff)==LT_OK,"native alpha image fixture failed"); auto glyph=std::make_shared<nolf::Surface>(std::move(rgba));
    surfaces.draw_image(screen,glyph,0,0); require(sdk.GetPixel(screen,0,0,&pixel)==LT_OK,"native image framebuffer read failed"); near(pixel,0xff0000);
    require(sdk.GetPixel(screen,1,0,&pixel)==LT_OK,"native image transparent read failed"); near(pixel,0x0000ff);
    require(sdk.GetPixel(screen,2,0,&pixel)==LT_OK,"native image half-alpha read failed"); near(pixel,0x8080ff);
    require(sdk.FillRect(screen,nullptr,0x0000ff)==LT_OK,"clipped native image clear failed"); surfaces.draw_image(screen,glyph,0,1,nolf::PixelRect{1,1,2,2});
    require(sdk.GetPixel(screen,0,1,&pixel)==LT_OK,"native image clip read failed"); near(pixel,0x0000ff); require(sdk.DeleteSurface(native_image)==LT_OK,"native alpha image retirement failed");
    require(sdk.DrawBitmapToSurface(screen,const_cast<char*>("missing.pcx"),nullptr,0,0)==LTFALSE,"missing bitmap reported success");
    require(sdk.SetPixel(screen,0,0,0xaabbcc)==LT_OK && (surfaces.screenshot().at(0,0)&0xffffff)==0xaabbcc,"SDK screenshot bypassed framebuffer");
    require(sdk.DeleteSurface(source)==LT_OK && sdk.DeleteSurface(copy)==LT_OK && surfaces.live_surfaces()==1,"SDK GPU surface retirement failed");
    renderer.collect();
    std::cout << "Native SDK surface calls reached OpenGL, including all seven blends and framebuffer readback\n";
}
#ifdef NOLF_TEST_SYSTEM_FONTS
static void sdk_font_checks(nolf::platform::Desktop& desktop,nolf::GlSurfaces& renderer,unsigned width,unsigned height) {
    auto surfaces=std::make_shared<nolf::ClientSurfaces>(nullptr,width,height,renderer); const auto image_scope=surfaces->activate();
    auto provider=nolf::platform::make_freetype_fonts(nolf::platform::system_font_locator()); nolf::ClientFonts fonts(provider,surfaces); const auto active=fonts.activate();
    const auto& api=nolf::ClientFonts::functions(); const auto& images=nolf::ClientSurfaces::functions(); char name[]="Arial"; const auto font=api.CreateFont(name,0,20,LTFALSE,LTFALSE,LTFALSE); require(font,"GPU font creation failed");
    char text[]="AV é"; const auto string=sb_CreateString(text); require(string,"GPU font native string creation failed"); struct StringRelease { HSTRING value; ~StringRelease() { sb_FreeString(value); } } release{string};
    int text_width=0,text_height=0; api.GetStringDimensions(font,string,&text_width,&text_height); require(text_width>0 && text_width+8<int(width) && text_height+8<int(height),"GPU font fixture exceeds display");
    const auto cpu=images.CreateSurface(width,height),screen=images.GetScreenSurface(); require(cpu && screen,"GPU font surface creation failed");
    auto compare=[&] { const auto frame=surfaces->screenshot(); for (unsigned y=0;y<height;++y) for (unsigned x=0;x<width;++x) { HLTCOLOR pixel=0; require(images.GetPixel(cpu,x,y,&pixel)==LT_OK && rgb(frame,x,y)==pixel,"native text CPU/GPU pixels differ"); } };
    auto draw=[&](HLTCOLOR fore,HLTCOLOR back,HLTCOLOR background,LTRect rectangle) {
        require(images.FillRect(cpu,nullptr,background)==LT_OK && images.FillRect(screen,nullptr,background)==LT_OK,"GPU font clear failed");
        api.DrawStringToSurface(cpu,font,string,&rectangle,fore,back); api.DrawStringToSurface(screen,font,string,&rectangle,fore,back); compare();
    };
    draw(0xff0000,COLOR_TRANSPARENCY_MASK,0x0000ff,{3,3,3,3});
    draw(COLOR_TRANSPARENCY_MASK,0x0000ff,0x00ff00,{-4,-2,-4,-2});
    draw(0xffffff,0x203040,0xff0000,{1,2,1,2});
    const auto generation=rd_DeviceGeneration(); rd_DestroyDevice(); int valid_width=0; api.GetStringDimensions(font,string,&valid_width,nullptr); require(valid_width==text_width,"device shutdown invalidated native font metrics");
    require(rd_CreateDevice(&desktop,width,height,true,0,1) && rd_DeviceGeneration()!=generation,"font device recreation failed");
    draw(0xff0000,COLOR_TRANSPARENCY_MASK,0x0000ff,{3,3,3,3});
    require(images.DeleteSurface(cpu)==LT_OK,"GPU font CPU surface did not retire"); api.DeleteFont(font); require(!fonts.live_fonts() && !fonts.cached_bytes(),"GPU font resources survived handle retirement"); renderer.collect();
    std::cout<<"Native SDK font CPU/GPU pixels match for Unicode, both transparency masks, clipping and renderer recreation\n";
}
#endif
int main(int argc,char** argv) {
    try {
        using namespace nolf;
        platform::DesktopConfig config; config.width=argc==3 ? 640 : 128; config.height=argc==3 ? 480 : 128; config.vsync=false;
        platform::Desktop desktop(config);
        require(rd_CreateDevice(&desktop,config.width,config.height,true,0,4),"surface render device initialization failed");
        struct Device { ~Device() { rd_DestroyDevice(); } } device;
        if (const auto* expected_driver=std::getenv("NOLF_EXPECT_VIDEO_DRIVER"); expected_driver && std::string_view(desktop.driver())!=expected_driver)
            throw std::runtime_error("SDL selected the wrong video backend");
        std::cout << "desktop=" << desktop.driver() << " renderer=" << rd_Describe() << '\n';
        const auto first_generation=rd_DeviceGeneration(); require(first_generation,"device generation missing");
        require(!rd_CreateDevice(&desktop,0,config.height,true,0,1) && rd_DeviceGeneration()==first_generation,
            "invalid render mode destroyed a working device");
        GlSurfaces renderer(config.width,config.height);
        unsigned untouched=0xdeadbeef;
        rejects([&] { rd_ReadPixels(-1,0,1,1,RD_PIX_BGRA,&untouched); });
        require(untouched==0xdeadbeef,"invalid framebuffer read changed destination storage");
        auto source=std::make_shared<Surface>(2,2); source->pixel(0,0,0xff0000); source->pixel(1,0,0x00ff00); source->pixel(0,1,0x0000ff); source->pixel(1,1,0xffffff);
        renderer.clear(std::nullopt,0x123456); renderer.draw(source,std::nullopt,{0,0,4,4});
        auto actual=renderer.read({0,0,8,8});
        require(rgb(actual,0,0)==0xff0000 && rgb(actual,3,0)==0x00ff00 && rgb(actual,0,3)==0x0000ff && rgb(actual,3,3)==0xffffff && rgb(actual,4,4)==0x123456,"GPU upload/nearest scaling/row order");
        SurfaceDraw keyed; keyed.color_key=0x80ff0000;
        renderer.clear(std::nullopt,0x123456); renderer.draw(source,std::nullopt,{0,0,2,2},keyed); actual=renderer.read({0,0,2,2});
        require(rgb(actual,0,0)==0x123456 && rgb(actual,1,0)==0x00ff00,"GPU color-key alpha");
        source->pixel(1,0,0xffff00); renderer.draw(source,std::nullopt,{0,0,2,2},keyed); actual=renderer.read({0,0,2,2});
        require(rgb(actual,1,0)==0xffff00 && renderer.cached_textures()==1,"surface edit did not invalidate the upload");
        keyed.solid=0x00ffff; renderer.draw(source,std::nullopt,{0,0,2,2},keyed); actual=renderer.read({0,0,2,2});
        require(rgb(actual,1,0)==0x00ffff && rgb(actual,0,0)==0x123456,"GPU solid-color text mask");
        auto mask=std::make_shared<Surface>(2,1); mask->pixel(0,0,0x111111); mask->pixel(1,0,0xeeeeee); keyed.solid.reset(); keyed.mask=mask;
        renderer.clear(std::nullopt,0x123456); renderer.draw(source,std::nullopt,{0,0,4,4},keyed); actual=renderer.read({0,0,4,4});
        require(rgb(actual,0,0)==0x123456 && rgb(actual,0,2)==0x111111 && rgb(actual,2,2)==0xeeeeee,"GPU tiled surface mask");
        mask->pixel(0,0,0xabcdef); renderer.draw(source,std::nullopt,{0,0,4,4},keyed); actual=renderer.read({0,0,4,4});
        require(rgb(actual,0,2)==0xabcdef,"edited mask was not uploaded again");
        renderer.clear(std::nullopt,0x123456); renderer.draw(source,PixelRect{-1,-1,1,1},{0,0,4,4}); actual=renderer.read({0,0,4,4});
        require(rgb(actual,0,0)==0x123456 && rgb(actual,1,2)==0x123456 && rgb(actual,2,2)==0xff0000,"GPU source clipping changed scale mapping");
        const std::array<WarpPoint,4> warp{{{-100,-100,0,0},{100,-100,2,0},{100,100,2,2},{-100,100,0,2}}};
        renderer.warp(source,warp); actual=renderer.read({0,0,2,2});
        require(rgb(actual,0,0)==0xff0000 && rgb(actual,1,1)==0xffffff,"GPU warp source clamp or sampling");
        auto red=std::make_shared<Surface>(1,1); red->pixel(0,0,0xff0000); red->alpha(.5f);
        renderer.clear(std::nullopt,0x0000ff); renderer.draw(red,std::nullopt,{0,0,16,16}); near(rgb(renderer.read({0,0,8,8}),4,4),0x800080);
        auto gray=std::make_shared<Surface>(1,1); gray->pixel(0,0,0x808080);
        constexpr std::array<unsigned,7> expected{0x808080,0x808080,0xc0c0c0,0x202020,0x404040,0x202020,0xa0a0a0};
        for (unsigned mode=0;mode<expected.size();++mode) {
            renderer.clear(std::nullopt,0x404040); SurfaceDraw style; style.blend=static_cast<SurfaceBlend>(mode);
            renderer.draw(gray,std::nullopt,{0,0,16,16},style); near(rgb(renderer.read({0,0,8,8}),4,4),expected[mode]);
        }
        auto get_integer=reinterpret_cast<void (*)(unsigned,int*)>(desktop.gl_symbol("glGetIntegerv"));
        auto get_float=reinterpret_cast<void (*)(unsigned,float*)>(desktop.gl_symbol("glGetFloatv"));
        auto enabled=reinterpret_cast<unsigned char (*)(unsigned)>(desktop.gl_symbol("glIsEnabled"));
        auto error=reinterpret_cast<unsigned (*)()>(desktop.gl_symbol("glGetError"));
        require(get_integer && get_float && enabled && error,"missing GL verification entry points");
        rd_Enable(RD_DEPTH_TEST); rd_Disable(RD_BLEND); rd_MatrixMode(RD_PROJECTION); rd_LoadIdentity(); rd_Ortho(-2,3,-4,5,1,9);
        if (g_RDCaps.nTextureUnits>1) rd_ActiveTexture(1);
        int previous_mode=0,previous_unit=0; std::array<float,16> previous_matrix{},after_matrix{};
        get_integer(0x0ba0,&previous_mode); get_integer(0x84e0,&previous_unit); get_float(0x0ba7,previous_matrix.data());
        renderer.draw(source,std::nullopt,{0,0,2,2});
        int after_mode=0,after_unit=0; get_integer(0x0ba0,&after_mode); get_integer(0x84e0,&after_unit); get_float(0x0ba7,after_matrix.data());
        require(after_mode==previous_mode && after_unit==previous_unit && after_matrix==previous_matrix && enabled(0x0b71) && !enabled(0x0be2),"2D surface draw leaked state into 3D rendering");
        require(!error(),"OpenGL reported a surface rendering error");
        auto foreign=std::async(std::launch::async,[&] { try { renderer.draw(source,std::nullopt,{0,0,2,2}); } catch (const std::logic_error&) { return true; } return false; });
        require(foreign.get(),"surface draw accepted a foreign render thread");
        {
            GlSurfaces bounded(config.width,config.height,16); auto other=std::make_shared<Surface>(2,2);
            bounded.draw(source,std::nullopt,{0,0,2,2}); bounded.draw(other,std::nullopt,{0,0,2,2}); require(bounded.cached_textures()==1,"surface cache exceeded its texture budget");
            other.reset(); bounded.collect(); require(bounded.cached_textures()==0,"expired CPU surface retained a GPU texture");
        }
        const auto orphan=rd_GenTexture(); rd_ActiveTexture(0); rd_BindTexture(orphan); const unsigned white=0xffffffff;
        rejects([&] { rd_TexImage(-1,RD_FMT_RGBA8,1,1,RD_PIX_BGRA,&white); });
        rd_TexImage(0,RD_FMT_RGBA8,1,1,RD_PIX_BGRA,&white);
        ClientSurfaces sdk_owner({},config.width,config.height,renderer); auto sdk_scope=sdk_owner.activate();
        const auto& sdk=ClientSurfaces::functions(); const auto sdk_source=sdk.CreateSurface(1,1),sdk_screen=sdk.GetScreenSurface();
        require(sdk_source && sdk.SetPixel(sdk_source,0,0,0xabcdef)==LT_OK,"SDK restart fixture source failed");
        rd_DestroyDevice(); require(!rd_DeviceGeneration(),"destroyed device remained active");
        rejects([&] { renderer.draw(source,std::nullopt,{0,0,2,2}); });
        require(sdk.DrawSurfaceToSurface(sdk_screen,sdk_source,nullptr,0,0)==LT_ERROR,"SDK draw succeeded without a render device");
        HLTCOLOR saved=0; require(sdk.GetPixel(sdk_source,0,0,&saved)==LT_OK && saved==0xabcdef,"device shutdown lost SDK-owned CPU pixels");
        require(desktop.make_current(),"cannot inspect preserved SDL context");
        auto is_texture=reinterpret_cast<unsigned char (*)(unsigned)>(desktop.gl_symbol("glIsTexture")); require(is_texture && !is_texture(orphan),"device shutdown retained owned GL textures"); desktop.release_context();
        require(rd_CreateDevice(&desktop,config.width,config.height,true,0,1) && rd_DeviceGeneration()!=first_generation,"surface device restart failed");
        renderer.clear(std::nullopt,0); renderer.draw(source,std::nullopt,{0,0,2,2}); actual=renderer.read({0,0,2,2});
        require(rgb(actual,1,0)==0xffff00 && renderer.cached_textures()==1,"CPU surface did not survive device restart");
        require(sdk.GetScreenSurface()==sdk_screen && sdk.DrawSurfaceToSurface(sdk_screen,sdk_source,nullptr,0,0)==LT_OK &&
            sdk.GetPixel(sdk_screen,0,0,&saved)==LT_OK && saved==0xabcdef,"SDK handles did not survive device recreation");
        require(sdk.DeleteSurface(sdk_source)==LT_OK,"SDK restart source retirement failed"); renderer.collect();
        sdk_surface_checks(renderer,config.width,config.height);
#ifdef NOLF_TEST_SYSTEM_FONTS
        sdk_font_checks(desktop,renderer,config.width,config.height);
#endif
        if (argc==3) {
            auto assets=std::make_shared<Assets>(); assets->mount_game(argv[1]); const auto image=decode_pcx(assets->read("menu/art/splash.pcx"));
            ClientSurfaces surfaces(assets,config.width,config.height,renderer); auto scope=surfaces.activate(); const auto& sdk=ClientSurfaces::functions();
            const auto splash=sdk.CreateSurfaceFromBitmap(const_cast<char*>("menu/art/splash.pcx")),screen=sdk.GetScreenSurface();
            require(splash && sdk.FillRect(screen,nullptr,0)==LT_OK && sdk.DrawSurfaceToSurface(screen,splash,nullptr,0,0)==LT_OK,"original splash SDK draw failed");
            auto frame=surfaces.screenshot();
            require(frame.width()==image.width() && frame.height()==image.height(),"original splash dimensions changed");
            for (std::size_t i=0;i<frame.pixels().size();++i) require((frame.pixels()[i]&0xffffff)==(image.pixels()[i]&0xffffff),"original PCX changed during SDK/OpenGL presentation");
            platform::atomic_write(argv[2],encode_bmp(frame)); require(sdk.DeleteSurface(splash)==LT_OK,"original splash SDK release failed");
            std::cout << "Original 640x480 splash presented through SDK calls and read back exactly\n";
        }
        std::cout << "Native OpenGL surface checks passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
