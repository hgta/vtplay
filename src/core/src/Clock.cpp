#include "Clock.h"

#include <cmath>

namespace vtcore {

void AudioMasterClock::start() {
    paused_.store(false);
    audioPtsSec_.store(0.0);
    virtualPtsSec_.store(0.0);
    pausedVirtualAt_ = 0.0;
    wallStart_ = std::chrono::steady_clock::now();
    started_.store(true);
}

void AudioMasterClock::pause() {
    if (paused_.exchange(true)) return;
    pausedAt_ = std::chrono::steady_clock::now();
    pausedVirtualAt_ = virtualPtsSec_.load();
}

void AudioMasterClock::resume() {
    if (!paused_.exchange(false)) return;
    // 暂停期间 wallStart 相对位置要向后推，否则 nowSec 会瞬间跳。
    auto pausedDur = std::chrono::steady_clock::now() - pausedAt_;
    wallStart_ += pausedDur;
}

void AudioMasterClock::reset() {
    paused_.store(false);
    audioPtsSec_.store(0.0);
    virtualPtsSec_.store(0.0);
    pausedVirtualAt_ = 0.0;
    started_.store(false);
}

double AudioMasterClock::nowSec() const {
    if (!started_.load()) {
        return 0.0;  // 尚未打开媒体：位置为 0，避免未初始化墙钟产生跳变
    }
    if (paused_.load()) {
        return pausedVirtualAt_;
    }
    // 音频主时钟：设备实际送出的位置最可靠，且不会越过媒体末尾。
    double aPts = audioPtsSec_.load();
    if (aPts > 0.0) return aPts;

    // 无音频轨道时回退到墙钟（按倍速缩放）。
    auto now = std::chrono::steady_clock::now();
    double wall = std::chrono::duration<double>(now - wallStart_).count();
    double r = rate_.load();
    return std::isfinite(wall) ? wall * r : 0.0;
}

void AudioMasterClock::onAudioBytesPlayed(long long bytesPlayed, int sampleRate, int channels) {
    if (sampleRate <= 0 || channels <= 0) return;
    double sec = static_cast<double>(bytesPlayed) / (sampleRate * channels * 2 /*S16*/);
    audioPtsSec_.store(sec);
    // 同步虚拟时间：以音频为主。
    virtualPtsSec_.store(sec);
    if (paused_.load()) pausedVirtualAt_ = sec;
}

void AudioMasterClock::setAudioPts(double sec) {
    if (!std::isfinite(sec) || sec < 0.0) return;
    audioPtsSec_.store(sec);
    virtualPtsSec_.store(sec);
    if (paused_.load()) pausedVirtualAt_ = sec;
}

void AudioMasterClock::setRate(double rate) {
    if (rate < 0.25) rate = 0.25;
    if (rate > 4.0)  rate = 4.0;
    rate_.store(rate);
}

} // namespace vtcore