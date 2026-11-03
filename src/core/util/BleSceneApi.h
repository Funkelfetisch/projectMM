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
};
} // namespace mm
