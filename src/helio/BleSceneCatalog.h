#pragma once

#include "core/module/Scheduler.h"
#include "core/module/StateDocument.h"
#include "core/util/JsonSink.h"
#include "core/util/JsonUtil.h"
#include "core/util/ModuleFactory.h"
#include "helio/BleSceneData.h"
#include "helio/BleAdvertisementRegistry.h"
#include "core/util/BleAdvertisement.h"
#include "light/util/Palette.h"

#include <cstdint>
#include <charconv>
#include <cstring>
#include <memory>

namespace mm {

/// Complete product scenes shared with the dashboard, applied through the normal state-document engine.
class BleSceneCatalog {
    using Entry = product_scenes::Entry;
    struct Probe {
        std::unique_ptr<MoonModule> effect;
        std::unique_ptr<MoonModule> driver;
        json::JsonDoc exposed;
    };

public:
    /// Content revision covering scene identity, defaults and the browser's source data.
    static const char* revision() { return product_scenes::kRevision; }
    /// Number of scenes in this firmware's shared product catalog.
    static uint16_t count() { return sizeof(product_scenes::kEntries) / sizeof(Entry); }
    /// Whether a stable ID exists, independently of the active module graph.
    static bool exists(const char* id) { return find(id) != nullptr; }

    /// Every page leaves room for the cursor and the caller's closing brace.
    static constexpr size_t kPageTail = sizeof("],\"next\":65535}") - 1;

    /// As many whole scenes as the page holds, so a round trip is never spent on one small item; the cursor is the first scene's index and null marks the end.
    static const char* writePage(JsonSink& sink, uint16_t offset) {
        if (offset > count()) return "scene-offset";
        sink.append("\"catalog\":"); sink.writeJsonString(revision());
        sink.append(",\"scenes\":[");
        uint16_t next = offset;
        for (; next < count(); ++next) {
            const auto& entry = product_scenes::kEntries[next];
            char item[192]; JsonSink row(item, sizeof(item));
            if (next > offset) row.append(",");
            row.append("{\"id\":"); row.writeJsonString(entry.id);
            row.append(",\"name\":"); row.writeJsonString(entry.name);
            row.append(entry.audio ? ",\"audio\":true" : ",\"audio\":false");
            row.append(std::strstr(entry.controls, "\"white\"") ? ",\"rgbw\":true}" : ",\"rgbw\":false}");
            if (row.overflowed()) return "scene-page";
            // The first item always goes in, so every cursor advances; an oversized one shows as the caller's overflow.
            if (next > offset && row.size() + kPageTail > sink.room()) break;
            sink.append(item, row.size());
        }
        sink.append("],\"next\":");
        if (next < count()) sink.appendf("%u", static_cast<unsigned>(next));
        else sink.append("null");
        return nullptr;
    }

