#pragma once

#include <cstdint>
#include <string>

namespace vtcore {

/// 打开媒体后解析出的元数据。核心层不产出面向界面的中文文案，
/// 只提供原始数值与中性格式化（界面层负责本地化与最终排版）。
struct MediaInfo {
    std::string url;             // 媒体路径/URL
    std::string fileName;        // 文件名（不含目录）
    double durationSec = 0.0;    // 总时长，秒
    int    videoWidth = 0;
    int    videoHeight = 0;
    double videoFrameRate = 0.0; // 平均帧率
    bool   hasVideo = false;
    bool   hasAudio = false;
    std::string videoCodec;
    std::string audioCodec;

    // ---- 扩展元数据：供界面展示与导出估算使用 ----
    // 约定：0 表示"该字段不可用"，界面须据此降级显示，不得显示为 0。
    int64_t fileSizeBytes   = 0;  // 文件大小
    int64_t videoBitRate    = 0;  // 视频码率（bps）
    int     audioSampleRate = 0;  // 音频采样率（Hz）
    int     audioChannels   = 0;  // 音频声道数
    int64_t audioBitRate    = 0;  // 音频码率（bps）
};

/// 从路径或 URL 中提取文件名（兼容 '/' 与 '\\' 分隔符，忽略 query/fragment）。
std::string fileNameFromPath(const std::string& path);

/// 码率格式化。compact=false 得到 "54 Mbps"，compact=true 得到 "54Mbps"。
/// bps <= 0 返回 "—"（表示不可用）。
std::string formatBitrate(int64_t bps, bool compact = false);

/// 文件大小格式化。compact=false 得到 "158 MB"，compact=true 得到 "158MB"。
/// bytes <= 0 返回 "—"（表示不可用）。
std::string formatSize(int64_t bytes, bool compact = false);

/// 一行规格摘要，如 "2160×3840 · 60fps · 54Mbps · 158MB"。
/// 缺失的字段自动省略；全部缺失时返回空串。
std::string formatSpecSummary(const MediaInfo& info);

} // namespace vtcore
