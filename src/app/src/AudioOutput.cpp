#include "AudioOutput.h"

#include "MediaPipeline.h"
#include "AudioFrame.h"
#include "Clock.h"
#include "PlayerState.h"

#include <QAudioDevice>
#include <QAudioSink>
#include <QAudioFormat>
#include <QMediaDevices>
#include <QTimer>
#include <QDebug>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace vtapp {

qint64 AudioOutput::PcmDevice::readData(char* data, qint64 maxlen) {
    // 拉模式契约：必须尽量填满缓冲并返回非零长度。
    // 一旦返回 0，QAudioSink 会判定欠载 → 进入 IdleState 并**停止继续拉取**，
    // 之后即使状态恢复为 Playing 也不会再调用本函数（表现为"永久无声"）。
    // 因此任何无数据的情况都用静音样本填充，保证设备始终在运行。
    if (!out->pipeline_) {
        std::memset(data, 0, static_cast<size_t>(maxlen));
        return maxlen;
    }

    // 未打开媒体 / 暂停 / 已结束：输出静音（设备保持运行，恢复播放立即有声）。
    const auto st = out->pipeline_->status();
    if (st != vtcore::PlayerStatus::Playing) {
        std::memset(data, 0, static_cast<size_t>(maxlen));
        return maxlen;
    }

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

    // 数据不足时用静音补齐，保证返回值非零（见函数开头的契约说明）。
    if (produced < maxlen) {
        std::memset(data + produced, 0, static_cast<size_t>(maxlen - produced));
    }

    out->dbgCalls_++;
    out->dbgBytes_ += produced;
    if (produced == 0) out->dbgEmpty_++;
    return maxlen;
}

AudioOutput::AudioOutput(QObject* parent) : QObject(parent) {
    // ---- 音频格式协商 ----
    // 默认请求 S16 / 立体声 / 48kHz；若默认设备不支持（例如只支持 44.1kHz
    // 的设备），回退到设备首选采样率/声道，并让解码器按该格式重采样。
    // 否则 QAudioSink 可能拒绝启动（无声）。
    const QAudioDevice dev = QMediaDevices::defaultAudioOutput();

    QAudioFormat fmt;
    fmt.setSampleRate(48000);
    fmt.setChannelCount(2);
    fmt.setSampleFormat(QAudioFormat::Int16);

    if (dev.isNull()) {
        std::fprintf(stderr, "[audio] 警告：系统没有可用的音频输出设备\n");
    } else if (!dev.isFormatSupported(fmt)) {
        const QAudioFormat pref = dev.preferredFormat();
        std::fprintf(stderr,
            "[audio] 48kHz/2ch/S16 不被设备支持，改用首选格式 %dHz/%dch\n",
            pref.sampleRate(), pref.channelCount());
        if (pref.sampleRate()   > 0) fmt.setSampleRate(pref.sampleRate());
        if (pref.channelCount() > 0) fmt.setChannelCount(pref.channelCount());
    }
    rate_     = fmt.sampleRate()   > 0 ? fmt.sampleRate()   : 48000;
    channels_ = fmt.channelCount() > 0 ? fmt.channelCount() : 2;
    fmt.setSampleFormat(QAudioFormat::Int16);   // 解码链路统一输出 S16

    if (!dev.isNull()) {
        std::fprintf(stderr, "[audio] device='%s' rate=%d ch=%d supported=%d\n",
                     dev.description().toUtf8().constData(),
                     rate_, channels_, (int)dev.isFormatSupported(fmt));
        std::fflush(stderr);
    }

    device_.reset(new PcmDevice(this));

    auto* sink = new QAudioSink(fmt, this);
    sink_.reset(sink);
    sink_->setVolume(static_cast<qreal>(volume_));

    // 状态变化日志：便于定位"无声"（IdleState=3 / StoppedState=2 表示设备未在播放）
    connect(sink, &QAudioSink::stateChanged, this, [](QAudio::State s) {
        std::fprintf(stderr, "[audio] state -> %d\n", static_cast<int>(s));
        std::fflush(stderr);
    });

    // 拉模式：QAudioSink 从 PcmDevice 拉取 PCM
    sink_->start(device_.data());
    std::fprintf(stderr, "[audio] sink started: state=%d error=%d\n",
                 static_cast<int>(sink_->state()), static_cast<int>(sink_->error()));
    std::fflush(stderr);

    // 看门狗：若设备意外停摆（StoppedState），自动重启，避免"永久无声"
    auto* watchdog = new QTimer(this);
    connect(watchdog, &QTimer::timeout, this, [this]() {
        if (!sink_) return;
        if (sink_->state() == QAudio::StoppedState) {
            std::fprintf(stderr, "[audio] sink stopped (error=%d) -> restarting\n",
                         static_cast<int>(sink_->error()));
            std::fflush(stderr);
            sink_->start(device_.data());
        }
    });
    watchdog->start(1000);

    // 播放期统计（前 15 秒，每秒一行）：用于确认音频数据是否真的在送往设备。
    // bytes 持续增长 = 正常；bytes 不涨且 empty 增长 = 解码侧没数据（画面/声音不同步）。
    auto* stats = new QTimer(this);
    connect(stats, &QTimer::timeout, this, [this, stats]() {
        if (++statTick_ > 15) { stats->stop(); return; }
        if (!pipeline_) return;
        std::fprintf(stderr,
            "[audio] t=%2ds status=%-7s calls=%lld bytes=%lld empty=%lld sinkState=%d\n",
            statTick_, vtcore::toString(pipeline_->status()),
            dbgCalls_, dbgBytes_, dbgEmpty_,
            static_cast<int>(sink_ ? sink_->state() : -1));
        std::fflush(stderr);
    });
    stats->start(1000);
}

AudioOutput::~AudioOutput() {
    if (sink_) sink_->stop();
}

void AudioOutput::attach(vtcore::MediaPipeline* p) {
    pipeline_ = p;
    // 把协商后的音频格式告知解码器（决定 libswresample 的重采样目标）
    if (pipeline_) pipeline_->setAudioFormat(rate_, channels_);
}

void AudioOutput::setVolume(double v) {
    volume_ = v;
    if (sink_) sink_->setVolume(static_cast<qreal>(muted_ ? 0.0 : v));
}

void AudioOutput::setMuted(bool m) {
    muted_ = m;
    if (sink_) sink_->setVolume(static_cast<qreal>(m ? 0.0 : volume_));
}

} // namespace vtapp
