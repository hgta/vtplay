#pragma once

#include <cstdint>
#include <vector>

namespace vtcore {

/// 解码后的视频帧：上传到 GPU 纹理前使用的统一格式（RGBA8888）。
struct VideoFrame {
    std::vector<uint8_t> rgba; // RGBA bytes
    int    width = 0;
    int    height = 0;
    int    stride = 0;         // rgba.size() >= height * stride
    double ptsSec = 0.0;       // 显示时间戳（秒）
    bool   valid = false;      // 解码/有效标记
};

} // namespace vtcore