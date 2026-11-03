/// @module platform
/// The real ESP32 ADC implementation reaches a recording SDK seam, once for S3 and once for the legacy ADC1-only policy.

#include "doctest.h"
#include "platform/platform.h"
#include "core/util/TryLock.h"
#include "adc_fixture_sdk.h"

#include <memory>

#define CONFIG_IDF_TARGET_ESP32S3 1
#define SOC_ADC_CALIB_CHAN_COMPENS_SUPPORTED 0
#define ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED 1
#define ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED 0
#define platform adc_fixture_platform
#include "platform/esp32/platform_esp32_adc.cpp"
#undef platform
#undef CONFIG_IDF_TARGET_ESP32S3
#undef ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
#undef ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED

#define CONFIG_IDF_TARGET_ESP32S3 0
#define CONFIG_IDF_TARGET_ESP32 1
#define ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED 0
#define ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED 1
#define platform legacy_adc_fixture_platform
#include "platform/esp32/platform_esp32_adc.cpp"
#undef platform
#undef CONFIG_IDF_TARGET_ESP32S3
#undef CONFIG_IDF_TARGET_ESP32
#undef SOC_ADC_CALIB_CHAN_COMPENS_SUPPORTED
#undef ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
#undef ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED

#define CONFIG_IDF_TARGET_ESP32S3 1
#define SOC_ADC_CALIB_CHAN_COMPENS_SUPPORTED 1
#define ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED 1
#define ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED 0
#define platform channel_adc_fixture_platform
#include "platform/esp32/platform_esp32_adc.cpp"
#undef platform
#undef CONFIG_IDF_TARGET_ESP32S3
#undef SOC_ADC_CALIB_CHAN_COMPENS_SUPPORTED
#undef ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
#undef ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED

namespace {
namespace s3 = mm::adc_fixture_platform;
namespace legacy = mm::legacy_adc_fixture_platform;
namespace perChannel = mm::channel_adc_fixture_platform;

template<class Unit, size_t Count>
void resetUnits(Unit (&units)[Count]) {
    for (auto& unit : units) {
        std::destroy_at(&unit);
        std::construct_at(&unit);
    }
}

struct AdcFixture {
    AdcFixture() { reset(); }
    ~AdcFixture() { reset(); }
    static void reset() {
        resetUnits(s3::g_adcUnits);
        resetUnits(legacy::g_adcUnits);
        resetUnits(perChannel::g_adcUnits);
        adc_fixture::state = adc_fixture::State{};
    }
};
} // namespace

TEST_CASE("ESP32 ADC: S3 battery pins use ADC2 channels and their own calibration") {
    AdcFixture fixture;
    auto& sdk = adc_fixture::state;
    uint16_t millivolts = 17;
    REQUIRE(s3::adcReadMv(1, millivolts));
    CHECK(millivolts == 1100);
    REQUIRE(s3::adcReadMv(15, millivolts));
    CHECK(millivolts == 2200);
    REQUIRE(s3::adcReadMv(16, millivolts));
    CHECK(millivolts == 2200);
    REQUIRE(s3::adcReadMv(15, millivolts));
    REQUIRE(sdk.opened.size() == 2);
    CHECK(sdk.opened[0].unit_id == ADC_UNIT_1);
    CHECK(sdk.opened[1].unit_id == ADC_UNIT_2);
    CHECK(sdk.opened[1].ulp_mode == 0);
    REQUIRE(sdk.configured.size() == 3);
    CHECK(sdk.configured[1].unit == ADC_UNIT_2);
    CHECK(sdk.configured[1].channel == ADC_CHANNEL_4);
    CHECK(sdk.configured[2].channel == ADC_CHANNEL_5);
    CHECK(sdk.configured[1].config.atten == ADC_ATTEN_DB_12);
    CHECK(sdk.configured[1].config.bitwidth == ADC_BITWIDTH_DEFAULT);
    REQUIRE(sdk.curves.size() == 2);
    CHECK(sdk.curves[0].unit_id == ADC_UNIT_1);
    CHECK(sdk.curves[1].unit_id == ADC_UNIT_2);
    CHECK(sdk.curves[1].chan == ADC_CHANNEL_4);
    CHECK(sdk.curves[1].atten == ADC_ATTEN_DB_12);
    CHECK(sdk.curves[1].bitwidth == ADC_BITWIDTH_DEFAULT);
    REQUIRE(sdk.conversions.size() == 4);
    CHECK(sdk.conversions[0].handle != sdk.conversions[1].handle);
    CHECK(sdk.conversions[1].handle == sdk.conversions[2].handle);
    REQUIRE(sdk.reads.size() == 4);
    CHECK(sdk.reads[2].unit == ADC_UNIT_2);
    CHECK(sdk.reads[2].channel == ADC_CHANNEL_5);
    CHECK(sdk.conversions[1].raw == sdk.raw);
}

