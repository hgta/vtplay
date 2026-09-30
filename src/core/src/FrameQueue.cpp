#include "FrameQueue.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <vector>

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

void FrameQueue::setKeepAfter(double pts) {
    std::lock_guard<std::mutex> lk(mu_);
    keepAfterPts_ = pts;
}

// 丢弃最旧的一个「可丢」视频帧。调用方须持有 mu_。
//
// 「可丢」= 不在保留窗口内。窗口 = 基点之后最近的 kKeepCount 帧：
// 基点由渲染层给出（用户正看着的那一帧），窗口挡住的就是「紧接着要显示的那几帧」，
// 逐帧前进直接从这里取，不必回关键帧重解。
// 返回 false 表示当前没有可丢的帧（全都受保护）。
bool FrameQueue::dropOldestDroppableLocked() {
    if (keepAfterPts_ < 0.0) { dropOldest(true); return true; }

    // 收集视频帧下标并按 PTS 升序（队列未排序，需自行排序）
    std::vector<size_t> idx;
    for (size_t i = 0; i < q_.size(); ++i) {
        if (q_[i].isVideo) idx.push_back(i);
    }
    if (idx.empty()) return false;
    std::sort(idx.begin(), idx.end(),
              [this](size_t a, size_t b) { return q_[a].pts < q_[b].pts; });

    // 基点之后的帧里，最近的 kKeepCount 个受保护
    std::vector<size_t> protect;
    for (size_t i : idx) {
        if (q_[i].pts > keepAfterPts_) {
            protect.push_back(i);
            if (static_cast<int>(protect.size()) >= kKeepCount) break;
        }
    }

    for (size_t i : idx) {
        bool guarded = false;
        for (size_t p : protect) {
            if (p == i) { guarded = true; break; }
        }
        if (!guarded) {
            q_.erase(q_.begin() + static_cast<long>(i));
            return true;
        }
    }
    return false;
}

// 视频帧：队列满时阻塞等待消费者（背压），而不是丢最旧。
//
// 为什么不能"满则丢最旧"：解码速度通常快于显示速度（例如 4K 软解 68fps >
// 60fps 显示），丢最旧会把"该显示的帧"持续丢掉，队列里只剩 PTS 远大于
// 当前时钟的"未来帧"，而渲染是按媒体时钟取帧（PTS <= 时钟）→ 永远取不到
// → 画面冻结（音频链路独立，故表现为"画面卡住、声音正常"）。
//
// 超时兜底：暂停或窗口不可见时消费者会停摆，不能无限阻塞，否则会经
// 包队列反向卡死 demux 线程。超时后丢弃最旧帧，保证管线始终可推进。
void FrameQueue::pushVideo(VideoFrame f) {
    Node n;
    n.pts = f.ptsSec;
    n.isVideo = true;
    n.vf = std::move(f);
    {
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait_for(lk, std::chrono::milliseconds(100),
                     [&]{ return aborted_ || videoCountLocked() < videoCapacity_; });
        if (aborted_) return;
        if (videoCountLocked() >= videoCapacity_) {
            // 超时兜底：丢最旧的一个「可丢」帧。保留窗口内的帧不丢——它们正是
            // 逐帧前进要用的「显示位置之后的那几帧」（见 setKeepAfter）。
            // 若全都受保护，就不再丢、直接推入：容量可临时超出，
            // 上界为 videoCapacity_ + kKeepCount。
            dropOldestDroppableLocked();
        }
        q_.push_back(std::move(n));
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

std::optional<VideoFrame> FrameQueue::popVideoAfter(double pts) {
    std::unique_lock<std::mutex> lk(mu_);

    // 视频队列未排序，需遍历找出 PTS 严格大于 pts 的最小者。
    size_t best = q_.size();
    double bestPts = 0.0;
    for (size_t i = 0; i < q_.size(); ++i) {
        if (!q_[i].isVideo) continue;
        if (q_[i].pts > pts && (best == q_.size() || q_[i].pts < bestPts)) {
            best = i;
            bestPts = q_[i].pts;
        }
    }
    if (best == q_.size()) return std::nullopt;

    Node n = std::move(q_[best]);
    q_.erase(q_.begin() + static_cast<long>(best));
    lk.unlock();
    cv_.notify_all();
    return std::move(n.vf);
}

size_t FrameQueue::videoCountAfter(double pts) const {
    std::lock_guard<std::mutex> lk(mu_);
    size_t n = 0;
    for (const auto& node : q_) {
        if (node.isVideo && node.pts > pts) ++n;
    }
    return n;
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
