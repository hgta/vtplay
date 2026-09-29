#pragma once

#include <QObject>
#include <QIODevice>
#include <QAudioSink>
#include <QAudioFormat>

#include <vector>

namespace vtcore { class MediaPipeline; }

namespace vtapp {

/// 基于 QAudioSink 的 PCM 拉取器。从管线取音频帧、写入设备，并把"已播放字节数"
/// 折算成主时钟 PTS 反馈给 MediaPipeline（音频主时钟同步）。
class AudioOutput : public QObject {
    Q_OBJECT
public:
    explicit AudioOutput(QObject* parent = nullptr);
    ~AudioOutput();

    void attach(vtcore::MediaPipeline* pipeline);
    void setVolume(double v);
    void setMuted(bool m);

private:
    struct PcmDevice : public QIODevice {
        AudioOutput* out;
        explicit PcmDevice(AudioOutput* o) : out(o) {
            // QAudioSink 拉模式要求设备处于打开且可读状态
            open(QIODevice::ReadOnly);
        }
        qint64 readData(char* data, qint64 maxlen) override;
        qint64 writeData(const char*, qint64) override { return -1; }
        bool isSequential() const override { return true; }
        // 实时音频源：必须报告"总有数据可读"，否则 QAudioSink 拉模式
        // 会认为无数据可用而永不调用 readData（导致完全无声）。
        qint64 bytesAvailable() const override {
            return 64 * 1024 + QIODevice::bytesAvailable();
        }

        // 当前正在输出的音频帧：跨 readData 调用保持，避免丢弃帧内剩余数据。
        std::vector<unsigned char> curPcm_;
        size_t curOffset_ = 0;
        double curFramePts_   = 0.0;
        int    curSampleRate_ = 48000;
        int    curChannels_   = 2;
    };

    QScopedPointer<PcmDevice>    device_;
    QScopedPointer<QAudioSink>   sink_;
    vtcore::MediaPipeline*       pipeline_ = nullptr;
    double volume_ = 1.0;
    bool muted_ = false;
    /// 连续多少次 readData 没取到音频（用于判定播放结束）。
    int emptyStreak_ = 0;
};

} // namespace vtapp