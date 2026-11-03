#pragma once

#include "core/module/MoonModule.h"
#include "core/util/ActiveInstance.h"
#include "platform/platform.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace mm {

/// Continuous BH1750 illuminance, sampled on a worker so I²C never blocks rendering.
/// Prior art: ROHM BH1750FVI Technical Note Rev.D, H-resolution mode and raw / 1.2 lux.
class Bh1750Service : public MoonModule {
public:
    ModuleRole role() const MM_NONBLOCKING override { return ModuleRole::Service; }

    /// I²C controller connected to the illuminance sensor.
    uint8_t bus = 1;
    /// GPIO carrying the sensor's I²C data signal.
    int8_t sda = 26;
    /// GPIO carrying the sensor's I²C clock signal.
    int8_t scl = 11;
    /// I²C address selected by the sensor's address pin.
    uint8_t address = 0x23;
    /// Use the sensor's target brightness while a valid reading is available.
    bool automatic = true;
    /// Illuminance paired with the dark-end brightness of the response curve.
    uint16_t darkLux = 0;
    /// Brightness paired with the dark-end illuminance.
    uint8_t darkBrightness = 16;
    /// Illuminance paired with the bright-end brightness of the response curve.
    uint16_t brightLux = 500;
    /// Brightness paired with the bright-end illuminance.
    uint8_t brightBrightness = 255;
    /// Illuminance change needed to update the accepted reading and target brightness.
    uint16_t hysteresisLux = 15;

    ~Bh1750Service() override { stop(); }

    void defineControls() override {
        controls_.addControl("automatic", automatic);
        controls_.addReadOnly("lux", luxText_, sizeof(luxText_));
        controls_.addReadOnly("target", targetText_, sizeof(targetText_));
        controls_.addControl("darkLux", darkLux, 0, 65535);
        controls_.addControl("darkBrightness", darkBrightness, 0, 255);
        controls_.addControl("brightLux", brightLux, 0, 65535);
        controls_.addControl("brightBrightness", brightBrightness, 0, 255);
        controls_.addControl("hysteresisLux", hysteresisLux, 0, 65535);
        controls_.addControl("bus", bus, 0, 1);
        controls_.setAdvanced(controls_.count() - 1);
        controls_.addPin("sda", sda);
        controls_.setAdvanced(controls_.count() - 1);
        controls_.addPin("scl", scl);
        controls_.setAdvanced(controls_.count() - 1);
        controls_.addControl("address", address, 0x08, 0x77);
        controls_.setAdvanced(controls_.count() - 1);
        MoonModule::defineControls();
    }

    void prepare() override {
        stop();
        seat_.claim();
        valid_ = false;
        opened_ = false;
        failures_ = 0;
        std::snprintf(luxText_, sizeof(luxText_), "--");
        std::snprintf(targetText_, sizeof(targetText_), "manual");
        notifyTargetChanged();
        if (sda < 0 || scl < 0) {
            setStatus("set sda + scl pins", Severity::Warning);
            return;
        }
        ioBus_ = bus;
        ioSda_ = static_cast<uint16_t>(sda);
        ioScl_ = static_cast<uint16_t>(scl);
        ioAddress_ = address;
        stopped_.store(false, std::memory_order_release);
        if (!platform::spawnPinnedTask(worker_, "bh1750", &workerEntry, this, 4096, 2, -1)) {
            setStatus("BH1750 worker unavailable", Severity::Warning);
            return;
        }
        request(Io::Open);
    }

    void release() override {
        stop();
        valid_ = false;
        seat_.vacate();
        notifyTargetChanged();
        MoonModule::release();
    }

