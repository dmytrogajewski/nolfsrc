#include "platform/video.h"
#include "engine/portable/media_source.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <future>
#include <iostream>
#include <vector>

namespace {
void require(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
template<class F> void rejects(F&& fn) { bool rejected=false; try { fn(); } catch (const std::exception&) { rejected=true; } require(rejected,"invalid video operation accepted"); }
struct Memory final : nolf::platform::MediaSource {
    std::vector<std::byte> bytes;
    bool fail=false;
    std::uint64_t size() const override { return bytes.size(); }
    void read(std::uint64_t offset,std::span<std::byte> output) const override {
        if (fail) throw std::runtime_error("native video source fixture failure");
        if (offset>bytes.size() || output.size()>bytes.size()-std::size_t(offset)) throw std::out_of_range("fixture read exceeds media extent");
        std::copy_n(bytes.data()+offset,output.size(),output.data());
    }
};
std::uint64_t hash(const nolf::platform::VideoFrame& frame) {
    std::uint64_t value=1469598103934665603ull;
    for (const auto byte : frame.rgba) { value^=std::to_integer<std::uint8_t>(byte); value*=1099511628211ull; }
    return value;
}
void checks() {
    auto empty=std::make_shared<Memory>(); rejects([&] { nolf::platform::make_ffmpeg_video(empty); });
    auto noise=std::make_shared<Memory>(); noise->bytes.assign(4096,std::byte{0x5a}); rejects([&] { nolf::platform::make_ffmpeg_video(noise); });
    auto failure=std::make_shared<Memory>(); failure->bytes.assign(4096,std::byte{}); failure->fail=true; bool propagated=false;
    try { nolf::platform::make_ffmpeg_video(failure); } catch (const std::runtime_error& e) { propagated=std::string(e.what())=="native video source fixture failure"; }
    require(propagated,"video source exception crossed C callback or was discarded");
}
// Every retail movie decodes completely; seeking reproduces exact frames for
// these stateful delta codecs, and each soundtrack decodes for the mixer.
void corpus(const char* data) {
    nolf::Assets assets; assets.mount_game(data); std::size_t movies=0; std::uint64_t frames=0,seeks=0,samples=0;
    for (const auto& name : assets.names()) {
        if (!name.ends_with(".bik") && !name.ends_with(".smk")) continue;
        try {
            const auto source=std::make_shared<nolf::AssetMediaSource>(assets.locate(name));
            auto decoder=nolf::platform::make_ffmpeg_video(source); const auto info=decoder->info();
            require(info.width && info.height && info.frame_rate>0,"original movie has no picture");
            std::vector<std::uint64_t> hashes; double last=-1;
            while (auto frame=decoder->next()) {
                require(frame->width==info.width && frame->height==info.height && frame->rgba.size()==std::size_t(info.width)*info.height*4,"original movie frame size differs");
                require(frame->time>last && frame->duration>0,"original movie timestamps not increasing");
                last=frame->time; hashes.push_back(hash(*frame));
                require(hashes.size()<=100000,"original movie exceeds frame budget");
            }
            require(hashes.size()>1 && !decoder->next(),"original movie ended early or restarted");
            if (info.frames) require(*info.frames==hashes.size(),"original movie frame count differs from container");
            if (info.duration) require(std::abs(*info.duration-last)<=2/info.frame_rate+.5,"original movie duration differs from final frame");
            for (const auto target : {std::uint64_t{0},std::uint64_t{1},hashes.size()/2,hashes.size()-1}) {
                decoder->seek(target); const auto frame=decoder->next(); require(frame && hash(*frame)==hashes[target],"original movie seek frame differs"); ++seeks;
            }
            decoder->seek(hashes.size()); require(!decoder->next(),"movie seek to end yielded a frame"); rejects([&] { decoder->seek(hashes.size()+1); });
            require(std::async(std::launch::async,[&] { try { decoder->info(); } catch (const std::logic_error&) { return true; } return false; }).get(),"video decoder crossed threads");
            if (info.audio) {
                auto audio=nolf::platform::make_ffmpeg_audio(source,{48000,2}); std::array<float,8192> block{}; std::uint64_t decoded=0;
                while (const auto count=audio->read(block)) decoded+=count;
                require(decoded>0 && std::abs(double(decoded)/48000-last)<2,"original movie soundtrack length differs from picture");
                samples+=decoded;
            }
            ++movies; frames+=hashes.size();
        } catch (const std::exception& e) { throw std::runtime_error(name+": "+e.what()); }
    }
    require(movies>0,"no original movies found");
    std::cout<<"Native decoded video: "<<movies<<" movies, "<<frames<<" frames, "<<seeks<<" exact seeks, "<<samples<<" soundtrack frames\n";
}
}
int main(int argc,char** argv) { try { checks(); if (argc>1) corpus(argv[1]); std::cout<<"Native video malformed input, source failure and decoding passed\n"; return 0; } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
