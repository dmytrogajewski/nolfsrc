#include "platform/audio.h"
#include "platform/ffmpeg/media_io.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libswresample/swresample.h>
}
#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace nolf::platform {
namespace {
using ffmpeg::check;
struct Session : ffmpeg::MediaInput {
    AVCodecContext* codec=nullptr;
    AVPacket* packet=nullptr;
    AVFrame* frame=nullptr;
    SwrContext* resampler=nullptr;
    int stream=-1;
    bool demux_eof=false,flush_sent=false,decoder_eof=false,drained=false;
    ~Session() {
        swr_free(&resampler); av_frame_free(&frame); av_packet_free(&packet); avcodec_free_context(&codec);
    }
};
class FfmpegAudio final : public AudioDecoder {
    const std::thread::id owner_=std::this_thread::get_id();
    std::unique_ptr<Session> session_=std::make_unique<Session>();
    AudioFormat format_;
    std::optional<double> duration_;
    std::optional<std::uint64_t> source_limit_;
    std::uint64_t source_frames_=0;
    std::vector<float> pending_;
    std::size_t offset_=0;
    void thread() const { if (owner_!=std::this_thread::get_id()) throw std::logic_error("audio decoder used from another thread"); }
    void convert(bool flush) {
        auto& s=*session_; const auto* frame=s.frame;
        if (!s.resampler) {
            if (flush) { s.drained=true; return; }
            if (frame->sample_rate!=s.codec->sample_rate || av_channel_layout_compare(&frame->ch_layout,&s.codec->ch_layout)!=0 || frame->format!=s.codec->sample_fmt) throw std::runtime_error("audio format changed within stream");
            AVChannelLayout layout{}; av_channel_layout_default(&layout,static_cast<int>(format_.channels));
            const auto result=swr_alloc_set_opts2(&s.resampler,&layout,AV_SAMPLE_FMT_FLT,static_cast<int>(format_.rate),&s.codec->ch_layout,s.codec->sample_fmt,s.codec->sample_rate,0,nullptr); av_channel_layout_uninit(&layout);
            check(result,"configure native audio resampler"); check(swr_init(s.resampler),"initialize native audio resampler");
        }
        const auto samples=flush ? 0 : frame->nb_samples; if (samples<0 || samples>1048576) throw std::runtime_error("audio frame exceeds sample budget");
        const auto capacity=swr_get_out_samples(s.resampler,samples); check(capacity,"measure native audio output"); if (capacity>1048576) throw std::runtime_error("audio resampling exceeds frame budget");
        pending_.resize(std::size_t(std::max(capacity,1))*format_.channels); std::uint8_t* output=reinterpret_cast<std::uint8_t*>(pending_.data());
        const auto count=swr_convert(s.resampler,&output,std::max(capacity,1),flush ? nullptr : const_cast<const std::uint8_t**>(frame->extended_data),samples); check(count,"resample native audio");
        pending_.resize(std::size_t(count)*format_.channels); offset_=0;
        if (flush && !count) s.drained=true;
        if (std::any_of(pending_.begin(),pending_.end(),[](float value) { return !std::isfinite(value); })) throw std::runtime_error("audio contains nonfinite samples");
    }
    bool pump() {
        auto& s=*session_; pending_.clear(); offset_=0;
        unsigned packets=0;
        for (unsigned step=0;step<65536;++step) {
            if (s.drained) return false;
            if (s.decoder_eof) { convert(true); if (!pending_.empty()) return true; continue; }
            const auto result=avcodec_receive_frame(s.codec,s.frame);
            if (result==0) {
                if (s.frame->nb_samples<0) throw std::runtime_error("invalid decoded sample count");
                if (source_limit_) {
                    s.frame->nb_samples=static_cast<int>(std::min<std::uint64_t>(static_cast<unsigned>(s.frame->nb_samples),*source_limit_-source_frames_));
                    source_frames_+=static_cast<unsigned>(s.frame->nb_samples);
                    if (source_frames_==*source_limit_) s.decoder_eof=true;
                }
                if (s.frame->nb_samples) convert(false);
                av_frame_unref(s.frame); if (!pending_.empty()) return true; continue;
            }
            if (result==AVERROR_EOF) { s.decoder_eof=true; continue; }
            check(result==AVERROR(EAGAIN) ? 0 : result,"decode native audio frame");
            if (s.demux_eof) {
                if (s.flush_sent) throw std::runtime_error("audio decoder stalled after flush");
                s.checked(avcodec_send_packet(s.codec,nullptr),"flush native audio decoder"); s.flush_sent=true; continue;
            }
            while (true) {
                if (++packets>65536) throw std::runtime_error("audio demux packet budget exceeded");
                const auto packet=av_read_frame(s.file,s.packet);
                if (packet==AVERROR_EOF) { s.demux_eof=true; break; }
                s.checked(packet,"read native audio packet");
                if (s.packet->stream_index==s.stream) {
                    const auto sent=avcodec_send_packet(s.codec,s.packet); av_packet_unref(s.packet); s.checked(sent,"submit native audio packet"); break;
                }
                av_packet_unref(s.packet);
            }
        }
        throw std::runtime_error("audio packet processing budget exceeded");
    }
public:
    FfmpegAudio(std::shared_ptr<const MediaSource> source,AudioFormat format,std::optional<std::uint64_t> source_frames) : format_(format),source_limit_(source_frames) {
        if (!source || format.rate<8000 || format.rate>192000 || !format.channels || format.channels>2) throw std::invalid_argument("invalid native audio decoder request");
        auto& s=*session_; s.open(std::move(source));
        const AVCodec* decoder=nullptr; s.stream=av_find_best_stream(s.file,AVMEDIA_TYPE_AUDIO,-1,-1,&decoder,0); check(s.stream,"find native audio stream");
        s.codec=avcodec_alloc_context3(decoder); if (!s.codec) throw std::bad_alloc(); check(avcodec_parameters_to_context(s.codec,s.file->streams[s.stream]->codecpar),"configure native audio codec");
        if (s.codec->sample_rate<1000 || s.codec->sample_rate>192000 || s.codec->ch_layout.nb_channels<1 || s.codec->ch_layout.nb_channels>8) throw std::runtime_error("audio input format exceeds limits");
        s.codec->max_samples=1048576; s.codec->thread_count=1; check(avcodec_open2(s.codec,decoder,nullptr),"open native audio codec");
        s.packet=av_packet_alloc(); s.frame=av_frame_alloc(); if (!s.packet || !s.frame) throw std::bad_alloc();
        const auto* stream=s.file->streams[s.stream];
        if (stream->duration!=AV_NOPTS_VALUE && stream->duration>=0) duration_=double(stream->duration)*av_q2d(stream->time_base);
        else if (s.file->duration!=AV_NOPTS_VALUE && s.file->duration>=0) duration_=double(s.file->duration)/AV_TIME_BASE;
        if (source_limit_) duration_=double(*source_limit_)/s.codec->sample_rate;
    }
    AudioFormat format() const override { thread(); return format_; }
    std::optional<double> duration() const override { thread(); return duration_; }
    std::size_t read(std::span<float> output) override {
        thread(); if (output.size()%format_.channels) throw std::invalid_argument("partial audio output frame"); std::size_t done=0;
        while (done<output.size()) {
            if (offset_==pending_.size() && !pump()) break;
            const auto count=std::min(output.size()-done,pending_.size()-offset_); std::copy_n(pending_.data()+offset_,count,output.data()+done); offset_+=count; done+=count;
        }
        return done/format_.channels;
    }
    void seek(std::uint64_t frame) override {
        thread(); auto& s=*session_; const auto* stream=s.file->streams[s.stream];
        s.checked(av_seek_frame(s.file,s.stream,stream->start_time==AV_NOPTS_VALUE ? 0 : stream->start_time,AVSEEK_FLAG_BACKWARD),"rewind native audio stream");
        avcodec_flush_buffers(s.codec); swr_free(&s.resampler); av_packet_unref(s.packet); av_frame_unref(s.frame); pending_.clear(); offset_=0;
        s.demux_eof=s.flush_sent=s.decoder_eof=s.drained=false;
        source_frames_=0;
        // Decode from the beginning so compressed block alignment and encoder
        // delay never change the exact sample selected for a loop or seek.
        std::array<float,4096> skip{};
        while (frame) { const auto count=std::min<std::uint64_t>(frame,skip.size()/format_.channels); const auto got=read(std::span(skip).first(std::size_t(count)*format_.channels)); if (!got) throw std::out_of_range("audio seek exceeds decoded extent"); frame-=got; }
    }
};
}
std::unique_ptr<AudioDecoder> make_ffmpeg_audio(std::shared_ptr<const MediaSource> source,AudioFormat format,std::optional<std::uint64_t> source_frames) { return std::make_unique<FfmpegAudio>(std::move(source),format,source_frames); }
}
