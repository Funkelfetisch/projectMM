#pragma once

#include "core/util/JsonUtil.h"

#include <cstdio>
#include <cstring>

namespace mm::helio {

/// Translate the saved HELIO audio selector when its config predates the `levels` control.
/// The old selector is local / receive / simulate; MoonLight uses simulate / receive / local.
inline int migratedAudioMode(const char* savedServices) {
    if (!savedServices) return -1;
    for (unsigned i = 0; i < 256; i++) {
        char key[32];
        std::snprintf(key, sizeof(key), "%u.type", i);
        if (!json::hasKey(savedServices, key)) continue;
        char type[32] = {};
        json::parseString(savedServices, key, type, sizeof(type));
        if (std::strcmp(type, "AudioService") != 0) continue;

        std::snprintf(key, sizeof(key), "%u.levels", i);
        if (json::hasKey(savedServices, key)) return -1;
        std::snprintf(key, sizeof(key), "%u.sckPin", i);
        if (json::parseInt(savedServices, key) != 40) return -1;
        std::snprintf(key, sizeof(key), "%u.wsPin", i);
        if (json::parseInt(savedServices, key) != 38) return -1;
        std::snprintf(key, sizeof(key), "%u.sdPin", i);
        if (json::parseInt(savedServices, key) != 39) return -1;

        std::snprintf(key, sizeof(key), "%u.mode", i);
        if (!json::hasKey(savedServices, key)) return -1;
        const int oldMode = json::parseInt(savedServices, key);
        if (oldMode < 0 || oldMode > 2) return -1;
        return oldMode == 0 ? 2 : oldMode == 2 ? 0 : 1;
    }
    return -1;
}

} // namespace mm::helio
