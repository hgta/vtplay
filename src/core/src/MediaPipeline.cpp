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
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <system_error>
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
    info_.fileName = fileNameFromPath(url);
    info_.durationSec = (fmt_->duration == AV_NOPTS_VALUE) ? 0.0
                        : static_cast<double>(fmt_->duration) / AV_TIME_BASE;
    // 文件大小仅对本地路径有意义；URL 或文件不可访问时保持 0（界面据此降级显示）。
    {
        std::error_code ec;
        const auto sz = std::filesystem::file_size(url, ec);
        if (!ec) info_.fileSizeBytes = static_cast<int64_t>(sz);
    }

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
        // 注意：FFmpeg 7+ 已移除 AVStream::bit_rate，只能取 codecpar 的值；
        // 为 0 表示容器未写入流级码率，由下方容器级估算兜底。
        info_.videoBitRate = vst_->codecpar->bit_rate;
    }
    if (ast_) {
        info_.audioSampleRate = ast_->codecpar->sample_rate;
        info_.audioChannels   = ast_->codecpar->ch_layout.nb_channels;
        info_.audioBitRate    = ast_->codecpar->bit_rate;
    }
    // MP4/MOV 通常不写流级视频码率：退化为「容器总码率 − 音频码率」的估算。
    if (info_.videoBitRate <= 0 && fmt_->bit_rate > 0) {
        const int64_t est = fmt_->bit_rate - info_.audioBitRate;
        if (est > 0) info_.videoBitRate = est;
    }

    // 复用队列对象：abort 标志与残留数据必须先复位，
    // 否则新线程立刻看到"已中止"的空队列（换文件播放无声无画面的根因）。
    videoPackets_.reset();
    audioPackets_.reset();
    frames_.reset();
    demuxDone_.store(false);
    // 逐帧状态必须随新媒体一起复位：否则上一次的显示位置会残留下来，
    // 让步进的 seek 目标落在错误的时间点上。
    lastVideoPts_.store(0.0);
    pendingStepFrame_.reset();

    running_.store(true);
    auto* demuxer = new Demuxer(fmt_, bestV, bestA, videoPackets_, audioPackets_);
    demuxer_ = demuxer;
    auto* vDecoder = vdec_ ? new Decoder(Decoder::Kind::Video, vdec_, vst_, videoPackets_, frames_, targetW_, targetH_, audioRate_, audioChannels_) : nullptr;
    auto* aDecoder = adec_ ? new Decoder(Decoder::Kind::Audio, adec_, ast_, audioPackets_, frames_, targetW_, targetH_, audioRate_, audioChannels_) : nullptr;

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
    // 清空元数据：否则界面会残留上一个文件的名称与规格。
    info_ = MediaInfo{};
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
    // 恢复播放即撤销保留窗口：队列该按正常流速推进了
    frames_.setKeepAfter(-1.0);
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
    // 逐帧预取的帧属于旧位置，必须丢弃：否则 seek 后渲染层会把它当成新帧显示
    pendingStepFrame_.reset();
    // 请求解封装线程清空包队列并定位；这里不阻塞、也不改变播放状态
    // （原实现在主线程 sleep 20ms 并强制置为 Paused，导致拖动进度条后播放停止）。
    if (demuxer_) demuxer_->requestSeek(sec);
}

double MediaPipeline::frameInterval() const {
    const double fps = info_.videoFrameRate;
    return (fps > 0.5 && fps < 1000.0) ? (1.0 / fps) : (1.0 / 25.0);
}



bool MediaPipeline::stepFrame(int direction) {
    if (status_.load() != PlayerStatus::Paused) return false;
    if (!info_.hasVideo) return false;

    const double interval = frameInterval();
    double base = lastVideoPts_.load();
    // 还没有过任何帧（刚打开/刚 seek 完）时退回时钟，至少能朝正确方向动
    if (base <= 0.0) base = positionSec();

    if (direction > 0) {
        // 前进优先走零成本路径：暂停时解码已被背压闸门挡住，紧跟显示位置的那几帧
        // 一定还在缓冲里，直接把「下一帧」预取出来交给渲染层即可，下一拍就显示。
        //
        // 预取而不用「推时钟 + 让渲染层按容差取」，是因为后者要依赖容差计算，
        // 帧落在边界上时会时而跳两帧、时而原地不动；预取给出的是确定的一帧。
        if (auto next = frames_.popVideoAfter(base)) {
            // 只在它确实是「紧邻的下一帧」时才走这条零成本路径。
            // 帧队列的视频侧会丢最旧的帧，缓冲里可能存在跳帧，
            // 此时预取会一步跨过好几帧——那就不叫逐帧了（实测会跳出 1~3 帧）。
            if (next->ptsSec - base <= interval * 1.5) {
                clock_.setAudioPts(next->ptsSec);
                pendingStepFrame_ = std::move(*next);
                return true;
            }
            // 有跳帧：丢弃这一帧退回重解（重解会重新产出它，不会丢内容）
        }
        // 目标取 1.2 个帧间隔：稳稳包住下一帧，又不会越过下下帧（1.2 < 2）。
        seek(base + interval * 1.2);
    } else {
        // 后退：解码是单向的，队列里没有已过去的帧，只能回到关键帧重解。
        // 目标取在「上一帧区间内」（-0.5 个间隔），保证取到的是上一帧而不是当前帧。
        seek(std::max(0.0, base - interval * 0.5));
    }
    return true;
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

void MediaPipeline::noteDeliveredFrame(const VideoFrame& f) {
    lastVideoPts_.store(f.ptsSec);
    // 暂停时把「用户正看着的这一帧」告知帧队列，它之后的几帧将被保护起来，
    // 供逐帧前进直接取用；播放态撤销窗口（渲染层持续消费，队列自然流动）。
    frames_.setKeepAfter(status_.load() == PlayerStatus::Paused ? f.ptsSec : -1.0);
}

std::optional<VideoFrame> MediaPipeline::takeVideoUpTo(double ptsSec, bool allowAhead) {
    // 逐帧前进预取的帧优先返回：它是确定的「下一帧」，不受时间容差影响
    if (pendingStepFrame_) {
        VideoFrame f = std::move(*pendingStepFrame_);
        pendingStepFrame_.reset();
        noteDeliveredFrame(f);
        return f;
    }
    auto v = frames_.popVideoUpTo(ptsSec, allowAhead);
    if (v && v->valid) noteDeliveredFrame(*v);
    return v;
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