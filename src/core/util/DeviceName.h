#pragma once

#include <cstddef>
#include <cstdint>

#include "platform/nonblocking.h"   // MM_NONBLOCKING: the app's tick1s calls it

namespace mm {

/// A prefix, `MM` unless a product names its own, a hyphen and the last two bytes of the base MAC, shared by the app and MoonBase, so a board's access point carries the name it has in MoonLight.

/// Write the default name for `mac` into `out`, empty when `out` cannot hold the prefix and five more bytes.
inline void defaultDeviceName(const uint8_t mac[6], char* out, size_t len, const char* prefix = "MM") MM_NONBLOCKING {
    size_t n = 0;
    while (prefix[n]) ++n;
    if (len < n + 6) { if (len) out[0] = 0; return; }
    constexpr char kHex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < n; ++i) out[i] = prefix[i];
    out[n] = '-';
    out[n + 1] = kHex[mac[4] >> 4]; out[n + 2] = kHex[mac[4] & 15];
    out[n + 3] = kHex[mac[5] >> 4]; out[n + 4] = kHex[mac[5] & 15];
    out[n + 5] = 0;
}

}  // namespace mm
