#pragma once
#include "engine/portable/binary.h"
#include "platform/filesystem.h"
#include "platform/system.h"
namespace audio_clip_fixture {
inline void four(nolf::BinaryWriter& output,const char* id) { output.bytes(std::as_bytes(std::span(id,4))); }
struct Files {
    const std::filesystem::path root=std::filesystem::temp_directory_path()/("nolf-audio-clips-"+std::to_string(nolf::platform::monotonic_nanoseconds()));
    Files() {
        nolf::BinaryWriter body; four(body,"WAVE"); four(body,"fmt "); body.integer<std::uint32_t>(16); body.integer<std::uint16_t>(1); body.integer<std::uint16_t>(1); body.integer<std::uint32_t>(8000); body.integer<std::uint32_t>(16000); body.integer<std::uint16_t>(2); body.integer<std::uint16_t>(16); four(body,"data"); body.integer<std::uint32_t>(1024);
        for (unsigned i=0;i<512;++i) body.integer<std::uint16_t>(std::uint16_t(i*32));
        nolf::BinaryWriter wave; four(wave,"RIFF"); wave.integer<std::uint32_t>(body.data().size()); wave.bytes(body.data());
        for (const auto* name : {"a.wav","b.wav","c.wav","d.wav"}) nolf::platform::atomic_write(root/name,wave.data());
    }
    ~Files() { std::error_code error; std::filesystem::remove_all(root,error); }
};
}
