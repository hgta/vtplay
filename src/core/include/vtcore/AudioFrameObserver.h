#pragma once

#include "AudioFrame.h"

namespace vtcore {

/// AI 字幕预留接口：观察者接收解码后的统一格式 PCM 流。
/// MVP 阶段无观察者注册，性能开销为零；有观察者时由 MediaPipeline 在产出音频帧后调用。
class AudioFrameObserver {
public:
    virtual ~AudioFrameObserver() = default;
    virtual void onAudioFrame(const AudioFrame& frame) = 0;
};

} // namespace vtcore