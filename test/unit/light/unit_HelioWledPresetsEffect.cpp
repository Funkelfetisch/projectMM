// @module HelioWledPresetsEffect

#include "doctest.h"
#include "core/AudioService.h"
#include "light/effects/HelioWledPresetsEffect.h"
#include "light/layouts/GridLayout.h"
#include "light/layouts/Layouts.h"
#include "light/layers/Layer.h"
#include "platform/platform.h"

#include <array>
#include <cstring>

namespace {

struct SilentAudio {
    mm::AudioService audio;

    SilentAudio() { audio.applyState(); }

    ~SilentAudio() { audio.release(); }
};

struct SignalAudio {
    mm::AudioService audio;

    SignalAudio() {
        audio.mode = mm::AudioService::kSimMode;
        audio.simulate = 0;
        audio.applyState();
    }

    void tick() { audio.tick(); }

    ~SignalAudio() { audio.release(); }
};

struct HelioAudioGateProbe : mm::HelioWledPresetsEffect {
    static bool active(uint16_t level, uint16_t levelSmoothed, uint16_t peakHz, uint16_t peakMag,
                       uint8_t band0 = 0, uint8_t band1 = 0, uint8_t band2 = 0) {
        AudioView audio;
        audio.level = level;
        audio.levelSmoothed = levelSmoothed;
        audio.peakHz = peakHz;
        audio.peakMag = peakMag;
        audio.bands[0] = band0;
        audio.bands[1] = band1;
        audio.bands[2] = band2;
        return audioActive(audio);
    }
};

struct RenderStats {
    size_t lit = 0;
    size_t distinct = 0;
    uint8_t max = 0;
};

template<typename Effect>
RenderStats renderSceneStats() {
    SilentAudio silence;
    CHECK(mm::AudioService::latestFrame()->level == 0);
    CHECK(mm::AudioService::latestFrame()->peakHz == 0);

    mm::Layouts layouts;
    mm::GridLayout grid;
    mm::Layer layer;
    Effect effect;

    grid.width = 96;
    grid.height = 1;
    grid.depth = 1;
    layouts.addChild(&grid);
    layer.setLayouts(&layouts);
    layer.setChannelsPerLight(3);
    layer.addChild(&effect);
    if constexpr (requires(Effect& e) { e.idleBeat = false; }) {
        effect.idleBeat = false;
    }

    layer.applyState();

    const uint32_t sampleTimes[] = {
        1000u, 1050u, 1125u, 1210u, 1400u, 1700u, 2100u, 2600u, 3200u,
    };
    for (uint32_t t : sampleTimes) {
        mm::platform::setTestNowMs(t);
        layer.tick();
    }
    mm::platform::setTestNowMs(0);

    RenderStats stats;
    uint32_t seen[96] = {};
    auto& buf = layer.buffer();
    for (size_t i = 0; i < buf.count(); i++) {
        const size_t off = i * 3;
        const uint8_t r = buf.data()[off + 0];
        const uint8_t g = buf.data()[off + 1];
        const uint8_t b = buf.data()[off + 2];
        const uint8_t m = r > g ? (r > b ? r : b) : (g > b ? g : b);
        if (m > 4) stats.lit++;
        if (m > stats.max) stats.max = m;
        const uint32_t packed = (static_cast<uint32_t>(r) << 16) |
                                (static_cast<uint32_t>(g) << 8) |
                                static_cast<uint32_t>(b);
        if (packed == 0) continue;
        bool known = false;
        for (size_t j = 0; j < stats.distinct; j++) {
            if (seen[j] == packed) { known = true; break; }
        }
        if (!known && stats.distinct < 96) seen[stats.distinct++] = packed;
    }
    return stats;
}

template<typename Effect>
bool rendersAnyOnSilence() {
    SilentAudio silence;
    CHECK(mm::AudioService::latestFrame()->level == 0);
    CHECK(mm::AudioService::latestFrame()->peakHz == 0);

    mm::Layouts layouts;
    mm::GridLayout grid;
    mm::Layer layer;
    Effect effect;

    grid.width = 64;
    grid.height = 1;
    grid.depth = 1;
    layouts.addChild(&grid);
    layer.setLayouts(&layouts);
    layer.setChannelsPerLight(3);
    layer.addChild(&effect);
    if constexpr (requires(Effect& e) { e.idleBeat = false; }) {
        effect.idleBeat = false;
    }

    layer.applyState();

    mm::platform::setTestNowMs(1000);
    layer.tick();
    mm::platform::setTestNowMs(0);

    auto& buf = layer.buffer();
    for (size_t i = 0; i < buf.bytes(); i++) {
        if (buf.data()[i] != 0) return true;
    }
    return false;
}

template<typename Effect>
bool rendersWithSignal() {
    SignalAudio signal;

    mm::Layouts layouts;
    mm::GridLayout grid;
    mm::Layer layer;
    Effect effect;

    grid.width = 64;
    grid.height = 1;
    grid.depth = 1;
    layouts.addChild(&grid);
    layer.setLayouts(&layouts);
    layer.setChannelsPerLight(3);
    layer.addChild(&effect);
    if constexpr (requires(Effect& e) { e.idleBeat = false; }) {
        effect.idleBeat = false;
    }

    layer.applyState();

    for (uint32_t t = 1000; t < 1800; t += 50) {
        mm::platform::setTestNowMs(t);
        signal.tick();
        layer.tick();
        auto& buf = layer.buffer();
        for (size_t i = 0; i < buf.bytes(); i++) {
            if (buf.data()[i] != 0) {
                mm::platform::setTestNowMs(0);
                return true;
            }
        }
    }
    mm::platform::setTestNowMs(0);
    return false;
}

template<typename Effect>
void checkSceneDefaults(const char* name, uint8_t preset, uint8_t playlist,
                        uint8_t speed, uint8_t intensity, const char* palette,
                        uint8_t white) {
    Effect effect;
    CAPTURE(name);
    CHECK(effect.preset == preset);
    CHECK(effect.playlist == playlist);
    CHECK_FALSE(effect.autoPlay);
    CHECK_FALSE(effect.idleBeat);
    CHECK(effect.speed == speed);
    CHECK(effect.effectIntensity == intensity);
    REQUIRE(effect.paletteIndex < mm::palettes::kCount);
    CHECK(std::strcmp(mm::palettes::kBuiltins[effect.paletteIndex].name, palette) == 0);
    CHECK(effect.white == white);
}

uint8_t paletteIndex(const char* name) {
    for (uint8_t i = 0; i < mm::palettes::kCount; i++) {
        if (std::strcmp(mm::palettes::kBuiltins[i].name, name) == 0) return i;
    }
    FAIL_CHECK("missing palette");
    return 0;
}

}  // namespace

