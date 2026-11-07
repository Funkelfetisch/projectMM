/// @module S3Migration

#include "doctest.h"
#include "helio/S3Migration.h"
#include "platform/platform.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace mm {
namespace {
namespace migration = helio::migration;

enum class Table : uint8_t { Wled, MoonLight, Erased };

struct BootRecord {
    uint32_t sequence = 1;
    bool valid = true;
    bool operator==(const BootRecord&) const = default;
};

struct MigrationFlash {
    Table table = Table::Wled;
    std::array<BootRecord, 2> boot = {{{3, true}, {1, true}}};
    std::array<uint8_t, 64> payload{};
    std::array<uint8_t, 64> signedPayload{};
    std::string journalNetwork = "private test network";
    std::string filesystem;
    bool journalCommitted = true, adequatePower = true;

    MigrationFlash() {
        for (size_t index = 0; index < payload.size(); ++index)
            payload[index] = signedPayload[index] = static_cast<uint8_t>(index * 7);
    }

    int bootSlot() const {
        if (table == Table::Erased) return -1;
        uint32_t newest = 0;
        for (const auto& record : boot) if (record.valid) newest = std::max(newest, record.sequence);
        return newest ? static_cast<int>((newest - 1) % 2) : 0;
    }

    bool payloadReady() const { return payload == signedPayload; }
    bool networkReady() const { return filesystem == journalNetwork; }
};

class MigrationStorageForTest final : public migration::Storage {
public:
    explicit MigrationStorageForTest(MigrationFlash& flash) : flash_(flash) {}

    int cutAfter = -1, checkpoints = 0;
    bool failPin = false, failNetworkReadback = false, failTableReadback = false, failActivation = false;
    int tableWrites = 0, activationAttempts = 0;

    bool journalDurable() override { return flash_.journalCommitted; }
    bool payloadValid() override { return flash_.payloadReady(); }
    bool powerReady() override { return flash_.adequatePower; }

    bool pinRecovery() override {
        if (failPin) return false;
        const size_t newest = flash_.boot[0].valid &&
            (!flash_.boot[1].valid || flash_.boot[0].sequence >= flash_.boot[1].sequence) ? 0 : 1;
        for (const size_t sector : {1 - newest, newest}) {
            flash_.boot[sector].valid = false;
            if (!checkpoint()) return false;
            flash_.boot[sector] = {1, true};
            if (!checkpoint()) return false;
        }
        return true;
    }

    bool writeNetwork() override {
        flash_.filesystem.clear();
        if (!checkpoint()) return false;
        flash_.filesystem = flash_.journalNetwork.substr(0, flash_.journalNetwork.size() / 2);
        if (!checkpoint()) return false;
        flash_.filesystem = flash_.journalNetwork;
        return checkpoint();
    }

    bool networkValid() override { return !failNetworkReadback && flash_.networkReady(); }

    bool writeTable() override {
        ++tableWrites;
        flash_.table = Table::Erased;
        if (!checkpoint()) return false;
        flash_.table = Table::MoonLight;
        if (!checkpoint()) return false;
        return !failTableReadback;
    }

    bool activatePayload() override {
        ++activationAttempts;
        if (failActivation || flash_.table != Table::MoonLight ||
            !flash_.payloadReady() || !flash_.networkReady()) return false;
        for (auto& record : flash_.boot) {
            record.valid = false;
            if (!checkpoint()) return false;
            record = {2, true};
            if (!checkpoint()) return false;
        }
        return true;
    }

private:
    bool checkpoint() { return ++checkpoints != cutAfter; }
    MigrationFlash& flash_;
};

migration::Result runUntilBoot(MigrationStorageForTest& storage, MigrationFlash& flash) {
    const auto first = migration::finish(storage, flash.table == Table::MoonLight);
    if (first != migration::Result::RebootRecovery) return first;
    CHECK(flash.bootSlot() == 0);
    return migration::finish(storage, true);
}
}

TEST_CASE("S3 migration refuses an uncommitted journal before changing flash") {
    MigrationFlash flash;
    flash.journalCommitted = false;
    const auto originalPayload = flash.payload;
    const auto originalBoot = flash.boot;
    MigrationStorageForTest storage(flash);
    CHECK(migration::finish(storage, false) == migration::Result::Refused);
    CHECK(storage.checkpoints == 0);
    CHECK(flash.table == Table::Wled);
    CHECK(flash.payload == originalPayload);
    CHECK(flash.boot == originalBoot);
    CHECK(flash.filesystem.empty());
}

