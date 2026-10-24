#include "engine/client/client_videos.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <random>

namespace {
void require(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
struct Display final : nolf::SurfaceDisplay {
    std::shared_ptr<const nolf::Surface> surface;
    nolf::PixelRect destination{};
    nolf::SurfaceDraw style;
    unsigned draws=0,clears=0;
    void resize(std::uint32_t,std::uint32_t) override {}
    void draw(const std::shared_ptr<const nolf::Surface>& s,std::optional<nolf::PixelRect>,nolf::PixelRect d,nolf::SurfaceDraw st) override { surface=s; destination=d; style=st; ++draws; }
    void warp(const std::shared_ptr<const nolf::Surface>&,std::span<const nolf::WarpPoint>,nolf::SurfaceDraw) override {}
    void clear(std::optional<nolf::PixelRect>,std::uint32_t) override { ++clears; }
    nolf::Image read(nolf::PixelRect) override { return nolf::Image(1,1); }
    std::uint32_t shown() const { require(surface!=nullptr,"no movie frame drawn"); return surface->image().at(0,0)&0xffffff; }
};
// Frame i is filled with colour i so the displayed frame is observable.
struct Frames final : nolf::platform::VideoDecoder {
    unsigned count,width,height; double rate; bool audio; unsigned index=0; std::optional<unsigned> fail_at;
    Frames(unsigned n,unsigned w,unsigned h,double r,bool a) : count(n),width(w),height(h),rate(r),audio(a) {}
    nolf::platform::VideoInfo info() const override { return {width,height,rate,count/rate,count,audio}; }
    std::optional<nolf::platform::VideoFrame> next() override {
        if (fail_at && index==*fail_at) throw std::runtime_error("fixture video corruption");
        if (index==count) return {};
        nolf::platform::VideoFrame frame{width,height,index/rate,1/rate,std::vector<std::byte>(std::size_t(width)*height*4)};
        for (std::size_t p=0;p<frame.rgba.size();p+=4) { frame.rgba[p+2]=std::byte(index); frame.rgba[p+3]=std::byte{255}; }
        ++index; return frame;
    }
    void seek(std::uint64_t) override { throw std::logic_error("unused"); }
};
struct Track final : nolf::platform::AudioDecoder {
    nolf::platform::AudioFormat output; std::size_t remaining,chunk; std::optional<std::size_t> fail_below;
    Track(nolf::platform::AudioFormat f,std::size_t frames,std::size_t c) : output(f),remaining(frames),chunk(c) {}
    nolf::platform::AudioFormat format() const override { return output; }
    std::optional<double> duration() const override { return {}; }
    std::size_t read(std::span<float> samples) override {
        if (fail_below && remaining<=*fail_below) throw std::runtime_error("fixture soundtrack corruption");
        const auto frames=std::min({remaining,chunk,samples.size()/std::size_t(output.channels)}); remaining-=frames; return frames;
    }
    void seek(std::uint64_t) override { throw std::logic_error("unused"); }
};
struct Device final : nolf::platform::AudioOutput {
    nolf::platform::AudioFormat output{1000,2}; std::size_t queued=0,written=0; bool cleared=false;
    nolf::platform::AudioFormat format() const override { return output; }
    void write(std::span<const float> samples) override { queued+=samples.size()/2; written+=samples.size()/2; }
    std::size_t queued_frames() const override { return queued; }
    void pause(bool) override {}
    void clear() override { queued=0; cleared=true; }
    void play(std::size_t frames) { queued-=std::min(queued,frames); }
};
struct Fixture {
    std::filesystem::path root;
    std::shared_ptr<nolf::Assets> assets=std::make_shared<nolf::Assets>();
    double now=0;
    std::shared_ptr<Device> device;
    std::function<std::unique_ptr<nolf::platform::VideoDecoder>()> video;
    std::function<std::unique_ptr<nolf::platform::AudioDecoder>(nolf::platform::AudioFormat)> track;
    bool no_device=false;
    Display display;
    Fixture() {
        root=std::filesystem::temp_directory_path()/("nolf-video-"+std::to_string(std::random_device{}()));
        std::filesystem::create_directories(root/"Movies"); std::ofstream(root/"Movies"/"Intro.bik")<<"fixture"; assets->mount_directory(root);
    }
    ~Fixture() { std::error_code ignored; std::filesystem::remove_all(root,ignored); }
    nolf::VideoServices services() {
        return {assets,[this](auto) { return video(); },
            [this](auto,nolf::platform::AudioFormat f,auto) { if (!track) throw std::runtime_error("no fixture soundtrack"); return track(f); },
            [this](nolf::platform::AudioFormat) -> std::shared_ptr<nolf::platform::AudioOutput> { if (no_device) throw std::runtime_error("no audio device"); device=std::make_shared<Device>(); return device; },
            [this] { return now; }};
    }
};
char name[]="movies/intro.BIK";
void checks() {
    {
        Fixture f; f.video=[] { return std::make_unique<Frames>(10,320,200,10,false); }; nolf::ClientVideos videos(f.services(),f.display,640,480); HVIDEO video=nullptr;
        char missing[]="Movies\\Absent.bik"; require(videos.StartOnScreenVideo(missing,PLAYBACK_FULLSCREEN,video)==LT_NOTFOUND && !video,"missing movie accepted");
        require(videos.StartOnScreenVideo(nullptr,0,video)==LT_INVALIDPARAMS && videos.StartOnScreenVideo(name,PLAYBACK_FROMMEMORY,video)==LT_INVALIDPARAMS,"invalid movie request accepted");
        require(videos.StartTextureVideo(name,0,video)==LT_NOTINITIALIZED && !video && videos.BindTextureVideoToPoly(nullptr,0)==LT_NOTINITIALIZED,"texture video claimed");
        char folded[]="Movies\\Intro.bik"; require(videos.StartOnScreenVideo(folded,PLAYBACK_FULLSCREEN,video)==LT_OK && video && videos.active()==1,"movie start failed");
        require(videos.UpdateVideo(video)==LT_OK && f.display.shown()==0 && f.display.clears==1,"first movie frame not shown");
        require(f.display.destination.left==0 && f.display.destination.top==40 && f.display.destination.right==640 && f.display.destination.bottom==440 && f.display.style.linear,"fullscreen movie not letterboxed");
        f.now=.35; require(videos.UpdateVideo(video)==LT_OK && f.display.shown()==3 && videos.GetVideoStatus(video)==LT_OK,"late movie frames not dropped");
        f.now=.95; require(videos.UpdateVideo(video)==LT_OK && f.display.shown()==9 && videos.GetVideoStatus(video)==LT_OK,"final frame ended early");
        f.now=1; require(videos.GetVideoStatus(video)==LT_FINISHED && videos.UpdateVideo(video)==LT_OK && f.display.shown()==9,"finished movie does not hold its last frame");
        require(std::async(std::launch::async,[&] { return videos.UpdateVideo(video); }).get()==LT_ERROR,"movie used from another thread");
        require(videos.StopVideo(video)==LT_OK && videos.StopVideo(video)==LT_INVALIDPARAMS && videos.UpdateVideo(video)==LT_INVALIDPARAMS && videos.GetVideoStatus(video)==LT_INVALIDPARAMS && !videos.active(),"stopped movie handle still valid");
        require(videos.StartOnScreenVideo(name,0,video)==LT_OK && videos.UpdateVideo(video)==LT_OK,"windowed movie failed");
        require(f.display.destination.left==160 && f.display.destination.top==140 && f.display.destination.right==480 && f.display.destination.bottom==340 && !f.display.style.linear,"windowed movie not centered at native size");
    }
    {
        // The soundtrack, not the wall clock, decides which frame is due. It
        // ends before the picture; the tail continues on the wall clock.
        Fixture f; f.video=[] { return std::make_unique<Frames>(10,64,48,10,true); }; f.track=[](auto format) { return std::make_unique<Track>(format,800,64); };
        nolf::ClientVideos videos(f.services(),f.display,64,48); HVIDEO video=nullptr; require(videos.StartOnScreenVideo(name,PLAYBACK_FULLSCREEN,video)==LT_OK,"audio movie start failed");
        auto run=[&](std::size_t frames) { while (frames) { const auto step=std::min<std::size_t>(frames,50); f.device->play(step); frames-=step; videos.pump(); } };
        require(f.device && f.device->queued>=100 && f.device->queued<164,"soundtrack not queued to its lead");
        f.now=100; require(videos.UpdateVideo(video)==LT_OK && f.display.shown()==0,"picture followed the wall clock over the soundtrack");
        run(350); require(videos.UpdateVideo(video)==LT_OK && f.display.shown()==3 && f.device->queued>=100,"picture did not follow the soundtrack");
        auto device=f.device; run(450); require(device->written==800 && !device->queued,"soundtrack not fully played");
        require(videos.UpdateVideo(video)==LT_OK && f.display.shown()==8 && videos.GetVideoStatus(video)==LT_OK,"picture lost the soundtrack position at its end");
        f.now=100.15; require(videos.UpdateVideo(video)==LT_OK && f.display.shown()==9 && videos.GetVideoStatus(video)==LT_OK,"silent tail did not continue on the wall clock");
        f.now=100.2; require(videos.GetVideoStatus(video)==LT_FINISHED,"movie did not finish after its final frame");
    }
    {
        Fixture f; f.no_device=true; f.video=[] { return std::make_unique<Frames>(4,8,8,10,true); }; f.track=[](auto format) { return std::make_unique<Track>(format,400,64); };
        nolf::ClientVideos videos(f.services(),f.display,8,8); HVIDEO video=nullptr; require(videos.StartOnScreenVideo(name,0,video)==LT_OK,"movie needs an audio device");
        f.now=.25; require(videos.UpdateVideo(video)==LT_OK && f.display.shown()==2,"deviceless movie not on wall clock");
    }
    {
        Fixture f; f.video=[] { return std::make_unique<Frames>(10,8,8,10,true); };
        f.track=[](auto format) { auto track=std::make_unique<Track>(format,1000,64); track->fail_below=600; return track; };
        nolf::ClientVideos videos(f.services(),f.display,8,8); HVIDEO video=nullptr; require(videos.StartOnScreenVideo(name,0,video)==LT_OK,"audio movie start failed");
        auto device=f.device; auto run=[&](std::size_t frames) { while (frames) { const auto step=std::min<std::size_t>(frames,50); device->play(step); frames-=step; videos.pump(); } };
        run(250); require(videos.UpdateVideo(video)==LT_OK && f.display.shown()==2,"soundtrack clock not used");
        run(200); require(device->cleared && device->written<=450,"corrupt soundtrack not silenced");
        f.now=.4; require(videos.UpdateVideo(video)==LT_OK && f.display.shown()>=6 && f.display.shown()<=8,"picture did not continue from the soundtrack position");
    }
    {
        Fixture f; f.video=[] { auto frames=std::make_unique<Frames>(10,8,8,10,false); frames->fail_at=3; return frames; };
        nolf::ClientVideos videos(f.services(),f.display,8,8); HVIDEO video=nullptr; require(videos.StartOnScreenVideo(name,0,video)==LT_OK,"movie start failed");
        f.now=.5; require(videos.UpdateVideo(video)==LT_ERROR && videos.StopVideo(video)==LT_OK,"corrupt movie not reported");
    }
}
// Every retail movie plays to completion on the real decoders, with the
// soundtrack clock advanced in 30 Hz device steps.
void corpus(const char* data) {
    auto assets=std::make_shared<nolf::Assets>(); assets->mount_game(data); double now=0; std::shared_ptr<Device> device; Display display;
    nolf::VideoServices services{assets,nolf::platform::make_ffmpeg_video,nolf::platform::make_ffmpeg_audio,
        [&](nolf::platform::AudioFormat) { device=std::make_shared<Device>(); device->output={48000,2}; return device; },[&] { return now; }};
    nolf::ClientVideos videos(services,display,1920,1080); unsigned movies=0,shown=0;
    for (const char* movie : {"Movies\\SierraLogo.bik","Movies\\foxpc.bik","Movies\\LithLogo.bik","Movies\\LTLogo.bik"}) {
        std::string path=movie; HVIDEO video=nullptr; require(videos.StartOnScreenVideo(path.data(),PLAYBACK_FULLSCREEN,video)==LT_OK && device,"original movie start failed");
        const nolf::Surface* last=nullptr; unsigned steps=0;
        while (videos.GetVideoStatus(video)==LT_OK) {
            require(videos.UpdateVideo(video)==LT_OK,"original movie playback failed");
            if (display.surface.get()!=last) { last=display.surface.get(); ++shown; }
            device->play(1600); now+=1./30; require(++steps<30*120,"original movie did not finish");
        }
        require(display.destination.bottom-display.destination.top<=1080 && display.destination.right-display.destination.left<=1920,"original movie exceeds display");
        require(videos.StopVideo(video)==LT_OK,"original movie stop failed"); ++movies;
    }
    require(!videos.active(),"original movies leaked");
    std::cout<<"Native client videos: "<<movies<<" original movies played to completion, "<<shown<<" frames presented\n";
}
}
int main(int argc,char** argv) { try { checks(); if (argc>1) corpus(argv[1]); std::cout<<"Native client video timing, soundtrack clock, letterboxing and failure handling passed\n"; return 0; } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
