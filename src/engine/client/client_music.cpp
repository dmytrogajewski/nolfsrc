#include "engine/client/client_music.h"
#include "engine/portable/control_file.h"
#include "engine/portable/string.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <thread>

namespace nolf {
namespace {
constexpr unsigned music_rate=48000;
// Synthesis stays this far ahead of the device.
constexpr double device_lead=.1;
// The next queued segment is scheduled once the playing one is this close to
// its end, standing in for DirectMusic's almost-end notification.
constexpr double almost_end=.25;
constexpr std::size_t render_frames=480;
// The synthesizer peaks near 3.3x full scale on the loudest original
// intensities at unity; this keeps them just inside it.
constexpr float synth_headroom=.3f;
// A queue whose loops never reach a segment would otherwise spin forever.
constexpr unsigned queue_steps=4096;
std::string lower(std::string_view text) { std::string result(text); for (auto& c : result) c=char(ascii_lower(static_cast<unsigned char>(c))); return result; }
platform::MusicTiming timing(LTDMEnactTypes enact) {
    switch (enact) {
    case LTDMEnactImmediatly: return platform::MusicTiming::Instant;
    case LTDMEnactNextBeat: return platform::MusicTiming::Beat;
    case LTDMEnactNextGrid: return platform::MusicTiming::Grid;
    case LTDMEnactNextSegment: return platform::MusicTiming::Segment;
    // Segments' authored default boundary is not exposed; measures are the
    // DirectMusic Producer default.
    default: return platform::MusicTiming::Measure;
    }
}
}
struct ClientMusic::State {
    using Segment=std::shared_ptr<const platform::MusicSegment>;
    enum class Step { PlaySegment,PlayTransition,Loop,Next };
    struct Pending { Step step; Segment segment; int value=0; };
    struct Intensity { std::vector<Segment> segments; int loop=0,next=0; };
    struct Transition { LTDMEnactTypes enact=LTDMEnactNextMeasure; bool manual=true; Segment segment; };
    // A MOTIF line: the motif and the boundary its line names for a default request.
    struct Motif { std::shared_ptr<const platform::MusicMotif> motif; LTDMEnactTypes enact=LTDMEnactDefault; };
    struct Level {
        std::string directory,banks;
        int intensities=0,initial=0,current=0;
        long volume=0;
        std::vector<Intensity> intensity;
        std::vector<Transition> transitions;
        std::map<std::string,Motif> motifs;
        std::map<std::string,Segment> loaded;
        Transition& transition(int from,int to) { return transitions[std::size_t(from)*(intensities+1)+to]; }
    };
    MusicServices services;
    std::unique_ptr<platform::MusicPerformance> performance;
    std::shared_ptr<platform::AudioOutput> output;
    std::optional<Level> level;
    std::vector<Pending> pending;
    // The last request handed to the performance; the queue resumes after it.
    std::optional<std::size_t> queued_after;
    std::optional<std::uint32_t> handled;
    const std::thread::id owner=std::this_thread::get_id();
    explicit State(MusicServices source) : services(std::move(source)) {
        if (!services.assets || !services.performance || !services.output) throw std::invalid_argument("native music services are required");
    }
    void thread() const { if (owner!=std::this_thread::get_id()) throw std::logic_error("client music used from another thread"); }
    bool ready() const { thread(); return performance && level; }
    std::optional<std::vector<std::byte>> resolve(std::string_view file) const {
        if (!level) return {};
        for (const auto* directory : {&level->directory,&level->banks}) {
            if (directory->empty() && directory!=&level->directory) continue;
            const auto path=*directory+std::string(file); if (services.assets->contains(path)) return services.assets->read(path);
        }
        return {};
    }
    Segment segment(std::string_view name) {
        const auto key=lower(name); if (const auto found=level->loaded.find(key); found!=level->loaded.end()) return found->second;
        auto loaded=performance->load(name); level->loaded.emplace(key,loaded); return loaded;
    }
    void play(const Segment& segment,LTDMEnactTypes enact) { performance->play(segment,timing(enact)); }
    // A segment scheduled at a boundary supersedes the playing one: its end
    // must not consume the queue, which resumes once the new segment starts.
    void supersede(const Segment& segment,LTDMEnactTypes enact) { handled=performance->playback().serial; play(segment,enact); }
    LTRESULT change(int next,LTDMEnactTypes start) {
        if (!ready()) return LT_ERROR;
        if (next==0) { stop(start); return LT_ERROR; }
        if (next<0 || next>level->intensities) return LT_ERROR;
        if (level->current==next) return LT_OK;
        const auto& transition=level->transition(level->current,next); const auto& intensity=level->intensity[std::size_t(next)];
        pending.clear(); const auto enact=start==LTDMEnactInvalid ? transition.enact : start; const bool queue=enact==LTDMEnactNextSegment;
        if (transition.segment) { if (queue) pending.push_back({Step::PlayTransition,transition.segment}); else supersede(transition.segment,enact); }
        else if (!queue && !intensity.segments.empty()) supersede(intensity.segments.front(),enact);
        const bool first_playing=!transition.segment && !queue && !intensity.segments.empty();
        for (const auto& segment : intensity.segments) pending.push_back({Step::PlaySegment,segment});
        pending.push_back({Step::Loop,nullptr,intensity.loop});
        if (intensity.next>0) pending.push_back({Step::Next,nullptr,intensity.next});
        queued_after=first_playing ? std::optional<std::size_t>(0) : std::nullopt;
        level->current=next; return LT_OK;
    }
    // The original plays a preloaded motif as a secondary segment, found by
    // name alone; a default request takes its line's boundary, and a request
    // for the next segment starts it with the next segment.
    LTRESULT motif(const char* name,LTDMEnactTypes start) {
        if (!ready() || !name) return LT_ERROR;
        const auto found=level->motifs.find(lower(name)); if (found==level->motifs.end()) return LT_ERROR;
        const auto enact=start==LTDMEnactDefault ? found->second.enact : start;
        performance->play_motif(found->second.motif,timing(enact)); return LT_OK;
    }
    void stop(LTDMEnactTypes start) {
        if (start!=LTDMEnactNextSegment) performance->play(nullptr,timing(start));
        pending.clear(); queued_after.reset();
    }
    // The shipped notification thread's SEGALMOSTEND walk of the queue.
    void advance() {
        std::size_t node=queued_after ? *queued_after+1 : 0;
        for (unsigned step=0;node<pending.size();++step) {
            if (step>=queue_steps) { pending.clear(); queued_after.reset(); return; }
            auto& request=pending[node];
            switch (request.step) {
            case Step::Loop:
                if (request.value==0) { ++node; break; }
                if (request.value>0) --request.value;
                node=request.value==0 ? node+1 : 0; break;
            case Step::Next: change(request.value,LTDMEnactInvalid); node=0; break;
            case Step::PlaySegment: play(request.segment,LTDMEnactNextSegment); queued_after=node; return;
            case Step::PlayTransition: play(request.segment,LTDMEnactNextSegment); pending.erase(pending.begin()+std::ptrdiff_t(node)); queued_after.reset(); return;
            }
        }
        queued_after.reset();
    }
    void render(std::span<float> stereo) {
        while (!stereo.empty()) {
            const auto state=performance->playback();
            if (state.playing && state.serial!=handled && state.remaining<=std::size_t(almost_end*music_rate)) { handled=state.serial; advance(); }
            const auto frames=std::min<std::size_t>(render_frames,stereo.size()/2);
            performance->render(stereo.first(frames*2)); stereo=stereo.subspan(frames*2);
        }
    }
    void pump() {
        if (!performance || !output) return;
        const auto target=std::size_t(output->format().rate*device_lead); std::vector<float> block;
        while (output->queued_frames()<target) {
            block.assign(std::min<std::size_t>(render_frames,target-output->queued_frames())*2,0.f); render(block);
            for (auto& sample : block) sample=std::clamp(sample,-1.f,1.f);
            output->write(block);
        }
    }
    void unload() { if (performance) performance->play(nullptr,platform::MusicTiming::Instant); pending.clear(); queued_after.reset(); level.reset(); }
};
ClientMusic::ClientMusic(MusicServices services) : state_(std::make_unique<State>(std::move(services))) {}
ClientMusic::~ClientMusic()=default;
void ClientMusic::pump() { state_->thread(); state_->pump(); }
LTRESULT ClientMusic::Init() {
    auto& s=*state_; s.thread(); if (s.performance) return LT_OK;
    try {
        auto output=s.services.output({music_rate,2}); if (!output || output->format()!=platform::AudioFormat{music_rate,2}) return LT_ERROR;
        s.performance=s.services.performance([&s](std::string_view file) { return s.resolve(file); },music_rate); s.performance->volume(synth_headroom);
        s.output=std::move(output); return LT_OK;
    } catch (...) { s.performance.reset(); s.output.reset(); return LT_ERROR; }
}
LTRESULT ClientMusic::Term() { auto& s=*state_; s.thread(); s.unload(); s.performance.reset(); s.output.reset(); s.handled.reset(); return LT_OK; }
LTRESULT ClientMusic::InitLevel(const char* directory,const char* control,const char* define1,const char* define2,const char* define3) {
    auto& s=*state_; s.thread();
    if (!s.performance || s.level || !directory || !control) return LT_ERROR;
    try {
        std::string folder=directory; if (!folder.empty() && folder.back()!='\\' && folder.back()!='/') folder+='\\';
        const std::string_view name=control; const auto path=name.find_first_of("\\/:")==name.npos ? folder+std::string(name) : std::string(name);
        if (!s.services.assets->contains(path)) return LT_ERROR;
        std::vector<std::string> defines; for (const auto* define : {define1,define2,define3}) if (define) defines.emplace_back(define);
        const auto bytes=s.services.assets->read(path); const auto file=ControlFile::parse(bytes,defines);
        State::Level level; level.directory=folder;
        const auto value=[&](const char* key,auto fallback) { const auto* found=file.key(key); return found && found->integer(0) ? *found->integer(0) : fallback; };
        level.intensities=value("NUMINTENSITIES",0); level.initial=value("INITIALINTENSITY",0); level.volume=value("INITIALVOLUME",0);
        if (const auto* banks=file.key("DLSBANKDIRECTORY"); banks && !banks->words.empty()) { level.banks=banks->words.front(); if (level.banks.back()!='\\' && level.banks.back()!='/') level.banks+='\\'; }
        if (level.intensities<1 || level.intensities>1024) return LT_ERROR;
        level.intensity.resize(std::size_t(level.intensities)+1); level.transitions.resize(std::size_t(level.intensities+1)*(level.intensities+1));
        s.level=std::move(level); auto& l=*s.level; s.performance->scope();
        for (const auto* key : file.keys("INTENSITY")) {
            const auto number=key->integer(0).value_or(0); if (number<1 || number>l.intensities || key->words.size()<4) continue;
            auto& intensity=l.intensity[std::size_t(number)];
            for (std::size_t word=3;word<key->words.size();++word) { auto segment=s.segment(key->words[word]); if (!segment) break; intensity.segments.push_back(std::move(segment)); }
            intensity.loop=*key->integer(1); intensity.next=*key->integer(2);
        }
        for (const auto* key : file.keys("TRANSITION")) {
            const auto from=key->integer(0).value_or(0),to=key->integer(1).value_or(0);
            if (from<1 || from>l.intensities || to<1 || to>l.intensities || key->words.size()<4) continue;
            const auto enact=StringToEnactType(key->words[2].c_str()); if (enact==LTDMEnactInvalid) continue;
            const bool manual=ascii_compare(key->words[3],"MANUAL")==0; if (!manual && ascii_compare(key->words[3],"AUTOMATIC")!=0) continue;
            auto& transition=l.transition(from,to);
            if (key->words.size()>4) transition.segment=s.segment(key->words[4]);
            transition.enact=enact; transition.manual=manual;
        }
        for (const auto* key : file.keys("MOTIF")) {
            if (key->words.size()<2) continue;
            State::Motif motif; motif.motif=s.performance->load_motif(key->words[0],key->words[1]); if (!motif.motif) continue;
            if (key->words.size()>2) if (const auto enact=StringToEnactType(key->words[2].c_str()); enact!=LTDMEnactInvalid) motif.enact=enact;
            l.motifs.insert_or_assign(lower(key->words[1]),std::move(motif));
        }
        s.pending.clear(); s.queued_after.reset(); l.current=0; return LT_OK;
    } catch (...) { s.level.reset(); return LT_ERROR; }
}
LTRESULT ClientMusic::TermLevel() { auto& s=*state_; if (!s.ready()) return LT_ERROR; s.unload(); return LT_OK; }
LTRESULT ClientMusic::Play() { auto& s=*state_; if (!s.ready()) return LT_ERROR; try { return s.change(s.level->initial,LTDMEnactImmediatly); } catch (...) { return LT_ERROR; } }
LTRESULT ClientMusic::Stop(const LTDMEnactTypes start) { auto& s=*state_; if (!s.ready()) return LT_ERROR; try { s.stop(start); return LT_OK; } catch (...) { return LT_ERROR; } }
LTRESULT ClientMusic::Pause(const LTDMEnactTypes) { return LT_OK; }
LTRESULT ClientMusic::UnPause() { return LT_OK; }
LTRESULT ClientMusic::SetVolume(const long volume) {
    auto& s=*state_; if (!s.ready()) return LT_ERROR;
    // DirectMusic master volume: hundredths of a decibel, 0 unity, clamped to
    // -200..+20 dB. The game adds its slider (-5000..5000) to the level's
    // initial volume.
    const auto decibels=double(std::clamp(volume,-20000L,2000L))/100;
    try { s.performance->volume(float(synth_headroom*std::pow(10.,decibels/20))); return LT_OK; } catch (...) { return LT_ERROR; }
}
LTRESULT ClientMusic::ChangeIntensity(const int intensity,const LTDMEnactTypes start) { try { return state_->change(intensity,start); } catch (...) { return LT_ERROR; } }
LTRESULT ClientMusic::PlaySecondary(const char*,const LTDMEnactTypes) { return LT_ERROR; }
LTRESULT ClientMusic::StopSecondary(const char*,const LTDMEnactTypes) { return state_->ready() ? LT_OK : LT_ERROR; }
LTRESULT ClientMusic::PlayMotif(const char*,const char* motif,const LTDMEnactTypes start) { try { return state_->motif(motif,start); } catch (...) { return LT_ERROR; } }
LTRESULT ClientMusic::StopMotif(const char*,const char*,const LTDMEnactTypes) { return state_->ready() ? LT_OK : LT_ERROR; }
int ClientMusic::GetCurIntensity() { return state_->ready() ? state_->level->current : 0; }
LTDMEnactTypes ClientMusic::StringToEnactType(const char* name) {
    if (!name) return LTDMEnactInvalid;
    static constexpr std::pair<const char*,LTDMEnactTypes> names[]={{"Invalid",LTDMEnactInvalid},{"Default",LTDMEnactDefault},{"Immediatly",LTDMEnactImmediatly},
        {"Immediate",LTDMEnactImmediatly},{"NextBeat",LTDMEnactNextBeat},{"NextMeasure",LTDMEnactNextMeasure},{"NextGrid",LTDMEnactNextGrid},{"NextSegment",LTDMEnactNextSegment},
        {"Beat",LTDMEnactNextBeat},{"Measure",LTDMEnactNextMeasure},{"Grid",LTDMEnactNextGrid},{"Segment",LTDMEnactNextSegment}};
    for (const auto& [text,type] : names) if (ascii_compare(name,text)==0) return type;
    return LTDMEnactInvalid;
}
void ClientMusic::EnactTypeToString(LTDMEnactTypes type,char* name) {
    if (!name) return;
    const char* text="";
    switch (type) {
    case LTDMEnactInvalid: text="Invalid"; break;
    case LTDMEnactDefault: text="Default"; break;
    case LTDMEnactImmediatly: text="Immediate"; break;
    case LTDMEnactNextBeat: text="Beat"; break;
    case LTDMEnactNextMeasure: text="Measure"; break;
    case LTDMEnactNextGrid: text="Grid"; break;
    case LTDMEnactNextSegment: text="Segment"; break;
    }
    std::strcpy(name,text);
}
int ClientMusic::GetNumIntensities() { return state_->ready() ? state_->level->intensities : 0; }
int ClientMusic::GetInitialIntensity() { return state_->ready() ? state_->level->initial : 0; }
long ClientMusic::GetInitialVolume() { return state_->ready() ? state_->level->volume : 0; }
}
