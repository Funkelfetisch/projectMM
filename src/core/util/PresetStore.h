#pragma once

#include "core/module/Scheduler.h"
#include "core/module/StateDocument.h"
#include "core/util/JsonSink.h"
#include "core/util/JsonUtil.h"
#include "core/util/ModuleFactory.h"
#include "core/util/TryLock.h"
#include "core/util/sha256.h"
#include "platform/platform.h"

#include <cstdio>
#include <cstring>

namespace mm {

/// Native preset documents shared by control surfaces and HTTP, with conditional atomic writes.
class PresetStore {
public:
    static constexpr const char* kDirectory = "/.config/presets";
    static constexpr uint8_t kMaxNameLen = 32;
    static constexpr uint8_t kMaxPresets = 64;
    inline static constexpr const char* kCapturable[] = {"Layouts", "Effects", "Drivers", "Services"};
    inline static constexpr const char* kCaptureRole[] = {"layout", "effects", "driver", "service"};
    enum class Mode { Create, Match, Overwrite };

    struct Result {
        int status = 200;
        const char* error = "";
        bool created = false;
        bool applied = false;
        char etag[65] = {};
        char outputEtag[65] = {};
        StateDocumentResult state;
        explicit operator bool() const { return status >= 200 && status < 300; }
    };

    struct File {
        char* text = nullptr;
        size_t size = 0;
        char etag[65] = {};
        File() = default;
        ~File() { platform::free(text); }
        File(const File&) = delete;
        File& operator=(const File&) = delete;
    };

    /// A bounded membership snapshot; each item's content and tag are read separately when it is written.
    struct IndexSnapshot {
        char names[kMaxPresets][kMaxNameLen]; ///< Names in filesystem enumeration order, including their terminators.
        uint8_t count; ///< Valid names, at most kMaxPresets.
    };

    static bool validName(const char* name) {
        if (!name || !name[0] || std::strlen(name) >= kMaxNameLen) return false;
        for (const unsigned char* p = reinterpret_cast<const unsigned char*>(name); *p; ++p)
            if (*p < 0x20 || *p > 0x7e || *p == '/' || *p == '\\' || *p == '.') return false;
        return true;
    }

    static void pathFor(const char* name, char* path, size_t capacity) {
        std::snprintf(path, capacity, "%s/%s.json", kDirectory, name);
    }

    /// Publish the changed file; its consumers decide whether their displayed metadata needs a schema resync.
    static void notifyChanged(Scheduler& scheduler, const char* name) {
        char path[80];
        pathFor(name, path, sizeof(path));
        scheduler.notifyFileChanged(path);
    }

    static Result read(const char* name, File& out) {
        if (!validName(name)) return failure(400, "invalid preset name");
        LockGuard guard(lock_);
        if (!guard) return failure(503, "preset store is busy");
        return readUnlocked(name, out);
    }

    /// Create refuses an existing name; Match requires its strong content tag; Overwrite is an explicit control-surface save.
    static Result put(const char* name, const char* text, Mode mode, const char* etag = nullptr, bool lookOnly = true) {
        if (!validName(name)) return failure(400, "invalid preset name");
        if (mode == Mode::Match && (!etag || !etag[0])) return failure(428, "If-Match is required");
        const auto valid = validate(text, lookOnly);
        if (!valid) return valid;
        LockGuard guard(lock_);
        if (!guard) return failure(503, "preset store is busy");
        char path[80]; pathFor(name, path, sizeof(path));
        const bool exists = platform::fsExists(path);
        if (mode == Mode::Create && exists) return failure(412, "preset already exists");
        File previous;
        if (mode == Mode::Match) {
            const auto readResult = readUnlocked(name, previous);
            if (!readResult) return readResult.status == 404 ? failure(412, "preset changed or was deleted") : readResult;
            if (std::strcmp(etag, previous.etag)) return failure(412, "preset changed");
        }
        if (!exists) {
            unsigned count = 0;
            platform::fsList(kDirectory, countEntry, &count);
            if (count >= kMaxPresets) return failure(409, "preset list is full");
        }
        const size_t size = std::strlen(text);
        if (!platform::fsWriteAtomic(path, text, size)) return failure(507, "could not store preset");
        Result result;
        result.status = exists ? 200 : 201;
        result.created = !exists;
        sha256Hex(text, size, result.etag, kSha256DigestSize);
        return result;
    }

