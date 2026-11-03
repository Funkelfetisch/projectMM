// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace mm {
/// Adds an authorization gate to a FirmwareUploadSession backend.
/// Policy::verify(size, digest) must verify a trusted, target-specific release
/// authorization; hashing image bytes alone does not authenticate a publisher.
/// Both dependencies outlive this adapter, and the caller serializes access.
template <typename Backend, typename Policy>
class FirmwareAuthorizedBackend {
public:
    FirmwareAuthorizedBackend(Backend& backend, Policy& policy) : backend_(backend), policy_(policy) {}
    FirmwareAuthorizedBackend(const FirmwareAuthorizedBackend&) = delete;
    FirmwareAuthorizedBackend& operator=(const FirmwareAuthorizedBackend&) = delete;
    size_t capacity() const { return backend_.capacity(); }
    /// Whether this adapter still holds the underlying writer, which a failed finalization or cleanup leaves true.
    bool active() const { return active_; }
    bool authorize(size_t size, const std::array<uint8_t, 32>& digest) {
        approved_ = false;
        if (active_ || !policy_.verify(size, digest)) return false;
        approvedSize_ = size;
        approved_ = true;
        return true;
    }
    bool begin(size_t size) {
        const bool allowed = approved_ && !active_ && size == approvedSize_;
        approved_ = false;
        if (!allowed || !backend_.begin(size)) return false;
        active_ = true;
        return true;
    }
    bool write(const uint8_t* data, size_t size) { return active_ && backend_.write(data, size); }
    bool end(bool commit) {
        if (!active_) return false;
        // A failed finalization does not prove that the writer was released.
        // Retain ownership until explicit cleanup succeeds or the device resets.
        if (!backend_.end(commit)) return false;
        active_ = false;
        return true;
    }
    bool abort() requires requires(Backend& backend) { backend.abort(); } {
        if (!active_) return false;
        // Keep this adapter locked if cleanup failed: the session reports the
        // error and may be gone, but the underlying writer is still uncertain.
        if (!backend_.abort()) return false;
        active_ = false;
        return true;
    }
private:
    Backend& backend_;
    Policy& policy_;
    size_t approvedSize_ = 0;
    bool approved_ = false;
    bool active_ = false;
};
} // namespace mm
