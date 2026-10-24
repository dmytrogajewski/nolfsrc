#include "platform/ffmpeg/media_io.h"
extern "C" {
#include <libavutil/error.h>
}
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <string>

namespace nolf::platform::ffmpeg {
void check(int result,const char* operation) {
    if (result>=0) return;
    std::array<char,AV_ERROR_MAX_STRING_SIZE> text{}; av_strerror(result,text.data(),text.size());
    throw std::runtime_error(std::string(operation)+": "+text.data());
}
MediaInput::~MediaInput() {
    avformat_close_input(&file);
    if (io) { av_freep(&io->buffer); avio_context_free(&io); }
}
void MediaInput::checked(int result,const char* operation) { if (io_error) std::rethrow_exception(io_error); check(result,operation); }
int MediaInput::read(void* opaque,std::uint8_t* data,int length) noexcept {
    auto& self=*static_cast<MediaInput*>(opaque);
    try {
        if (length<=0) return AVERROR(EINVAL);
        if (self.position>=self.size) return AVERROR_EOF;
        const auto count=std::min<std::uint64_t>(static_cast<unsigned>(length),self.size-self.position);
        self.source->read(self.position,std::span(reinterpret_cast<std::byte*>(data),std::size_t(count))); self.position+=count; return static_cast<int>(count);
    } catch (...) { self.io_error=std::current_exception(); return AVERROR(EIO); }
}
std::int64_t MediaInput::seek(void* opaque,std::int64_t offset,int origin) noexcept {
    auto& self=*static_cast<MediaInput*>(opaque);
    if (origin&AVSEEK_SIZE) return static_cast<std::int64_t>(self.size);
    origin&=~AVSEEK_FORCE;
    const auto base=origin==SEEK_SET ? 0u : origin==SEEK_CUR ? self.position : self.size;
    if (origin!=SEEK_SET && origin!=SEEK_CUR && origin!=SEEK_END) return AVERROR(EINVAL);
    std::uint64_t target=0;
    if (offset<0) { const auto back=std::uint64_t(-(offset+1))+1; if (back>base) return AVERROR(EINVAL); target=base-back; }
    else { if (std::uint64_t(offset)>self.size-base) return AVERROR(EINVAL); target=base+std::uint64_t(offset); }
    self.position=target; return static_cast<std::int64_t>(target);
}
void MediaInput::open(std::shared_ptr<const MediaSource> input) {
    if (source || !input) throw std::invalid_argument("invalid native media source");
    source=std::move(input); size=source->size();
    if (!size || size>std::uint64_t(std::numeric_limits<std::int64_t>::max())) throw std::invalid_argument("invalid native media extent");
    auto* buffer=static_cast<std::uint8_t*>(av_malloc(32768)); if (!buffer) throw std::bad_alloc();
    io=avio_alloc_context(buffer,32768,0,this,read,nullptr,seek); if (!io) { av_free(buffer); throw std::bad_alloc(); }
    io->seekable=AVIO_SEEKABLE_NORMAL; file=avformat_alloc_context(); if (!file) throw std::bad_alloc(); file->pb=io; file->flags|=AVFMT_FLAG_CUSTOM_IO;
    file->io_open=+[](AVFormatContext*,AVIOContext**,const char*,int,AVDictionary**) -> int { return AVERROR(EPERM); };
    file->probesize=1024*1024; file->max_analyze_duration=5*AV_TIME_BASE;
    checked(avformat_open_input(&file,nullptr,nullptr,nullptr),"open native media container"); checked(avformat_find_stream_info(file,nullptr),"read native media headers");
}
}
