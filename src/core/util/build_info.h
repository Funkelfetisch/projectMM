#pragma once

#include <cstring>

/// Every fact about which binary this is, as the build system knew it.
///
/// `build_info.cpp` defines them all, the one unit that sees the build flags and the generated `build_id.h`, so a new commit or build recompiles that unit and no other.

namespace mm {

/// The semver: `library.json`'s, or the release pipeline's `MM_VERSION`, `<next>-dev.<N>` on the moving `latest` channel.
extern const char* const kVersion;
/// When it was built, for a human rather than for identity.
extern const char* const kBuildDate;
/// The short git hash it was built from, `+` when the tree was dirty, `nogit` without a checkout: what landed on a board.
extern const char* const kBuildId;
/// The compiled-binary variant (`MM_FIRMWARE_NAME`), such as `esp32-eth`, or `unknown` for a local desktop build.
extern const char* const kFirmwareName;
/// Product variants share the dashboard, scene catalogue and BLE bootstrap.
inline bool isProductFirmware(const char* name) {
    return name && (std::strcmp(name, "helio1") == 0 || std::strcmp(name, "nebulite-s3") == 0 ||
                    std::strcmp(name, "nebulite-esp32") == 0);
}

/// The NEBULITE garments, worn on a battery and driven by their own app: the radio they boot with is BLE alone. The family by its name prefix, the same rule `isNebuliteFirmware` in the dashboard applies, so a garment added later needs neither edited.
inline bool isNebuliteFirmware(const char* name) {
    return name && std::strncmp(name, "nebulite", 8) == 0;
}

/// The release channel it shipped on (`MM_RELEASE`), such as `latest` or `v1.0.0`, empty for a local build.
extern const char* const kRelease;

} // namespace mm
