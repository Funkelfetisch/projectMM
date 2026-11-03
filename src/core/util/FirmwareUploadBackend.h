#pragma once

#include <cstddef>
#include <cstdint>

namespace mm {
/// Adapts a sequential app-slot writer and an exclusive update gate to FirmwareUploadSession; Port::begin releases its resources on failure, Port::end on every return, and the shared gate covers every update transport. Gate and port outlive it.
template <typename Port, typename Gate>
class FirmwareUploadBackend {
    static_assert([] {
        if constexpr (requires { Port::kUsesPlatformUpdateGate; }) return !Port::kUsesPlatformUpdateGate;
        return true;
    }(), "A platform-gated port must be used directly; acquiring its gate twice rejects updates");
public:
    /// Bind the port and the gate by reference; neither is copied.
    FirmwareUploadBackend(Port& port, Gate& gate) : port_(port), gate_(gate) {}
    /// Not copyable: it holds the gate while an update is open.
    FirmwareUploadBackend(const FirmwareUploadBackend&) = delete;
    FirmwareUploadBackend& operator=(const FirmwareUploadBackend&) = delete;
    /// Abort an update still open, which releases the gate.
    ~FirmwareUploadBackend() { if (active_) end(false); }
    /// The capacity of the port's app slot, in bytes.
    size_t capacity() const { return port_.capacity(); }
    /// Take the gate and open the port for an image of `size` bytes; false when an update is open, the gate is busy or the port refuses.
    bool begin(size_t size) {
        if (active_ || !gate_.acquire()) return false;
        if (!port_.begin(size)) { gate_.release(); return false; }
        active_ = true; return true;
    }
    /// Append `size` bytes to the open update; false when none is open or the port fails.
    bool write(const uint8_t* bytes, size_t size) { return active_ && port_.write(bytes, size); }
    /// Close the open update, committing or discarding it, and release the gate; false when none is open.
    bool end(bool commit) {
        if (!active_) return false;
        const bool result = port_.end(commit);
        active_ = false; gate_.release(); return result;
    }
private:
    Port& port_;
    Gate& gate_;
    bool active_ = false;
};
} // namespace mm
