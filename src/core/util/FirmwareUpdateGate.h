#pragma once

#include <atomic>

namespace mm {
/// Nonblocking exclusive reservation; the successful caller alone releases it.
class FirmwareUpdateGate {
public:
    /// A free gate that no caller holds.
    FirmwareUpdateGate() = default;
    /// A gate is one reservation, so it is never copied.
    FirmwareUpdateGate(const FirmwareUpdateGate&) = delete;
    FirmwareUpdateGate& operator=(const FirmwareUpdateGate&) = delete;
    /// Takes the reservation without blocking; false when another caller holds it.
    bool acquire() { return !held_.test_and_set(std::memory_order_acquire); }
    /// Gives the reservation up; only the caller whose acquire succeeded calls it.
    void release() { held_.clear(std::memory_order_release); }
    /// Whether any caller holds the reservation.
    bool busy() const { return held_.test(std::memory_order_acquire); }
private:
    std::atomic_flag held_ = ATOMIC_FLAG_INIT;
};

/// A single owner can pass a reservation to an asynchronous task by moving it.
class FirmwareUpdateLease {
public:
    /// A lease on `gate` that holds nothing until `acquire` succeeds.
    explicit FirmwareUpdateLease(FirmwareUpdateGate& gate) : gate_(&gate) {}
    /// A reservation has one owner, so a lease is never copied.
    FirmwareUpdateLease(const FirmwareUpdateLease&) = delete;
    FirmwareUpdateLease& operator=(const FirmwareUpdateLease&) = delete;
    /// Takes over the reservation and leaves the source holding nothing.
    FirmwareUpdateLease(FirmwareUpdateLease&& other) noexcept : gate_(other.gate_), held_(other.held_) {
        other.held_ = false;
    }
    FirmwareUpdateLease& operator=(FirmwareUpdateLease&& other) noexcept {
        if (this != &other) { release(); gate_ = other.gate_; held_ = other.held_; other.held_ = false; }
        return *this;
    }
    /// Gives the reservation back when this lease still holds it.
    ~FirmwareUpdateLease() { release(); }
    /// Reserves the gate through this lease; false when it already holds one or the gate is busy.
    bool acquire() {
        if (held_ || !gate_->acquire()) return false;
        held_ = true; return true;
    }
    /// Gives the reservation back when this lease holds it.
    void release() { if (held_) { held_ = false; gate_->release(); } }
    /// Whether this lease holds the reservation.
    bool held() const { return held_; }
private:
    FirmwareUpdateGate* gate_;
    bool held_ = false;
};
} // namespace mm
