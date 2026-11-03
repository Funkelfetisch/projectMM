#pragma once

#include <cstddef>
#include <cstdint>

namespace mm {
/// Adapts a sequential app-slot writer and an exclusive update gate to FirmwareUploadSession.
/// Port::begin must release resources on failure; Port::end releases resources on every return.
/// Gate and port outlive this backend. The shared gate must cover every update transport.
template <typename Port, typename Gate>
class FirmwareUploadBackend {
    static_assert([] {
        if constexpr (requires { Port::kUsesPlatformUpdateGate; }) return !Port::kUsesPlatformUpdateGate;
        return true;
    }(), "A platform-gated port must be used directly; acquiring its gate twice rejects updates");
public:
    FirmwareUploadBackend(Port& port, Gate& gate) : port_(port), gate_(gate) {}
    FirmwareUploadBackend(const FirmwareUploadBackend&) = delete;
    FirmwareUploadBackend& operator=(const FirmwareUploadBackend&) = delete;
    ~FirmwareUploadBackend() { if (active_) end(false); }
    size_t capacity() const { return port_.capacity(); }
    bool begin(size_t size) {
        if (active_ || !gate_.acquire()) return false;
        if (!port_.begin(size)) { gate_.release(); return false; }
        active_ = true; return true;
    }
    bool write(const uint8_t* bytes, size_t size) { return active_ && port_.write(bytes, size); }
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
