#pragma once

#include "core/util/JsonSink.h"
#include "core/util/ScratchBuffer.h"
#include "light/layers/Buffer.h"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace mm {
/// Lossy pull snapshot of the already-composited source, in output-index order.
/// The render task never waits for the reader. A new frame replaces the old one;
/// revision-pinned pages either describe one frame or report it changed.
class PreviewFrameSnapshot {
public:
    /// The pixel copy lives on the heap of the module that owns the snapshot, sized to the captured frame, so the module object stays small.
    explicit PreviewFrameSnapshot(MoonModule& owner) : pixels_(owner) {}
    static constexpr uint16_t kMaxLights = 512;
    static constexpr uint16_t kPageLights = 32;
    void invalidate() { generation_.fetch_add(1, std::memory_order_relaxed); }
    /// Called from the owner's prepare(), never on the encode task: hold room for `lights` (up to kMaxLights) and drop any captured frame.
    void reserve(size_t lights) {
        while (lock_.test_and_set(std::memory_order_acquire)) {}
        valid_ = false; count_ = 0;
        pixels_.resize((lights < kMaxLights ? lights : kMaxLights) * 3);
        lock_.clear(std::memory_order_release);
        invalidate();
    }
    /// Called only on the encode task while its source frame is stable.
    void captureIfRequested(const Buffer* source, const uint8_t whitePoint[3], uint32_t now) {
        if (!requested_.exchange(false, std::memory_order_acq_rel)) return;
        if (lock_.test_and_set(std::memory_order_acquire)) { requested_.store(true, std::memory_order_release); return; }
        capturedGeneration_ = generation_.load(std::memory_order_relaxed);
        tooMany_ = source && source->count() > kMaxLights;
        valid_ = source && source->data() && source->count() > 0 && source->channelsPerLight() >= 3 && !tooMany_;
        count_ = valid_ ? static_cast<uint16_t>(source->count()) : 0;
        if (valid_ && pixels_.count() < static_cast<size_t>(count_) * 3) { valid_ = false; count_ = 0; }  // prepare() has not sized it for this frame yet
        if (valid_) {
            const uint8_t* data = source->data(); const auto channels = source->channelsPerLight();
            for (uint16_t i = 0; i < count_; ++i) for (uint8_t c = 0; c < 3; ++c) {
                const uint8_t* pixel = data + static_cast<size_t>(i) * channels;
                const uint16_t white = channels >= 4 ? pixel[3] : 0;
                const uint16_t value = pixel[c] + (white * whitePoint[c] + 127) / 255;
                pixels_.data()[i * 3 + c] = static_cast<uint8_t>(value > 255 ? 255 : value);
            }
        }
        capturedAt_ = now; ++sequence_;
        lock_.clear(std::memory_order_release);
    }
    /// Called by RPC task. Hex keeps a 32-light page inside the 512-byte envelope.
    const char* write(JsonSink& sink, uint16_t offset, const char* expected, uint32_t now) {
        if (lock_.test_and_set(std::memory_order_acquire)) return "frame-busy";
        uint8_t page[kPageLights * 3]; uint16_t count = 0, end = 0; char revision[33]{};
        const char* error = nullptr;
        std::snprintf(revision, sizeof(revision), "%016llx%016llx", static_cast<unsigned long long>(capturedGeneration_), static_cast<unsigned long long>(sequence_));
        if (generation_.load(std::memory_order_relaxed) == capturedGeneration_ && tooMany_ && now - capturedAt_ <= 5000) error = "frame-too-large";
        else if (!valid_ || generation_.load(std::memory_order_relaxed) != capturedGeneration_ || now - capturedAt_ > 5000) error = "frame-pending";
        else if (offset && (!expected || std::strcmp(expected, revision))) error = "frame-changed";
        else if (offset >= count_) error = "frame-offset";
        else {
            count = count_; end = count - offset > kPageLights ? offset + kPageLights : count;
            std::memcpy(page, pixels_.data() + static_cast<size_t>(offset) * 3, static_cast<size_t>(end - offset) * 3);
        }
        lock_.clear(std::memory_order_release);
        if (error) {
            if (offset == 0 && !std::strcmp(error, "frame-pending")) requested_.store(true, std::memory_order_release);
            return error;
        }
        if (end == count) requested_.store(true, std::memory_order_release);
        sink.append("\"frame\":{\"revision\":"); sink.writeJsonString(revision);
        sink.appendf(",\"total\":%u,\"offset\":%u,\"rgb\":\"", static_cast<unsigned>(count), static_cast<unsigned>(offset));
        static constexpr char hex[] = "0123456789abcdef";
        for (uint16_t i = 0; i < (end - offset) * 3; ++i) { char byte[3] = {hex[page[i] >> 4], hex[page[i] & 15], 0}; sink.append(byte); }
        sink.append("\",\"next\":");
        if (end < count) sink.appendf("%u", static_cast<unsigned>(end)); else sink.append("null");
        sink.append("}");
        return sink.overflowed() ? "frame-too-large" : nullptr;
    }
private:
    std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
    std::atomic<bool> requested_{false};
    std::atomic<uint32_t> generation_{0};
    ScratchBuffer<uint8_t> pixels_;
    uint16_t count_ = 0;
    bool valid_ = false, tooMany_ = false;
    uint32_t capturedAt_ = 0, capturedGeneration_ = 0;
    uint64_t sequence_ = 0;
};
} // namespace mm