    /// As many control descriptors and options as the page holds; the cursor counts parts (a control, or one option of a list) and an option list continues on the next page under the same control name.
    static const char* writeInfo(JsonSink& sink, const char* id, uint16_t offset) {
        const Entry* entry = find(id);
        if (!entry) return "scene-id";
        Probe probe;
        if (const char* error = makeProbe(*entry, probe)) return error;
        uint16_t parts = 0;
        for (int i = probe.exposed.rootNode()->firstChild; i >= 0; i = probe.exposed.nodes[i].next) {
            const auto* control = descriptor(probe, *entry, probe.exposed.nodes[i].key);
            if (!control) return "scene-schema";
            parts = static_cast<uint16_t>(parts + (optionCount(*control) ? optionCount(*control) : 1));
        }
        if (offset > parts) return "scene-offset";
        sink.append("\"catalog\":"); sink.writeJsonString(revision());
        sink.append(",\"scene\":"); sink.writeJsonString(id);
        sink.append(",\"controls\":[");
        uint16_t part = 0, next = parts;
        bool first = true;
        for (int i = probe.exposed.rootNode()->firstChild; i >= 0 && next == parts; i = probe.exposed.nodes[i].next) {
            const auto& control = *descriptor(probe, *entry, probe.exposed.nodes[i].key);
            const uint16_t options = optionCount(control), span = options ? options : 1;
            if (part + span <= offset) { part = static_cast<uint16_t>(part + span); continue; }
            const uint16_t from = offset > part ? static_cast<uint16_t>(offset - part) : 0;
            char head[192]; JsonSink object(head, sizeof(head));
            if (!first) object.append(",");
            object.append("{\"name\":"); object.writeJsonString(control.name);
            if (const char* label = controlLabel(entry->id, control.name)) { object.append(",\"label\":"); object.writeJsonString(label); }
            object.append(",\"type\":"); object.writeJsonString(controlTypeName(control.type));
            object.append(",\"default\":"); writeValue(object, control);
            if (control.type != ControlType::Bool)
                object.appendf(",\"min\":%d,\"max\":%d", static_cast<int>(control.min),
                               static_cast<int>(options ? control.max - 1 : control.max));
            if (options) object.appendf(",\"optionCount\":%u,\"optionOffset\":%u,\"options\":[", options, from);
            if (object.overflowed()) return "scene-schema";
            // A descriptor never travels without its first option, so the head's size counts that option too.
            char item[128]; JsonSink row(item, sizeof(item));
            if (options && !writeOption(row, control, from, false)) return "scene-option";
            const size_t close = options ? 2 : 1;
            if (!first && object.size() + row.size() + close + kPageTail > sink.room()) { next = static_cast<uint16_t>(part + from); break; }
            sink.append(head, object.size());
            sink.append(item, row.size());
            for (uint16_t option = static_cast<uint16_t>(from + 1); option < options; ++option) {
                JsonSink more(item, sizeof(item));
                if (!writeOption(more, control, option, true)) return "scene-option";
                if (more.size() + close + kPageTail > sink.room()) { next = static_cast<uint16_t>(part + option); break; }
                sink.append(item, more.size());
            }
            sink.append(options ? "]}" : "}");
            first = false; part = static_cast<uint16_t>(part + span);
        }
        sink.append("],\"next\":");
        if (next < parts) sink.appendf("%u", static_cast<unsigned>(next));
        else sink.append("null");
        return nullptr;
    }

    /// Validate names, actual control types/ranges and the replaceable scene region without touching live state.
    static StateDocumentResult validate(Scheduler& scheduler, const char* id, const char* catalog, const char* controls) {
        const Entry* entry = find(id);
        if (!entry) return failure("scene-id");
        if (!catalog || std::strcmp(catalog, revision())) return failure("scene-catalog");
        MoonModule* effects = scheduler.firstByName("Effects");
        if (!region(effects)) return failure("scene-region");
        Probe probe;
        if (const char* error = makeProbe(*entry, probe)) return failure(error);
        return overrides(*entry, probe, controls);
    }

