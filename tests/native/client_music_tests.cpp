#include "engine/client/client_music.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <random>

namespace {
using nolf::platform::MusicTiming;
void require(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
constexpr std::uint64_t rate=48000,measure=rate/2;
// dmusic's scheduling: boundaries from the playing segment's start, a later
// request at the same time replaces an earlier one, and a starting segment
// discards everything still scheduled.
struct Simulated final : nolf::platform::MusicPerformance {
    struct Segment final : nolf::platform::MusicSegment { std::string name; std::uint64_t frames; double length() const override { return double(frames)/rate; } };
    nolf::platform::MusicResolver resolver;
    std::map<std::string,std::uint64_t> lengths;
    std::uint64_t now=0,start=0;
    std::shared_ptr<const Segment> current;
    std::map<std::uint64_t,std::shared_ptr<const Segment>> scheduled;
    std::uint32_t serial=0;
    float gain=1;
    std::vector<std::pair<std::string,std::uint64_t>> started;
    unsigned scopes=0;
    void scope() override { ++scopes; }
    std::shared_ptr<const nolf::platform::MusicSegment> load(std::string_view name) override {
        if (!resolver(name)) return nullptr;
        auto segment=std::make_shared<Segment>(); segment->name=std::string(name); const auto found=lengths.find(segment->name);
        segment->frames=found==lengths.end() ? 2*measure : found->second; return segment;
    }
    void play(const std::shared_ptr<const nolf::platform::MusicSegment>& segment,MusicTiming timing) override {
        std::uint64_t at=now;
        if (timing==MusicTiming::Segment && current) at=std::max(now,start+current->frames);
        if (timing==MusicTiming::Measure && now>start) at=start+(now-start+measure-1)/measure*measure;
        if (timing==MusicTiming::Beat && now>start) at=start+(now-start+measure/4-1)/(measure/4)*(measure/4);
        scheduled[at]=std::static_pointer_cast<const Segment>(segment);
    }
    void volume(float value) override { gain=value; }
    nolf::platform::MusicPlayback playback() const override {
        if (!current) return {serial,0,false};
        const auto end=start+current->frames; return {serial,std::size_t(end>now ? end-now : 0),true};
    }
    void render(std::span<float> stereo) override {
        const auto until=now+stereo.size()/2;
        while (!scheduled.empty() && scheduled.begin()->first<until) {
            const auto [at,segment]=*scheduled.begin(); now=std::max(now,at); scheduled.clear(); current=segment; start=now; ++serial;
            if (segment) started.emplace_back(segment->name,now);
        }
        now=until; std::fill(stereo.begin(),stereo.end(),current && now<start+current->frames ? .25f : 0.f);
    }
};
struct Device final : nolf::platform::AudioOutput {
    nolf::platform::AudioFormat output{48000,2}; std::size_t queued=0; std::vector<float> samples; bool keep=false;
    nolf::platform::AudioFormat format() const override { return output; }
    void write(std::span<const float> data) override { queued+=data.size()/2; if (keep) samples.insert(samples.end(),data.begin(),data.end()); }
    std::size_t queued_frames() const override { return queued; }
    void pause(bool) override {}
    void clear() override { queued=0; }
};
struct Fixture {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("nolf-music-"+std::to_string(std::random_device{}()));
    std::shared_ptr<nolf::Assets> assets=std::make_shared<nolf::Assets>();
    Simulated* performance=nullptr;
    std::shared_ptr<Device> device;
    bool no_device=false;
    std::unique_ptr<nolf::ClientMusic> music;
    explicit Fixture(std::string_view control,std::map<std::string,std::uint64_t> lengths = {}) {
        std::filesystem::create_directories(root/"Music"/"Test");
        std::ofstream(root/"Music"/"Test"/"Test.txt")<<control;
        for (const char* name : {"A.sgt","B.sgt","C.sgt","D.sgt","E.sgt","T.sgt","U.sgt"}) std::ofstream(root/"Music"/"Test"/name)<<"segment";
        assets->mount_directory(root);
        music=std::make_unique<nolf::ClientMusic>(nolf::MusicServices{assets,
            [this,lengths](nolf::platform::MusicResolver resolver,unsigned r) { require(r==rate,"music rate"); auto p=std::make_unique<Simulated>(); p->resolver=std::move(resolver); p->lengths=lengths; performance=p.get(); return p; },
            [this](nolf::platform::AudioFormat format) -> std::shared_ptr<nolf::platform::AudioOutput> { if (no_device) throw std::runtime_error("no device"); device=std::make_shared<Device>(); device->output=format; return device; }});
    }
    ~Fixture() { music.reset(); std::error_code ignored; std::filesystem::remove_all(root,ignored); }
    void level() { require(music->Init()==LT_OK && music->InitLevel("Music\\Test\\","Test.txt",nullptr,nullptr,nullptr)==LT_OK,"music level init failed"); }
    // The device consumes in 10 ms steps, as the host pumps every frame.
    void run(double seconds) { for (auto frames=std::uint64_t(seconds*rate);frames;) { const auto step=std::min<std::uint64_t>(frames,480); device->queued-=std::min<std::size_t>(device->queued,step); frames-=step; music->pump(); } }
    std::vector<std::string> names() const { std::vector<std::string> result; for (const auto& [name,at] : performance->started) result.push_back(name); return result; }
    void contiguous() const {
        for (std::size_t i=1;i<performance->started.size();++i) {
            const auto previous=performance->started[i-1]; const auto length=performance->lengths.contains(previous.first) ? performance->lengths.at(previous.first) : 2*measure;
            require(performance->started[i].second==previous.second+length,"queued segment did not start exactly at the previous end");
        }
    }
};
using Names=std::vector<std::string>;
constexpr char control[]=
    "NUMINTENSITIES 5\nINITIALINTENSITY 1\nINITIALVOLUME -300\n"
    "INTENSITY 1 -1 0 A.sgt, B.sgt\nINTENSITY 2 0 3 C.sgt D.sgt\nINTENSITY 3 2 0 E.sgt\nINTENSITY 4 -1 0 Missing.sgt A.sgt\nINTENSITY 5 -1 0 Missing.sgt\n"
    "TRANSITION 1 2 MEASURE MANUAL T.sgt\nTRANSITION 2 1 SEGMENT AUTOMATIC U.sgt\nTRANSITION 1 3 BOGUS MANUAL\nTRANSITION 3 1 IMMEDIATE OTHER\n";
void checks() {
    {
        Fixture f(control); auto& m=*f.music;
        require(m.InitLevel("Music\\Test\\","Test.txt",nullptr,nullptr,nullptr)==LT_ERROR && m.Play()==LT_ERROR && m.ChangeIntensity(1,LTDMEnactDefault)==LT_ERROR,"music used before Init");
        f.level(); require(m.InitLevel("Music\\Test\\","Test.txt",nullptr,nullptr,nullptr)==LT_ERROR,"second level accepted");
        require(f.performance->scopes==1,"level did not start a resource scope");
        require(m.GetNumIntensities()==5 && m.GetInitialIntensity()==1 && m.GetInitialVolume()==-300 && m.GetCurIntensity()==0,"control file values wrong");
        require(m.Play()==LT_OK && m.GetCurIntensity()==1,"play failed"); f.run(4.1);
        require((f.names()==Names{"A.sgt","B.sgt","A.sgt","B.sgt","A.sgt"}),"intensity did not loop its segments in order"); f.contiguous();
        // Transition at the next measure, then the new segments, then the
        // automatic switch to intensity 3, whose loop count of two plays its
        // list twice in total and then ends.
        f.performance->started.clear(); f.run(.3); const auto changed=f.performance->now;
        require(m.ChangeIntensity(2,LTDMEnactInvalid)==LT_OK && m.GetCurIntensity()==2,"intensity change failed"); f.run(5.1);
        require((f.names()==Names{"T.sgt","C.sgt","D.sgt","E.sgt","E.sgt"}),"transition, segments or follow-on intensity wrong");
        require(f.performance->started.front().second==(changed+measure-1)/measure*measure && f.performance->started.front().second>changed,"transition not on the measure boundary");
        require(m.GetCurIntensity()==3,"follow-on intensity not current"); f.run(2); require(f.performance->started.size()==5,"finished intensity kept playing");
        require(m.ChangeIntensity(3,LTDMEnactDefault)==LT_OK && m.ChangeIntensity(6,LTDMEnactDefault)==LT_ERROR && m.ChangeIntensity(-1,LTDMEnactDefault)==LT_ERROR,"intensity range checks wrong");
        // A queued change waits for the playing segment to end.
        require(m.ChangeIntensity(1,LTDMEnactImmediatly)==LT_OK,"immediate change failed"); f.performance->started.clear(); f.run(.5);
        require(m.ChangeIntensity(2,LTDMEnactNextSegment)==LT_OK,"queued change failed"); f.run(2.1);
        require((f.names()==Names{"A.sgt","T.sgt","C.sgt"}),"next-segment change cut the playing segment or skipped its transition");
        require(m.ChangeIntensity(0,LTDMEnactImmediatly)==LT_ERROR,"intensity zero must stop and report an error, as shipped"); f.run(.1);
        require(!f.performance->playback().playing && m.GetCurIntensity()==2,"stop did not silence or reset the intensity");
        f.run(2); require(f.performance->started.size()==3,"stopped music resumed");
        require(m.SetVolume(0)==LT_OK,"volume failed"); const auto unity=f.performance->gain;
        require(unity>.2f && unity<=1 && m.SetVolume(-2000)==LT_OK && std::abs(f.performance->gain-unity*.1f)<1e-6f &&
            m.SetVolume(2000)==LT_OK && std::abs(f.performance->gain-unity*10)<1e-5f && m.SetVolume(5000)==LT_OK && std::abs(f.performance->gain-unity*10)<1e-5f &&
            m.SetVolume(-30000)==LT_OK && f.performance->gain<unity*1e-9f,"volume mapping wrong");
        require(m.Pause(LTDMEnactDefault)==LT_OK && m.UnPause()==LT_OK && m.PlaySecondary("A.sgt",LTDMEnactDefault)==LT_ERROR && m.PlayMotif(nullptr,"M",LTDMEnactDefault)==LT_ERROR,"unsupported operations misreported");
        require(std::async(std::launch::async,[&] { return m.ChangeIntensity(1,LTDMEnactDefault); }).get()==LT_ERROR,"music used from another thread");
        require(m.TermLevel()==LT_OK && m.GetNumIntensities()==0 && m.TermLevel()==LT_ERROR,"level termination failed"); f.level();
        require(m.Term()==LT_OK && m.Play()==LT_ERROR,"music termination failed");
    }
    {
        // A segment that ends at its last measure while a change is scheduled
        // for that same boundary plays the change first, then its successor.
        Fixture f(control,{{"A.sgt",2*measure},{"C.sgt",measure}}); auto& m=*f.music; f.level(); m.Play(); f.run(.6);
        f.performance->started.clear(); require(m.ChangeIntensity(2,LTDMEnactNextMeasure)==LT_OK,"measure change failed"); f.run(.2);
        require(f.names().empty(),"measure change started early"); f.run(.3);
        require((f.names()==Names{"T.sgt"}) && f.performance->started[0].second==measure*2,"superseding segment not at the shared boundary"); f.run(1.6);
        require((f.names()==Names{"T.sgt","C.sgt","D.sgt"}),"successor replaced the superseding segment"); f.contiguous();
    }
    {
        // Missing segments truncate an intensity; one with none must not hang.
        Fixture f(control); auto& m=*f.music; f.level();
        require(m.ChangeIntensity(4,LTDMEnactImmediatly)==LT_OK,"empty intensity change failed"); f.run(1); require(f.names().empty(),"segment after a missing one was kept");
        require(m.ChangeIntensity(5,LTDMEnactImmediatly)==LT_OK,"empty intensity change failed"); f.run(1);
        require(m.ChangeIntensity(1,LTDMEnactImmediatly)==LT_OK,"recovery change failed"); f.run(.1); require((f.names()==Names{"A.sgt"}),"music did not recover");
    }
    {
        Fixture f(control); auto& m=*f.music; char name[16]{};
        require(m.StringToEnactType("nextmeasure")==LTDMEnactNextMeasure && m.StringToEnactType("Immediatly")==LTDMEnactImmediatly && m.StringToEnactType("SEGMENT")==LTDMEnactNextSegment && m.StringToEnactType("x")==LTDMEnactInvalid && m.StringToEnactType(nullptr)==LTDMEnactInvalid,"enact names wrong");
        m.EnactTypeToString(LTDMEnactImmediatly,name); require(std::string(name)=="Immediate","enact string wrong");
        require(m.InitLevel("Music\\Test\\","Absent.txt",nullptr,nullptr,nullptr)==LT_ERROR,"level without Init accepted"); require(m.Init()==LT_OK,"init failed");
        require(m.InitLevel("Music\\Test\\","Absent.txt",nullptr,nullptr,nullptr)==LT_ERROR && m.InitLevel(nullptr,"Test.txt",nullptr,nullptr,nullptr)==LT_ERROR,"missing control file accepted");
    }
    { Fixture f(control); f.no_device=true; require(f.music->Init()==LT_ERROR,"music initialized without an audio device"); }
}
// Logs what the scheduler asks of the real performance and when segments start.
struct Traced final : nolf::platform::MusicPerformance {
    struct Named final : nolf::platform::MusicSegment { std::shared_ptr<const nolf::platform::MusicSegment> inner; std::string name; double length() const override { return inner->length(); } };
    std::unique_ptr<nolf::platform::MusicPerformance> inner; std::uint64_t frames=0; std::uint32_t serial=0; std::ostream* log=nullptr;
    double now() const { return double(frames)/rate; }
    void scope() override { inner->scope(); }
    std::shared_ptr<const nolf::platform::MusicSegment> load(std::string_view name) override {
        auto segment=inner->load(name); if (!segment) return nullptr; auto named=std::make_shared<Named>(); named->inner=std::move(segment); named->name=std::string(name); return named;
    }
    void play(const std::shared_ptr<const nolf::platform::MusicSegment>& segment,MusicTiming timing) override {
        const auto* named=static_cast<const Named*>(segment.get());
        if (log) *log<<"  "<<now()<<"s request "<<(named ? named->name : "stop")<<" timing "<<int(timing)<<'\n';
        inner->play(named ? named->inner : nullptr,timing);
    }
    void volume(float gain) override { inner->volume(gain); }
    nolf::platform::MusicPlayback playback() const override { return inner->playback(); }
    void render(std::span<float> stereo) override {
        inner->render(stereo); frames+=stereo.size()/2; const auto state=inner->playback();
        if (state.serial!=serial && log) *log<<"  "<<now()<<"s started #"<<state.serial<<" playing "<<state.playing<<" remaining "<<double(state.remaining)/rate<<"s\n";
        serial=state.serial;
    }
};
// Every original control file loads on the real synthesizer, and each of its
// intensities renders finite audio through the scheduler.
void corpus(const char* data,const char* demo) {
    auto assets=std::make_shared<nolf::Assets>(); assets->mount_game(data); std::shared_ptr<Device> device;
    nolf::ClientMusic music({assets,nolf::platform::make_dmusic_performance,[&](nolf::platform::AudioFormat format) { device=std::make_shared<Device>(); device->output=format; device->keep=true; return device; }});
    require(music.Init()==LT_OK,"original music init failed"); unsigned levels=0,intensities=0,audible=0; double peak=0;
    std::map<std::string,std::string> controls; for (const auto& name : assets->names()) if (name.starts_with("music/") && name.ends_with(".txt") && std::count(name.begin(),name.end(),'/')==2) controls.emplace(name.substr(0,name.rfind('/')+1),name.substr(name.rfind('/')+1));
    for (const auto& [directory,control] : controls) {
        require(music.InitLevel(directory.c_str(),control.c_str(),nullptr,nullptr,nullptr)==LT_OK,"original music level failed");
        for (int intensity=1;intensity<=music.GetNumIntensities();++intensity) {
            device->samples.clear(); require(music.ChangeIntensity(intensity,LTDMEnactImmediatly)==LT_OK || music.GetCurIntensity()==intensity,"original intensity change failed");
            for (int step=0;step<60;++step) { device->queued=0; music.pump(); }
            double energy=0; for (const auto sample : device->samples) { require(std::isfinite(sample),"original music rendered non-finite samples"); energy+=double(sample)*sample; peak=std::max(peak,double(std::abs(sample))); }
            audible+=energy>1e-3; ++intensities;
        }
        require(music.TermLevel()==LT_OK,"original music level termination failed"); ++levels;
    }
    require(peak<1,"original music clipped");
    std::cout<<"Native music: "<<levels<<" original control files, "<<intensities<<" intensities rendered, "<<audible<<" audible, peak "<<peak<<'\n';
    if (!demo) return;
    // Theme, ambient, medium, high then back down, for listening.
    Traced* traced=nullptr; nolf::ClientMusic theme({assets,[&](nolf::platform::MusicResolver resolver,unsigned r) { auto t=std::make_unique<Traced>(); t->inner=nolf::platform::make_dmusic_performance(std::move(resolver),r); t->log=&std::cout; traced=t.get(); return t; },
        [&](nolf::platform::AudioFormat format) { device=std::make_shared<Device>(); device->output=format; device->keep=true; return device; }});
    require(theme.Init()==LT_OK && theme.InitLevel("Music\\NOLFTheme\\","NolfTheme.txt",nullptr,nullptr,nullptr)==LT_OK,"theme level failed");
    for (const auto [intensity,seconds] : {std::pair{3,20},{5,40},{6,40},{4,30}}) { std::cout<<" intensity "<<intensity<<" at "<<traced->now()<<"s\n"; theme.ChangeIntensity(intensity,LTDMEnactInvalid); for (int i=0;i<seconds*10;++i) { device->queued=0; theme.pump(); } }
    std::ofstream out(demo,std::ios::binary); const auto bytes=std::uint32_t(device->samples.size()*2);
    const auto put=[&](auto value) { out.write(reinterpret_cast<const char*>(&value),sizeof value); };
    out.write("RIFF",4); put(36+bytes); out.write("WAVEfmt ",8); put(std::uint32_t(16)); put(std::uint16_t(1)); put(std::uint16_t(2)); put(std::uint32_t(48000)); put(std::uint32_t(48000*4)); put(std::uint16_t(4)); put(std::uint16_t(16));
    out.write("data",4); put(bytes); for (const auto sample : device->samples) put(std::int16_t(std::lround(std::clamp(sample,-1.f,1.f)*32767)));
    std::cout<<"Native music demo: "<<device->samples.size()/2/48000<<" s written to "<<demo<<'\n';
}
}
int main(int argc,char** argv) { try { checks(); if (argc>1) corpus(argv[1],argc>2 ? argv[2] : nullptr); std::cout<<"Native music scheduling, transitions, loops, volume and lifetime passed\n"; return 0; } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
