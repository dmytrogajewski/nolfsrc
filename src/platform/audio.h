#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>

namespace nolf::platform {
// A decoder can read an archive extent without extracting it to a host path.
class MediaSource {
public:
    virtual ~MediaSource()=default;
    virtual std::uint64_t size() const=0;
    virtual void read(std::uint64_t offset,std::span<std::byte>) const=0;
};
struct AudioFormat {
    unsigned rate=48000,channels=2;
    bool operator==(const AudioFormat&) const=default;
};
// Interleaved native floats. A read returns complete frames, zero at EOF.
// Decoder methods and destruction belong to the creating thread.
class AudioDecoder {
public:
    virtual ~AudioDecoder()=default;
    virtual AudioFormat format() const=0;
    virtual std::optional<double> duration() const=0;
    virtual std::size_t read(std::span<float>)=0;
    virtual void seek(std::uint64_t frame)=0;
};
// A source-frame limit excludes compressed block padding before resampling.
std::unique_ptr<AudioDecoder> make_ffmpeg_audio(std::shared_ptr<const MediaSource>,AudioFormat = {},std::optional<std::uint64_t> source_frames = {});
using AudioDecoderFactory=std::function<std::unique_ptr<AudioDecoder>(std::shared_ptr<const MediaSource>,AudioFormat,std::optional<std::uint64_t>)>;

// Playback owns an independent audio-subsystem lease; it can outlive a window.
// Operations and destruction belong to the creating thread. A new output plays
// what is written until it is paused.
class AudioOutput {
public:
    virtual ~AudioOutput()=default;
    virtual AudioFormat format() const=0;
    virtual void write(std::span<const float>)=0;
    virtual std::size_t queued_frames() const=0;
    virtual void pause(bool)=0;
    virtual void clear()=0;
};
std::shared_ptr<AudioOutput> make_sdl_audio(AudioFormat = {});
using AudioOutputFactory=std::function<std::shared_ptr<AudioOutput>(AudioFormat)>;
}
