// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace mm {
/// Adds an authorization gate to a FirmwareUploadSession backend: Policy::verify(size, digest) must check a trusted, target-specific release authorization, since an image hash alone does not authenticate a publisher.
template <typename Backend, typename Policy>
class FirmwareAuthorizedBackend {
public:
    /// Borrows the backend and the policy, which both outlive this adapter; the caller serializes access.
    FirmwareAuthorizedBackend(Backend& backend, Policy& policy) : backend_(backend), policy_(policy) {}
    /// An adapter borrows its dependencies and cannot be copied.
    FirmwareAuthorizedBackend(const FirmwareAuthorizedBackend&) = delete;
    FirmwareAuthorizedBackend& operator=(const FirmwareAuthorizedBackend&) = delete;
    /// The bytes the backend holds.
    size_t capacity() const { return backend_.capacity(); }
    /// Whether this adapter still holds the underlying writer, which a failed finalization or cleanup leaves true.
    bool active() const { return active_; }
    /// Ask the policy to approve an image of `size` bytes with this digest; one approval admits one begin.
    bool authorize(size_t size, const std::array<uint8_t, 32>& digest) {
        approved_ = false;
        if (active_ || !policy_.verify(size, digest)) return false;
        approvedSize_ = size;
        approved_ = true;
        return true;
    }
    /// Open the backend for exactly the size the policy approved, spending the approval.
    bool begin(size_t size) {
        const bool allowed = approved_ && !active_ && size == approvedSize_;
        approved_ = false;
        if (!allowed || !backend_.begin(size)) return false;
        active_ = true;
        return true;
    }
    /// Pass bytes to the backend while a write is open.
    bool write(const uint8_t* data, size_t size) { return active_ && backend_.write(data, size); }
    /// Finish the backend's write, committing or dropping it; a failed finish keeps the adapter locked.
    bool end(bool commit) {
        if (!active_) return false;
        // A failed finalization does not prove the writer was released, so ownership stays until cleanup succeeds or the device resets.
        if (!backend_.end(commit)) return false;
        active_ = false;
        return true;
    }
    bool abort() requires requires(Backend& backend) { backend.abort(); } {
        if (!active_) return false;
        // Keep this adapter locked if cleanup failed: the session may be gone, but the underlying writer is still uncertain.
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
