#pragma once

#include "platform/platform.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace mm {
/// Existing platform OTA primitives, restricted to the inactive application slot.
struct PlatformFirmwareOta {
    static size_t capacity() { return platform::otaNextAppCapacity(); }
    static bool begin(platform::OtaWrite& writer, size_t length, char* status, size_t statusSize, uint32_t* written) {
        return platform::otaBegin(writer, platform::OtaTarget::App, length, status, statusSize, written);
    }
    static bool write(platform::OtaWrite& writer, const uint8_t* data, size_t length) {
        return platform::otaWrite(writer, data, length);
    }
    static bool end(platform::OtaWrite& writer, bool commit) { return platform::otaEnd(writer, commit); }
    static bool abort(platform::OtaWrite& writer) { return platform::otaAbortApp(writer); }
};

/// Owns the platform writer and its status storage until a commit or abort finishes.
template <typename Api = PlatformFirmwareOta>
class FirmwareOtaPort {
public:
    // The real platform writer already owns the shared HTTP/URL/OTA gate.
    // Wrap this port directly in FirmwareUploadSession, not in a second gate.
    static constexpr bool kUsesPlatformUpdateGate = std::is_same_v<Api, PlatformFirmwareOta>;
    FirmwareOtaPort() = default;
    FirmwareOtaPort(const FirmwareOtaPort&) = delete;
    FirmwareOtaPort& operator=(const FirmwareOtaPort&) = delete;
    ~FirmwareOtaPort() { if (writer_.impl) Api::end(writer_, false); }
    size_t capacity() const { return Api::capacity(); }
    bool begin(size_t length) {
        if (writer_.impl || !length) return false;
        written_ = 0; status_[0] = '\0';
        return Api::begin(writer_, length, status_, sizeof(status_), &written_);
    }
    bool write(const uint8_t* data, size_t length) {
        return writer_.impl && data && length && Api::write(writer_, data, length);
    }
    bool end(bool commit) { return writer_.impl && Api::end(writer_, commit); }
    bool abort() requires requires(platform::OtaWrite& writer) { Api::abort(writer); } {
        return writer_.impl && Api::abort(writer_);
    }
    const char* status() const { return status_; }
    uint32_t written() const { return written_; }
private:
    platform::OtaWrite writer_{};
    char status_[96] = {};
    uint32_t written_ = 0;
};
} // namespace mm
