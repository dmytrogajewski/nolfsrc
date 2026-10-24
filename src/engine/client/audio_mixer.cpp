#include "engine/client/audio_mixer.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <map>
#include <thread>

namespace nolf {
namespace {
std::atomic<std::uint64_t> next_voice{1};
bool finite(LTVector value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::abs(value.x)<=1e12f && std::abs(value.y)<=1e12f && std::abs(value.z)<=1e12f; }
double dot(LTVector a,LTVector b) { return double(a.x)*b.x+double(a.y)*b.y+double(a.z)*b.z; }
void validate(const AudioVoiceSettings& settings) {
    if (!finite(settings.position) || !finite(settings.velocity) || !std::isfinite(settings.gain) || settings.gain<0 || settings.gain>1 || !std::isfinite(settings.pitch) || settings.pitch<0 || settings.pitch>16 || !std::isfinite(settings.inner_radius) || !std::isfinite(settings.outer_radius) || settings.inner_radius<0 || settings.outer_radius<settings.inner_radius || settings.outer_radius>1e9f || !std::isfinite(settings.occlusion) || !std::isfinite(settings.obstruction) || settings.occlusion<0 || settings.occlusion>1 || settings.obstruction<0 || settings.obstruction>1 || settings.priority>255) throw std::invalid_argument("invalid native audio voice settings");
}
struct Voice {
    AudioVoiceStatus status;
    std::unique_ptr<AudioFilter> filter;
    float filtered_left=0,filtered_right=0;
    void normalize() {
        auto& s=status; const auto frames=s.clip->frames(); std::size_t start=0,end=frames;
        if (s.settings.loop && s.clip->wave().loop && s.clip->wave().loop->length) {
            const auto& loop=*s.clip->wave().loop;
            start=std::min<std::size_t>(loop.start,frames); end=std::min<std::size_t>(std::size_t(loop.start)+loop.length,frames);
        }
        if (s.frame>=double(end)) {
            if (s.settings.loop && start<end) s.frame=double(start)+std::fmod(s.frame-double(start),double(end-start));
            else { s.frame=double(frames); s.done=true; }
        }
    }
    float sample(unsigned channel) const {
        const auto& s=status; const auto channels=s.clip->wave().channels; const auto frames=s.clip->frames(); const auto first=std::min<std::size_t>(static_cast<std::size_t>(s.frame),frames-1);
        auto next=std::min(first+1,frames-1);
        if (s.settings.loop) {
            std::size_t start=0,end=frames; if (const auto& loop=s.clip->wave().loop; loop && loop->length) { start=std::min<std::size_t>(loop->start,frames); end=std::min<std::size_t>(std::size_t(loop->start)+loop->length,frames); }
            if (first+1>=end && start<end) next=start;
        }
        const auto fraction=static_cast<float>(s.frame-double(first)); const auto samples=s.clip->samples(); channel=std::min<unsigned>(channel,channels-1);
        const auto order=filter ? filter->interpolation_order() : 1;
        if (order==0) return samples[(fraction<.5f ? first : next)*channels+channel];
        if (order>1) {
            const auto at=[&](std::int64_t index) {
                std::int64_t start=0,end=static_cast<std::int64_t>(frames);
                if (s.settings.loop && s.clip->wave().loop && s.clip->wave().loop->length) { const auto& loop=*s.clip->wave().loop; start=loop.start; end=std::min<std::int64_t>(start+loop.length,end); }
                if (s.settings.loop && end>start && (index>=end || (index<start && first>=std::size_t(start)))) { index=start+(index-start)%(end-start); if (index<start) index+=end-start; }
                index=std::clamp<std::int64_t>(index,0,static_cast<std::int64_t>(frames)-1); return samples[std::size_t(index)*channels+channel];
            };
            const auto t=fraction,y0=at(static_cast<std::int64_t>(first)),y1=at(static_cast<std::int64_t>(first)+1),y2=at(static_cast<std::int64_t>(first)+2);
            if (order==2) return y0*(t-1)*(t-2)*.5f-y1*t*(t-2)+y2*t*(t-1)*.5f;
            const auto ym=at(static_cast<std::int64_t>(first)-1);
            return -ym*t*(t-1)*(t-2)/6+y0*(t+1)*(t-1)*(t-2)*.5f-y1*(t+1)*t*(t-2)*.5f+y2*(t+1)*t*(t-1)/6;
        }
        return samples[first*channels+channel]+(samples[next*channels+channel]-samples[first*channels+channel])*fraction;
    }
};
}
struct AudioMixer::State {
    const std::thread::id owner=std::this_thread::get_id();
    platform::AudioFormat format;
    AudioListener listener;
    unsigned audible=128;
    float gain=1;
    bool paused=false;
    std::map<AudioVoice,Voice> voices;
    std::unique_ptr<AudioFilter> reverb;
    float reverb_gain=0;
    static constexpr std::size_t filter_budget=32*1024*1024;
    void thread() const { if (owner!=std::this_thread::get_id()) throw std::logic_error("audio mixer used from another thread"); }
    void budget(AudioVoice replaced,const AudioFilter* next) const {
        std::size_t bytes=next ? next->bytes() : 0;
        if (replaced && reverb) bytes+=reverb->bytes();
        for (const auto& [id,voice] : voices) if (id!=replaced && voice.filter) bytes+=voice.filter->bytes();
        if (bytes>filter_budget) throw std::length_error("native sound filters exhaust allocation budget");
    }
};
AudioMixer::AudioMixer(platform::AudioFormat format,unsigned audible) : state_(std::make_unique<State>()) {
    configure(format,audible);
}
void AudioMixer::configure(platform::AudioFormat format,unsigned audible) {
    state_->thread();
    if (format.rate<8000 || format.rate>192000 || format.channels!=2 || !audible || audible>256) throw std::invalid_argument("invalid native audio mixer format/budget");
    if (format!=state_->format) {
        std::vector<std::pair<AudioVoice,std::unique_ptr<AudioFilter>>> filters; std::size_t bytes=0;
        auto room=state_->reverb ? state_->reverb->reconfigure(format.rate) : nullptr; if (room) bytes+=room->bytes();
        for (const auto& [id,voice] : state_->voices) if (voice.filter) { auto next=voice.filter->reconfigure(format.rate); bytes+=next->bytes(); if (bytes>State::filter_budget) throw std::length_error("new audio format exceeds filter budget"); filters.emplace_back(id,std::move(next)); }
        for (auto& [id,next] : filters) state_->voices.at(id).filter=std::move(next);
        state_->reverb=std::move(room);
    }
    state_->format=format; state_->audible=audible;
}
AudioMixer::~AudioMixer()=default;
AudioVoice AudioMixer::play(std::shared_ptr<const AudioClip> clip,AudioVoiceSettings settings) {
    state_->thread(); validate(settings); if (!clip || !clip->frames()) throw std::invalid_argument("empty native audio voice"); if (state_->voices.size()>=4096) throw std::length_error("native audio voice limit exceeded");
    auto id=next_voice.load(std::memory_order_relaxed);
    do { if (!id || id==std::numeric_limits<AudioVoice>::max()) throw std::overflow_error("native audio voice identities exhausted"); } while (!next_voice.compare_exchange_weak(id,id+1,std::memory_order_relaxed));
    Voice voice; voice.status.clip=std::move(clip); voice.status.settings=settings; state_->voices.emplace(id,std::move(voice)); return id;
}
bool AudioMixer::stop(AudioVoice voice) { state_->thread(); return state_->voices.erase(voice)!=0; }
bool AudioMixer::release(AudioVoice id) { state_->thread(); const auto it=state_->voices.find(id); if (it==state_->voices.end()) return false; it->second.status.settings.retain=false; if (it->second.status.done) state_->voices.erase(it); return true; }
bool AudioMixer::settings(AudioVoice id,const AudioVoiceSettings& settings) { state_->thread(); validate(settings); const auto it=state_->voices.find(id); if (it==state_->voices.end()) return false; it->second.status.settings=settings; return true; }
bool AudioMixer::filter(AudioVoice id,std::string_view name) {
    state_->thread(); const auto it=state_->voices.find(id); if (it==state_->voices.end()) return false;
    auto next=name.empty() ? nullptr : std::make_unique<AudioFilter>(name,state_->format.rate); state_->budget(id,next.get()); it->second.filter=std::move(next); return true;
}
bool AudioMixer::filter_parameter(AudioVoice id,std::string_view name,float value) {
    state_->thread(); const auto it=state_->voices.find(id); if (it==state_->voices.end() || !it->second.filter) return false;
    auto next=it->second.filter->reconfigure(state_->format.rate); if (!next->parameter(name,value)) return false;
    state_->budget(id,next.get()); it->second.filter=std::move(next); return true;
}
std::optional<float> AudioMixer::filter_parameter(AudioVoice id,std::string_view name) const {
    state_->thread(); const auto it=state_->voices.find(id); return it==state_->voices.end() || !it->second.filter ? std::nullopt : it->second.filter->parameter(name);
}
void AudioMixer::reverb(float volume,float reflection,float decay,float damping,unsigned acoustics) {
    state_->thread();
    if (!std::isfinite(volume) || !std::isfinite(reflection) || !std::isfinite(decay) || !std::isfinite(damping) || volume<0 || volume>1 || reflection<0 || reflection>5 || decay<.1f || decay>20 || damping<0 || damping>2 || acoustics>25) throw std::invalid_argument("invalid native room reverb");
    auto next=std::make_unique<AudioFilter>("Reverb3 Filter",state_->format.rate);
    next->parameter("Reverb Time",std::min(20000.f,decay*1000)); next->parameter("Reverb PreDelay",reflection*1000);
    next->parameter("Reverb Damping",std::clamp(12000.f/std::pow(4.f,damping),1.f,96000.f)); next->parameter("Reverb EAX Environment",static_cast<float>(acoustics)); next->parameter("Reverb Mix",1);
    state_->budget(0,next.get()); state_->reverb=std::move(next); state_->reverb_gain=volume;
}
std::optional<AudioVoiceStatus> AudioMixer::status(AudioVoice id) const { state_->thread(); const auto it=state_->voices.find(id); return it==state_->voices.end() ? std::nullopt : std::optional(it->second.status); }
void AudioMixer::listener(const AudioListener& listener) {
    state_->thread(); const auto length=std::sqrt(dot(listener.right,listener.right));
    if (!finite(listener.position) || !finite(listener.velocity) || !finite(listener.right) || !std::isfinite(listener.distance_factor) || listener.distance_factor<=0 || listener.distance_factor>1000 || length<1e-6) throw std::invalid_argument("invalid native audio listener");
    state_->listener=listener; state_->listener.right/=static_cast<float>(length);
}
AudioListener AudioMixer::listener() const { state_->thread(); return state_->listener; }
void AudioMixer::gain(float gain) { state_->thread(); if (!std::isfinite(gain) || gain<0 || gain>1) throw std::invalid_argument("invalid master audio gain"); state_->gain=gain; }
void AudioMixer::pause(bool paused) { state_->thread(); state_->paused=paused; }
std::size_t AudioMixer::voices() const { state_->thread(); return state_->voices.size(); }
void AudioMixer::render(std::span<float> output) {
    auto& self=*state_; self.thread(); if (output.size()%2 || output.size()/2>self.format.rate) throw std::invalid_argument("invalid native audio mixing block"); std::fill(output.begin(),output.end(),0);
    if (self.paused) return;
    std::vector<Voice*> voices; voices.reserve(self.voices.size());
    for (auto& [id,voice] : self.voices) if (!voice.status.done) voices.push_back(&voice);
    const auto audible=[&](const Voice* voice) {
        const auto& settings=voice->status.settings; if (settings.gain==0) return false;
        if (!settings.spatial && !settings.ambient) return true;
        const auto delta=settings.position-self.listener.position; return dot(delta,delta)<double(settings.outer_radius)*settings.outer_radius;
    };
    std::stable_sort(voices.begin(),voices.end(),[&](const auto* a,const auto* b) { const bool aa=audible(a),ab=audible(b); return aa!=ab ? aa>ab : a->status.settings.priority>b->status.settings.priority; });
    const auto frames=output.size()/2;
    std::vector<float> room(self.reverb && self.reverb_gain>0 ? output.size() : 0,0);
    for (std::size_t index=0;index<voices.size();++index) {
        auto& voice=*voices[index]; auto& s=voice.status; const auto& settings=s.settings;
        double left=settings.gain,right=left,doppler=1;
        if (settings.spatial || settings.ambient) {
            const auto delta=settings.position-self.listener.position; const auto distance=std::sqrt(dot(delta,delta));
            const auto gain=distance<=settings.inner_radius ? 1. : distance>=settings.outer_radius ? 0. : (settings.outer_radius-distance)/(settings.outer_radius-settings.inner_radius);
            left*=gain; right*=gain;
            if (settings.spatial && distance>1e-6) {
                const auto direction=delta/static_cast<float>(distance); const auto pan=std::clamp(dot(direction,self.listener.right),-1.,1.);
                left*=std::sqrt((1-pan)*.5); right*=std::sqrt((1+pan)*.5);
                const auto projected=[&](LTVector velocity) { return std::clamp(dot(velocity,direction)*self.listener.distance_factor,-308.97,308.97); };
                doppler=std::clamp((343.3+projected(self.listener.velocity))/(343.3+projected(settings.velocity)),.25,4.);
            }
        }
        const auto step=double(s.clip->wave().sample_rate)/self.format.rate*settings.pitch*doppler;
        if (index>=self.audible) { s.frame+=double(frames)*step; voice.normalize(); continue; }
        const auto filter=1-std::exp(-2*3.141592653589793*16000*std::pow(.0625,settings.occlusion)/self.format.rate);
        const auto damping=(1-.7*settings.occlusion)*(1-.5*settings.obstruction);
        for (std::size_t frame=0;frame<frames;++frame) {
            voice.normalize(); if (s.done) break;
            float l=voice.sample(0),r=voice.sample(1); if (settings.spatial) l=r=(l+r)*.5f;
            if (voice.filter) voice.filter->process(l,r);
            if (settings.occlusion>0) { voice.filtered_left+=static_cast<float>((l-voice.filtered_left)*filter); voice.filtered_right+=static_cast<float>((r-voice.filtered_right)*filter); l=voice.filtered_left; r=voice.filtered_right; }
            const auto a=static_cast<float>(l*left*damping),b=static_cast<float>(r*right*damping);
            output[frame*2]+=a; output[frame*2+1]+=b;
            if (!room.empty() && settings.reverb) { room[frame*2]+=a; room[frame*2+1]+=b; }
            s.frame+=step;
        }
        voice.normalize();
    }
    if (!room.empty()) for (std::size_t frame=0;frame<frames;++frame) { auto& l=room[frame*2]; auto& r=room[frame*2+1]; self.reverb->process(l,r); output[frame*2]+=l*self.reverb_gain; output[frame*2+1]+=r*self.reverb_gain; }
    for (auto& sample : output) sample=std::clamp(sample*self.gain,-1.0f,1.0f);
    std::erase_if(self.voices,[](const auto& entry) { return entry.second.status.done && !entry.second.status.settings.retain; });
}
}
