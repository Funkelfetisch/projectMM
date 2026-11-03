#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace mm {
/// Experimental transfer frame, independent of BLE framing and connection ownership.
///
/// All multibyte numbers are little-endian. The caller owns the source buffer.
struct FirmwareUploadFrame {
    enum class Kind : uint8_t { Begin = 1, Chunk = 2, Finish = 3, Abort = 4, Status = 5, Resume = 6 };
    /// The longest frame a parse accepts, in bytes.
    static constexpr size_t kMaxBytes = 512;
    /// What the frame asks for.
    Kind kind{};
    /// Begin: total image length; Chunk: exact byte offset.
    uint32_t value = 0;
    /// The image digest a Begin carries.
    std::array<uint8_t, 32> digest{};
    /// The resume token a Begin, Resume or Status carries.
    std::array<uint8_t, 16> token{};
    /// The chunk bytes, pointing into the caller's buffer.
    const uint8_t* data = nullptr;
    /// How many bytes `data` holds.
    size_t length = 0;

    /// On failure the output is cleared; no bytes outside the input are read.
    static bool parse(const uint8_t* bytes, size_t size, FirmwareUploadFrame& out) {
        out = {};
        if (!bytes || size < 2 || size > kMaxBytes || bytes[0] != 1) return false;
        const uint8_t op = bytes[1];
        if (op < static_cast<uint8_t>(Kind::Begin) || op > static_cast<uint8_t>(Kind::Resume)) return false;
        FirmwareUploadFrame frame; frame.kind = static_cast<Kind>(op);
        if (frame.kind == Kind::Begin || frame.kind == Kind::Chunk) {
            if (size < 6) return false;
            frame.value = uint32_t(bytes[2]) | (uint32_t(bytes[3]) << 8) |
                          (uint32_t(bytes[4]) << 16) | (uint32_t(bytes[5]) << 24);
            if (frame.kind == Kind::Begin) {
                if ((size != 38 && size != 54) || !frame.value) return false;
                std::memcpy(frame.digest.data(), bytes + 6, frame.digest.size());
                if (size == 54) {
                    std::memcpy(frame.token.data(), bytes + 38, frame.token.size());
                    if (!hasToken(frame.token)) return false;
                }
            } else {
                if (size == 6) return false;
                frame.data = bytes + 6; frame.length = size - 6;
            }
        } else if (frame.kind == Kind::Resume || (frame.kind == Kind::Status && size == 18)) {
            if (size != 18) return false;
            std::memcpy(frame.token.data(), bytes + 2, frame.token.size());
            if (!hasToken(frame.token)) return false;
        } else if (size != 2) return false;
        out = frame; return true;
    }
private:
    static bool hasToken(const std::array<uint8_t, 16>& token) {
        for (uint8_t byte : token) if (byte) return true;
        return false;
    }
};
} // namespace mm
