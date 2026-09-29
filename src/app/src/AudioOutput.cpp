#include "AudioOutput.h"

#include "MediaPipeline.h"
#include "AudioFrame.h"
#include "Clock.h"
#include "PlayerState.h"

#include <QAudioSink>
#include <QAudioFormat>
#include <QMediaDevices>
#include <QDebug>
#include <algorithm>
#include <cstring>

namespace vtapp {

qint64 AudioOutput::PcmDevice::readData(char* data, qint64 maxlen) {
    if (!out->pipeline_) return 0;
    // 未打开媒体：直接返回，不阻塞音频设备线程。
    if (out->pipeline_->status() == vtcore::PlayerStatus::Idle) return 0;

    qint64 produced = 0;
    while (produced < maxlen) {
        // 当前帧已消费完：取下一帧音频（只取音频节点，视频节点留给渲染层）。
        if (curOffset_ >= curPcm_.size()) {
            // 有限等待：拿不到就返回已产出数据，避免长时间占住设备线程。
            auto af = out->pipeline_->takeAudioFrame(20);
            if (!af) break;                 // 超时/队列中止：本次到此为止
            if (!af->valid || af->pcm.empty()) break;

            curPcm_        = std::move(af->pcm);
            curOffset_     = 0;
            curFramePts_   = af->ptsSec;
            curSampleRate_ = af->sampleRate > 0 ? af->sampleRate : 48000;
            curChannels_   = af->channels   > 0 ? af->channels   : 2;
        }

        const qint64 need = maxlen - produced;
        const qint64 avail = static_cast<qint64>(curPcm_.size() - curOffset_);
        const qint64 take = std::min(need, avail);
        std::memcpy(data + produced, curPcm_.data() + curOffset_, static_cast<size_t>(take));
        curOffset_ += take;
        produced   += take;

        // 上报绝对媒体时间：帧 PTS + 帧内已送出部分（S16 => 每帧 2*声道 字节）。
        const double bytesPerSec = double(curSampleRate_) * curChannels_ * 2.0;
        if (bytesPerSec > 0.0) {
            const double sec = curFramePts_ + double(curOffset_) / bytesPerSec;
            out->pipeline_->onAudioPosition(sec);
        }
    }

    // 播放结束判定：解封装已到末尾且持续取不到音频数据（约 0.5 秒），
    // 认为缓冲播完 → 上报 Eof（冻结时钟，避免进度条越过时长）。
    if (produced == 0) {
        if (++out->emptyStreak_ > 25 && out->pipeline_->isDemuxDone()) {
            out->pipeline_->notifyPlaybackEof();
        }
    } else {
        out->emptyStreak_ = 0;
    }
    return produced;
}

AudioOutput::AudioOutput(QObject* parent) : QObject(parent) {

    QAudioFormat fmt;
    fmt.setSampleRate(48000);
    fmt.setChannelCount(2);
    fmt.setSampleFormat(QAudioFormat::Int16);
    device_.reset(new PcmDevice(this));

    auto* sink = new QAudioSink(fmt, this);
    sink_.reset(sink);
    sink_->setVolume(static_cast<qreal>(volume_));
    // 拉模式：QAudioSink 从 PcmDevice 拉取 PCM（设备格式固定为 48k/2ch/S16，
    // 与解码器 swr 输出一致）。
    sink_->start(device_.data());
}

AudioOutput::~AudioOutput() {
    if (sink_) sink_->stop();
}

void AudioOutput::attach(vtcore::MediaPipeline* p) { pipeline_ = p; }

void AudioOutput::setVolume(double v) {
    volume_ = v;
    if (sink_) sink_->setVolume(static_cast<qreal>(muted_ ? 0.0 : v));
}

void AudioOutput::setMuted(bool m) {
    muted_ = m;
    if (sink_) sink_->setVolume(static_cast<qreal>(m ? 0.0 : volume_));
}

} // namespace vtapp
