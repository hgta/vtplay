// vtplay-pipeline-check
// headless 自测：打开一个媒体文件，统计解码帧数、PTS 连续性、同步偏差。
// 用法：pipeline_check <input>

#include "MediaPipeline.h"
#include "MediaInfo.h"
#include "VideoFrame.h"
#include "AudioFrame.h"
#include "PlayerState.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <optional>
#include <thread>
#include <variant>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: pipeline_check <file>\n");
        return 2;
    }
    vtcore::MediaPipeline p;
    try {
        p.open(argv[1]);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "open failed: %s\n", e.what());
        return 1;
    }

    auto info = p.info();
    std::printf("loaded: %.2fs, %dx%d, hasV=%d hasA=%d, codec=%s/%s\n",
        info.durationSec, info.videoWidth, info.videoHeight,
        (int)info.hasVideo, (int)info.hasAudio,
        info.videoCodec.c_str(), info.audioCodec.c_str());

    // 扩展元数据（供界面展示与导出估算；不可用字段以 "—" 表示）
    std::printf("file   : %s (%s)\n",
        info.fileName.empty() ? "(none)" : info.fileName.c_str(),
        vtcore::formatSize(info.fileSizeBytes).c_str());
    std::printf("video  : %s\n", vtcore::formatBitrate(info.videoBitRate).c_str());
    std::printf("audio  : %dHz %dch %s\n",
        info.audioSampleRate, info.audioChannels,
        vtcore::formatBitrate(info.audioBitRate).c_str());
    std::printf("summary: %s\n", vtcore::formatSpecSummary(info).c_str());

    std::atomic<long> videoFrames{0};
    std::atomic<long> audioFrames{0};
    std::atomic<bool> stop{false};

    std::thread consumer([&]{
        double lastPts = -1.0;
        while (!stop.load()) {
            auto f = p.takeFrame();
            if (std::holds_alternative<vtcore::VideoFrame>(f)) {
                auto& v = std::get<vtcore::VideoFrame>(f);
                if (!v.valid) break; // queue aborted
                videoFrames.fetch_add(1);
                if (lastPts >= 0 && (v.ptsSec - lastPts) < 0) {
                    std::fprintf(stderr, "WARN: video PTS non-monotonic at %.3f -> %.3f\n",
                                 lastPts, v.ptsSec);
                }
                lastPts = v.ptsSec;
            } else {
                auto& a = std::get<vtcore::AudioFrame>(f);
                if (!a.valid) break; // queue aborted
                audioFrames.fetch_add(1);
            }
        }
    });

    p.play();
    auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 5.0
           && p.status() != vtcore::PlayerStatus::Eof) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    auto v = p.peekLatestVideo();
    std::printf("latest video pts after 5s: %.3fs (valid=%d)\n", v.ptsSec, (int)v.valid);

    stop.store(true);
    if (consumer.joinable()) consumer.join();

    // ---- 逐帧步进自测 ----
    // 用与 VideoRenderer::onTick 相同的取帧逻辑模拟「渲染层」：每一拍把到点的帧
    // 全部取走、只留最后一个。这样测的是「用户看到的画面是否真的变了」，
    // 而不是只看时钟数值。
    // 与 VideoRenderer::onTick 保持一致：播放中留 20ms 容差，暂停/逐帧不留。
    // 这里必须同口径，否则测出来的步进幅度会与真实观感不符（曾因此误判为「跳两帧」）。
    double lastShown = -1.0;
    auto pumpDisplay = [&]() -> double {
        const double tol = (p.status() == vtcore::PlayerStatus::Playing) ? 0.02 : 0.0;
        std::optional<vtcore::VideoFrame> chosen;
        while (auto f = p.takeVideoUpTo(p.positionSec() + tol, false)) {
            chosen = std::move(f);
        }
        if (chosen && chosen->valid) lastShown = chosen->ptsSec;
        return lastShown;
    };

    p.pause();
    for (int i = 0; i < 40 && lastShown < 0.0; ++i) {
        pumpDisplay();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    const double basePts = lastShown;
    std::printf("step: baseline pts=%.3f (fps=%.2f)\n", basePts, info.videoFrameRate);

    // 步进一次并等「画面稳定」再取值；同时量出**画面变化所需时间**（用户感知延迟）。
    //
    // 不能一看到变化就返回：后退要回关键帧重解，seek 后先解出来的是关键帧本身
    // （可能比目标早半秒），那一刻读到的是中间态而不是最终落点。
    // 所以：用「首次变化」计时，用「稳定」取值。
    auto stepOnce = [&](int dir, double from, int budgetMs, double& ms) -> double {
        const auto t0 = std::chrono::steady_clock::now();
        ms = -1.0;
        if (!p.stepFrame(dir)) return from;
        double last = pumpDisplay();
        int  stable = 0;
        bool changed = false;
        for (int k = 0; k < budgetMs / 20; ++k) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            const double v = pumpDisplay();
            const bool moved = (dir > 0) ? (v > from + 1e-6) : (v < from - 1e-6);
            if (moved && !changed) {
                changed = true;
                ms = std::chrono::duration<double, std::milli>(
                         std::chrono::steady_clock::now() - t0).count();
            }
            if (std::abs(v - last) < 1e-6) {
                if (++stable >= 8 && changed) break;   // 连续 8 拍不再变化 -> 已稳定
            } else {
                stable = 0;
            }
            last = v;
        }
        return last;
    };

    int fwdOk = 0, backOk = 0;
    double fwdMs = 0.0, backMs = 0.0;
    double prev = basePts;
    for (int i = 1; i <= 5; ++i) {
        double ms = -1.0;
        const double next = stepOnce(+1, prev, 600, ms);
        std::printf("  step +1 #%d -> pts=%.3f (delta=%+.4f, %6.0f ms)\n",
                    i, next, next - prev, ms);
        if (next > prev + 1e-6) { ++fwdOk; fwdMs += ms; }
        prev = next;
    }
    for (int i = 1; i <= 5; ++i) {
        // 后退要回关键帧重解，给足预算
        double ms = -1.0;
        const double next = stepOnce(-1, prev, 3000, ms);
        std::printf("  step -1 #%d -> pts=%.3f (delta=%+.4f, %6.0f ms)\n",
                    i, next, next - prev, ms);
        if (next < prev - 1e-6) { ++backOk; backMs += ms; }
        prev = next;
    }
    std::printf("step: forward %d/5 ok (avg %.0f ms), backward %d/5 ok (avg %.0f ms), "
                "returned to %.3f from %.3f\n",
                fwdOk, fwdOk ? fwdMs / fwdOk : 0.0,
                backOk, backOk ? backMs / backOk : 0.0, prev, basePts);

    p.close();

    std::printf("stats: videoFrames=%ld audioFrames=%ld\n",
        videoFrames.load(), audioFrames.load());
    return (fwdOk == 5 && backOk == 5) ? 0 : 3;
}