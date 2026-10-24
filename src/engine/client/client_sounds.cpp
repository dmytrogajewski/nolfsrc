#include "engine/client/client_sounds.h"
#include "engine/server/sound_request.h"
#include "engine/portable/string.h"
#include <cstdlib>
#include "platform/logging.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <thread>

namespace nolf {
namespace {
class Inactive : public std::logic_error { public: using std::logic_error::logic_error; };
template<class F> LTRESULT operation(F&& function) noexcept {
    try { return function(); } catch (const Inactive&) { return LT_NOTINITIALIZED; }
    catch (const std::bad_alloc&) { return LT_OUTOFMEMORY; }
    catch (const std::invalid_argument&) { return LT_INVALIDPARAMS; }
    catch (...) { return LT_ERROR; }
}
constexpr char provider_name[]="Native software spatial audio";
constexpr std::size_t pcm_budget=64*1024*1024;
AudioVoice identity(HLTSOUND handle) { return reinterpret_cast<std::uintptr_t>(handle); }
HLTSOUND handle(AudioVoice voice) {
    if (!voice || voice>std::numeric_limits<std::uintptr_t>::max()) throw std::overflow_error("SDK sound identities exhausted");
    return reinterpret_cast<HLTSOUND>(static_cast<std::uintptr_t>(voice));
}
SoundPose checked(SoundPose pose) {
    for (const auto v : {pose.position.x,pose.position.y,pose.position.z})
        if (!std::isfinite(v) || std::abs(v)>1e12f) throw std::invalid_argument("invalid sound position");
    double norm=0; for (const auto v : pose.rotation.m_Quat) { if (!std::isfinite(v)) throw std::invalid_argument("invalid listener rotation"); norm+=double(v)*v; }
    if (norm<1e-12) throw std::invalid_argument("empty listener rotation");
    for (auto& v : pose.rotation.m_Quat) v=static_cast<float>(v/std::sqrt(norm));
    return pose;
}
}
struct ClientSounds::State {
    static thread_local std::shared_ptr<State> current;
    struct Record { HOBJECT attached=nullptr; std::vector<int16> pcm; };
    ClientSoundEvents events;
    std::shared_ptr<AudioClips> clips;
    platform::AudioOutputFactory factory;
    std::shared_ptr<platform::AudioOutput> output;
    AudioMixer mixer;
    std::map<AudioVoice,Record> records;
    std::map<Sound3DProvider*,std::unique_ptr<Sound3DProvider>,std::less<Sound3DProvider*>> providers;
    std::vector<std::unique_ptr<Sound3DProvider>> retired_providers;
    std::size_t pcm_bytes=0;
    uint16 volume=100;
    ReverbProperties reverb{REVERBPARAM_ALL,0,REVERB_ACOUSTICS_GENERIC,0,1,1};
    bool paused=false,in_client=true,teleport=true;
    SoundPose pose;
    LTVector previous_position{0,0,0};
    bool first_frame=true;
    const std::thread::id owner=std::this_thread::get_id();
    State(std::shared_ptr<AudioClips> source,platform::AudioOutputFactory sink) : clips(std::move(source)),factory(std::move(sink)) {
        if (!clips || !factory) throw std::invalid_argument("native sound services are required");
    }
    void thread() const { if (owner!=std::this_thread::get_id()) throw std::logic_error("client sounds used from another thread"); }
    static std::shared_ptr<State> active() { const auto self=current; if (!self) throw Inactive("sound SDK call outside callback scope"); self->thread(); return self; }
    AudioVoiceStatus get(HLTSOUND sound) const {
        if (!records.contains(identity(sound))) throw std::invalid_argument("invalid native sound handle");
        const auto status=mixer.status(identity(sound)); if (!status) throw std::invalid_argument("retired native sound handle"); return *status;
    }
    void erase(AudioVoice id) {
        const auto it=records.find(id); if (it!=records.end()) { pcm_bytes-=it->second.pcm.capacity()*sizeof(int16); records.erase(it); } mixer.stop(id);
    }
    void collect() {
        for (auto it=records.begin();it!=records.end();) {
            if (!mixer.status(it->first)) { pcm_bytes-=it->second.pcm.capacity()*sizeof(int16); it=records.erase(it); }
            else ++it;
        }
    }
    LTRESULT GetSound3DProviderLists(Sound3DProvider*& result,LTBOOL) {
        result=nullptr; if (providers.size()>=32 || providers.size()+retired_providers.size()>=4096) return LT_ERROR;
        auto item=std::make_unique<Sound3DProvider>(); item->m_dwProviderID=SOUND3DPROVIDERID_UNKNOWN;
        std::memcpy(item->m_szProvider,provider_name,sizeof(provider_name)); item->m_dwCaps=SOUND3DPROVIDER_CAPS_REVERB; item->m_pNextProvider=nullptr;
        auto* ptr=item.get(); providers.emplace(ptr,std::move(item)); result=ptr; return LT_OK;
    }
    LTRESULT ReleaseSound3DProviderList(Sound3DProvider* list) {
        const auto it=providers.find(list); if (it==providers.end()) return LT_INVALIDPARAMS;
        // Keep the allocation as a tombstone so a stale list cannot free a
        // subsequently allocated list at the same address.
        retired_providers.push_back(std::move(it->second)); providers.erase(it); return LT_OK;
    }
    LTRESULT InitSound(InitSoundInfo* info) {
        if (!info) return LT_INVALIDPARAMS;
        info->m_dwResults=0;
        if (!std::memchr(info->m_sz3DProvider,0,sizeof(info->m_sz3DProvider)) ||
            (info->m_sz3DProvider[0] && ascii_compare(info->m_sz3DProvider,provider_name)!=0) ||
            info->m_nSampleRate<8000 || info->m_nSampleRate>48000 ||
            (info->m_nBitsPerSample!=8 && info->m_nBitsPerSample!=16) ||
            info->m_nNumSWVoices>128 || info->m_nNum3DVoices>128 || info->m_nVolume>100 ||
            !std::isfinite(info->m_fDistanceFactor) || info->m_fDistanceFactor<=0 || info->m_fDistanceFactor>1000) return LT_INVALIDPARAMS;
        const platform::AudioFormat format{info->m_nSampleRate,2};
        auto next=factory(format); if (!next || next->format()!=format) throw std::runtime_error("audio provider returned a different format");
        next->pause(paused);
        auto listener=mixer.listener(); listener.distance_factor=info->m_fDistanceFactor;
        mixer.configure(format,std::max(1u,unsigned(info->m_nNumSWVoices)+info->m_nNum3DVoices)); mixer.listener(listener);
        mixer.gain(info->m_nVolume/100.f); volume=info->m_nVolume;
        if (output) output->clear();
        output=std::move(next);
        if (!(info->m_dwFlags&INITSOUNDINFOFLAG_RELOADSOUNDS)) {
            for (const auto& [id,record] : records) mixer.stop(id);
            records.clear(); pcm_bytes=0;
        }
        info->m_dwResults=INITSOUNDINFORESULTS_REVERB;
        return LT_OK;
    }
    LTRESULT PlaySound(PlaySoundInfo* info,HLTSOUND& result) {
        result=nullptr; if (!info) return LT_INVALIDPARAMS; info->m_hSound=nullptr;
        if (!output) return LT_NOTINITIALIZED;
        auto request=*info; const auto valid=sound_NormalizeRequest(request); if (valid!=LT_OK || request.m_fPitchShift>16) return LT_INVALIDPARAMS;
        if (!clips) return LT_NOTINITIALIZED;
        if (!clips->contains(request.m_szSoundName)) return LT_MISSINGFILE;
        auto clip=clips->load(request.m_szSoundName);
        AudioVoiceSettings settings; settings.gain=request.m_nVolume/100.f; settings.pitch=request.m_fPitchShift;
        settings.priority=request.m_nPriority; settings.loop=(request.m_dwFlags&PLAYSOUND_LOOP)!=0;
        // A client-local sound heard from the client plays unpositioned
        // (clientsoundmgr.cpp 0x00481806).
        const bool local=(request.m_dwFlags&PLAYSOUND_CLIENTLOCAL) && in_client;
        settings.spatial=!local && (request.m_dwFlags&PLAYSOUND_3D)!=0; settings.ambient=!local && !settings.spatial && (request.m_dwFlags&PLAYSOUND_AMBIENT)!=0;
        settings.retain=(request.m_dwFlags&PLAYSOUND_GETHANDLE)!=0; settings.position=request.m_vPosition;
        settings.reverb=(request.m_dwFlags&PLAYSOUND_REVERB)!=0;
        settings.outer_radius=request.m_fOuterRadius; settings.inner_radius=std::min(request.m_fInnerRadius,settings.outer_radius);
        Record record;
        if ((request.m_dwFlags&PLAYSOUND_ATTACHED) && request.m_hObject) {
            const auto callbacks=events; const auto position=callbacks.object ? callbacks.object(request.m_hObject) : std::nullopt;
            if (!position) return LT_INVALIDPARAMS;
            settings.position=checked(*position).position; record.attached=request.m_hObject;
        }
        const auto voice=mixer.play(std::move(clip),settings); HLTSOUND sound=nullptr;
        try { sound=handle(voice); records.emplace(voice,std::move(record)); } catch (...) { mixer.stop(voice); throw; }
        // Original OnPlaySound configures the newly created sound's filters.
        // Give that callback a valid handle even for automatically retired sounds.
        request.m_hSound=sound;
        try { const auto callbacks=events; if (callbacks.played) callbacks.played(request,sound); }
        catch (...) { erase(voice); throw; }
        if (settings.retain && mixer.status(voice)) result=sound;
        if (trace()) platform::log("sound",std::string("play ")+request.m_szSoundName+" flags "+std::to_string(request.m_dwFlags)+(result ? " handle" : "")+(settings.loop ? " loop" : "")+
            " at "+std::to_string(settings.position.x)+","+std::to_string(settings.position.y)+","+std::to_string(settings.position.z)+" radius "+std::to_string(settings.outer_radius)+
            " listener "+std::to_string(pose.position.x)+","+std::to_string(pose.position.y)+","+std::to_string(pose.position.z));
        info->m_hSound=result; return LT_OK;
    }
    LTRESULT GetSoundDuration(HLTSOUND sound,LTFLOAT& duration) { duration=0; duration=static_cast<float>(get(sound).clip->duration()); return LT_OK; }
    LTRESULT IsSoundDone(HLTSOUND sound,LTBOOL& done) { done=LTTRUE; done=get(sound).done ? LTTRUE : LTFALSE; return LT_OK; }
    // NOLF_SOUND_TRACE logs every play and stop.
    static bool trace() { static const bool enabled=std::getenv("NOLF_SOUND_TRACE")!=nullptr; return enabled; }
    LTRESULT KillSound(HLTSOUND sound) {
        const auto status=get(sound);
        if (trace()) platform::log("sound",std::string("kill ")+(status.clip ? status.clip->name() : std::string("?")));
        erase(identity(sound)); return LT_OK;
    }
    LTRESULT KillSoundLoop(HLTSOUND sound) { auto status=get(sound); status.settings.loop=false; mixer.settings(identity(sound),status.settings); return LT_OK; }
    LTRESULT ReleaseSoundHandle(HLTSOUND sound) {
        const auto status=get(sound); if (!status.settings.retain) return LT_ERROR;
        mixer.release(identity(sound)); collect(); return LT_OK;
    }
    LTRESULT GetVolume(uint16& result) { result=volume; return LT_OK; }
    LTRESULT SetVolume(uint16 next) { if (next>100) return LT_INVALIDPARAMS; mixer.gain(next/100.f); volume=next; return LT_OK; }
    LTRESULT SetReverbProperties(ReverbProperties* properties) {
        if (!properties || (properties->m_dwParams&~uint32(REVERBPARAM_ALL))) return LT_INVALIDPARAMS;
        auto next=reverb; const auto mask=properties->m_dwParams;
        if (mask&REVERBPARAM_VOLUME) next.m_fVolume=properties->m_fVolume;
        if (mask&REVERBPARAM_ACOUSTICS) next.m_dwAcoustics=properties->m_dwAcoustics;
        if (mask&REVERBPARAM_REFLECTTIME) next.m_fReflectTime=properties->m_fReflectTime;
        if (mask&REVERBPARAM_DECAYTIME) next.m_fDecayTime=properties->m_fDecayTime;
        if (mask&REVERBPARAM_DAMPING) next.m_fDamping=properties->m_fDamping;
        if (next.m_fVolume!=reverb.m_fVolume || next.m_dwAcoustics!=reverb.m_dwAcoustics || next.m_fReflectTime!=reverb.m_fReflectTime || next.m_fDecayTime!=reverb.m_fDecayTime || next.m_fDamping!=reverb.m_fDamping)
            mixer.reverb(next.m_fVolume,next.m_fReflectTime,next.m_fDecayTime,next.m_fDamping,next.m_dwAcoustics);
        reverb=next; return LT_OK;
    }
    LTRESULT GetReverbProperties(ReverbProperties* properties) { if (!properties) return LT_INVALIDPARAMS; *properties=reverb; return LT_OK; }
    LTRESULT SetSoundOcclusion(HLTSOUND sound,LTFLOAT level) { auto status=get(sound); status.settings.occlusion=level; mixer.settings(identity(sound),status.settings); return LT_OK; }
    LTRESULT GetSoundOcclusion(HLTSOUND sound,LTFLOAT* level) { if (!level) return LT_INVALIDPARAMS; *level=0; *level=get(sound).settings.occlusion; return LT_OK; }
    LTRESULT SetSoundObstruction(HLTSOUND sound,LTFLOAT level) { auto status=get(sound); status.settings.obstruction=level; mixer.settings(identity(sound),status.settings); return LT_OK; }
    LTRESULT GetSoundObstruction(HLTSOUND sound,LTFLOAT* level) { if (!level) return LT_INVALIDPARAMS; *level=0; *level=get(sound).settings.obstruction; return LT_OK; }
    LTRESULT SetSoundFilter(HLTSOUND sound,const char* filter) {
        get(sound); if (!filter) return LT_INVALIDPARAMS;
        if (*filter && std::none_of(AudioFilter::catalog().begin(),AudioFilter::catalog().end(),[&](const auto& item) { return ascii_compare(item.name,filter)==0; })) return LT_NOTFOUND;
        mixer.filter(identity(sound),filter); return LT_OK;
    }
    LTRESULT SetSoundFilterParam(HLTSOUND sound,const char* param,float value) {
        get(sound); if (!param) return LT_INVALIDPARAMS;
        return mixer.filter_parameter(identity(sound),param,value) ? LT_OK : LT_NOTFOUND;
    }
    LTRESULT GetSoundFilterParam(HLTSOUND sound,const char* param,float* value) {
        if (!value || !param) return LT_INVALIDPARAMS;
        *value=0; get(sound); const auto result=mixer.filter_parameter(identity(sound),param); if (!result) return LT_NOTFOUND;
        *value=*result; return LT_OK;
    }
    LTRESULT GetFilterName(uint32 index,const char** filter) {
        if (!filter) return LT_INVALIDPARAMS;
        *filter=nullptr; const auto catalog=AudioFilter::catalog(); if (index>=catalog.size()) return LT_NOTFOUND;
        *filter=catalog[index].name.data(); return LT_OK;
    }
    LTRESULT GetFilterParamName(uint32 index,const char* filter,const char** param) {
        if (!param || !filter) return LT_INVALIDPARAMS;
        *param=nullptr; for (const auto& item : AudioFilter::catalog()) if (ascii_compare(item.name,filter)==0) {
            if (index>=item.parameters.size()) return LT_NOTFOUND;
            *param=item.parameters[index].name.data(); return LT_OK;
        }
        return LT_NOTFOUND;
    }
    LTRESULT SetSoundPosition(HLTSOUND sound,LTVector* position) {
        auto status=get(sound); if (position) { SoundPose next; next.position=*position; status.settings.position=checked(next).position; mixer.settings(identity(sound),status.settings); records.at(identity(sound)).attached=nullptr; } return LT_OK;
    }
    LTRESULT GetSoundPosition(HLTSOUND sound,LTVector* position) { const auto status=get(sound); if (position) *position=status.settings.position; return LT_OK; }
    LTRESULT PauseSounds() { if (output) output->pause(true); mixer.pause(true); paused=true; return LT_OK; }
    LTRESULT ResumeSounds() { if (output) output->pause(false); mixer.pause(false); paused=false; return LT_OK; }
    LTRESULT SetListener(LTBOOL client,LTVector* position,LTRotation* rotation,LTBOOL jump) {
        if (!client) { auto next=pose; if (position) next.position=*position; if (rotation) next.rotation=*rotation; pose=checked(next); }
        in_client=client!=0; teleport=jump!=0; apply_listener(0); return LT_OK;
    }
    void apply_listener(float elapsed) {
        auto listener=mixer.listener(); const auto next=checked(pose);
        listener.velocity=velocity(next.position,previous_position,elapsed,teleport || first_frame);
        listener.position=next.position; LTMatrix matrix; next.rotation.ConvertToMatrix(matrix);
        LTVector up,forward; matrix.GetBasisVectors(&listener.right,&up,&forward); mixer.listener(listener);
    }
    static LTVector velocity(LTVector next,LTVector previous,float elapsed,bool jump=false) {
        if (jump || elapsed<1e-6f) return {0,0,0};
        const auto component=[&](float a,float b) { return static_cast<float>(std::clamp((double(a)-b)/elapsed,-1e12,1e12)); };
        return {component(next.x,previous.x),component(next.y,previous.y),component(next.z,previous.z)};
    }
    LTRESULT GetSoundTimer(HLTSOUND sound,LTFLOAT* timer) {
        if (!timer) return LT_INVALIDPARAMS;
        *timer=0; const auto status=get(sound);
        *timer=static_cast<float>(status.frame/status.clip->wave().sample_rate); return status.done ? LT_FINISHED : LT_OK;
    }
    LTRESULT GetSoundOffset(HLTSOUND sound,uint32* offset,uint32* size) {
        if (!offset || !size) return LT_INVALIDPARAMS;
        *offset=*size=0; const auto status=get(sound);
        if (status.clip->samples().size()>std::numeric_limits<uint32>::max()) return LT_ERROR;
        // The original implementation returns sample elements for BOTH fields.
        // NodeController indexes int16 data with these values during lip sync.
        *size=static_cast<uint32>(status.clip->samples().size());
        *offset=static_cast<uint32>(std::min<std::size_t>(static_cast<std::size_t>(status.frame)*status.clip->wave().channels,*size));
        return status.done ? LT_FINISHED : LT_OK;
    }
    LTRESULT GetSoundData(HLTSOUND sound,int16*& sixteen,int8*& eight,uint32* rate,uint32* channels) {
        sixteen=nullptr; eight=nullptr; if (rate) *rate=0; if (channels) *channels=0;
        if (!rate || !channels) return LT_INVALIDPARAMS;
        const auto status=get(sound); auto& pcm=records.at(identity(sound)).pcm;
        if (pcm.empty()) {
            const auto samples=status.clip->samples(); if (samples.size()>pcm_budget/sizeof(int16) || pcm_bytes>pcm_budget-samples.size()*sizeof(int16)) return LT_OUTOFMEMORY;
            std::vector<int16> next(samples.size());
            if (next.capacity()*sizeof(int16)>pcm_budget-pcm_bytes) return LT_OUTOFMEMORY;
            for (std::size_t i=0;i<samples.size();++i) next[i]=static_cast<int16>(std::clamp(std::lround(std::clamp(double(samples[i]),-1.,1.)*32768.),-32768l,32767l));
            pcm_bytes+=next.capacity()*sizeof(int16); pcm=std::move(next);
        }
        sixteen=pcm.data(); *rate=status.clip->wave().sample_rate; *channels=status.clip->wave().channels; return LT_OK;
    }
    void update(float elapsed,std::optional<SoundPose> client) {
        if (!std::isfinite(elapsed) || elapsed<0 || elapsed>60) throw std::invalid_argument("invalid audio frame time");
        if (client && in_client) pose=checked(*client);
        apply_listener(elapsed); teleport=false; first_frame=false; previous_position=pose.position;
        std::vector<AudioVoice> attached; for (const auto& [id,record] : records) if (record.attached) attached.push_back(id);
        for (const auto id : attached) {
            const auto it=records.find(id); if (it==records.end()) continue; const auto object=it->second.attached;
            const auto callbacks=events; const auto next=callbacks.object ? callbacks.object(object) : std::nullopt;
            // A lookup is allowed to remove sounds or retire the facade.
            if (!records.contains(id)) continue;
            auto status=mixer.status(id); if (!status) continue;
            if (!next) { erase(id); continue; }
            const auto position=checked(*next).position;
            status->settings.velocity=velocity(position,status->settings.position,elapsed);
            status->settings.position=position; mixer.settings(id,status->settings);
        }
        collect();
    }
    void pump() {
        if (!output || paused) return;
        const auto format=output->format(); const auto queued=output->queued_frames(); const auto target=std::size_t(format.rate)/20;
        if (queued>=target) return;
        std::vector<float> samples((target-queued)*2); mixer.render(samples); output->write(samples); collect();
    }
};
thread_local std::shared_ptr<ClientSounds::State> ClientSounds::State::current;
ClientSounds::ClientSounds(std::shared_ptr<AudioClips> clips,platform::AudioOutputFactory output) : state_(std::make_shared<State>(std::move(clips),std::move(output))) { m_ClientServerType=ClientType; }
ClientSounds::~ClientSounds()=default;
ClientSounds::Scope::Scope(const ClientSounds& sounds) : previous_(State::current),current_(sounds.state_) { current_->thread(); State::current=current_; }
ClientSounds::Scope::~Scope() { State::current=std::move(previous_); }
void ClientSounds::events(ClientSoundEvents events) { const auto self=state_; self->thread(); auto previous=std::move(self->events); self->events=std::move(events); }
void ClientSounds::update(float elapsed,std::optional<SoundPose> client) { const auto self=state_; self->thread(); self->update(elapsed,client); }
void ClientSounds::pump() { const auto self=state_; self->thread(); self->pump(); }
std::size_t ClientSounds::voices() const { const auto self=state_; self->thread(); return self->mixer.voices(); }
void ClientSounds::install(ILTClient& client) {
    const auto& api=functions();
#define NOLF_SOUND_API(name) client.name=api.name;
#include "client_sound_members.inc"
#undef NOLF_SOUND_API
}
LTRESULT ClientSounds::PlaySound(PlaySoundInfo* info,HLTSOUND& result) { const auto self=state_; return operation([&]() -> LTRESULT { self->thread(); return self->PlaySound(info,result); }); }
LTRESULT ClientSounds::GetSoundDuration(HLTSOUND sound,LTFLOAT& duration) { const auto self=state_; return operation([&]() -> LTRESULT { self->thread(); return self->GetSoundDuration(sound,duration); }); }
LTRESULT ClientSounds::IsSoundDone(HLTSOUND sound,LTBOOL& done) { const auto self=state_; return operation([&]() -> LTRESULT { self->thread(); return self->IsSoundDone(sound,done); }); }
LTRESULT ClientSounds::KillSound(HLTSOUND sound) { const auto self=state_; return operation([&]() -> LTRESULT { self->thread(); return self->KillSound(sound); }); }
LTRESULT ClientSounds::KillSoundLoop(HLTSOUND sound) { const auto self=state_; return operation([&]() -> LTRESULT { self->thread(); return self->KillSoundLoop(sound); }); }
LTRESULT ClientSounds::GetSound3DProviderLists(Sound3DProvider*& list,LTBOOL verify) { const auto self=state_; return operation([&]() -> LTRESULT { self->thread(); return self->GetSound3DProviderLists(list,verify); }); }
LTRESULT ClientSounds::ReleaseSound3DProviderList(Sound3DProvider* list) { const auto self=state_; return operation([&]() -> LTRESULT { self->thread(); return self->ReleaseSound3DProviderList(list); }); }
LTRESULT ClientSounds::InitSound(InitSoundInfo* info) { const auto self=state_; return operation([&]() -> LTRESULT { self->thread(); return self->InitSound(info); }); }
LTRESULT ClientSounds::GetVolume(uint16& volume) { const auto self=state_; return operation([&]() -> LTRESULT { self->thread(); return self->GetVolume(volume); }); }
LTRESULT ClientSounds::SetVolume(uint16 volume) { const auto self=state_; return operation([&]() -> LTRESULT { self->thread(); return self->SetVolume(volume); }); }
LTRESULT ClientSounds::SetReverbProperties(ReverbProperties* properties) { const auto self=state_; return operation([&]() -> LTRESULT { self->thread(); return self->SetReverbProperties(properties); }); }
LTRESULT ClientSounds::GetReverbProperties(ReverbProperties* properties) { const auto self=state_; return operation([&]() -> LTRESULT { self->thread(); return self->GetReverbProperties(properties); }); }
LTRESULT ClientSounds::SetSoundOcclusion(HLTSOUND sound,LTFLOAT level) { const auto self=state_; return operation([&]() -> LTRESULT { self->thread(); return self->SetSoundOcclusion(sound,level); }); }
LTRESULT ClientSounds::GetSoundOcclusion(HLTSOUND sound,LTFLOAT* level) { const auto self=state_; return operation([&]() -> LTRESULT { self->thread(); return self->GetSoundOcclusion(sound,level); }); }
LTRESULT ClientSounds::SetSoundObstruction(HLTSOUND sound,LTFLOAT level) { const auto self=state_; return operation([&]() -> LTRESULT { self->thread(); return self->SetSoundObstruction(sound,level); }); }
LTRESULT ClientSounds::GetSoundObstruction(HLTSOUND sound,LTFLOAT* level) { const auto self=state_; return operation([&]() -> LTRESULT { self->thread(); return self->GetSoundObstruction(sound,level); }); }
LTRESULT ClientSounds::SetSoundFilter(HLTSOUND sound,const char* filter) { const auto self=state_; return operation([&]() -> LTRESULT { self->thread(); return self->SetSoundFilter(sound,filter); }); }
LTRESULT ClientSounds::SetSoundFilterParam(HLTSOUND sound,const char* param,float value) { const auto self=state_; return operation([&]() -> LTRESULT { self->thread(); return self->SetSoundFilterParam(sound,param,value); }); }
LTRESULT ClientSounds::GetSoundFilterParam(HLTSOUND sound,const char* param,float* value) { const auto self=state_; return operation([&]() -> LTRESULT { self->thread(); return self->GetSoundFilterParam(sound,param,value); }); }
LTRESULT ClientSounds::GetFilterName(uint32 index,const char** filter) { const auto self=state_; return operation([&]() -> LTRESULT { self->thread(); return self->GetFilterName(index,filter); }); }
LTRESULT ClientSounds::GetFilterParamName(uint32 index,const char* filter,const char** param) { const auto self=state_; return operation([&]() -> LTRESULT { self->thread(); return self->GetFilterParamName(index,filter,param); }); }
LTRESULT ClientSounds::SetSoundPosition(HLTSOUND sound,LTVector* position) { const auto self=state_; return operation([&]() -> LTRESULT { self->thread(); return self->SetSoundPosition(sound,position); }); }
LTRESULT ClientSounds::GetSoundPosition(HLTSOUND sound,LTVector* position) { const auto self=state_; return operation([&]() -> LTRESULT { self->thread(); return self->GetSoundPosition(sound,position); }); }
LTRESULT ClientSounds::PauseSounds() { const auto self=state_; return operation([&]() -> LTRESULT { self->thread(); return self->PauseSounds(); }); }
LTRESULT ClientSounds::ResumeSounds() { const auto self=state_; return operation([&]() -> LTRESULT { self->thread(); return self->ResumeSounds(); }); }
LTRESULT ClientSounds::SetListener(LTBOOL client,LTVector* position,LTRotation* rotation,LTBOOL teleport) { const auto self=state_; return operation([&]() -> LTRESULT { self->thread(); return self->SetListener(client,position,rotation,teleport); }); }
LTRESULT ClientSounds::ReleaseSoundHandle(HLTSOUND sound) { const auto self=state_; return operation([&]() -> LTRESULT { self->thread(); return self->ReleaseSoundHandle(sound); }); }
const SoundFunctions& ClientSounds::functions() {
    static const SoundFunctions api{
        .GetSound3DProviderLists=+[](Sound3DProvider*& list,LTBOOL verify) -> LTRESULT { return operation([&]() -> LTRESULT { return State::active()->GetSound3DProviderLists(list,verify); }); },
        .ReleaseSound3DProviderList=+[](Sound3DProvider* list) -> void { operation([&]() -> LTRESULT { return State::active()->ReleaseSound3DProviderList(list); }); },
        .InitSound=+[](InitSoundInfo* info) -> LTRESULT { return operation([&]() -> LTRESULT { return State::active()->InitSound(info); }); },
        .GetSoundVolume=+[]() -> uint16 { uint16 result=0; operation([&]() -> LTRESULT { result=[&]() -> uint16 { uint16 volume=0; State::active()->GetVolume(volume); return volume; }(); return LT_OK; }); return result; },
        .SetSoundVolume=+[](uint16 volume) -> void { operation([&]() -> LTRESULT { return State::active()->SetVolume(volume); }); },
        .SetReverbProperties=+[](ReverbProperties* properties) -> LTRESULT { return operation([&]() -> LTRESULT { return State::active()->SetReverbProperties(properties); }); },
        .GetReverbProperties=+[](ReverbProperties* properties) -> LTRESULT { return operation([&]() -> LTRESULT { return State::active()->GetReverbProperties(properties); }); },
        .PlaySound=+[](PlaySoundInfo* info) -> LTRESULT { return operation([&]() -> LTRESULT { HLTSOUND result=nullptr; return State::active()->PlaySound(info,result); }); },
        .SetSoundPosition=+[](HLTSOUND sound,LTVector* position) -> LTRESULT { return operation([&]() -> LTRESULT { return State::active()->SetSoundPosition(sound,position); }); },
        .GetSoundPosition=+[](HLTSOUND sound,LTVector* position) -> LTRESULT { return operation([&]() -> LTRESULT { return State::active()->GetSoundPosition(sound,position); }); },
        .PauseSounds=+[]() -> void { operation([&]() -> LTRESULT { return State::active()->PauseSounds(); }); },
        .ResumeSounds=+[]() -> void { operation([&]() -> LTRESULT { return State::active()->ResumeSounds(); }); },
        .GetSoundDuration=+[](HLTSOUND sound,LTFLOAT* duration) -> LTRESULT { return operation([&]() -> LTRESULT { if (!duration) return LT_INVALIDPARAMS; *duration=0; return State::active()->GetSoundDuration(sound,*duration); }); },
        .GetSoundTimer=+[](HLTSOUND sound,LTFLOAT* timer) -> LTRESULT { return operation([&]() -> LTRESULT { return State::active()->GetSoundTimer(sound,timer); }); },
        .GetSoundData=+[](HLTSOUND sound,int16*& sixteen,int8*& eight,uint32* rate,uint32* channels) -> LTRESULT { return operation([&]() -> LTRESULT { return State::active()->GetSoundData(sound,sixteen,eight,rate,channels); }); },
        .GetSoundOffset=+[](HLTSOUND sound,uint32* offset,uint32* size) -> LTRESULT { return operation([&]() -> LTRESULT { return State::active()->GetSoundOffset(sound,offset,size); }); },
        .IsDone=+[](HLTSOUND sound) -> LTBOOL { LTBOOL result=LTTRUE; operation([&]() -> LTRESULT { result=[&]() -> LTBOOL { LTBOOL done=LTTRUE; State::active()->IsSoundDone(sound,done); return done; }(); return LT_OK; }); return result; },
        .KillSound=+[](HLTSOUND sound) -> void { operation([&]() -> LTRESULT { return State::active()->KillSound(sound); }); },
        .KillSoundLoop=+[](HLTSOUND sound) -> void { operation([&]() -> LTRESULT { return State::active()->KillSoundLoop(sound); }); },
        .SetListener=+[](LTBOOL client,LTVector* position,LTRotation* rotation) -> void { operation([&]() -> LTRESULT { return State::active()->SetListener(client,position,rotation,LTTRUE); }); },
        .GetListener=+[](LTBOOL* client,LTVector* position,LTRotation* rotation) -> void { operation([&]() -> LTRESULT { const auto self=State::active(); if (client) *client=self->in_client ? LTTRUE : LTFALSE; if (position) *position=self->pose.position; if (rotation) *rotation=self->pose.rotation; return LT_OK; }); },
    }; return api;
}
}
