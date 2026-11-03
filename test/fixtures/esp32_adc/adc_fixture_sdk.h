#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

// The SDK seam records calls made by the production ADC implementation.
using esp_err_t = int;
inline constexpr esp_err_t ESP_OK = 0;
inline constexpr esp_err_t ESP_FAIL = -1;
inline constexpr esp_err_t ESP_ERR_NO_MEM = 0x101;
inline constexpr esp_err_t ESP_ERR_INVALID_ARG = 0x102;
inline constexpr esp_err_t ESP_ERR_NOT_SUPPORTED = 0x106;
inline constexpr esp_err_t ESP_ERR_TIMEOUT = 0x107;

enum adc_unit_t : int { ADC_UNIT_1, ADC_UNIT_2, ADC_UNIT_3 };
enum adc_channel_t : int {
    ADC_CHANNEL_0, ADC_CHANNEL_1, ADC_CHANNEL_2, ADC_CHANNEL_3, ADC_CHANNEL_4,
    ADC_CHANNEL_5, ADC_CHANNEL_6, ADC_CHANNEL_7, ADC_CHANNEL_8, ADC_CHANNEL_9
};
enum adc_atten_t { ADC_ATTEN_DB_12 = 3 };
enum adc_bitwidth_t { ADC_BITWIDTH_DEFAULT = 0 };

struct adc_oneshot_unit_init_cfg_t {
    adc_unit_t unit_id;
    int clk_src;
    int ulp_mode;
};
struct adc_oneshot_chan_cfg_t {
    adc_atten_t atten;
    adc_bitwidth_t bitwidth;
};
struct adc_cali_curve_fitting_config_t {
    adc_unit_t unit_id;
    adc_channel_t chan;
    adc_atten_t atten;
    adc_bitwidth_t bitwidth;
};
struct adc_cali_line_fitting_config_t {
    adc_unit_t unit_id;
    adc_atten_t atten;
    adc_bitwidth_t bitwidth;
    uint32_t default_vref;
};

namespace adc_fixture {
struct UnitHandle { adc_unit_t unit; };
struct CalibrationHandle {
    adc_unit_t unit = ADC_UNIT_1;
    adc_channel_t channel = ADC_CHANNEL_0;
};
struct ChannelCall {
    adc_unit_t unit;
    adc_channel_t channel;
    adc_oneshot_chan_cfg_t config;
};
struct ReadCall { adc_unit_t unit; adc_channel_t channel; };
struct ConversionCall { CalibrationHandle* handle; int raw; };

struct State {
    std::array<UnitHandle, 2> units{{{ADC_UNIT_1}, {ADC_UNIT_2}}};
    std::array<std::array<CalibrationHandle, 10>, 2> calibrations{};
    std::vector<int> mappings;
    std::vector<adc_oneshot_unit_init_cfg_t> opened;
    std::vector<ChannelCall> configured;
    std::vector<ReadCall> reads;
    std::vector<adc_cali_curve_fitting_config_t> curves;
    std::vector<adc_cali_line_fitting_config_t> lines;
    std::vector<ConversionCall> conversions;
    esp_err_t openResult = ESP_OK;
    esp_err_t configureResult = ESP_OK;
    esp_err_t readResult = ESP_OK;
    esp_err_t curveResult = ESP_OK;
    esp_err_t lineResult = ESP_OK;
    esp_err_t conversionResult = ESP_OK;
    int raw = 1234;
    std::array<int, 2> millivolts{{1100, 2200}};
    std::function<void()> duringRead;
};
inline State state{};

inline bool validUnit(adc_unit_t unit) { return unit == ADC_UNIT_1 || unit == ADC_UNIT_2; }
inline bool validChannel(adc_channel_t channel) {
    return static_cast<int>(channel) >= 0 && static_cast<int>(channel) <= 9;
}
} // namespace adc_fixture

using adc_oneshot_unit_handle_t = adc_fixture::UnitHandle*;
using adc_cali_handle_t = adc_fixture::CalibrationHandle*;