    static Result erase(const char* name, const char* etag, bool requireMatch = true) {
        if (!validName(name)) return failure(400, "invalid preset name");
        if (requireMatch && (!etag || !etag[0])) return failure(428, "If-Match is required");
        LockGuard guard(lock_);
        if (!guard) return failure(503, "preset store is busy");
        File file;
        const auto loaded = readUnlocked(name, file);
        if (!loaded) return loaded.status == 404 && requireMatch ? failure(412, "preset changed or was deleted") : loaded;
        if (requireMatch && std::strcmp(etag, file.etag)) return failure(412, "preset changed");
        char path[80]; pathFor(name, path, sizeof(path));
        return platform::fsRemove(path) ? Result{} : failure(507, "could not delete preset");
    }

    static Result rename(const char* from, const char* to) {
        if (!validName(from) || !validName(to)) return failure(400, "invalid preset name");
        if (std::strcmp(from, to) == 0) return {};
        LockGuard guard(lock_);
        if (!guard) return failure(503, "preset store is busy");
        File file;
        const auto loaded = readUnlocked(from, file);
        if (!loaded) return loaded;
        char destination[80]; pathFor(to, destination, sizeof(destination));
        if (platform::fsExists(destination)) return failure(409, "preset name is taken");
        if (!platform::fsWriteAtomic(destination, file.text, file.size)) return failure(507, "could not rename preset");
        char source[80]; pathFor(from, source, sizeof(source));
        if (!platform::fsRemove(source)) {
            platform::fsRemove(destination);
            return failure(507, "could not complete preset rename");
        }
        return {};
    }

    /// A look changes rendering content and palette settings; its file metadata grants no additional writes.
    static bool isLook(const json::JsonDoc& doc) {
        const auto* root = doc.rootNode();
        if (!root || root->type != json::JsonType::Object) return false;
        bool content = false;
        for (const auto* node = doc.node(root->firstChild); node; node = doc.node(node->next)) {
            if (node->key[0] == '$') continue;
            if (node->type != json::JsonType::Object) return false;
            if (std::strcmp(node->key, "Effects") == 0) {
                if (!lookTree(doc, node, ModuleRole::Generic)) return false;
            } else if (std::strcmp(node->key, "Drivers") == 0) {
                bool palette = false;
                for (const auto* field = doc.node(node->firstChild); field; field = doc.node(field->next)) {
                    if (std::strcmp(field->key, "palette") && std::strcmp(field->key, "paletteScript")) return false;
                    if (field->type != json::JsonType::String && field->type != json::JsonType::Int) return false;
                    palette = true;
                }
                if (!palette) return false;
            } else return false;
            content = true;
        }
        return content;
    }

    /// Hash the actual configured rendering state, excluding live and read-only counters through the native writer.
    static Result outputEtag(Scheduler& scheduler, char (&etag)[65]) {
        JsonSink document;
        document.append("{");
        auto* effects = scheduler.firstByName("Effects");
        if (effects) writeStateMember(document, *effects, true);
        else document.append("\"Effects\":null");
        document.append(",\"palette\":");
        const MoonModule* drivers = scheduler.firstByName("Drivers");
        const auto* palette = namedControl(drivers, "palette");
        if (palette) writeControlValue(document, *palette, true);
        else document.append("null");
        document.append(",\"paletteScript\":");
        const auto* script = namedControl(drivers, "paletteScript");
        if (script) writeControlValue(document, *script, true);
        else document.append("null");
        document.append("}");
        if (document.overflowed()) return failure(507, "out of memory reading output state");
        // Numeric controls may be marked read-only independently of their type; those counters stay outside the guard too.
        if (effects && hasReadonly(*effects)) {
            json::JsonDoc doc;
            if (!json::parse(document.data(), doc)) return failure(507, "could not read output state");
            stripReadonly(doc, *effects, json::member(doc, doc.rootNode(), effects->name()));
            document.clear();
            json::write(document, doc, doc.rootNode());
            if (document.overflowed()) return failure(507, "out of memory reading output state");
        }
        sha256Hex(document.data(), document.size(), etag, kSha256DigestSize);
        return {};
    }

