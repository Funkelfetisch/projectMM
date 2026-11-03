#pragma once

#include "light/effects/EffectBase.h"
#include "light/effects/WledBlinkTiming.h"
#include "light/effects/WledTwoColorPalette.h"

namespace mm {

/// Single-segment WLED Blink with palette 3, used by the old Blaulicht look.
///
/// Color inputs are linear scene values; the output driver applies its curve.
///
/// WLED's global frame time and palette transition can change its appearance, so this effect alone is not a verified migration of an archived preset.
class NebuliteBlinkEffect : public EffectBase {
public:
    /// Declares the full 3D set; the render indexes lights directly.
    Dim dimensions() const override { return Dim::D3; }
    /// Indexes the real lights in wiring order rather than the layout's bounding box.
    bool usesPhysicalLightOrder() const override { return true; }
    /// How fast the blink repeats.
    uint8_t speed = 230;
    /// How long the on phase lasts within each blink.
    uint8_t intensity = 128;
    /// Primary color, red part.
    uint8_t red = 0;
    /// Primary color, green part.
    uint8_t green = 0;
    /// Primary color, blue part.
    uint8_t blue = 255;
    /// Secondary color, red part.
    uint8_t secondaryRed = 0;
    /// Secondary color, green part.
    uint8_t secondaryGreen = 0;
    /// Secondary color, blue part.
    uint8_t secondaryBlue = 0;
    /// White part of the secondary color, shown in the off phase.
    uint8_t secondaryWhite = 0;
    /// How the two colors blend along the segment.
    uint8_t paletteBlend = 0;
    /// WLED frame time in milliseconds, the truncated 1000 / FPS.
    uint16_t frameMs = 23;
    /// First light of the segment.
    uint16_t segmentStart = 0;
    /// End of the segment, exclusive; zero means the end of the strip.
    uint16_t segmentStop = 0;

    /// Publish the timing, both colors, the palette blend and the segment.
    void defineControls() override {
        controls_.addControl("speed", speed, 0, 255);
        controls_.addControl("effect intensity", intensity, 0, 255);
        controls_.addControl("red", red, 0, 255);
        controls_.addControl("green", green, 0, 255);
        controls_.addControl("blue", blue, 0, 255);
        controls_.addControl("secondary red", secondaryRed, 0, 255);
        controls_.addControl("secondary green", secondaryGreen, 0, 255);
        controls_.addControl("secondary blue", secondaryBlue, 0, 255);
        controls_.addControl("secondary white", secondaryWhite, 0, 255);
        controls_.addControl("palette blend", paletteBlend, 0, 3);
        // Both archived wearable configs set 42 FPS; WLED truncates 1000 / FPS.
        controls_.addControl("frame ms", frameMs, 1, 1000);
        controls_.addControl("segment start", segmentStart);
        controls_.addControl("segment stop", segmentStop);
    }

    /// Render one frame at the effect's own clock.
    void tick() MM_NONBLOCKING override { renderAt(elapsed()); }

    /// Public for deterministic source-frame tests; it still uses the real layer.
    void renderAt(uint32_t now) {
        uint8_t* out = buffer();
        const uint32_t count = nrOfLights();
        const uint8_t channels = channelsPerLight();
        if (!out || channels < 3 || !count) return;
        const uint32_t start = segmentStart < count ? segmentStart : count;
        const uint32_t stop = segmentStop == 0 || segmentStop > count ? count : segmentStop;
        const bool on = phase_.next(now, speed, intensity, frameMs);
        // WLED compares the selected color to the first color, so equal RGBW colors use the palette even in the second phase.
        const bool palettePhase = on || (red == secondaryRed && green == secondaryGreen &&
                                         blue == secondaryBlue && secondaryWhite == 0);
        const RGB primary{red, green, blue};
        const RGB secondary{secondaryRed, secondaryGreen, secondaryBlue};
        for (uint32_t i = 0; i < count; ++i) {
            const bool inside = i >= start && i < stop;
            const RGB color = !inside ? RGB{0, 0, 0} : palettePhase
                ? WledTwoColorPalette::sample(primary, secondary, i - start, stop - start, paletteBlend)
                : secondary;
            const size_t offset = static_cast<size_t>(i) * channels;
            out[offset] = color.r; out[offset + 1] = color.g; out[offset + 2] = color.b;
            if (channels > 3) out[offset + 3] = inside && !palettePhase ? secondaryWhite : 0;
        }
    }
private:
    WledBlinkTiming phase_;
};
} // namespace mm