TEST_CASE("ESP32 ADC: invalid mappings never initialize or read a converter") {
    AdcFixture fixture;
    for (uint8_t gpio : {uint8_t{248}, uint8_t{249}, uint8_t{250}, uint8_t{255}}) {
        uint16_t raw = 77;
        uint16_t millivolts = 88;
        CHECK_FALSE(s3::adcRead(gpio, raw));
        CHECK_FALSE(s3::adcReadMv(gpio, millivolts));
        CHECK(raw == 77);
        CHECK(millivolts == 88);
    }
    CHECK(adc_fixture::state.opened.empty());
    CHECK(adc_fixture::state.reads.empty());
}

TEST_CASE("ESP32 ADC: a timed-out read preserves caller data and skips calibration") {
    AdcFixture fixture;
    auto& sdk = adc_fixture::state;
    sdk.readResult = ESP_ERR_TIMEOUT;
    uint16_t raw = 77;
    uint16_t millivolts = 88;
    CHECK_FALSE(s3::adcRead(15, raw));
    CHECK_FALSE(s3::adcReadMv(15, millivolts));
    CHECK(raw == 77);
    CHECK(millivolts == 88);
    CHECK(sdk.curves.empty());
    sdk.readResult = ESP_OK;
    REQUIRE(s3::adcReadMv(15, millivolts));
    CHECK(millivolts == 2200);
    CHECK(sdk.opened.size() == 1);
    CHECK(sdk.configured.size() == 1);
}

TEST_CASE("ESP32 ADC: raw and calibrated conversion failures preserve caller data") {
    AdcFixture fixture;
    auto& sdk = adc_fixture::state;
    uint16_t millivolts = 88;
    REQUIRE(s3::adcReadMv(15, millivolts));
    sdk.conversionResult = ESP_FAIL;
    millivolts = 88;
    CHECK_FALSE(s3::adcReadMv(15, millivolts));
    CHECK(millivolts == 88);
    sdk.conversionResult = ESP_OK;
    for (int invalid : {-1, 65536}) {
        sdk.millivolts[1] = invalid;
        CHECK_FALSE(s3::adcReadMv(15, millivolts));
        CHECK(millivolts == 88);
    }
    for (int invalid : {-1, 65536}) {
        sdk.raw = invalid;
        uint16_t raw = 77;
        CHECK_FALSE(s3::adcRead(15, raw));
        CHECK(raw == 77);
        CHECK_FALSE(s3::adcReadMv(15, millivolts));
        CHECK(millivolts == 88);
    }
    CHECK(sdk.curves.size() == 1);
}

TEST_CASE("ESP32 ADC: unavailable calibration is cached per unit without inventing voltage") {
    AdcFixture fixture;
    auto& sdk = adc_fixture::state;
    sdk.curveResult = ESP_ERR_NOT_SUPPORTED;
    uint16_t millivolts = 88;
    CHECK_FALSE(s3::adcReadMv(15, millivolts));
    sdk.curveResult = ESP_OK;
    CHECK_FALSE(s3::adcReadMv(16, millivolts));
    CHECK(millivolts == 88);
    REQUIRE(sdk.curves.size() == 1);
    REQUIRE(s3::adcReadMv(1, millivolts));
    CHECK(millivolts == 1100);
    REQUIRE(sdk.curves.size() == 2);
    CHECK(sdk.curves[1].unit_id == ADC_UNIT_1);
    uint16_t raw = 77;
    CHECK(s3::adcRead(15, raw));
    CHECK(raw == sdk.raw);
}

TEST_CASE("ESP32 ADC: calibration allocation failure can recover on the next sample") {
    AdcFixture fixture;
    auto& sdk = adc_fixture::state;
    sdk.curveResult = ESP_ERR_NO_MEM;
    uint16_t millivolts = 88;
    CHECK_FALSE(s3::adcReadMv(15, millivolts));
    CHECK(millivolts == 88);
    sdk.curveResult = ESP_OK;
    REQUIRE(s3::adcReadMv(15, millivolts));
    CHECK(millivolts == 2200);
    CHECK(sdk.curves.size() == 2);
    CHECK(sdk.opened.size() == 1);
    CHECK(sdk.configured.size() == 1);
}

