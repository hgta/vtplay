#pragma once

#include <string>
#include <cstdint>

namespace vtcore {

struct MediaInfo {
    std::string url;          // 媒体路径/URL
    double durationSec = 0.0; // 总时长，秒
    int    videoWidth = 0;
    int    videoHeight = 0;
    double videoFrameRate = 0.0; // 平均帧率
    bool   hasVideo = false;
    bool   hasAudio = false;
    std::string videoCodec;
    std::string audioCodec;
};

} // namespace vtcore