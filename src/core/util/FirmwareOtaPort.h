#pragma once

#include "platform/platform.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace mm {
/// Existing platform OTA primitives, restricted to the inactive application slot.
struct PlatformFirmwareOta {
    /// The bytes the inactive application slot holds.
    static size_t capacity() { return platform::otaNextAppCapacity(); }
    /// Open the inactive application slot for `length` bytes, writing the status text and the running byte count through the given storage.
    static bool begin(platform::OtaWrite& writer, size_t length, char* status, size_t statusSize, uint32_t* written) {
        return platform::otaBegin(writer, platform::OtaTarget::App, length, status, statusSize, written);
    }
    /// Append `length` bytes to the open slot.
    static bool write(platform::OtaWrite& writer, const uint8_t* data, size_t length) {
        return platform::otaWrite(writer, data, length);
    }
    /// Finish the write, making the slot bootable on a commit and dropping it otherwise.
    static bool end(platform::OtaWrite& writer, bool commit) { return platform::otaEnd(writer, commit); }
    /// Abandon the write and leave the running application as it was.
    static bool abort(platform::OtaWrite& writer) { return platform::otaAbortApp(writer); }
};

/// Owns the platform writer and its status storage until a commit or abort finishes.
template <typename Api = PlatformFirmwareOta>
class FirmwareOtaPort {
public:
    /// The real platform writer already owns the shared HTTP/URL/OTA gate, so wrap this port directly in FirmwareUploadSession, not in a second gate.
    static constexpr bool kUsesPlatformUpdateGate = std::is_same_v<Api, PlatformFirmwareOta>;
    /// An idle port with no open write.
    FirmwareOtaPort() = default;
    /// A port owns its writer and cannot be copied.
    FirmwareOtaPort(const FirmwareOtaPort&) = delete;
    FirmwareOtaPort& operator=(const FirmwareOtaPort&) = delete;
    /// Drops an unfinished write without committing it.
    ~FirmwareOtaPort() { if (writer_.impl) Api::end(writer_, false); }
    /// The bytes the target slot holds.
    size_t capacity() const { return Api::capacity(); }
    /// Open a write of `length` bytes and reset the status and count; false while a write is open or for an empty image.
    bool begin(size_t length) {
        if (writer_.impl || !length) return false;
        written_ = 0; status_[0] = '\0';
        return Api::begin(writer_, length, status_, sizeof(status_), &written_);
    }
    /// Append bytes to the open write; false when no write is open or nothing is given.
    bool write(const uint8_t* data, size_t length) {
        return writer_.impl && data && length && Api::write(writer_, data, length);
    }
    /// Finish the open write, committing the image or dropping it.
    bool end(bool commit) { return writer_.impl && Api::end(writer_, commit); }
    /// Abandon the open write when the platform supports it.
    bool abort() requires requires(platform::OtaWrite& writer) { Api::abort(writer); } {
        return writer_.impl && Api::abort(writer_);
    }
    /// The platform's status text for the last begin.
    const char* status() const { return status_; }
    /// The bytes the platform has written so far.
    uint32_t written() const { return written_; }
private:
    platform::OtaWrite writer_{};
    char status_[96] = {};
    uint32_t written_ = 0;
};
} // namespace mm
