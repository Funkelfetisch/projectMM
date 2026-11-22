/// @module SphereMeridianEffect
#include "doctest.h"
#include "core/services/AudioService.h"
#include "light/effects/SphereAntipodeDropEffect.h"
#include "light/effects/SphereBassBreathEffect.h"
#include "light/effects/SphereBeatEchoEffect.h"
#include "light/effects/SphereDayAndNightEffect.h"
#include "light/effects/SphereHarmonicsEffect.h"
#include "light/effects/SphereKaleidoBloomEffect.h"
#include "light/effects/SphereMeridianEffect.h"
#include "light/effects/SphereMirrorSparksEffect.h"
#include "light/effects/SphereOrbitExchangeEffect.h"
#include "light/effects/SphereSpectrumHaloEffect.h"
#include "light/effects/SphereTideEffect.h"
#include "light/layouts/GridLayout.h"
#include "light/layouts/Layouts.h"
#include "platform/platform.h"

#include <memory>

namespace {

/// A strip of `count` RGBW lights showing one effect, as the sphere's ring does.
struct Ring {
    mm::Layouts layouts;
    mm::GridLayout grid;
    mm::Layer layer;
    explicit Ring(mm::EffectBase& effect, uint16_t count) {
        grid.width = count; grid.height = 1; grid.depth = 1;
        layouts.addChild(&grid);
        layer.setLayouts(&layouts); layer.setChannelsPerLight(4);
        layer.addChild(&effect); layer.applyState(); layer.tick();
    }
    /// Show the frame for `ms` on the shared test clock; 0 would hand the clock back to real time, so the first frame is at 1 ms.
    void at(uint32_t ms) { mm::platform::setTestNowMs(ms ? ms : 1); layer.tick(); }
    const uint8_t* light(size_t i) { return layer.buffer().data() + 4 * i; }
    int brightness(size_t i) { const uint8_t* p = light(i); return std::max({p[0], p[1], p[2]}); }
};

/// The simulated song the audio service plays, so the music effects have something to follow.
struct Song {
    mm::AudioService audio;
    Song() { audio.mode = mm::AudioService::kSimMode; audio.simulate = 0; audio.applyState(); }
    ~Song() { audio.release(); }
};

template<typename Effect>
void paintsEveryRing() {
    for (uint16_t count : {311, 1, 2, 310}) {
        CAPTURE(count);
        Song song;
        Effect effect;
        Ring ring(effect, count);
        REQUIRE(ring.layer.buffer().count() == count);
        bool lit = false;
        for (uint32_t t : {0u, 8u, 1000u, 2500u, 5000u, 17000u, 61000u, 3600000u, 4000000000u}) {
            song.audio.tick();
            ring.at(t);
            for (size_t i = 0; i < count; ++i) {
                CHECK(ring.light(i)[3] == 0);
                lit |= ring.brightness(i) > 0;
            }
        }
        if (count > 2) CHECK(lit);
    }
}

}  // namespace

TEST_CASE("Every sphere effect lights a ring of any size in RGB to music, its white channel dark, from the first frame to days later") {
    paintsEveryRing<mm::SphereMeridianEffect>();
    paintsEveryRing<mm::SphereAntipodeDropEffect>();
    paintsEveryRing<mm::SphereHarmonicsEffect>();
    paintsEveryRing<mm::SphereDayAndNightEffect>();
    paintsEveryRing<mm::SphereOrbitExchangeEffect>();
    paintsEveryRing<mm::SphereTideEffect>();
    paintsEveryRing<mm::SphereKaleidoBloomEffect>();
    paintsEveryRing<mm::SphereSpectrumHaloEffect>();
    paintsEveryRing<mm::SphereBassBreathEffect>();
    paintsEveryRing<mm::SphereBeatEchoEffect>();
    paintsEveryRing<mm::SphereMirrorSparksEffect>();
}

TEST_CASE("Spectrum Halo shows every band twice, on lights straight across the sphere from each other") {
    mm::SphereSpectrumHaloEffect effect;
    effect.idle = 16;  // a silent room, so the bands rest at the idle glow
    Ring ring(effect, 310);
    ring.at(5000);
    for (size_t i = 0; i < 155; ++i) {
        CAPTURE(i);
        for (int c = 0; c < 3; ++c) CHECK(ring.light(i)[c] == ring.light(i + 155)[c]);
    }
}

