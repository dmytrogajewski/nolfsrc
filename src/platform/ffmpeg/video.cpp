#include "platform/video.h"
#include "platform/ffmpeg/media_io.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}
#include <cmath>
#include <limits>
#include <stdexcept>
#include <thread>

namespace nolf::platform {
namespace {
using ffmpeg::check;
struct Session : ffmpeg::MediaInput {
    AVCodecContext* codec=nullptr;
    AVPacket* packet=nullptr;
    AVFrame* frame=nullptr;
    SwsContext* scaler=nullptr;
    int stream=-1;
    bool eof=false,flushed=false,done=false;
    ~Session() { sws_freeContext(scaler); av_frame_free(&frame); av_packet_free(&packet); avcodec_free_context(&codec); }
};
class FfmpegVideo final : public VideoDecoder {
    const std::thread::id owner_=std::this_thread::get_id();
    std::unique_ptr<Session> session_=std::make_unique<Session>();
    VideoInfo info_;
    std::uint64_t frame_=0;
    double last_time_=-1;
    void thread() const { if (owner_!=std::this_thread::get_id()) throw std::logic_error("video decoder used from another thread"); }
    VideoFrame convert() {
        auto& s=*session_; const auto* input=s.frame;
        if (input->width!=int(info_.width) || input->height!=int(info_.height) || input->format<0) throw std::runtime_error("video frame format changed");
        // The caller keeps a previous frame while decoding its successor.
        VideoFrame output; output.width=info_.width; output.height=info_.height;
        output.duration=1/info_.frame_rate;
        const auto* stream=s.file->streams[s.stream]; const auto base=stream->start_time==AV_NOPTS_VALUE ? 0 : stream->start_time;
        output.time=input->best_effort_timestamp==AV_NOPTS_VALUE ? double(frame_)/info_.frame_rate : (double(input->best_effort_timestamp)-double(base))*av_q2d(stream->time_base);
#if LIBAVUTIL_VERSION_MAJOR >= 58
        const auto ticks=input->duration;
#else
        const auto ticks=input->pkt_duration;
#endif
        if (ticks>0) output.duration=double(ticks)*av_q2d(stream->time_base);
        if (!std::isfinite(output.time) || !std::isfinite(output.duration) || output.time<0 || output.duration<=0 || output.duration>60 || output.time<last_time_ || output.time>86400) throw std::runtime_error("invalid video timestamp");
        s.scaler=sws_getCachedContext(s.scaler,input->width,input->height,static_cast<AVPixelFormat>(input->format),input->width,input->height,AV_PIX_FMT_RGBA,SWS_POINT,nullptr,nullptr,nullptr);
        if (!s.scaler) throw std::runtime_error("native video pixel conversion unavailable");
        output.rgba.resize(std::size_t(info_.width)*info_.height*4); std::uint8_t* pixels[]={reinterpret_cast<std::uint8_t*>(output.rgba.data()),nullptr,nullptr,nullptr};
        int stride[]={int(info_.width*4),0,0,0};
        const auto rows=sws_scale(s.scaler,input->data,input->linesize,0,input->height,pixels,stride);
        if (rows!=int(info_.height)) throw std::runtime_error("incomplete native video pixel conversion");
        last_time_=output.time; ++frame_; return output;
    }
public:
    explicit FfmpegVideo(std::shared_ptr<const MediaSource> source) {
        auto& s=*session_; s.open(std::move(source));
        const AVCodec* decoder=nullptr; s.stream=av_find_best_stream(s.file,AVMEDIA_TYPE_VIDEO,-1,-1,&decoder,0); check(s.stream,"find native video stream");
        s.codec=avcodec_alloc_context3(decoder); if (!s.codec) throw std::bad_alloc(); check(avcodec_parameters_to_context(s.codec,s.file->streams[s.stream]->codecpar),"configure native video codec");
        const auto width=s.codec->width,height=s.codec->height;
        if (width<=0 || height<=0 || width>4096 || height>4096 || std::uint64_t(width)*height>16*1024*1024) throw std::runtime_error("video dimensions exceed frame budget");
        s.codec->max_pixels=16*1024*1024; s.codec->thread_count=1; check(avcodec_open2(s.codec,decoder,nullptr),"open native video codec");
        s.packet=av_packet_alloc(); s.frame=av_frame_alloc(); if (!s.packet || !s.frame) throw std::bad_alloc();
        auto* stream=s.file->streams[s.stream]; const auto rate=av_guess_frame_rate(s.file,stream,nullptr); info_.width=unsigned(width); info_.height=unsigned(height); info_.frame_rate=rate.num>0 && rate.den>0 ? av_q2d(rate) : 30;
        if (!std::isfinite(info_.frame_rate) || info_.frame_rate<.1 || info_.frame_rate>240) throw std::runtime_error("video frame rate exceeds limits");
        if (stream->nb_frames>0) info_.frames=static_cast<std::uint64_t>(stream->nb_frames);
        if (stream->duration!=AV_NOPTS_VALUE && stream->duration>=0) info_.duration=double(stream->duration)*av_q2d(stream->time_base);
        else if (s.file->duration!=AV_NOPTS_VALUE && s.file->duration>=0) info_.duration=double(s.file->duration)/AV_TIME_BASE;
        info_.audio=av_find_best_stream(s.file,AVMEDIA_TYPE_AUDIO,-1,-1,nullptr,0)>=0;
    }
    VideoInfo info() const override { thread(); return info_; }
    std::optional<VideoFrame> next() override {
        thread(); auto& s=*session_; if (s.done) return {};
        unsigned packets=0;
        for (unsigned step=0;step<65536;++step) {
            const auto received=avcodec_receive_frame(s.codec,s.frame);
            if (received==0) { auto output=convert(); av_frame_unref(s.frame); return output; }
            if (received==AVERROR_EOF) { s.done=true; return {}; }
            check(received==AVERROR(EAGAIN) ? 0 : received,"decode native video frame");
            if (s.eof) {
                if (s.flushed) throw std::runtime_error("native video decoder stalled after flush");
                s.checked(avcodec_send_packet(s.codec,nullptr),"flush native video decoder"); s.flushed=true; continue;
            }
            while (true) {
                if (++packets>65536) throw std::runtime_error("video demux packet budget exceeded");
                const auto result=av_read_frame(s.file,s.packet); if (result==AVERROR_EOF) { s.eof=true; break; }
                s.checked(result,"read native video packet");
                if (s.packet->stream_index==s.stream) { const auto sent=avcodec_send_packet(s.codec,s.packet); av_packet_unref(s.packet); s.checked(sent,"submit native video packet"); break; }
                av_packet_unref(s.packet);
            }
        }
        throw std::runtime_error("video packet processing budget exceeded");
    }
    void seek(std::uint64_t target) override {
        thread(); auto& s=*session_; auto* stream=s.file->streams[s.stream];
        s.checked(av_seek_frame(s.file,s.stream,stream->start_time==AV_NOPTS_VALUE ? 0 : stream->start_time,AVSEEK_FLAG_BACKWARD),"rewind native video");
        avcodec_flush_buffers(s.codec); av_frame_unref(s.frame); av_packet_unref(s.packet); s.eof=s.flushed=s.done=false; frame_=0; last_time_=-1;
        while (target--) if (!next()) throw std::out_of_range("video seek exceeds frame extent");
    }
};
}
std::unique_ptr<VideoDecoder> make_ffmpeg_video(std::shared_ptr<const MediaSource> source) { return std::make_unique<FfmpegVideo>(std::move(source)); }
}
