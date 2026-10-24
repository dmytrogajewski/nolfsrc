#include "platform/audio.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace nolf::platform {
namespace {
void check(bool value,const char* operation) { if (!value) throw std::runtime_error(std::string(operation)+": "+SDL_GetError()); }
struct AudioLease {
    AudioLease() { check(SDL_InitSubSystem(SDL_INIT_AUDIO),"initialize native audio lease"); }
    ~AudioLease() { SDL_QuitSubSystem(SDL_INIT_AUDIO); }
};
struct StreamDelete { void operator()(SDL_AudioStream* stream) const { SDL_DestroyAudioStream(stream); } };
using Stream=std::unique_ptr<SDL_AudioStream,StreamDelete>;
constexpr SDL_AudioSpec mix_spec{SDL_AUDIO_F32,2,48000};

// The process plays through one SDL device and mixes its outputs here. SDL
// moves every open device when the system default output changes, and doing
// that with several open has crashed SDL's PipeWire playback thread.
class SharedDevice {
public:
    static std::shared_ptr<SharedDevice> acquire() {
        static std::mutex mutex; static std::weak_ptr<SharedDevice> current;
        std::lock_guard lock(mutex);
        if (auto device=current.lock()) return device;
        auto device=std::shared_ptr<SharedDevice>(new SharedDevice); current=device; return device;
    }
    SharedDevice(const SharedDevice&)=delete;
    SharedDevice& operator=(const SharedDevice&)=delete;
    void add(SDL_AudioStream* input,const std::atomic<bool>& paused) { std::lock_guard lock(mutex_); inputs_.push_back({input,&paused}); }
    // After this returns the device no longer reads the input.
    void remove(SDL_AudioStream* input) {
        std::lock_guard lock(mutex_);
        std::erase_if(inputs_,[input](const Input& entry) { return entry.stream==input; });
    }
private:
    struct Input { SDL_AudioStream* stream; const std::atomic<bool>* paused; };
    SharedDevice() {
        device_.reset(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&mix_spec,&SharedDevice::fill,this)); check(bool(device_),"open native audio device");
        check(SDL_ResumeAudioStreamDevice(device_.get()),"start native audio playback");
    }
    static void SDLCALL fill(void* self,SDL_AudioStream* output,int additional,int) { static_cast<SharedDevice*>(self)->mix(output,additional); }
    void mix(SDL_AudioStream* output,int bytes) {
        if (bytes<=0) return;
        thread_local std::vector<float> mixed,input;
        const auto samples=std::size_t(bytes)/sizeof(float);
        mixed.assign(samples,0.f); input.resize(samples);
        {
            std::lock_guard lock(mutex_);
            for (const auto& entry : inputs_) {
                if (entry.paused->load(std::memory_order_relaxed)) continue;
                // An underrun plays out what the resampler still holds back.
                if (SDL_GetAudioStreamAvailable(entry.stream)<int(samples*sizeof(float))) SDL_FlushAudioStream(entry.stream);
                const auto read=SDL_GetAudioStreamData(entry.stream,input.data(),int(samples*sizeof(float)));
                for (std::size_t i=0;read>0 && i<std::size_t(read)/sizeof(float);++i) mixed[i]+=input[i];
            }
        }
        for (auto& sample : mixed) sample=std::clamp(sample,-1.f,1.f);
        SDL_PutAudioStreamData(output,mixed.data(),int(samples*sizeof(float)));
    }
    AudioLease lease_;
    std::mutex mutex_;
    std::vector<Input> inputs_;
    // Closed first: no callback runs once it is destroyed.
    Stream device_;
};
class SdlAudio final : public AudioOutput {
    const std::thread::id owner_=std::this_thread::get_id();
    AudioFormat format_;
    std::shared_ptr<SharedDevice> device_;
    std::atomic<bool> paused_{false};
    Stream stream_;
    void thread() const { if (owner_!=std::this_thread::get_id()) throw std::logic_error("audio output used from another thread"); }
public:
    explicit SdlAudio(AudioFormat format) : format_(format) {
        if (format.rate<8000 || format.rate>192000 || !format.channels || format.channels>2) throw std::invalid_argument("invalid native audio output format");
        device_=SharedDevice::acquire();
        const SDL_AudioSpec spec{SDL_AUDIO_F32,static_cast<int>(format.channels),static_cast<int>(format.rate)};
        stream_.reset(SDL_CreateAudioStream(&spec,&mix_spec)); check(bool(stream_),"create native audio output");
        device_->add(stream_.get(),paused_);
    }
    ~SdlAudio() override { device_->remove(stream_.get()); }
    SdlAudio(const SdlAudio&)=delete;
    SdlAudio& operator=(const SdlAudio&)=delete;
    AudioFormat format() const override { thread(); return format_; }
    void write(std::span<const float> samples) override {
        thread(); const auto frames=samples.size()/format_.channels;
        if (samples.size()%format_.channels || frames>format_.rate || queued_frames()+frames>format_.rate) throw std::invalid_argument("native audio queue exceeds one second");
        if (std::any_of(samples.begin(),samples.end(),[](float sample) { return !std::isfinite(sample); })) throw std::invalid_argument("nonfinite native audio output");
        if (!samples.empty()) check(SDL_PutAudioStreamData(stream_.get(),samples.data(),static_cast<int>(samples.size_bytes())),"queue native audio frames");
    }
    std::size_t queued_frames() const override { thread(); const auto bytes=SDL_GetAudioStreamQueued(stream_.get()); check(bytes>=0,"query native audio queue"); return std::size_t(bytes)/(sizeof(float)*format_.channels); }
    void pause(bool paused) override { thread(); paused_.store(paused,std::memory_order_relaxed); }
    void clear() override { thread(); check(SDL_ClearAudioStream(stream_.get()),"clear native audio queue"); }
};
}
std::shared_ptr<AudioOutput> make_sdl_audio(AudioFormat format) { return std::make_shared<SdlAudio>(format); }
}
