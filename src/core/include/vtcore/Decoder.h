#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/pixfmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include "AudioFrame.h"
#include "FrameQueue.h"
#include "PacketQueue.h"
#include "VideoFrame.h"

#include <atomic>

namespace vtcore {

/// 单流解码线程封装：消费 PacketQueue，产出 VideoFrame 或 AudioFrame 推入 FrameQueue。
class Decoder {
public:
    enum class Kind { Video, Audio };

    Decoder(Kind kind, AVCodecContext* ctx, AVStream* stream,
            PacketQueue& in, FrameQueue& out,
            const std::atomic<int>& targetW, const std::atomic<int>& targetH);
    ~Decoder();

    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;

    void run();
    void requestStop();
    void requestFlush(); // 线程内部下一次循环清空解码器缓冲

    // AI 字幕观察者转发（仅音频）
    void setAudioObserver(class AudioFrameObserver* obs);

private:
    void runVideo();
    void runAudio();
    void handleSeekFlush();

    Kind kind_;
    AVCodecContext* ctx_;
    AVStream* stream_;
    PacketQueue& in_;
    FrameQueue&  out_;
    const std::atomic<int>& targetW_;  // 输出尺寸上限（0=源尺寸）
    const std::atomic<int>& targetH_;
    SwsContext* sws_ = nullptr;       // video: YUV->RGBA（按目标尺寸缩放）
    SwrContext* swr_ = nullptr;       // audio: ->S16/2ch/48k
    AVPixelFormat swsDst_ = AV_PIX_FMT_RGBA;
    int swsW_ = 0, swsH_ = 0;         // 当前 sws 输出尺寸
    int swsSrcFmt_ = -1;              // 当前 sws 源像素格式
    int outSampleRate_ = 48000;
    int outChannels_   = 2;

    AudioFrameObserver* audioObs_ = nullptr;

    std::atomic<bool> stop_{false};
    std::atomic<bool> flushPending_{false};
};

} // namespace vtcore