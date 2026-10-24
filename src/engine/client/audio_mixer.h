#pragma once
#include "engine/client/audio_clips.h"
#include "engine/client/audio_filter.h"
#include "ltvector.h"

namespace nolf {
using AudioVoice=std::uint64_t;
struct AudioVoiceSettings {
    LTVector position{0,0,0},velocity{0,0,0};
    float gain=1,pitch=1,inner_radius=0,outer_radius=1000;
    float occlusion=0,obstruction=0;
    unsigned priority=128;
    bool loop=false,spatial=false,ambient=false,retain=false,reverb=false;
};
struct AudioListener {
    LTVector position{0,0,0},right{1,0,0},velocity{0,0,0};
    float distance_factor=.01f;
};
struct AudioVoiceStatus {
    std::shared_ptr<const AudioClip> clip;
    AudioVoiceSettings settings;
    double frame=0;
    bool done=false;
};
// Pure sample mixing owns voice clocks, interpolation, authored loops,
// distance/pan/doppler and priority virtualization; AudioOutput owns playback.
class AudioMixer {
    struct State;
    std::unique_ptr<State> state_;
public:
    explicit AudioMixer(platform::AudioFormat = {},unsigned audible_voices=128);
    ~AudioMixer();
    AudioMixer(const AudioMixer&)=delete;
    AudioMixer& operator=(const AudioMixer&)=delete;
    // Device changes preserve voice identities and source-frame clocks.
    void configure(platform::AudioFormat,unsigned audible_voices);
    AudioVoice play(std::shared_ptr<const AudioClip>,AudioVoiceSettings = {});
    bool stop(AudioVoice);
    bool release(AudioVoice);
    bool settings(AudioVoice,const AudioVoiceSettings&);
    bool filter(AudioVoice,std::string_view);
    bool filter_parameter(AudioVoice,std::string_view,float);
    std::optional<float> filter_parameter(AudioVoice,std::string_view) const;
    void reverb(float volume,float reflection,float decay,float damping,unsigned acoustics);
    std::optional<AudioVoiceStatus> status(AudioVoice) const;
    void listener(const AudioListener&);
    AudioListener listener() const;
    void gain(float);
    void pause(bool);
    void render(std::span<float> stereo);
    std::size_t voices() const;
};
}