TEST_CASE("HELIO native scene defaults match the saved WLED presets") {
    checkSceneDefaults<mm::HelioCandleMultiEffect>("Candle Glow", 0, 0, 53, 255, "Rainbow", 255);
    checkSceneDefaults<mm::HelioColorTwinklesEffect>("Color Sparkles", 1, 0, 128, 128, "Hult", 255);
    checkSceneDefaults<mm::HelioFreqMapAmberEffect>("Amber Bands", 2, 1, 128, 128, "Rainbow", 255);
    checkSceneDefaults<mm::HelioLakeEffect>("Lake", 3, 0, 128, 128, "Rainbow", 255);
    checkSceneDefaults<mm::HelioLoadingEffect>("Orbit", 4, 0, 128, 16, "Analogous", 255);
    checkSceneDefaults<mm::HelioPinkPaletteEffect>("Pink Glow", 5, 0, 128, 128, "Splash", 255);
    checkSceneDefaults<mm::HelioPhasedEffect>("Phase Shimmer", 6, 0, 128, 128, "Tiamat", 255);
    checkSceneDefaults<mm::HelioRippleEffect>("Ripple", 7, 0, 128, 128, "Rainbow", 255);
    checkSceneDefaults<mm::HelioRunningEffect>("Chase", 8, 0, 135, 128, "Orangery", 0);
    checkSceneDefaults<mm::HelioStarburstEffect>("Starburst", 9, 1, 75, 128, "Candy", 0);
    checkSceneDefaults<mm::HelioGravCenterEffect>("Gravity Center", 10, 1, 128, 128, "Rainbow", 0);
    checkSceneDefaults<mm::HelioJugglesEffect>("Juggling Dots", 11, 1, 128, 255, "Yelblu Hot", 0);
    checkSceneDefaults<mm::HelioMultiCometEffect>("Comets", 12, 1, 91, 7, "Yelblu Hot", 0);
    checkSceneDefaults<mm::HelioNoiseMeterEffect>("Level Meter", 13, 1, 248, 128, "Yelblu Hot", 0);
    checkSceneDefaults<mm::HelioPlasmoidEffect>("Plasmoid", 14, 1, 128, 80, "Hult", 0);
    checkSceneDefaults<mm::HelioFreqMapWideEffect>("Wide Spectrum", 15, 1, 192, 128, "Aqua Flash", 0);
    checkSceneDefaults<mm::HelioGEQEffect>("Equalizer", 16, 1, 128, 128, "Rainbow", 0);
    checkSceneDefaults<mm::HelioGravFreqEffect>("Gravity Bands", 17, 1, 71, 98, "Orangery", 0);
    checkSceneDefaults<mm::HelioFreqWaveEffect>("Frequency Wave", 18, 1, 202, 247, "Rainbow", 0);

    mm::HelioGEQEffect equalizer;
    CHECK(equalizer.custom1 == 255);
    CHECK(equalizer.custom2 == 64);

    mm::HelioFreqWaveEffect frequencyWave;
    CHECK(frequencyWave.custom1 == 9);
    CHECK(frequencyWave.custom2 == 255);
    CHECK(frequencyWave.custom3 == 9);
    CHECK(frequencyWave.audioMap == 2);
}

