#pragma once

#include "core/module/StateDocument.h"
#include <cstdint>

namespace mm {
class JsonSink;
class Scheduler;

/// Domain callbacks keep product scenes outside the core: the BLE request dispatcher and the WLED shim reach the catalog through them.
struct ProductSceneApi {
    const char* (*page)(JsonSink&, uint16_t);
    const char* (*info)(JsonSink&, const char*, uint16_t);
    StateDocumentResult (*validate)(Scheduler&, const char*, const char*, const char*);
    StateDocumentResult (*apply)(Scheduler&, const char*, const char*, const char*, bool);
    void (*state)(JsonSink&, Scheduler&);
    /// The tag of every scene's descriptors, which `info` reports so a client with a matching export reads none; null reports none.
    uint64_t (*descriptors)() = nullptr;
    /// The scene at `index` in catalog order, by id and display name; false past the end, and null where the product lists none.
    bool (*at)(uint16_t index, const char*& id, const char*& name) = nullptr;
    /// The catalog index of the complete scene running now, or -1 when none is recognized.
    int (*current)(Scheduler&) = nullptr;
    /// The catalog revision a scene is applied against.
    const char* (*revision)() = nullptr;
};
} // namespace mm