TEST_CASE("ESP32 ADC: unit and channel initialization failures retry without poisoning their caches") {
    AdcFixture fixture;
    auto& sdk = adc_fixture::state;
    sdk.openResult = ESP_ERR_NO_MEM;
    uint16_t raw = 77;
    CHECK_FALSE(s3::adcRead(15, raw));
    CHECK(raw == 77);
    sdk.openResult = ESP_OK;
    sdk.configureResult = ESP_FAIL;
    CHECK_FALSE(s3::adcRead(15, raw));
    CHECK(raw == 77);
    sdk.configureResult = ESP_OK;
    REQUIRE(s3::adcRead(15, raw));
    CHECK(raw == sdk.raw);
    CHECK(sdk.opened.size() == 2);
    CHECK(sdk.configured.size() == 2);
}

TEST_CASE("ESP32 ADC: same-unit contention refuses immediately while the other unit can progress") {
    AdcFixture fixture;
    auto& sdk = adc_fixture::state;
    uint16_t nestedSame = 77;
    uint16_t nestedOther = 88;
    bool sameSucceeded = true;
    bool otherSucceeded = false;
    sdk.duringRead = [&] {
        sameSucceeded = s3::adcReadMv(2, nestedSame);
        otherSucceeded = s3::adcReadMv(15, nestedOther);
    };
    uint16_t millivolts = 99;
    REQUIRE(s3::adcReadMv(1, millivolts));
    CHECK_FALSE(sameSucceeded);
    CHECK(nestedSame == 77);
    CHECK(otherSucceeded);
    CHECK(nestedOther == 2200);
    CHECK(millivolts == 1100);
    CHECK(sdk.reads.size() == 2);
    REQUIRE(s3::adcReadMv(2, nestedSame));
    CHECK(nestedSame == 1100);
}

TEST_CASE("ESP32 ADC: the legacy target refuses ADC2 before opening or sampling hardware") {
    AdcFixture fixture;
    uint16_t raw = 77;
    uint16_t millivolts = 88;
    CHECK_FALSE(legacy::adcRead(25, raw));
    CHECK_FALSE(legacy::adcReadMv(25, millivolts));
    CHECK(raw == 77);
    CHECK(millivolts == 88);
    CHECK(adc_fixture::state.opened.empty());
    CHECK(adc_fixture::state.configured.empty());
    CHECK(adc_fixture::state.reads.empty());
    CHECK(adc_fixture::state.lines.empty());
}

TEST_CASE("ESP32 ADC: legacy ADC1 uses line fitting without an invented reference voltage") {
    AdcFixture fixture;
    auto& sdk = adc_fixture::state;
    uint16_t millivolts = 88;
    REQUIRE(legacy::adcReadMv(32, millivolts));
    CHECK(millivolts == 1100);
    CHECK(sdk.curves.empty());
    REQUIRE(sdk.lines.size() == 1);
    CHECK(sdk.lines[0].unit_id == ADC_UNIT_1);
    CHECK(sdk.lines[0].atten == ADC_ATTEN_DB_12);
    CHECK(sdk.lines[0].bitwidth == ADC_BITWIDTH_DEFAULT);
    CHECK(sdk.lines[0].default_vref == 0);
    REQUIRE(legacy::adcReadMv(32, millivolts));
    CHECK(sdk.lines.size() == 1);
}

TEST_CASE("ESP32 ADC: a channel-compensated target retains a calibration per channel") {
    AdcFixture fixture;
    uint16_t millivolts = 88;
    REQUIRE(perChannel::adcReadMv(1, millivolts));
    REQUIRE(perChannel::adcReadMv(2, millivolts));
    REQUIRE(perChannel::adcReadMv(1, millivolts));
    auto& sdk = adc_fixture::state;
    REQUIRE(sdk.curves.size() == 2);
    CHECK(sdk.curves[0].chan == ADC_CHANNEL_0);
    CHECK(sdk.curves[1].chan == ADC_CHANNEL_1);
    REQUIRE(sdk.conversions.size() == 3);
    CHECK(sdk.conversions[0].handle != sdk.conversions[1].handle);
    CHECK(sdk.conversions[0].handle == sdk.conversions[2].handle);
}