TEST_CASE("HELIO audio gate ignores low-level mic noise") {
    CHECK_FALSE(HelioAudioGateProbe::active(6, 6, 120, 6, 6));
    CHECK_FALSE(HelioAudioGateProbe::active(18, 18, 0, 0, 18, 18));
    CHECK_FALSE(HelioAudioGateProbe::active(0, 0, 440, 18, 22));
}

TEST_CASE("HELIO audio gate accepts real audio energy") {
    CHECK(HelioAudioGateProbe::active(32, 0, 0, 0));
    CHECK(HelioAudioGateProbe::active(0, 32, 0, 0));
    CHECK(HelioAudioGateProbe::active(0, 0, 440, 32));
    CHECK(HelioAudioGateProbe::active(0, 0, 0, 0, 22, 23));
    CHECK(HelioAudioGateProbe::active(0, 0, 0, 0, 64));
}

TEST_CASE("HELIO audio scenes stay black on silence without demo audio") {
    CHECK_FALSE(rendersAnyOnSilence<mm::HelioStarburstEffect>());
    CHECK_FALSE(rendersAnyOnSilence<mm::HelioGravCenterEffect>());
    CHECK_FALSE(rendersAnyOnSilence<mm::HelioJugglesEffect>());
    CHECK_FALSE(rendersAnyOnSilence<mm::HelioMultiCometEffect>());
    CHECK_FALSE(rendersAnyOnSilence<mm::HelioNoiseMeterEffect>());
    CHECK_FALSE(rendersAnyOnSilence<mm::HelioPlasmoidEffect>());
    CHECK_FALSE(rendersAnyOnSilence<mm::HelioFreqMapWideEffect>());
    CHECK_FALSE(rendersAnyOnSilence<mm::HelioGEQEffect>());
    CHECK_FALSE(rendersAnyOnSilence<mm::HelioGravFreqEffect>());
    CHECK_FALSE(rendersAnyOnSilence<mm::HelioFreqWaveEffect>());
}

TEST_CASE("HELIO audio scenes render when fed signal") {
    CHECK(rendersWithSignal<mm::HelioStarburstEffect>());
    CHECK(rendersWithSignal<mm::HelioGravCenterEffect>());
    CHECK(rendersWithSignal<mm::HelioJugglesEffect>());
    CHECK(rendersWithSignal<mm::HelioMultiCometEffect>());
    CHECK(rendersWithSignal<mm::HelioNoiseMeterEffect>());
    CHECK(rendersWithSignal<mm::HelioPlasmoidEffect>());
    CHECK(rendersWithSignal<mm::HelioFreqMapWideEffect>());
    CHECK(rendersWithSignal<mm::HelioGEQEffect>());
    CHECK(rendersWithSignal<mm::HelioGravFreqEffect>());
    CHECK(rendersWithSignal<mm::HelioFreqWaveEffect>());
}