    /// Emit a complete native scene without changing the running graph; optional metadata describes the product editor.
    static StateDocumentResult writeDocument(Scheduler& scheduler, const char* id, const char* catalog,
                                              const char* controls, JsonSink& document, bool uiMetadata = false,
                                              const char* seedSource = nullptr) {
        auto checked = validate(scheduler, id, catalog, controls);
        if (!checked.ok) return checked;
        {
            const auto& entry = *find(id);
            Probe probe;
            if (const char* error = makeProbe(entry, probe)) return failure(error);
            checked = overrides(entry, probe, controls);
            if (!checked.ok) return checked;
            auto* effects = scheduler.firstByName("Effects");
            auto* layer = region(effects);
            std::unique_ptr<MoonModule> layerDefaults(ModuleFactory::create(layer->typeName()));
            if (!layerDefaults) return failure("scene-layer");
            layerDefaults->defineControls();
            document.append("{\"Effects\":{\"$patch\":\"replace\",\"enabled\":true,");
            for (uint8_t i = 0; i < effects->childCount(); ++i) {
                const auto* child = effects->child(i);
                if (child == layer || !child->isWiredByCode()) continue;
                document.writeJsonString(child->name()); document.append(":{\"enabled\":false},");
            }
            document.writeJsonString(layer->name());
            document.append(":{\"$patch\":\"replace\",\"enabled\":true,");
            writeControls(document, *layerDefaults, false, uiMetadata);
            for (uint8_t i = 0; i < layer->childCount(); ++i) {
                const auto* child = layer->child(i);
                if (!child->isWiredByCode() || std::strcmp(child->name(), kSceneName) == 0) continue;
                document.append(","); document.writeJsonString(child->name()); document.append(":{\"enabled\":false}");
            }
            document.append(",\"BLE Scene\":{\"type\":"); document.writeJsonString(entry.type);
            document.append(",\"enabled\":true,"); writeControls(document, *probe.effect, false, uiMetadata);
            document.append("}}}");
            if (entry.globalPalette) {
                document.append(",\"Drivers\":{\"palette\":"); writeValue(document, *descriptor(probe, entry, "palette"), uiMetadata); document.append("}");
            }
            if (uiMetadata) {
                document.append(",\"$origin\":{\"scene\":"); document.writeJsonString(entry.id);
                document.append(",\"catalog\":"); document.writeJsonString(revision());
                if (seedSource) { document.append(",\"seedSource\":"); document.writeJsonString(seedSource); }
                document.append("},\"$uiSchema\":{\"effectType\":"); document.writeJsonString(entry.type);
                document.append(",\"effectPath\":[\"Effects\","); document.writeJsonString(layer->name());
                document.append(",\"BLE Scene\"],\"controls\":[");
                bool comma = false;
                for (int i = probe.exposed.rootNode()->firstChild; i >= 0; i = probe.exposed.nodes[i].next) {
                    const auto& value = *descriptor(probe, entry, probe.exposed.nodes[i].key);
                    if (comma) document.append(",");
                    document.append("{\"name\":"); document.writeJsonString(value.name);
                    document.append(",\"type\":"); document.writeJsonString(controlTypeName(value.type));
                    document.appendf(",\"min\":%ld,\"max\":%ld,\"default\":", static_cast<long>(value.min),
                                     static_cast<long>(optionCount(value) ? value.max - 1 : value.max));
                    writeValue(document, value);
                    if (value.hidden) document.append(",\"hidden\":true");
                    if (entry.globalPalette && std::strcmp(value.name, "palette") == 0)
                        document.append(",\"path\":[\"Drivers\"]");
                    document.append("}"); comma = true;
                }
                document.append("]}");
            }
            document.append("}");
        }
        if (document.overflowed()) return failure("out of memory");
        return {};
    }

    /// Seed a missing card from its unique existing instance; ambiguous instances retain factory defaults.
    /// Disabled native effects have no remembered global palette, so only an active instance contributes Drivers.palette.
    static StateDocumentResult writeSeedDocument(Scheduler& scheduler, const char* id, JsonSink& document) {
        const Entry* entry = find(id);
        if (!entry) return failure("scene-id");
        MoonModule* instance = nullptr;
        bool ambiguous = false;
        findInstance(scheduler.firstByName("Effects"), entry->type, instance, ambiguous);
        JsonSink controls;
        controls.append("{");
        if (instance && !ambiguous) {
            Probe probe;
            if (const char* error = makeProbe(*entry, probe)) return failure(error);
            bool comma = false;
            for (int i = probe.exposed.rootNode()->firstChild; i >= 0; i = probe.exposed.nodes[i].next) {
                const char* name = probe.exposed.nodes[i].key;
                const bool globalPalette = entry->globalPalette && std::strcmp(name, "palette") == 0;
                if (globalPalette && !instance->effectivelyEnabled()) continue;
                const auto* expected = descriptor(probe, *entry, name);
                const auto* actual = control(globalPalette ? scheduler.firstByName("Drivers") : instance, name);
                if (!expected || !actual || actual->type != expected->type || !scalar(*actual)) return failure("scene-instance-control");
                if (comma) controls.append(",");
                controls.writeJsonString(name); controls.append(":"); writeValue(controls, *actual); comma = true;
            }
        }
        controls.append("}");
        if (controls.overflowed()) return failure("out of memory");
        return writeDocument(scheduler, id, revision(), controls.data(), document, true,
                             ambiguous ? "factory-ambiguous" : instance ? "existing-instance" : "factory");
    }

    /// Replace competing scene content through the same document emitted for offline product presets.
    static StateDocumentResult apply(Scheduler& scheduler, const char* id, const char* catalog, const char* controls, bool live) {
        JsonSink document;
        const auto written = writeDocument(scheduler, id, catalog, controls, document);
        if (!written.ok) return written;
        return applyStateDocument(scheduler, document.data(), live ? StateSource::Live : StateSource::Request);
    }

