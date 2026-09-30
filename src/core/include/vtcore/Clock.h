#pragma once

#include <atomic>
#include <chrono>

namespace vtcore {

/// 以音频设备已播放字节数（折算 PTS）为主时钟。
/// 单调、不与系统墙钟耦合；用于视频帧的等待/丢弃判定。
class AudioMasterClock {
public:
    void   start();
    void   pause();
    void   resume();
    void   reset();

    /// 当前已播放媒体时间（秒），相对管线 start/reset 起点。
    double nowSec() const;

    /// 已播放字节数折算秒（由 QAudioSink 调用方提供）。
    void   onAudioBytesPlayed(long long bytesPlayed, int sampleRate, int channels);

    /// 由音频输出层上报的"当前已送出的绝对媒体时间"（秒，含起始 PTS）。
    /// 音频设备实际播放位置是播放器最可靠的时间来源。
    void   setAudioPts(double sec);

    /// 当前目标速率倍率（1.0=正常）。
    void   setRate(double rate);
    double rate() const { return rate_; }

private:
    /// 运行态位置（不考虑暂停冻结）。暂停时用它作为冻结值。
    double runningSec() const;

    std::atomic<double> audioPtsSec_{0.0};   // 累计音频 PTS
    std::atomic<double> virtualPtsSec_{0.0}; // 暂停期间冻结 / 倍速播放时的虚拟时间
    std::atomic<bool>   paused_{false};
    /// 是否已 start()：未启动时 nowSec() 返回 0，避免使用未初始化的墙钟起点。
    std::atomic<bool>   started_{false};
    std::atomic<double> rate_{1.0};
    std::chrono::steady_clock::time_point wallStart_{};
    std::chrono::steady_clock::time_point pausedAt_{};
    double pausedVirtualAt_ = 0.0;
};

} // namespace vtcore