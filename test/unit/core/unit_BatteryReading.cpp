/// @module BatteryReading

#include "doctest.h"
#include "core/util/BatteryReading.h"

#include <cstdint>
#include <string>
#include <utility>

namespace {
using Reading = mm::BatteryReading;
Reading::Config model() { return {2, 1, 0, 3000, 4200, 1000, true}; }
std::string document(const Reading& reading, uint32_t now) {
    char buffer[192]; mm::JsonSink sink(buffer, sizeof(buffer));
    reading.writeJson(sink, now);
    REQUIRE_FALSE(sink.overflowed());
    return buffer;
}
}

TEST_CASE("battery reading: unknown and absent are distinct from an empty battery") {
    Reading reading; uint8_t percent = 77;
    CHECK_FALSE(reading.acceptAdc(2100, 0));
    CHECK_FALSE(reading.percent(0, percent)); CHECK(percent == 77);
    CHECK(document(reading, 0).find("\"presence\":\"unknown\"") != std::string::npos);
    CHECK(document(reading, 0).find("\"percent\":null") != std::string::npos);
    reading.configure(model(), Reading::Presence::Absent);
    CHECK_FALSE(reading.acceptAdc(2100, 0));
    CHECK(document(reading, 0).find("\"presence\":\"absent\"") != std::string::npos);
    CHECK(document(reading, 0).find("\"percent\":null") != std::string::npos);
}

TEST_CASE("battery reading: divider ratios and signed offsets are explicit millivolts") {
    Reading reading; uint16_t mv = 0;
    auto config = model(); reading.configure(config, Reading::Presence::Present);
    REQUIRE(reading.acceptAdc(1800, 10)); REQUIRE(reading.lastVoltage(mv)); CHECK(mv == 3600);
    config.gainNumerator = 43; config.gainDenominator = 10;
    reading.configure(config, Reading::Presence::Present);
    REQUIRE(reading.acceptAdc(900, 20)); REQUIRE(reading.lastVoltage(mv)); CHECK(mv == 3870);
    config.offsetMv = -70; reading.configure(config, Reading::Presence::Present);
    REQUIRE(reading.acceptAdc(900, 30)); REQUIRE(reading.lastVoltage(mv)); CHECK(mv == 3800);
    config = model(); config.offsetMv = 960; reading.configure(config, Reading::Presence::Present);
    REQUIRE(reading.acceptAdc(1500, 40)); REQUIRE(reading.lastVoltage(mv)); CHECK(mv == 3960);
}

TEST_CASE("battery reading: voltage estimates require presence and explicit model opt-in") {
    Reading reading; uint8_t percent = 77; auto config = model();
    reading.configure(config, Reading::Presence::Unknown); REQUIRE(reading.acceptAdc(1800, 0));
    CHECK_FALSE(reading.percent(0, percent)); CHECK(percent == 77);
    config.estimateEnabled = false; reading.configure(config, Reading::Presence::Present);
    REQUIRE(reading.acceptAdc(1800, 0)); CHECK_FALSE(reading.percent(0, percent));
    config.estimateEnabled = true; config.fullMv = config.emptyMv;
    reading.configure(config, Reading::Presence::Present);
    REQUIRE(reading.acceptAdc(1800, 0)); CHECK_FALSE(reading.percent(0, percent));
}

TEST_CASE("battery reading: configured voltage estimates clamp and round without wrapping") {
    Reading reading; reading.configure(model(), Reading::Presence::Present); uint8_t percent = 0;
    for (const auto& [adc, expected] : {std::pair<uint16_t, uint8_t>{1400, 0}, {1500, 0}, {1800, 50}, {1803, 51}, {2100, 100}, {2200, 100}}) {
        REQUIRE(reading.acceptAdc(adc, 0)); REQUIRE(reading.percent(0, percent)); CHECK(percent == expected);
    }
}