    static Result apply(Scheduler& scheduler, const char* name, const char* etag,
                        bool lookOnly = true, bool requireMatch = true, const char* outputMatch = nullptr) {
        if (!validName(name)) return failure(400, "invalid preset name");
        if (requireMatch && (!etag || !etag[0])) return failure(428, "If-Match is required");
        LockGuard guard(lock_);
        if (!guard) return failure(503, "preset store is busy");
        File file;
        const auto loaded = readUnlocked(name, file);
        if (!loaded) return loaded.status == 404 && requireMatch ? failure(412, "preset changed or was deleted") : loaded;
        if (requireMatch && std::strcmp(etag, file.etag)) return failure(412, "preset changed");
        if (outputMatch) {
            char actual[65];
            const auto hashed = outputEtag(scheduler, actual);
            if (!hashed) return hashed;
            if (std::strcmp(outputMatch, actual)) return failure(412, "output changed; select the preset explicitly");
        }
        const auto valid = validate(file.text, lookOnly);
        if (!valid) return valid;
        Result result;
        std::memcpy(result.etag, file.etag, sizeof(result.etag));
        // A validated look changes only Effects and live palette values. Other services keep running.
        result.state = applyStateDocument(scheduler, file.text, StateSource::Request,
                                          lookOnly ? scheduler.firstByName("Effects") : nullptr);
        if (result.state.ok) {
            if (lookOnly) scheduler.preparePending();
            else scheduler.prepareTree();
            if (!scheduler.preparationResult().ok) {
                const uint16_t changes = result.state.changes;
                result.state = scheduler.preparationResult();
                result.state.changes = static_cast<uint16_t>(result.state.changes + changes);
            }
        }
        if (!result.state.ok) { result.status = 400; result.error = result.state.error; }
        else {
            result.applied = true;
            const auto hashed = outputEtag(scheduler, result.outputEtag);
            if (!hashed) result.outputEtag[0] = 0;
        }
        return result;
    }

    /// Snapshot names alone under the store lock; an overflow returns no partial membership list.
    static Result snapshotIndex(IndexSnapshot& out) {
        out = {};
        LockGuard guard(lock_);
        if (!guard) return failure(503, "preset store is busy");
        Snapshot snapshot{out};
        platform::fsList(kDirectory, snapshotEntry, &snapshot, platform::FsListMode::NamesOnly);
        if (!snapshot.result) out = {};
        return snapshot.result;
    }

    /// Write one current index item, releasing its file, parser and store lock before returning.
    static Result writeIndexItem(const char* name, JsonSink& sink) {
        if (!validName(name)) return failure(400, "invalid preset name");
        LockGuard guard(lock_);
        if (!guard) return failure(503, "preset store is busy");
        return writeIndexItemUnlocked(name, sink);
    }

    /// The file index is independent of whichever look currently renders.
    static Result writeIndex(JsonSink& sink) {
        LockGuard guard(lock_);
        if (!guard) return failure(503, "preset store is busy");
        Index index{sink};
        sink.append("\"items\":[");
        platform::fsList(kDirectory, indexEntry, &index);
        sink.append("],\"limit\":64");
        if (sink.overflowed()) return failure(507, "out of memory reading presets");
        return index.result;
    }

    /// Change only a stored file's pad; pad-only placeholders retain their metadata without becoming look documents.
    static Result setSlot(const char* name, uint8_t slot, const char* etag) {
        if (!validName(name) || slot >= kMaxPresets) return failure(400, "invalid preset pad");
        if (!etag || !etag[0]) return failure(428, "preset content tag is required");
        LockGuard guard(lock_);
        if (!guard) return failure(503, "preset store is busy");
        File file;
        const auto loaded = readUnlocked(name, file);
        if (!loaded) return loaded.status == 404 ? failure(412, "preset changed or was deleted") : loaded;
        if (std::strcmp(etag, file.etag)) return failure(412, "preset changed");
        JsonSink document;
        bool noMemory = false;
        if (!restampSlot(document, file.text, slot, &noMemory))
            return failure(noMemory ? 507 : 400, "could not update preset pad");
        char path[80]; pathFor(name, path, sizeof(path));
        if (!platform::fsWriteAtomic(path, document.data(), document.size())) return failure(507, "could not store preset pad");
        Result result;
        sha256Hex(document.data(), document.size(), result.etag, kSha256DigestSize);
        return result;
    }

    /// Put the one pad member first, preserving every other value and discarding earlier pad members.
    static bool restampSlot(JsonSink& sink, const char* text, uint8_t slot, bool* noMemory = nullptr) {
        json::JsonDoc doc;
        if (noMemory) *noMemory = false;
        if (slot >= kMaxPresets || !json::parse(text, doc) || !doc.rootNode() || doc.rootNode()->type != json::JsonType::Object) {
            if (noMemory) *noMemory = doc.outOfMemory;
            return false;
        }
        sink.appendf("{\"$slot\":%u", static_cast<unsigned>(slot));
        for (const auto* field = doc.node(doc.rootNode()->firstChild); field; field = doc.node(field->next)) {
            if (!std::strcmp(field->key, "$slot")) continue;
            sink.append(","); sink.writeJsonString(field->key); sink.append(":");
            json::write(sink, doc, field);
        }
        sink.append("}");
        if (noMemory) *noMemory = sink.overflowed();
        return !sink.overflowed();
    }

private:
    static inline TryLock lock_;
    struct Index { JsonSink& sink; bool first = true; Result result{}; };
    struct Snapshot { IndexSnapshot& names; Result result{}; };
    static Result failure(int status, const char* error) { Result result; result.status = status; result.error = error; return result; }

