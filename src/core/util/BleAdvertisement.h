#pragma once

#include <array>
#include <cstdint>

namespace mm {
/// Public scan hints; every absent observation keeps its independent unknown marker.
struct BleAdvertisementProduct {
    /// Product identity from the append-only registry; zero is unclassified.
    uint8_t code = 0;
    uint8_t diameterCm = 0;  ///< Explicit physical metadata from the product profile.
};
struct BleAdvertisementSnapshot {
    /// The product the advertisement names.
    BleAdvertisementProduct product;
    /// Lights on the fixture; zero, or more than 65535, is left out of the advertisement.
    uint32_t ledCount = 0;
    /// Whether the power state was observed, and whether it is on.
    bool powerKnown = false, powerOn = false;
    /// Whether the RGBW kind was observed, and whether the fixture is RGBW.
    bool rgbwKnown = false, rgbw = false;
    /// Scene identity from the append-only registry; zero is unknown.
    uint8_t animation = 0;
    /// Whether the speed was observed; `speed` then carries it.
    bool speedKnown = false;
    int32_t speed = 0;       ///< Native integer units; negative values retain their sign.
    /// Whether the color was observed; red, green and blue then carry it.
    bool rgbKnown = false;
    /// The color channels, each advertised only within 0 to 255.
    int32_t red = 0, green = 0, blue = 0;
    /// Whether the white level was observed; it is advertised for an RGBW fixture only.
    bool whiteKnown = false;
    /// The white level, advertised only within 0 to 255.
    int32_t white = 0;
    /// Palette identity from the append-only registry; 255 is unknown.
    uint8_t palette = 255;
};
inline bool bleAdvertisementByte(int32_t value) { return value >= 0 && value <= 255; }
inline bool bleAdvertisementProductCode(uint8_t code) {
    return code <= 2 || (code >= 16 && code <= 23);
}
inline std::array<uint8_t, 13> encodeBleAdvertisement(const BleAdvertisementSnapshot& value) {
    std::array<uint8_t, 13> bytes{};
    bytes[0] = 1;
    if (value.powerKnown) bytes[1] |= static_cast<uint8_t>(1 | (value.powerOn ? 2 : 0));
    if (value.rgbwKnown) bytes[1] |= static_cast<uint8_t>(4 | (value.rgbw ? 8 : 0));
    bytes[2] = bleAdvertisementProductCode(value.product.code) ? value.product.code : 0;
    if (value.ledCount > 0 && value.ledCount <= 65535) {
        bytes[3] = static_cast<uint8_t>(value.ledCount);
        bytes[4] = static_cast<uint8_t>(value.ledCount >> 8);
    }
    bytes[5] = bytes[2] == 1 || bytes[2] == 2 ? value.product.diameterCm : 0;
    bytes[6] = value.animation;
    if (value.speedKnown && value.speed >= -32768 && value.speed <= 32767) {
        bytes[1] |= 16;
        const uint16_t speed = static_cast<uint16_t>(value.speed);
        bytes[7] = static_cast<uint8_t>(speed);
        bytes[8] = static_cast<uint8_t>(speed >> 8);
    }
    if (value.rgbKnown && bleAdvertisementByte(value.red) && bleAdvertisementByte(value.green) && bleAdvertisementByte(value.blue)) {
        bytes[1] |= 32;
        const uint16_t color = static_cast<uint16_t>(((value.red >> 3) << 11) | ((value.green >> 2) << 5) | (value.blue >> 3));
        bytes[9] = static_cast<uint8_t>(color);
        bytes[10] = static_cast<uint8_t>(color >> 8);
    }
    if (value.rgbwKnown && value.rgbw && value.whiteKnown && bleAdvertisementByte(value.white)) { bytes[1] |= 64; bytes[11] = static_cast<uint8_t>(value.white); }
    bytes[12] = value.palette;
    return bytes;
}
}  // namespace mm