TEST_CASE("S3 migration retains recovery when staged payload bytes differ from the signed release") {
    MigrationFlash flash;
    flash.payload[17] ^= 0x80;
    MigrationStorageForTest storage(flash);
    CHECK(migration::finish(storage, false) == migration::Result::AwaitUpload);
    CHECK(storage.checkpoints == 0);
    CHECK(flash.table == Table::Wled);
    CHECK(flash.bootSlot() == 0);
    CHECK_FALSE(flash.payloadReady());
}

TEST_CASE("S3 migration leaves flash untouched when the final power check fails") {
    MigrationFlash flash;
    flash.adequatePower = false;
    MigrationStorageForTest storage(flash);
    CHECK(migration::finish(storage, false) == migration::Result::Refused);
    CHECK(storage.checkpoints == 0);
    CHECK(flash.table == Table::Wled);
    CHECK(flash.bootSlot() == 0);
}

TEST_CASE("S3 migration stops before changing the table when recovery pinning or network readback fails") {
    for (const bool failPin : {false, true}) {
        MigrationFlash flash;
        MigrationStorageForTest storage(flash);
        storage.failPin = failPin;
        storage.failNetworkReadback = !failPin;
        CHECK(migration::finish(storage, false) == migration::Result::Failed);
        CHECK(flash.table == Table::Wled);
        CHECK(storage.tableWrites == 0);
        CHECK(storage.activationAttempts == 0);
        CHECK(flash.bootSlot() == 0);
    }
}

TEST_CASE("S3 migration loads the new table on a recovery reboot before activating MoonLight") {
    MigrationFlash flash;
    MigrationStorageForTest storage(flash);
    CHECK(migration::finish(storage, false) == migration::Result::RebootRecovery);
    CHECK(flash.table == Table::MoonLight);
    CHECK(flash.bootSlot() == 0);
    CHECK(storage.activationAttempts == 0);
    CHECK(flash.payloadReady());
    CHECK(flash.networkReady());
    MigrationStorageForTest rebooted(flash);
    CHECK(migration::finish(rebooted, true) == migration::Result::BootPayload);
    CHECK(rebooted.tableWrites == 0);
    CHECK(flash.bootSlot() == 1);
}

TEST_CASE("S3 migration never activates a payload after a partition table readback difference") {
    MigrationFlash flash;
    MigrationStorageForTest storage(flash);
    storage.failTableReadback = true;
    CHECK(migration::finish(storage, false) == migration::Result::Failed);
    CHECK(storage.activationAttempts == 0);
    CHECK(flash.bootSlot() == 0);
}

TEST_CASE("S3 migration keeps the verified recovery image selected after activation failure") {
    MigrationFlash flash;
    flash.table = Table::MoonLight;
    MigrationStorageForTest storage(flash);
    storage.failActivation = true;
    CHECK(migration::finish(storage, true) == migration::Result::Failed);
    CHECK(flash.bootSlot() == 0);
    CHECK(flash.payloadReady());
    CHECK(flash.networkReady());
}

TEST_CASE("S3 migration resumes every interrupted simulated flash substep except the table sector erase") {
    MigrationFlash baseline;
    MigrationStorageForTest uninterrupted(baseline);
    REQUIRE(runUntilBoot(uninterrupted, baseline) == migration::Result::BootPayload);
    REQUIRE(uninterrupted.checkpoints > 10);
    int usbRecoveryCases = 0;
    for (int cut = 1; cut <= uninterrupted.checkpoints; ++cut) {
        CAPTURE(cut);
        MigrationFlash flash;
        const auto originalPayload = flash.payload;
        const auto originalNetwork = flash.journalNetwork;
        MigrationStorageForTest interrupted(flash);
        interrupted.cutAfter = cut;
        CHECK(runUntilBoot(interrupted, flash) == migration::Result::Failed);
        CHECK(flash.payload == originalPayload);
        CHECK(flash.journalNetwork == originalNetwork);
        if (flash.bootSlot() < 0) {
            ++usbRecoveryCases;
            CHECK(flash.table == Table::Erased);
            CHECK(flash.networkReady());
            continue;
        }
        if (flash.bootSlot() == 0) {
            MigrationStorageForTest rebooted(flash);
            CHECK(runUntilBoot(rebooted, flash) == migration::Result::BootPayload);
        }
        CHECK(flash.bootSlot() == 1);
        CHECK(flash.payloadReady());
        CHECK(flash.networkReady());
        CHECK(flash.journalNetwork == originalNetwork);
    }
    CHECK(usbRecoveryCases == 1);
}