    static const ControlDescriptor* namedControl(const MoonModule* module, const char* name) {
        for (uint8_t i = 0; module && i < module->controls().count(); ++i)
            if (!std::strcmp(module->controls()[i].name, name)) return &module->controls()[i];
        return nullptr;
    }

    static bool hasReadonly(const MoonModule& module) {
        for (uint8_t i = 0; i < module.controls().count(); ++i)
            if (module.controls()[i].readonly) return true;
        for (uint8_t i = 0; i < module.childCount(); ++i)
            if (hasReadonly(*module.child(i))) return true;
        return false;
    }

    static void stripReadonly(json::JsonDoc& doc, const MoonModule& module, const json::JsonNode* object) {
        if (!object || object->type != json::JsonType::Object) return;
        const int objectIndex = static_cast<int>(object - doc.nodes);
        for (uint8_t i = 0; i < module.controls().count(); ++i) {
            const auto& control = module.controls()[i];
            if (!control.readonly) continue;
            int* link = &doc.nodes[objectIndex].firstChild;
            while (*link >= 0) {
                auto& field = doc.nodes[*link];
                if (!std::strcmp(field.key, control.name)) { *link = field.next; break; }
                link = &field.next;
            }
        }
        for (uint8_t i = 0; i < module.childCount(); ++i)
            stripReadonly(doc, *module.child(i), json::member(doc, &doc.nodes[objectIndex], module.child(i)->name()));
    }

    static Result readUnlocked(const char* name, File& out) {
        char path[80]; pathFor(name, path, sizeof(path));
        const int64_t size = platform::fsSize(path);
        if (size < 0) return failure(404, "preset not found");
        if (size > static_cast<int64_t>(kStateDocumentMax)) return failure(400, "invalid preset size");
        platform::free(out.text);
        out.text = static_cast<char*>(platform::alloc(static_cast<size_t>(size) + 1));
        if (!out.text) return failure(507, "out of memory reading preset");
        if (platform::fsRead(path, out.text, static_cast<size_t>(size) + 1) != size || std::memchr(out.text, 0, static_cast<size_t>(size)))
            return failure(507, "could not read complete preset");
        out.text[size] = 0;
        out.size = static_cast<size_t>(size);
        sha256Hex(out.text, out.size, out.etag, kSha256DigestSize);
        return {};
    }

    static Result validate(const char* text, bool lookOnly) {
        if (!text || !text[0] || std::strlen(text) > kStateDocumentMax) return failure(400, "invalid preset size");
        if (std::strstr(text, "\\u0000")) return failure(400, "NUL is not a preset string");
        json::JsonDoc doc;
        if (!json::parse(text, doc)) return failure(doc.outOfMemory ? 507 : 400, "could not parse preset document");
        const auto* root = doc.rootNode();
        if (!root || root->type != json::JsonType::Object) return failure(400, "preset must be a state document");
        bool content = false;
        for (int i = 0; i < doc.count; ++i) {
            const auto& object = doc.nodes[i];
            if (object.type != json::JsonType::Object) continue;
            for (const auto* field = doc.node(object.firstChild); field; field = doc.node(field->next))
                for (const auto* next = doc.node(field->next); next; next = doc.node(next->next))
                    if (std::strcmp(field->key, next->key) == 0) return failure(400, "duplicate preset member");
        }
        for (const auto* node = doc.node(root->firstChild); node; node = doc.node(node->next)) {
            if (node->key[0] == '$') {
                if (!std::strcmp(node->key, "$slot") && (node->type != json::JsonType::Int || node->decimal || node->intValue < 0 || node->intValue >= kMaxPresets))
                    return failure(400, "invalid preset slot");
                continue;
            }
            if (node->type != json::JsonType::Object) return failure(400, "preset root names module objects");
            content = true;
        }
        if (!content) return failure(400, "preset has no module content");
        if (lookOnly && !isLook(doc)) return failure(403, "look presets may set only Effects and palette settings");
        return {};
    }

