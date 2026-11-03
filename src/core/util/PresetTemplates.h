#pragma once

namespace mm {
class JsonSink;
class Scheduler;

/// Optional product templates, emitted as native documents without applying them.
struct PresetTemplates {
    void (*writeIndex)(JsonSink&);
    const char* (*writeDocument)(JsonSink&, Scheduler&, const char* id);
};
}  // namespace mm