    void tick1s() MM_NONBLOCKING override {
        seat_.claim();
        const Io done = completed_.exchange(Io::Idle, std::memory_order_acq_rel);
        const uint32_t now = platform::millis();
        if (done != Io::Idle) {
            busy_ = false;
            if (done == Io::Opened) {
                opened_ = true;
                failures_ = 0;
                nextActionMs_ = now + 180;  // first H-resolution conversion can take 180 ms
                setStatus("waiting for first measurement");
            } else if (done == Io::Sampled) {
                failures_ = 0;
                accept(rawToLux(raw_.load(std::memory_order_relaxed)));
            } else {
                if (failures_ < UINT8_MAX) ++failures_;
                if (done == Io::OpenFailed || failures_ >= 3) {
                    opened_ = false;
                    valid_ = false;
                    notifyTargetChanged();
                    nextActionMs_ = now + 5000;
                }
                setStatus("BH1750 unavailable", Severity::Warning);
            }
        }
        if (worker_.impl && !busy_ && static_cast<int32_t>(now - nextActionMs_) >= 0)
            request(opened_ ? Io::Read : Io::Open);
        MoonModule::tick1s();
    }

    void onControlChanged(const char* name) override {
        if (std::strcmp(name, "automatic") == 0 || std::strcmp(name, "darkLux") == 0 ||
            std::strcmp(name, "darkBrightness") == 0 || std::strcmp(name, "brightLux") == 0 ||
            std::strcmp(name, "brightBrightness") == 0 || std::strcmp(name, "enabled") == 0) {
            if (valid_) target_ = brightnessForLux(acceptedLux_, darkLux, brightLux,
                                                   darkBrightness, brightBrightness);
            updateText();
            notifyTargetChanged();
        }
        MoonModule::onControlChanged(name);
    }

    void onEnabled(bool) override { notifyTargetChanged(); }

    bool affectsPrepare(const char* name) const override {
        return std::strcmp(name, "bus") == 0 || std::strcmp(name, "sda") == 0 ||
               std::strcmp(name, "scl") == 0 || std::strcmp(name, "address") == 0;
    }

    static bool automaticBrightness(uint8_t& out) {
        auto* sensor = ActiveInstance<Bh1750Service>::active();
        if (!sensor || !sensor->effectivelyEnabled() || !sensor->automatic || !sensor->valid_) return false;
        out = sensor->target_;
        return true;
    }

    using TargetChangedFn = void(*)();
    static void setTargetChangedHook(TargetChangedFn hook) { targetChanged_ = hook; }

    static uint16_t rawToLux(uint16_t raw) {
        return static_cast<uint16_t>((static_cast<uint32_t>(raw) * 5 + 3) / 6);
    }

    static uint8_t brightnessForLux(uint16_t lux, uint16_t lowLux, uint16_t highLux,
                                    uint8_t lowBrightness, uint8_t highBrightness) {
        if (lowLux > highLux) {
            const uint16_t swapLux = lowLux; lowLux = highLux; highLux = swapLux;
            const uint8_t swapBrightness = lowBrightness;
            lowBrightness = highBrightness; highBrightness = swapBrightness;
        }
        if (lux <= lowLux) return lowBrightness;
        if (lux >= highLux) return highBrightness;
        const int32_t span = highLux - lowLux;
        const int32_t change = static_cast<int32_t>(highBrightness) - lowBrightness;
        const int32_t numerator = change * (lux - lowLux);
        return static_cast<uint8_t>(static_cast<int32_t>(lowBrightness) +
                                    (numerator + (numerator >= 0 ? span / 2 : -span / 2)) / span);
    }

    void injectLuxForTest(uint16_t lux) { seat_.claim(); accept(lux); }
    uint16_t acceptedLux() const { return acceptedLux_; }
    bool hasReading() const { return valid_; }

private:
    enum class Io : uint8_t { Idle, Open, Read, Opened, Sampled, OpenFailed, ReadFailed };

    static void workerEntry(void* user) { static_cast<Bh1750Service*>(user)->work(); }

