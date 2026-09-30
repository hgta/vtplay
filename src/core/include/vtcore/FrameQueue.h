#pragma once

#include "AudioFrame.h"
#include "VideoFrame.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <queue>
#include <variant>

namespace vtcore {

/// 统一帧队列（视频/音频）。生产者：解码线程；消费者：输出/同步层。
class FrameQueue {
public:
    explicit FrameQueue(size_t capacity = 8);
    ~FrameQueue() = default;

    FrameQueue(const FrameQueue&) = delete;
    FrameQueue& operator=(const FrameQueue&) = delete;

    void pushVideo(VideoFrame f);
    void pushAudio(AudioFrame f);

    /// 取下一帧（按时间戳优先返回最早的音频或视频）。无帧时阻塞；abort 时返回无效帧。
    /// 返回的 variant 索引：0=video,1=audio。
    std::variant<VideoFrame, AudioFrame> pop();

    /// 只取最早的一帧音频（视频节点保留给渲染层，不被消费）。
    /// timeoutMs < 0 表示无限等待；超时或队列中止且无音频时返回 nullopt。
    std::optional<AudioFrame> popAudio(int timeoutMs = -1);

    /// 按媒体时间取视频帧：返回 PTS <= ptsSec 中最早的一帧（已就绪、该显示了）。
    /// allowAhead=true 时（结尾 drain 阶段）若没有到点的帧，退化为取最早一帧。
    /// 没有可取帧时返回 nullopt（调用方保持当前画面）。
    std::optional<VideoFrame> popVideoUpTo(double ptsSec, bool allowAhead = false);

    /// 不阻塞地获取最近一帧视频（用于暂停帧保持/拖拽预览降级）；无则无效帧。
    VideoFrame peekLatestVideo();

    /// 取 PTS **严格大于** pts 的最早一帧视频（逐帧前进用：精确取「下一帧」）。
    /// 不丢弃其它帧；没有时返回 nullopt。
    std::optional<VideoFrame> popVideoAfter(double pts);

    /// PTS 大于 pts 的视频帧数量。解码背压据此判断「显示位置之后还缓存着几帧」。
    size_t videoCountAfter(double pts) const;

    void clear();
    void abort();

    /// 复位到可用状态：清空残留帧并解除 abort 标志（打开新媒体时复用队列）。
    void reset();

    bool isAborted() const { return aborted_; }

private:
    /// 丢弃最旧的一个指定类型节点（溢出控制）。调用方须持有 mu_。
    void dropOldest(bool wantVideo);
    size_t videoCountLocked() const;
    size_t audioCountLocked() const;

    struct Node {
        double pts;
        bool   isVideo;
        VideoFrame vf;
        AudioFrame af;
    };

    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Node> q_;
    size_t capacity_;
    /// 视频节点上限：超出丢最旧视频帧（丢帧优于延迟，4K 下也要控内存）。
    size_t videoCapacity_;
    /// 音频节点上限：超出阻塞生产者（音频绝不可丢，否则破音）。
    /// 约 2 秒缓冲（96 帧 × 21ms）：太小会在解码抖动时造成欠载断音。
    size_t audioCapacity_ = 96;
    bool aborted_ = false;
};

} // namespace vtcore