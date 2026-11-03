#pragma once

#include "core/util/JsonSink.h"
#include "core/util/sha256.h"
#include "core/module/Scheduler.h"
#include "light/layouts/PixelMapLayout.h"
#include <cstring>

namespace mm {
/// Paged, read-only description of configured output-to-2D coordinates.
/// The content token prevents pages from different map versions being mixed;
/// it is neither authentication nor a claim about physical wiring or the
/// current Layer's modifiers, raster reduction or LED colours.
class BlePixelMapPage {
public:
    static constexpr uint16_t kPageSize = 16;

    /// Describe one enabled point-map layout. Other or composite layouts are
    /// deliberately unknown, rather than inferred from a product name.
    static const char* writeCurrent(JsonSink& sink, Scheduler& scheduler, uint16_t offset, const char* expectedRevision) {
        const MoonModule* layouts = nullptr;
        for (uint8_t i = 0; i < scheduler.moduleCount(); ++i) {
            auto* root = scheduler.module(i);
            if (root && std::strcmp(root->name(), "Layouts") == 0) { layouts = root; break; }
        }
        const MoonModule* selected = nullptr;
        if (layouts && layouts->enabled()) for (uint8_t i = 0; i < layouts->childCount(); ++i) {
            const auto* child = layouts->child(i);
            if (!child || !child->enabled()) continue;
            if (selected) { selected = nullptr; break; }
            selected = child;
        }
        if (!selected || std::strcmp(selected->typeName(), "PixelMapLayout") != 0) {
            if (offset) return "map-changed";
            sink.append("\"map\":null"); return nullptr;
        }
        return write(sink, *static_cast<const PixelMapLayout*>(selected), offset, expectedRevision);
    }

    static const char* write(JsonSink& sink, const PixelMapLayout& map, uint16_t offset, const char* expectedRevision) {
        const uint16_t count = map.lightCount();
        if (!count) return "map-invalid";
        char revision[33];
        sha256Hex(map.points, std::strlen(map.points), revision);
        // Page zero establishes a snapshot token. A stale continuation never
        // combines new coordinates with an older first page.
        if (offset && (!expectedRevision || std::strcmp(expectedRevision, revision))) return "map-changed";
        // A shortened map must report its changed revision before checking the
        // old cursor, so the reader can restart instead of seeing a range error.
        // The end cursor is not a page: valid pages always contain a point.
        if (offset >= count) return "map-offset";
        struct Point { uint8_t x, y; } points[PixelMapLayout::kMaxPoints]{};
        struct Capture { Point* points; uint16_t count; bool valid; } capture{points, 0, true};
        map.placeLights(CoordSink{[](void* context, nrOfLightsType index, lengthType x, lengthType y, lengthType z) {
            auto& c = *static_cast<Capture*>(context);
            if (index != c.count || index >= PixelMapLayout::kMaxPoints || x < 0 || y < 0 || z != 0 ||
                x > static_cast<lengthType>(PixelMapLayout::kMaxCoordinate) ||
                y > static_cast<lengthType>(PixelMapLayout::kMaxCoordinate)) { c.valid = false; return; }
            c.points[index] = {static_cast<uint8_t>(x), static_cast<uint8_t>(y)}; ++c.count;
        }, nullptr, &capture});
        if (!capture.valid || capture.count != count) return "map-invalid";
        const uint16_t end = count - offset > kPageSize ? offset + kPageSize : count;
        sink.append("\"map\":{\"kind\":\"pixel-map-2d\",\"revision\":"); sink.writeJsonString(revision);
        sink.appendf(",\"total\":%u,\"offset\":%u,\"points\":[", static_cast<unsigned>(count), static_cast<unsigned>(offset));
        for (uint16_t i = offset; i < end; ++i) {
            if (i != offset) sink.append(",");
            sink.appendf("[%u,%u]", static_cast<unsigned>(points[i].x), static_cast<unsigned>(points[i].y));
        }
        sink.append("],\"next\":");
        if (end < count) sink.appendf("%u", static_cast<unsigned>(end)); else sink.append("null");
        sink.append("}");
        return sink.overflowed() ? "map-too-large" : nullptr;
    }
};
} // namespace mm
