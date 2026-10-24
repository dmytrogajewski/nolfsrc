#include "engine/client/audio_filter.h"
#include "engine/portable/string.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace nolf {
namespace {
using P=AudioFilterParameter;
constexpr P autopan[]={{"Autopan Mode",1,0,1},{"Autopan Rate",2000,1,60000}};
constexpr P band[]={{"BandPass Width",400,1,96000},{"BandPass Center",2000,1,96000}};
constexpr P chorus[]={{"Chorus Rate",11,0,100},{"Chorus Delay",20,0,1000},{"Chorus Depth",20,0,1000},{"Chorus Feedback",0,-.98f,.98f},{"Chorus Mix",.5f,0,1}};
constexpr P compressor[]={{"Compressor Ratio",4,1,100},{"Compressor Mix",1,0,1}};
constexpr P flange[]={{"Flange Depth",6,0,1000},{"Flange Feedback",.7f,-.98f,.98f},{"Flange Mix",.5f,0,1},{"Flange SweepRate",2,0,100}};
constexpr P high[]={{"HighPass Cutoff",2000,1,96000}};
constexpr P lagrange[]={{"LaGrange Filter Order",3,0,3}};
constexpr P low[]={{"LowPass Cutoff",100,1,96000}};
constexpr P mono[]={{"Mono Delay Feedback",.5f,-.98f,.98f},{"Mono Delay Mix",.5f,0,1},{"Mono Delay Time",200,0,2000}};
constexpr P parametric[]={{"ParamEQ Q",1,.05f,100},{"ParamEQ Center",1000,1,96000},{"ParamEQ Gain",-8,-60,24}};
constexpr P phaser[]={{"Phaser Rate",2,0,100},{"Phaser Range",4,0,10},{"Phaser Feedback",.8f,-.98f,.98f},{"Phaser BaseFreq",200,1,96000},{"Phaser Mix",.8f,0,1}};
constexpr P resonator[]={{"Resonator Center",1000,1,96000},{"Resonator Width",200,1,96000}};
constexpr P reverb1[]={{"Reverb Time",200,1,20000},{"Reverb Mix",.2f,0,1}};
constexpr P reverb2[]={{"Reverb Time",3000,1,20000},{"Reverb PreDelay",0,0,5000},{"Reverb Mix",.3f,0,1}};
constexpr P reverb3[]={{"Reverb Time",3000,1,20000},{"Reverb EAX Environment",0,0,25},{"Reverb Damping",5000,1,96000},{"Reverb Quality",0,0,3},{"Lowpass Cutoff",22050,1,96000},{"Reverb PreDelay",0,0,5000},{"Reverb Mix",0,0,1}};
constexpr P ring[]={{"Ring Mod Mix",.5f,0,1},{"Ring Mod Mode",1,0,1},{"Ring Mod Freq",2,0,20000}};
constexpr P stereo[]={{"Stereo Delay TimeL",200,0,2000},{"Stereo Delay TimeR",250,0,2000},{"Stereo Delay Feedback",.5f,-.98f,.98f},{"Stereo Delay Mix",.5f,0,1}};
constexpr P shelf[]={{"ShelvingEQ LowGain",-10,-60,24},{"ShelvingEQ LowFreq",500,1,96000},{"ShelvingEQ HighGain",2,-60,24},{"ShelvingEQ HighFreq",4000,1,96000}};
constexpr AudioFilterDescription descriptions[]={
    {"Autopan Filter",autopan},{"BandPass Filter",band},{"Chorus Filter",chorus},{"Compressor Filter",compressor},
    {"Flange Filter",flange},{"HighPass Filter",high},{"Lagrangian Interpolator",lagrange},{"LowPass Filter",low},
    {"Mono Delay Filter",mono},{"Parametric EQ Filter",parametric},{"Phaser Filter",phaser},{"Resonator EQ Filter",resonator},
    {"Reverb1 Filter",reverb1},{"Reverb2 Filter",reverb2},{"Reverb3 Filter",reverb3},{"Ring Modulator Filter",ring},
    {"Stereo Delay Filter",stereo},{"ShelvingEQ Filter",shelf}
};
constexpr double pi=std::numbers::pi;
bool equal(std::string_view a,std::string_view b) { return ascii_compare(a,b)==0; }
struct Delay {
    std::vector<float> data;
    std::size_t cursor=0;
    float low=0;
    void size(std::size_t n) { n=std::max<std::size_t>(n,2); if (data.size()!=n) { data.assign(n,0); cursor=0; low=0; } }
    float read(double frames) const {
        frames=std::clamp(frames,1.,double(data.size()-1)); const auto whole=static_cast<std::size_t>(frames); const auto fraction=static_cast<float>(frames-whole);
        const auto a=data[(cursor+data.size()-whole)%data.size()],b=data[(cursor+data.size()-whole-1)%data.size()]; return a+(b-a)*fraction;
    }
    void write(float sample) { data[cursor]=sample; cursor=(cursor+1)%data.size(); }
};
struct Biquad {
    double b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;
    float process(float x) { const auto y=b0*x+z1; z1=b1*x-a1*y+z2; z2=b2*x-a2*y; return static_cast<float>(y); }
    void coefficients(unsigned kind,double rate,double frequency,double q,double gain=0) {
        const auto w=2*pi*std::clamp(frequency,1.,rate*.49)/rate,c=std::cos(w),s=std::sin(w),alpha=s/(2*std::clamp(q,.05,100.));
        double a0=1+alpha; a1=-2*c; a2=1-alpha;
        if (kind==0) { b0=(1-c)/2; b1=1-c; b2=b0; }
        else if (kind==1) { b0=(1+c)/2; b1=-(1+c); b2=b0; }
        else if (kind==2) { b0=alpha; b1=0; b2=-alpha; }
        else { const auto amplitude=std::pow(10.,gain/40); b0=1+alpha*amplitude; b1=-2*c; b2=1-alpha*amplitude; a0=1+alpha/amplitude; a2=1-alpha/amplitude; }
        b0/=a0; b1/=a0; b2/=a0; a1/=a0; a2/=a0;
    }
};
}
struct AudioFilter::State {
    unsigned kind,rate;
    std::vector<float> parameters;
    std::array<Delay,12> delays;
    std::array<Biquad,2> biquad;
    std::array<float,2> envelope{},low{},high{},feedback{};
    std::array<std::array<float,4>,2> allpass{};
    double phase=0;
    State(unsigned id,unsigned frequency) : kind(id),rate(frequency) { for (const auto& p : descriptions[kind].parameters) parameters.push_back(p.initial); configure(); }
    void configure() {
        const auto& p=parameters;
        if (kind==1 || kind==5 || kind==7 || kind==9 || kind==11) {
            const auto mode=kind==5 ? 1u : kind==7 ? 0u : kind==9 ? 3u : 2u;
            const auto frequency=kind==1 || kind==9 ? p[1] : p[0];
            const auto q=kind==1 || kind==11 ? frequency/(kind==1 ? p[0] : p[1]) : kind==9 ? p[0] : .70710678f;
            for (auto& filter : biquad) filter.coefficients(mode,rate,frequency,q,kind==9 ? p[2] : 0);
        }
        double delay_ms=0;
        if (kind==2) delay_ms=p[1]+p[2]; else if (kind==4) delay_ms=p[0]*2; else if (kind==8) delay_ms=p[2]; else if (kind==16) delay_ms=std::max(p[0],p[1]);
        if (kind==2 || kind==4 || kind==8 || kind==16) for (unsigned i=0;i<2;++i) delays[i].size(static_cast<std::size_t>(delay_ms*.001*rate)+3);
        if (kind>=12 && kind<=14) {
            const auto pre=kind==12 ? 0 : kind==13 ? p[1] : p[5];
            for (unsigned i=0;i<2;++i) delays[8+i].size(static_cast<std::size_t>(pre*.001*rate)+3);
            // Parallel, mutually incommensurate delay lines spread reflections.
            constexpr double lengths[]={.0297,.0371,.0411,.0437};
            const auto scale=kind==14 ? .7+parameters[1]*.035 : 1.;
            for (unsigned i=0;i<8;++i) delays[i].size(static_cast<std::size_t>((lengths[i/2]+(i%2)*.0013)*rate*scale)+2);
        }
    }
    void process(float& left,float& right) {
        auto& p=parameters; float x[2]={left,right}; const auto wet=[&](unsigned ch,float effect,float mix) { x[ch]=x[ch]*(1-mix)+effect*mix; };
        const auto lfo=[&](double hz) { const auto value=std::sin(phase*2*pi); phase=std::fmod(phase+hz/rate,1.); return static_cast<float>(value); };
        if (kind==0) { const auto t=lfo(1000./p[1]); const auto pan=p[0]<.5f ? t : static_cast<float>(2/pi*std::asin(t)); x[0]*=(1-pan)*.5f; x[1]*=(1+pan)*.5f; }
        else if (kind==1 || kind==5 || kind==7 || kind==9 || kind==11) for (unsigned ch=0;ch<2;++ch) x[ch]=biquad[ch].process(x[ch]);
        else if (kind==3) {
            for (unsigned ch=0;ch<2;++ch) { const auto amplitude=std::abs(x[ch]); const auto coefficient=std::exp(-1./(rate*(amplitude>envelope[ch] ? .005 : .1))); envelope[ch]=static_cast<float>(amplitude+(envelope[ch]-amplitude)*coefficient); const auto gain=envelope[ch]>.25f ? std::pow(envelope[ch]/.25f,1/p[0]-1) : 1.; wet(ch,static_cast<float>(x[ch]*gain),p[1]); }
        } else if (kind==2 || kind==4 || kind==8 || kind==16) {
            const auto modulation=kind==2 ? lfo(p[0]) : kind==4 ? lfo(p[3]) : 0;
            const auto mix=kind==2 ? p[4] : kind==4 ? p[2] : kind==8 ? p[1] : p[3];
            const auto feedback_gain=kind==2 ? p[3] : kind==4 ? p[1] : kind==8 ? p[0] : p[2];
            const auto monaural=(x[0]+x[1])*.5f;
            for (unsigned ch=0;ch<2;++ch) {
                const auto delay_ms=kind==2 ? std::max(0.f,p[1]+modulation*p[2]) : kind==4 ? p[0]*(1+modulation) : kind==8 ? p[2] : p[ch];
                const auto delayed=delays[ch].read(delay_ms*.001*rate); delays[ch].write((kind==8 ? monaural : x[ch])+delayed*feedback_gain); wet(ch,delayed,mix);
            }
        } else if (kind==10) {
            const auto frequency=std::clamp(double(p[3])*std::pow(2.,lfo(p[0])*p[1]),1.,rate*.45);
            const auto tangent=std::tan(pi*frequency/rate); const auto coefficient=static_cast<float>((1-tangent)/(1+tangent));
            for (unsigned ch=0;ch<2;++ch) {
                auto y=x[ch]+feedback[ch]*p[2];
                for (auto& z : allpass[ch]) { const auto out=-coefficient*y+z; z=y+coefficient*out; y=out; }
                feedback[ch]=y; wet(ch,y,p[4]);
            }
        } else if (kind>=12 && kind<=14) {
            const auto mix=kind==12 ? p[1] : kind==13 ? p[2] : p[6];
            const auto cutoff=kind==14 ? std::min(p[2],p[4]) : float(rate)*.45f;
            const auto damping=static_cast<float>(1-std::exp(-2*pi*std::clamp(double(cutoff),1.,rate*.49)/rate));
            const auto pre_ms=kind==12 ? 0 : kind==13 ? p[1] : p[5];
            for (unsigned ch=0;ch<2;++ch) {
                auto input=x[ch]; if (pre_ms>0) { input=delays[8+ch].read(pre_ms*.001*rate); delays[8+ch].write(x[ch]); }
                float sum=0;
                for (unsigned i=0;i<4;++i) {
                    auto& delay=delays[i*2+ch]; const auto reflected=delay.read(double(delay.data.size()-1)); delay.low+=(reflected-delay.low)*damping;
                    const auto decay=static_cast<float>(std::pow(.001,double(delay.data.size()-1)/rate/(p[0]*.001)));
                    delay.write(input+delay.low*decay); sum+=reflected;
                }
                wet(ch,sum*.25f,mix);
            }
        } else if (kind==15) {
            auto modulation=lfo(p[2]); if (p[1]>=.5f) modulation=static_cast<float>(2/pi*std::asin(modulation));
            wet(0,x[0]*modulation,p[0]); wet(1,x[1]*modulation,p[0]);
        } else if (kind==17) {
            const auto a=static_cast<float>(1-std::exp(-2*pi*std::min(double(p[1]),rate*.49)/rate)),b=static_cast<float>(1-std::exp(-2*pi*std::min(double(p[3]),rate*.49)/rate));
            const auto gl=static_cast<float>(std::pow(10.,p[0]/20)),gh=static_cast<float>(std::pow(10.,p[2]/20));
            for (unsigned ch=0;ch<2;++ch) { low[ch]+=(x[ch]-low[ch])*a; high[ch]+=(x[ch]-high[ch])*b; x[ch]+=low[ch]*(gl-1)+(x[ch]-high[ch])*(gh-1); }
        }
        left=x[0]; right=x[1];
    }
};
AudioFilter::AudioFilter(std::string_view name,unsigned rate) {
    if (rate<8000 || rate>192000) throw std::invalid_argument("invalid filter sample rate");
    for (unsigned i=0;i<std::size(descriptions);++i) if (equal(name,descriptions[i].name)) { state_=std::make_unique<State>(i,rate); return; }
    throw std::invalid_argument("unknown native audio filter");
}
AudioFilter::~AudioFilter()=default;
std::span<const AudioFilterDescription> AudioFilter::catalog() { return descriptions; }
bool AudioFilter::parameter(std::string_view name,float value) {
    if (!std::isfinite(value)) throw std::invalid_argument("nonfinite audio filter parameter");
    const auto description=descriptions[state_->kind];
    for (std::size_t i=0;i<description.parameters.size();++i) if (equal(name,description.parameters[i].name)) {
        const auto& p=description.parameters[i]; if (value<p.minimum || value>p.maximum) throw std::invalid_argument("audio filter parameter outside range");
        auto next=std::make_unique<State>(state_->kind,state_->rate); next->parameters=state_->parameters; next->parameters[i]=value; next->configure(); state_=std::move(next); return true;
    }
    return false;
}
std::optional<float> AudioFilter::parameter(std::string_view name) const {
    const auto& description=descriptions[state_->kind]; for (std::size_t i=0;i<description.parameters.size();++i) if (equal(name,description.parameters[i].name)) return state_->parameters[i]; return {};
}
std::unique_ptr<AudioFilter> AudioFilter::reconfigure(unsigned rate) const {
    auto result=std::make_unique<AudioFilter>(descriptions[state_->kind].name,rate); result->state_->parameters=state_->parameters; result->state_->configure(); return result;
}
void AudioFilter::process(float& left,float& right) { state_->process(left,right); }
unsigned AudioFilter::interpolation_order() const { return state_->kind==6 ? static_cast<unsigned>(std::lround(state_->parameters[0])) : 1; }
std::size_t AudioFilter::bytes() const { std::size_t bytes=sizeof(State)+state_->parameters.capacity()*sizeof(float); for (const auto& line : state_->delays) bytes+=line.data.capacity()*sizeof(float); return bytes; }
}
