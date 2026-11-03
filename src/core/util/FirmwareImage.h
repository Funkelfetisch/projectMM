#pragma once

/// What an ESP32 firmware image says about itself, read without ESP-IDF.
///
/// @technical
///
/// ## Why the layout is redefined here
///
/// The layout is a fixed on-disk format the bootloader parses: magic, chip id, then the app descriptor carrying the project name and version.
/// Those are the same bytes whether they arrive on a device or in a test.
/// Defining it here rather than including `esp_app_format.h` is what lets the vetting run in a host test, which matters because the code it guards erases a device's only recovery image.
/// `unit_FirmwareImage` pins the layout against real binaries built by the ESP32 toolchain.
///
/// ## The three ways an image can be wrong
///
/// `moonBaseRejection` tests them in the order they are cheapest to detect:
///
/// | Reason | What it usually is |
/// |--------|--------------------|
/// | not an image | a 404 body, an HTML error page, a `.zip` |
/// | the wrong chip | one MoonBase per chip, one paste apart, and a checksum will not catch it |
/// | not MoonBase | an app image, which sits beside it on the releases page |
///
/// ## Two names, because a device refuses a name it does not know
///
/// An image carries its ESP-IDF project name in its descriptor, and every install path checks it before a byte is written.
/// A device accepts only the names compiled into it, so a build that ships under a new name is refused by everything in the field.
/// The name therefore changes in two releases: v6.0.0 knows both and ships under the old one, and the release after it ships under the new one.
/// `kAppImageNames` and `kMoonBaseImageNames` hold the pair, first the name a build carries today.
/// The old name stays accepted, so a device can go back to v6.0.0 and the app slot keeps refusing an old recovery image.
///
/// One gap stays open, and MIGRATING names it: a device updated over the air from v5 keeps its v5 recovery image, which knows only the old recovery name.
/// Offered the new recovery image as an app, that one does not recognize it and writes it over the app, so only a cable recovers the device.
/// It closes per device once its recovery image is reinstalled.
///
/// ## A download that goes silent
///
/// ESP-IDF's OTA read reports a timeout as "still in progress", so a connection that stops delivering bytes without closing keeps an install waiting for good.
/// Both install loops, the app's and MoonBase's, therefore abort once no byte has arrived for `kDownloadStallMs`.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace mm::firmware {

/// Check buffered and incoming bytes against a slot and an optional declared length without overflow.
inline bool uploadFits(size_t written, size_t buffered, size_t incoming, size_t slotSize, size_t expected) {
    if (written > slotSize || buffered > slotSize - written || incoming > slotSize - written - buffered) return false;
    return !expected || (written <= expected && buffered <= expected - written && incoming <= expected - written - buffered);
}

/// A known-length transfer must contain exactly the announced image before it can commit.
inline bool uploadComplete(size_t written, size_t buffered, size_t expected) {
    return !expected || (written <= expected && buffered == expected - written);
}

/// First byte of every ESP32 image.
constexpr uint8_t kImageMagic = 0xE9;
/// Marks the app descriptor that follows the image and first segment headers.
constexpr uint32_t kDescMagic = 0xABCD5432;
/// Offset of the app descriptor: a 24-byte image header plus an 8-byte segment header.
constexpr size_t kDescOffset = 32;
/// Offset of the chip id within the image header.
constexpr size_t kChipIdOffset = 12;
/// Enough bytes to identify an image: through the descriptor's version and project name.
constexpr size_t kIdentifyBytes = kDescOffset + 96;

/// Optional custom descriptor immediately after ESP-IDF's 256-byte app descriptor.
/// Untagged historical images remain readable; a tag is identity metadata, not a signature.
constexpr size_t kVariantOffset = kDescOffset + 256;
constexpr size_t kVariantBytes = 40;
inline constexpr char kVariantMagic[8] = {'M','M','V','A','R','0','1','\0'};
enum class VariantStatus : uint8_t { Missing, Valid, Malformed };
struct VariantInfo { VariantStatus status = VariantStatus::Missing; char name[32] = {}; };

inline VariantInfo identifyVariant(const uint8_t* data, size_t length) {
    VariantInfo out;
    if (!data || length < kVariantOffset + sizeof(kVariantMagic) ||
        std::memcmp(data + kVariantOffset, kVariantMagic, sizeof(kVariantMagic))) return out;
    out.status = VariantStatus::Malformed;
    if (length < kVariantOffset + kVariantBytes) return out;
    const char* name = reinterpret_cast<const char*>(data + kVariantOffset + sizeof(kVariantMagic));
    size_t n = 0;
    for (; n < sizeof(out.name) && name[n]; ++n) {
        const char c = name[n];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return out;
    }
    if (!n || n == sizeof(out.name)) return out;
    std::memcpy(out.name, name, n);
    out.status = VariantStatus::Valid;
    return out;
}

