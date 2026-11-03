#pragma once

#include "light/effects/EffectBase.h"

namespace mm {

/// Expanding concentric rings from random centers, with additive overlap and calm defaults.
/// @card RingsEffect.gif
///
/// Each ring grows continuously and respawns somewhere fresh once it leaves the grid. Several overlap, and where two rings cross their brightness sums. @ref RipplesEffect holds the sine-wave water surface, where this one is the concentric form.
///
/// @attribution David Jupijn
class RingsEffect : public EffectBase {
public:
    /// Catalog tags: MoonLight origin, David Jupijn and Rising Step.
    const char* tags() const override { return "💫🦅🖌️🎡"; }
    /// Iterates y and x, which extrude fills through a volume.
    Dim dimensions() const override { return Dim::D2; }

    /// How many rings the effect can hold at once.
    static constexpr uint8_t MAX_RIPPLES = 8;

    /// How many concentric rings; a few slow ones read as clean circles.
    uint8_t count = 2;
    /// Expansion rate.
    uint8_t speed = 30;
    /// Ring band width.
    uint8_t thickness = 3;
    /// Rotates every ring's hue.
    uint8_t hue_shift = 0;
    /// Background brightness at hue_shift's palette position; a black palette color stays black.
    uint8_t backgroundBrightness = 0;

    /// Maximum fully dark mapped-source interval in milliseconds; zero rescues the first dark frame.
    uint16_t maxPauseMs = 500;

    /// Cache physical geometry during preparation, outside the rendering hot path.
    void prepare() override {
        mappedPhysicalCount_ = layer() ? layer()->physicalLightCount() : 0;
        pauseStarted_ = false;
        failedRescue_ = false;
    }

    /// Publish the population, growth rate, band width, palette shift, background and pause limit.
    void defineControls() override {
        controls_.addControl("count", count, 1, 255);
        controls_.addControl("speed", speed, 1, 255);
        controls_.addControl("thickness", thickness, 1, 255);
        controls_.addControl("hue_shift", hue_shift, 0, 255);
        controls_.addControl("background brightness", backgroundBrightness, 0, 255);
        controls_.addControl("max pause ms", maxPauseMs, 0, 2000);
    }

