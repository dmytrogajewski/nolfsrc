#pragma once
#include "platform/audio.h"
extern "C" {
#include <libavformat/avformat.h>
}
#include <exception>

namespace nolf::platform::ffmpeg {
void check(int,const char*);
// Shared archive-only demux input. Construct before calling open so every
// partially initialized FFmpeg allocation is covered by destruction.
struct MediaInput {
    std::shared_ptr<const MediaSource> source;
    std::uint64_t size=0,position=0;
    std::exception_ptr io_error;
    AVIOContext* io=nullptr;
    AVFormatContext* file=nullptr;
    MediaInput()=default;
    ~MediaInput();
    MediaInput(const MediaInput&)=delete;
    MediaInput& operator=(const MediaInput&)=delete;
    void open(std::shared_ptr<const MediaSource>);
    void checked(int,const char*);
private:
    static int read(void*,std::uint8_t*,int) noexcept;
    static std::int64_t seek(void*,std::int64_t,int) noexcept;
};
}
