#include "engine/portable/image.h"
#include "engine/portable/binary.h"
#include "engine/portable/assets.h"
#include "engine/portable/checksum.h"
#include <array>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>

static void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F action) { bool rejected=false; try { action(); } catch (const std::exception&) { rejected=true; } require(rejected,"invalid image accepted"); }
static std::vector<std::byte> pcx(bool rgb,bool compressed) {
    std::vector<std::byte> file(128);
    auto put=[&](unsigned offset,unsigned value) { file[offset]=std::byte(value&255); };
    put(0,10); put(1,5); put(2,compressed); put(3,8);
    put(8,rgb ? 1 : 2); put(10,1); put(65,rgb ? 3 : 1); put(66,4);
    const std::vector<unsigned char> rows=rgb ? std::vector<unsigned char>{255,0,42,42,0,255,42,42,0,0,42,42,0,255,42,42,0,255,42,42,255,255,42,42} :
        std::vector<unsigned char>{1,2,3,0,255,2,1,0};
    for (auto value : rows) { if (compressed && value>=192) file.push_back(std::byte{0xc1}); file.push_back(std::byte(value)); }
    if (!rgb) {
        file.push_back(std::byte{12});
        for (unsigned i=0;i<256;++i) for (unsigned channel=0;channel<3;++channel)
            file.push_back(std::byte(i==255 || i==channel+1 ? 255 : 0));
    }
    return file;
}
int main(int argc,char** argv) {
    try {
        for (bool compressed : {false,true}) for (bool rgb : {false,true}) {
            const auto data=pcx(rgb,compressed); const auto image=nolf::decode_pcx(data);
            require(image.width()==(rgb ? 2u : 3u) && image.height()==2,"PCX image dimensions");
            require(image.at(0,0)==0xffff0000 && image.at(1,0)==0xff00ff00,"PCX palette/planar channel order");
            require(image.at(0,1)==(rgb ? 0xff0000ff : 0xffffffff) && image.at(image.width()-1,1)==(rgb ? 0xffffffff : 0xffff0000),"PCX pitch padding or row order");
            for (std::size_t size=0;size<data.size();++size) rejects([&] { nolf::decode_pcx(std::span(data).first(size)); });
            auto bad=data; bad[3]=std::byte{4}; rejects([&] { nolf::decode_pcx(bad); });
            bad=data; bad[66]=std::byte{1}; rejects([&] { nolf::decode_pcx(bad); });
            bad=data; bad[8]=std::byte{0xff}; bad[9]=std::byte{0xff}; rejects([&] { nolf::decode_pcx(bad); });
        }
        {
            const auto original=pcx(false,true);
            for (std::size_t byte=0;byte<original.size();++byte) {
                auto mutation=original; mutation[byte]^=std::byte{0xff};
                std::optional<nolf::Image> decoded;
                try { decoded.emplace(nolf::decode_pcx(mutation)); }
                catch (const std::runtime_error&) {} catch (const std::length_error&) {}
                if (decoded) require(decoded->pixels().size()<=nolf::Image::maximum_pixels,"PCX mutation exceeded allocation budget");
            }
        }
        auto bad=pcx(false,true); bad[128]=std::byte{0xc0}; rejects([&] { nolf::decode_pcx(bad); });
        bad=pcx(false,true); bad[128]=std::byte{0xff}; rejects([&] { nolf::decode_pcx(bad); });
        auto crossing=pcx(false,true); crossing[8]=std::byte{1}; crossing[66]=std::byte{2};
        crossing.erase(crossing.begin()+128,crossing.end()-769);
        crossing.insert(crossing.begin()+128,{std::byte{0xc4},std::byte{1}});
        const auto uniform=nolf::decode_pcx(crossing);
        require(uniform.at(1,1)==0xffff0000,"PCX run across row boundary");
        nolf::Image image(2,2); image.pixels()[0]=0xffff0000; image.pixels()[1]=0xff00ff00; image.pixels()[2]=0xff0000ff; image.pixels()[3]=0xffffffff;
        nolf::Image moved(std::move(image)); require(!image.width() && !image.height(),"moved image retained a usable extent without pixel storage");
        rejects([&] { image.at(0,0); }); rejects([&] { nolf::encode_bmp(image); });
        image=moved; moved.pixels()[0]=0xff123456; require(image.at(0,0)==0xffff0000,"image copy retained aliased pixel ownership");
        const auto bmp=nolf::encode_bmp(image); nolf::BinaryReader header(bmp);
        require(header.integer<std::uint16_t>()==0x4d42 && header.integer<std::uint32_t>()==70 && bmp.size()==70,"BMP header layout");
        require(bmp[54]==std::byte{255} && bmp[55]==std::byte{0} && bmp[56]==std::byte{0} && bmp[62]==std::byte{0} && bmp[64]==std::byte{255},"BMP bottom-up BGR rows");
        rejects([] { nolf::Image invalid(0,1); }); rejects([] { nolf::Image invalid(16384,16384); }); rejects([&] { image.at(2,0); });
        if (argc==3) {
            nolf::Assets assets; assets.mount_game(argv[1]); std::ofstream report(argv[2]);
            if (!report) throw std::runtime_error("cannot create image corpus report");
            unsigned count=0;
            for (const auto& name : assets.names()) if (name.ends_with(".pcx")) {
                const auto decoded=nolf::decode_pcx(assets.read(name)); nolf::Crc32 checksum; checksum.update(std::as_bytes(decoded.pixels()));
                report << name << '\t' << decoded.width() << '\t' << decoded.height() << '\t' << checksum.value() << '\n'; ++count;
            }
            require(count>200,"original PCX corpus missing"); std::cout << count << " original PCX files decoded\n";
        }
        std::cout << "Native image checks passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