inline esp_err_t adc_oneshot_io_to_channel(int gpio, adc_unit_t* unit, adc_channel_t* channel) {
    adc_fixture::state.mappings.push_back(gpio);
    switch (gpio) {
        case 1: *unit = ADC_UNIT_1; *channel = ADC_CHANNEL_0; break;
        case 2: *unit = ADC_UNIT_1; *channel = ADC_CHANNEL_1; break;
        case 15: *unit = ADC_UNIT_2; *channel = ADC_CHANNEL_4; break;
        case 16: *unit = ADC_UNIT_2; *channel = ADC_CHANNEL_5; break;
        case 25: *unit = ADC_UNIT_2; *channel = ADC_CHANNEL_8; break;
        case 32: *unit = ADC_UNIT_1; *channel = ADC_CHANNEL_4; break;
        case 248: *unit = ADC_UNIT_1; *channel = static_cast<adc_channel_t>(10); break;
        case 249: *unit = ADC_UNIT_1; *channel = static_cast<adc_channel_t>(-1); break;
        case 250: *unit = ADC_UNIT_3; *channel = ADC_CHANNEL_0; break;
        default: return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

inline esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t* config,
                                    adc_oneshot_unit_handle_t* handle) {
    auto& state = adc_fixture::state;
    state.opened.push_back(*config);
    if (!adc_fixture::validUnit(config->unit_id)) return ESP_ERR_INVALID_ARG;
    if (state.openResult != ESP_OK) return state.openResult;
    *handle = &state.units[config->unit_id];
    return ESP_OK;
}

inline esp_err_t adc_oneshot_config_channel(adc_oneshot_unit_handle_t handle, adc_channel_t channel,
                                          const adc_oneshot_chan_cfg_t* config) {
    if (!handle || !adc_fixture::validChannel(channel)) return ESP_ERR_INVALID_ARG;
    adc_fixture::state.configured.push_back({handle->unit, channel, *config});
    return adc_fixture::state.configureResult;
}

inline esp_err_t adc_oneshot_read(adc_oneshot_unit_handle_t handle, adc_channel_t channel, int* raw) {
    if (!handle || !adc_fixture::validChannel(channel)) return ESP_ERR_INVALID_ARG;
    auto& state = adc_fixture::state;
    state.reads.push_back({handle->unit, channel});
    auto callback = std::move(state.duringRead);
    state.duringRead = {};
    if (callback) callback();
    *raw = state.raw;
    return state.readResult;
}

inline esp_err_t adc_cali_create_scheme_curve_fitting(const adc_cali_curve_fitting_config_t* config,
                                                     adc_cali_handle_t* handle) {
    auto& state = adc_fixture::state;
    state.curves.push_back(*config);
    if (!adc_fixture::validUnit(config->unit_id) || !adc_fixture::validChannel(config->chan)) return ESP_ERR_INVALID_ARG;
    if (state.curveResult != ESP_OK) return state.curveResult;
    auto& calibration = state.calibrations[config->unit_id][config->chan];
    calibration = {config->unit_id, config->chan};
    *handle = &calibration;
    return ESP_OK;
}

inline esp_err_t adc_cali_create_scheme_line_fitting(const adc_cali_line_fitting_config_t* config,
                                                    adc_cali_handle_t* handle) {
    auto& state = adc_fixture::state;
    state.lines.push_back(*config);
    if (!adc_fixture::validUnit(config->unit_id)) return ESP_ERR_INVALID_ARG;
    if (state.lineResult != ESP_OK) return state.lineResult;
    auto& calibration = state.calibrations[config->unit_id][0];
    calibration = {config->unit_id, ADC_CHANNEL_0};
    *handle = &calibration;
    return ESP_OK;
}

inline esp_err_t adc_cali_raw_to_voltage(adc_cali_handle_t handle, int raw, int* millivolts) {
    if (!handle) return ESP_ERR_INVALID_ARG;
    auto& state = adc_fixture::state;
    state.conversions.push_back({handle, raw});
    *millivolts = state.millivolts[handle->unit];
    return state.conversionResult;
}
