#include "platform/audio.h"
#include "engine/portable/binary.h"
#include "engine/portable/media_source.h"
#include "engine/portable/wave.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <future>
#include <iostream>
#include <map>

namespace {
void require(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
template<class F> void rejects(F&& fn) { bool rejected=false; try { fn(); } catch (const std::exception&) { rejected=true; } require(rejected,"invalid audio operation accepted"); }
struct Memory final : nolf::platform::MediaSource {
    std::vector<std::byte> bytes;
    bool fail=false;
    std::uint64_t size() const override { return bytes.size(); }
    void read(std::uint64_t offset,std::span<std::byte> output) const override {
        if (fail) throw std::runtime_error("native audio source fixture failure");
        if (offset>bytes.size() || output.size()>bytes.size()-std::size_t(offset)) throw std::out_of_range("fixture read exceeds media extent");
        std::copy_n(bytes.data()+offset,output.size(),output.data());
    }
};
void four(nolf::BinaryWriter& output,const char* id) { output.bytes(std::as_bytes(std::span(id,4))); }
std::shared_ptr<Memory> wave(unsigned depth=16,unsigned channels=1,unsigned frames=1000,bool ima=false) {
    nolf::BinaryWriter body; four(body,"WAVE"); four(body,"fmt "); body.integer<std::uint32_t>(ima ? 20 : 16);
    body.integer<std::uint16_t>(ima ? 17 : 1); body.integer<std::uint16_t>(channels); body.integer<std::uint32_t>(8000);
    body.integer<std::uint32_t>(ima ? 7111 : 8000*channels*depth/8); body.integer<std::uint16_t>(ima ? 8 : channels*depth/8); body.integer<std::uint16_t>(ima ? 4 : depth);
    if (ima) { body.integer<std::uint16_t>(2); body.integer<std::uint16_t>(9); four(body,"fact"); body.integer<std::uint32_t>(4); body.integer<std::uint32_t>(9); }
    four(body,"data"); body.integer<std::uint32_t>(ima ? 8 : frames*channels*depth/8);
    if (ima) { body.integer<std::uint16_t>(1000); body.integer<std::uint16_t>(0); body.integer<std::uint32_t>(0); }
    else for (unsigned i=0;i<frames;++i) for (unsigned c=0;c<channels;++c) {
        const auto index=(i+c)%5; constexpr std::array<std::int16_t,5> samples{-32768,-16384,0,16384,32767};
        if (depth==8) body.integer<std::uint8_t>(std::uint8_t((int(samples[index])+32768)/256));
        else body.integer<std::uint16_t>(std::bit_cast<std::uint16_t>(samples[index]));
    }
    if (body.data().size()%2) body.integer<std::uint8_t>(0);
    nolf::BinaryWriter file; four(file,"RIFF"); file.integer<std::uint32_t>(body.data().size()); file.bytes(body.data()); auto result=std::make_shared<Memory>(); result->bytes=file.data(); return result;
}
std::vector<float> decode(nolf::platform::AudioDecoder& decoder,std::size_t limit=48000*60*30) {
    const auto channels=decoder.format().channels; std::array<float,4096> block{}; std::vector<float> result;
    while (const auto frames=decoder.read(std::span(block).first(block.size()/channels*channels))) {
        require(result.size()+frames*channels<=limit*channels,"audio fixture exceeds duration limit"); result.insert(result.end(),block.begin(),block.begin()+frames*channels);
    }
    return result;
}
void checks() {
    for (unsigned depth : {8u,16u}) for (unsigned channels : {1u,2u}) {
        auto source=wave(depth,channels); auto decoder=nolf::platform::make_ffmpeg_audio(source,{8000,channels}); const auto samples=decode(*decoder);
        require(decoder->format()==nolf::platform::AudioFormat{8000,channels} && decoder->duration() && std::abs(*decoder->duration()-.125)<.00001 && samples.size()==1000*channels,"PCM audio duration/frames differ");
        constexpr std::array<float,5> expected{-1,-.5f,0,.5f,32767.f/32768};
        for (unsigned frame=0;frame<1000;++frame) for (unsigned c=0;c<channels;++c) {
            auto value=expected[(frame+c)%5]; if (depth==8 && value>0.9f) value=127.f/128;
            require(samples[frame*channels+c]==value,"PCM native float decoding differs");
        }
        decoder->seek(37); std::array<float,20> next{}; require(decoder->read(std::span(next).first(channels*5))==5,"audio exact seek ended early");
        require(std::equal(next.begin(),next.begin()+channels*5,samples.begin()+37*channels),"audio seek changed sample alignment");
        decoder->seek(0); require(decode(*decoder)==samples,"rewound audio differs"); rejects([&] { decoder->seek(1001); });
        require(std::async(std::launch::async,[&] { try { decoder->format(); } catch (const std::logic_error&) { return true; } return false; }).get(),"audio decoder crossed threads");
    }
    auto ima=nolf::platform::make_ffmpeg_audio(wave(4,1,9,true),{8000,1}); const auto samples=decode(*ima); require(samples.size()==9 && std::all_of(samples.begin(),samples.end(),[](float value) { return value==1000.f/32768; }),"IMA ADPCM predictor/nibble/frame decode differs");
    auto resampled=nolf::platform::make_ffmpeg_audio(wave(),{48000,2}); const auto stereo=decode(*resampled); require(stereo.size()==12000,"resampler tail lost frames"); for (std::size_t i=0;i<stereo.size();i+=2) require(stereo[i]==stereo[i+1],"mono rematrix differs between output channels");
    auto limited=nolf::platform::make_ffmpeg_audio(wave(),{48000,1},500); const auto cropped=decode(*limited); require(cropped.size()==3000 && limited->duration()==.0625,"source sample limit included encoded padding or lost resampler tail"); limited->seek(2999); std::array<float,2> tail{}; require(limited->read(tail)==1,"source limit not retained across seek");
    auto failure=wave(); failure->fail=true; bool propagated=false; try { nolf::platform::make_ffmpeg_audio(failure); } catch (const std::runtime_error& e) { propagated=std::string(e.what())=="native audio source fixture failure"; } require(propagated,"source exception crossed C callback or was discarded");
    auto empty=std::make_shared<Memory>(); rejects([&] { nolf::platform::make_ffmpeg_audio(empty); });
    auto bad=wave(); bad->bytes.resize(8); rejects([&] { nolf::platform::make_ffmpeg_audio(bad); }); rejects([&] { nolf::platform::make_ffmpeg_audio(wave(),{0,2}); });
    auto retained=wave(); auto decoder=nolf::platform::make_ffmpeg_audio(retained,{8000,1}); retained.reset(); require(decode(*decoder).size()==1000,"audio source did not survive caller retirement");
}
void corpus(const char* data) {
    nolf::Assets assets; assets.mount_game(data); std::map<unsigned,std::size_t> formats; std::uint64_t frames=0,loops=0; std::size_t total=0;
    for (const auto& name : assets.names()) {
        if (!name.ends_with(".wav")) continue;
        try {
            const auto source=assets.locate(name); const auto metadata=nolf::read_wave(source);
            auto decoder=nolf::platform::make_ffmpeg_audio(std::make_shared<nolf::AssetMediaSource>(source),{metadata.sample_rate,metadata.channels},metadata.frames); const auto samples=decode(*decoder); const auto decoded=samples.size()/metadata.channels;
            require(decoded>0 && decoder->duration().has_value(),"original audio empty/missing duration");
            if ((metadata.format==1 || metadata.format==17) && (!metadata.frames || decoded!=*metadata.frames)) throw std::runtime_error("original decoded frame count differs: codec="+std::to_string(metadata.format)+" decoded="+std::to_string(decoded)+" RIFF="+std::to_string(metadata.frames.value_or(0)));
            if (metadata.loop) { ++loops; decoder->seek(metadata.loop->start); std::array<float,20> check{}; const auto count=decoder->read(std::span(check).first(metadata.channels*5)); require(count && std::equal(check.begin(),check.begin()+count*metadata.channels,samples.begin()+metadata.loop->start*metadata.channels),"original loop sample alignment differs"); }
            ++formats[metadata.format]; ++total; frames+=decoded;
        } catch (const std::exception& e) { throw std::runtime_error(name+": "+e.what()); }
    }
    std::cout<<"Native decoded audio: "<<total<<" WAVE files, "<<frames<<" frames, "<<loops<<" authored loops; codecs"; for (const auto& [format,count] : formats) std::cout<<' '<<format<<'='<<count; std::cout<<'\n';
}
}
int main(int argc,char** argv) { try { checks(); if (argc>1) corpus(argv[1]); std::cout<<"Native audio PCM/IMA decoding, resampling, exact seek, malformed input and retained source passed\n"; return 0; } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
