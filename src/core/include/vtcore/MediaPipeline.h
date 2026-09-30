#pragma once

#include "AudioFrameObserver.h"
#include "Clock.h"
#include "FrameQueue.h"
#include "MediaInfo.h"
#include "PacketQueue.h"
#include "PlayerState.h"

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>

struct AVFormatContext;
struct AVCodecContext;
struct AVStream;
#include "Demuxer.h"

namespace vtcore {

class Demuxer;
class Decoder;

/// 自研媒体管线。不依赖 Qt UI，可单独 headless 自测。
class MediaPipeline {
public:
    MediaPipeline();
    ~MediaPipeline();

    MediaPipeline(const MediaPipeline&) = delete;
    MediaPipeline& operator=(const MediaPipeline&) = delete;

    // ---- 生命周期 ----
    /// 打开文件并启动 demux + decode 线程。失败抛 std::runtime_error。
    void open(const std::string& url);

    /// 异步停止：唤醒所有阻塞线程、释放资源。多次调用安全。
    void close();

    // ---- 状态 ----
    PlayerStatus status() const { return status_; }
    MediaInfo    info() const;
    double      positionSec() const;
    double      durationSec() const;
    double      rate() const { return clock_.rate(); }
    void        setRate(double r);
    double      volume() const { return volume_; }
    void        setVolume(double v); // 0.0–1.0

    // ---- 帧访问（消费） ----
    /// 阻塞取下一帧（视频或音频）。abort/close 后返回无效帧。
    std::variant<VideoFrame, AudioFrame> takeFrame();

    /// 取下一帧音频（视频节点保留给渲染层）。timeoutMs<0 无限等待；
    /// 超时或队列中止且无音频时返回 nullopt。
    std::optional<AudioFrame> takeAudioFrame(int timeoutMs = -1);

    /// 不阻塞地获取最近一帧视频（暂停帧保持/拖拽预览）。
    VideoFrame peekLatestVideo();

    /// 按媒体时间取应显示的视频帧（音视频同步的关键）：
    /// 返回 PTS <= ptsSec 中最早的一帧并移除；无到点帧时返回 nullopt。
    std::optional<VideoFrame> takeVideoUpTo(double ptsSec, bool allowAhead = false);

    /// 设置音频输出格式（需与音频设备实际支持的格式一致）。
    /// 默认 S16 / 立体声 / 48kHz；设备不支持时由输出层协商后写入。
    void setAudioFormat(int sampleRate, int channels) {
        if (sampleRate > 0) audioRate_.store(sampleRate);
        if (channels   > 0) audioChannels_.store(channels);
    }

    /// 设置视频解码输出尺寸上限（0 表示按源尺寸输出）。
    /// UI 层把渲染区尺寸告知管线，解码时就缩放到可用尺寸，
    /// 避免 4K 源在高分屏上做无谓的大帧转换/上传（性能关键路径）。
    void setVideoTargetSize(int w, int h);

    // ---- 控制 ----
    void play();
    void pause();
    void stop();

    /// seek 到目标时间（秒）。异步；执行后管线可能短暂进入 Loading。
    void seek(double sec);

    /// 暂停状态下按帧步进：direction > 0 前进一帧，< 0 后退一帧。
    /// 返回是否真的发生了步进（非暂停、或无视频轨道时返回 false）。
    ///
    /// 两个方向代价不同，因为解码是单向的：
    ///   前进——解码线程本来就在跑，帧队列里已有未来的帧，只需把时钟推过下一帧，
    ///         渲染层下一拍就会显示它（无需 seek，代价接近零）；
    ///   后退——队列里没有已过去的帧，只能 seek 回关键帧再解码到目标，
    ///         因此耗时取决于关键帧间隔（GOP 越长越慢）。
    bool stepFrame(int direction);

    // ---- AI 字幕预留 ----
    void registerAudioObserver(std::shared_ptr<AudioFrameObserver> obs);
    void unregisterAudioObserver();

    /// 外部音频输出层把已播放字节数反馈给主时钟。
    void onAudioBytesPlayed(long long bytesPlayed, int sampleRate, int channels) {
        clock_.onAudioBytesPlayed(bytesPlayed, sampleRate, channels);
    }

