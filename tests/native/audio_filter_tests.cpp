#include "engine/client/audio_filter.h"
#include "engine/client/audio_mixer.h"
#include "engine/portable/string.h"
#include "audio_clip_fixture.h"
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>

namespace {
void require(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
template<class F> void rejects(F&& function) { bool rejected=false; try { function(); } catch (const std::exception&) { rejected=true; } require(rejected,"invalid filter operation accepted"); }
double response(const char* name,double frequency) {
    nolf::AudioFilter filter(name,16000); filter.parameter(name[0]=='L' ? "LowPass Cutoff" : "HighPass Cutoff",500);
    double energy=0; for (unsigned i=0;i<16000;++i) { float l=static_cast<float>(std::sin(2*3.141592653589793*frequency*i/16000)),r=l; filter.process(l,r); require(l==r,"symmetric filter changed stereo image"); if (i>=8000) energy+=double(l)*l; } return energy;
}
void checks() {
    require(nolf::AudioFilter::catalog().size()==18,"filter catalog incomplete");
    require(response("LowPass Filter",100)>response("LowPass Filter",4000)*100,"underwater low-pass does not reject high frequencies");
    require(response("HighPass Filter",4000)>response("HighPass Filter",100)*100,"high-pass does not reject bass");
    nolf::AudioFilter delay("Stereo Delay Filter",8000); delay.parameter("Stereo Delay TimeL",10); delay.parameter("Stereo Delay TimeR",20); delay.parameter("Stereo Delay Mix",1); delay.parameter("Stereo Delay Feedback",0);
    for (unsigned i=0;i<200;++i) { float l=i==0 ? 1.f : 0,r=l; delay.process(l,r); require(std::abs(l-(i==80 ? 1.f : 0))<1e-5 && std::abs(r-(i==160 ? 1.f : 0))<1e-5,"authored stereo delay timing differs"); }
    for (const auto rate : {8000u,44100u,192000u}) for (const auto& description : nolf::AudioFilter::catalog()) {
        nolf::AudioFilter filter(description.name,rate); require(filter.bytes()<16*1024*1024,"one filter exceeds bounded allocation");
        for (const auto& parameter : description.parameters) for (const auto value : {parameter.minimum,parameter.maximum,parameter.initial}) {
            require(filter.parameter(parameter.name,value) && filter.parameter(parameter.name)==value,"filter parameter roundtrip failed");
            for (unsigned i=0;i<4096;++i) { float l=i%31==0 ? .25f : 0,r=-l; filter.process(l,r); require(std::isfinite(l) && std::isfinite(r) && std::abs(l)<10000 && std::abs(r)<10000,"bounded filter produced unstable samples"); }
        }
        auto changed=filter.reconfigure(22050); for (const auto& p : description.parameters) require(changed->parameter(p.name)==filter.parameter(p.name),"rate change lost authored parameter");
        require(!filter.parameter("unknown",1) && !filter.parameter("unknown"),"unknown parameter accepted");
        rejects([&] { filter.parameter(description.parameters[0].name,std::numeric_limits<float>::quiet_NaN()); });
    }
    audio_clip_fixture::Files files; auto assets=std::make_shared<nolf::Assets>(); assets->mount_directory(files.root); nolf::AudioClips cache(assets,nolf::platform::make_ffmpeg_audio); auto clip=cache.load("a.wav");
    nolf::AudioMixer mixer({8000,2}); mixer.reverb(.5f,0,1,1,8); nolf::AudioVoiceSettings settings; settings.reverb=true; const auto voice=mixer.play(clip,settings); std::vector<float> samples(8000); mixer.render(samples); require(!mixer.status(voice),"reverb retained completed one-shot"); mixer.render(samples);
    double energy=0; for (const auto x : samples) energy+=double(x)*x; require(energy>1e-8,"room reflections stopped when source sound retired");
    mixer.reverb(0,0,1,1,0);
    settings={}; settings.pitch=.5f; settings.retain=true; const auto interpolated=mixer.play(clip,settings); mixer.filter(interpolated,"Lagrangian Interpolator"); mixer.filter_parameter(interpolated,"LaGrange Filter Order",0); std::array<float,8> nearest{}; mixer.render(nearest); require(nearest[2]==clip->samples()[1],"Lagrangian nearest setting did not change interpolation"); mixer.stop(interpolated);
    // Filter allocations share a mixer budget even across independent voices.
    nolf::AudioMixer bounded({192000,2}); bool exhausted=false; unsigned count=0;
    for (;count<128;++count) { const auto id=bounded.play(clip,settings); bounded.filter(id,"Stereo Delay Filter"); try { bounded.filter_parameter(id,"Stereo Delay TimeL",2000); } catch (const std::length_error&) { exhausted=true; break; } }
    require(exhausted && count>0 && count<128,"live filters did not enforce shared memory budget");
}
std::string trim(std::string value) { const auto begin=value.find_first_not_of(" \t\r"); if (begin==value.npos) return {}; const auto end=value.find_last_not_of(" \t\r"); return value.substr(begin,end-begin+1); }
std::string quoted(std::string value) { value=trim(value); const auto a=value.find('"'),b=value.find('"',a==value.npos ? 0 : a+1); return a!=value.npos && b!=value.npos ? value.substr(a+1,b-a-1) : value; }
void corpus(const char* data) {
    nolf::Assets assets; assets.mount_game(data); const auto bytes=assets.read("attributes/soundfilters.txt"); std::istringstream stream(std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size()));
    std::string line,name; std::array<std::string,10> parameters{}; std::array<std::optional<float>,10> values{}; bool comment=false; unsigned count=0,pairs=0;
    const auto flush=[&] {
        if (name.empty()) return;
        nolf::AudioFilter filter(name,22050); for (unsigned i=0;i<10;++i) if (!parameters[i].empty()) { require(values[i].has_value(),"authored filter variable has no value"); if (!filter.parameter(parameters[i],*values[i])) throw std::runtime_error("authored filter parameter unsupported: "+name+" / "+parameters[i]); ++pairs; }
        double energy=0; for (unsigned i=0;i<22050;++i) { float l=i<1000 ? static_cast<float>(std::sin(i*.71)*.25) : 0,r=l; filter.process(l,r); require(std::isfinite(l) && std::isfinite(r),"authored DSP produced nonfinite audio"); energy+=double(l)*l+double(r)*r; } require(energy>0,"authored filter produced no samples"); ++count;
    };
    while (std::getline(stream,line)) {
        line=trim(line); if (line.starts_with("/*")) comment=true;
        if (comment) { if (line.find("*/")!=line.npos) comment=false; continue; }
        if (line.starts_with('[')) { flush(); name.clear(); parameters={}; values={}; continue; }
        const auto equals=line.find('='); if (equals==line.npos) continue; const auto key=trim(line.substr(0,equals)),value=line.substr(equals+1);
        if (key=="FilterName") name=quoted(value);
        else for (unsigned i=0;i<10;++i) { if (key=="Var"+std::to_string(i)) parameters[i]=quoted(value); if (key=="Value"+std::to_string(i)) values[i]=std::stof(value); }
    }
    flush(); require(count==30,"original filter corpus incomplete"); std::cout<<count<<" original filter definitions / "<<pairs<<" authored parameters rendered natively\n";
}
}
int main(int argc,char** argv) { try { checks(); if (argc>1) corpus(argv[1]); std::cout<<"Native filter frequency response, exact stereo delay, all DSP ranges, room tails, interpolation and shared budgets passed\n"; return 0; } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; } }
