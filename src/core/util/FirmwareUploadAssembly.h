#pragma once

#include "core/util/FirmwareUploadFrame.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace mm {
/// Bounded reassembly for a transport that supplies a frame length and offset.
/// This specifies no wire encoding and does not authorize or dispatch an update.
/// Calls must be serialized; owner is a unique connection generation, never zero.
class FirmwareUploadAssembly {
public:
    enum class Result { Pending, Ready, Invalid, Owner, Offset, Busy };

    explicit FirmwareUploadAssembly(uint32_t timeoutMs) : timeoutMs_(timeoutMs) {}

    Result append(uint64_t owner, size_t total, size_t offset,
                  const uint8_t* bytes, size_t size, uint32_t now) {
        expire(now);
        if (!owner || !bytes || !size || total < 2 || total > buffer_.size() ||
            offset > total || size > total - offset) return Result::Invalid;
        if (owner_ && owner_ != owner) return Result::Owner;
        if (ready_) return Result::Busy;
        if (!owner_) {
            if (offset) return Result::Offset;
            owner_ = owner; total_ = total; received_ = 0;
        }
        if (total != total_ || offset != received_) return Result::Offset;
        std::memcpy(buffer_.data() + received_, bytes, size);
        received_ += size; lastProgress_ = now;
        ready_ = received_ == total_;
        return ready_ ? Result::Ready : Result::Pending;
    }
    const uint8_t* data() const { return ready_ ? buffer_.data() : nullptr; }
    size_t size() const { return ready_ ? received_ : 0; }
    void release(uint64_t owner) { if (owner && owner == owner_) clear(); }
    bool expire(uint32_t now) {
        if (!owner_ || !timeoutMs_ || timeoutMs_ > uint32_t(std::numeric_limits<int32_t>::max()) ||
            uint32_t(now - lastProgress_) < timeoutMs_) return false;
        clear(); return true;
    }
private:
    void clear() { owner_ = 0; total_ = received_ = 0; ready_ = false; }
    std::array<uint8_t, FirmwareUploadFrame::kMaxBytes> buffer_{};
    uint64_t owner_ = 0;
    size_t total_ = 0, received_ = 0;
    uint32_t timeoutMs_, lastProgress_ = 0;
    bool ready_ = false;
};
} // namespace mm