    static bool lookTree(const json::JsonDoc& doc, const json::JsonNode* object, ModuleRole role) {
        for (const auto* field = doc.node(object->firstChild); field; field = doc.node(field->next)) {
            if (field->key[0] == '$' || !std::strcmp(field->key, "type")) continue;
            if (field->type != json::JsonType::Object) continue;
            const auto* type = json::member(doc, field, "type");
            ModuleRole childRole = role == ModuleRole::Generic ? ModuleRole::Layer : ModuleRole::Effect;
            if (type) {
                if (type->type != json::JsonType::String) return false;
                bool known = false;
                for (uint8_t i = 0; i < ModuleFactory::typeCount(); ++i) {
                    if (std::strcmp(ModuleFactory::typeName(i), type->str)) continue;
                    childRole = ModuleFactory::typeRole(i); known = true; break;
                }
                if (!known) return false;
            }
            if (role == ModuleRole::Generic ? childRole != ModuleRole::Layer :
                childRole != ModuleRole::Effect && childRole != ModuleRole::Modifier) return false;
            if (!lookTree(doc, field, childRole)) return false;
        }
        return true;
    }

    static bool entryName(const char* file, bool directory, char (&name)[kMaxNameLen]) {
        if (directory) return false;
        const size_t size = std::strlen(file);
        if (size < 6 || size - 5 >= sizeof(name) || std::strcmp(file + size - 5, ".json")) return false;
        std::memcpy(name, file, size - 5); name[size - 5] = 0;
        return validName(name);
    }
    static void countEntry(const char* file, bool directory, uint32_t, void* user) {
        char name[kMaxNameLen];
        if (entryName(file, directory, name)) ++*static_cast<unsigned*>(user);
    }
    static void indexEntry(const char* fileName, bool directory, uint32_t, void* user) {
        auto& index = *static_cast<Index*>(user);
        char name[kMaxNameLen];
        if (!index.result || !entryName(fileName, directory, name)) return;
        index.result = writeIndexItemUnlocked(name, index.sink, !index.first);
        if (index.result) index.first = false;
    }

    static void snapshotEntry(const char* fileName, bool directory, uint32_t, void* user) {
        auto& snapshot = *static_cast<Snapshot*>(user);
        char name[kMaxNameLen];
        if (!snapshot.result || !entryName(fileName, directory, name)) return;
        if (snapshot.names.count >= kMaxPresets) {
            snapshot.result = failure(507, "preset index exceeds limit");
            return;
        }
        std::memcpy(snapshot.names.names[snapshot.names.count++], name, std::strlen(name) + 1);
    }

    /// Read and serialize the actual item once for both whole-list and incremental consumers.
    static Result writeIndexItemUnlocked(const char* name, JsonSink& sink, bool commaBefore = false) {
        File file;
        const auto loaded = readUnlocked(name, file);
        if (!loaded) return loaded;
        json::JsonDoc doc;
        const bool valid = json::parse(file.text, doc) && doc.rootNode() && doc.rootNode()->type == json::JsonType::Object;
        if (doc.outOfMemory) return failure(507, "out of memory reading presets");
        if (commaBefore) sink.append(",");
        sink.append("{\"name\":"); sink.writeJsonString(name);
        sink.append(",\"etag\":"); sink.writeJsonString(file.etag);
        sink.append(",\"slot\":");
        const auto* slot = valid ? json::member(doc, doc.rootNode(), "$slot") : nullptr;
        if (slot && slot->type == json::JsonType::Int && !slot->decimal && slot->intValue >= 0 && slot->intValue < kMaxPresets)
            sink.appendf("%ld", slot->intValue);
        else sink.append("null");
        sink.append(",\"kind\":"); sink.writeJsonString(!valid ? "invalid" : isLook(doc) ? "look" : "configuration");
        sink.append(",\"roles\":[");
        bool comma = false;
        for (uint8_t i = 0; i < sizeof(kCapturable) / sizeof(kCapturable[0]); ++i) {
            const char* role = kCapturable[i];
            if (!valid || !json::member(doc, doc.rootNode(), role)) continue;
            if (comma) sink.append(",");
            comma = true;
            sink.writeJsonString(kCaptureRole[i]);
        }
        sink.append("],\"originScene\":");
        const auto* origin = valid ? json::member(doc, doc.rootNode(), "$origin") : nullptr;
        const auto* scene = origin && origin->type == json::JsonType::Object ? json::member(doc, origin, "scene") : nullptr;
        if (scene && scene->type == json::JsonType::String) sink.writeJsonString(scene->str);
        else sink.append("null");
        sink.append("}");
        return sink.overflowed() ? failure(507, "out of memory reading presets") : Result{};
    }
};

}  // namespace mm
