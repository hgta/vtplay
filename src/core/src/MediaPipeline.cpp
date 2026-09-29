#include "MediaPipeline.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/cpu.h>
#include <libavutil/time.h>
}

#include "Decoder.h"
#include "Demuxer.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <thread>

namespace vtcore {

MediaPipeline::MediaPipeline()  = default;
MediaPipeline::~MediaPipeline() { close(); }

MediaInfo MediaPipeline::info() const { return info_; }
double MediaPipeline::durationSec() const { return info_.durationSec; }
double MediaPipeline::positionSec() const { return clock_.nowSec(); }

void MediaPipeline::setRate(double r) {
    clock_.setRate(r);
}

void MediaPipeline::setVideoTargetSize(int w, int h) {
    targetW_.store(w > 0 ? w : 0);
    targetH_.store(h > 0 ? h : 0);
}

void MediaPipeline::setVolume(double v) {
    if (v < 0) v = 0;
    if (v > 1) v = 1;
    volume_.store(v);
}

void MediaPipeline::setStatus(PlayerStatus s) { status_.store(s); }

void MediaPipeline::open(const std::string& url) {
    // 确保 FFmpeg 全局初始化一次
    static std::once_flag initFlag;
    std::call_once(initFlag, [](){
        avformat_network_init();
    });

    close();
    setStatus(PlayerStatus::Loading);

    if (avformat_open_input(&fmt_, url.c_str(), nullptr, nullptr) < 0) {
        setStatus(PlayerStatus::Idle);
        throw std::runtime_error("vtcore::MediaPipeline::open: avformat_open_input failed");
    }
    if (avformat_find_stream_info(fmt_, nullptr) < 0) {
        avformat_close_input(&fmt_);
        setStatus(PlayerStatus::Idle);
        throw std::runtime_error("vtcore::MediaPipeline::open: avformat_find_stream_info failed");
    }

    info_.url = url;
    info_.durationSec = (fmt_->duration == AV_NOPTS_VALUE) ? 0.0
                        : static_cast<double>(fmt_->duration) / AV_TIME_BASE;

    int bestV = -1, bestA = -1;
    for (unsigned i = 0; i < fmt_->nb_streams; ++i) {
        AVStream* st = fmt_->streams[i];
        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && bestV < 0) bestV = (int)i;
        if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && bestA < 0) bestA = (int)i;
    }
    info_.hasVideo = bestV >= 0;
    info_.hasAudio = bestA >= 0;

    auto openCodec = [&](int idx, AVCodecContext*& outCtx, AVStream*& outSt,
                         std::string& codecName) -> bool {
        if (idx < 0) return false;
        AVStream* st = fmt_->streams[idx];
        const AVCodec* dec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!dec) return false;
        AVCodecContext* c = avcodec_alloc_context3(dec);
        if (avcodec_parameters_to_context(c, st->codecpar) < 0) { avcodec_free_context(&c); return false; }
        // 显式启用多线程解码：4K 软解性能的关键。
        // 默认自动检测在本环境下退化为 threads=1（单核跑满仍不实时）。
        const int cores = av_cpu_count();
        c->thread_count = cores > 1 ? std::min(cores, 8) : 1;
        c->thread_type  = FF_THREAD_FRAME | FF_THREAD_SLICE;
        if (avcodec_open2(c, dec, nullptr) < 0) { avcodec_free_context(&c); return false; }
        outCtx = c;
        outSt  = st;
        codecName = dec->name;
        return true;
    };

    if (!openCodec(bestV, vdec_, vst_, info_.videoCodec)) bestV = -1;
    if (!openCodec(bestA, adec_, ast_, info_.audioCodec)) bestA = -1;
    info_.hasVideo = vdec_ != nullptr;
    info_.hasAudio = adec_ != nullptr;

    if (vst_) {
        info_.videoWidth  = vst_->codecpar->width;
        info_.videoHeight = vst_->codecpar->height;
        AVRational fr = av_guess_frame_rate(fmt_, vst_, nullptr);
        info_.videoFrameRate = (fr.num && fr.den) ? av_q2d(fr) : 0.0;
    }

    // 复用队列对象：abort 标志与残留数据必须先复位，
    // 否则新线程立刻看到"已中止"的空队列（换文件播放无声无画面的根因）。
    videoPackets_.reset();
    audioPackets_.reset();
    frames_.reset();
    demuxDone_.store(false);

    running_.store(true);
    auto* demuxer = new Demuxer(fmt_, bestV, bestA, videoPackets_, audioPackets_);
    demuxer_ = demuxer;
    auto* vDecoder = vdec_ ? new Decoder(Decoder::Kind::Video, vdec_, vst_, videoPackets_, frames_, targetW_, targetH_) : nullptr;
    auto* aDecoder = adec_ ? new Decoder(Decoder::Kind::Audio, adec_, ast_, audioPackets_, frames_, targetW_, targetH_) : nullptr;

    demuxTh_ = std::thread([this, demuxer]() {
        demuxer->run();
        delete demuxer;
        if (this->demuxer_ == demuxer) this->demuxer_ = nullptr;
        videoPackets_.abort();
        audioPackets_.abort();
        // 解封装先于播放结束（Demuxer 读取远快于播放）。
        // 这里只记录状态，不能立刻冻结时钟/置 Eof：此时音频输出还有
        // 数秒缓冲没播完，提前冻结会让进度条早停。真正的结束由
        // 音频输出在"持续取不到数据"时经 notifyPlaybackEof() 上报。
        demuxDone_.store(true);
        if (!info_.hasAudio && running_.load()) {
            // 无音频时没有播放进度参照，直接在此判定结束。
            clock_.pause();
            if (status_.load() == PlayerStatus::Playing) setStatus(PlayerStatus::Eof);
        }
    });

    if (vDecoder) {
        videoDecTh_ = std::thread([this, vDecoder]() {
            vDecoder->run();
            delete vDecoder;
        });
    } else {
        videoPackets_.abort();
    }
    if (aDecoder) {
        audioDecTh_ = std::thread([this, aDecoder]() {
            std::shared_ptr<AudioFrameObserver> obs;
            {
                std::lock_guard<std::mutex> lk(obsMu_);
                obs = audioObs_;
            }
            aDecoder->setAudioObserver(obs.get());
            aDecoder->run();
            delete aDecoder;
        });
    } else {
        audioPackets_.abort();
    }

    clock_.start();
    setStatus(PlayerStatus::Paused); // 加载完成待用户按播放
}

