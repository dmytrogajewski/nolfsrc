#include "engine/client/audio_mixer.h"
#include "audio_clip_fixture.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <future>
#include <iostream>
#include <limits>

namespace {
void require(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
template<class F> void rejects(F&& fn) { bool rejected=false; try { fn(); } catch (const std::exception&) { rejected=true; } require(rejected,"invalid sound mixing operation accepted"); }
void checks() {
    audio_clip_fixture::Files files; auto assets=std::make_shared<nolf::Assets>(); assets->mount_directory(files.root); nolf::AudioClips cache(assets,nolf::platform::make_ffmpeg_audio); const auto clip=cache.load("a.wav");
    nolf::AudioMixer mixer({8000,2}); nolf::AudioVoiceSettings settings; settings.retain=true; auto voice=mixer.play(clip,settings); std::array<float,8> output{}; mixer.render(output);
    for (unsigned i=0;i<4;++i) require(output[i*2]==clip->samples()[i] && output[i*2+1]==clip->samples()[i],"local mono voice output differs"); require(mixer.status(voice)->frame==4,"native sample clock differs");
    settings.pitch=2; require(mixer.settings(voice,settings),"pitch update failed"); mixer.render(output); for (unsigned i=0;i<4;++i) require(output[i*2]==clip->samples()[4+i*2],"pitch interpolation step differs"); require(mixer.status(voice)->frame==12,"pitch clock differs");
    mixer.pause(true); mixer.render(output); require(std::all_of(output.begin(),output.end(),[](float value) { return value==0; }) && mixer.status(voice)->frame==12,"pause advanced sound clock or leaked samples"); mixer.pause(false); mixer.gain(.5f); mixer.render(output); require(output[0]==clip->samples()[12]*.5f,"master volume failed"); mixer.gain(1); require(mixer.stop(voice) && !mixer.status(voice),"voice stop did not release record");
    settings={}; settings.loop=settings.retain=true; voice=mixer.play(clip,settings); std::array<float,1056> looped{}; mixer.render(looped); require(!mixer.status(voice)->done && mixer.status(voice)->frame==16 && looped[1026]==clip->samples()[1],"whole sound looping lost sample alignment");
    settings.loop=false; mixer.settings(voice,settings); std::array<float,1024> ended{}; mixer.render(ended); require(mixer.status(voice)->done && mixer.status(voice)->frame==512 && ended.back()==0,"loop release did not play out remaining sound"); require(mixer.release(voice) && !mixer.status(voice),"finished retained voice did not retire");
    const auto once=mixer.play(clip); mixer.render(ended); require(!mixer.status(once) && mixer.voices()==0,"one-shot voice not automatically released"); voice=mixer.play(clip); require(voice!=once && !mixer.stop(once),"voice identity reused"); mixer.stop(voice);
    nolf::AudioMixer priorities({8000,2},1); settings={}; settings.priority=1; settings.gain=.5f; const auto low=priorities.play(clip,settings); settings.priority=255; settings.gain=1; const auto high=priorities.play(clip,settings); priorities.render(output);
    require(output[2]==clip->samples()[1] && priorities.status(low)->frame==4,"priority voice selection/virtual clock failed"); priorities.stop(high); priorities.render(output); require(output[0]==clip->samples()[4]*.5f,"virtual sound did not resume at advanced clock"); priorities.stop(low);
    settings={}; settings.spatial=true; settings.position={100,0,0}; settings.outer_radius=1; settings.priority=255; const auto far=priorities.play(clip,settings); settings={}; settings.priority=1; const auto near=priorities.play(clip,settings); priorities.render(output); require(output[2]==clip->samples()[1] && priorities.status(far)->frame==4,"inaudible priority voice displaced an audible sound"); priorities.stop(far); priorities.stop(near);
    settings={}; settings.spatial=true; settings.position={1,0,0}; settings.inner_radius=2; settings.outer_radius=10; voice=mixer.play(clip,settings); mixer.render(output); require(output[2]==0 && output[3]==clip->samples()[1],"right source did not pan right"); mixer.stop(voice);
    auto listener=mixer.listener(); listener.right={-1,0,0}; mixer.listener(listener); voice=mixer.play(clip,settings); mixer.render(output); require(output[2]==clip->samples()[1] && output[3]==0,"listener orientation did not rotate sound image"); mixer.stop(voice); listener.right={1,0,0}; mixer.listener(listener);
    settings.spatial=false; settings.ambient=true; settings.inner_radius=0; settings.outer_radius=2; voice=mixer.play(clip,settings); mixer.render(output); require(output[2]==clip->samples()[1]*.5f && output[3]==output[2],"ambient distance rolloff inherited orientation"); mixer.stop(voice);
    settings.ambient=false; settings.spatial=true; settings.inner_radius=2; settings.outer_radius=10; settings.velocity={10000,0,0}; voice=mixer.play(clip,settings); mixer.render(output); require(mixer.status(voice)->frame<4 && mixer.status(voice)->frame>0,"receding source did not lower doppler pitch"); mixer.stop(voice);
    settings={}; settings.occlusion=1; settings.obstruction=.5f; voice=mixer.play(clip,settings); mixer.render(output); require(output[2]>0 && output[2]<clip->samples()[1]*.3f && output[2]==output[3],"occlusion lowpass/obstruction attenuation failed");
    auto invalid=settings; invalid.pitch=std::numeric_limits<float>::quiet_NaN(); rejects([&] { mixer.settings(voice,invalid); }); require(mixer.status(voice)->settings.pitch==1,"invalid sound update changed live voice"); invalid.position.x=std::numeric_limits<float>::max(); rejects([&] { mixer.play(clip,invalid); }); listener.right={0,0,0}; rejects([&] { mixer.listener(listener); }); rejects([&] { mixer.render(std::span(output).first(3)); }); rejects([&] { mixer.gain(2); });
    require(std::async(std::launch::async,[&] { try { mixer.voices(); } catch (const std::logic_error&) { return true; } return false; }).get(),"audio mixer crossed owner thread");
}
}
int main() { try { checks(); std::cout<<"Native sound mixing, pitch/clocks, looping, volume/pause, priority virtualization, pan/distance/doppler and occlusion passed\n"; return 0; } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
