#pragma once

#include "core/AudioFrame.h"
#include "core/color.h"
#include "core/math8.h"
#include "core/noise.h"
#include "light/Palette.h"
#include "light/effects/EffectBase.h"
#include "light/layers/Layer.h"
#include "platform/platform.h"

#include <cstdio>
#include <cmath>
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
    bool usesPhysicalLightOrder() const override { return true; }

    uint8_t playlist = 1;  // 0 = Static Animations, 1 = Audio Animations
    uint8_t preset = static_cast<uint8_t>(Preset::GEQ);
    bool autoPlay = true;
    bool idleBeat = false;
    uint8_t speed = 128;
    uint8_t effectIntensity = 128;
    uint8_t custom1 = 128;
    uint8_t custom2 = 128;
    uint8_t custom3 = 16;
    uint8_t paletteIndex = 0;
    uint8_t red = 255;
    uint8_t green = 133;
    uint8_t blue = 0;
    uint8_t white = 0;
    uint8_t secondaryRed = 0;
    uint8_t secondaryGreen = 0;
    uint8_t secondaryBlue = 0;
    bool option1 = false;
    bool option2 = false;
    bool option3 = false;
    uint8_t soundSimulation = 0;
    uint8_t audioMap = 0;
    uint8_t output = 255;

    void defineControls() override {
        const bool locked = lockedScene();
        if (locked) {
            const uint8_t idx = lockedPreset();
            preset = idx;
            playlist = playlistForPreset(idx);
            autoPlay = false;
        }
        auto hideLast = [&]() { controls_.setHidden(static_cast<uint8_t>(controls_.count() - 1), true); };
        if (!locked) {
            controls_.addSelect("playlist", playlist, kPlaylistOptions, kPlaylistCount);
            controls_.addSelect("preset", preset, kPresetOptions, kPresetCount);
            controls_.addBool("autoPlay", autoPlay);
        }
        controls_.addBool("idleBeat", idleBeat);
        controls_.addUint8("speed", speed, 0, 255);
        controls_.addUint8("effect intensity", effectIntensity, 0, 255);
        controls_.addUint8("custom 1", custom1, 0, 255);
        controls_.addUint8("custom 2", custom2, 0, 255);
        controls_.addUint8("custom 3", custom3, 0, 255);
        controls_.addPalette("palette", paletteIndex, mm::paletteOptions, mm::palettes::kCount);
        controls_.addUint8("red", red, 0, 255);
        hideLast();
        controls_.addUint8("green", green, 0, 255);
        hideLast();
        controls_.addUint8("blue", blue, 0, 255);
        hideLast();
        controls_.addUint8("white", white, 0, 255);
        hideLast();
        controls_.addUint8("secondary red", secondaryRed, 0, 255);
        hideLast();
        controls_.addUint8("secondary green", secondaryGreen, 0, 255);
        hideLast();
        controls_.addUint8("secondary blue", secondaryBlue, 0, 255);
        hideLast();
        controls_.addBool("option 1", option1);
        controls_.addBool("option 2", option2);
        controls_.addBool("option 3", option3);
        controls_.addUint8("sound simulation", soundSimulation, 0, 7);
        controls_.addUint8("audio map", audioMap, 0, 15);
        controls_.addUint8("output", output, 0, 255);
        controls_.addButton("source defaults");
    }

    void prepare() override {
        cursor_ = 0;
        const bool locked = lockedScene();
        activePreset_ = locked ? lockedPreset() : currentPreset(elapsed());
        if (locked) {
            preset = activePreset_;
            playlist = playlistForPreset(activePreset_);
            autoPlay = false;
        } else {
            observedPlaylist_ = playlist;
            observedAutoPlay_ = autoPlay;
            loadPresetControls(activePreset_);
        }
        resetPresetState(elapsed());
        twinkleAttemptedCount_ = static_cast<size_t>(-1);
        ensureTwinkles();
        ensureSceneState();
        setDynamicBytes(twinkleCount_ * (sizeof(RGB) + 2u) + sceneStateBytes_);
    }

    void onControlChanged(const char* controlName) override {
        if (!controlName) return;
        if (std::strcmp(controlName, "source defaults") == 0) {
            loadPresetControls(activePreset_);
            resetPresetState(elapsed());
            rebuildControls();
            return;
        }
        if (!lockedScene() &&
            (std::strcmp(controlName, "playlist") == 0 ||
             std::strcmp(controlName, "autoPlay") == 0 ||
             std::strcmp(controlName, "preset") == 0)) {
            cursor_ = 0;
            observedPlaylist_ = playlist;
            observedAutoPlay_ = autoPlay;
            activePreset_ = currentPreset(elapsed());
            loadPresetControls(activePreset_);
            resetPresetState(elapsed());
            rebuildControls();
            return;
        }
        if (std::strcmp(controlName, "palette") == 0 ||
            std::strcmp(controlName, "red") == 0 ||
            std::strcmp(controlName, "green") == 0 ||
            std::strcmp(controlName, "blue") == 0 ||
            std::strcmp(controlName, "white") == 0 ||
            std::strcmp(controlName, "secondary red") == 0 ||
            std::strcmp(controlName, "secondary green") == 0 ||
            std::strcmp(controlName, "secondary blue") == 0 ||
            std::strcmp(controlName, "custom 1") == 0 ||
            std::strcmp(controlName, "custom 2") == 0 ||
            std::strcmp(controlName, "custom 3") == 0 ||
            std::strcmp(controlName, "option 1") == 0 ||
            std::strcmp(controlName, "option 2") == 0 ||
            std::strcmp(controlName, "option 3") == 0 ||
            std::strcmp(controlName, "sound simulation") == 0 ||
            std::strcmp(controlName, "audio map") == 0) {
            paletteLoaded_ = 255;
            resetTwinkles();
        }
    }

    void release() override {
        releaseTwinkles();
        releaseSceneState();
        setDynamicBytes(0);
        MoonModule::release();
    }
    ~HelioWledPresetsEffect() override { releaseTwinkles(); releaseSceneState(); }

    void tick() MM_NONBLOCKING override {
        if (lightCount() == 0 || channelsPerLight() == 0 || !buffer()) return;
        ensureTwinkles();

        const uint32_t now = elapsed();
        const uint8_t next = lockedScene() ? lockedPreset() : currentPreset(now);
        if (next != activePreset_) {
            activePreset_ = next;
            if (!lockedScene()) loadPresetControls(activePreset_);
            resetPresetState(now);
            rebuildControls();
        }

        AudioView audio;
        readAudio(audio, now);

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

protected:
    virtual uint8_t lockedPreset() const { return 255; }

    bool lockedScene() const {
        const uint8_t idx = lockedPreset();
        return idx < kPresetCount;
    }

    void configureSinglePreset(uint8_t idx) {
        if (idx >= kPresetCount) idx = 0;
        preset = idx;
        playlist = playlistForPreset(idx);
        autoPlay = false;
        idleBeat = false;
        loadPresetControls(idx);
        activePreset_ = idx;
    }

    struct AudioView {
        uint16_t level = 0;
        uint16_t levelSmoothed = 0;
        uint16_t peakHz = 0;
        uint16_t peakMag = 0;
        uint8_t bands[16] = {};
    };

    static bool audioActive(const AudioView& audio) {
        if (audio.level > kAudioLevelGate || audio.levelSmoothed > kAudioLevelGate) return true;
        if (audio.peakHz > 80 && audio.peakMag > kAudioPeakMagGate) return true;
        uint8_t activeBands = 0;
        for (uint8_t band : audio.bands) {
            if (band > kAudioSingleBandGate) return true;
            if (band > kAudioBandGate && ++activeBands >= kAudioBandCountGate) return true;
        }
        return false;
    }

private:
    struct PresetConfig {
        uint8_t wledId;
        uint8_t speed;
        uint8_t intensity;
        uint8_t sourcePalette;
        const char* paletteName;
        RGB primary;
        uint8_t white;
        RGB secondary;
        uint8_t custom1;
        uint8_t custom2;
        uint8_t custom3;
        bool option1;
        bool option2;
        bool option3;
        uint8_t soundSimulation;
        uint8_t audioMap;
    };

    struct CandleState {
        uint8_t current;
        uint8_t target;
        uint8_t step;
    };

    struct RippleState {
        uint8_t state;
        uint8_t color;
        uint16_t position;
    };

    struct StarState {
        RGB color;
        uint8_t fragmentCount;
        uint32_t birthMs;
        uint32_t lastMs;
        int32_t velocityQ8;
        uint16_t origin;
        int32_t fragmentsQ8[10];
    };

    static constexpr uint16_t kAudioLevelGate = 24;
    static constexpr uint16_t kAudioPeakMagGate = 24;
    static constexpr uint8_t kAudioBandGate = 18;
    static constexpr uint8_t kAudioSingleBandGate = 48;
    static constexpr uint8_t kAudioBandCountGate = 2;

    inline static constexpr const char* kPlaylistOptions[] = {
        "Static Animations",
        "Audio Animations",
    };
    inline static constexpr uint8_t kPlaylistCount =
        static_cast<uint8_t>(sizeof(kPlaylistOptions) / sizeof(kPlaylistOptions[0]));

    inline static constexpr const char* kPresetOptions[] = {
        "Candle Glow",
        "Color Sparkles",
        "Amber Bands",
        "Lake",
        "Orbit",
        "Pink Glow",
        "Phase Shimmer",
        "Ripple",
        "Chase",
        "Starburst",
        "Gravity Center",
        "Juggling Dots",
        "Comets",
        "Level Meter",
        "Plasmoid",
        "Wide Spectrum",
        "Equalizer",
        "Gravity Bands",
        "Frequency Wave",
    };
    inline static constexpr uint8_t kPresetCount =
        static_cast<uint8_t>(sizeof(kPresetOptions) / sizeof(kPresetOptions[0]));

    inline static constexpr PresetConfig kConfigs[] = {
        {102,  53, 255,  0, "Rainbow",   {255, 133,   0}, 255, {  0,   0,   0}, 128, 128, 16, false, false, false, 0, 0},
        { 74, 128, 128, 28, "Hult",      {255, 133,   0}, 255, {  0,   0,   0}, 128, 128, 16, false, false, false, 0, 0},
        {155, 128, 128, 11, "Rainbow",   {255, 133,   0}, 255, {  0,   0,   0}, 128, 128, 16, false, false, false, 0, 0},
        { 75, 128, 128,  0, "Rainbow",   {255, 133,   0}, 255, {  0,   0,   0}, 128, 128, 16, false, false, false, 0, 0},
        { 47, 128,  16, 18, "Analogous", {255, 133,   0}, 255, {  0,   0,   0}, 128, 128, 16, false, false, false, 0, 0},
        { 65, 128, 128, 19, "Splash",    {255, 133,   0}, 255, {  0,   0,   0}, 128, 128,  0, false, false, false, 0, 0},
        {105, 128, 128, 45, "Tiamat",    {255, 133,   0}, 255, {  0,   0,   0}, 128, 128, 16, false, false, false, 0, 0},
        { 79, 128, 128,  0, "Rainbow",   {255, 133,   0}, 255, {  0,   0,   0}, 128, 128, 16, false, false, false, 0, 0},
        { 15, 135, 128, 73, "Orangery",  {255, 133,   0},   0, {  0,   0,   0}, 128, 128, 16, false, false, false, 0, 0},
        {192,  75, 128, 57, "Candy",     {255, 133,   0},   0, {  0,   0,   0}, 128, 128, 16, false, false, false, 0, 0},
        {156, 128, 128, 11, "Rainbow",   {255, 133,   0},   0, {  0,   0,   0}, 128, 128, 16, false, false, false, 0, 2},
        {130, 128, 255, 64, "Yelblu Hot",{255, 133,   0},   0, {  0,   0,   0}, 128, 128, 16, false, false, false, 0, 0},
        {191,  91,   7, 64, "Yelblu Hot",{255, 133,   0},   0, {  0,   0,   0}, 128, 128, 16, false, false, false, 1, 7},
        {136, 248, 128, 64, "Yelblu Hot",{255, 135,   0},   0, {  0,   0,   0}, 128, 128, 16, false, false, false, 0, 2},
        {133, 128,  80, 28, "Hult",      {255, 135,   0},   0, {  0,   0,   0}, 128, 128, 16, false, false, false, 0, 0},
        {155, 192, 128, 63, "Aqua Flash",{255, 135,   0},   0, {  0,   0,   0}, 128, 128, 16, true,  false, false, 0, 0},
        {139, 128, 128, 11, "Rainbow",   {255, 135,   0},   0, {111, 255,   0}, 255,  64, 16, false, false, false, 0, 0},
        {158,  71,  98,  1, "Orangery",  {255, 135,   0},   0, {111, 255,   0}, 128, 128, 16, false, false, false, 0, 0},
        {137, 202, 247,  1, "Rainbow",   {  0,   0, 255},   0, {111, 255,   0},   9, 255,  9, false, false, false, 0, 2},
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

    static bool containsPreset(const uint8_t* order, uint8_t count, uint8_t idx) {
        for (uint8_t i = 0; i < count; i++) if (order[i] == idx) return true;
        return false;
    }

    static uint8_t playlistForPreset(uint8_t idx) {
        return containsPreset(kStaticOrder, static_cast<uint8_t>(sizeof(kStaticOrder) / sizeof(kStaticOrder[0])), idx)
            ? 0 : 1;
    }

    uint8_t activePreset_ = static_cast<uint8_t>(Preset::GEQ);
    uint8_t cursor_ = 0;
    uint8_t observedPlaylist_ = 255;
    bool observedAutoPlay_ = false;
    uint32_t lastSwitchMs_ = 0;
    Palette presetPalette_{};
    uint8_t paletteLoaded_ = 255;
    char statusBuf_[64] = {};
    RGB* twinkleRgb_ = nullptr;
    uint8_t* twinkleWhite_ = nullptr;
    uint8_t* twinkleUp_ = nullptr;
    size_t twinkleCount_ = 0;
    size_t twinkleAttemptedCount_ = static_cast<size_t>(-1);
    uint32_t lastTwinkleStepMs_ = 0;
    uint8_t* sceneState_ = nullptr;
    size_t sceneStateBytes_ = 0;
    uint16_t smoothedPeakHz_ = 0;
    int32_t phasedPhaseQ8_ = 0;
    int16_t plasmoidPhaseA_ = 0;
    int16_t plasmoidPhaseB_ = 0;
    uint16_t noisePhaseA_ = 0;
    uint16_t noisePhaseB_ = 0;
    int gravityTop_ = -1;
    int gravityCounter_ = 0;
    uint8_t gravColorIndex_ = 0;
    size_t cometPositions_[16] = {};
    uint8_t cometArmed_ = 0;
    uint32_t lastCometStep_ = 0;
    uint16_t geqPeaks_[16] = {};
    uint32_t geqRippleStepMs_ = 0;
    uint32_t geqFrame_ = 0;
    uint8_t freqWaveStep_ = 255;
    Random8 rng_{0xC0107A55u};

    static constexpr uint32_t kTwinkleFrameMs = 24;

    static_assert(kPresetCount == static_cast<uint8_t>(Preset::Count),
                  "HELIO preset table mismatch");
    static_assert(sizeof(kConfigs) / sizeof(kConfigs[0]) == kPresetCount,
                  "HELIO preset config table mismatch");

    const PresetConfig& cfg() const { return kConfigs[activePreset_ < kPresetCount ? activePreset_ : 0]; }

    static uint8_t paletteIndexForName(const char* name) {
        if (!name) return 0;
        for (uint8_t i = 0; i < mm::palettes::kCount; i++) {
            if (std::strcmp(mm::palettes::kBuiltins[i].name, name) == 0) return i;
        }
        return 0;
    }

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

    RGB primary() const { return RGB{red, green, blue}; }
    RGB secondary() const { return RGB{secondaryRed, secondaryGreen, secondaryBlue}; }
    bool sourceUsesPrimary() const { return cfg().sourcePalette == 0 && paletteIndex == 0; }

    uint8_t scaled(uint8_t v, uint8_t bri) const {
        return scale8(scale8(v, bri), output);
    }

    uint8_t presetBri() const {
        const uint16_t v = static_cast<uint16_t>(effectIntensity) + 96u;
        return v > 255u ? 255u : static_cast<uint8_t>(v);
    }

    uint32_t tempoNow(uint32_t now) const {
        return static_cast<uint32_t>((static_cast<uint64_t>(now) * (16u + speed)) / 144u);
    }

    void loadPresetControls(uint8_t idx) {
        if (idx >= kPresetCount) idx = 0;
        const PresetConfig& c = kConfigs[idx];
        speed = c.speed;
        effectIntensity = c.intensity;
        paletteIndex = paletteIndexForName(c.paletteName);
        red = c.primary.r;
        green = c.primary.g;
        blue = c.primary.b;
        white = c.white;
        secondaryRed = c.secondary.r;
        secondaryGreen = c.secondary.g;
        secondaryBlue = c.secondary.b;
        custom1 = c.custom1;
        custom2 = c.custom2;
        custom3 = c.custom3;
        option1 = c.option1;
        option2 = c.option2;
        option3 = c.option3;
        soundSimulation = c.soundSimulation;
        audioMap = c.audioMap;
        paletteLoaded_ = 255;
    }

    void resetPresetState(uint32_t now) {
        paletteLoaded_ = 255;
        lastSwitchMs_ = now;
        smoothedPeakHz_ = 0;
        resetTwinkles();
        resetSceneState();
        clearFrame();
        refreshStatus();
    }

    void setPixel(size_t i, RGB c, uint8_t whiteValue = 0, uint8_t bri = 255) {
        const uint8_t cpl = channelsPerLight();
        uint8_t* buf = buffer();
        if (!buf || cpl == 0 || i >= lightCount()) return;
        const size_t off = i * cpl;
        if (off + cpl > bufferBytes()) return;
        if (cpl >= 1) buf[off + 0] = scaled(c.r, bri);
        if (cpl >= 2) buf[off + 1] = scaled(c.g, bri);
        if (cpl >= 3) buf[off + 2] = scaled(c.b, bri);
        if (cpl >= 4) buf[off + 3] = scale8(scale8(whiteValue, bri), output);
    }

    void addPixel(size_t i, RGB c, uint8_t whiteValue = 0, uint8_t bri = 255) {
        const uint8_t cpl = channelsPerLight();
        uint8_t* buf = buffer();
        if (!buf || cpl == 0 || i >= lightCount()) return;
        const size_t off = i * cpl;
        if (off + cpl > bufferBytes()) return;
        if (cpl >= 1) buf[off + 0] = qadd8(buf[off + 0], scaled(c.r, bri));
        if (cpl >= 2) buf[off + 1] = qadd8(buf[off + 1], scaled(c.g, bri));
        if (cpl >= 3) buf[off + 2] = qadd8(buf[off + 2], scaled(c.b, bri));
        if (cpl >= 4) buf[off + 3] = qadd8(buf[off + 3], scale8(scale8(whiteValue, bri), output));
    }

    static uint8_t blendByte(uint8_t from, uint8_t to, uint8_t amount) {
        const int16_t delta = static_cast<int16_t>(to) - static_cast<int16_t>(from);
        return static_cast<uint8_t>(static_cast<int16_t>(from) + (delta * amount) / 255);
    }

    void blendPixel(size_t i, RGB c, uint8_t whiteValue, uint8_t amount) {
        const uint8_t cpl = channelsPerLight();
        uint8_t* buf = buffer();
        if (!buf || cpl == 0 || i >= lightCount()) return;
        const size_t off = i * cpl;
        if (off + cpl > bufferBytes()) return;
        if (cpl >= 1) buf[off + 0] = blendByte(buf[off + 0], scale8(c.r, output), amount);
        if (cpl >= 2) buf[off + 1] = blendByte(buf[off + 1], scale8(c.g, output), amount);
        if (cpl >= 3) buf[off + 2] = blendByte(buf[off + 2], scale8(c.b, output), amount);
        if (cpl >= 4) buf[off + 3] = blendByte(buf[off + 3], scale8(whiteValue, output), amount);
    }

    void copyPixel(size_t to, size_t from) {
        const uint8_t cpl = channelsPerLight();
        uint8_t* buf = buffer();
        if (!buf || cpl == 0 || to >= lightCount() || from >= lightCount()) return;
        const size_t toOff = to * cpl;
        const size_t fromOff = from * cpl;
        if (toOff + cpl > bufferBytes() || fromOff + cpl > bufferBytes()) return;
        std::memmove(buf + toOff, buf + fromOff, cpl);
    }

    void fadeToBlackBy(uint8_t fadeBy) {
        if (fadeBy == 0) return;
        uint8_t* buf = buffer();
        const size_t bytes = bufferBytes();
        if (!buf) return;
        const uint8_t keep = static_cast<uint8_t>(255u - fadeBy);
        for (size_t i = 0; i < bytes; i++) buf[i] = scale8(buf[i], keep);
    }

    void fadeOut(uint8_t rate) {
        uint8_t* buf = buffer();
        const size_t bytes = bufferBytes();
        if (!buf) return;
        const uint16_t denominator = static_cast<uint16_t>(((255u - rate) >> 1u) + 1u);
        for (size_t i = 0; i < bytes; i++) {
            const uint8_t value = buf[i];
            if (value == 0) continue;
            const uint16_t step = static_cast<uint16_t>(value / denominator + 1u);
            buf[i] = step >= value ? 0 : static_cast<uint8_t>(value - step);
        }
    }

    static uint8_t cubicWave8(uint8_t phase) {
        const uint8_t triangle = triwave8(phase);
        const uint16_t square = (static_cast<uint16_t>(triangle) * triangle) / 255u;
        const uint16_t cube = (square * triangle) / 255u;
        const int16_t eased = static_cast<int16_t>(3u * square) - static_cast<int16_t>(2u * cube);
        return eased < 0 ? 0 : static_cast<uint8_t>(eased > 255 ? 255 : eased);
    }

    static int16_t signedBeat(uint8_t bpm, uint32_t now, int16_t low, int16_t high) {
        const uint8_t wave = sin8(beat8(bpm, now));
        return static_cast<int16_t>(low + (static_cast<int32_t>(wave) * (high - low)) / 255);
    }

    const Palette& presetPalette() {
        const uint8_t idx = paletteIndex < mm::palettes::kCount ? paletteIndex : 0;
        if (paletteLoaded_ != idx) {
            presetPalette_ = Palettes::fromBuiltin(idx);
            paletteLoaded_ = idx;
        }
        return presetPalette_;
    }

    RGB palette(uint8_t index, uint8_t bri = 255) {
        return colorFromPalette(presetPalette(), index, bri);
    }

    uint8_t presetWhite(uint8_t bri = 255) const {
        return scale8(white, bri);
    }

    uint8_t customBri(uint8_t v, uint8_t lo = 0, uint8_t hi = 255) const {
        return static_cast<uint8_t>(lo + (static_cast<uint16_t>(v) * (hi - lo)) / 255u);
    }

    void ensureTwinkles() {
        const size_t n = lightCount();
        if (n == twinkleCount_ && twinkleRgb_ && twinkleWhite_ && twinkleUp_) return;
        if (n == twinkleAttemptedCount_) return;
        releaseTwinkles();
        twinkleAttemptedCount_ = n;
        if (n == 0) { setDynamicBytes(0); return; }
        twinkleRgb_ = static_cast<RGB*>(platform::alloc(n * sizeof(RGB)));
        twinkleWhite_ = static_cast<uint8_t*>(platform::alloc(n));
        twinkleUp_ = static_cast<uint8_t*>(platform::alloc(n));
        if (!twinkleRgb_ || !twinkleWhite_ || !twinkleUp_) {
            releaseTwinkles();
            twinkleAttemptedCount_ = n;
            return;
        }
        twinkleCount_ = n;
        resetTwinkles();
        setDynamicBytes(n * (sizeof(RGB) + 2u));
    }

    void releaseTwinkles() {
        if (twinkleRgb_) { platform::free(twinkleRgb_); twinkleRgb_ = nullptr; }
        if (twinkleWhite_) { platform::free(twinkleWhite_); twinkleWhite_ = nullptr; }
        if (twinkleUp_) { platform::free(twinkleUp_); twinkleUp_ = nullptr; }
        twinkleCount_ = 0;
        twinkleAttemptedCount_ = static_cast<size_t>(-1);
        lastTwinkleStepMs_ = 0;
    }

    static size_t rippleCount(size_t n) {
        const size_t count = 1u + (n >> 2u);
        return count < 100u ? count : 100u;
    }

    static size_t starCount(size_t n) {
        const size_t count = 1u + (n >> 3u);
        return count < 42u ? count : 42u;
    }

    static size_t requiredSceneStateBytes(size_t n) {
        size_t bytes = n * sizeof(CandleState);
        const size_t rippleBytes = rippleCount(n) * sizeof(RippleState);
        const size_t starBytes = starCount(n) * sizeof(StarState);
        if (rippleBytes > bytes) bytes = rippleBytes;
        if (starBytes > bytes) bytes = starBytes;
        return bytes;
    }

    void ensureSceneState() {
        const size_t bytes = requiredSceneStateBytes(lightCount());
        if (sceneState_ && sceneStateBytes_ == bytes) return;
        releaseSceneState();
        if (bytes == 0) return;
        sceneState_ = static_cast<uint8_t*>(platform::alloc(bytes));
        if (!sceneState_) return;
        sceneStateBytes_ = bytes;
        std::memset(sceneState_, 0, sceneStateBytes_);
    }

    void releaseSceneState() {
        if (sceneState_) platform::free(sceneState_);
        sceneState_ = nullptr;
        sceneStateBytes_ = 0;
    }

    template<typename T>
    T* sceneState() {
        return sceneStateBytes_ >= sizeof(T) ? reinterpret_cast<T*>(sceneState_) : nullptr;
    }

    void resetSceneState() {
        if (sceneState_ && sceneStateBytes_) std::memset(sceneState_, 0, sceneStateBytes_);
        phasedPhaseQ8_ = 0;
        plasmoidPhaseA_ = 0;
        plasmoidPhaseB_ = 0;
        noisePhaseA_ = 0;
        noisePhaseB_ = 0;
        gravityTop_ = -1;
        gravityCounter_ = 0;
        gravColorIndex_ = 0;
        const size_t n = lightCount();
        for (size_t& position : cometPositions_) position = n;
        cometArmed_ = 0;
        lastCometStep_ = 0;
        std::memset(geqPeaks_, 0, sizeof(geqPeaks_));
        geqRippleStepMs_ = 0;
        geqFrame_ = 0;
        freqWaveStep_ = 255;
    }

    void resetTwinkles() {
        if (twinkleRgb_ && twinkleCount_) std::memset(twinkleRgb_, 0, twinkleCount_ * sizeof(RGB));
        if (twinkleWhite_ && twinkleCount_) std::memset(twinkleWhite_, 0, twinkleCount_);
        if (twinkleUp_ && twinkleCount_) std::memset(twinkleUp_, 0, twinkleCount_);
        lastTwinkleStepMs_ = 0;
    }

    bool twinkleOff(size_t i) const {
        return twinkleRgb_[i].r == 0 && twinkleRgb_[i].g == 0 &&
               twinkleRgb_[i].b == 0 && twinkleWhite_[i] == 0;
    }

    size_t randomLight(size_t n) {
        if (n == 0) return 0;
        if (n <= 0x10000u) return static_cast<size_t>(rng_.next16() % n);
        const uint32_t draw = (static_cast<uint32_t>(rng_.next16()) << 16) | rng_.next16();
        return static_cast<size_t>(draw % n);
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

    uint8_t mappedBand(size_t i, size_t n) const {
        if (n <= 1) return 0;
        switch (audioMap) {
            case 2: {  // Arc-ish: strongest bass in the center, highs toward the edges.
                const size_t center = n / 2u;
                const size_t d = circularDistance(i, center, n);
                return static_cast<uint8_t>((d * 32u) / n > 15u ? 15u : (d * 32u) / n);
            }
            case 7: {  // Pinwheel-ish: fold alternating lobes through the band range.
                const uint8_t base = static_cast<uint8_t>((i * 16u) / n);
                return static_cast<uint8_t>((base + ((i & 1u) ? 4u : 0u)) & 0x0Fu);
            }
            default:
                return static_cast<uint8_t>((i * 16u) / n);
        }
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
        std::snprintf(statusBuf_, sizeof(statusBuf_), "%s: %s", list, kPresetOptions[idx]);
        setStatus(statusBuf_);
    }

    void readAudio(AudioView& out, uint32_t now) const {
        const AudioFrame* f = AudioService::latestFrame();
        if (f) {
            out.level = f->level;
            out.levelSmoothed = f->levelSmoothed;
            out.peakHz = f->peakHz;
            out.peakMag = f->peakMag;
            for (uint8_t i = 0; i < 16; i++) {
                out.bands[i] = f->bands[i];
            }
        }
        if (!idleBeat || audioActive(out)) return;

        const uint8_t bpm = soundSimulation == 1 ? 120 : static_cast<uint8_t>(96u + soundSimulation * 8u);
        const uint8_t phase = beat8(bpm, now);
        const uint8_t beat = sin8(phase);
        const uint8_t barStep = static_cast<uint8_t>((phase >> 6) & 0x03u);
        const uint8_t stomp = soundSimulation == 1
            ? (barStep == 0 || barStep == 1 ? scale8(beat, 230) : scale8(sin8(static_cast<uint8_t>(phase + 96)), 180))
            : scale8(beat, beat);
        const uint8_t clap = soundSimulation == 1 && barStep == 2 ? scale8(beat, 210) : 0;
        const uint8_t kick = qadd8(stomp, clap);
        out.level = static_cast<uint16_t>(40u + scale8(kick, 190));
        out.levelSmoothed = static_cast<uint16_t>(50u + scale8(sin8(static_cast<uint8_t>(phase + 18)), 170));
        out.peakHz = static_cast<uint16_t>(80u + (static_cast<uint16_t>(sin8(static_cast<uint8_t>(phase + 40 + audioMap * 13u))) * 1800u) / 255u);
        out.peakMag = out.level;
        for (uint8_t i = 0; i < 16; i++) {
            const uint8_t carrier = sin8(static_cast<uint8_t>(phase + i * 17u));
            const uint8_t falloff = static_cast<uint8_t>(255u - i * 9u);
            const uint8_t bass = i < 3 ? scale8(kick, 210) : scale8(kick, 70);
            const uint8_t midClap = (soundSimulation == 1 && i >= 5 && i <= 9) ? scale8(clap, 160) : 0;
            out.bands[i] = scale8(qadd8(qadd8(bass, midClap), scale8(carrier, 72)), falloff);
        }
    }

    void renderCandle(uint32_t now) {
        const size_t n = lightCount();
        (void)now;
        CandleState* states = sceneState<CandleState>();
        if (!states || sceneStateBytes_ < n * sizeof(CandleState)) return;
        const uint8_t range = effectIntensity;
        const uint8_t randomRange = range >> 1u;
        const uint8_t speedFactor = speed > 252 ? 1 : speed > 99 ? 2 : speed > 49 ? 3 : 4;
        for (size_t i = 0; i < n; i++) {
            CandleState& state = states[i];
            if (state.step == 0) {
                state.current = 128;
                state.target = static_cast<uint8_t>(130u + rng_.below(4));
                state.step = 1;
            }
            bool reached = false;
            if (state.target > state.current) {
                state.current = qadd8(state.current, state.step);
                reached = state.current >= state.target;
            } else {
                state.current = qsub8(state.current, state.step);
                reached = state.current <= state.target;
            }
            if (reached) {
                uint16_t target = randomRange
                    ? static_cast<uint16_t>(rng_.below(randomRange) + rng_.below(randomRange))
                    : 0u;
                if (target < (randomRange >> 1u)) target = static_cast<uint16_t>((randomRange >> 1u) + (randomRange ? rng_.below(randomRange) : 0u));
                target += static_cast<uint16_t>(255u - range);
                state.target = static_cast<uint8_t>(target > 255u ? 255u : target);
                const uint8_t difference = state.target > state.current
                    ? static_cast<uint8_t>(state.target - state.current)
                    : static_cast<uint8_t>(state.current - state.target);
                state.step = static_cast<uint8_t>(difference >> speedFactor);
                if (state.step == 0) state.step = 1;
            }
            const RGB source = sourceUsesPrimary() ? primary() : palette(posByte(i, n));
            setPixel(i, blend(secondary(), source, state.current),
                     sourceUsesPrimary() ? scale8(white, state.current) : 0);
        }
    }

    void renderColorTwinkles(uint32_t now) {
        const size_t n = lightCount();
        ensureTwinkles();
        if (!twinkleRgb_ || !twinkleWhite_ || !twinkleUp_ || twinkleCount_ != n) return;

        if (lastTwinkleStepMs_ == 0 || now - lastTwinkleStepMs_ >= kTwinkleFrameMs) {
            const uint8_t fadeUpAmount = output > 28 ? static_cast<uint8_t>(8u + (speed >> 2))
                                                     : static_cast<uint8_t>(68u - output);
            const uint8_t fadeDownAmount = output > 28 ? static_cast<uint8_t>(8u + (speed >> 3))
                                                       : static_cast<uint8_t>(68u - output);
            const uint8_t keep = static_cast<uint8_t>(255u - fadeDownAmount);

            for (size_t i = 0; i < n; i++) {
                RGB& c = twinkleRgb_[i];
                if (twinkleUp_[i]) {
                    c.r = qadd8(c.r, nscale8_video(c.r, fadeUpAmount));
                    c.g = qadd8(c.g, nscale8_video(c.g, fadeUpAmount));
                    c.b = qadd8(c.b, nscale8_video(c.b, fadeUpAmount));
                    twinkleWhite_[i] = qadd8(twinkleWhite_[i], nscale8_video(twinkleWhite_[i], fadeUpAmount));
                    if (c.r == 255 || c.g == 255 || c.b == 255 || twinkleWhite_[i] == 255) {
                        twinkleUp_[i] = 0;
                    }
                } else {
                    c.r = scale8(c.r, keep);
                    c.g = scale8(c.g, keep);
                    c.b = scale8(c.b, keep);
                    twinkleWhite_[i] = scale8(twinkleWhite_[i], keep);
                }
            }

            for (size_t j = 0; j <= n / 50u; j++) {
                if (rng_.next8() <= effectIntensity) {
                    for (uint8_t attempt = 0; attempt < 5; attempt++) {
                        const size_t i = randomLight(n);
                        if (twinkleOff(i)) {
                            twinkleRgb_[i] = palette(rng_.next8(), 64);
                            twinkleWhite_[i] = 0;
                            twinkleUp_[i] = 1;
                            break;
                        }
                    }
                }
            }

            lastTwinkleStepMs_ = now;
        }

        for (size_t i = 0; i < n; i++) {
            setPixel(i, twinkleRgb_[i], twinkleWhite_[i]);
        }
    }

    void renderLake(uint32_t now) {
        const size_t n = lightCount();
        const uint8_t lakeSpeed = static_cast<uint8_t>(speed / 10u);
        const int16_t wave1 = signedBeat(static_cast<uint8_t>(lakeSpeed + 2u), now, -64, 64);
        const int16_t wave2 = signedBeat(static_cast<uint8_t>(lakeSpeed + 1u), now, -64, 64);
        const uint8_t wave3 = static_cast<uint8_t>(signedBeat(static_cast<uint8_t>(lakeSpeed + 2u), now, 0, 80));
        for (size_t i = 0; i < n; i++) {
            const uint8_t index = static_cast<uint8_t>(
                cos8(static_cast<uint8_t>(i * 15u + wave1)) / 2u +
                cubicWave8(static_cast<uint8_t>(i * 23u + wave2)) / 2u);
            const uint8_t luminance = index > wave3 ? static_cast<uint8_t>(index - wave3) : 0;
            const RGB color = sourceUsesPrimary() ? primary() : palette(index);
            setPixel(i, color, sourceUsesPrimary() ? white : 0, luminance);
        }
    }

    void renderLoading(uint32_t now) {
        const size_t n = lightCount();
        if (n == 0) return;
        const uint16_t counter = static_cast<uint16_t>(now * ((speed >> 2u) + 1u));
        const size_t head = (static_cast<uint32_t>(counter) * n) >> 16u;
        const size_t width = effectIntensity > 0 ? effectIntensity : 1u;
        for (size_t i = 0; i < n; i++) {
            const size_t distance = i > head ? head + n - i : head - i;
            const uint8_t amount = distance < width
                ? static_cast<uint8_t>((distance * 255u) / width)
                : 255u;
            const RGB background = palette(posByte(i, n));
            setPixel(i, blend(primary(), background, amount), scale8(white, static_cast<uint8_t>(255u - amount)));
        }
    }

    void renderPalettePink(uint32_t now) {
        const size_t n = lightCount();
        if (n == 0) return;
        uint16_t counter = 0;
        if (speed != 0) counter = static_cast<uint16_t>(now * ((speed >> 3u) + 1u)) >> 8u;
        for (size_t i = 0; i < n; i++) {
            const uint8_t colorIndex = static_cast<uint8_t>((static_cast<uint32_t>(i) * 255u) / n - counter);
            setPixel(i, palette(colorIndex));
        }
    }

    void renderPhased(uint32_t now) {
        const size_t n = lightCount();
        if (n == 0) return;
        phasedPhaseQ8_ += static_cast<int32_t>(speed) * 8;
        uint8_t index = static_cast<uint8_t>(now / 64u);
        const uint8_t indexStep = static_cast<uint8_t>(256u / n);
        const uint8_t cutoff = static_cast<uint8_t>(255u - effectIntensity);
        for (size_t i = 0; i < n; i++) {
            const int32_t phase = (phasedPhaseQ8_ * static_cast<int32_t>(i % 5u + 1u)) / 512;
            const uint8_t wave = cubicWave8(static_cast<uint8_t>((i + 1u) * 16u + phase));
            const uint8_t amount = wave > cutoff ? static_cast<uint8_t>(wave - cutoff) : 0;
            setPixel(i, blend(secondary(), palette(index), amount), 0);
            index = static_cast<uint8_t>(index + indexStep + (n > 256u ? 1u : 0u));
        }
    }

    void renderRipple(uint32_t now) {
        const size_t n = lightCount();
        if (n == 0) return;
        (void)now;
        clearFrame();
        RippleState* ripples = sceneState<RippleState>();
        const size_t count = rippleCount(n);
        if (!ripples || sceneStateBytes_ < count * sizeof(RippleState)) return;
        const uint8_t decay = static_cast<uint8_t>((speed >> 4u) + 1u);
        for (size_t r = 0; r < count; r++) {
            RippleState& ripple = ripples[r];
            uint16_t state = ripple.state;
            if (state != 0) {
                const uint16_t generation = state / decay;
                const uint16_t propagation = generation > 0
                    ? static_cast<uint16_t>((generation - 1u) * (speed + 1u))
                    : 0u;
                const int propagationInteger = propagation >> 8u;
                const uint8_t propagationFraction = static_cast<uint8_t>(propagation & 0xFFu);
                const uint8_t amplitude = state < 17u
                    ? triwave8(static_cast<uint8_t>((state - 1u) * 8u))
                    : static_cast<uint8_t>(255u - ((state - 17u) * 253u) / 238u);
                const int left = static_cast<int>(ripple.position) - propagationInteger - 1;
                const RGB color = sourceUsesPrimary() ? primary() : palette(ripple.color);
                for (int v = left; v < left + 4; v++) {
                    const uint8_t amount = scale8(cubicWave8(static_cast<uint8_t>((propagationFraction >> 2u) + (v - left) * 64)), amplitude);
                    if (v >= 0 && static_cast<size_t>(v) < n) blendPixel(static_cast<size_t>(v), color, sourceUsesPrimary() ? white : 0, amount);
                    const int mirrored = left + propagationInteger * 2 + 3 - (v - left);
                    if (mirrored >= 0 && static_cast<size_t>(mirrored) < n) blendPixel(static_cast<size_t>(mirrored), color, sourceUsesPrimary() ? white : 0, amount);
                }
                state = static_cast<uint16_t>(state + decay);
                ripple.state = state > 254u ? 0 : static_cast<uint8_t>(state);
            } else if (static_cast<uint16_t>(rng_.next16() % 15100u) <= effectIntensity) {
                ripple.state = 1;
                ripple.position = static_cast<uint16_t>(randomLight(n));
                ripple.color = rng_.next8();
            }
        }
    }

    void renderRunning(uint32_t now) {
        const size_t n = lightCount();
        if (n == 0) return;
        const uint8_t scale = effectIntensity >> 2u;
        const uint32_t counter = (static_cast<uint64_t>(now) * speed) >> 9u;
        for (size_t i = 0; i < n; i++) {
            const uint8_t amount = sin8(static_cast<uint8_t>(i * scale - counter));
            const RGB color = sourceUsesPrimary() ? primary() : palette(posByte(i, n));
            setPixel(i, blend(secondary(), color, amount), sourceUsesPrimary() ? scale8(white, amount) : 0);
        }
    }

    void renderFreqMap(uint32_t now, const AudioView& audio, bool reactive) {
        const size_t n = lightCount();
        if (n == 0) return;
        fadeOut(speed);
        size_t location = 0;
        uint8_t colorIndex = effectIntensity;
        uint8_t brightness = 0;
        if (reactive) {
            if (!audioActive(audio)) return;
            const uint16_t hz = audio.peakHz > 60 ? audio.peakHz : 60;
            if (option1) {
                smoothedPeakHz_ = smoothedPeakHz_ == 0
                    ? hz
                    : static_cast<uint16_t>((static_cast<uint32_t>(smoothedPeakHz_) * 5u + hz) / 6u);
            }
            const uint16_t mappedHz = option1 ? smoothedPeakHz_ : hz;
            const float logarithmic = (std::log10(static_cast<float>(mappedHz)) - 1.78f) / (3.71f - 1.78f);
            const float clamped = logarithmic < 0.0f ? 0.0f : logarithmic > 1.0f ? 1.0f : logarithmic;
            location = static_cast<size_t>(clamped * static_cast<float>(n - 1u) + 0.5f);
            colorIndex = static_cast<uint8_t>(effectIntensity + static_cast<uint8_t>(clamped * 255.0f));
            const float magnitude = static_cast<float>(audio.peakMag) * 0.25f;
            const uint16_t scaledMagnitude = static_cast<uint16_t>(std::sqrt(magnitude) * 16.0f);
            brightness = static_cast<uint8_t>(scaledMagnitude > 255u ? 255u : scaledMagnitude);
        } else {
            location = beatsin16(bpmFromSpeed(speed, 3, 11), now, 0, static_cast<uint16_t>(n - 1u));
            colorIndex = static_cast<uint8_t>(effectIntensity + posByte(location, n));
            brightness = 220;
        }
        blendPixel(location, palette(colorIndex), 0, brightness);
    }

    void renderGEQ(uint32_t now, const AudioView& audio) {
        const size_t n = lightCount();
        if (n == 0) return;
        const uint8_t bands = static_cast<uint8_t>(1u + (static_cast<uint16_t>(custom1) * 15u) / 255u);
        size_t columns = bands < 2u ? 2u : bands;
        const size_t maxColumns = (n + 1u) / 2u;
        if (columns > maxColumns) columns = maxColumns;
        if (columns <= 1u) return;
        const size_t rows = n / columns;
        if (rows <= 1u) return;
        const size_t offset = (n - rows * columns + 1u) / 2u;

        const uint8_t fadeDelay = static_cast<uint8_t>((256u - speed) / 64u);
        if (fadeDelay <= 1u || geqFrame_ % fadeDelay == 0u) fadeOut(speed);
        geqFrame_++;
        const bool rippleTime = now - geqRippleStepMs_ >= static_cast<uint32_t>(256u - effectIntensity);
        if (rippleTime) geqRippleStepMs_ = now;

        auto flatPixel = [rows, columns, offset, n](size_t x, size_t y) -> size_t {
            if (x >= columns || y >= rows) return n;
            y = rows - y - 1u;
            if (rows > 4u) y = (y & 1u) ? (rows + y) / 2u : (rows - 1u - y) / 2u;
            const size_t pixel = x * rows + y + offset;
            return pixel < n ? pixel : n;
        };

        uint16_t lastHeight = 0;
        for (size_t x = 0; x < columns; x++) {
            const uint8_t logicalBand = static_cast<uint8_t>((x * bands) / columns);
            const uint8_t band = bands > 1u
                ? static_cast<uint8_t>((static_cast<uint16_t>(logicalBand) * 15u) / (bands - 1u))
                : 0u;
            uint16_t height = audio.bands[band & 0x0Fu];
            if (option2 && x > 0u && x + 1u < columns) {
                const uint8_t nextLogical = static_cast<uint8_t>(((x + 1u) * bands) / columns);
                const uint8_t nextBand = bands > 1u
                    ? static_cast<uint8_t>((static_cast<uint16_t>(nextLogical) * 15u) / (bands - 1u))
                    : 0u;
                height = static_cast<uint16_t>((7u * height + 3u * lastHeight + 3u * audio.bands[nextBand & 0x0Fu]) / 12u);
                if (height > 255u) height = 255u;
            }
            lastHeight = audio.bands[band & 0x0Fu];
            size_t barHeight = (height * rows) / 255u;
            if (barHeight > rows) barHeight = rows;
            if (barHeight > geqPeaks_[x]) geqPeaks_[x] = static_cast<uint16_t>(barHeight);

            RGB lastColor{};
            for (size_t y = 0; y < barHeight; y++) {
                const uint8_t colorIndex = option1
                    ? static_cast<uint8_t>((y * 255u) / (rows - 1u))
                    : static_cast<uint8_t>(band * 17u);
                lastColor = palette(colorIndex);
                const size_t pixel = flatPixel(x, rows - 1u - y);
                if (pixel < n) setPixel(pixel, lastColor);
            }
            if (effectIntensity < 255u && geqPeaks_[x] > 0u && geqPeaks_[x] < rows) {
                const size_t pixel = flatPixel(x, rows - geqPeaks_[x]);
                const RGB peak = secondaryRed || secondaryGreen || secondaryBlue ? secondary() : lastColor;
                if (pixel < n) setPixel(pixel, peak);
            }
            if (rippleTime && geqPeaks_[x] > 0u) geqPeaks_[x]--;
        }
    }

    void renderNoiseMeter(uint32_t now, const AudioView& audio) {
        const size_t n = lightCount();
        if (n == 0) return;
        const uint8_t fadeRate = static_cast<uint8_t>(200u + (static_cast<uint16_t>(speed) * 54u) / 255u);
        fadeOut(fadeRate);
        const uint16_t raw = audioActive(audio) ? (audio.level > 255u ? 255u : audio.level) : 0u;
        const uint16_t smooth = audioActive(audio) ? (audio.levelSmoothed > 255u ? 255u : audio.levelSmoothed) : 0u;
        uint32_t width = (static_cast<uint32_t>(raw) * 2u * effectIntensity) / 255u;
        if (width > 255u) width = 255u;
        size_t lit = (width * n) / 255u;
        if (lit > n) lit = n;
        for (size_t i = 0; i < lit; i++) {
            const uint8_t index = inoise8(static_cast<uint32_t>(i) * smooth + noisePhaseA_,
                                          noisePhaseB_ + static_cast<uint32_t>(i) * smooth);
            setPixel(i, palette(index));
        }
        noisePhaseA_ = static_cast<uint16_t>(noisePhaseA_ + beatsin8(5, now, 0, 10));
        noisePhaseB_ = static_cast<uint16_t>(noisePhaseB_ + beatsin8(4, now, 0, 10));
    }

    void renderGravCenter(uint32_t now, const AudioView& audio) {
        const size_t n = lightCount();
        if (n == 0) return;
        fadeOut(251);
        const size_t center = n / 2u;
        const uint16_t level = audioActive(audio)
            ? (audio.levelSmoothed > 255u ? 255u : audio.levelSmoothed)
            : 0u;
        const uint16_t sample = static_cast<uint16_t>((static_cast<uint32_t>(level) * effectIntensity) / (255u * 8u));
        size_t reach = (static_cast<uint32_t>(sample) * n) / 32u;
        if (reach > center) reach = center;
        const uint8_t amount = static_cast<uint8_t>(sample * 8u > 255u ? 255u : sample * 8u);
        for (size_t i = 0; i < reach; i++) {
            const uint8_t index = inoise8(static_cast<uint32_t>(i) * sample + now,
                                          5000u + static_cast<uint32_t>(i) * sample);
            const RGB color = blend(secondary(), palette(index), amount);
            setPixel(center + i, color);
            if (center > i) setPixel(center - i - 1u, color);
        }
        const uint8_t gravity = static_cast<uint8_t>(8u - speed / 32u);
        if (static_cast<int>(reach) >= gravityTop_) gravityTop_ = static_cast<int>(reach) - 1;
        else if (gravityCounter_ % gravity == 0) gravityTop_--;
        if (gravityTop_ >= 0 && static_cast<size_t>(gravityTop_) < center) {
            const RGB peak = palette(static_cast<uint8_t>(now));
            setPixel(center + static_cast<size_t>(gravityTop_), peak);
            setPixel(center - 1u - static_cast<size_t>(gravityTop_), peak);
        }
        gravityCounter_ = (gravityCounter_ + 1) % gravity;
    }

    void renderGravFreq(uint32_t now, const AudioView& audio) {
        const size_t n = lightCount();
        if (n == 0) return;
        fadeToBlackBy(96);
        const size_t center = n / 2u;
        const uint16_t level = audioActive(audio)
            ? (audio.levelSmoothed > 255u ? 255u : audio.levelSmoothed)
            : 0u;
        const uint16_t sample = static_cast<uint16_t>((static_cast<uint32_t>(level) * effectIntensity) / (255u * 8u));
        size_t reach = (static_cast<uint32_t>(sample) * n) / 32u;
        if (reach > center) reach = center;
        uint8_t frequencyIndex = 0;
        if (audio.peakHz > 0) {
            const float mapped = (std::log(static_cast<float>(audio.peakHz > 60u ? audio.peakHz : 60u)) - std::log(60.0f)) /
                                 (std::log(11025.0f) - std::log(60.0f));
            const float clamped = mapped < 0.0f ? 0.0f : mapped > 1.0f ? 1.0f : mapped;
            frequencyIndex = static_cast<uint8_t>(clamped * 255.0f);
        }
        const uint8_t paletteOffset = static_cast<uint8_t>(
            (static_cast<uint16_t>(frequencyIndex) + gravColorIndex_) / 2u + now / 64u);
        for (size_t i = 0; i < reach; i++) {
            const RGB color = palette(paletteOffset);
            setPixel(center + i, color);
            if (center > i) setPixel(center - i - 1u, color);
        }
        const uint8_t gravity = static_cast<uint8_t>(8u - speed / 32u);
        if (static_cast<int>(reach) >= gravityTop_) gravityTop_ = static_cast<int>(reach) - 1;
        else if (gravityCounter_ % gravity == 0) gravityTop_--;
        if (gravityTop_ >= 0 && speed < 255u && static_cast<size_t>(gravityTop_) < center) {
            const RGB peak{128, 128, 128};
            setPixel(center + static_cast<size_t>(gravityTop_), peak);
            setPixel(center - 1u - static_cast<size_t>(gravityTop_), peak);
        }
        gravityCounter_ = (gravityCounter_ + 1) % gravity;
        gravColorIndex_ = frequencyIndex;
    }

    void renderFreqWave(uint32_t now, const AudioView& audio) {
        const size_t n = lightCount();
        if (n == 0) return;
        const uint8_t tick = speed < 255u
            ? static_cast<uint8_t>(((static_cast<uint64_t>(now) * 1000u) / (static_cast<uint32_t>(256u - speed) * 500u)) % 16u)
            : 0u;
        if (speed <= 254u && tick == freqWaveStep_) return;
        freqWaveStep_ = tick;

        const uint16_t level = audioActive(audio)
            ? (audio.levelSmoothed > 255u ? 255u : audio.levelSmoothed)
            : 0u;
        const uint8_t preamp = custom3 < 1u ? 1u : custom3 > 31u ? 31u : custom3;
        const float sensitivity = 0.5f * (0.5f + (static_cast<float>(preamp - 1u) * 9.5f) / 30.0f);
        float value = static_cast<float>(level) * effectIntensity * sensitivity / 256.0f;
        if (value > 255.0f) value = 255.0f;
        const uint8_t brightness = static_cast<uint8_t>(value);
        RGB color{};
        if (audioActive(audio) && audio.peakHz >= 80u && audio.peakHz <= 10800u) {
            const float low = static_cast<float>(80u + 3u * custom1);
            const float high = static_cast<float>(80u + 42u * custom2);
            float mapped = 0.0f;
            if (option1) {
                const float logLow = std::log(low);
                const float logHigh = std::log(high);
                if (std::fabs(logHigh - logLow) > 0.05f) mapped = (std::log(static_cast<float>(audio.peakHz)) - logLow) * 255.0f / (logHigh - logLow);
            } else if (high != low) {
                mapped = (static_cast<float>(audio.peakHz) - low) * 255.0f / (high - low);
            }
            if (mapped < 0.0f) mapped = 0.0f;
            if (mapped > 255.0f) mapped = 255.0f;
            color = hsvToRgb(static_cast<uint8_t>(mapped),
                             static_cast<uint8_t>(176u + brightness / 4u), brightness);
        }
        const size_t center = n / 2u;
        setPixel(center, color);
        for (size_t i = n - 1u; i > center; i--) copyPixel(i, i - 1u);
        for (size_t i = 0; i < center; i++) copyPixel(i, i + 1u);
    }

    void renderJuggles(uint32_t now, const AudioView& audio) {
        const size_t n = lightCount();
        if (n == 0) return;
        fadeOut(224);
        const uint8_t drive = audioActive(audio)
            ? static_cast<uint8_t>(audio.levelSmoothed > 255u ? 255u : audio.levelSmoothed)
            : 0;
        const uint8_t balls = static_cast<uint8_t>(1u + (effectIntensity >> 5));
        for (uint8_t j = 0; j < balls; j++) {
            const uint16_t pos16 = beatsin16(static_cast<uint8_t>(speed / 4u + j * 2u), now, 0, static_cast<uint16_t>(n - 1));
            const size_t pos = pos16 < n ? pos16 : n - 1u;
            setPixel(pos, blend(secondary(), palette(static_cast<uint8_t>(now / 4u + j * 2u)), drive));
        }
    }

    void renderPlasmoid(uint32_t now, const AudioView& audio) {
        const size_t n = lightCount();
        if (n == 0) return;
        fadeToBlackBy(48);
        plasmoidPhaseA_ = static_cast<int16_t>(plasmoidPhaseA_ + signedBeat(6, now, -4, 4));
        plasmoidPhaseB_ = static_cast<int16_t>(plasmoidPhaseB_ + signedBeat(7, now, -4, 4));
        const uint16_t level = audioActive(audio)
            ? (audio.levelSmoothed > 255u ? 255u : audio.levelSmoothed)
            : 0u;
        const uint16_t threshold = static_cast<uint16_t>((static_cast<uint32_t>(level) * effectIntensity) / 64u);
        const uint8_t frequencyA = static_cast<uint8_t>(1u + (3u * speed) / 32u);
        const uint8_t frequencyB = static_cast<uint8_t>(97u + (5u * speed) / 32u);
        for (size_t i = 0; i < n; i++) {
            uint8_t brightness = static_cast<uint8_t>(
                cubicWave8(static_cast<uint8_t>(i * frequencyA + plasmoidPhaseA_)) / 2u +
                cos8(static_cast<uint8_t>(i * frequencyB + plasmoidPhaseB_)) / 2u);
            const uint8_t colorIndex = brightness;
            if (threshold < brightness) brightness = 0;
            addPixel(i, blend(secondary(), palette(colorIndex), brightness));
        }
    }

    void renderMultiComet(uint32_t now, const AudioView& audio) {
        const size_t n = lightCount();
        if (n == 0) return;
        const uint32_t cycleTime = (255u - speed) / 4u > 0u ? (255u - speed) / 4u : 1u;
        const uint32_t step = now / cycleTime;
        if (lastCometStep_ == step) return;
        lastCometStep_ = step;
        fadeOut(static_cast<uint8_t>(254u - effectIntensity / 2u));

        const size_t armRange = n < 256u ? n : 256u;
        bool launched = false;
        for (size_t i = 0; i < 16u; i++) {
            if (cometPositions_[i] < n) {
                const size_t position = cometPositions_[i]++;
                setPixel(position, palette(posByte(position, n)));
                continue;
            }
            if (armRange > 0 && rng_.next16() % armRange < 3u) cometArmed_++;
            if (cometArmed_ > 2u) cometArmed_ = 2u;
            if (cometArmed_ > 0u && !launched && audioActive(audio) &&
                (audio.peakMag > kAudioPeakMagGate || audio.level > 104u)) {
                cometPositions_[i] = 0;
                cometArmed_--;
                launched = true;
            }
        }
    }

    void renderStarburst(uint32_t now, const AudioView& audio) {
        const size_t n = lightCount();
        if (n == 0) return;
        clearFrame();
        StarState* stars = sceneState<StarState>();
        const size_t count = starCount(n);
        if (!stars || sceneStateBytes_ < count * sizeof(StarState)) return;

        int birthRate = static_cast<int>(144u - (speed >> 1u));
        const uint16_t smooth = audio.levelSmoothed > 255u ? 255u : audio.levelSmoothed;
        const uint16_t raw = audio.level > 255u ? 255u : audio.level;
        int burstAdjustment = smooth > 159u ? 96 : 0;
        if (raw <= 56u) burstAdjustment = -64;
        birthRate -= burstAdjustment;
        if (birthRate < 4) birthRate = 4;
        if (birthRate > 144) birthRate = 144;

        for (size_t j = 0; j < count; j++) {
            StarState& star = stars[j];
            const bool create = audioActive(audio) && (audio.peakMag > kAudioPeakMagGate || raw > 48u) &&
                                rng_.below(static_cast<uint8_t>(birthRate)) == 0;
            if (create && star.birthMs == 0) {
                const size_t originRange = n > 1u ? n - 1u : 1u;
                star.origin = static_cast<uint16_t>(randomLight(originRange));
                star.color = hsvToRgb(rng_.next8(), 255, 255);
                const uint32_t first = rng_.next8();
                const uint32_t second = rng_.next8();
                star.velocityQ8 = static_cast<int32_t>((375u * first * second * 256u) / (255u * 255u));
                star.birthMs = now == 0 ? 1u : now;
                star.lastMs = now;
                const uint8_t upper = static_cast<uint8_t>(6u + (effectIntensity >> 5u));
                star.fragmentCount = rng_.below(3, upper > 10u ? 10u : upper);
                if (star.fragmentCount > 10u) star.fragmentCount = 10u;
                for (uint8_t i = 0; i < star.fragmentCount; i++) star.fragmentsQ8[i] = static_cast<int32_t>(star.origin) << 8u;
            }
        }

        for (size_t j = 0; j < count; j++) {
            StarState& star = stars[j];
            if (star.birthMs == 0) continue;
            const uint32_t dt = now - star.lastMs;
            for (uint8_t i = 0; i < star.fragmentCount; i++) {
                const uint8_t velocityBand = i >> 1u;
                star.fragmentsQ8[i] += static_cast<int32_t>((static_cast<int64_t>(star.velocityQ8) * dt * velocityBand) / 3000);
            }
            star.lastMs = now;
            const int32_t velocityScale = dt * 3u >= 1000u ? 0 : static_cast<int32_t>(1000u - dt * 3u);
            star.velocityQ8 = static_cast<int32_t>((static_cast<int64_t>(star.velocityQ8) * velocityScale) / 1000);

            const uint32_t age = now - star.birthMs;
            if (age > 1750u) {
                star.birthMs = 0;
                continue;
            }
            RGB color = star.color;
            uint8_t fade = 0;
            if (age < 250u) {
                color = blend(RGB{255, 255, 255}, color, static_cast<uint8_t>((age * 254u) / 250u));
            } else {
                fade = static_cast<uint8_t>(((age - 250u) * 255u) / 1500u);
                color = blend(color, secondary(), fade);
            }
            const int32_t radiusQ8 = static_cast<int32_t>((static_cast<uint32_t>(255u - fade) * 512u) / 255u);
            const int32_t originQ8 = static_cast<int32_t>(star.origin) << 8u;
            for (uint8_t i = 0; i < star.fragmentCount; i++) {
                for (uint8_t mirror = 0; mirror < 2u; mirror++) {
                    const int32_t location = mirror ? originQ8 * 2 - star.fragmentsQ8[i] : star.fragmentsQ8[i];
                    int32_t start = (location - radiusQ8) >> 8u;
                    int32_t end = (location + radiusQ8 + 255) >> 8u;
                    if (start < 0) start = 0;
                    if (end <= start) end = start + 1;
                    if (end > static_cast<int32_t>(n)) end = static_cast<int32_t>(n);
                    for (int32_t pixel = start; pixel < end; pixel++) setPixel(static_cast<size_t>(pixel), color);
                }
            }
        }
    }
};

template<uint8_t PresetIndex>
class HelioPresetSceneEffect : public HelioWledPresetsEffect {
public:
    HelioPresetSceneEffect() { configureSinglePreset(PresetIndex); }

protected:
    uint8_t lockedPreset() const override { return PresetIndex; }
};

class HelioCandleMultiEffect   : public HelioPresetSceneEffect<0> {};
class HelioColorTwinklesEffect : public HelioPresetSceneEffect<1> {};
class HelioFreqMapAmberEffect  : public HelioPresetSceneEffect<2> {};
class HelioLakeEffect          : public HelioPresetSceneEffect<3> {};
class HelioLoadingEffect       : public HelioPresetSceneEffect<4> {};
class HelioPinkPaletteEffect   : public HelioPresetSceneEffect<5> {};
class HelioPhasedEffect        : public HelioPresetSceneEffect<6> {};
class HelioRippleEffect        : public HelioPresetSceneEffect<7> {};
class HelioRunningEffect       : public HelioPresetSceneEffect<8> {};
class HelioStarburstEffect     : public HelioPresetSceneEffect<9> {};
class HelioGravCenterEffect    : public HelioPresetSceneEffect<10> {};
class HelioJugglesEffect       : public HelioPresetSceneEffect<11> {};
class HelioMultiCometEffect    : public HelioPresetSceneEffect<12> {};
class HelioNoiseMeterEffect    : public HelioPresetSceneEffect<13> {};
class HelioPlasmoidEffect      : public HelioPresetSceneEffect<14> {};
class HelioFreqMapWideEffect   : public HelioPresetSceneEffect<15> {};
class HelioGEQEffect           : public HelioPresetSceneEffect<16> {};
class HelioGravFreqEffect      : public HelioPresetSceneEffect<17> {};
class HelioFreqWaveEffect      : public HelioPresetSceneEffect<18> {};

}  // namespace mm
