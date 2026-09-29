#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>

struct AVPacket;

namespace vtcore {

/// 有界 AVPacket 队列；背压 demux 线程。
/// 内部所有权：队列持有 AVPacket*；关闭/清空时调用 av_packet_free 释放。
class PacketQueue {
public:
    explicit PacketQueue(size_t capacity = 64);
    ~PacketQueue();

    PacketQueue(const PacketQueue&) = delete;
    PacketQueue& operator=(const PacketQueue&) = delete;

    /// 推入一个 packet；满时阻塞直到有空间或 abort()。
    /// 成功则取得所有权；失败（abort）原包由调用方释放。
    bool push(AVPacket* pkt);

    /// 取出 packet；空时阻塞；返回 nullptr 表示已 abort。
    AVPacket* pop();

    /// 唤醒所有阻塞并停止。push/pop 之后的调用立即返回失败/nullptr。
    void abort();

    /// 清空队列并释放其中 AVPacket。
    void clear();

    /// 复位到可用状态：清空残留包并解除 abort 标志。
    /// 用于复用同一个队列对象打开新媒体（abort 是不可逆的，必须显式复位）。
    void reset();

    bool isAborted() const { return aborted_; }
    size_t size() const {
        std::lock_guard<std::mutex> lk(mu_);
        return q_.size();
    }

private:
    mutable std::mutex mu_;
    std::condition_variable cvNotEmpty_;
    std::condition_variable cvNotFull_;
    std::queue<AVPacket*> q_;
    size_t capacity_;
    bool aborted_ = false;
};

} // namespace vtcore