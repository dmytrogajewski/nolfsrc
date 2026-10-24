#include "engine/client/audio_clips.h"
#include "engine/portable/media_source.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <map>
#include <thread>

namespace nolf {
namespace {
struct AudioBudget { const std::size_t limit; std::atomic<std::size_t> used{0}; explicit AudioBudget(std::size_t value) : limit(value) {} };
}
struct AudioAllocation {
    std::shared_ptr<AudioBudget> budget;
    std::size_t bytes=0;
    ~AudioAllocation() { if (budget) budget->used.fetch_sub(bytes,std::memory_order_relaxed); }
};
struct AudioClips::State {
    const std::thread::id owner=std::this_thread::get_id();
    std::shared_ptr<const Assets> assets;
    platform::AudioDecoderFactory decoder;
    std::shared_ptr<AudioBudget> budget;
    struct Entry { std::shared_ptr<const AudioClip> clip; std::uint64_t used=0; };
    std::map<std::string,Entry> entries;
    std::uint64_t clock=0;
    void thread() const { if (owner!=std::this_thread::get_id()) throw std::logic_error("audio clip cache used from another thread"); }
    void reserve(AudioAllocation& allocation,std::size_t size) {
        if (size>budget->limit) throw std::length_error("audio clip exceeds cache budget");
        if (size<=allocation.bytes) { budget->used.fetch_sub(allocation.bytes-size,std::memory_order_relaxed); allocation.bytes=size; return; }
        const auto extra=size-allocation.bytes;
        while (extra>budget->limit-budget->used.load(std::memory_order_relaxed)) {
            auto victim=entries.end();
            for (auto it=entries.begin();it!=entries.end();++it) if (it->second.clip.use_count()==1 && (victim==entries.end() || it->second.used<victim->second.used)) victim=it;
            if (victim==entries.end()) throw std::length_error("live audio clips exhaust cache budget");
            entries.erase(victim);
        }
        budget->used.fetch_add(extra,std::memory_order_relaxed); allocation.bytes=size;
    }
};
AudioClips::AudioClips(std::shared_ptr<const Assets> assets,platform::AudioDecoderFactory decoder,std::size_t budget) : state_(std::make_shared<State>()) {
    if (!assets || !decoder || budget<sizeof(float) || budget>2ull*1024*1024*1024) throw std::invalid_argument("invalid native audio cache request");
    state_->assets=std::move(assets); state_->decoder=std::move(decoder); state_->budget=std::make_shared<AudioBudget>(budget);
}
AudioClips::~AudioClips()=default;
std::shared_ptr<const AudioClip> AudioClips::load(std::string_view name) {
    const auto self=state_; self->thread(); const auto key=platform::asset_key(name);
    if (const auto found=self->entries.find(key);found!=self->entries.end()) { found->second.used=++self->clock; return found->second.clip; }
    auto clip=std::shared_ptr<AudioClip>(new AudioClip); clip->name_=key;
    const auto source=self->assets->locate(key); clip->wave_=read_wave(source);
    const platform::AudioFormat format{clip->wave_.sample_rate,clip->wave_.channels};
    auto decoder=self->decoder(std::make_shared<AssetMediaSource>(source),format,clip->wave_.frames);
    if (!decoder || decoder->format()!=format) throw std::runtime_error("sound decoder did not preserve source rate/channels");
    clip->allocation_=std::make_shared<AudioAllocation>(); clip->allocation_->budget=self->budget;
    if (clip->wave_.frames) {
        if (*clip->wave_.frames>self->budget->limit/(sizeof(float)*format.channels)) throw std::length_error("sound source exceeds audio cache budget");
        const auto samples=std::size_t(*clip->wave_.frames)*format.channels; self->reserve(*clip->allocation_,samples*sizeof(float)); clip->samples_.resize(samples);
        std::size_t used=0;
        while (used<samples) { const auto frames=decoder->read(std::span(clip->samples_).subspan(used)); if (!frames) break; if (frames>(samples-used)/format.channels) throw std::runtime_error("sound decoder exceeded requested output"); used+=frames*format.channels; }
        if (used!=samples && clip->wave_.format!=85) throw std::runtime_error("truncated decoded sound frames");
        clip->samples_.resize(used); self->reserve(*clip->allocation_,clip->samples_.capacity()*sizeof(float));
    } else {
        std::array<float,4096> block{};
        while (const auto frames=decoder->read(std::span(block).first(block.size()/format.channels*format.channels))) {
            if (frames>block.size()/format.channels) throw std::runtime_error("sound decoder exceeded requested block");
            const auto samples=frames*format.channels; const auto size=clip->samples_.size()+samples;
            if (size<samples || size>self->budget->limit/sizeof(float)) throw std::length_error("decoded sound exceeds audio cache budget");
            self->reserve(*clip->allocation_,size*sizeof(float)); clip->samples_.reserve(size); self->reserve(*clip->allocation_,clip->samples_.capacity()*sizeof(float)); clip->samples_.insert(clip->samples_.end(),block.begin(),block.begin()+samples);
        }
    }
    if (!clip->frames()) throw std::runtime_error("sound has no decoded audio frames");
    self->entries.emplace(key,State::Entry{clip,++self->clock}); return clip;
}
void AudioClips::collect() { const auto self=state_; self->thread(); std::erase_if(self->entries,[](const auto& entry) { return entry.second.clip.use_count()==1; }); }
bool AudioClips::contains(std::string_view name) const { const auto self=state_; self->thread(); return self->assets->contains(name); }
std::size_t AudioClips::bytes() const { const auto self=state_; self->thread(); return self->budget->used.load(std::memory_order_relaxed); }
std::size_t AudioClips::cached() const { state_->thread(); return state_->entries.size(); }
}
