#pragma once

#include "core/AudioFrame.h"
#include "core/AudioModule.h"
#include "core/color.h"
#include "core/math8.h"
#include "light/Palette.h"
#include "light/effects/EffectBase.h"
#include "light/layers/Layer.h"

#include <cstdio>
#include <cstring>

namespace mm {

// HELIO product preset player for the curated helio1 bundle. This keeps the
// source effect IDs, names, speed/intensity values, and playlist order as data,
// but renders with projectMM's palette/audio/light-buffer primitives.
class HelioWledPresetsEffect : public EffectBase {
    enum class Preset : uint8_t {
        CandleMulti = 0,
        ColorTwinkles,
        FreqMapStatic,
        Lake,
        Loading,
        PalettePink,
        Phased,
        Ripple,
        Running,
        Starburst,
        GravCenter,
        Juggles,
        MultiComet,
        NoiseMeter,
        Plasmoid,
        FreqMapAudio,
        GEQ,
        GravFreq,
        FreqWave,
        Count
    };

public:
    Dim dimensions() const override { return Dim::D3; }  // writes the whole linear strip buffer

    uint8_t playlist = 1;  // 0 = Static Animations, 1 = Audio Animations
    uint8_t preset = static_cast<uint8_t>(Preset::GEQ);
    bool autoPlay = true;
    bool idleBeat = true;
    uint8_t intensity = 255;

    void onBuildControls() override {
        controls_.addSelect("playlist", playlist, kPlaylistOptions, kPlaylistCount);
        controls_.addSelect("preset", preset, kPresetOptions, kPresetCount);
        controls_.addBool("autoPlay", autoPlay);
        controls_.addBool("idleBeat", idleBeat);
        controls_.addUint8("intensity", intensity, 0, 255);
    }

    void onBuildState() override {
        cursor_ = 0;
        observedPlaylist_ = playlist;
        observedAutoPlay_ = autoPlay;
        activePreset_ = currentPreset(elapsed());
        paletteLoaded_ = 255;
        lastSwitchMs_ = elapsed();
        clearFrame();
        refreshStatus();
    }

    void onUpdate(const char* controlName) override {
        if (!controlName) return;
        if (std::strcmp(controlName, "playlist") == 0 ||
            std::strcmp(controlName, "autoPlay") == 0 ||
            std::strcmp(controlName, "preset") == 0) {
            cursor_ = 0;
            observedPlaylist_ = playlist;
            observedAutoPlay_ = autoPlay;
            activePreset_ = currentPreset(elapsed());
            paletteLoaded_ = 255;
            lastSwitchMs_ = elapsed();
            clearFrame();
            refreshStatus();
        }
    }

    void loop() override {
        if (lightCount() == 0 || channelsPerLight() == 0 || !buffer()) return;

        const uint32_t now = elapsed();
        const uint8_t next = currentPreset(now);
        if (next != activePreset_) {
            activePreset_ = next;
            paletteLoaded_ = 255;
            clearFrame();
            refreshStatus();
        }

        AudioView audio;
        readAudio(audio, now);

        clearFrame();
        switch (static_cast<Preset>(activePreset_)) {
            case Preset::CandleMulti:   renderCandle(now); break;
            case Preset::ColorTwinkles: renderColorTwinkles(now); break;
            case Preset::FreqMapStatic: renderFreqMap(now, audio, false); break;
            case Preset::Lake:          renderLake(now); break;
            case Preset::Loading:       renderLoading(now); break;
            case Preset::PalettePink:   renderPalettePink(now); break;
            case Preset::Phased:        renderPhased(now); break;
            case Preset::Ripple:        renderRipple(now); break;
            case Preset::Running:       renderRunning(now); break;
            case Preset::Starburst:     renderStarburst(now, audio); break;
            case Preset::GravCenter:    renderGravCenter(now, audio); break;
            case Preset::Juggles:       renderJuggles(now, audio); break;
            case Preset::MultiComet:    renderMultiComet(now, audio); break;
            case Preset::NoiseMeter:    renderNoiseMeter(now, audio); break;
            case Preset::Plasmoid:      renderPlasmoid(now, audio); break;
            case Preset::FreqMapAudio:  renderFreqMap(now, audio, true); break;
            case Preset::GEQ:           renderGEQ(now, audio); break;
            case Preset::GravFreq:      renderGravFreq(now, audio); break;
            case Preset::FreqWave:      renderFreqWave(now, audio); break;
            case Preset::Count:         break;
        }
    }

private:
    struct PresetConfig {
        uint8_t wledId;
        uint8_t speed;
        uint8_t intensity;
        uint8_t palette;
        RGB primary;
        uint8_t white;
        RGB secondary;
    };