    /// Grow every ring, respawning those that left, then light each pixel near one.
    void tick() MM_NONBLOCKING override {
        uint8_t* buf = buffer();
        lengthType w = width();
        lengthType h = height();
        uint8_t cpl = channelsPerLight();

        // The true distance to the far corner, kept wide: clamping it stalled every ring short of the edge.
        const uint32_t maxR32 = dist16(static_cast<int32_t>(w), static_cast<int32_t>(h));
        const uint16_t maxR = static_cast<uint16_t>(maxR32 < 1 ? 1 : (maxR32 > 65535 ? 65535 : maxR32));
        const uint32_t maxRadiusQ7 = static_cast<uint32_t>(maxR) << 7;
        const uint32_t respawnRadiusQ7 = (static_cast<uint32_t>(maxR) + 1u) << 7;
        const uint32_t thicknessQ7 = static_cast<uint32_t>(thickness) << 7;
        const uint8_t activeCount = count < MAX_RIPPLES ? count : MAX_RIPPLES;
        const uint32_t now = elapsed();
        const RGB background = colorFromPalette(*Palettes::active(), hue_shift, backgroundBrightness);

        if (!initialized_) {
            for (uint8_t i = 0; i < MAX_RIPPLES; i++) {
                spawn(i, w, h);
                // Staggered, so the rings start spread across every size.
                radiusQ7_[i] = static_cast<uint32_t>((i * maxR) / MAX_RIPPLES) << 7;
            }
            initialized_ = true;
            lastElapsed_ = now;
        }

        const uint32_t dt = now - lastElapsed_;
        lastElapsed_ = now;
        // The renderer keeps the same 1/128-pixel units as motion, so slow growth changes the band between whole-pixel crossings.
        const uint64_t growthQ7 = static_cast<uint64_t>(speed) * dt;
        uint8_t ageFade[MAX_RIPPLES];

        for (uint8_t i = 0; i < activeCount; i++) {
            const uint64_t next = static_cast<uint64_t>(radiusQ7_[i]) + growthQ7;
            // Keep the whole-pixel respawn boundary and discard overshoot: at most one new ring per tick.
            if (next >= respawnRadiusQ7) {
                spawn(i, w, h);
            } else {
                radiusQ7_[i] = static_cast<uint32_t>(next);
            }
            // An older ring fades out; the final fractional pixel stays dark until the respawn boundary.
            ageFade[i] = radiusQ7_[i] >= maxRadiusQ7 ? 0
                : static_cast<uint8_t>(255u - ((radiusQ7_[i] * 255u) / maxRadiusQ7));
        }

        const auto render = [&]() {
            for (lengthType y = 0; y < h; y++) {
                uint8_t* row = buf + static_cast<size_t>(y) * static_cast<size_t>(w) * cpl;
                for (lengthType x = 0; x < w; x++) {
                    uint16_t r_acc = background.r, g_acc = background.g, b_acc = background.b;
                    for (uint8_t i = 0; i < activeCount; i++) {
                        const int32_t dx = static_cast<int32_t>(x) - cx_[i];
                        const int32_t dy = static_cast<int32_t>(y) - cy_[i];
                        // Wide, and computed rather than tabled since this is a moving ring's center.
                        const uint32_t distanceQ7 = dist16(dx, dy) << 7;
                        const uint32_t diffQ7 = distanceQ7 > radiusQ7_[i]
                            ? distanceQ7 - radiusQ7_[i] : radiusQ7_[i] - distanceQ7;
                        if (diffQ7 < thicknessQ7) {
                            // Brightest on the ring itself, falling off across its band.
                            uint8_t falloff = static_cast<uint8_t>(((thicknessQ7 - diffQ7) * 255u) / thicknessQ7);
                            uint8_t intensity = scale8(falloff, ageFade[i]);
                            RGB c = colorFromPalette(*Palettes::active(), static_cast<uint8_t>(hue_[i] + hue_shift), intensity);
                            r_acc = static_cast<uint16_t>(r_acc + c.r);
                            g_acc = static_cast<uint16_t>(g_acc + c.g);
                            b_acc = static_cast<uint16_t>(b_acc + c.b);
                        }
                    }
                    if (cpl >= 1) row[0] = r_acc > 255 ? 255 : static_cast<uint8_t>(r_acc);
                    if (cpl >= 2) row[1] = g_acc > 255 ? 255 : static_cast<uint8_t>(g_acc);
                    if (cpl >= 3) row[2] = b_acc > 255 ? 255 : static_cast<uint8_t>(b_acc);
                    row += cpl;
                }
            }
        };
        render();
        if (!pauseStarted_) {
            lastVisibleElapsed_ = now;
            pauseStarted_ = true;
        }
        if (mappedVisible(buf, w, h, cpl)) {
            lastVisibleElapsed_ = now;
            failedRescue_ = false;
        } else if (activeCount && thickness && now - lastVisibleElapsed_ >= maxPauseMs
                   && (!failedRescue_ || now - lastFailedRescue_ >= 500u)) {
            uint8_t rescued = 0;
            if (restartMappedRing(activeCount, w, h, rescued)) {
                ageFade[rescued] = 255;
                render(); // the rescued ring is visible in this frame, without advancing any other ring
                lastVisibleElapsed_ = now;
            } else {
                failedRescue_ = true;
                lastFailedRescue_ = now;
            }
        }
    }

private:
    nrOfLightsType mappedPhysicalCount_ = 0; ///< physical destination bound, cached outside the frame loop
    bool pauseStarted_ = false;             ///< the first rendered frame starts the pause clock
    bool failedRescue_ = false;             ///< black palettes and empty maps retry at a bounded rate
    uint32_t lastVisibleElapsed_ = 0;       ///< wrap-safe source visibility clock
    uint32_t lastFailedRescue_ = 0;         ///< last unsuccessful rescue attempt

    /// Whether this logical entry reaches an actual light after static mapping.
    bool mapped(nrOfLightsType logical) const {
        bool found = false;
        layer()->lut().forEachDestination(logical, [&](nrOfLightsType destination) {
            found = found || destination < mappedPhysicalCount_;
        });
        return found;
    }

