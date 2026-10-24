#include "engine/client/client_sounds.h"
#include "audio_clip_fixture.h"
#include <algorithm>
#include <cstring>
#include <future>
#include <iostream>
#include <limits>
#include <chrono>
#include <thread>

namespace {
void require(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
class Output final : public nolf::platform::AudioOutput {
public:
    nolf::platform::AudioFormat spec;
    std::vector<float> samples;
    bool paused=false;
    explicit Output(nolf::platform::AudioFormat format) : spec(format) {}
    nolf::platform::AudioFormat format() const override { return spec; }
    void write(std::span<const float> data) override { samples.insert(samples.end(),data.begin(),data.end()); }
    std::size_t queued_frames() const override { return samples.size()/2; }
    void pause(bool state) override { paused=state; }
    void clear() override { samples.clear(); }
};
PlaySoundInfo request(uint32 flags=PLAYSOUND_GETHANDLE) {
    PlaySoundInfo info{}; info.m_vPosition.Init(0,0,0); std::strcpy(info.m_szSoundName,"a.wav"); info.m_dwFlags=flags; return info;
}
void checks() {
    audio_clip_fixture::Files files; auto assets=std::make_shared<nolf::Assets>(); assets->mount_directory(files.root);
    auto clips=std::make_shared<nolf::AudioClips>(assets,nolf::platform::make_ffmpeg_audio);
    std::shared_ptr<Output> output; bool fail=false;
    auto factory=[&](nolf::platform::AudioFormat format) -> std::shared_ptr<nolf::platform::AudioOutput> {
        if (fail) throw std::runtime_error("injected device-open failure");
        output=std::make_shared<Output>(format); return output;
    };
    auto sounds=std::make_unique<nolf::ClientSounds>(clips,factory); auto& api=nolf::ClientSounds::functions();
    InitSoundInfo init{}; init.Init(); init.m_nSampleRate=8000;
    require(api.InitSound(&init)==LT_NOTINITIALIZED,"sound thunk accepted missing callback scope");
    ILTClientSoundMgr& sdk=*sounds; require(sdk.InitSound(&init)==LT_OK && init.m_dwResults==INITSOUNDINFORESULTS_REVERB,"real SDK sound initialization failed");
    Sound3DProvider* providers=nullptr; require(sdk.GetSound3DProviderLists(providers,LTTRUE)==LT_OK && providers && !providers->m_pNextProvider,"native provider enumeration failed");
    require(sdk.ReleaseSound3DProviderList(providers)==LT_OK && sdk.ReleaseSound3DProviderList(providers)==LT_INVALIDPARAMS,"provider ownership/double release failed");
    require(sdk.ReleaseSound3DProviderList(reinterpret_cast<Sound3DProvider*>(1))==LT_INVALIDPARAMS,"foreign provider pointer accepted");
    auto info=request(); HLTSOUND sound=nullptr;
    {
        auto scope=sounds->activate(); require(api.PlaySound(&info)==LT_OK && info.m_hSound,"typed SDK sound play failed"); sound=info.m_hSound;
        float duration=0; require(sdk.GetSoundDuration(sound,duration)==LT_OK && std::abs(duration-.064f)<1e-6f,"SDK sound duration differs");
        int16* pcm=nullptr; int8* eight=reinterpret_cast<int8*>(1); uint32 rate=0,channels=0;
        require(api.GetSoundData(sound,pcm,eight,&rate,&channels)==LT_OK && pcm && !eight && rate==8000 && channels==1,"lip-sync PCM retrieval failed");
        for (unsigned i=0;i<512;++i) require(pcm[i]==int(i*32),"lip-sync PCM samples differ");
        uint32 offset=99,size=99; require(api.GetSoundOffset(sound,&offset,&size)==LT_OK && offset==0 && size==512,"lip-sync offset/size must be sample elements");
        sounds->pump(); require(output->samples.size()==800 && output->samples[2]==1/1024.f,"SDK pump did not mix source samples");
        require(api.GetSoundOffset(sound,&offset,&size)==LT_OK && offset==400 && size==512,"SDK sound sample clock differs");
        int16* again=nullptr; require(api.GetSoundData(sound,again,eight,&rate,&channels)==LT_OK && again==pcm,"borrowed lip-sync storage moved");
        require(sdk.PauseSounds()==LT_OK && output->paused,"SDK sound pause failed"); output->clear(); sounds->pump(); require(output->samples.empty(),"paused SDK sound advanced");
        require(sdk.ResumeSounds()==LT_OK && !output->paused,"SDK sound resume failed"); sounds->pump();
        LTBOOL done=LTFALSE; require(sdk.IsSoundDone(sound,done)==LT_OK && done && api.GetSoundOffset(sound,&offset,&size)==LT_FINISHED && offset==size,"finished retained sound was freed or timed incorrectly");
        require(sdk.ReleaseSoundHandle(sound)==LT_OK && sdk.KillSound(sound)==LT_INVALIDPARAMS && sounds->voices()==0,"released finished sound remained live");
        info=request(PLAYSOUND_LOOP|PLAYSOUND_GETHANDLE); require(sdk.PlaySound(&info,sound)==LT_OK && sound,"looped SDK play failed");
        output->clear(); sounds->pump(); const auto retained=sound; int16* stored=nullptr; api.GetSoundData(sound,stored,eight,&rate,&channels);
        init.m_dwFlags=INITSOUNDINFOFLAG_RELOADSOUNDS; init.m_nSampleRate=16000;
        require(sdk.InitSound(&init)==LT_OK && sdk.GetSoundDuration(retained,duration)==LT_OK,"device reload lost sound identity");
        require(api.GetSoundData(retained,again,eight,&rate,&channels)==LT_OK && stored==again,"device reload invalidated borrowed PCM");
        fail=true; require(sdk.InitSound(&init)==LT_ERROR && sdk.GetSoundDuration(retained,duration)==LT_OK,"failed device change destroyed active sound"); fail=false;
        require(sdk.KillSoundLoop(retained)==LT_OK,"loop stop failed"); output->clear(); sounds->pump(); require(sdk.IsSoundDone(retained,done)==LT_OK && done,"loop stop did not play to end");
        require(sdk.KillSound(retained)==LT_OK,"retained sound kill failed");
        info=request(0); require(sdk.PlaySound(&info,sound)==LT_OK && !sound && !info.m_hSound,"one-shot sound exposed an unrequested handle"); output->clear(); sounds->pump(); output->clear(); sounds->pump(); require(sounds->voices()==0,"SDK one-shot sound was retained");
        auto missing=request(); std::strcpy(missing.m_szSoundName,"absent.wav"); require(sdk.PlaySound(&missing,sound)==LT_MISSINGFILE,"missing sound did not report missing file");
        auto invalid=request(PLAYSOUND_CTRL_PITCH); invalid.m_fPitchShift=std::numeric_limits<float>::quiet_NaN(); require(sdk.PlaySound(&invalid,sound)==LT_INVALIDPARAMS && !sound,"nonfinite sound pitch accepted");
        require(sdk.SetVolume(101)==LT_INVALIDPARAMS && sdk.SetVolume(25)==LT_OK && api.GetSoundVolume()==25,"SDK volume range differs");
        info=request(); require(sdk.PlaySound(&info,sound)==LT_OK,"filter fixture play failed");
        require(sdk.SetSoundFilter(sound,"LowPass Filter")==LT_OK && sdk.SetSoundFilterParam(sound,"LowPass Cutoff",500)==LT_OK,"authored underwater filter rejected");
        float cutoff=0; require(sdk.GetSoundFilterParam(sound,"LowPass Cutoff",&cutoff)==LT_OK && cutoff==500,"filter parameter retrieval differs");
        require(sdk.SetSoundFilterParam(sound,"LowPass Cutoff",std::numeric_limits<float>::quiet_NaN())==LT_INVALIDPARAMS,"nonfinite filter parameter accepted");
        require(sdk.SetSoundFilter(sound,"Unknown Filter")==LT_NOTFOUND && sdk.GetSoundFilterParam(sound,"LowPass Cutoff",&cutoff)==LT_OK && cutoff==500,"unknown filter changed live DSP");
        const char* name=nullptr; unsigned count=0; while (sdk.GetFilterName(count,&name)==LT_OK) { require(name && *name,"empty filter name"); ++count; } require(count==18,"native filter catalog incomplete");
        ReverbProperties room{REVERBPARAM_VOLUME|REVERBPARAM_DECAYTIME,.5f,0,0,2,0}; require(sdk.SetReverbProperties(&room)==LT_OK,"room reverb setup failed");
        room={}; require(sdk.GetReverbProperties(&room)==LT_OK && room.m_fVolume==.5f && room.m_fDecayTime==2 && room.m_fDamping==1,"partial reverb update overwrote unspecified fields");
        room.m_fVolume=2; require(sdk.SetReverbProperties(&room)==LT_INVALIDPARAMS,"invalid room reverb accepted"); sdk.KillSound(sound);
    }
    require(std::async(std::launch::async,[&] { return sounds->SetVolume(1); }).get()==LT_ERROR,"sound virtual crossed owner thread");
    // Original callbacks see a valid handle; a callback can kill its own sound.
    bool called=false; nolf::ClientSoundEvents callbacks;
    callbacks.played=[&](PlaySoundInfo& playing,HLTSOUND handle) { called=true; float duration=0; require(playing.m_hSound==handle && api.GetSoundDuration(handle,&duration)==LT_OK,"OnPlaySound received no live sound"); api.KillSound(handle); };
    sounds->events(std::move(callbacks));
    { auto scope=sounds->activate(); info=request(); require(api.PlaySound(&info)==LT_OK && called && !info.m_hSound && sounds->voices()==0,"reentrant callback kill left stale result"); }
    // Attached-object lookup owns its return value and can invalidate an object.
    callbacks={}; bool exists=true; auto object=reinterpret_cast<HOBJECT>(17);
    callbacks.object=[&](HOBJECT handle) -> std::optional<nolf::SoundPose> { if (!exists || handle!=object) return {}; nolf::SoundPose pose; pose.position={10,0,0}; return pose; };
    sounds->events(std::move(callbacks));
    info=request(PLAYSOUND_3D|PLAYSOUND_ATTACHED|PLAYSOUND_GETHANDLE); info.m_hObject=object; info.m_fOuterRadius=100; info.m_fInnerRadius=5;
    require(sounds->PlaySound(&info,sound)==LT_OK && sound,"attached sound play failed"); LTVector position(0,0,0); require(sounds->GetSoundPosition(sound,&position)==LT_OK && position.x==10,"attached sound did not follow object");
    exists=false; sounds->update(.016f); require(sounds->KillSound(sound)==LT_INVALIDPARAMS,"removed attachment left a live sound");
    // A nested foreign scope rejects another service's handles.
    info=request(); require(sounds->PlaySound(&info,sound)==LT_OK,"sound fixture play failed"); nolf::ClientSounds foreign(clips,factory);
    { auto scope=foreign.activate(); float duration=0; require(api.GetSoundDuration(sound,&duration)==LT_INVALIDPARAMS,"foreign sound scope accepted an external handle"); }
    // Scope and per-call leases survive retirement from original game code.
    callbacks={}; callbacks.played=[&](PlaySoundInfo&,HLTSOUND) { sounds.reset(); }; sounds->events(std::move(callbacks));
    { auto scope=sounds->activate(); info=request(); require(api.PlaySound(&info)==LT_OK && !sounds && info.m_hSound,"callback retirement destroyed active sound state"); api.KillSound(info.m_hSound); }
    require(api.GetSoundDuration(sound,&init.m_fDistanceFactor)==LT_NOTINITIALIZED,"retired scope remained active");
}
void native_output() {
    audio_clip_fixture::Files files; auto assets=std::make_shared<nolf::Assets>(); assets->mount_directory(files.root);
    auto clips=std::make_shared<nolf::AudioClips>(assets,nolf::platform::make_ffmpeg_audio);
    nolf::ClientSounds sounds(clips,nolf::platform::make_sdl_audio); auto scope=sounds.activate();
    InitSoundInfo init{}; init.Init(); init.m_nSampleRate=44100; require(sounds.InitSound(&init)==LT_OK,"SDK could not open real SDL audio adapter");
    auto info=request(PLAYSOUND_GETHANDLE|PLAYSOUND_LOOP|PLAYSOUND_3D|PLAYSOUND_REVERB); info.m_vPosition={10,0,0}; info.m_fOuterRadius=100; info.m_fInnerRadius=20;
    HLTSOUND sound=nullptr; require(sounds.PlaySound(&info,sound)==LT_OK && sound,"SDL-backed SDK sound could not start");
    require(sounds.SetSoundFilter(sound,"LowPass Filter")==LT_OK && sounds.SetSoundFilterParam(sound,"LowPass Cutoff",500)==LT_OK,"SDL-backed authored filter failed");
    for (unsigned i=0;i<10;++i) { sounds.update(.01f); sounds.pump(); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    uint32 offset=0,size=0; require(nolf::ClientSounds::functions().GetSoundOffset(sound,&offset,&size)==LT_OK && offset>0 && offset<size,"SDL-backed looping sound did not advance");
    require(sounds.PauseSounds()==LT_OK && sounds.ResumeSounds()==LT_OK && sounds.KillSound(sound)==LT_OK,"SDL-backed playback control failed");
}
}
int main() { try { checks(); native_output(); std::cout<<"Native SDK sound virtuals/thunks, PCM lip sync, device reload, SDL playback, filters, loops/pause, attachment and callback ownership passed\n"; return 0; } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; } }