    struct AudioView {
        uint16_t level = 0;
        uint16_t levelSmoothed = 0;
        uint16_t peakHz = 0;
        uint16_t peakMag = 0;
        uint8_t bands[16] = {};
    };

    inline static constexpr const char* kPlaylistOptions[] = {
        "Static Animations",
        "Audio Animations",
    };
    inline static constexpr uint8_t kPlaylistCount =
        static_cast<uint8_t>(sizeof(kPlaylistOptions) / sizeof(kPlaylistOptions[0]));

    inline static constexpr const char* kPresetOptions[] = {
        "Candle Multi",
        "Colortwinkles",
        "Freqmap amber",
        "Lake",
        "Loading",
        "Pink Palette",
        "Phased",
        "Ripple",
        "Running",
        "Fw Starburst audio",
        "Gravcenter",
        "Juggles",
        "Multi Comet audio",
        "Noisemeter",
        "Plasmoid",
        "Freqmap wide",
        "GEQ",
        "Gravfreq",
        "Freqwave",
    };
    inline static constexpr uint8_t kPresetCount =
        static_cast<uint8_t>(sizeof(kPresetOptions) / sizeof(kPresetOptions[0]));

    inline static constexpr PresetConfig kConfigs[] = {
        {102,  53, 255,  0, {255, 133,   0}, 255, {  0,   0,   0}},
        { 74, 128, 128, 28, {255, 133,   0}, 255, {  0,   0,   0}},
        {155, 128, 128, 11, {255, 133,   0}, 255, {  0,   0,   0}},
        { 75, 128, 128,  0, {255, 133,   0}, 255, {  0,   0,   0}},
        { 47, 128,  16, 18, {255, 133,   0}, 255, {  0,   0,   0}},
        { 65, 128, 128, 19, {255, 133,   0}, 255, {  0,   0,   0}},
        {105, 128, 128, 45, {255, 133,   0}, 255, {  0,   0,   0}},
        { 79, 128, 128,  0, {255, 133,   0}, 255, {  0,   0,   0}},
        { 15, 135, 128, 73, {255, 133,   0},   0, {  0,   0,   0}},
        {192,  75, 128, 57, {255, 133,   0},   0, {  0,   0,   0}},
        {156, 128, 128, 11, {255, 133,   0},   0, {  0,   0,   0}},
        {130, 128, 255, 64, {255, 133,   0},   0, {  0,   0,   0}},
        {191,  91,   7, 64, {255, 133,   0},   0, {  0,   0,   0}},
        {136, 248, 128, 64, {255, 135,   0},   0, {  0,   0,   0}},
        {133, 128,  80, 28, {255, 135,   0},   0, {  0,   0,   0}},
        {155, 192, 128, 63, {255, 135,   0},   0, {  0,   0,   0}},
        {139, 128, 128, 11, {255, 135,   0},   0, {111, 255,   0}},
        {158,  71,  98,  1, {255, 135,   0},   0, {111, 255,   0}},
        {137, 202, 247,  1, {  0,   0, 255},   0, {111, 255,   0}},
    };

    inline static constexpr uint8_t kStaticOrder[] = {
        static_cast<uint8_t>(Preset::Ripple),
        static_cast<uint8_t>(Preset::Lake),
        static_cast<uint8_t>(Preset::Phased),
        static_cast<uint8_t>(Preset::Running),
        static_cast<uint8_t>(Preset::CandleMulti),
        static_cast<uint8_t>(Preset::Loading),
        static_cast<uint8_t>(Preset::ColorTwinkles),
        static_cast<uint8_t>(Preset::PalettePink),
    };

