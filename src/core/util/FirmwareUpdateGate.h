#pragma once

#include <atomic>

namespace mm {
/// Nonblocking exclusive reservation; the successful caller alone releases it.
class FirmwareUpdateGate {
public:
    FirmwareUpdateGate() = default;
    FirmwareUpdateGate(const FirmwareUpdateGate&) = delete;
    FirmwareUpdateGate& operator=(const FirmwareUpdateGate&) = delete;
    bool acquire() { return !held_.test_and_set(std::memory_order_acquire); }
    void release() { held_.clear(std::memory_order_release); }
    bool busy() const { return held_.test(std::memory_order_acquire); }
private:
    std::atomic_flag held_ = ATOMIC_FLAG_INIT;
};

/// A single owner can pass a reservation to an asynchronous task by moving it.
class FirmwareUpdateLease {
public:
    explicit FirmwareUpdateLease(FirmwareUpdateGate& gate) : gate_(&gate) {}
    FirmwareUpdateLease(const FirmwareUpdateLease&) = delete;
    FirmwareUpdateLease& operator=(const FirmwareUpdateLease&) = delete;
    FirmwareUpdateLease(FirmwareUpdateLease&& other) noexcept : gate_(other.gate_), held_(other.held_) {
        other.held_ = false;
    }
    FirmwareUpdateLease& operator=(FirmwareUpdateLease&& other) noexcept {
        if (this != &other) { release(); gate_ = other.gate_; held_ = other.held_; other.held_ = false; }
        return *this;
    }
    ~FirmwareUpdateLease() { release(); }
    bool acquire() {
        if (held_ || !gate_->acquire()) return false;
        held_ = true; return true;
    }
    void release() { if (held_) { held_ = false; gate_->release(); } }
    bool held() const { return held_; }
private:
    FirmwareUpdateGate* gate_;
    bool held_ = false;
};
} // namespace mm
