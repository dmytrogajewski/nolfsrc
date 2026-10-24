#include "engine/client/surface.h"
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

static void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F action) { bool rejected=false; try { action(); } catch (const std::exception&) { rejected=true; } require(rejected,"invalid surface operation accepted"); }
static nolf::Image pattern() { nolf::Image image(2,2); image.pixels()[0]=0xffff0000; image.pixels()[1]=0xff00ff00; image.pixels()[2]=0xff0000ff; image.pixels()[3]=0xffffffff; return image; }
int main() {
    try {
        using namespace nolf;
        Surface source(pattern()),destination(4,4);
        Image rgba(3,1,0); rgba.pixels()[0]=0xffff0000; rgba.pixels()[1]=0x0000ff00; rgba.pixels()[2]=0x80ffffff; Surface glyphs(std::move(rgba));
        Surface alpha_target(4,1); alpha_target.fill(std::nullopt,0x0000ff); alpha_target.composite(glyphs,0,0);
        require(alpha_target.image().at(0,0)==0xffff0000 && alpha_target.image().at(1,0)==0xff0000ff && alpha_target.image().at(2,0)==0xff8080ff,"native image composition lost straight alpha or transparency");
        alpha_target.fill(std::nullopt,0x0000ff); alpha_target.composite(glyphs,-1,0,PixelRect{0,0,1,1}); require(alpha_target.image().at(0,0)==0xff0000ff && alpha_target.image().at(1,0)==0xff0000ff,"clipped native image wrote outside destination rectangle");
        Surface transparent(Image(3,1,0)); transparent.composite(glyphs,0,0); require(transparent.image().at(1,0)==0 && transparent.image().at(2,0)==0x80ffffff,"transparent destination changed straight-alpha source channels");
        Image self_pixels(3,1,0); self_pixels.pixels()[0]=0x80ff0000; self_pixels.pixels()[1]=0x800000ff; Surface self(std::move(self_pixels)); self.composite(self,1,0);
        require(self.image().at(1,0)==0xc0aa0055 && self.image().at(2,0)==0x800000ff,"overlapping image composition read pixels it had modified");
        const auto alpha_revision=alpha_target.revision(); rejects([&] { alpha_target.composite(glyphs,std::numeric_limits<int>::max(),0); }); require(alpha_target.revision()==alpha_revision,"invalid image coordinates changed target revision");
        destination.blit(source,std::nullopt,{0,0,4,4});
        for (unsigned y=0;y<4;++y) for (unsigned x=0;x<4;++x) require(destination.pixel(x,y)==source.pixel(x/2,y/2),"nearest scaling changed original pixel blocks");
        destination.fill(std::nullopt,0); destination.blit(source,std::nullopt,{-2,-2,2,2});
        require(destination.pixel(0,0)==0xffffff && destination.pixel(1,1)==0xffffff && destination.pixel(2,2)==0,"clipped scaling shifted source mapping");
        destination.fill(std::nullopt,0); destination.blit(source,PixelRect{-1,-1,1,1},{0,0,4,4});
        require(destination.pixel(0,0)==0 && destination.pixel(1,2)==0 && destination.pixel(2,2)==0xff0000,"outside source rectangle did not clip proportionally");
        Surface overlap(5,1); for (unsigned x=0;x<5;++x) overlap.pixel(x,0,x+1);
        overlap.blit(overlap,PixelRect{0,0,4,1},{1,0,5,1});
        for (unsigned x=1;x<5;++x) require(overlap.pixel(x,0)==x,"overlapping blit read its modified destination");
        Surface borders(6,5); borders.fill(PixelRect{2,1,4,3},0xff00ff); const auto border=borders.border(0);
        require(border.left==2 && border.top==1 && border.right==2 && border.bottom==2,"surface border margins");
        SurfacePaint keyed; keyed.color_key=0x80ff0000;
        destination.fill(std::nullopt,0x123456); destination.blit(source,std::nullopt,{0,0,2,2},keyed);
        require(destination.pixel(0,0)==0x123456 && destination.pixel(1,0)==0x00ff00,"color key compared transparency flags as RGB");
        keyed.solid=0xffff00; destination.blit(source,std::nullopt,{0,0,2,2},keyed);
        require(destination.pixel(1,1)==0xffff00 && destination.pixel(0,0)==0x123456,"solid fill did not preserve transparent source");
        Surface mask(2,1); mask.pixel(0,0,0x111111); mask.pixel(1,0,0xeeeeee); keyed.solid.reset(); keyed.mask=&mask;
        destination.blit(source,std::nullopt,{0,0,4,4},keyed);
        require(destination.pixel(0,2)==0x111111 && destination.pixel(2,2)==0xeeeeee,"mask did not tile in source coordinates");
        Surface bad_mask(3,1); keyed.mask=&bad_mask; const auto revision=destination.revision();
        rejects([&] { destination.blit(source,std::nullopt,{0,0,2,2},keyed); }); require(destination.revision()==revision,"invalid draw changed surface revision");
        destination.fill(std::nullopt,0); destination.transform(source,std::nullopt,1,1,0,1,1);
        for (unsigned y=0;y<2;++y) for (unsigned x=0;x<2;++x) require(destination.pixel(x+1,y+1)==source.pixel(x,y),"identity affine warp changed pixel order");
        destination.fill(std::nullopt,0); destination.transform(source,std::nullopt,1,1,std::acos(-1.0)/2,1,1);
        require(destination.pixel(1,1)==0x0000ff && destination.pixel(2,1)==0xff0000 && destination.pixel(1,2)==0xffffff,"affine rotation lost the origin or winding");
        const std::array<WarpPoint,4> clamped{{{-100,-100,0,0},{100,-100,2,0},{100,100,2,2},{-100,100,0,2}}};
        destination.warp(source,clamped); require(destination.pixel(0,0)==0xff0000 && destination.pixel(1,1)==0xffffff,"warp source vertices were not clamped before interpolation");
        auto invalid=clamped; invalid[1].dest_x=std::numeric_limits<double>::quiet_NaN(); rejects([&] { destination.warp(source,invalid); });
        invalid=clamped; invalid[2].dest_x=-1; rejects([&] { destination.warp(source,invalid); });
        rejects([&] { destination.alpha(std::numeric_limits<float>::infinity()); }); rejects([&] { destination.pixel(99,99,0); });
        destination.fill(PixelRect{std::numeric_limits<int>::min(),std::numeric_limits<int>::min(),std::numeric_limits<int>::max(),std::numeric_limits<int>::max()},0xabcdef);
        require(destination.pixel(3,3)==0xabcdef,"extreme rectangle clipping overflowed");
        destination.blit(source,std::nullopt,{std::numeric_limits<int>::min(),std::numeric_limits<int>::min(),std::numeric_limits<int>::max(),std::numeric_limits<int>::max()});
        require(destination.pixel(0,0)==0xffffff,"extreme scale arithmetic overflowed");
        require(source.identity()!=destination.identity(),"surface identity reused");
        std::cout << "Native surface operations passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
