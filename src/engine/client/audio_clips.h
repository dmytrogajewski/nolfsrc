#pragma once
#include "engine/portable/wave.h"
#include "platform/audio.h"

namespace nolf {
struct AudioAllocation;
class AudioClip {
    friend class AudioClips;
    AudioClip()=default;
    std::shared_ptr<AudioAllocation> allocation_;
    WaveMetadata wave_;
    std::string name_;
    std::vector<float> samples_;
public:
    ~AudioClip()=default;
    const std::string& name() const { return name_; }
    const WaveMetadata& wave() const { return wave_; }
    std::span<const float> samples() const { return samples_; }
    std::size_t frames() const { return samples_.size()/wave_.channels; }
    double duration() const { return double(frames())/wave_.sample_rate; }
};
// The SDK and mixer retain immutable clips from this shared, bounded cache.
// Live voices count against its budget even after level/cache retirement.
class AudioClips {
    struct State;
    std::shared_ptr<State> state_;
public:
    AudioClips(std::shared_ptr<const Assets>,platform::AudioDecoderFactory,std::size_t budget=128*1024*1024);
    ~AudioClips();
    AudioClips(const AudioClips&)=delete;
    AudioClips& operator=(const AudioClips&)=delete;
    std::shared_ptr<const AudioClip> load(std::string_view);
    bool contains(std::string_view) const;
    void collect();
    std::size_t bytes() const;
    std::size_t cached() const;
};
}
