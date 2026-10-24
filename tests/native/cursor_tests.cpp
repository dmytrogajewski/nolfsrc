#include "engine/portable/cursor.h"
#include "engine/portable/resources.h"
#include "cursor_fixture.h"
#include <algorithm>
#include <iostream>

namespace {
void require(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
template<class F> void rejects(F&& fn) { bool rejected=false; try { fn(); } catch (const std::exception&) { rejected=true; } require(rejected,"invalid cursor accepted"); }
void checks(const char* data) {
    for (unsigned depth : {1,4,8,24,32}) for (bool top_down : {false,true}) {
        const auto bytes=cursor_fixture::bitmap(depth,top_down); const auto image=nolf::decode_cursor(bytes);
        require(image.width==8 && image.height==2 && image.hot_x==1 && image.hot_y==0,"cursor dimensions/hotspot differ");
        require(image.rgba[0]==0xff000000 && image.rgba[1]==0xffffffff && image.rgba[2]==0 && image.rgba[3]==0xffffff && image.rgba[8]==0xff000000,"cursor palette/rows/AND plane differ");
        require(image.mono_data[0]==0x9f && image.mono_mask[0]==0xcf && image.mono_data[1]==0xff && image.mono_mask[1]==0xff && image.color_xor,"monochrome cursor black/white/transparent/invert truth table differs");
        const auto group=cursor_fixture::group(bytes.size(),depth); const auto entries=nolf::decode_cursor_group(group);
        require(entries.size()==1 && entries[0].width==8 && entries[0].height==4 && entries[0].depth==depth && entries[0].bytes==bytes.size() && entries[0].id==1,"cursor group directory differs");
        for (std::size_t size=0;size<bytes.size();++size) rejects([&] { nolf::decode_cursor(std::span(bytes).first(size)); });
        for (std::size_t size=0;size<group.size();++size) rejects([&] { nolf::decode_cursor_group(std::span(group).first(size)); });
        auto bad=bytes; bad[0]=std::byte{8}; rejects([&] { nolf::decode_cursor(bad); });
        bad=bytes; bad[20]=std::byte{1}; rejects([&] { nolf::decode_cursor(bad); });
        bad=bytes; bad.push_back(std::byte{}); rejects([&] { nolf::decode_cursor(bad); });
    }
    const auto alpha=nolf::decode_cursor(cursor_fixture::bitmap(32,false,true)); require(alpha.rgba[0]==0x80000000 && alpha.rgba[3]==0x80ffffff && alpha.mono_data.empty() && !alpha.color_xor,"native alpha cursor fell back to monochrome/AND semantics");
    if (data) {
        const nolf::Resources resources(data); const auto entries=nolf::decode_cursor_group(resources.raw("cres","12","104"));
        require(entries.size()==2,"original pointer cursor directory changed"); unsigned color=0,mono=0;
        for (const auto& entry : entries) {
            const auto bytes=resources.raw("cres","1",std::to_string(entry.id)); const auto image=nolf::decode_cursor(bytes);
            require(bytes.size()==entry.bytes && image.width==32 && image.height==32,"original pointer dimensions changed");
            const auto opaque=std::count_if(image.rgba.begin(),image.rgba.end(),[](auto pixel) { return pixel>>24; }); require(opaque==352,"original pointer opaque silhouette differs");
            if (image.mono_data.empty()) { require(image.color_xor && image.hot_x==1 && image.hot_y==1,"original color XOR/hotspot missing"); ++color; }
            else { require(!image.color_xor && image.hot_x==0 && image.hot_y==0,"original compatible mono pointer differs"); ++mono; }
        }
        require(color==1 && mono==1,"original cursor alternatives missing"); std::cout<<"Original CRES pointer: color XOR and compatible monochrome, 352 opaque pixels each\n";
    }
}
}
int main(int argc,char** argv) { try { checks(argc>1 ? argv[1] : nullptr); std::cout<<"Native cursor directory, DIB palettes, masks, alpha, orientation and malformed resources passed\n"; return 0; } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
