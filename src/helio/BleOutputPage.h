#pragma once

#include "core/module/Scheduler.h"
#include "core/util/JsonSink.h"
#include "core/util/sha256.h"
#include "light/drivers/DriverBase.h"
#include <cstdio>
#include <cstring>

namespace mm {
/// Reports configured driver windows and resolved fixture channels, not wiring,
/// actual light count, pin qualification, or an exclusive role for any pixel.
class BleOutputPage {
    static void describe(JsonSink& sink, const MoonModule& child) {
        sink.append("{\"type\":"); sink.writeJsonString(child.typeName());
        const bool known = !std::strcmp(child.typeName(), "RmtLedDriver") ||
                           !std::strcmp(child.typeName(), "PwmLightDriver");
        if (known) {
            const auto& driver = static_cast<const DriverBase&>(child);
            const auto& c = driver.correction();
            sink.appendf(",\"start\":%ld,\"count\":%ld,\"rgb\":%s,\"white\":%s",
                static_cast<long>(driver.windowStart()), static_cast<long>(driver.windowCount()),
                c.offRed != Correction::kAbsent && c.offGreen != Correction::kAbsent && c.offBlue != Correction::kAbsent ? "true" : "false",
                c.offWhite != Correction::kAbsent || c.offWarmWhite != Correction::kAbsent ? "true" : "false");
        }
        sink.append("}");
    }
public:
    static const char* writeCurrent(JsonSink& sink, Scheduler& scheduler, uint16_t offset, const char* expected) {
        const MoonModule* root = nullptr;
        for (uint8_t i = 0; i < scheduler.moduleCount(); ++i) {
            const auto* module = scheduler.module(i);
            if (module && !std::strcmp(module->name(), "Drivers")) { root = module; break; }
        }
        Sha256 hash;
        uint16_t total = 0;
        if (root && root->enabled()) for (uint8_t i = 0; i < root->childCount(); ++i) {
            const auto* child = root->child(i);
            if (!child || !child->enabled() || !std::strcmp(child->typeName(), "PreviewDriver")) continue;
            char buf[256]; JsonSink item(buf, sizeof(buf)); describe(item, *child);
            if (item.overflowed()) return "outputs-too-large";
            hash.update(buf, std::strlen(buf)); ++total;
        }
        uint8_t digest[kSha256DigestSize]; hash.digest(digest);
        char revision[33];
        for (unsigned i = 0; i < 16; ++i) std::snprintf(revision + i * 2, 3, "%02x", digest[i]);
        if (offset && (!expected || std::strcmp(expected, revision))) return "outputs-changed";
        if ((total == 0 && offset != 0) || (total && offset >= total)) return "outputs-offset";
        sink.append("\"outputs\":{\"revision\":"); sink.writeJsonString(revision);
        sink.appendf(",\"total\":%u,\"offset\":%u,\"drivers\":[", static_cast<unsigned>(total), static_cast<unsigned>(offset));
        uint16_t seen = 0;
        if (root && root->enabled()) for (uint8_t i = 0; i < root->childCount(); ++i) {
            const auto* child = root->child(i);
            if (!child || !child->enabled() || !std::strcmp(child->typeName(), "PreviewDriver")) continue;
            if (seen++ == offset) { describe(sink, *child); break; }
        }
        sink.append("],\"next\":");
        if (offset + 1 < total) sink.appendf("%u", static_cast<unsigned>(offset + 1)); else sink.append("null");
        sink.append("}");
        return sink.overflowed() ? "outputs-too-large" : nullptr;
    }
};
} // namespace mm