    inline static constexpr uint8_t kAudioOrder[] = {
        static_cast<uint8_t>(Preset::GEQ),
        static_cast<uint8_t>(Preset::GravFreq),
        static_cast<uint8_t>(Preset::NoiseMeter),
        static_cast<uint8_t>(Preset::FreqMapAudio),
        static_cast<uint8_t>(Preset::FreqWave),
        static_cast<uint8_t>(Preset::Juggles),
        static_cast<uint8_t>(Preset::GravCenter),
        static_cast<uint8_t>(Preset::MultiComet),
        static_cast<uint8_t>(Preset::Plasmoid),
        static_cast<uint8_t>(Preset::Starburst),
    };

    uint8_t activePreset_ = static_cast<uint8_t>(Preset::GEQ);
    uint8_t cursor_ = 0;
    uint8_t observedPlaylist_ = 255;
    bool observedAutoPlay_ = false;
    uint32_t lastSwitchMs_ = 0;
    Palette presetPalette_{};
    uint8_t paletteLoaded_ = 255;
    char statusBuf_[64] = {};

    static_assert(kPresetCount == static_cast<uint8_t>(Preset::Count),
                  "HELIO preset table mismatch");
    static_assert(sizeof(kConfigs) / sizeof(kConfigs[0]) == kPresetCount,
                  "HELIO preset config table mismatch");

    const PresetConfig& cfg() const { return kConfigs[activePreset_ < kPresetCount ? activePreset_ : 0]; }

    size_t lightCount() const {
        Layer* l = layer();
        return l ? static_cast<size_t>(l->buffer().count()) : 0;
    }

    size_t bufferBytes() const {
        Layer* l = layer();
        return l ? l->buffer().bytes() : 0;
    }

    void clearFrame() {
        uint8_t* buf = buffer();
        const size_t bytes = bufferBytes();
        if (buf && bytes > 0) std::memset(buf, 0, bytes);
    }

    uint8_t scaled(uint8_t v, uint8_t bri) const {
        return scale8(scale8(v, bri), intensity);
    }

    uint8_t presetBri() const {
        const uint16_t v = static_cast<uint16_t>(cfg().intensity) + 96u;
        return v > 255u ? 255u : static_cast<uint8_t>(v);
    }

    void setPixel(size_t i, RGB c, uint8_t white = 0, uint8_t bri = 255) {
        const uint8_t cpl = channelsPerLight();
        uint8_t* buf = buffer();
        if (!buf || cpl == 0 || i >= lightCount()) return;
        const size_t off = i * cpl;
        if (off + cpl > bufferBytes()) return;
        if (cpl >= 1) buf[off + 0] = scaled(c.r, bri);
        if (cpl >= 2) buf[off + 1] = scaled(c.g, bri);
        if (cpl >= 3) buf[off + 2] = scaled(c.b, bri);
        if (cpl >= 4) buf[off + 3] = scale8(scale8(white, bri), intensity);
    }

    void addPixel(size_t i, RGB c, uint8_t white = 0, uint8_t bri = 255) {
        const uint8_t cpl = channelsPerLight();
        uint8_t* buf = buffer();
        if (!buf || cpl == 0 || i >= lightCount()) return;
        const size_t off = i * cpl;
        if (off + cpl > bufferBytes()) return;
        if (cpl >= 1) buf[off + 0] = qadd8(buf[off + 0], scaled(c.r, bri));
        if (cpl >= 2) buf[off + 1] = qadd8(buf[off + 1], scaled(c.g, bri));
        if (cpl >= 3) buf[off + 2] = qadd8(buf[off + 2], scaled(c.b, bri));
        if (cpl >= 4) buf[off + 3] = qadd8(buf[off + 3], scale8(scale8(white, bri), intensity));
    }

