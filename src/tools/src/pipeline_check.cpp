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
#include <cstdio>
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
    p.close();
    if (consumer.joinable()) consumer.join();

    std::printf("stats: videoFrames=%ld audioFrames=%ld\n",
        videoFrames.load(), audioFrames.load());
    return 0;
}