TEST_CASE("HELIO static scene modules render a detailed idle frame") {
    auto check = [](const char* name, RenderStats stats) {
        CAPTURE(name);
        CHECK(stats.max > 24);
        CHECK(stats.lit >= 6);
        CHECK(stats.distinct >= 3);
    };

    check("Candle Glow", renderSceneStats<mm::HelioCandleMultiEffect>());
    check("Color Sparkles", renderSceneStats<mm::HelioColorTwinklesEffect>());
    check("Amber Bands", renderSceneStats<mm::HelioFreqMapAmberEffect>());
    check("Lake", renderSceneStats<mm::HelioLakeEffect>());
    check("Orbit", renderSceneStats<mm::HelioLoadingEffect>());
    check("Pink Glow", renderSceneStats<mm::HelioPinkPaletteEffect>());
    check("Phase Shimmer", renderSceneStats<mm::HelioPhasedEffect>());
    check("Ripple", renderSceneStats<mm::HelioRippleEffect>());
    check("Chase", renderSceneStats<mm::HelioRunningEffect>());
}

TEST_CASE("HELIO Color Sparkles keeps animating with palettes containing dim colors") {
    SilentAudio silence;
    mm::Layouts layouts;
    mm::GridLayout grid;
    mm::Layer layer;
    mm::HelioColorTwinklesEffect effect;

    grid.width = 222;
    grid.height = 1;
    grid.depth = 1;
    layouts.addChild(&grid);
    layer.setLayouts(&layouts);
    layer.setChannelsPerLight(3);
    effect.paletteIndex = paletteIndex("Yelblu");
    effect.speed = 127;
    effect.effectIntensity = 128;
    effect.output = 255;
    layer.addChild(&effect);
    layer.applyState();

    constexpr uint32_t kFrameMs = 24;
    constexpr uint32_t kDurationMs = 120000;
    constexpr uint32_t kObservationMs = 10000;
    std::array<uint8_t, 222 * 3> previous{};
    size_t changingFrames = 0;
    size_t brightFrames = 0;
    bool havePrevious = false;

    for (uint32_t t = kFrameMs; t <= kDurationMs; t += kFrameMs) {
        mm::platform::setTestNowMs(t);
        layer.tick();
        if (t <= kDurationMs - kObservationMs) continue;

        const auto& buffer = layer.buffer();
        REQUIRE(buffer.bytes() == previous.size());
        if (havePrevious && std::memcmp(previous.data(), buffer.data(), previous.size()) != 0) {
            changingFrames++;
        }
        havePrevious = true;

        uint8_t brightest = 0;
        for (size_t i = 0; i < buffer.bytes(); i++) {
            if (buffer.data()[i] > brightest) brightest = buffer.data()[i];
        }
        if (brightest > 32) brightFrames++;
        std::memcpy(previous.data(), buffer.data(), previous.size());
    }
    mm::platform::setTestNowMs(0);

    CHECK(changingFrames > 300);
    CHECK(brightFrames > 300);
}

TEST_CASE("HELIO native physical scenes use physical light order") {
    mm::HelioCandleMultiEffect candle;
    mm::HelioFreqMapAmberEffect amber;
    mm::HelioLoadingEffect orbit;
    mm::HelioRunningEffect chase;
    mm::HelioStarburstEffect starburst;
    mm::HelioGravCenterEffect gravCenter;
    mm::HelioJugglesEffect juggles;
    mm::HelioMultiCometEffect comets;
    mm::HelioPlasmoidEffect plasmoid;
    mm::HelioGravFreqEffect gravFreq;
    mm::HelioFreqWaveEffect freqWave;

    CHECK(candle.usesPhysicalLightOrder());
    CHECK(amber.usesPhysicalLightOrder());
    CHECK(orbit.usesPhysicalLightOrder());
    CHECK(chase.usesPhysicalLightOrder());
    CHECK(starburst.usesPhysicalLightOrder());
    CHECK(gravCenter.usesPhysicalLightOrder());
    CHECK(juggles.usesPhysicalLightOrder());
    CHECK(comets.usesPhysicalLightOrder());
    CHECK(plasmoid.usesPhysicalLightOrder());
    CHECK(gravFreq.usesPhysicalLightOrder());
    CHECK(freqWave.usesPhysicalLightOrder());
}