    const Palette& presetPalette() {
        const uint8_t idx = cfg().palette;
        if (paletteLoaded_ != idx) {
            presetPalette_ = Palettes::fromBuiltin(idx);
            paletteLoaded_ = idx;
        }
        return presetPalette_;
    }

    RGB palette(uint8_t index, uint8_t bri = 255) {
        return colorFromPalette(presetPalette(), index, bri);
    }

    static uint8_t posByte(size_t i, size_t n) {
        if (n <= 1) return 0;
        return static_cast<uint8_t>((static_cast<uint32_t>(i) * 255u) / static_cast<uint32_t>(n - 1));
    }

    static uint8_t hash8(uint32_t a, uint32_t b) {
        uint32_t x = a * 0x45d9f3bu + b * 0x27d4eb2du + 0x165667b1u;
        x ^= x >> 16;
        x *= 0x7feb352du;
        x ^= x >> 15;
        return static_cast<uint8_t>(x >> 24);
    }

    static uint8_t bpmFromSpeed(uint8_t speed, uint8_t lo = 18, uint8_t hi = 150) {
        return static_cast<uint8_t>(lo + (static_cast<uint16_t>(speed) * (hi - lo)) / 255u);
    }

    static size_t circularDistance(size_t a, size_t b, size_t n) {
        if (n == 0) return 0;
        const size_t d = a > b ? a - b : b - a;
        const size_t wrap = n - d;
        return d < wrap ? d : wrap;
    }

    static uint8_t circularFalloff(size_t a, size_t b, size_t n, size_t radius) {
        if (radius == 0) return 0;
        const size_t dist = circularDistance(a, b, n);
        if (dist >= radius) return 0;
        return static_cast<uint8_t>(255u - (static_cast<uint32_t>(dist) * 255u) / static_cast<uint32_t>(radius));
    }

    uint8_t currentPreset(uint32_t now) {
        if (playlist >= kPlaylistCount) playlist = 1;
        if (preset >= kPresetCount) preset = 0;
        if (observedPlaylist_ != playlist || observedAutoPlay_ != autoPlay) {
            observedPlaylist_ = playlist;
            observedAutoPlay_ = autoPlay;
            cursor_ = 0;
            lastSwitchMs_ = now;
        }
        if (!autoPlay) return preset;

        const uint8_t* order = playlist == 0 ? kStaticOrder : kAudioOrder;
        const uint8_t count = playlist == 0
            ? static_cast<uint8_t>(sizeof(kStaticOrder) / sizeof(kStaticOrder[0]))
            : static_cast<uint8_t>(sizeof(kAudioOrder) / sizeof(kAudioOrder[0]));
        const uint32_t durMs = playlist == 0 ? 10000u : 30000u;  // Source playlist dur 100/300, in 100 ms units
        if (count == 0) return preset;
        if (cursor_ >= count) cursor_ = 0;
        while (now - lastSwitchMs_ >= durMs) {
            lastSwitchMs_ += durMs;
            cursor_ = static_cast<uint8_t>((cursor_ + 1u) % count);
        }
        preset = order[cursor_];
        return preset;
    }

    void refreshStatus() {
        const uint8_t idx = activePreset_ < kPresetCount ? activePreset_ : 0;
        const char* list = playlist == 0 ? "static" : "audio";
        std::snprintf(statusBuf_, sizeof(statusBuf_), "%s: %s (source %u)",
                      list, kPresetOptions[idx], static_cast<unsigned>(kConfigs[idx].wledId));
        setStatus(statusBuf_);
    }

