#pragma once

#include "light/layouts/LayoutBase.h"

namespace mm {
/// A bounded 2D point per output, listed in physical output order.
///
/// Coordinates are integer `x,y` pairs separated by semicolons. Repeated coordinates intentionally sample the same field point.
class PixelMapLayout : public LayoutBase {
public:
    /// The most points a layout lists.
    static constexpr size_t kMaxPoints = 128;
    /// The largest x or y a point takes.
    static constexpr unsigned kMaxCoordinate = 63;
    /// The `x,y` pairs, separated by semicolons, in output order.
    char points[1024] = "0,0";

    /// The points sit on a plane, so the layout is two-dimensional.
    Dim dimensions() const override { return Dim::D2; }
    /// Publish the points text area, which accepts only a list `validPoints` approves.
    void defineControls() override { controls_.addTextArea("points", points, sizeof(points), validPoints); }
    /// The number of listed points, zero while the list is invalid.
    nrOfLightsType lightCount() const override { return static_cast<nrOfLightsType>(walk(points, nullptr)); }
    /// Place one light per listed point in output order; an invalid list places none.
    void placeLights(const CoordSink& sink) const override {
        if (!validPoints(points)) return;
        walk(points, &sink);
    }
    /// Whether `value` lists at least one point, every coordinate is in range and the count is within the limit.
    static bool validPoints(const char* value) { return walk(value, nullptr) != 0; }
private:
    static size_t walk(const char* value, const CoordSink* sink) {
        if (!value || !*value) return 0;
        size_t count = 0;
        while (*value) {
            unsigned x = 0, y = 0;
            if (!number(value, x) || *value++ != ',' || !number(value, y) || count >= kMaxPoints) return 0;
            if (sink) sink->pixel(static_cast<nrOfLightsType>(count), static_cast<lengthType>(x), static_cast<lengthType>(y), 0);
            ++count;
            if (!*value) break;
            if (*value++ != ';' || !*value) return 0;
        }
        return count;
    }
    static bool number(const char*& cursor, unsigned& out) {
        if (*cursor < '0' || *cursor > '9') return false;
        while (*cursor >= '0' && *cursor <= '9') {
            out = out * 10u + unsigned(*cursor++ - '0');
            if (out > kMaxCoordinate) return false;
        }
        return true;
    }
};
} // namespace mm
