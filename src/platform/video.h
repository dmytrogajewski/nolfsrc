#pragma once
#include "platform/audio.h"
#include <vector>

namespace nolf::platform {
struct VideoInfo {
    unsigned width=0,height=0;
    double frame_rate=0;
    std::optional<double> duration;
    std::optional<std::uint64_t> frames;
    bool audio=false;
};
struct VideoFrame {
    unsigned width=0,height=0;
    double time=0,duration=0;
    std::vector<std::byte> rgba;
};
class VideoDecoder {
public:
    virtual ~VideoDecoder()=default;
    virtual VideoInfo info() const=0;
    virtual std::optional<VideoFrame> next()=0;
    // Exact source-frame selection, including stateful palette/delta codecs.
    virtual void seek(std::uint64_t frame)=0;
};
std::unique_ptr<VideoDecoder> make_ffmpeg_video(std::shared_ptr<const MediaSource>);
using VideoDecoderFactory=std::function<std::unique_ptr<VideoDecoder>(std::shared_ptr<const MediaSource>)>;
}