TEST_CASE("S3 migration accepts only the complete expected partition geometry and flags") {
    CHECK(migration::matches(migration::kWled, migration::kWled));
    CHECK(migration::matches(migration::kMoonLight, migration::kMoonLight));
    CHECK_FALSE(migration::matches(migration::kWled, migration::kMoonLight));
    for (size_t index = 0; index < migration::kWled.size(); ++index) {
        auto changed = migration::kWled;
        changed[index].flags = 1;
        CHECK_FALSE(migration::matches(changed, migration::kWled));
        changed = migration::kWled;
        changed[index].offset += 0x1000;
        CHECK_FALSE(migration::matches(changed, migration::kWled));
        changed = migration::kWled;
        changed[index].size -= 0x1000;
        CHECK_FALSE(migration::matches(changed, migration::kWled));
        changed = migration::kWled;
        changed[index].type ^= 1;
        CHECK_FALSE(migration::matches(changed, migration::kWled));
        changed = migration::kWled;
        changed[index].subtype ^= 1;
        CHECK_FALSE(migration::matches(changed, migration::kWled));
    }
    CHECK_FALSE(migration::matches(std::span(migration::kWled).first(5), migration::kWled));
    CHECK(migration::kWledUploadLimit == 2093056);
    CHECK(migration::kPayloadOffset + migration::kPayloadSize == migration::kFilesystemOffset);
    CHECK(migration::kFilesystemOffset + migration::kFilesystemSize == 0x800000);
}
}

