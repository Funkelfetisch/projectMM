#pragma once

#include "core/module/MoonModule.h"
#include "core/module/Scheduler.h"
#include "core/util/BatteryReading.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace mm {

/// One explicitly configured battery channel, sampled by its BatteryService parent.
/// @guide Unset pins and divider values leave the reading unknown. Presence is declared independently of voltage. Charge percentage is an optional linear voltage estimate with explicit endpoints, not a capacity measurement.
class BatteryChannel : public MoonModule {
public:
    ModuleRole role() const MM_NONBLOCKING override { return ModuleRole::Service; }

    /// Stable physical battery identity, first or second, independent of the module's display name.
    uint8_t slot = 1;
    /// ADC input for the board's battery divider; configure from verified wiring.
    int8_t adcPin = -1;
    /// Optional divider switch, unset for a permanently connected divider.
    int8_t senseGate = -1;
    /// The switch's active level; the board profile owns its polarity.
    bool gateHigh = true;
    /// Divider settling delay; gated inputs require a qualified nonzero delay.
    uint16_t settleMs = 0;
    /// Numerator of the explicit battery-voltage to ADC-voltage ratio.
    uint16_t gainNumerator = 0;
    /// Denominator of that ratio; zero leaves sampling disabled.
    uint16_t gainDenominator = 0;
    /// Calibration offset in millivolts, applied after divider scaling.
    int16_t offsetMv = 0;
    /// Declared installation: unknown, present or absent; independent of measured voltage.
    uint8_t presence = 0;
    /// Explicit opt-in to the configured linear voltage estimate.
    bool estimate = false;
    /// Measured/modeled voltage at the discharged endpoint; zero leaves percentage unknown.
    uint16_t emptyMv = 0;
    /// Measured/modeled voltage at the charged endpoint; it must exceed emptyMv.
    uint16_t fullMv = 0;
    /// How long a last successful sample remains current after read failures.
    uint16_t staleSeconds = 15;

    void defineControls() override {
        static constexpr const char* kPresence[] = {"unknown", "present", "absent"};
        controls_.addControl("slot", slot, 1, 2);
        controls_.addReadOnly("sample", sampleText_, sizeof(sampleText_));
        controls_.addSelect("presence", presence, kPresence, 3);
        controls_.addPin("adcPin", adcPin);
        controls_.addControl("gainNumerator", gainNumerator);
        controls_.addControl("gainDenominator", gainDenominator);
        controls_.addControl("offsetMv", offsetMv);
        controls_.addControl("estimate", estimate);
        controls_.addControl("emptyMv", emptyMv);
        controls_.addControl("fullMv", fullMv);
        controls_.addControl("staleSeconds", staleSeconds, 1, 3600);
        controls_.addPin("senseGate", senseGate);
        controls_.addControl("gateHigh", gateHigh);
        controls_.addControl("settleMs", settleMs, 0, 2000);
        MoonModule::defineControls();
    }

    void prepare() override { configureReading(); updateText(platform::millis()); }
    void release() override {
        reading_.configure({}, BatteryReading::Presence::Unknown);
        std::snprintf(sampleText_, sizeof(sampleText_), "%s", "null");
        MoonModule::release();
    }
    bool affectsPrepare(const char*) const override { return true; }
    const void* pinReservationOwner() const override {
        auto* owner = parent();
        return owner && std::strcmp(owner->typeName(), "BatteryService") == 0 ? owner : nullptr;
    }
    bool prepareControlWrite(const char* name) override {
        if (std::strcmp(name, "sample") == 0) return true;
        MoonModule* owner = parent();
        if (!owner || std::strcmp(owner->typeName(), "BatteryService") != 0) return true;
        owner->quiesce();
        if (owner->blocksAncestorRemoval()) return false;
        // A document may write the existing value, in which case its normal
        // change reaction does not run. Restart the parked sampler either way.
        if (auto* scheduler = Scheduler::instance()) scheduler->requestPrepare(owner);
        return true;
    }
    /// Stop the parent's in-flight sampling before this channel is disabled or changed.
    void quiesce() override {
        MoonModule* owner = parent();
        if (owner && std::strcmp(owner->typeName(), "BatteryService") == 0) owner->quiesce();
    }
    void onControlChanged(const char* name) override {
        quiesce();
        configureReading(); updateText(platform::millis());
        MoonModule::onControlChanged(name);
    }

    /// Idempotent at parent and child prepare, so unrelated tree rebuilds preserve sample age.
    void configureReading() {
        const Source source{slot, adcPin, senseGate, gateHigh, settleMs};
        if (!(source_ == source)) reading_.invalidate();
        source_ = source;
        reading_.configure({gainNumerator, gainDenominator, offsetMv, emptyMv, fullMv,
                            static_cast<uint32_t>(staleSeconds) * 1000, estimate},
                           presence == 1 ? BatteryReading::Presence::Present :
                           presence == 2 ? BatteryReading::Presence::Absent : BatteryReading::Presence::Unknown);
    }
    /// Whether this declared channel has enough hardware information for sampling; absence requires no ADC access.
    bool canSample() const {
        return presence != 2 && adcPin >= 0 && gainNumerator && gainDenominator && staleSeconds &&
               (senseGate < 0 || settleMs > 0);
    }
    /// Accept a successful worker result without treating a failed read as zero.
    void acceptAdc(uint16_t millivolts, uint32_t sampledAt) { reading_.acceptAdc(millivolts, sampledAt); }
    /// A sampler that cannot claim its pins must not expose a prior reading as current.
    void invalidateReading(uint32_t now) { reading_.invalidate(); updateText(now); }
    /// The transport-neutral current measurement, including validity and age.
    const BatteryReading& reading() const { return reading_; }
    /// Pack identity and sample have one JSON shape on HTTP and BLE.
    void writeJson(JsonSink& sink, uint32_t now, bool webRevision = false) const {
        sink.appendf("{\"id\":%u,\"sample\":", static_cast<unsigned>(slot));
        reading_.writeJson(sink, now);
        sink.append(",\"charging\":\"unknown\"");
        if (webRevision) sink.appendf(",\"revision\":%u", static_cast<unsigned>(webRevision_));
        sink.append("}");
    }
    /// Refresh the read-only HTTP/WS telemetry from the same model the BLE path reads.
    void updateText(uint32_t now) {
        ++webRevision_;
        JsonSink sink(sampleText_, sizeof(sampleText_));
        writeJson(sink, now, true);
        if (sink.overflowed()) {
            std::snprintf(sampleText_, sizeof(sampleText_), "%s", "null");
            setStatus("battery telemetry overflow", Severity::Error);
        } else if (presence == 2) setStatus("battery declared absent");
        else if (!canSample()) setStatus("configure ADC, divider and gate settling", Severity::Warning);
        else if (!reading_.fresh(now)) setStatus("battery measurement unavailable or stale", Severity::Warning);
        else if (presence == 0) setStatus("voltage measured; battery presence unknown", Severity::Warning);
        else {
            uint8_t level = 0;
            setStatus(reading_.percent(now, level) ? "voltage estimate" : "voltage only");
        }
    }

private:
    struct Source {
        uint8_t slot = 1;
        int8_t adcPin = -1;
        int8_t senseGate = -1;
        bool gateHigh = true;
        uint16_t settleMs = 0;
        bool operator==(const Source&) const = default;
    };
    Source source_{};
    BatteryReading reading_{};
    uint32_t webRevision_ = 0;
    /// Pack identity, validity, age and optional voltage estimate as JSON.
    char sampleText_[192] = "null";
};

} // namespace mm