TEST_CASE("Day and Night puts midnight straight across from noon") {
    mm::SphereDayAndNightEffect effect;
    effect.lights = 0;  // no towns, so the night is only its blue
    Ring ring(effect, 310);
    for (uint32_t t : {0u, 7000u, 21000u}) {
        CAPTURE(t);
        ring.at(t);
        size_t noon = 0;
        for (size_t i = 0; i < 310; ++i)
            if (ring.light(i)[1] + ring.light(i)[2] > ring.light(noon)[1] + ring.light(noon)[2]) noon = i;
        const uint8_t* day = ring.light(noon);
        CHECK(day[0] == 255);
        CHECK(day[2] >= 170);  // the whitest place is noon
        const uint8_t* night = ring.light((noon + 155) % 310);
        CHECK(night[0] == 0);
        CHECK(night[2] < 40);  // deep blue
    }
}

TEST_CASE("Meridian lights both ends of its line, half a turn apart") {
    mm::SphereMeridianEffect effect;
    effect.tail = 1;  // ends only, so the tails cannot blur where the ends are
    Ring ring(effect, 310);
    for (uint32_t t = 0; t <= 3000; t += 20) ring.at(t);
    size_t end = 0;
    for (size_t i = 0; i < 310; ++i) if (ring.brightness(i) > ring.brightness(end)) end = i;
    CHECK(ring.brightness(end) > 100);
    const size_t across = (end + 155) % 310;
    CHECK(std::max({ring.brightness(across), ring.brightness((across + 1) % 310), ring.brightness((across + 309) % 310)}) > 100);
    CHECK(ring.brightness((end + 78) % 310) == 0);  // nothing crosswise to the line
}

TEST_CASE("Antipode Drop fades its drops out and never leaves the ring dark") {
    for (uint8_t rate : {uint8_t{0}, uint8_t{150}, uint8_t{255}}) {
        CAPTURE(rate);
        mm::SphereAntipodeDropEffect effect;
        effect.rate = rate;
        Ring ring(effect, 310);
        long before = -1;
        for (uint32_t t = 1; t <= 30000; t += 10) {
            ring.at(t);
            long total = 0;
            for (size_t i = 0; i < 310; ++i) total += ring.brightness(i);
            CAPTURE(t);
            CHECK(total > 0);                                    // something is always lit
            if (before >= 0) CHECK(total >= before - 1200);  // a frame dims, it never cuts a drop off
            before = total;
        }
    }
}

TEST_CASE("Orbit Exchange keeps both tails as the lights pass, so the ring never loses them at once") {
    mm::SphereOrbitExchangeEffect effect;
    effect.flash = 0;  // the tails alone, so a vanishing tail cannot hide behind the flash
    Ring ring(effect, 310);
    long before = -1;
    for (uint32_t t = 1; t <= 20000; t += 10) {
        ring.at(t);
        long total = 0;
        for (size_t i = 0; i < 310; ++i) total += ring.brightness(i);
        CAPTURE(t);
        if (before >= 0) CHECK(total >= before - 1200);
        before = total;
    }
}

template<typename Effect>
void landsOnItsReflections() {
    Song song;
    Effect effect;
    Ring ring(effect, 310);
    for (uint32_t t = 1; t <= 6000; t += 10) {
        song.audio.tick();
        ring.at(t);
        if (t % 500 != 1) continue;
        CAPTURE(t);
        for (size_t i = 1; i < 155; ++i) {
            CAPTURE(i);
            for (int c = 0; c < 3; ++c) {
                CHECK(std::abs(ring.light(i)[c] - ring.light(i + 155)[c]) <= 3);  // straight across
                CHECK(std::abs(ring.light(i)[c] - ring.light(310 - i)[c]) <= 3);  // across the line, in reverse order
            }
        }
    }
}

TEST_CASE("The music sphere effects draw the same light straight across the ball and mirrored across their line, so every reflection lands on its own image") {
    landsOnItsReflections<mm::SphereBeatEchoEffect>();
    landsOnItsReflections<mm::SphereMirrorSparksEffect>();
}

TEST_CASE("Bass Breath's glow is the same at both ends of its line and on both sides of it while the line stands still") {
    Song song;
    mm::SphereBassBreathEffect effect;
    effect.turn = 0;
    Ring ring(effect, 310);
    for (uint32_t t = 1; t <= 6000; t += 10) {
        song.audio.tick();
        ring.at(t);
    }
    for (size_t i = 1; i < 155; ++i) {
        CAPTURE(i);
        for (int c = 0; c < 3; ++c) {
            CHECK(std::abs(ring.light(i)[c] - ring.light(i + 155)[c]) <= 3);
            CHECK(std::abs(ring.light(i)[c] - ring.light(310 - i)[c]) <= 3);
        }
    }
}
