#pragma once

#include "core/util/JsonSink.h"

#include <cstdint>

namespace mm {

/// One battery's declared presence and timestamped measurement, shared by web and BLE.
/// @technical Voltage-derived charge is an explicit estimate, with configured endpoints; an ADC failure supplies no empty-battery value.
class BatteryReading {
public:
    /// Presence comes from the board's qualified detector or declared installation, independently of its ADC voltage.
    enum class Presence : uint8_t { Unknown, Present, Absent };

    /// Divider conversion uses integer millivolts; no historical gain, offset or charge curve is implied.
    struct Config {
        /// Numerator of the divider gain applied to the ADC millivolts; zero rejects every sample.
        uint32_t gainNumerator = 0;
        /// Denominator of the divider gain; zero rejects every sample.
        uint32_t gainDenominator = 0;
        /// Millivolts added after the gain.
        int32_t offsetMv = 0;
        /// Pack voltage the estimate reads as 0 percent.
        uint16_t emptyMv = 0;
        /// Pack voltage the estimate reads as 100 percent.
        uint16_t fullMv = 0;
        /// How long a sample counts as fresh, in milliseconds.
        uint32_t staleAfterMs = 15000;
        /// Whether the voltage is also reported as a percentage.
        bool estimateEnabled = false;
        bool operator==(const Config&) const = default;
    };

    /// Reconfiguration invalidates old measurements; an unchanged prepare preserves the sample and its age.
    void configure(const Config& config, Presence presence) {
        if (config_ == config && presence_ == presence) return;
        config_ = config;
        presence_ = presence;
        invalidate();
    }

    /// A changed input source cannot inherit another divider's last sample.
    void invalidate() {
        hasSample_ = false;
        millivolts_ = 0;
        observedAtMs_ = 0;
        elapsedMs_ = 0;
    }

    /// Convert a successful calibrated ADC read; false leaves the last valid sample untouched.
    bool acceptAdc(uint16_t adcMv, uint32_t now) {
        if (!adcMv || presence_ == Presence::Absent || !config_.gainNumerator ||
            !config_.gainDenominator || !config_.staleAfterMs) return false;
        const int64_t scaled = (static_cast<int64_t>(adcMv) * config_.gainNumerator +
                                config_.gainDenominator / 2) / config_.gainDenominator + config_.offsetMv;
        if (scaled <= 0 || scaled > UINT16_MAX) return false;
        millivolts_ = static_cast<uint16_t>(scaled);
        observedAtMs_ = now;
        elapsedMs_ = 0;
        hasSample_ = true;
        return true;
    }

    /// The declared presence, never inferred from zero volts or USB power.
    Presence presence() const { return presence_; }
    /// Whether the last successful sample is still within its configured lifetime; subtraction handles uptime wrap.
    bool fresh(uint32_t now) const {
        uint32_t elapsed = 0;
        return age(now, elapsed) && elapsed <= config_.staleAfterMs;
    }
    /// The last valid voltage, including a stale one whose age the consumer must display.
    bool lastVoltage(uint16_t& out) const {
        if (!hasSample_) return false;
        out = millivolts_;
        return true;
    }
    /// Observed age on the consuming task, saturating rather than making an old sample fresh after a full uptime wrap.
    bool age(uint32_t now, uint32_t& out) const {
        if (!hasSample_) return false;
        elapsedMs_ += static_cast<uint32_t>(now - observedAtMs_);
        observedAtMs_ = now;
        out = elapsedMs_ > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(elapsedMs_);
        return true;
    }
    /// An explicitly enabled linear voltage estimate, bounded to 0..100; unknown, absent or stale samples have no percentage.
    bool percent(uint32_t now, uint8_t& out) const {
        if (presence_ != Presence::Present || !fresh(now) || !config_.estimateEnabled ||
            !config_.emptyMv || config_.fullMv <= config_.emptyMv) return false;
        if (millivolts_ <= config_.emptyMv) out = 0;
        else if (millivolts_ >= config_.fullMv) out = 100;
        else {
            const uint32_t span = config_.fullMv - config_.emptyMv;
            out = static_cast<uint8_t>(((millivolts_ - config_.emptyMv) * 100u + span / 2) / span);
        }
        return true;
    }

    /// The same validity and units on every transport; null never masquerades as a discharged battery.
    void writeJson(JsonSink& sink, uint32_t now) const {
        sink.append("{\"presence\":");
        sink.writeJsonString(presence_ == Presence::Present ? "present" :
                             presence_ == Presence::Absent ? "absent" : "unknown");
        sink.append(",\"state\":");
        sink.writeJsonString(!hasSample_ ? "unknown" : fresh(now) ? "fresh" : "stale");
        sink.append(",\"voltageMv\":");
        if (hasSample_) sink.appendf("%u", static_cast<unsigned>(millivolts_));
        else sink.append("null");
        sink.append(",\"percent\":");
        uint8_t level = 0;
        const bool hasPercent = percent(now, level);
        if (hasPercent) sink.appendf("%u", static_cast<unsigned>(level));
        else sink.append("null");
        sink.append(",\"ageMs\":");
        uint32_t elapsed = 0;
        if (age(now, elapsed)) sink.appendf("%u", static_cast<unsigned>(elapsed));
        else sink.append("null");
        sink.append(",\"estimate\":");
        sink.append(hasPercent ? "\"voltage\"}" : "null}");
    }

private:
    Config config_{};
    Presence presence_ = Presence::Unknown;
    bool hasSample_ = false;
    uint16_t millivolts_ = 0;
    mutable uint32_t observedAtMs_ = 0;
    mutable uint64_t elapsedMs_ = 0;
};

} // namespace mm