    /// 外部音频输出层上报"当前已送出的绝对媒体时间"（秒），作为主时钟。
    void onAudioPosition(double sec) {
        clock_.setAudioPts(sec);
    }

    /// 解封装是否已读到媒体末尾（此时音频/视频仍有缓冲待播）。
    bool isDemuxDone() const { return demuxDone_.load(); }

    /// 音频输出层在"持续取不到音频数据"时调用，判定播放真正结束：
    /// 冻结时钟并置 Eof。仅在已读到末尾时生效。
    void notifyPlaybackEof() {
        if (!demuxDone_.load()) return;
        if (!running_.load()) return;
        clock_.pause();
        if (status_.load() == PlayerStatus::Playing) setStatus(PlayerStatus::Eof);
    }

private:
    void threadDemuxLoop();
    void threadVideoDecodeLoop();
    void threadAudioDecodeLoop();

    void flushAll();
    void setStatus(PlayerStatus s);

    /// 单帧时长（秒）。帧率不可信时按 25fps 兜底：宁可步进幅度略偏，
    /// 也不要原地不动——后者会让用户以为功能坏了。
    double frameInterval() const;

    // FFmpeg 资源
    AVFormatContext* fmt_ = nullptr;
    AVCodecContext*  vdec_ = nullptr;
    AVCodecContext*  adec_ = nullptr;
    AVStream*        vst_ = nullptr;
    AVStream*        ast_ = nullptr;

    // demuxer 由 MediaPipeline 拥有
    class Demuxer* demuxer_ = nullptr;

    // demux / decode 队列
    // 包队列给足缓冲。demux 是单线程顺序读取，任一队列填满都会阻塞整个
    // 读取循环（进而让另一条流断供）。4K60 下视频包约 110KB/包，
    // 且音频消费速度恒定为实时，所以两侧都要留足余量：
    //   视频包 256 × ~110KB ≈ 28MB（约 4 秒）
    //   音频包 512 × ~5KB   ≈ 2.5MB（约 12 秒）
    PacketQueue videoPackets_{256};
    PacketQueue audioPackets_{512};
    // 帧队列：视频容量 = 24/2 = 12 帧（按显示尺寸解码后每帧约 2-3MB），
    // 队列满时阻塞解码器而非丢帧（见 FrameQueue::pushVideo 说明）。
    // 音频容量 ~2 秒（96 帧 × 21ms），避免音频欠载造成断音。
    FrameQueue  frames_{24};

    // 线程
    std::thread demuxTh_;
    std::thread videoDecTh_;
    std::thread audioDecTh_;

    // 状态
    std::atomic<PlayerStatus> status_{PlayerStatus::Idle};
    MediaInfo info_{};
    AudioMasterClock clock_;
    std::atomic<double> volume_{1.0};
    std::atomic<bool> seekPending_{false};
    std::atomic<double> seekTarget_{0.0};

    // 速率与倍速
    std::atomic<double> videoTimeBase_{0.0}; // 视频流的 time_base

    // AI 字幕观察者
    std::shared_ptr<AudioFrameObserver> audioObs_;
    std::mutex obsMu_;

    // 生命周期
    std::atomic<bool> running_{false};
    /// 解封装线程是否已读到末尾（播放结束的判定前置条件）。
    std::atomic<bool> demuxDone_{false};
    /// 视频解码输出尺寸上限（0=按源尺寸）。解码线程读取，UI 线程写入。
    std::atomic<int> targetW_{0};
    std::atomic<int> targetH_{0};
    /// 音频输出格式（决定 libswresample 的重采样目标）。
    std::atomic<int> audioRate_{48000};
    std::atomic<int> audioChannels_{2};
    /// 最近一次交给渲染层的视频帧 PTS。逐帧步进以它为基准，而不是用时钟：
    /// 时钟可能停在两帧之间的任意位置，用它加减一帧会时而跳两帧、时而原地不动。
    std::atomic<double> lastVideoPts_{0.0};
};

} // namespace vtcore