    void readAudio(AudioView& out, uint32_t now) const {
        const AudioFrame* f = AudioModule::latestFrame();
        bool hasSignal = false;
        if (f) {
            out.level = f->level;
            out.levelSmoothed = f->levelSmoothed;
            out.peakHz = f->peakHz;
            out.peakMag = f->peakMag;
            for (uint8_t i = 0; i < 16; i++) {
                out.bands[i] = f->bands[i];
                hasSignal = hasSignal || f->bands[i] > 5;
            }
            hasSignal = hasSignal || f->level > 5 || f->levelSmoothed > 5;
        }
        if (!idleBeat || hasSignal) return;

        const uint8_t phase = beat8(120, now);
        const uint8_t beat = sin8(phase);
        const uint8_t kick = scale8(beat, beat);
        out.level = static_cast<uint16_t>(40u + scale8(kick, 190));
        out.levelSmoothed = static_cast<uint16_t>(50u + scale8(sin8(static_cast<uint8_t>(phase + 18)), 170));
        out.peakHz = static_cast<uint16_t>(80u + (static_cast<uint16_t>(sin8(static_cast<uint8_t>(phase + 40))) * 1800u) / 255u);
        out.peakMag = out.level;
        for (uint8_t i = 0; i < 16; i++) {
            const uint8_t carrier = sin8(static_cast<uint8_t>(phase + i * 17u));
            const uint8_t falloff = static_cast<uint8_t>(255u - i * 9u);
            const uint8_t bass = i < 3 ? scale8(kick, 210) : scale8(kick, 70);
            out.bands[i] = scale8(qadd8(bass, scale8(carrier, 72)), falloff);
        }
    }

    void renderCandle(uint32_t now) {
        const size_t n = lightCount();
        const uint32_t bucket = now / 45u;
        for (size_t i = 0; i < n; i++) {
            const uint8_t flicker = static_cast<uint8_t>(150u + (hash8(static_cast<uint32_t>(i), bucket) >> 1));
            const uint8_t wave = scale8(sin8(static_cast<uint8_t>(posByte(i, n) * 3u + now / 32u)), 50);
            const uint8_t bri = qadd8(flicker, wave);
            setPixel(i, cfg().primary, cfg().white, bri);
        }
    }

    void renderColorTwinkles(uint32_t now) {
        const size_t n = lightCount();
        const uint32_t bucket = now / 80u;
        for (size_t i = 0; i < n; i++) {
            const uint8_t seed = hash8(static_cast<uint32_t>(i), 19u);
            const uint8_t gate = hash8(static_cast<uint32_t>(i), bucket >> 2);
            uint8_t bri = scale8(sin8(static_cast<uint8_t>(seed + now / 6u)), 52);
            if (gate > 224) bri = qadd8(bri, scale8(sin8(static_cast<uint8_t>(now / 3u + seed)), 210));
            const RGB c = gate & 1 ? palette(static_cast<uint8_t>(seed + now / 24u), bri)
                                   : cfg().primary;
            setPixel(i, c, cfg().white, bri);
        }
    }

    void renderLake(uint32_t now) {
        const size_t n = lightCount();
        for (size_t i = 0; i < n; i++) {
            const uint8_t x = posByte(i, n);
            const uint8_t a = sin8(static_cast<uint8_t>(x * 2u + now / 48u));
            const uint8_t b = sin8(static_cast<uint8_t>(x * 5u - now / 83u));
            const uint8_t idx = static_cast<uint8_t>((static_cast<uint16_t>(a) + b) / 2u + now / 96u);
            setPixel(i, palette(idx, 210));
        }
    }

    void renderLoading(uint32_t now) {
        const size_t n = lightCount();
        if (n == 0) return;
        const size_t head = (static_cast<uint64_t>(now) * bpmFromSpeed(cfg().speed, 25, 140) * n) / 60000u % n;
        const size_t radius = n / 6u + 2u;
        for (size_t i = 0; i < n; i++) {
            const uint8_t bri = circularFalloff(i, head, n, radius);
            if (bri) setPixel(i, cfg().primary, cfg().white, bri);
        }
    }