TEST_CASE("battery reading: unavailable or zero ADC samples never replace a valid reading") {
    Reading reading; reading.configure(model(), Reading::Presence::Present);
    REQUIRE(reading.acceptAdc(1800, 100)); CHECK_FALSE(reading.acceptAdc(0, 500));
    uint16_t mv = 0; REQUIRE(reading.lastVoltage(mv)); CHECK(mv == 3600);
    uint32_t age = 0; REQUIRE(reading.age(500, age)); CHECK(age == 400);
    uint8_t percent = 77; REQUIRE(reading.percent(500, percent)); CHECK(percent == 50);
    CHECK_FALSE(reading.percent(1101, percent)); CHECK(percent == 50);
    CHECK(document(reading, 1101).find("\"state\":\"stale\"") != std::string::npos);
    CHECK(document(reading, 1101).find("\"percent\":null") != std::string::npos);
}

TEST_CASE("battery reading: positive offset cannot turn a zero ADC reading into a battery sample") {
    Reading reading;
    auto config = model();
    config.offsetMv = 120;
    reading.configure(config, Reading::Presence::Present);
    CHECK_FALSE(reading.acceptAdc(0, 1));
    uint16_t mv = 0;
    CHECK_FALSE(reading.lastVoltage(mv));
    REQUIRE(reading.acceptAdc(1800, 100));
    REQUIRE(reading.lastVoltage(mv));
    CHECK(mv == 3720);
    CHECK_FALSE(reading.acceptAdc(0, 500));
    REQUIRE(reading.lastVoltage(mv));
    CHECK(mv == 3720);
    uint32_t age = 0;
    REQUIRE(reading.age(500, age));
    CHECK(age == 400);
}

TEST_CASE("battery reading: freshness survives uptime wrap") {
    Reading reading; reading.configure(model(), Reading::Presence::Present);
    REQUIRE(reading.acceptAdc(1800, UINT32_MAX - 100));
    CHECK(reading.fresh(899));
    uint32_t age = 0; REQUIRE(reading.age(899, age)); CHECK(age == 1000);
    CHECK_FALSE(reading.fresh(900));
}

TEST_CASE("battery reading: a long-failed sensor cannot become fresh after the uptime wraps again") {
    Reading reading; reading.configure(model(), Reading::Presence::Present);
    REQUIRE(reading.acceptAdc(1800, 100));
    uint32_t age = 0; REQUIRE(reading.age(UINT32_MAX - 100, age));
    CHECK_FALSE(reading.fresh(UINT32_MAX - 100));
    CHECK_FALSE(reading.fresh(200));
    REQUIRE(reading.age(200, age)); CHECK(age == UINT32_MAX);
}

TEST_CASE("battery reading: changed calibration clears the old sample while unchanged prepare preserves it") {
    Reading reading; auto config = model(); reading.configure(config, Reading::Presence::Present);
    REQUIRE(reading.acceptAdc(1800, 100)); reading.configure(config, Reading::Presence::Present);
    uint32_t age = 0; REQUIRE(reading.age(500, age)); CHECK(age == 400);
    config.offsetMv = 10; reading.configure(config, Reading::Presence::Present);
    CHECK_FALSE(reading.age(500, age)); CHECK_FALSE(reading.fresh(500));
    uint8_t percent = 77; CHECK_FALSE(reading.percent(500, percent)); CHECK(percent == 77);
}

TEST_CASE("battery reading: invalid scaling and signed extremes supply no wrapped voltage") {
    Reading reading; auto config = model(); uint16_t mv = 123;
    config.gainDenominator = 0; reading.configure(config, Reading::Presence::Present);
    CHECK_FALSE(reading.acceptAdc(1800, 0)); CHECK_FALSE(reading.lastVoltage(mv)); CHECK(mv == 123);
    config = model(); config.gainNumerator = UINT32_MAX; reading.configure(config, Reading::Presence::Present);
    CHECK_FALSE(reading.acceptAdc(UINT16_MAX, 0));
    config = model(); config.offsetMv = INT32_MIN; reading.configure(config, Reading::Presence::Present);
    CHECK_FALSE(reading.acceptAdc(1800, 0));
    config = model(); config.staleAfterMs = 0; reading.configure(config, Reading::Presence::Present);
    CHECK_FALSE(reading.acceptAdc(1800, 0));
}
