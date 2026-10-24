#include "platform/audio.h"
#include <array>
#include <chrono>
#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
void require(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
template<class F> void rejects(F&& fn) { bool rejected=false; try { fn(); } catch (const std::exception&) { rejected=true; } require(rejected,"invalid audio output accepted"); }
void drains(nolf::platform::AudioOutput& output,const char* why) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while (output.queued_frames() && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    require(!output.queued_frames(),why);
}
void checks() {
    {
        // A new output plays without being resumed.
        auto fresh=nolf::platform::make_sdl_audio(); std::array<float,1024> silence{}; fresh->write(silence);
        drains(*fresh,"new native audio output did not play");
    }
    {
        // Outputs in different formats play together; a paused one keeps its queue.
        auto music=nolf::platform::make_sdl_audio({44100,2}),voice=nolf::platform::make_sdl_audio({22050,1});
        std::vector<float> stereo(4410*2,.1f),mono(2205,.1f); voice->pause(true); music->write(stereo); voice->write(mono);
        drains(*music,"mixed native audio output did not play");
        require(voice->queued_frames()==2205,"paused mixed native audio output advanced");
        voice->pause(false); drains(*voice,"resumed mixed native audio output did not play");
    }
    auto output=nolf::platform::make_sdl_audio(); require(output->format()==nolf::platform::AudioFormat{48000,2},"native audio output format differs"); output->pause(true);
    std::array<float,1024> samples{}; samples[0]=.5f; output->write(samples); require(output->queued_frames()==512,"paused native audio queue differs");
    rejects([&] { nolf::platform::make_sdl_audio({0,2}); }); output->write(samples); require(output->queued_frames()==1024,"failed construction shut down active audio device");
    rejects([&] { output->write(std::span(samples).first(3)); }); auto bad=samples; bad[0]=std::numeric_limits<float>::quiet_NaN(); rejects([&] { output->write(bad); });
    std::vector<float> large(48001*2); rejects([&] { output->write(large); }); require(output->queued_frames()==1024,"invalid audio write changed playback queue");
    require(std::async(std::launch::async,[&] { try { output->queued_frames(); } catch (const std::logic_error&) { return true; } return false; }).get(),"audio output crossed owner thread");
    output->pause(false); drains(*output,"native SDL audio device did not consume queued samples");
    output->pause(true); output->write(samples); output->clear(); require(!output->queued_frames(),"native audio queue clear failed");
    { auto second=nolf::platform::make_sdl_audio(); second->write(samples); } output->write(samples); require(output->queued_frames()==512,"one audio device retirement shut down another"); output->clear();
}
}
int main() { try { checks(); std::cout<<"Native SDL audio queue, initial playback, playback, pause, clear, bounds, thread and independent device lifetime passed\n"; return 0; } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