    void renderPalettePink(uint32_t now) {
        const size_t n = lightCount();
        for (size_t i = 0; i < n; i++) {
            const uint8_t x = posByte(i, n);
            const RGB pal = palette(static_cast<uint8_t>(x + now / 52u), 220);
            const uint8_t pinkMix = sin8(static_cast<uint8_t>(x * 2u + now / 80u));
            const RGB c{
                qadd8(scale8(pal.r, static_cast<uint8_t>(255 - (pinkMix >> 2))), scale8(cfg().primary.r, pinkMix >> 1)),
                qadd8(scale8(pal.g, static_cast<uint8_t>(255 - (pinkMix >> 2))), scale8(cfg().primary.g, pinkMix >> 2)),
                qadd8(scale8(pal.b, static_cast<uint8_t>(255 - (pinkMix >> 2))), scale8(cfg().primary.b, pinkMix >> 1)),
            };
            setPixel(i, c, 0, 220);
        }
    }

    void renderPhased(uint32_t now) {
        const size_t n = lightCount();
        for (size_t i = 0; i < n; i++) {
            const uint8_t x = posByte(i, n);
            const uint8_t p1 = sin8(static_cast<uint8_t>(x * 3u + now / 38u));
            const uint8_t p2 = cos8(static_cast<uint8_t>(x * 5u - now / 53u));
            const uint8_t bri = static_cast<uint8_t>((static_cast<uint16_t>(p1) + p2) / 2u);
            setPixel(i, palette(static_cast<uint8_t>(x + bri + now / 120u), bri));
        }
    }

    void renderRipple(uint32_t now) {
        const size_t n = lightCount();
        if (n == 0) return;
        const size_t radius = n / 5u + 3u;
        for (uint8_t c = 0; c < 4; c++) {
            const size_t center = (static_cast<size_t>(hash8(c, 7u)) * n) / 255u;
            const size_t moving = (center + (static_cast<uint64_t>(now + c * 900u) * n) / 5200u) % n;
            for (size_t i = 0; i < n; i++) {
                const size_t d = circularDistance(i, moving, n);
                const uint8_t wave = sin8(static_cast<uint8_t>((d * 255u) / radius - now / 12u));
                const uint8_t fall = d < radius ? static_cast<uint8_t>(255u - (d * 255u) / radius) : 0;
                const uint8_t bri = scale8(wave, fall);
                if (bri > 8) addPixel(i, palette(static_cast<uint8_t>(c * 53u + now / 70u), bri), cfg().white, bri);
            }
        }
    }

    void renderRunning(uint32_t now) {
        const size_t n = lightCount();
        const uint8_t phase = beat8(bpmFromSpeed(cfg().speed, 20, 180), now);
        for (size_t i = 0; i < n; i++) {
            const uint8_t x = posByte(i, n);
            const uint8_t bri = scale8(sin8(static_cast<uint8_t>(x * 5u + phase)), presetBri());
            setPixel(i, cfg().primary, cfg().white, bri);
        }
    }

    void renderFreqMap(uint32_t now, const AudioView& audio, bool reactive) {
        const size_t n = lightCount();
        if (n == 0) return;
        size_t peak = (static_cast<uint64_t>(now) * n) / 9000u % n;
        uint8_t drive = 160;
        if (reactive) {
            const uint16_t hz = audio.peakHz > 80 ? audio.peakHz : 80;
            const uint16_t capped = hz > 2200 ? 2200 : hz;
            peak = (static_cast<uint32_t>(capped - 80u) * n) / 2120u;
            drive = audio.levelSmoothed > 255 ? 255 : static_cast<uint8_t>(audio.levelSmoothed);
        }
        const size_t radius = n / 7u + 3u;
        for (size_t i = 0; i < n; i++) {
            uint8_t bri = circularFalloff(i, peak, n, radius);
            const uint8_t band = static_cast<uint8_t>((i * 16u) / (n ? n : 1u));
            if (reactive) bri = qadd8(scale8(bri, 210), scale8(audio.bands[band], 90));
            else bri = scale8(bri, 180);
            if (bri > 4) setPixel(i, palette(static_cast<uint8_t>(posByte(i, n) + now / 42u), scale8(bri, drive)));
        }
    }

