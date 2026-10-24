#pragma once
#include "engine/client/audio_mixer.h"
#include "iltclient.h"
#include "iltsoundmgr.h"
#include <functional>

namespace nolf {
struct SoundFunctions {
#define NOLF_SOUND_API(name) decltype(ILTClient::name) name=nullptr;
#include "client_sound_members.inc"
#undef NOLF_SOUND_API
};
struct SoundPose { LTVector position{0,0,0}; LTRotation rotation{0,0,0,1}; };
struct ClientSoundEvents {
    // Retained before closures, including while a callback retires its host.
    std::shared_ptr<const void> code;
    std::function<std::optional<SoundPose>(HOBJECT)> object;
    std::function<void(PlaySoundInfo&,HLTSOUND)> played;
};
class ClientSounds final : public ILTClientSoundMgr {
    struct State;
    std::shared_ptr<State> state_;
public:
    ClientSounds(std::shared_ptr<AudioClips>,platform::AudioOutputFactory);
    ~ClientSounds();
    ClientSounds(const ClientSounds&)=delete;
    ClientSounds& operator=(const ClientSounds&)=delete;
    class Scope {
        std::shared_ptr<State> previous_,current_;
    public:
        explicit Scope(const ClientSounds&);
        ~Scope();
        Scope(const Scope&)=delete;
        Scope& operator=(const Scope&)=delete;
    };
    Scope activate() const { return Scope(*this); }
    static const SoundFunctions& functions();
    static void install(ILTClient&);
    void events(ClientSoundEvents);
    // The host supplies the client pose and elapsed time once per frame.
    void update(float elapsed,std::optional<SoundPose> client = {});
    void pump();
    std::size_t voices() const;
    LTRESULT PlaySound(PlaySoundInfo*,HLTSOUND&) override;
    LTRESULT GetSoundDuration(HLTSOUND,LTFLOAT&) override;
    LTRESULT IsSoundDone(HLTSOUND,LTBOOL&) override;
    LTRESULT KillSound(HLTSOUND) override;
    LTRESULT KillSoundLoop(HLTSOUND) override;
    LTRESULT GetSound3DProviderLists(Sound3DProvider*&,LTBOOL) override;
    LTRESULT ReleaseSound3DProviderList(Sound3DProvider*) override;
    LTRESULT InitSound(InitSoundInfo*) override;
    LTRESULT GetVolume(uint16&) override;
    LTRESULT SetVolume(uint16) override;
    LTRESULT SetReverbProperties(ReverbProperties*) override;
    LTRESULT GetReverbProperties(ReverbProperties*) override;
    LTRESULT SetSoundOcclusion(HLTSOUND,LTFLOAT) override;
    LTRESULT GetSoundOcclusion(HLTSOUND,LTFLOAT*) override;
    LTRESULT SetSoundObstruction(HLTSOUND,LTFLOAT) override;
    LTRESULT GetSoundObstruction(HLTSOUND,LTFLOAT*) override;
    LTRESULT SetSoundFilter(HLTSOUND,const char*) override;
    LTRESULT SetSoundFilterParam(HLTSOUND,const char*,float) override;
    LTRESULT GetSoundFilterParam(HLTSOUND,const char*,float*) override;
    LTRESULT GetFilterName(uint32,const char**) override;
    LTRESULT GetFilterParamName(uint32,const char*,const char**) override;
    LTRESULT SetSoundPosition(HLTSOUND,LTVector*) override;
    LTRESULT GetSoundPosition(HLTSOUND,LTVector*) override;
    LTRESULT PauseSounds() override;
    LTRESULT ResumeSounds() override;
    LTRESULT SetListener(LTBOOL,LTVector*,LTRotation*,LTBOOL) override;
    LTRESULT ReleaseSoundHandle(HLTSOUND) override;
};
}
