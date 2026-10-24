#include "engine/portable/texture.h"
#include "engine/portable/assets.h"
#include "engine/portable/checksum.h"
#include "engine/portable/wire.h"
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <limits>
#include <stdexcept>

namespace {
using Bytes=std::vector<std::byte>;
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F&& call) { bool rejected=false; try { call(); } catch (const std::exception&) { rejected=true; } require(rejected,"malformed DTX accepted"); }
Bytes fixture(unsigned format,unsigned width,unsigned height,Bytes data,unsigned mips=1,unsigned sections=0) {
    Bytes file(164); nolf::wire::store(file.data()+4,std::int32_t(-5));
    nolf::wire::store(file.data()+8,std::uint16_t(width)); nolf::wire::store(file.data()+10,std::uint16_t(height));
    nolf::wire::store(file.data()+12,std::uint16_t(mips)); nolf::wire::store(file.data()+14,std::uint16_t(sections));
    nolf::wire::store(file.data()+16,std::uint32_t(8)); file[26]=std::byte(format);
    file.insert(file.end(),data.begin(),data.end()); return file;
}
Bytes words(std::initializer_list<std::uint32_t> values) {
    Bytes bytes; for (auto value : values) { auto at=bytes.size(); bytes.resize(at+4); nolf::wire::store(bytes.data()+at,value); } return bytes;
}
void pixels() {
    auto file=fixture(3,2,2,words({0x11223344,0x55667788,0,0xff123456}));
    nolf::wire::store(file.data()+20,std::uint32_t(0x12345678)); nolf::wire::store(file.data()+30,1.5f);
    nolf::wire::store(file.data()+34,std::int16_t(-45)); file[36]=std::byte{'x'};
    auto texture=nolf::decode_dtx(file);
    require(texture.mips.size()==1 && texture.mips[0].at(0,0)==0x11223344 && texture.mips[0].at(0,1)==0 && texture.header.user_flags==0x12345678 && texture.header.detail_scale==2.5f && texture.header.detail_angle==-45 && texture.header.commands=="x","DTX raw channels/alpha or metadata changed");
    file[26]=std::byte{0}; auto old=nolf::decode_dtx(file);
    require(old.header.format==nolf::TextureFormat::Rgba && old.mips[0].at(1,1)==0xff123456,"legacy zero DTX format did not normalize to raw32");
    file.clear(); file.shrink_to_fit(); require(texture.mips[0].at(1,0)==0x55667788,"decoded DTX borrowed input pixel storage");
    auto mip=fixture(3,2,2,words({1,2,3,4,5}),2); auto chain=nolf::decode_dtx(mip);
    require(chain.mips.size()==2 && chain.mips[1].width()==1 && chain.mips[1].at(0,0)==5,"DTX mip offset/dimensions");
}
void blocks() {
    Bytes block(8); nolf::wire::store(block.data(),std::uint16_t(0xf800)); nolf::wire::store(block.data()+2,std::uint16_t(0x001f));
    nolf::wire::store(block.data()+4,std::uint32_t(0xe4e4e4e4));
    auto decoded=nolf::decode_dtx(fixture(4,4,4,block));
    require(decoded.mips[0].at(0,0)==0xffff0000 && decoded.mips[0].at(1,0)==0xff0000ff && decoded.mips[0].at(2,0)==0xffaa0055 && decoded.mips[0].at(3,0)==0xff5500aa,"DXT1 color endpoint/interpolation");
    nolf::wire::store(block.data(),std::uint16_t(0x001f)); nolf::wire::store(block.data()+2,std::uint16_t(0xf800));
    decoded=nolf::decode_dtx(fixture(4,4,4,block));
    require(decoded.mips[0].at(2,0)==0xff7f007f && decoded.mips[0].at(3,0)==0,"DXT1 three-color/transparent selector");
    auto edge=nolf::decode_dtx(fixture(4,1,3,block)); require(edge.mips[0].pixels().size()==3 && edge.mips[0].at(0,2)==0xff0000ff,"DXT edge block crop");
    Bytes explicit_alpha(16); for (unsigned i=0;i<16;++i) explicit_alpha[i/2]|=std::byte(i<<((i%2)*4));
    nolf::wire::store(explicit_alpha.data()+8,std::uint16_t(0x001f)); nolf::wire::store(explicit_alpha.data()+10,std::uint16_t(0xf800)); nolf::wire::store(explicit_alpha.data()+12,std::uint32_t(0xffffffff));
    auto dxt3=nolf::decode_dtx(fixture(5,4,4,explicit_alpha));
    for (unsigned i=0;i<16;++i) require(dxt3.mips[0].pixels()[i]==((std::uint32_t(i*17)<<24)|0xaa0055),"DXT3 alpha or forced four-color interpolation");
    for (bool reversed : {false,true}) {
        Bytes interpolated(16); interpolated[0]=reversed ? std::byte{0} : std::byte{255}; interpolated[1]=reversed ? std::byte{255} : std::byte{0};
        std::uint64_t selectors=0; for (unsigned i=0;i<16;++i) selectors|=std::uint64_t(i%8)<<(3*i);
        for (unsigned i=0;i<6;++i) interpolated[i+2]=std::byte((selectors>>(8*i))&255);
        nolf::wire::store(interpolated.data()+8,std::uint16_t(0x07e0));
        auto dxt5=nolf::decode_dtx(fixture(6,4,4,interpolated));
        const std::array<unsigned,8> expected=reversed ? std::array<unsigned,8>{0,255,51,102,153,204,0,255} : std::array<unsigned,8>{255,0,218,182,145,109,72,36};
        for (unsigned i=0;i<16;++i) require(dxt5.mips[0].pixels()[i]==((expected[i%8]<<24)|0x00ff00),"DXT5 alpha endpoints/selectors/interpolation");
    }
}
void palette() {
    Bytes payload{std::byte{0},std::byte{1},std::byte{2},std::byte{255}};
    Bytes section(32,std::byte{0xfe}); const std::string type="PALLETE32";
    for (std::size_t i=0;i<type.size();++i) section[i]=std::byte(type[i]); section[type.size()]=std::byte{0};
    nolf::wire::store(section.data()+28,std::uint32_t(1024));
    for (std::uint32_t i=0;i<256;++i) { auto at=section.size(); section.resize(at+4); nolf::wire::store(section.data()+at,0x80000000u|i*0x010101u); }
    payload.insert(payload.end(),section.begin(),section.end()); auto file=fixture(7,2,2,payload,1,1);
    const auto decoded=nolf::decode_dtx(file);
    require(decoded.sections.size()==1 && decoded.sections[0].type==type && decoded.sections[0].data.size()==1024 && decoded.mips[0].at(1,0)==0x80010101 && decoded.mips[0].at(1,1)==0x80ffffff,"DTX packed palette channel order, alpha or section padding");
    auto duplicate=file; duplicate.insert(duplicate.end(),section.begin(),section.end()); nolf::wire::store(duplicate.data()+14,std::uint16_t(2)); rejects([&] { nolf::decode_dtx(duplicate); });
    file.resize(file.size()-1); rejects([&] { nolf::decode_dtx(file); });
    rejects([&] { nolf::decode_dtx(fixture(7,2,2,Bytes(4))); });
}
void malformed() {
    auto valid=fixture(3,2,2,words({1,2,3,4}));
    for (std::size_t size=0;size<valid.size();++size) rejects([&] { nolf::decode_dtx(std::span(valid).first(size)); });
    for (std::size_t offset : {0u,4u,8u,10u,12u,14u,26u}) {
        auto bad=valid; bad[offset]=std::byte{255}; bad[offset+1]=std::byte{255}; rejects([&] { nolf::decode_dtx(bad); });
    }
    auto nan=valid; nolf::wire::store(nan.data()+30,std::numeric_limits<float>::quiet_NaN()); rejects([&] { nolf::decode_dtx(nan); });
    auto legacy=valid; nolf::wire::store(legacy.data()+16,std::uint32_t(0)); nolf::wire::store(legacy.data()+14,std::uint16_t(65535));
    require(nolf::decode_dtx(legacy).sections.empty(),"unfixed legacy DTX section count was trusted");
    auto excessive=valid; nolf::wire::store(excessive.data()+8,std::uint16_t(16384)); nolf::wire::store(excessive.data()+10,std::uint16_t(16384)); rejects([&] { nolf::decode_dtx(excessive); });
}
void corpus(const char* data,const char* report,const char* exported) {
    nolf::Assets assets; assets.mount_game(data); std::ofstream output;
    if (report) { output.open(report); if (!output) throw std::runtime_error("cannot open texture report"); }
    unsigned count=0,mips=0; std::map<unsigned,unsigned> formats;
    for (const auto& name : assets.names()) if (name.ends_with(".dtx")) {
        try {
            const auto bytes=assets.read(name); const auto texture=nolf::decode_dtx(bytes);
            if (exported) nolf::platform::atomic_write(std::filesystem::path(exported)/nolf::platform::asset_key(name),bytes);
            ++formats[texture.header.storage_format]; ++count;
            for (std::size_t mip=0;mip<texture.mips.size();++mip) {
                const auto& image=texture.mips[mip]; nolf::Crc32 crc; crc.update(std::as_bytes(image.pixels())); ++mips;
                if (output.is_open()) output<<name<<'\t'<<mip<<'\t'<<image.width()<<'\t'<<image.height()<<'\t'<<crc.value()<<'\n';
            }
        } catch (const std::exception& error) { throw std::runtime_error(name+": "+error.what()); }
    }
    require(count,"no original DTX textures found");
    if (output.is_open()) { output.flush(); if (!output) throw std::runtime_error("texture report write failed"); }
    for (auto [format,number] : formats) std::cout<<"DTX format "<<format<<": "<<number<<'\n';
    std::cout<<"Decoded "<<count<<" original DTX textures, "<<mips<<" mip images\n";
}
}
int main(int argc,char** argv) {
    try {
        pixels(); blocks(); palette(); malformed(); const char *data=nullptr,*report=nullptr,*exported=nullptr;
        for (int i=1;i<argc;++i) {
            const std::string_view flag=argv[i]; if (++i==argc) throw std::runtime_error("texture test option needs a path");
            if (flag=="--data") data=argv[i]; else if (flag=="--report") report=argv[i]; else if (flag=="--export-dtx") exported=argv[i]; else throw std::runtime_error("unknown texture test option");
        }
        if (!data && (report || exported)) throw std::runtime_error("texture reports/exports require --data");
        if (data) corpus(data,report,exported);
        std::cout<<"Native DTX pixels, compression, palettes, metadata and bounds passed\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
