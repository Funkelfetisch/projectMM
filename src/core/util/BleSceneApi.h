#pragma once

#include "core/module/StateDocument.h"
#include <cstdint>

namespace mm {
class JsonSink;
class Scheduler;

/// Domain callbacks keep product scenes outside the core BLE request dispatcher.
struct BleSceneApi {
    const char* (*page)(JsonSink&, uint16_t);
    const char* (*info)(JsonSink&, const char*, uint16_t);
    StateDocumentResult (*validate)(Scheduler&, const char*, const char*, const char*);
    StateDocumentResult (*apply)(Scheduler&, const char*, const char*, const char*, bool);
    void (*state)(JsonSink&, Scheduler&);
    /// The tag of every scene's descriptors, which `info` reports so a client with a matching export reads none; null reports none.
    uint64_t (*descriptors)() = nullptr;
};
} // namespace mm