namespace mm {
namespace {
// A throwaway P-256 key and its signature over claims for a 4096-byte nebulite-s3 image, security version 1; no release key.
constexpr std::array<uint8_t, 65> kTestKey = { 0x04, 0x3a, 0x04, 0x08, 0x26, 0x58, 0x0a, 0x09, 0x46, 0xa5, 0x27, 0x74, 0xad, 0xa0, 0xfd, 0xac, 0xc6, 0x39, 0xbe, 0x2e, 0xca, 0xe5, 0xa7, 0xec, 0x9e, 0x75, 0xc9, 0x90, 0x2b, 0xdf, 0x34, 0x33, 0xf0, 0xeb, 0xd1, 0xdd, 0xfd, 0x9d, 0xd3, 0x26, 0xc1, 0x60, 0xef, 0xe9, 0x01, 0xd4, 0x5e, 0x87, 0x45, 0x78, 0x73, 0x6c, 0x23, 0x2d, 0x50, 0x64, 0xb3, 0x6c, 0xf4, 0x4e, 0xbe, 0x9f, 0xed, 0x51, 0x60 };
constexpr std::array<uint8_t, 60> kTestClaims = { 0x48, 0x45, 0x4c, 0x49, 0x4f, 0x2d, 0x46, 0x57, 0x2d, 0x41, 0x55, 0x54, 0x48, 0x2d, 0x56, 0x31, 0x29, 0x1b, 0x7d, 0xd2, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x01, 0xd0, 0x10, 0xf6, 0xd7, 0x6d, 0x0e, 0xb4, 0xdc, 0xe5, 0xd5, 0xb5, 0xb3, 0x40, 0x14, 0xa8, 0xa1, 0x57, 0xec, 0x43, 0x80, 0xa6, 0x6c, 0x24, 0xd7, 0xd4, 0x55, 0xa9, 0xbf, 0x65, 0x2d, 0xb1, 0x4a };
constexpr std::array<uint8_t, 64> kTestSignature = { 0xe5, 0xd3, 0xa9, 0xac, 0x5d, 0xa0, 0x82, 0xc2, 0xd7, 0xc6, 0xd1, 0x7e, 0x6c, 0x9a, 0x9a, 0x24, 0x0e, 0xa7, 0xa3, 0x53, 0x24, 0x17, 0xf3, 0x06, 0x54, 0xfa, 0x16, 0xb3, 0x97, 0xaf, 0xfe, 0x62, 0x1e, 0xb0, 0xc0, 0xa3, 0x86, 0x1b, 0xbe, 0x1d, 0xd8, 0xcb, 0x5b, 0x1f, 0xf2, 0x77, 0xce, 0x92, 0xe6, 0x0a, 0xe8, 0xf2, 0x51, 0x97, 0x36, 0x74, 0xa7, 0x32, 0x49, 0xb0, 0xb8, 0x44, 0xa0, 0x9d };
constexpr uint32_t kGarmentTarget = 0x291b7dd2, kHelioTarget = 0x1ab87b7d;   // FNV-1a of "nebulite-s3" and "helio1"

// The desktop links no crypto library, so this stands in for P-256: it accepts the fixture key's signature over the fixture claims' SHA-256, and nothing else.
bool fixtureSignatureCheck(const uint8_t* key, size_t keySize, const uint8_t* digest, size_t digestSize,
                           const uint8_t* signature, size_t signatureSize) {
    Sha256 sha;
    std::array<uint8_t, 32> claimsDigest{};
    if (!sha.update(kTestClaims.data(), kTestClaims.size())) return false;
    sha.digest(claimsDigest.data());
    return keySize == kTestKey.size() && std::equal(key, key + keySize, kTestKey.begin()) &&
           digestSize == claimsDigest.size() && std::equal(digest, digest + digestSize, claimsDigest.begin()) &&
           signatureSize == kTestSignature.size() && std::equal(signature, signature + signatureSize, kTestSignature.begin());
}
struct FixtureSignature {
    FixtureSignature() { platform::setTestFirmwareSignatureCheck(&fixtureSignatureCheck); }
    ~FixtureSignature() { platform::setTestFirmwareSignatureCheck(nullptr); }
};

FirmwareReleaseClaims testClaims(std::array<uint8_t, 64> signature = kTestSignature) {
    FirmwareReleaseClaims claims;
    REQUIRE(decodeFirmwareReleaseClaims(kTestClaims.data(), kTestClaims.size(), signature.data(), signature.size(), claims));
    return claims;
}
}

TEST_CASE("S3 migration installs only its own product's release, signed, recent enough and staged byte for byte") {
    FixtureSignature signatures;
    const auto claims = testClaims();
    const auto digest = claims.imageDigest;
    CHECK(migration::payloadAcceptable(claims, kGarmentTarget, 1, kTestKey, 4096, digest));
    // A staged byte that differs changes the SHA-256.
    auto other = digest; other[0] ^= 1;
    CHECK_FALSE(migration::payloadAcceptable(claims, kGarmentTarget, 1, kTestKey, 4096, other));
    // A shorter or longer upload than the claims name.
    CHECK_FALSE(migration::payloadAcceptable(claims, kGarmentTarget, 1, kTestKey, 4095, digest));
    // A signature that is not the publisher's.
    auto forged = kTestSignature; forged[10] ^= 0x40;
    CHECK_FALSE(migration::payloadAcceptable(testClaims(forged), kGarmentTarget, 1, kTestKey, 4096, digest));
    // A garment release on the HELIO migrator.
    CHECK_FALSE(migration::payloadAcceptable(claims, kHelioTarget, 1, kTestKey, 4096, digest));
    // A release older than the compiled security version.
    CHECK_FALSE(migration::payloadAcceptable(claims, kGarmentTarget, 2, kTestKey, 4096, digest));
}

TEST_CASE("S3 migration reads the battery as WLED does and starts only on enough charge, leaving an implausible reading undecided") {
    // WLED's battery usermod: ADC volts times 1.66, plus 0.96 V.
    CHECK(migration::batteryMillivolts(1950) == 4197);
    CHECK(migration::batteryVerdict(migration::batteryMillivolts(1950)) == migration::BatteryVerdict::Allows);
    CHECK(migration::batteryVerdict(3599) == migration::BatteryVerdict::Low);
    CHECK(migration::batteryVerdict(3600) == migration::BatteryVerdict::Allows);
    CHECK(migration::batteryVerdict(migration::batteryMillivolts(0)) == migration::BatteryVerdict::Unknown);
    CHECK(migration::batteryVerdict(4600) == migration::BatteryVerdict::Unknown);
}
TEST_CASE("S3 migration writes the next contiguous chunk, answers a retried one, and names where an out-of-order one should continue") {
    using migration::ChunkVerdict;
    constexpr uint32_t size = 3 * 16384 + 100, chunk = 16384;
    CHECK(migration::chunkVerdict(0, 0, chunk, size, chunk) == ChunkVerdict::Write);
    CHECK(migration::chunkVerdict(chunk, 0, chunk, size, chunk) == ChunkVerdict::Duplicate);       // the reply was lost
    CHECK(migration::chunkVerdict(chunk, 2 * chunk, chunk, size, chunk) == ChunkVerdict::Offset);  // a gap
    CHECK(migration::chunkVerdict(3 * chunk, 3 * chunk, 100, size, chunk) == ChunkVerdict::Write); // the short last one
    CHECK(migration::chunkVerdict(0, 0, chunk + 1, size, chunk) == ChunkVerdict::Length);          // over the maximum
    CHECK(migration::chunkVerdict(0, 0, 0, size, chunk) == ChunkVerdict::Length);
    CHECK(migration::chunkVerdict(0, 0, 1000, size, chunk) == ChunkVerdict::Length);               // not whole sectors, not the last
    CHECK(migration::chunkVerdict(3 * chunk, 3 * chunk, 200, size, chunk) == ChunkVerdict::Length); // past the end
    CHECK(migration::chunkVerdict(0, 0xfffff000u, chunk, size, chunk) == ChunkVerdict::Offset);   // no wrap-around
}

TEST_CASE("S3 migration joins WLED's station network only when WLED counts it as configured") {
    CHECK(migration::wledStationConfigured("MM-8200"));
    CHECK_FALSE(migration::wledStationConfigured("Your_Network"));
    CHECK_FALSE(migration::wledStationConfigured(""));
}
} // namespace mm
