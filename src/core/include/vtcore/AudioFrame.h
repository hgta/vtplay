#pragma once

#include <cstdint>
#include <vector>

namespace vtcore {

/// 解码后统一格式的音频帧：S16 / 立体声 / 48 kHz。
struct AudioFrame {
    std::vector<uint8_t> pcm;   // 交错 S16 L R
    int    sampleRate = 48000;
    int    channels   = 2;
    double ptsSec     = 0.0;
    bool   valid      = false;
};

} // namespace vtcore