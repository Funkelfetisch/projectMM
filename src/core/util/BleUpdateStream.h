#pragma once

/// The image bytes a phone streams without response for an update over BLE, between the link that writes them and the loop that stores them.

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "core/util/SpscRing.h"
#include "platform/nonblocking.h"   // MM_NONBLOCKING: the render loop drains the stream

namespace mm {

/// One transfer's stream: a packet that continues what it holds is kept, and every acknowledgement the phone asks for is answered once.
///
/// A packet is `[u32 LE offset][bytes]`. Bit 31 of the offset asks for an acknowledgement once everything up to the packet's end is stored, as Nordic DFU's packet receipt request does. A packet that leaves a gap, or finds the ring full, is dropped, and the next acknowledgement then names what is stored so the phone resends from there. Each answer also names the end of the request it answers.
///
/// The BLE host task calls `accept` and the render loop the rest, without a lock: the ring is single-producer single-consumer, and each other field has one writer or changes by compare-and-swap. `open` needs no lock either, since a link's writes arrive in order: the owner's last packets are taken before its `ota-begin`, and its first packets follow the reply that `open` precedes.
class BleUpdateStream {
public:
    /// The bytes a phone sends past its last acknowledgement; the ring holds a window and a packet more.
    static constexpr uint32_t kWindowBytes = 4096;
    /// The bit in a packet's offset that asks for an acknowledgement.
    static constexpr uint32_t kAckRequest = 0x80000000u;

    /// Take `owner`'s packets from image offset `from`, dropping whatever an earlier transfer left.
    void open(uint64_t owner, uint32_t from) MM_NONBLOCKING {
        open_.store(false, std::memory_order_release);
        uint8_t discard[64];
        while (ring_.pop(discard, sizeof(discard))) {}
        next_.store(from, std::memory_order_relaxed);
        taken_ = from;
        ackAt_.store(0, std::memory_order_relaxed);
        resend_.store(false, std::memory_order_relaxed);
        owner_.store(owner, std::memory_order_relaxed);
        open_.store(true, std::memory_order_release);
    }
    /// Stop taking packets.
    void close() MM_NONBLOCKING { open_.store(false, std::memory_order_release); }
    /// Whether packets are taken.
    bool isOpen() const MM_NONBLOCKING { return open_.load(std::memory_order_acquire); }

    /// Take one written packet from `owner`; false when it is not the owner's, is malformed, or was dropped.
    bool accept(uint64_t owner, const uint8_t* packet, size_t length) {
        if (!open_.load(std::memory_order_acquire) || owner != owner_.load(std::memory_order_relaxed) || !packet || length <= 4) return false;
        const uint32_t word = static_cast<uint32_t>(packet[0]) | static_cast<uint32_t>(packet[1]) << 8 |
                              static_cast<uint32_t>(packet[2]) << 16 | static_cast<uint32_t>(packet[3]) << 24;
        const uint32_t offset = word & ~kAckRequest;
        const uint32_t end = offset + static_cast<uint32_t>(length - 4);
        if (word & kAckRequest) {
            uint32_t asked = ackAt_.load(std::memory_order_relaxed);
            while (end > asked && !ackAt_.compare_exchange_weak(asked, end, std::memory_order_release, std::memory_order_relaxed)) {}
        }
        const uint32_t next = next_.load(std::memory_order_relaxed);
        if (offset > next) {
            resend_.store(true, std::memory_order_release);
            return false;
        }
        if (end <= next) return true;   // already held: a resend overlapping what arrived
        const size_t skip = next - offset, count = length - 4 - skip;
        const size_t pushed = ring_.push(packet + 4 + skip, count);
        next_.store(next + static_cast<uint32_t>(pushed), std::memory_order_release);
        if (pushed == count) return true;
        resend_.store(true, std::memory_order_release);
        return false;
    }

    /// Move up to `capacity` held bytes into `out`; `offset` is the image offset of the first.
    size_t take(uint8_t* out, size_t capacity, uint32_t& offset) MM_NONBLOCKING {
        const size_t count = ring_.pop(out, capacity);
        offset = taken_;
        taken_ += static_cast<uint32_t>(count);
        return count;
    }

    /// Whether to answer the phone now, `stored` being the bytes durably written: what it asked for is stored, or a dropped packet left the rest to resend. `asked` is the end the answered request named, so a phone tells an answer to its window from a late one.
    bool acknowledge(uint32_t stored, uint32_t& asked) MM_NONBLOCKING {
        asked = ackAt_.load(std::memory_order_acquire);
        if (!asked) return false;
        const bool resend = resend_.load(std::memory_order_acquire) && taken_ == next_.load(std::memory_order_acquire) && stored == taken_;
        if (stored < asked && !resend) return false;
        // A request raised meanwhile is answered on a later call.
        if (!ackAt_.compare_exchange_strong(asked, 0, std::memory_order_acq_rel)) return false;
        resend_.store(false, std::memory_order_release);
        return true;
    }

private:
    SpscRing<uint8_t, 8192> ring_;
    std::atomic<bool> open_{false};
    std::atomic<uint64_t> owner_{0};   ///< the generation of the link that began the transfer
    std::atomic<uint32_t> next_{0};    ///< the image offset after the last byte held; the link writes it
    uint32_t taken_ = 0;               ///< the image offset of the next byte to take; the loop's own
    std::atomic<uint32_t> ackAt_{0};   ///< answer once the store reaches this offset; 0 when nothing was asked
    std::atomic<bool> resend_{false};  ///< a packet was dropped, so the answer names what is stored
};

}  // namespace mm
