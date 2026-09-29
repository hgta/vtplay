#include "PacketQueue.h"

extern "C" {
#include <libavcodec/packet.h>
}

namespace vtcore {

PacketQueue::PacketQueue(size_t capacity) : capacity_(capacity) {}
PacketQueue::~PacketQueue() { clear(); abort(); }

void PacketQueue::abort() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (aborted_) return;
        aborted_ = true;
    }
    cvNotEmpty_.notify_all();
    cvNotFull_.notify_all();
}

void PacketQueue::clear() {
    std::lock_guard<std::mutex> lk(mu_);
    while (!q_.empty()) {
        AVPacket* p = q_.front();
        q_.pop();
        if (p) av_packet_free(&p);
    }
    cvNotFull_.notify_all();
}

void PacketQueue::reset() {
    std::lock_guard<std::mutex> lk(mu_);
    while (!q_.empty()) {
        AVPacket* p = q_.front();
        q_.pop();
        if (p) av_packet_free(&p);
    }
    aborted_ = false;
    cvNotEmpty_.notify_all();
    cvNotFull_.notify_all();
}

bool PacketQueue::push(AVPacket* pkt) {
    if (!pkt) return false;
    std::unique_lock<std::mutex> lk(mu_);
    cvNotFull_.wait(lk, [&]{ return aborted_ || q_.size() < capacity_; });
    if (aborted_) {
        lk.unlock();
        return false;
    }
    q_.push(pkt);
    lk.unlock();
    cvNotEmpty_.notify_one();
    return true;
}

AVPacket* PacketQueue::pop() {
    std::unique_lock<std::mutex> lk(mu_);
    cvNotEmpty_.wait(lk, [&]{ return aborted_ || !q_.empty(); });
    if (aborted_ && q_.empty()) return nullptr;
    AVPacket* p = q_.front();
    q_.pop();
    lk.unlock();
    cvNotFull_.notify_one();
    return p;
}

} // namespace vtcore