    /// Inspect the D2 source plane that Layer extrudes through every mapped depth slice.
    bool mappedVisible(const uint8_t* buf, lengthType w, lengthType h, uint8_t cpl) const {
        const size_t plane = static_cast<size_t>(w) * h;
        if (!buf || !plane || !cpl || !mappedPhysicalCount_) return false;
        const auto& lut = layer()->lut();
        const auto limit = lut.logicalCount() < layer()->buffer().count()
            ? lut.logicalCount() : layer()->buffer().count();
        const size_t planeBytes = plane * cpl;
        size_t pixel = 0;
        for (nrOfLightsType logical = 0; logical < limit; ++logical) {
            if (pixel == planeBytes) pixel = 0;
            if (mapped(logical)
                && (buf[pixel] || (cpl > 1 && buf[pixel + 1]) || (cpl > 2 && buf[pixel + 2]))) return true;
            pixel += cpl;
        }
        return false;
    }

    /// Rebirth one old ring on a mapped light, using only colors present in the active palette.
    bool restartMappedRing(uint8_t activeCount, lengthType w, lengthType h, uint8_t& rescued) {
        const size_t plane = static_cast<size_t>(w) * h;
        const auto& lut = layer()->lut();
        const auto limit = lut.logicalCount() < layer()->buffer().count()
            ? lut.logicalCount() : layer()->buffer().count();
        if (!plane || !limit || !mappedPhysicalCount_) return false;
        uint8_t oldest = 0;
        for (uint8_t i = 1; i < activeCount; ++i)
            if (radiusQ7_[i] > radiusQ7_[oldest]) oldest = i;
        uint8_t hue = hue_[oldest];
        bool visibleColor = false;
        for (uint8_t candidate = 0; candidate <= 16; ++candidate) {
            const uint8_t index = candidate == 0 ? static_cast<uint8_t>(hue + hue_shift)
                : static_cast<uint8_t>((candidate - 1u) * 16u);
            const RGB color = colorFromPalette(*Palettes::active(), index, 255);
            if (color.r || color.g || color.b) {
                hue = static_cast<uint8_t>(index - hue_shift);
                visibleColor = true;
                break;
            }
        }
        if (!visibleColor) return false;
        const nrOfLightsType start = static_cast<nrOfLightsType>((static_cast<uint64_t>(rng_.next16()) * limit) >> 16);
        for (nrOfLightsType offset = 0; offset < limit; ++offset) {
            const nrOfLightsType logical = static_cast<nrOfLightsType>((static_cast<size_t>(start) + offset) % limit);
            if (!mapped(logical)) continue;
            const size_t pixel = logical % plane;
            cx_[oldest] = static_cast<lengthType>(pixel % w);
            cy_[oldest] = static_cast<lengthType>(pixel / w);
            radiusQ7_[oldest] = 0;
            hue_[oldest] = hue;
            rescued = oldest;
            failedRescue_ = false;
            return true;
        }
        return false;
    }

    lengthType cx_[MAX_RIPPLES] = {};     ///< each ring's center on x
    lengthType cy_[MAX_RIPPLES] = {};     ///< and on y
    uint32_t radiusQ7_[MAX_RIPPLES] = {}; ///< radii in 1/128-pixel units, shared by motion and band rendering
    uint8_t hue_[MAX_RIPPLES] = {};       ///< each ring's palette entry
    bool initialized_ = false;            ///< false until the first tick seeds the rings
    uint32_t lastElapsed_ = 0;            ///< the previous frame's timestamp
    Random8 rng_{0xC0DECAFEu};            ///< the spawn's placement and color
    /// One random byte, the shape the spawn below wants.
    uint8_t rand8() { return rng_.next8(); }

    /// Put ring `i` somewhere fresh, at zero radius and a new color.
    void spawn(uint8_t i, lengthType w, lengthType h) {
        cx_[i] = static_cast<lengthType>((static_cast<uint16_t>(rand8()) * w) >> 8);
        cy_[i] = static_cast<lengthType>((static_cast<uint16_t>(rand8()) * h) >> 8);
        radiusQ7_[i] = 0;
        hue_[i] = rand8();
    }
};

} // namespace mm