void MediaPipeline::close() {
    if (!running_.exchange(false)) {
        return;
    }
    if (demuxer_) demuxer_->requestStop();

    videoPackets_.abort();
    audioPackets_.abort();
    frames_.abort();

    if (demuxTh_.joinable())    demuxTh_.join();
    if (videoDecTh_.joinable()) videoDecTh_.join();
    if (audioDecTh_.joinable()) audioDecTh_.join();

    flushAll();
    setStatus(PlayerStatus::Idle);
    clock_.reset();
}

void MediaPipeline::flushAll() {
    if (vdec_) { avcodec_free_context(&vdec_); vst_ = nullptr; }
    if (adec_) { avcodec_free_context(&adec_); ast_ = nullptr; }
    if (fmt_)  { avformat_close_input(&fmt_); }
    videoPackets_.clear();
    audioPackets_.clear();
    frames_.clear();
}

void MediaPipeline::play() {
    if (status_.load() == PlayerStatus::Idle) return;
    clock_.resume();
    setStatus(PlayerStatus::Playing);
}

void MediaPipeline::pause() {
    if (status_.load() != PlayerStatus::Playing) return;
    clock_.pause();
    setStatus(PlayerStatus::Paused);
}

void MediaPipeline::stop() {
    close();
}

void MediaPipeline::seek(double sec) {
    if (!running_.load()) return;
    if (sec < 0) sec = 0;
    if (info_.durationSec > 0 && sec > info_.durationSec) sec = info_.durationSec;

    // 时钟立刻跳到目标位置：渲染层据此判断"该显示哪一帧"。
    clock_.setAudioPts(sec);
    // 丢弃已解码的旧帧，否则 seek 后仍会播出原位置的音视频（听起来位置错乱）。
    frames_.reset();
    // 请求解封装线程清空包队列并定位；这里不阻塞、也不改变播放状态
    // （原实现在主线程 sleep 20ms 并强制置为 Paused，导致拖动进度条后播放停止）。
    if (demuxer_) demuxer_->requestSeek(sec);
}

void MediaPipeline::stepFrame() {
    if (status_.load() != PlayerStatus::Paused) return;
    // MVP 简化：仅刷新最近一帧后立即显示（实际需要按帧间隔解码一帧）。
    VideoFrame v = frames_.peekLatestVideo();
    if (v.valid) {
        frames_.pushVideo(std::move(v));
    }
}

std::variant<VideoFrame, AudioFrame> MediaPipeline::takeFrame() {
    return frames_.pop();
}

std::optional<AudioFrame> MediaPipeline::takeAudioFrame(int timeoutMs) {
    return frames_.popAudio(timeoutMs);
}

VideoFrame MediaPipeline::peekLatestVideo() {
    return frames_.peekLatestVideo();
}

std::optional<VideoFrame> MediaPipeline::takeVideoUpTo(double ptsSec, bool allowAhead) {
    return frames_.popVideoUpTo(ptsSec, allowAhead);
}

void MediaPipeline::registerAudioObserver(std::shared_ptr<AudioFrameObserver> obs) {
    std::lock_guard<std::mutex> lk(obsMu_);
    audioObs_ = std::move(obs);
}

void MediaPipeline::unregisterAudioObserver() {
    std::lock_guard<std::mutex> lk(obsMu_);
    audioObs_.reset();
}

void MediaPipeline::threadDemuxLoop()         {}
void MediaPipeline::threadVideoDecodeLoop()   {}
void MediaPipeline::threadAudioDecodeLoop()   {}

} // namespace vtcore