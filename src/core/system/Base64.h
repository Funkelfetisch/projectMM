#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace mm {

/// RFC 4648 with the standard alphabet and `=` padding.
///
/// @technical
///
/// Three places use it: the WebSocket handshake response, the password serialization in the state response, and the chunks of a BLE firmware update.
/// The handshake encodes a hash of the client key and the protocol's magic string, the password is obfuscated with an XOR before encoding, and the update decodes each image chunk.
/// Every payload is short, so both directions are straightforward rather than optimized.

/// Encode `in` into `out` as a null-terminated string, truncating rather than overflowing.
inline void base64Encode(std::span<const uint8_t> in, std::span<char> out) {
    if (out.empty()) return;  // no room even for the terminator
    static constexpr char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const size_t inLen = in.size();
    const size_t outMax = out.size();
    size_t oi = 0;
    for (size_t i = 0; i < inLen && oi + 4 < outMax; i += 3) {
        uint32_t n = static_cast<uint32_t>(in[i]) << 16;
        if (i + 1 < inLen) n |= static_cast<uint32_t>(in[i + 1]) << 8;
        if (i + 2 < inLen) n |= static_cast<uint32_t>(in[i + 2]);
        out[oi++] = table[(n >> 18) & 0x3F];
        out[oi++] = table[(n >> 12) & 0x3F];
        out[oi++] = (i + 1 < inLen) ? table[(n >> 6) & 0x3F] : '=';
        out[oi++] = (i + 2 < inLen) ? table[n & 0x3F] : '=';
    }
    out[oi] = 0;
}

/// The six-bit value of a standard-alphabet character, or -1 for any other.
inline int base64Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    return c == '+' ? 62 : c == '/' ? 63 : -1;
}

/// Decode one group of four characters, `symbols` of them data and the rest `=`, into `symbols - 1` bytes; false for another character or for unused bits that are not zero.
inline bool base64Group(const char* text, size_t symbols, uint8_t* out) {
    uint32_t n = 0;
    for (size_t i = 0; i < 4; ++i) {
        const int value = i < symbols ? base64Value(text[i]) : (text[i] == '=' ? 0 : -1);
        if (value < 0) return false;
        n = (n << 6) | static_cast<uint32_t>(value);
    }
    // The bits a short group leaves over must be zero: the only text that decodes to these bytes.
    const uint32_t unused = symbols == 3 ? 0xFFu : symbols == 2 ? 0xFFFFu : 0u;
    if (n & unused) return false;
    for (size_t i = 0; i + 1 < symbols; ++i) out[i] = static_cast<uint8_t>(n >> (16 - 8 * i));
    return true;
}

/// Decode canonical RFC 4648 text (padded, its unused bits zero) into `out`, true with the byte count in `decoded`; anything else, or output that does not fit, is false with `decoded` zero.
inline bool base64Decode(std::string_view in, std::span<uint8_t> out, size_t& decoded) {
    decoded = 0;
    if (in.size() % 4 != 0) return false;
    size_t padding = 0;
    if (!in.empty() && in.back() == '=') padding = (in.size() >= 2 && in[in.size() - 2] == '=') ? 2 : 1;
    const size_t length = in.size() / 4 * 3 - padding;
    if (length > out.size()) return false;
    for (size_t i = 0; i < in.size(); i += 4) {
        const size_t symbols = (i + 4 == in.size()) ? 4 - padding : 4;
        if (!base64Group(in.data() + i, symbols, out.data() + i / 4 * 3)) return false;
    }
    decoded = length;
    return true;
}

} // namespace mm
