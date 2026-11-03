#pragma once
#include "core/module/Scheduler.h"
#include "light/drivers/PreviewDriver.h"
#include <cstring>
namespace mm {
/// Read a bounded snapshot in the same output order as the existing web preview.
class BlePreviewFramePage {
public:
    /// Writes one page of the enabled PreviewDriver's frame from `offset`, null on success or an error name such as "frame-unavailable".
    static const char* writeCurrent(JsonSink& sink, Scheduler& scheduler, uint16_t offset, const char* revision, uint32_t now) {
        for (uint8_t i = 0; i < scheduler.moduleCount(); ++i) {
            auto* root = scheduler.module(i);
            if (!root || !root->enabled() || std::strcmp(root->name(), "Drivers")) continue;
            for (uint8_t j = 0; j < root->childCount(); ++j) {
                auto* child = root->child(j);
                if (child && child->enabled() && !std::strcmp(child->typeName(), "PreviewDriver"))
                    return static_cast<PreviewDriver*>(child)->bleFrame().write(sink, offset, revision, now);
            }
        }
        return "frame-unavailable";
    }
};
} // namespace mm
