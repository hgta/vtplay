#include "Demuxer.h"
#include "PacketQueue.h"

#include <chrono>
#include <cstdio>
#include <thread>

namespace vtcore {

Demuxer::Demuxer(AVFormatContext* fmt, int vIdx, int aIdx, PacketQueue& vq, PacketQueue& aq)
    : fmt_(fmt), vIdx_(vIdx), aIdx_(aIdx), vq_(vq), aq_(aq) {}

Demuxer::~Demuxer() = default;

void Demuxer::requestStop() { stop_.store(true); }

void Demuxer::requestSeek(double sec) {
    seekTarget_ = sec;
    seekPending_.store(true);
}

void Demuxer::run() {
    while (!stop_.load()) {
        if (seekPending_.exchange(false)) {
            vq_.clear();
            aq_.clear();
            int64_t ts = static_cast<int64_t>(seekTarget_ * AV_TIME_BASE);
            av_seek_frame(fmt_, -1, ts, AVSEEK_FLAG_BACKWARD);
            eof_ = false;
        }

        AVPacket* pkt = av_packet_alloc();
        if (!pkt) break;
        int ret = av_read_frame(fmt_, pkt);
        if (ret < 0) {
            av_packet_free(&pkt);
            if (ret == AVERROR_EOF) {
                eof_ = true;
                // 发送 flush 空包（size==0 且 data==nullptr）：解码器据此进入
                // drain 模式，吐出内部缓冲的尾部帧。否则 HEVC 末尾数帧永远
                // 显示不出来 → 表现为"最后几秒画面卡住不动"。
                if (vIdx_ >= 0) {
                    AVPacket* f = av_packet_alloc();
                    if (f) vq_.push(f);
                }
                if (aIdx_ >= 0) {
                    AVPacket* f = av_packet_alloc();
                    if (f) aq_.push(f);
                }
                break;
            }
            // 短暂退避后重试（网络流中断等场景）
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        if (pkt->stream_index == vIdx_ && vIdx_ >= 0) {
            if (!vq_.push(pkt)) { av_packet_free(&pkt); break; }
        } else if (pkt->stream_index == aIdx_ && aIdx_ >= 0) {
            if (!aq_.push(pkt)) { av_packet_free(&pkt); break; }
        } else {
            av_packet_free(&pkt);
        }
    }
}

} // namespace vtcore