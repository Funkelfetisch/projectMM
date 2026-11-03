#pragma once

#include <cstring>

#include "core/util/JsonSink.h"
#include "core/util/build_info.h"
#include "helio/ProductProfiles.h"
#include "platform/platform.h"

/// The product setup choices the BLE `setup` op offers: the generated catalogue entries that name the running firmware and chip, so a garment updated over Bluetooth can be set up without Wi-Fi.
namespace mm::ble_product_profiles {

/// Whether a comma-separated firmware list names `firmware` as a whole entry.
inline bool lists(const char* list, const char* firmware) {
    const size_t length = firmware ? std::strlen(firmware) : 0;
    if (!list || !length) return false;
    for (const char* at = list; *at;) {
        const char* end = std::strchr(at, ',');
        const size_t entry = end ? static_cast<size_t>(end - at) : std::strlen(at);
        if (entry == length && std::strncmp(at, firmware, length) == 0) return true;
        if (!end) break;
        at = end + 1;
    }
    return false;
}

/// An entry fits when its catalogue row names this firmware and this chip.
inline bool fits(const product_profiles::Entry& entry, const char* firmware, const char* chip) {
    return lists(entry.firmwares, firmware) && chip && std::strcmp(entry.chip, chip) == 0;
}

/// The System/Drivers/Layouts document of `model` for this firmware and chip, or null when it does not fit.
inline const char* stateFor(const char* model, const char* firmware, const char* chip) {
    if (!model) return nullptr;
    for (const auto& entry : product_profiles::kEntries)
        if (fits(entry, firmware, chip) && std::strcmp(entry.name, model) == 0) return entry.state;
    return nullptr;
}

/// The names that fit this firmware and chip, as a JSON array.
inline void writeNames(JsonSink& sink, const char* firmware, const char* chip) {
    sink.append("[");
    bool comma = false;
    for (const auto& entry : product_profiles::kEntries) {
        if (!fits(entry, firmware, chip)) continue;
        if (comma) sink.append(",");
        sink.writeJsonString(entry.name);
        comma = true;
    }
    sink.append("]");
}

/// The running build's view, the form `BleControlService::setProductProfiles` takes.
inline const char* state(const char* model) { return stateFor(model, kFirmwareName, platform::chipModel()); }
inline void names(JsonSink& sink) { writeNames(sink, kFirmwareName, platform::chipModel()); }

}  // namespace mm::ble_product_profiles
