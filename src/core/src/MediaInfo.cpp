#include "MediaInfo.h"

#include <cmath>
#include <cstdio>

namespace vtcore {

namespace {

/// 字段不可用时的统一占位符。
const char* const kUnavailable = "—";

/// 定点格式化（避免为几个数字引入 <sstream>）。
std::string num(double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return buf;
}

} // namespace

std::string fileNameFromPath(const std::string& path) {
    std::string p = path;
    // 忽略 query / fragment（处理 file:///... 或网络 URL）
    const auto cut = p.find_first_of("?#");
    if (cut != std::string::npos) p = p.substr(0, cut);
    const auto sep = p.find_last_of("/\\");
    return (sep == std::string::npos) ? p : p.substr(sep + 1);
}

std::string formatBitrate(int64_t bps, bool compact) {
    if (bps <= 0) return kUnavailable;
    const double v  = static_cast<double>(bps);
    const char*  sp = compact ? "" : " ";
    if (v >= 1000000.0) {
        const double m = v / 1000000.0;
        // 数值较大时小数位没有展示意义（54.2 Mbps → 54 Mbps）
        return num(m, m < 10.0 ? 1 : 0) + sp + "Mbps";
    }
    if (v >= 1000.0) return num(v / 1000.0, 0) + sp + "kbps";
    return num(v, 0) + sp + "bps";
}

std::string formatSize(int64_t bytes, bool compact) {
    if (bytes <= 0) return kUnavailable;
    const double v   = static_cast<double>(bytes);
    const char*  sp  = compact ? "" : " ";
    const double kKi = 1024.0;
    const double kMi = kKi * 1024.0;
    const double kGi = kMi * 1024.0;
    if (v >= kGi) {
        const double g = v / kGi;
        return num(g, g < 10.0 ? 2 : 1) + sp + "GB";
    }
    if (v >= kMi) {
        const double m = v / kMi;
        return num(m, m < 10.0 ? 1 : 0) + sp + "MB";
    }
    if (v >= kKi) return num(v / kKi, 0) + sp + "KB";
    return num(v, 0) + sp + "B";
}

std::string formatSpecSummary(const MediaInfo& info) {
    std::string out;
    auto append = [&out](const std::string& piece) {
        if (piece.empty()) return;
        if (!out.empty()) out += " · ";
        out += piece;
    };

    if (info.hasVideo && info.videoWidth > 0 && info.videoHeight > 0) {
        append(num(info.videoWidth, 0) + "×" + num(info.videoHeight, 0));
    }
    if (info.videoFrameRate > 0.0) {
        const double fr = info.videoFrameRate;
        const bool integral = std::fabs(fr - std::rint(fr)) < 0.01;
        append(num(fr, integral ? 0 : 2) + "fps");
    }
    if (info.videoBitRate > 0)  append(formatBitrate(info.videoBitRate, true));
    if (info.fileSizeBytes > 0) append(formatSize(info.fileSizeBytes, true));
    return out;
}

} // namespace vtcore
