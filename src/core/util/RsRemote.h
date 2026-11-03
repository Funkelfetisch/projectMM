#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <span>

namespace mm {

/// The observed SP648E handset keys; ON and OFF are independent absolute commands.
enum class RsKey : uint8_t {
    On, Off, Brighter, Dimmer, ModeUp, ModeDown, SpeedUp, SpeedDown,
    Auto, Music, Timer, ColorDown, White, ColorUp, Yellow, Cyan, Pink, Red, Green, Blue, Unknown
};
inline constexpr std::array<const char*, 20> kRsKeyNames = {
    "on", "off", "brighter", "dimmer", "mode+", "mode-", "speed+", "speed-",
    "auto", "music", "timer", "color-", "white", "color+", "yellow", "cyan", "pink", "red", "green", "blue"
};

/// One structurally recognized frame; source bytes and the signature do not authenticate a handset.
struct RsRemoteEvent {
    RsKey key = RsKey::Unknown;
    uint8_t counter = 0;
    std::array<uint8_t, 8> source{};
};

/// Decode raw payloads rather than BLE AD fields: the leading 0x52 is not an AD length.
/// Bytes 12/13/15 remain undecoded; this validates structure, not an application checksum.
inline bool decodeRsRemote(std::span<const uint8_t> data, RsRemoteEvent& event) {
    constexpr uint8_t prefix[] = {0x52, 0x53, 0x02, 0x00};
    constexpr uint8_t suffix[] = {0x02, 0x02, 0xff};
    if ((data.size() != 24 && data.size() != 26) || std::memcmp(data.data(), prefix, 4) != 0 ||
        std::memcmp(data.data() + 16, suffix, 3) != 0 || data[19] != data.size() - 20 ||
        data[20] != 3 || data[21] != 0) return false;
    event = {};
    event.counter = data[14];
    std::memcpy(event.source.data(), data.data() + 4, event.source.size());
    if (data.size() == 24) {
        const uint16_t command = static_cast<uint16_t>((data[22] << 8) | data[23]);
        switch (command) {
            case 0x0201: event.key = RsKey::On; break;
            case 0x0200: event.key = RsKey::Off; break;
            case 0x0901: event.key = RsKey::Brighter; break;
            case 0x0900: event.key = RsKey::Dimmer; break;
            case 0x0401: event.key = RsKey::ModeUp; break;
            case 0x0400: event.key = RsKey::ModeDown; break;
            case 0x0b01: event.key = RsKey::SpeedUp; break;
            case 0x0b00: event.key = RsKey::SpeedDown; break;
            case 0x0d02: event.key = RsKey::Auto; break;
            case 0x1202: event.key = RsKey::Music; break;
            case 0x2900: event.key = RsKey::Timer; break;
            case 0x2800: event.key = RsKey::ColorDown; break;
            case 0x2801: event.key = RsKey::ColorUp; break;
            default: break;
        }
    } else if (data[22] == 5) {
        const uint32_t rgb = (static_cast<uint32_t>(data[23]) << 16) | (data[24] << 8) | data[25];
        switch (rgb) {
            case 0xffff00: event.key = RsKey::Yellow; break;
            case 0x00ffff: event.key = RsKey::Cyan; break;
            case 0xff00ff: event.key = RsKey::Pink; break;
            case 0xff0000: event.key = RsKey::Red; break;
            case 0xffffff: event.key = RsKey::White; break;
            case 0x00ff00: event.key = RsKey::Green; break;
            case 0x0000ff: event.key = RsKey::Blue; break;
            default: break;
        }
    }
    return true;
}

/// Suppress whole retransmission bursts while admitting a changed counter/payload immediately.
/// The caller owns time in milliseconds; unsigned subtraction survives the device timer wrapping.
class RsRemoteDecoder {
public:
    bool receive(std::span<const uint8_t> data, uint32_t nowMs, RsRemoteEvent& event) {
        if (!decodeRsRemote(data, event)) return false;
        size_t oldest = 0;
        for (size_t i = 0; i < seen_.size(); ++i) {
            auto& s = seen_[i];
            if (s.used && s.length == data.size() && std::memcmp(s.bytes.data(), data.data(), data.size()) == 0) {
                const bool duplicate = nowMs - s.lastMs < 2000;
                s.lastMs = nowMs;
                return !duplicate;
            }
            if (!s.used || (seen_[oldest].used && nowMs - s.lastMs > nowMs - seen_[oldest].lastMs)) oldest = i;
        }
        auto& s = seen_[oldest];
        std::memcpy(s.bytes.data(), data.data(), data.size());
        s.length = static_cast<uint8_t>(data.size()); s.lastMs = nowMs; s.used = true;
        return true;
    }
private:
    struct Seen { std::array<uint8_t, 26> bytes{}; uint32_t lastMs = 0; uint8_t length = 0; bool used = false; };
    std::array<Seen, 32> seen_{};
};
}  // namespace mm
