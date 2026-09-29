#pragma once

extern "C" {
#include <libavformat/avformat.h>
}

#include <atomic>
#include <string>

namespace vtcore {

class PacketQueue;

/// Demux 线程封装：循环 av_read_frame，按流索引分发到 video/audio packet 队列。
class Demuxer {
public:
    Demuxer(AVFormatContext* fmt, int videoStreamIdx, int audioStreamIdx,
            PacketQueue& vq, PacketQueue& aq);
    ~Demuxer();

    Demuxer(const Demuxer&) = delete;
    Demuxer& operator=(const Demuxer&) = delete;

    void run();           // 阻塞循环，abort 后返回
    void requestStop();
    void requestSeek(double sec); // 由 Demux 线程下一次循环处理

    bool eof() const { return eof_; }

private:
    AVFormatContext* fmt_;
    int vIdx_, aIdx_;
    PacketQueue& vq_;
    PacketQueue& aq_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> seekPending_{false};
    double seekTarget_ = 0.0;
    bool eof_ = false;
};

} // namespace vtcore