#include "FrameQueue.h"

#include <cassert>
#include <chrono>

namespace vtcore {

FrameQueue::FrameQueue(size_t capacity)
    : capacity_(capacity), videoCapacity_(capacity / 2 > 0 ? capacity / 2 : 1) {}

// 丢弃最旧的一个指定类型节点（溢出控制）。调用方须持有 mu_。
void FrameQueue::dropOldest(bool wantVideo) {
    for (auto it = q_.begin(); it != q_.end(); ++it) {
        if (it->isVideo == wantVideo) {
            q_.erase(it);
            return;
        }
    }
}

// 视频帧：允许丢弃（丢帧优于延迟），只限制视频节点数量。
void FrameQueue::pushVideo(VideoFrame f) {
    Node n;
    n.pts = f.ptsSec;
    n.isVideo = true;
    n.vf = std::move(f);
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (aborted_) return;
        q_.push_back(std::move(n));
        while (videoCountLocked() > videoCapacity_) dropOldest(true);
    }
    cv_.notify_all();
}

// 音频帧：绝不丢弃（丢帧会造成破音/跳音），改为背压阻塞生产者。
void FrameQueue::pushAudio(AudioFrame f) {
    Node n;
    n.pts = f.ptsSec;
    n.isVideo = false;
    n.af = std::move(f);
    {
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait(lk, [&]{ return aborted_ || audioCountLocked() < audioCapacity_; });
        if (aborted_) return;
        q_.push_back(std::move(n));
    }
    cv_.notify_all();
}

std::optional<VideoFrame> FrameQueue::popVideoUpTo(double ptsSec, bool allowAhead) {
    std::unique_lock<std::mutex> lk(mu_);

    // 找 PTS <= ptsSec 的最早视频帧（视频队列未排序，需遍历）。
    size_t best = q_.size();
    double bestPts = 0.0;
    size_t oldest = q_.size();
    double oldestPts = 0.0;
    for (size_t i = 0; i < q_.size(); ++i) {
        if (!q_[i].isVideo) continue;
        if (oldest == q_.size() || q_[i].pts < oldestPts) { oldest = i; oldestPts = q_[i].pts; }
        if (q_[i].pts <= ptsSec && (best == q_.size() || q_[i].pts < bestPts)) {
            best = i;
            bestPts = q_[i].pts;
        }
    }

    if (best == q_.size()) {
        // 没有到点的帧：结尾 drain 时可超前取最早一帧，否则保持当前画面。
        if (!allowAhead || oldest == q_.size()) return std::nullopt;
        best = oldest;
    }

    Node n = std::move(q_[best]);
    q_.erase(q_.begin() + static_cast<long>(best));
    lk.unlock();
    cv_.notify_all();
    return std::move(n.vf);
}

size_t FrameQueue::videoCountLocked() const {
    size_t n = 0;
    for (const auto& x : q_) if (x.isVideo) ++n;
    return n;
}

size_t FrameQueue::audioCountLocked() const {
    size_t n = 0;
    for (const auto& x : q_) if (!x.isVideo) ++n;
    return n;
}

std::variant<VideoFrame, AudioFrame> FrameQueue::pop() {
    std::unique_lock<std::mutex> lk(mu_);
    cv_.wait(lk, [&]{ return aborted_ || !q_.empty(); });
    if (aborted_ && q_.empty()) return VideoFrame{};
    // 选最早 PTS 的节点；同 PTS 时优先音频（主时钟）。
    size_t bestIdx = 0;
    for (size_t i = 1; i < q_.size(); ++i) {
        if (q_[i].pts < q_[bestIdx].pts) bestIdx = i;
    }
    Node n = std::move(q_[bestIdx]);
    q_.erase(q_.begin() + bestIdx);
    lk.unlock();
    // 唤醒可能因背压阻塞的音频生产者。
    cv_.notify_all();
    if (n.isVideo) return std::move(n.vf);
    return std::move(n.af);
}

std::optional<AudioFrame> FrameQueue::popAudio(int timeoutMs) {
    std::unique_lock<std::mutex> lk(mu_);
    auto hasAudio = [&]{
        for (const auto& n : q_) if (!n.isVideo) return true;
        return false;
    };
    if (timeoutMs < 0) {
        cv_.wait(lk, [&]{ return aborted_ || hasAudio(); });
    } else {
        // 有限等待：避免长时间占用音频设备线程（欠载好于阻塞）。
        cv_.wait_for(lk, std::chrono::milliseconds(timeoutMs),
                     [&]{ return aborted_ || hasAudio(); });
    }

    // 找最早 PTS 的音频节点；视频节点留在队列里由渲染层取用。
    size_t bestIdx = q_.size();
    for (size_t i = 0; i < q_.size(); ++i) {
        if (q_[i].isVideo) continue;
        if (bestIdx == q_.size() || q_[i].pts < q_[bestIdx].pts) bestIdx = i;
    }
    if (bestIdx == q_.size()) return std::nullopt; // 仅剩视频节点或已 abort

    Node n = std::move(q_[bestIdx]);
    q_.erase(q_.begin() + static_cast<long>(bestIdx));
    lk.unlock();
    // 唤醒可能因背压阻塞的音频生产者。
    cv_.notify_all();
    return std::move(n.af);
}

VideoFrame FrameQueue::peekLatestVideo() {
    std::lock_guard<std::mutex> lk(mu_);
    VideoFrame latest{};
    bool found = false;
    for (auto& n : q_) {
        if (n.isVideo && (!found || n.pts > latest.ptsSec)) {
            latest = n.vf;
            found = true;
        }
    }
    return latest;
}

void FrameQueue::clear() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        q_.clear();
    }
    // 清空后可能有生产者正因背压等待，需唤醒。
    cv_.notify_all();
}

void FrameQueue::abort() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        aborted_ = true;
    }
    cv_.notify_all();
}

void FrameQueue::reset() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        q_.clear();
        aborted_ = false;
    }
    cv_.notify_all();
}

} // namespace vtcore
