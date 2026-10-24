#include "platform/fonts.h"
#include <algorithm>
#include <future>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
template<class F> void rejects(F&& f) { bool rejected=false; try { f(); } catch (const std::exception&) { rejected=true; } require(rejected,"invalid font request accepted"); }
void checks() {
    auto locator=nolf::platform::system_font_locator(); auto fonts=nolf::platform::make_freetype_fonts(locator);
    nolf::platform::FontRequest request; request.family="Arial"; request.height=16; auto face=fonts->load(request);
    require(face && !face->family().empty() && face==fonts->load(request),"native font match/cache failed"); const auto metrics=face->metrics();
    require(metrics.height==16 && metrics.ascent>0 && metrics.ascent+metrics.descent==16 && metrics.underline_y<metrics.height && metrics.underline_thickness>0,"native font cell metrics differ");
    const auto a=face->glyph('A'),v=face->glyph('V'),space=face->glyph(' '),accent=face->glyph(0xe9);
    require(a.index && a.width>0 && a.height>0 && a.advance>0 && a.coverage.size()==std::size_t(a.width)*a.height && std::find(a.coverage.begin(),a.coverage.end(),255)!=a.coverage.end(),"native glyph bitmap/advance missing");
    require(std::all_of(a.coverage.begin(),a.coverage.end(),[](auto value) { return value==0 || value==255; }) && accent.index && accent.advance>0 && space.advance>0 && std::all_of(space.coverage.begin(),space.coverage.end(),[](auto value) { return value==0; }),"native Unicode/monochrome/space glyph contract differs");
    const auto kerning=face->kerning(a.index,v.index); require(kerning<=0 && kerning>=-metrics.height,"native font pair spacing out of range");
    request.bold=request.italic=true; auto styled=fonts->load(request); require(styled && styled!=face && styled->glyph('W').advance>0,"native styled font not matched");
    request={}; request.family="monospace"; auto mono=fonts->load(request); require(mono->glyph('i').advance==mono->glyph('W').advance,"monospace discovery did not match fixed width");
    request.family="Arial"; request.width=12; auto wide=fonts->load(request); int average=0; unsigned count=0;
    for (unsigned code='A';code<='z';++code) if (code<='Z' || code>='a') { average+=wide->glyph(code).advance; ++count; }
    require(std::abs(double(average)/count-12)<1,"native font requested average width not applied");
    request.width=0; request.height=-16; auto nominal=fonts->load(request); require(nominal->metrics().height>=16 && nominal->glyph('A').height>0,"negative character-height font sizing failed");
    request.height=0; rejects([&] { fonts->load(request); }); request.height=513; rejects([&] { fonts->load(request); }); request.height=16; request.width=-1; rejects([&] { fonts->load(request); }); request.width=0; request.family=std::string(257,'x'); rejects([&] { fonts->load(request); });
    rejects([&] { face->glyph(0xd800); }); rejects([&] { face->glyph(0x110000); });
    auto thread=std::async(std::launch::async,[&] { face->glyph('A'); }); rejects([&] { thread.get(); });
    request={}; request.family="NOLF missing family fallback"; require(fonts->load(request)->glyph('A').advance>0,"closest native font fallback failed");
    locator.reset(); fonts.reset(); require(face->glyph('B').advance>0 && face->family().size()>0,"native font bytes/library did not survive provider retirement");
    std::cout<<"Native Fontconfig/FreeType: "<<face->family()<<", cell="<<metrics.height<<", baseline="<<metrics.ascent<<", A="<<a.width<<'x'<<a.height<<", kerning(AV)="<<kerning<<'\n';
}
}
int main() { try { checks(); std::cout<<"Native font discovery, sizing, Unicode glyphs, styles, widths, thread ownership and retained rasterizer lifetime passed\n"; return 0; } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
