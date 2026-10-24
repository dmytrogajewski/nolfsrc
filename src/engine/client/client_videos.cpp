#include "engine/client/client_videos.h"
#include "engine/portable/media_source.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <thread>

namespace nolf {
namespace {
template<class F> LTRESULT operation(F&& function) noexcept {
    try { return function(); }
    catch (const std::bad_alloc&) { return LT_OUTOFMEMORY; }
    catch (const std::invalid_argument&) { return LT_INVALIDPARAMS; }
    catch (...) { return LT_ERROR; }
}
using Identity=std::uint64_t;
Identity identity(HVIDEO handle) { return reinterpret_cast<std::uintptr_t>(handle); }
HVIDEO handle(Identity id) {
    if (!id || id>std::numeric_limits<std::uintptr_t>::max()) throw std::overflow_error("SDK video identities exhausted");
    return reinterpret_cast<HVIDEO>(static_cast<std::uintptr_t>(id));
}
// Soundtracks stay about a tenth of a second ahead of the device.
constexpr double audio_lead=.1;
constexpr std::size_t audio_block=4096;
std::shared_ptr<Surface> picture(const platform::VideoFrame& frame) {
    Image image(frame.width,frame.height); auto pixels=image.pixels(); const auto* rgba=frame.rgba.data();
    for (std::size_t i=0;i<pixels.size();++i,rgba+=4)
        pixels[i]=0xff000000u | std::to_integer<std::uint32_t>(rgba[0])<<16 | std::to_integer<std::uint32_t>(rgba[1])<<8 | std::to_integer<std::uint32_t>(rgba[2]);
    return std::make_shared<Surface>(std::move(image));
}
}
struct ClientVideos::State {
    struct Movie {
        std::unique_ptr<platform::VideoDecoder> decoder;
        platform::VideoInfo info;
        std::unique_ptr<platform::AudioDecoder> soundtrack;
        std::shared_ptr<platform::AudioOutput> output;
        std::uint64_t written=0;
        bool audio_done=false,picture_done=false,fullscreen=false;
        // Time base when the soundtrack is not the clock.
        double base_time=0,base_wall=0;
        std::optional<platform::VideoFrame> pending;
        std::shared_ptr<const Surface> shown;
        double shown_until=0;
    };
    VideoServices services;
    SurfaceDisplay& display;
    std::uint32_t width,height;
    std::map<Identity,Movie> movies;
    Identity next=1;
    const std::thread::id owner=std::this_thread::get_id();
    State(VideoServices source,SurfaceDisplay& target,std::uint32_t w,std::uint32_t h) : services(std::move(source)),display(target) {
        if (!services.assets || !services.video || !services.clock) throw std::invalid_argument("native video services are required");
        size(w,h);
    }
    void thread() const { if (owner!=std::this_thread::get_id()) throw std::logic_error("client videos used from another thread"); }
    void size(std::uint32_t w,std::uint32_t h) { if (!w || !h) throw std::invalid_argument("empty video display"); width=w; height=h; }
    Movie& get(HVIDEO video) { const auto it=movies.find(identity(video)); if (it==movies.end()) throw std::invalid_argument("invalid native video handle"); return it->second; }
    double wall() const { const auto now=services.clock(); if (!std::isfinite(now)) throw std::runtime_error("invalid video clock"); return now; }
    // The soundtrack stays the clock until feed() hands its position over.
    double time(const Movie& movie) const {
        if (!movie.output) return movie.base_time+std::max(0.,wall()-movie.base_wall);
        const auto played=movie.written-std::min<std::uint64_t>(movie.written,movie.output->queued_frames());
        return double(played)/movie.output->format().rate;
    }
    // The picture continues on the wall clock from the soundtrack's position.
    void silence(Movie& movie) { const auto now=time(movie); movie.soundtrack.reset(); if (movie.output) movie.output->clear(); movie.output.reset(); movie.base_time=now; movie.base_wall=wall(); }
    void feed(Movie& movie) {
        if (!movie.output) return;
        try {
            if (movie.audio_done) { if (!movie.output->queued_frames()) silence(movie); return; }
            const auto format=movie.output->format(); const auto target=std::size_t(format.rate*audio_lead);
            std::vector<float> block(audio_block*format.channels);
            while (movie.output->queued_frames()<target) {
                const auto frames=movie.soundtrack->read(block); if (!frames) { movie.audio_done=true; break; }
                movie.output->write(std::span(block).first(frames*format.channels)); movie.written+=frames;
            }
        } catch (const std::exception&) { silence(movie); }
    }
    void advance(Movie& movie) {
        const auto now=time(movie); std::optional<platform::VideoFrame> due;
        while (!movie.picture_done) {
            if (!movie.pending) { movie.pending=movie.decoder->next(); if (!movie.pending) { movie.picture_done=true; break; } }
            if (movie.pending->time>now && (due || movie.shown)) break;
            due=std::move(movie.pending); movie.pending.reset();
        }
        if (due) { movie.shown=picture(*due); movie.shown_until=due->time+due->duration; }
    }
    void draw(const Movie& movie) {
        if (!movie.shown) return;
        const auto w=double(movie.info.width),h=double(movie.info.height);
        const auto scale=movie.fullscreen ? std::min(width/w,height/h) : 1.;
        const auto dw=int(std::lround(w*scale)),dh=int(std::lround(h*scale)),x=(int(width)-dw)/2,y=(int(height)-dh)/2;
        display.clear(std::nullopt,0); SurfaceDraw style; style.blend=SurfaceBlend::Solid; style.linear=movie.fullscreen && scale!=1;
        display.draw(movie.shown,std::nullopt,{x,y,x+dw,y+dh},style);
    }
    bool finished(const Movie& movie) const { return movie.picture_done && !movie.output && time(movie)>=movie.shown_until; }
    LTRESULT start(const char* name,uint32 flags,HVIDEO& result) {
        thread(); result=nullptr;
        if (!name || !*name || flags&~PLAYBACK_FULLSCREEN) return LT_INVALIDPARAMS;
        if (!services.assets->contains(name)) return LT_NOTFOUND;
        const auto source=std::make_shared<AssetMediaSource>(services.assets->locate(name));
        Movie movie; movie.decoder=services.video(source); movie.info=movie.decoder->info(); movie.fullscreen=flags&PLAYBACK_FULLSCREEN;
        if (movie.info.audio && services.audio && services.output) {
            // A movie still plays without a soundtrack or an audio device.
            try { movie.output=services.output({}); movie.soundtrack=services.audio(source,movie.output->format(),std::nullopt); }
            catch (const std::exception&) { movie.output.reset(); movie.soundtrack.reset(); }
        }
        movie.base_wall=wall(); const auto id=next++; auto video=handle(id);
        auto& stored=movies.emplace(id,std::move(movie)).first->second;
        try { feed(stored); } catch (...) { movies.erase(id); throw; }
        result=video; return LT_OK;
    }
};
ClientVideos::ClientVideos(VideoServices services,SurfaceDisplay& display,std::uint32_t width,std::uint32_t height)
    : state_(std::make_unique<State>(std::move(services),display,width,height)) {}
ClientVideos::~ClientVideos()=default;
void ClientVideos::resize(std::uint32_t width,std::uint32_t height) { state_->thread(); state_->size(width,height); }
void ClientVideos::pump() { state_->thread(); for (auto& [id,movie] : state_->movies) state_->feed(movie); }
std::size_t ClientVideos::active() const { state_->thread(); return state_->movies.size(); }
LTRESULT ClientVideos::StartOnScreenVideo(char* name,uint32 flags,HVIDEO& video) { return operation([&] { return state_->start(name,flags,video); }); }
LTRESULT ClientVideos::StartTextureVideo(char*,uint32,HVIDEO& video) { video=nullptr; return LT_NOTINITIALIZED; }
LTRESULT ClientVideos::UpdateVideo(HVIDEO video) {
    return operation([&] { state_->thread(); auto& movie=state_->get(video); state_->feed(movie); state_->advance(movie); state_->draw(movie); return LT_OK; });
}
LTRESULT ClientVideos::GetVideoStatus(HVIDEO video) {
    return operation([&] { state_->thread(); auto& movie=state_->get(video); state_->feed(movie); return state_->finished(movie) ? LT_FINISHED : LT_OK; });
}
LTRESULT ClientVideos::StopVideo(HVIDEO video) {
    return operation([&] { state_->thread(); const auto it=state_->movies.find(identity(video)); if (it==state_->movies.end()) return LT_INVALIDPARAMS; state_->movies.erase(it); return LT_OK; });
}
LTRESULT ClientVideos::BindTextureVideoToPoly(HVIDEO,HPOLY) { return LT_NOTINITIALIZED; }
}
