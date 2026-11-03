#pragma once

#include "core/util/PresetStore.h"
#include "helio/BleSceneCatalog.h"

namespace mm {

/// Factory looks use the scene engine's native documents and the shared conditional preset store.
class ProductPresets {
public:
    static constexpr const char* kMarker = "/.config/presets/.product-v1";

    /// Product templates exist independently of the user's saved, renamed or deleted preset files.
    static void writeIndex(JsonSink& sink) {
        sink.append("{\"catalog\":"); sink.writeJsonString(BleSceneCatalog::revision());
        sink.append(",\"items\":[");
        bool comma = false;
        for (const auto& entry : product_scenes::kEntries) {
            if (comma) sink.append(",");
            sink.append("{\"id\":"); sink.writeJsonString(entry.id);
            sink.append(",\"name\":"); sink.writeJsonString(entry.name); sink.append("}");
            comma = true;
        }
        sink.append("]}");
    }

    /// A complete editable look, emitted without applying controls or rebuilding the runtime graph.
    static const char* writeDocument(JsonSink& sink, Scheduler& scheduler, const char* id) {
        const auto result = BleSceneCatalog::writeDocument(scheduler, id, BleSceneCatalog::revision(), "{}", sink, true);
        return result.ok ? nullptr : result.error;
    }

    /// A display label may contain a musical note, while preset filenames are
    /// intentionally ASCII-only. Keep the original catalogue label unchanged.
    static bool fileName(const char* label, char (&name)[PresetStore::kMaxNameLen]) {
        if (!label) return false;
        const bool note = std::strncmp(label, "\xe2\x99\xaa ", 4) == 0 ||
                          std::strncmp(label, "\xe2\x99\xab ", 4) == 0;
        const int length = note ? std::snprintf(name, sizeof(name), "Music - %s", label + 4)
                                : std::snprintf(name, sizeof(name), "%s", label);
        return length > 0 && static_cast<size_t>(length) < sizeof(name) && PresetStore::validName(name);
    }

    /// Seed missing factory looks once; the marker survives deliberate deletion of every preset.
    static const char* initialize(Scheduler& scheduler) {
        if (platform::fsExists(kMarker)) return nullptr;
        // Validate the entire set before creating anything. Do not silently
        // alias two different factory scenes to the same stored preset name.
        for (size_t i = 0; i < BleSceneCatalog::count(); ++i) {
            char name[PresetStore::kMaxNameLen];
            if (!fileName(product_scenes::kEntries[i].name, name)) return "invalid factory preset name";
            for (size_t j = 0; j < i; ++j) {
                char previous[PresetStore::kMaxNameLen];
                if (!fileName(product_scenes::kEntries[j].name, previous) || !std::strcmp(name, previous))
                    return "duplicate factory preset name";
            }
        }
        if (!platform::fsMkdir(PresetStore::kDirectory)) return "could not create preset directory";
        for (const auto& entry : product_scenes::kEntries) {
            char name[PresetStore::kMaxNameLen];
            if (!fileName(entry.name, name)) return "invalid factory preset name";
            char path[80]; PresetStore::pathFor(name, path, sizeof(path));
            if (platform::fsExists(path)) continue;
            JsonSink document;
            const auto seeded = BleSceneCatalog::writeSeedDocument(scheduler, entry.id, document);
            if (!seeded.ok) return seeded.error;
            const auto stored = PresetStore::put(name, document.data(), PresetStore::Mode::Create);
            if (stored.status == 412) continue;
            if (!stored) return stored.error;
            PresetStore::notifyChanged(scheduler, name);
        }
        const char* revision = BleSceneCatalog::revision();
        return platform::fsWriteAtomic(kMarker, revision, std::strlen(revision)) ? nullptr : "could not mark product presets initialized";
    }
};

} // namespace mm