    void renderGEQ(uint32_t now, const AudioView& audio) {
        const size_t n = lightCount();
        for (size_t i = 0; i < n; i++) {
            const uint8_t band = static_cast<uint8_t>((i * 16u) / (n ? n : 1u));
            const uint8_t mag = audio.bands[band];
            const RGB c{
                qadd8(scale8(cfg().primary.r, mag), scale8(cfg().secondary.r, static_cast<uint8_t>(255 - band * 12u))),
                qadd8(scale8(cfg().primary.g, mag), scale8(cfg().secondary.g, static_cast<uint8_t>(band * 12u))),
                qadd8(scale8(cfg().primary.b, mag), scale8(cfg().secondary.b, static_cast<uint8_t>(band * 10u))),
            };
            const uint8_t beat = sin8(static_cast<uint8_t>(now / 16u + band * 13u));
            setPixel(i, c, cfg().white, qadd8(scale8(mag, 210), scale8(beat, 32)));
        }
    }

    void renderNoiseMeter(uint32_t now, const AudioView& audio) {
        const size_t n = lightCount();
        if (n == 0) return;
        const uint8_t level = audio.level > 255 ? 255 : static_cast<uint8_t>(audio.level);
        const size_t lit = (static_cast<uint32_t>(level) * n) / 255u;
        for (size_t i = 0; i < n; i++) {
            if (i <= lit) {
                const uint8_t x = posByte(i, n);
                const uint8_t noise = hash8(static_cast<uint32_t>(i), now / 28u);
                const uint8_t bri = qadd8(scale8(level, 190), scale8(noise, 64));
                setPixel(i, palette(static_cast<uint8_t>(x + noise + now / 60u), bri), cfg().white, bri);
            }
        }
    }

    void renderGravCenter(uint32_t now, const AudioView& audio) {
        const size_t n = lightCount();
        if (n == 0) return;
        const size_t center = n / 2u;
        const uint8_t level = audio.levelSmoothed > 255 ? 255 : static_cast<uint8_t>(audio.levelSmoothed);
        const size_t reach = (static_cast<uint32_t>(level) * n) / 510u + 1u;
        for (size_t i = 0; i < n; i++) {
            const size_t d = circularDistance(i, center, n);
            if (d <= reach) {
                const uint8_t bri = static_cast<uint8_t>(255u - (static_cast<uint32_t>(d) * 220u) / (reach + 1u));
                setPixel(i, palette(static_cast<uint8_t>(d * 9u + now / 70u), bri));
            }
        }
        const size_t dot = (center + (static_cast<uint32_t>(sin8(now / 14u)) * n) / 512u) % n;
        addPixel(dot, cfg().primary, cfg().white, 255);
    }

    void renderGravFreq(uint32_t now, const AudioView& audio) {
        const size_t n = lightCount();
        if (n == 0) return;
        for (uint8_t band = 0; band < 16; band++) {
            const uint8_t mag = audio.bands[band];
            const size_t base = (static_cast<size_t>(band) * n) / 16u;
            const size_t span = n / 16u + 1u;
            const size_t lift = (static_cast<uint32_t>(mag) * span) / 255u;
            const size_t pos = (base + lift + (sin8(static_cast<uint8_t>(now / 18u + band * 9u)) * span) / 512u) % n;
            addPixel(pos, palette(static_cast<uint8_t>(band * 16u + now / 55u), qadd8(mag, 70)), 0, qadd8(mag, 40));
            if (pos > 0) addPixel(pos - 1, palette(static_cast<uint8_t>(band * 16u), mag), 0, scale8(mag, 120));
        }
    }

    void renderFreqWave(uint32_t now, const AudioView& audio) {
        const size_t n = lightCount();
        for (size_t i = 0; i < n; i++) {
            const uint8_t x = posByte(i, n);
            const uint8_t band = static_cast<uint8_t>((i * 16u) / (n ? n : 1u));
            const uint8_t wave = sin8(static_cast<uint8_t>(x * 3u + now / 12u + audio.bands[band]));
            const uint8_t bri = qadd8(scale8(audio.bands[band], 160), scale8(wave, 90));
            const RGB c{
                qadd8(scale8(cfg().primary.r, wave), scale8(cfg().secondary.r, audio.bands[band])),
                qadd8(scale8(cfg().primary.g, wave), scale8(cfg().secondary.g, audio.bands[band])),
                qadd8(scale8(cfg().primary.b, wave), scale8(cfg().secondary.b, audio.bands[band])),
            };
            setPixel(i, c, 0, bri);
        }
    }

