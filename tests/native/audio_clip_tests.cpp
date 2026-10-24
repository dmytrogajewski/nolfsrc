#include "engine/client/audio_clips.h"
#include "audio_clip_fixture.h"
#include "engine/portable/binary.h"
#include "platform/system.h"
#include <algorithm>
#include <future>
#include <iostream>

namespace {
void require(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
template<class F> void rejects(F&& fn) { bool rejected=false; try { fn(); } catch (const std::exception&) { rejected=true; } require(rejected,"invalid sound cache operation accepted"); }
using Files=audio_clip_fixture::Files;
void checks() {
    Files files; auto assets=std::make_shared<nolf::Assets>(); assets->mount_directory(files.root);
    auto cache=std::make_unique<nolf::AudioClips>(assets,nolf::platform::make_ffmpeg_audio,4096);
    auto a=cache->load("A.WAV"); require(a->name()=="a.wav" && a->frames()==512 && a->duration()==.064 && a->wave().format==1 && a->samples()[1]==32.f/32768 && a==cache->load("a.wav"),"native sound cache identity/metadata/samples differ");
    auto b=cache->load("b.wav"); require(cache->bytes()==4096 && cache->cached()==2,"sound cache budget/entries differ"); b.reset(); auto c=cache->load("c.wav"); require(cache->cached()==2 && cache->bytes()==4096 && a==cache->load("a.wav"),"LRU evicted a live sound clip");
    rejects([&] { cache->load("d.wav"); }); require(cache->bytes()==4096 && a->frames()==512 && c->frames()==512,"budget failure changed retained clip data/accounting");
    c.reset(); cache->collect(); require(cache->cached()==1 && cache->bytes()==2048,"unused sound clips did not release budget");
    rejects([&] { cache->load("../a.wav"); }); rejects([&] { cache->load("missing.wav"); }); require(cache->bytes()==2048,"failed sound load leaked budget");
    require(std::async(std::launch::async,[&] { try { cache->bytes(); } catch (const std::logic_error&) { return true; } return false; }).get(),"audio cache accepted a foreign thread");
    assets.reset(); cache.reset(); require(a->samples()[511]==511.f/1024 && a->frames()==512,"sound samples/ledger did not survive cache retirement");
    auto source=std::make_shared<nolf::Assets>(); source->mount_directory(files.root);
    std::unique_ptr<nolf::AudioClips> retired;
    retired=std::make_unique<nolf::AudioClips>(source,[&](auto input,auto format,auto frames) { retired.reset(); return nolf::platform::make_ffmpeg_audio(std::move(input),format,frames); },4096);
    const auto pinned=retired->load("b.wav"); require(!retired && pinned->frames()==512,"audio factory retirement invalidated an active load");
}
void corpus(const char* data) {
    auto assets=std::make_shared<nolf::Assets>(); assets->mount_game(data); nolf::AudioClips cache(assets,nolf::platform::make_ffmpeg_audio); std::size_t total=0,peak=0; std::uint64_t frames=0;
    for (const auto& name : assets->names()) if (name.ends_with(".wav")) {
        try { const auto clip=cache.load(name); require(clip && clip->frames() && clip->samples().size()==clip->frames()*clip->wave().channels,"original sound clip missing samples"); peak=std::max(peak,cache.bytes()); require(peak<=128*1024*1024,"original sound cache exceeded budget"); ++total; frames+=clip->frames(); }
        catch (const std::exception& e) { throw std::runtime_error(name+": "+e.what()); }
    }
    cache.collect(); require(cache.bytes()==0 && cache.cached()==0,"original sound corpus retained unused clips");
    std::cout<<"Native cached audio: "<<total<<" sounds, "<<frames<<" frames, peak="<<peak<<"/134217728 bytes, unused clips released\n";
}
}
int main(int argc,char** argv) { try { checks(); if (argc>1) corpus(argv[1]); std::cout<<"Native sound clip decode/cache, pinned budgets, LRU, samples and factory lifetime passed\n"; return 0; } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
