#pragma once

#include "core/util/PresetStore.h"
#include "helio/BleSceneCatalog.h"

#include <string>

namespace mm {

/// Factory looks use the scene engine's native documents and the shared conditional preset store.
class ProductPresets {
public:
    /// The factory scenes already offered as looks, one id per line.
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

    /// A display label may carry a musical note while preset filenames stay ASCII-only, so the note becomes `Music - `; the catalog label is left unchanged.
    static bool fileName(const char* label, char (&name)[PresetStore::kMaxNameLen]) {
        if (!label) return false;
        const bool note = std::strncmp(label, "\xe2\x99\xaa ", 4) == 0 ||
                          std::strncmp(label, "\xe2\x99\xab ", 4) == 0;
        const int length = note ? std::snprintf(name, sizeof(name), "Music - %s", label + 4)
                                : std::snprintf(name, sizeof(name), "%s", label);
        return length > 0 && static_cast<size_t>(length) < sizeof(name) && PresetStore::validName(name);
    }

    /// Offer each factory scene once as a look: a deleted look stays deleted, and a scene the catalog gains later is offered on the next boot.
    static const char* initialize(Scheduler& scheduler) {
        const std::string offered = offeredScenes();
        bool due = false;
        for (const auto& entry : product_scenes::kEntries) due = due || !wasOffered(offered, entry.id);
        if (!due) return nullptr;
        // Validate the entire set before creating anything, so two different factory scenes never alias to the same stored preset name.
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
        std::string marker;
        for (const auto& entry : product_scenes::kEntries) {
            marker.append(entry.id).append("\n");
            if (wasOffered(offered, entry.id)) continue;
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
        return platform::fsWriteAtomic(kMarker, marker.data(), marker.size()) ? nullptr : "could not mark product presets initialized";
    }

private:
    /// The marker's scene ids framed by newlines, so a lookup matches whole ids; an older marker held a catalog revision, which matches none, so every missing look is seeded once.
    static std::string offeredScenes() {
        std::string text(2048, '\0');
        const int n = platform::fsExists(kMarker) ? platform::fsRead(kMarker, text.data(), text.size()) : 0;
        text.resize(n > 0 ? static_cast<size_t>(n) : 0);
        return "\n" + text + "\n";
    }
    static bool wasOffered(const std::string& offered, const char* id) {
        return offered.find("\n" + std::string(id) + "\n") != std::string::npos;
    }
};

} // namespace mm
