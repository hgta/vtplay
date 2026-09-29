#include "Decoder.h"

extern "C" {
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
}

#include "AudioFrameObserver.h"

#include <cmath>
#include <cstdio>
#include <thread>

namespace vtcore {

Decoder::Decoder(Kind kind, AVCodecContext* ctx, AVStream* stream,
                 PacketQueue& in, FrameQueue& out,
                 const std::atomic<int>& targetW, const std::atomic<int>& targetH,
                 const std::atomic<int>& audioRate, const std::atomic<int>& audioChannels)
    : kind_(kind), ctx_(ctx), stream_(stream), in_(in), out_(out),
      targetW_(targetW), targetH_(targetH),
      audioRate_(audioRate), audioChannels_(audioChannels) {}

Decoder::~Decoder() {
    if (sws_) sws_freeContext(sws_);
    if (swr_) swr_free(&swr_);
}

void Decoder::setAudioObserver(AudioFrameObserver* obs) { audioObs_ = obs; }
void Decoder::requestStop()  { stop_.store(true); }
void Decoder::requestFlush() { flushPending_.store(true); }

void Decoder::handleSeekFlush() {
    avcodec_flush_buffers(ctx_);
}

void Decoder::run() {
    if (kind_ == Kind::Video) runVideo();
    else                      runAudio();
}

void Decoder::runVideo() {
    AVFrame* frame = av_frame_alloc();
    AVPacket* pkt  = av_packet_alloc();
    if (!frame || !pkt) return;

    while (!stop_.load()) {
        if (flushPending_.exchange(false)) {
            handleSeekFlush();
            continue;
        }

        AVPacket* inPkt = in_.pop();
        if (!inPkt) break;

        // flush packet (size==0) 触底通知解码器。
        bool flush = (inPkt->size == 0 && inPkt->data == nullptr);

        int ret = avcodec_send_packet(ctx_, inPkt);
        av_packet_unref(inPkt);
        av_packet_free(&inPkt);
        if (ret < 0 && ret != AVERROR_EOF) {
            // 容错：跳过坏包
            continue;
        }

        while (true) {
            ret = avcodec_receive_frame(ctx_, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) break;

            // 计算期望输出尺寸：源尺寸等比缩放到不超过 targetW_/targetH_。
            int outW = frame->width;
            int outH = frame->height;
            const int maxW = targetW_.load();
            const int maxH = targetH_.load();
            if (maxW > 0 && maxH > 0 && (outW > maxW || outH > maxH)) {
                const double s = std::min(double(maxW) / outW, double(maxH) / outH);
                outW = std::max(2, static_cast<int>(outW * s) & ~1);
                outH = std::max(2, static_cast<int>(outH * s) & ~1);
            }

            // 建立/重建 sws：源格式或输出尺寸变化时都要重建。
            const int srcFmt = frame->format;
            if (!sws_ || outW != swsW_ || outH != swsH_ || srcFmt != swsSrcFmt_) {
                if (sws_) { sws_freeContext(sws_); sws_ = nullptr; }
                swsW_ = outW;
                swsH_ = outH;
                swsDst_ = AV_PIX_FMT_RGBA;
                swsSrcFmt_ = srcFmt;

                // 缩小时必须用「面积平均」类算法：双线性只采样 2x2 源像素，
                // 在 4K→显示尺寸（数倍缩小）时会大量丢失细节并产生块状伪影
                // （肉眼即"马赛克"）。SWS_AREA 会覆盖目标像素对应的整块源区域。
                // 放大时用双线性即可（不会丢信息）。
                const bool downscaling = (swsW_ < frame->width) || (swsH_ < frame->height);
                const int flags = downscaling ? SWS_AREA : SWS_BILINEAR;

                sws_ = sws_getContext(
                    frame->width, frame->height, static_cast<AVPixelFormat>(srcFmt),
                    swsW_, swsH_, swsDst_,
                    flags, nullptr, nullptr, nullptr);
            }

            VideoFrame vf;
            vf.width = swsW_;
            vf.height = swsH_;
            vf.stride = swsW_ * 4;
            vf.rgba.resize(static_cast<size_t>(vf.stride) * vf.height);
            vf.ptsSec = (frame->best_effort_timestamp == AV_NOPTS_VALUE)
                          ? 0.0
                          : frame->best_effort_timestamp * av_q2d(stream_->time_base);
            vf.valid = (sws_ != nullptr);

            if (sws_) {
                uint8_t* dst[1] = { vf.rgba.data() };
                int dstStride[1] = { vf.stride };
                sws_scale(sws_, frame->data, frame->linesize, 0, frame->height, dst, dstStride);
                out_.pushVideo(std::move(vf));
            }
        }

        if (flush) break;
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
}

void Decoder::runAudio() {
    AVFrame* frame = av_frame_alloc();
    AVPacket* pkt  = av_packet_alloc();
    if (!frame || !pkt) return;

    while (!stop_.load()) {
        if (flushPending_.exchange(false)) {
            handleSeekFlush();
            continue;
        }

        AVPacket* inPkt = in_.pop();
        if (!inPkt) break;

        bool flush = (inPkt->size == 0 && inPkt->data == nullptr);

        int ret = avcodec_send_packet(ctx_, inPkt);
        // send_packet consumes the packet's data on success; we still need to free the struct.
        // Call unref first to be defensive in case send_packet left a dangling reference.
        av_packet_unref(inPkt);
        av_packet_free(&inPkt);
        if (ret < 0 && ret != AVERROR_EOF) continue;

        while (true) {
            ret = avcodec_receive_frame(ctx_, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) break;

            // 目标格式取自设备协商结果（默认 S16/立体声/48kHz）。
            // 设备不支持默认格式时由输出层改写，这里据此重建 swr。
            const int wantRate = audioRate_.load();
            const int wantCh   = audioChannels_.load();
            if (!swr_ || wantRate != outSampleRate_ || wantCh != outChannels_) {
                if (swr_) { swr_free(&swr_); swr_ = nullptr; }
                outSampleRate_ = wantRate;
                outChannels_   = wantCh;

                AVChannelLayout outLayout;
                av_channel_layout_default(&outLayout, outChannels_);
                AVChannelLayout inLayout;
                av_channel_layout_default(&inLayout, frame->ch_layout.nb_channels);
                int rc = swr_alloc_set_opts2(&swr_,
                    &outLayout, AV_SAMPLE_FMT_S16, outSampleRate_,
                    &inLayout,  static_cast<AVSampleFormat>(frame->format), frame->sample_rate,
                    0, nullptr);
                if (rc < 0 || !swr_ || swr_init(swr_) < 0) {
                    swr_ = nullptr;
                    break;
                }
            }

            int outSamples = av_rescale_rnd(
                swr_get_delay(swr_, frame->sample_rate) + frame->nb_samples,
                outSampleRate_, frame->sample_rate, AV_ROUND_UP);
            int bytesPerOutFrame = outChannels_ * 2 /*S16*/ * outSamples;
            AudioFrame af;
            af.pcm.resize(bytesPerOutFrame);
            af.sampleRate = outSampleRate_;
            af.channels   = outChannels_;
            af.ptsSec = (frame->best_effort_timestamp == AV_NOPTS_VALUE)
                          ? 0.0
                          : frame->best_effort_timestamp * av_q2d(stream_->time_base);
            af.valid = true;

            uint8_t* dst[1] = { af.pcm.data() };
            int converted = swr_convert(swr_, dst, outSamples,
                                        const_cast<const uint8_t**>(frame->data), frame->nb_samples);
            if (converted > 0) {
                af.pcm.resize(static_cast<size_t>(outChannels_ * 2 * converted));
                out_.pushAudio(std::move(af));
                if (audioObs_) {
                    AudioFrame copy = af; // 观察者可能异步持有
                    audioObs_->onAudioFrame(copy);
                }
            }
        }

        if (flush) break;
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
}

} // namespace vtcore