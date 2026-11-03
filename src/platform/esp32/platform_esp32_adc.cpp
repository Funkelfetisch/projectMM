/// Calibrated oneshot ADC readings, with one retained peripheral and calibration cache per unit.
///
/// ADC2 is enabled on ESP32-S3, whose oneshot hardware arbitrates with Wi-Fi. Other targets retain the ADC1-only policy.
/// Cache initialization, conversion and calibration share a nonblocking per-unit latch, so a sensor worker cannot race a render-side input.

#include "platform/platform.h"
#include "core/util/TryLock.h"

#include "sdkconfig.h"
#include "soc/soc_caps.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include <cstddef>
#include <cstdint>

namespace mm::platform {
namespace {
constexpr size_t kAdcChannels = ADC_CHANNEL_9 + 1;
#if defined(CONFIG_IDF_TARGET_ESP32S3) && CONFIG_IDF_TARGET_ESP32S3
constexpr size_t kAdcUnits = 2;
#else
constexpr size_t kAdcUnits = 1;
#endif
#if defined(SOC_ADC_CALIB_CHAN_COMPENS_SUPPORTED) && SOC_ADC_CALIB_CHAN_COMPENS_SUPPORTED
constexpr size_t kCalibrationSlots = kAdcChannels;
#else
constexpr size_t kCalibrationSlots = 1;
#endif
struct Calibration {
    adc_cali_handle_t handle = nullptr;
    bool unsupported = false;
};
struct AdcUnit {
    TryLock lock{};
    adc_oneshot_unit_handle_t handle = nullptr;
    bool ready[kAdcChannels] = {};
    Calibration calibrations[kCalibrationSlots] = {};
};
AdcUnit g_adcUnits[kAdcUnits];

struct AdcPin {
    AdcUnit* device = nullptr;
    adc_unit_t unit = ADC_UNIT_1;
    adc_channel_t channel = ADC_CHANNEL_0;
};

bool resolvePin(uint8_t gpio, AdcPin& pin) {
    adc_unit_t unit;
    adc_channel_t channel;
    if (adc_oneshot_io_to_channel(gpio, &unit, &channel) != ESP_OK) return false;
    if (unit != ADC_UNIT_1) {
#if defined(CONFIG_IDF_TARGET_ESP32S3) && CONFIG_IDF_TARGET_ESP32S3
        if (unit != ADC_UNIT_2) return false;
#else
        return false;
#endif
    }
    const int index = static_cast<int>(channel);
    if (index < 0 || static_cast<size_t>(index) >= kAdcChannels) return false;
    pin = {&g_adcUnits[unit == ADC_UNIT_1 ? 0 : 1], unit, channel};
    return true;
}

bool readRawLocked(const AdcPin& pin, uint16_t& raw) {
    auto& unit = *pin.device;
    if (!unit.handle) {
        adc_oneshot_unit_init_cfg_t config{};
        config.unit_id = pin.unit;
        adc_oneshot_unit_handle_t handle = nullptr;
        if (adc_oneshot_new_unit(&config, &handle) != ESP_OK) return false;
        unit.handle = handle;
    }
    if (!unit.ready[pin.channel]) {
        adc_oneshot_chan_cfg_t config{};
        config.atten = ADC_ATTEN_DB_12;
        config.bitwidth = ADC_BITWIDTH_DEFAULT;
        if (adc_oneshot_config_channel(unit.handle, pin.channel, &config) != ESP_OK) return false;
        unit.ready[pin.channel] = true;
    }
    int result = 0;
    if (adc_oneshot_read(unit.handle, pin.channel, &result) != ESP_OK || result < 0 || result > UINT16_MAX) return false;
    raw = static_cast<uint16_t>(result);
    return true;
}

adc_cali_handle_t calibrationLocked(const AdcPin& pin) {
    auto& calibration = pin.device->calibrations[kCalibrationSlots == 1 ? 0 : pin.channel];
    if (calibration.handle || calibration.unsupported) return calibration.handle;
    adc_cali_handle_t handle = nullptr;
    esp_err_t result = ESP_ERR_NOT_SUPPORTED;
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t config{};
    config.unit_id = pin.unit;
    config.chan = pin.channel;
    config.atten = ADC_ATTEN_DB_12;
    config.bitwidth = ADC_BITWIDTH_DEFAULT;
    result = adc_cali_create_scheme_curve_fitting(&config, &handle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t config{};
    config.unit_id = pin.unit;
    config.atten = ADC_ATTEN_DB_12;
    config.bitwidth = ADC_BITWIDTH_DEFAULT;
    result = adc_cali_create_scheme_line_fitting(&config, &handle);
#endif
    if (result == ESP_OK) calibration.handle = handle;
    else if (result == ESP_ERR_NOT_SUPPORTED) calibration.unsupported = true;
    return calibration.handle;   // transient allocation failure may succeed on a later sample
}
} // namespace

bool adcRead(uint8_t gpio, uint16_t& raw) {
    AdcPin pin;
    if (!resolvePin(gpio, pin)) return false;
    LockGuard guard{pin.device->lock};
    return guard && readRawLocked(pin, raw);
}

uint16_t adcMaxCount() { return 4095; }

bool adcReadMv(uint8_t gpio, uint16_t& millivolts) {
    AdcPin pin;
    if (!resolvePin(gpio, pin)) return false;
    LockGuard guard{pin.device->lock};
    if (!guard) return false;
    uint16_t raw = 0;
    if (!readRawLocked(pin, raw)) return false;
    const adc_cali_handle_t calibration = calibrationLocked(pin);
    if (!calibration) return false;
    int result = 0;
    if (adc_cali_raw_to_voltage(calibration, raw, &result) != ESP_OK || result < 0 || result > UINT16_MAX) return false;
    millivolts = static_cast<uint16_t>(result);
    return true;
}

void setTestAdcValue(uint8_t, uint16_t) {}
void setTestAdcMv(uint8_t, uint16_t) {}
void clearTestAdcValue() {}
} // namespace mm::platform