    void work() {
        while (!stopped_.load(std::memory_order_acquire)) {
            if (!platform::waitNotify(worker_, 1000)) continue;
            if (stopped_.load(std::memory_order_acquire)) break;
            const Io action = requested_.exchange(Io::Idle, std::memory_order_acq_rel);
            if (action == Io::Open) {
                platform::i2cDeviceDeinit(device_);
                const uint8_t power = 0x01;
                const uint8_t continuousHighResolution = 0x10;
                const bool okay = platform::i2cDeviceInit(device_, ioBus_, ioSda_, ioScl_,
                                                           ioAddress_, 100000) &&
                    platform::i2cDeviceWrite(device_, &power, 1, 20) &&
                    platform::i2cDeviceWrite(device_, &continuousHighResolution, 1, 20);
                if (!okay) platform::i2cDeviceDeinit(device_);
                completed_.store(okay ? Io::Opened : Io::OpenFailed, std::memory_order_release);
            } else if (action == Io::Read) {
                uint8_t bytes[2] = {};
                const bool okay = platform::i2cDeviceRead(device_, bytes, sizeof(bytes), 20);
                if (okay) raw_.store(static_cast<uint16_t>((bytes[0] << 8) | bytes[1]),
                                     std::memory_order_relaxed);
                completed_.store(okay ? Io::Sampled : Io::ReadFailed, std::memory_order_release);
            }
        }
        platform::i2cDeviceDeinit(device_);
    }

    void request(Io action) {
        busy_ = true;
        requested_.store(action, std::memory_order_release);
        platform::notifyTask(worker_);
    }

    void stop() {
        stopped_.store(true, std::memory_order_release);
        platform::stopPinnedTask(worker_);
        requested_.store(Io::Idle, std::memory_order_release);
        completed_.store(Io::Idle, std::memory_order_release);
        busy_ = false;
    }

    void accept(uint16_t lux) {
        const uint16_t distance = lux > acceptedLux_ ? lux - acceptedLux_ : acceptedLux_ - lux;
        if (!valid_ || distance >= hysteresisLux) {
            acceptedLux_ = lux;
            target_ = brightnessForLux(lux, darkLux, brightLux,
                                       darkBrightness, brightBrightness);
        }
        const bool first = !valid_;
        valid_ = true;
        measuredLux_ = lux;
        updateText();
        if (first || distance >= hysteresisLux) notifyTargetChanged();
    }

    void updateText() {
        if (!valid_) return;
        std::snprintf(luxText_, sizeof(luxText_), "%u lx", static_cast<unsigned>(measuredLux_));
        if (automatic) {
            std::snprintf(targetText_, sizeof(targetText_), "%u", static_cast<unsigned>(target_));
            std::snprintf(statusText_, sizeof(statusText_), "%u lx -> brightness %u",
                          static_cast<unsigned>(measuredLux_), static_cast<unsigned>(target_));
        } else {
            std::snprintf(targetText_, sizeof(targetText_), "manual");
            std::snprintf(statusText_, sizeof(statusText_), "%u lx, manual brightness",
                          static_cast<unsigned>(measuredLux_));
        }
        setStatus(statusText_);
    }

    static void notifyTargetChanged() { if (targetChanged_) targetChanged_(); }

    platform::I2cDeviceHandle device_{};
    platform::WorkerTask worker_{};
    uint8_t ioBus_ = 0;
    uint8_t ioAddress_ = 0;
    uint16_t ioSda_ = 0;
    uint16_t ioScl_ = 0;
    std::atomic<bool> stopped_{false};
    std::atomic<Io> requested_{Io::Idle};
    std::atomic<Io> completed_{Io::Idle};
    std::atomic<uint16_t> raw_{0};
    ActiveInstance<Bh1750Service> seat_{*this};
    bool busy_ = false;
    bool opened_ = false;
    bool valid_ = false;
    uint8_t failures_ = 0;
    uint8_t target_ = 16;
    uint16_t measuredLux_ = 0;
    uint16_t acceptedLux_ = 0;
    uint32_t nextActionMs_ = 0;
    /// Latest measured illuminance; the initial placeholder indicates an unknown reading.
    char luxText_[16] = "--";
    /// Last calculated brightness target, or the manual-mode label.
    char targetText_[16] = "manual";
    char statusText_[48] = {};
    static inline TargetChangedFn targetChanged_ = nullptr;
};

} // namespace mm