    /// Recognize actual complete scene content; an externally composed or altered baseline has no canonical scene.
    static void writeState(JsonSink& sink, Scheduler& scheduler) {
        sink.append("\"scene\":");
        Probe probe;
        MoonModule* effect = nullptr;
        MoonModule* drivers = nullptr;
        const Entry* entry = recognized(scheduler, probe, effect, drivers);
        if (!entry) { sink.append("null"); return; }
        sink.append("{\"id\":"); sink.writeJsonString(entry->id);
        sink.append(",\"catalog\":"); sink.writeJsonString(revision()); sink.append(",\"controls\":{");
        bool comma = false;
        for (int i = probe.exposed.rootNode()->firstChild; i >= 0; i = probe.exposed.nodes[i].next) {
            const char* name = probe.exposed.nodes[i].key;
            const auto* actual = control(entry->globalPalette && std::strcmp(name, "palette") == 0 ? drivers : effect, name);
            if (comma) sink.append(",");
            sink.writeJsonString(name); sink.append(":"); writeValue(sink, *actual); comma = true;
        }
        sink.append("}}");
    }

    /// Observe only complete recognized content, never a remembered request or first-effect summary.
    static void advertisement(BleAdvertisementSnapshot& scan, Scheduler& scheduler) {
        Probe probe;
        MoonModule* effect = nullptr;
        MoonModule* drivers = nullptr;
        const Entry* entry = recognized(scheduler, probe, effect, drivers);
        if (!entry) return;
        scan.animation = ble_advertisement::sceneCode(entry->id);
        const auto observed = [&](const char* key, int32_t& value) {
            // The scene's exposed contract decides whether a same-named native control is meaningful.
            if (!json::member(probe.exposed, probe.exposed.rootNode(), key)) return false;
            return integer(control(entry->globalPalette && std::strcmp(key, "palette") == 0 ? drivers : effect, key), value);
        };
        scan.speedKnown = observed("speed", scan.speed);
        // bpm is deliberately not relabelled speed: those are distinct native controls.
        const bool red = observed("red", scan.red);
        const bool green = observed("green", scan.green);
        const bool blue = observed("blue", scan.blue);
        scan.rgbKnown = red && green && blue;
        scan.whiteKnown = observed("white", scan.white);
        int32_t picker = 0;
        if (observed("palette", picker) && picker >= 0 && picker < palettes::kCount)
            scan.palette = ble_advertisement::paletteCode(palettes::kBuiltins[picker].name);
    }

private:
    static const Entry* recognized(Scheduler& scheduler, Probe& probe, MoonModule*& effect, MoonModule*& drivers) {
        auto* layer = activeLayer(scheduler.firstByName("Effects"));
        effect = singleEffect(layer);
        if (!effect || !effect->effectivelyEnabled()) return nullptr;
        const Entry* entry = nullptr;
        for (const auto& candidate : product_scenes::kEntries)
            if (std::strcmp(candidate.type, effect->typeName()) == 0) { entry = &candidate; break; }
        if (!entry || makeProbe(*entry, probe) || !baseline(*effect, *probe.effect, probe.exposed) || !layerBaseline(*layer)) return nullptr;
        drivers = scheduler.firstByName("Drivers");
        if (entry->globalPalette && !control(drivers, "palette")) return nullptr;
        return entry;
    }
    static bool integer(const ControlDescriptor* value, int32_t& result) {
        if (!value || !value->ptr) return false;
        switch (value->type) {
            case ControlType::Uint8: case ControlType::Select: case ControlType::Palette:
                result = *static_cast<const uint8_t*>(value->ptr); return true;
            case ControlType::Uint16: result = *static_cast<const uint16_t*>(value->ptr); return true;
            case ControlType::Int16: result = *static_cast<const int16_t*>(value->ptr); return true;
            case ControlType::Int32: result = *static_cast<const int32_t*>(value->ptr); return true;
            default: return false;
        }
    }
    static constexpr const char* kSceneName = "BLE Scene";
    static const Entry* find(const char* id) {
        for (const auto& entry : product_scenes::kEntries)
            if (id && std::strcmp(id, entry.id) == 0) return &entry;
        return nullptr;
    }
    static void findInstance(MoonModule* module, const char* type, MoonModule*& instance, bool& ambiguous) {
        if (!module || ambiguous) return;
        if (module->role() == ModuleRole::Effect && std::strcmp(module->typeName(), type) == 0) {
            if (instance) { ambiguous = true; return; }
            instance = module;
        }
        for (uint8_t i = 0; i < module->childCount() && !ambiguous; ++i)
            findInstance(module->child(i), type, instance, ambiguous);
    }
    static StateDocumentResult failure(const char* error) { StateDocumentResult result; result.ok = false; result.error = error; return result; }
    static bool scalar(const ControlDescriptor& control) {
        return !control.readonly && (control.type == ControlType::Uint8 || control.type == ControlType::Uint16 ||
            control.type == ControlType::Int16 || control.type == ControlType::Int32 || control.type == ControlType::Bool ||
            control.type == ControlType::Select || control.type == ControlType::Palette);
    }
    static const ControlDescriptor* control(const MoonModule* module, const char* name) {
        for (uint8_t i = 0; module && i < module->controls().count(); ++i)
            if (std::strcmp(module->controls()[i].name, name) == 0) return &module->controls()[i];
        return nullptr;
    }
    /// The display name the catalogue gives a scene's control, or null where the control keeps its own name.
    static const char* controlLabel(const char* scene, const char* control) {
        for (const auto& row : product_scenes::kControlLabels)
            if (std::strcmp(row.scene, scene) == 0 && std::strcmp(row.control, control) == 0) return row.label;
        return nullptr;
    }
    static const ControlDescriptor* descriptor(const Probe& probe, const Entry& entry, const char* name) {
        return control(entry.globalPalette && std::strcmp(name, "palette") == 0 ? probe.driver.get() : probe.effect.get(), name);
    }
    static uint16_t optionCount(const ControlDescriptor& control) {
        return control.type == ControlType::Select || control.type == ControlType::Palette ? static_cast<uint16_t>(control.max) : 0;
    }
    /// One `{"value":n,"label":...}` option, comma-led when it follows another; false when the label does not fit.
    static bool writeOption(JsonSink& sink, const ControlDescriptor& control, uint16_t option, bool comma) {
        char label[96] = {};
        if (control.type == ControlType::Select) {
            const auto* labels = reinterpret_cast<const char* const*>(control.aux);
            if (labels && labels[option]) std::snprintf(label, sizeof(label), "%s", labels[option]);
        } else if (control.aux) {
            JsonSink name(label, sizeof(label)); name.requestName(static_cast<uint8_t>(option));
            reinterpret_cast<PaletteOptionsFn>(control.aux)(name);
            if (name.overflowed()) return false;
        }
        sink.appendf("%s{\"value\":%u,\"label\":", comma ? "," : "", static_cast<unsigned>(option));
        sink.writeJsonString(label); sink.append("}");
        return !sink.overflowed();
    }
    static void writeValue(JsonSink& sink, const ControlDescriptor& control, bool named = false) {
        auto value = control;
        value.persistLabel = named && (control.type == ControlType::Select || control.type == ControlType::Palette);
        writeControlValue(sink, value, named);
    }
    static const char* assign(const ControlDescriptor* target, const json::JsonDoc& source, const json::JsonNode* value) {
        if (!target || !scalar(*target)) return "scene-control";
        if (target->type == ControlType::Bool ? value->type != json::JsonType::Bool :
            value->type != json::JsonType::Int || value->decimal) return "scene-value";
        if (target->type != ControlType::Bool) {
            int64_t number = 0;
            const auto parsed = std::from_chars(value->str, value->str + value->numLen, number);
            if (parsed.ec != std::errc{} || parsed.ptr != value->str + value->numLen || number < target->min ||
                number > (optionCount(*target) ? target->max - 1 : target->max)) return "scene-range";
        }
        char bytes[64]; JsonSink wrapped(bytes, sizeof(bytes)); wrapped.append("{\"value\":"); json::write(wrapped, source, value); wrapped.append("}");
        return !wrapped.overflowed() && applyControlValue(*target, bytes, "value", ApplyPolicy::Strict) == ApplyResult::Ok ? nullptr : "scene-value";
    }
    static const char* makeProbe(const Entry& entry, Probe& probe) {
        probe.effect.reset(ModuleFactory::create(entry.type));
        if (!probe.effect || probe.effect->role() != ModuleRole::Effect) return "scene-unavailable";
        probe.effect->defineControls();
        if (entry.globalPalette) {
            probe.driver.reset(ModuleFactory::create("Drivers"));
            if (!probe.driver) return "scene-palette";
            probe.driver->defineControls();
        }
        if (!json::parse(entry.controls, probe.exposed)) return "out of memory";
        for (int i = probe.exposed.rootNode()->firstChild; i >= 0; i = probe.exposed.nodes[i].next)
            if (const char* error = assign(descriptor(probe, entry, probe.exposed.nodes[i].key), probe.exposed, &probe.exposed.nodes[i])) return error;
        return nullptr;
    }
    static StateDocumentResult overrides(const Entry& entry, Probe& probe, const char* text) {
        json::JsonDoc document;
        if (!text || !json::parse(text, document)) return failure("scene-controls");
        if (!document.rootNode() || document.rootNode()->type != json::JsonType::Object) return failure("scene-controls");
        for (int i = document.rootNode()->firstChild; i >= 0; i = document.nodes[i].next) {
            const auto& value = document.nodes[i];
            if (!json::member(probe.exposed, probe.exposed.rootNode(), value.key)) return failure("scene-control");
            for (int j = value.next; j >= 0; j = document.nodes[j].next)
                if (std::strcmp(value.key, document.nodes[j].key) == 0) return failure("scene-control");
            if (const char* error = assign(descriptor(probe, entry, value.key), document, &value)) return failure(error);
        }
        return {};
    }
    static MoonModule* region(MoonModule* effects) {
        if (!effects) return nullptr;
        MoonModule* sole = nullptr;
        MoonModule* preferred = nullptr;
        uint8_t layers = 0;
        for (uint8_t i = 0; i < effects->childCount(); ++i) {
            auto* child = effects->child(i);
            if (child->role() != ModuleRole::Layer) return nullptr;
            sole = child; ++layers;
            if (std::strcmp(child->name(), "Layer") == 0) preferred = child;
        }
        return preferred ? preferred : layers == 1 ? sole : nullptr;
    }
    static MoonModule* activeLayer(MoonModule* effects) {
        MoonModule* found = nullptr;
        for (uint8_t i = 0; effects && i < effects->childCount(); ++i) {
            auto* child = effects->child(i);
            if (!child->effectivelyEnabled()) continue;
            if (child->role() != ModuleRole::Layer || found) return nullptr;
            found = child;
        }
        return found;
    }
    static MoonModule* singleEffect(MoonModule* layer) {
        MoonModule* found = nullptr;
        for (uint8_t i = 0; layer && i < layer->childCount(); ++i) {
            auto* child = layer->child(i);
            if (!child->effectivelyEnabled()) continue;
            if (child->role() != ModuleRole::Effect || found || child->childCount()) return nullptr;
            found = child;
        }
        return found;
    }
    static bool sameValue(const ControlDescriptor* left, const ControlDescriptor* right) {
        if (!left || !right || left->type != right->type) return false;
        char a[64], b[64]; JsonSink first(a, sizeof(a)), second(b, sizeof(b)); writeValue(first, *left); writeValue(second, *right);
        return !first.overflowed() && !second.overflowed() && std::strcmp(a, b) == 0;
    }
    static bool baseline(const MoonModule& actual, const MoonModule& expected, const json::JsonDoc& exposed) {
        for (uint8_t i = 0; i < expected.controls().count(); ++i) {
            const auto& reference = expected.controls()[i];
            const auto* value = control(&actual, reference.name);
            if (!value || value->type != reference.type) return false;
            if (scalar(reference) && !json::member(exposed, exposed.rootNode(), reference.name) && !sameValue(value, &reference)) return false;
        }
        return actual.controls().count() == expected.controls().count();
    }
    static bool layerBaseline(const MoonModule& layer) {
        std::unique_ptr<MoonModule> expected(ModuleFactory::create(layer.typeName()));
        if (!expected) return false;
        expected->defineControls();
        for (uint8_t i = 0; i < expected->controls().count(); ++i)
            if (scalar(expected->controls()[i]) && !sameValue(control(&layer, expected->controls()[i].name), &expected->controls()[i])) return false;
        return true;
    }
    static void writeControls(JsonSink& sink, const MoonModule& module, bool comma, bool named = false) {
        for (uint8_t i = 0; i < module.controls().count(); ++i) {
            const auto& value = module.controls()[i];
            if (!scalar(value)) continue;
            if (comma) sink.append(",");
            sink.writeJsonString(value.name); sink.append(":"); writeValue(sink, value, named); comma = true;
        }
    }
};

} // namespace mm
