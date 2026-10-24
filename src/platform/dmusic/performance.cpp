#include "platform/music.h"
#include <dmusic.h>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>

namespace nolf::platform {
namespace {
void check(DmResult result,const char* operation) {
    if (result==DmResult_SUCCESS) return;
    if (result==DmResult_MEMORY_EXHAUSTED) throw std::bad_alloc();
    throw std::runtime_error(std::string(operation)+" failed: dmusic error "+std::to_string(int(result)));
}
DmTiming timing(MusicTiming value) {
    switch (value) {
    case MusicTiming::Instant: return DmTiming_INSTANT;
    case MusicTiming::Grid: return DmTiming_GRID;
    case MusicTiming::Beat: return DmTiming_BEAT;
    case MusicTiming::Measure: return DmTiming_MEASURE;
    case MusicTiming::Segment: return DmTiming_SEGMENT;
    }
    throw std::invalid_argument("invalid music timing");
}
class Segment final : public MusicSegment {
public:
    DmSegment* handle;
    explicit Segment(DmSegment* segment) : handle(segment) {}
    ~Segment() override { DmSegment_release(handle); }
    double length() const override { return DmSegment_getLength(handle); }
};
class DmusicPerformance final : public MusicPerformance {
    const std::thread::id owner_=std::this_thread::get_id();
    // The loader calls back into this resolver from load() only.
    MusicResolver resolver_;
    DmLoader* loader_=nullptr;
    DmPerformance* performance_=nullptr;
    void thread() const { if (owner_!=std::this_thread::get_id()) throw std::logic_error("music performance used from another thread"); }
    static void* resolve(void* context,const char* name,std::size_t* size) noexcept {
        try {
            std::string_view file=name; if (const auto slash=file.find_last_of("/\\"); slash!=file.npos) file.remove_prefix(slash+1);
            auto bytes=static_cast<DmusicPerformance*>(context)->resolver_(file); if (!bytes || bytes->empty()) return nullptr;
            // dmusic releases resolved files with its default allocator, free().
            auto* memory=std::malloc(bytes->size()); if (!memory) return nullptr;
            std::memcpy(memory,bytes->data(),bytes->size()); *size=bytes->size(); return memory;
        } catch (...) { return nullptr; }
    }
    // Segments keep what they downloaded; only the loader's caches go.
    static DmLoader* loader(void* context) {
        DmLoader* result=nullptr; check(DmLoader_create(&result,DmLoader_DOWNLOAD),"create music loader");
        try { check(DmLoader_addResolver(result,resolve,context),"register music resolver"); } catch (...) { DmLoader_release(result); throw; }
        return result;
    }
public:
    DmusicPerformance(MusicResolver resolver,unsigned rate) : resolver_(std::move(resolver)) {
        if (!resolver_ || rate<8000 || rate>192000) throw std::invalid_argument("invalid music performance configuration");
        loader_=loader(this);
        try { check(DmPerformance_create(&performance_,rate),"create music performance"); } catch (...) { DmLoader_release(loader_); throw; }
    }
    ~DmusicPerformance() override { DmPerformance_release(performance_); DmLoader_release(loader_); }
    DmusicPerformance(const DmusicPerformance&)=delete;
    DmusicPerformance& operator=(const DmusicPerformance&)=delete;
    void scope() override { thread(); auto* next=loader(this); DmLoader_release(loader_); loader_=next; }
    std::shared_ptr<const MusicSegment> load(std::string_view name) override {
        thread(); const std::string file(name); DmSegment* segment=nullptr;
        // On failure the loader has already released the segment it created.
        const auto result=DmLoader_getSegment(loader_,file.c_str(),&segment);
        if (result==DmResult_MEMORY_EXHAUSTED || result==DmResult_MUTEX_ERROR) check(result,"load music segment");
        if (result!=DmResult_SUCCESS || !segment) return nullptr;
        return std::make_shared<Segment>(segment);
    }
    void play(const std::shared_ptr<const MusicSegment>& segment,MusicTiming when) override {
        thread(); DmSegment* handle=nullptr;
        if (segment) { const auto* native=dynamic_cast<const Segment*>(segment.get()); if (!native) throw std::invalid_argument("foreign music segment"); handle=native->handle; }
        check(DmPerformance_playSegment(performance_,handle,timing(when)),"play music segment");
    }
    void volume(float gain) override { thread(); if (!(gain>=0 && gain<=1)) throw std::invalid_argument("invalid music volume"); DmPerformance_setVolume(performance_,gain); }
    MusicPlayback playback() const override {
        thread(); DmPlayback state{}; check(DmPerformance_getPlayback(performance_,&state),"query music playback");
        return {state.serial,state.remaining,state.playing!=0};
    }
    void render(std::span<float> stereo) override {
        thread(); if (stereo.size()%2) throw std::invalid_argument("music render needs whole stereo frames");
        if (stereo.empty()) return;
        check(DmPerformance_renderPcm(performance_,stereo.data(),stereo.size(),static_cast<DmRenderOptions>(DmRender_FLOAT|DmRender_STEREO)),"render music");
    }
};
}
std::unique_ptr<MusicPerformance> make_dmusic_performance(MusicResolver resolver,unsigned rate) { return std::make_unique<DmusicPerformance>(std::move(resolver),rate); }
}