/// Product variants reject other tagged variants; untagged releases remain compatible.
inline const char* variantRejection(const VariantInfo& variant, const char* expected) {
    if (variant.status == VariantStatus::Malformed) return "malformed firmware variant";
    if (variant.status == VariantStatus::Valid && expected && std::strcmp(variant.name, expected))
        return "firmware variant mismatch";
    return nullptr;
}

/// Every chip id `esp_chip_id_t` defines, so that no valid image is refused as being for another chip when the truth is that this table does not name it.
enum class ChipId : uint16_t {
    Esp32    = 0x0000,
    Esp32S2  = 0x0002,
    Esp32C3  = 0x0005,
    Esp32S3  = 0x0009,
    Esp32C2  = 0x000C,
    Esp32C6  = 0x000D,
    Esp32H2  = 0x0010,
    Esp32P4  = 0x0012,
    Esp32C5  = 0x0017,
    Esp32H4  = 0x001C,
    Esp32S31 = 0x0020,
    Invalid  = 0xFFFF,
};

/// What an image says about itself. Empty strings when the descriptor is absent or unreadable.
struct ImageInfo {
    bool   valid       = false;   ///< begins with the image magic
    bool   described   = false;   ///< carries a readable app descriptor
    ChipId chip        = ChipId::Invalid;  ///< which chip the image header names
    char   project[32] = {};      ///< one of `kAppImageNames` or `kMoonBaseImageNames`
    char   version[32] = {};      ///< the app version string the descriptor carries
};

/// Read what `buf` claims to be, never past `len`, so a truncated or hostile stream reports what it can.
inline ImageInfo identify(const uint8_t* buf, size_t len) {
    ImageInfo out;
    if (!buf || len == 0) return out;
    if (buf[0] != kImageMagic) return out;
    out.valid = true;
    if (len >= kChipIdOffset + 2) {
        uint16_t id = 0;
        std::memcpy(&id, buf + kChipIdOffset, 2);
        out.chip = static_cast<ChipId>(id);
    }
    if (len < kDescOffset + 96) return out;
    uint32_t magic = 0;
    std::memcpy(&magic, buf + kDescOffset, 4);
    if (magic != kDescMagic) return out;
    out.described = true;
    // Version at +16 and project name at +48, each a 32-byte field IDF may leave unterminated.
    std::memcpy(out.version, buf + kDescOffset + 16, 31);
    std::memcpy(out.project, buf + kDescOffset + 48, 31);
    return out;
}

/// The names this project's app image carries: @xref{two-names-because-a-device-refuses-a-name-it-does-not-know}.
inline constexpr const char* kAppImageNames[]      = {"MoonLight", "projectMM"};
/// The same pair for the recovery image, which an app slot must never receive.
inline constexpr const char* kMoonBaseImageNames[] = {"MoonLight-moonbase", "projectMM-moonbase"};

/// How long an install waits on a download delivering no bytes: @xref{a-download-that-goes-silent}.
inline constexpr uint32_t kDownloadStallMs = 60000;

/// Whether the descriptor field `name`, `cap` bytes that IDF may leave unterminated, is one of `names`.
template <size_t N>
inline bool namedAmong(const char* name, size_t cap, const char* const (&names)[N]) {
    for (const char* known : names) {
        if (std::strncmp(name, known, cap) == 0) return true;
    }
    return false;
}

/// Whether `name` is this project's app image.
inline bool isAppImage(const char* name, size_t cap = 32) { return namedAmong(name, cap, kAppImageNames); }

/// Whether `name` is this project's recovery image.
inline bool isMoonBaseImage(const char* name, size_t cap = 32) { return namedAmong(name, cap, kMoonBaseImageNames); }

/// The application upload must match this chip before its first flash write.
inline bool appImageChipMatches(const ImageInfo& info, ChipId chip) {
    return info.valid && info.chip == chip;
}

/// The reason this is not a MoonBase image for `chip`, or null when it is.
inline const char* moonBaseRejection(const ImageInfo& info, ChipId chip) {
    if (!info.valid)     return "not a firmware image";
    if (info.chip != chip) return "image is for another chip";
    if (!info.described) return "image carries no description";
    if (!isMoonBaseImage(info.project)) return "not a MoonBase image";
    return nullptr;
}

} // namespace mm::firmware