    void renderJuggles(uint32_t now, const AudioView& audio) {
        const size_t n = lightCount();
        if (n == 0) return;
        const uint8_t drive = audio.levelSmoothed > 255 ? 255 : static_cast<uint8_t>(audio.levelSmoothed);
        for (uint8_t j = 0; j < 8; j++) {
            const uint16_t pos16 = beatsin16(static_cast<uint8_t>(24u + j * 7u), now, 0, static_cast<uint16_t>(n - 1), 0, j * 23u);
            const size_t pos = pos16 < n ? pos16 : n - 1u;
            const uint8_t bri = qadd8(scale8(audio.bands[j * 2u], 180), scale8(drive, 80));
            addPixel(pos, palette(static_cast<uint8_t>(j * 32u + now / 40u), qadd8(bri, 60)), 0, qadd8(bri, 70));
        }
    }

    void renderPlasmoid(uint32_t now, const AudioView& audio) {
        const size_t n = lightCount();
        const uint8_t level = audio.levelSmoothed > 255 ? 255 : static_cast<uint8_t>(audio.levelSmoothed);
        for (size_t i = 0; i < n; i++) {
            const uint8_t x = posByte(i, n);
            const uint8_t a = sin8(static_cast<uint8_t>(x * 4u + now / 23u));
            const uint8_t b = cos8(static_cast<uint8_t>(x * 7u - now / 39u + level));
            const uint8_t field = static_cast<uint8_t>((static_cast<uint16_t>(a) + b + audio.bands[(i * 16u) / (n ? n : 1u)]) / 3u);
            const RGB c = palette(static_cast<uint8_t>(field + now / 80u), qadd8(field, scale8(level, 70)));
            setPixel(i, c);
        }
    }

    void renderMultiComet(uint32_t now, const AudioView& audio) {
        const size_t n = lightCount();
        if (n == 0) return;
        const uint8_t level = audio.levelSmoothed > 255 ? 255 : static_cast<uint8_t>(audio.levelSmoothed);
        const size_t tail = n / 10u + 4u;
        for (uint8_t c = 0; c < 7; c++) {
            const uint16_t bpm = static_cast<uint16_t>(24u + c * 8u + scale8(level, 40));
            const size_t head = (static_cast<uint64_t>(now + c * 700u) * bpm * n) / 60000u % n;
            for (size_t i = 0; i < n; i++) {
                const uint8_t bri = circularFalloff(i, head, n, tail);
                if (bri > 5) addPixel(i, palette(static_cast<uint8_t>(c * 36u + now / 36u), bri), cfg().white, qadd8(bri, scale8(level, 40)));
            }
        }
    }

    void renderStarburst(uint32_t now, const AudioView& audio) {
        const size_t n = lightCount();
        if (n == 0) return;
        const uint8_t drive = audio.level > 255 ? 255 : static_cast<uint8_t>(audio.level);
        for (uint8_t b = 0; b < 5; b++) {
            const uint32_t local = (now + b * 640u) % 2600u;
            const size_t center = (static_cast<size_t>(hash8(b, local / 2600u)) * n) / 255u;
            const size_t radius = (static_cast<uint64_t>(local) * (n / 2u + 1u)) / 2600u;
            for (size_t i = 0; i < n; i++) {
                const size_t d = circularDistance(i, center, n);
                const size_t edge = d > radius ? d - radius : radius - d;
                if (edge < n / 18u + 2u) {
                    const uint8_t bri = static_cast<uint8_t>(255u - (edge * 255u) / (n / 18u + 2u));
                    addPixel(i, palette(static_cast<uint8_t>(b * 47u + now / 33u), scale8(bri, qadd8(drive, 70))), cfg().white, bri);
                }
            }
        }
    }
};

}  // namespace mm
