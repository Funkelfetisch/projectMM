#pragma once

#include "core/util/FirmwareReleasePolicy.h"

#include <array>
#include <cstdint>
#include <span>

/// The S3 migration's decisions, pure so a host test drives them; moonbase/migrate-s3 runs them on the board.
///
/// A 2025 WLED S3 board installs the migrator through WLED's own update page. The migrator receives a signed MoonLight image over HTTP, then rewrites the partition table to MoonLight's dual-OTA layout and boots MoonLight. Each step re-reads what the flash holds, so a power cut anywhere except the table sector write starts it over.
namespace mm::helio::migration {

/// A partition's identity and geometry, including flags that affect raw writes.
struct Partition {
    /// Partition type, app 0 or data 1.
    uint8_t type;
    /// Partition subtype within its type.
    uint8_t subtype;
    /// First byte in flash.
    uint32_t offset;
    /// Length in bytes.
    uint32_t size;
    /// Partition flags, such as encrypted.
    uint32_t flags = 0;
    constexpr bool operator==(const Partition&) const = default;
};

/// The table a 2025 WLED S3 board carries, read from the 2025 updater's merged images; labels have no bearing on raw addresses.
inline constexpr std::array<Partition, 6> kWled = {{
    {1, 2, 0x9000, 0x5000}, {1, 0, 0xe000, 0x2000},
    {0, 0x10, 0x10000, 0x200000}, {0, 0x11, 0x210000, 0x200000},
    {1, 0x82, 0x410000, 0x3e0000}, {1, 3, 0x7f0000, 0x10000}
}};

/// MoonLight's 8 MB dual-OTA layout, esp32/partitions/esp32s3_n8r8.csv.
inline constexpr std::array<Partition, 5> kMoonLight = {{
    {1, 2, 0x9000, 0x5000}, {1, 0, 0xe000, 0x2000},
    {0, 0x10, 0x10000, 0x300000}, {0, 0x11, 0x310000, 0x300000},
    {1, 0x82, 0x610000, 0x1f0000}
}};

/// Whether a table is exactly the expected one, so an extra or encrypted entry refuses the migration before any write.
inline bool matches(std::span<const Partition> actual, std::span<const Partition> expected) {
    if (actual.size() != expected.size()) return false;
    for (size_t i = 0; i < actual.size(); ++i) if (actual[i] != expected[i]) return false;
    return true;
}

/// The largest image WLED's update handler gives Arduino's Update: its 2 MB slot less one 4 KB sector.
inline constexpr uint32_t kWledUploadLimit = 0x1ff000;
/// Where MoonLight lands: the new app1 (ota_1), which no partition of WLED's table describes.
inline constexpr uint32_t kPayloadOffset = 0x310000, kPayloadSize = 0x300000;
/// The new filesystem, which MoonLight mounts as LittleFS.
inline constexpr uint32_t kFilesystemOffset = 0x610000, kFilesystemSize = 0x1f0000;

/// Whether a staged MoonLight image is the release this migrator installs: signed by the publisher for this product, at least the compiled security version, within the payload slot, with the claimed size and SHA-256.
inline bool payloadAcceptable(const FirmwareReleaseClaims& claims, uint32_t productTarget, uint32_t minimumSecurity,
                              const std::array<uint8_t, 65>& publisherKey, uint32_t stagedSize,
                              const std::array<uint8_t, 32>& stagedDigest) {
    return claims.imageSize <= kPayloadSize &&
           FirmwareReleasePolicy(claims, productTarget, minimumSecurity, publisherKey).verify(stagedSize, stagedDigest);
}

/// The battery voltage the NEBULITE S3 battery controller's divider reports, as WLED's battery usermod computes it: ADC volts times 1.66, plus a 0.96 V offset.
inline uint32_t batteryMillivolts(uint32_t adcMillivolts) { return adcMillivolts * 166 / 100 + 960; }

/// What a battery reading says about running the migration.
enum class BatteryVerdict : uint8_t { Allows, Low, Unknown };

/// The lowest battery voltage the migration starts on, about a quarter of a LiPo's charge, so the table write never meets a collapsing cell.
inline constexpr uint32_t kMinimumBatteryMv = 3600;

/// A reading outside a LiPo's range is Unknown: the divider's calibration is not qualified on every board, so an implausible number neither refuses nor allows on its own evidence.
inline BatteryVerdict batteryVerdict(uint32_t batteryMv) {
    if (batteryMv < 3000 || batteryMv > 4500) return BatteryVerdict::Unknown;
    return batteryMv < kMinimumBatteryMv ? BatteryVerdict::Low : BatteryVerdict::Allows;
}

/// What an upload chunk at `offset` does against the bytes already `received` of a `size`-byte image.
enum class ChunkVerdict : uint8_t { Write, Duplicate, Offset, Length };

/// The next contiguous bytes are written, a chunk already held is a retry after a lost reply, and any other offset is refused with the count to continue from. Every chunk but the last fills whole 4 KB sectors, so each write erases only its own.
inline ChunkVerdict chunkVerdict(uint32_t received, uint32_t offset, uint32_t length, uint32_t size, uint32_t maxChunk) {
    const uint64_t end = uint64_t(offset) + length;
    if (!length || length > maxChunk) return ChunkVerdict::Length;
    if (end <= received) return ChunkVerdict::Duplicate;
    if (offset != received) return ChunkVerdict::Offset;
    if (end > size || (end < size && length % 4096)) return ChunkVerdict::Length;
    return ChunkVerdict::Write;
}

/// The writes and checks the migration runs, supplied by the board's adapter or by a simulated flash in a test.
class Storage {
public:
    virtual ~Storage() = default;
    /// The network journal, captured from WLED's filesystem, is committed in NVS and reads back the same.
    virtual bool journalDurable() = 0;
    /// The staged image at the payload offset is the signed release.
    virtual bool payloadValid() = 0;
    /// The board has the power to finish: mains, or a battery the verdict allows.
    virtual bool powerReady() = 0;
    /// Both otadata sectors select app0, where the migrator runs, under either table.
    virtual bool pinRecovery() = 0;
    /// The new filesystem holds the network document.
    virtual bool writeNetwork() = 0;
    /// The network document reads back the same.
    virtual bool networkValid() = 0;
    /// MoonLight's table is written at 0x8000 and reads back the same: the one write a power cut cannot be recovered from without USB.
    virtual bool writeTable() = 0;
    /// The bootloader selects ota_1, MoonLight, under the new table.
    virtual bool activatePayload() = 0;
};

/// Where a run of the migration ends.
enum class Result : uint8_t { Refused, AwaitUpload, Failed, RebootRecovery, BootPayload };

/// Run every remaining step in order, re-reading the flash rather than trusting progress flags. Under WLED's table it ends by writing MoonLight's and rebooting the migrator, so the bootloader and IDF load the new table; under MoonLight's it selects MoonLight.
inline Result finish(Storage& storage, bool targetTableLoaded) {
    if (!storage.journalDurable()) return Result::Refused;
    if (!storage.payloadValid()) return Result::AwaitUpload;
    if (!storage.powerReady()) return Result::Refused;
    if (!storage.pinRecovery() || !storage.writeNetwork() || !storage.networkValid()) return Result::Failed;
    if (!targetTableLoaded)
        return storage.writeTable() ? Result::RebootRecovery : Result::Failed;
    return storage.activatePayload() ? Result::BootPayload : Result::Failed;
}

} // namespace mm::helio::migration
