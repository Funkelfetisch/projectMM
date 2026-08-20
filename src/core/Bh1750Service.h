#pragma once

#include "core/ActiveInstance.h"
#include "core/MoonModule.h"
#include "platform/platform.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace mm {

/// BH1750FVI ambient-light input and automatic-brightness source. The service owns one
/// persistent I2C device, runs continuous high-resolution conversion, and publishes a
/// transient 0..255 brightness target. Drivers pulls that target; the user's persisted
/// manual brightness remains untouched and is the immediate fallback when this service is
/// disabled, unavailable, or set to manual.
///
/// The transfer function follows the BH1750FVI datasheet: continuous H-resolution command
/// 0x10, maximum first-conversion time 180 ms, and illuminance = raw / 1.2. Two editable
/// lux/brightness anchors define a linear response. `hysteresisLux` is a Schmitt-style
/// deadband around the last accepted reading, so sensor noise cannot continuously rebuild
/// the output correction.
///
/// **Prior art:** ROHM BH1750FVI Technical Note Rev.D, instruction set and measurement
/// sequence (https://www.mouser.com/datasheet/2/348/bh1750fvi-e-186247.pdf).
class Bh1750Service : public MoonModule {
public:
    ModuleRole role() const MM_NONBLOCKING override { return ModuleRole::Service; }

    // HELIO 1 wiring defaults. Every value remains a normal control, so another board can
    // add the same service and change the bus, pins, or alternate BH1750 address live.
    uint8_t bus = 1;
    int8_t sda = 26;
    int8_t scl = 11;
    uint8_t address = 0x23;

    bool automatic = true;
    uint16_t darkLux = 0;
    uint8_t darkBrightness = 16;
    uint16_t brightLux = 500;
    uint8_t brightBrightness = 255;
    uint16_t hysteresisLux = 15;

    ~Bh1750Service() override { withdraw(); }

    void defineControls() override {
        controls_.addBool("automatic", automatic);
        controls_.addReadOnly("lux", luxStr_, sizeof(luxStr_));
        controls_.addReadOnly("target", targetStr_, sizeof(targetStr_));

        controls_.addUint16("darkLux", darkLux);
        controls_.setNumberField(controls_.count() - 1);
        controls_.addUint8("darkBrightness", darkBrightness);
        controls_.addUint16("brightLux", brightLux);
        controls_.setNumberField(controls_.count() - 1);
        controls_.addUint8("brightBrightness", brightBrightness);
        controls_.addUint16("hysteresisLux", hysteresisLux);
        controls_.setNumberField(controls_.count() - 1);

        controls_.addUint8("bus", bus, 0, 1);
        controls_.setNumberField(controls_.count() - 1);
        controls_.setAdvanced(controls_.count() - 1);
        controls_.addPin("sda", sda);
        controls_.setAdvanced(controls_.count() - 1);
        controls_.addPin("scl", scl);
        controls_.setAdvanced(controls_.count() - 1);
        controls_.addUint8("address", address, 0x08, 0x77);
        controls_.setNumberField(controls_.count() - 1);
        controls_.setAdvanced(controls_.count() - 1);
        MoonModule::defineControls();
    }

    void prepare() override {
        stopIoWorker();
        invalidate();
        seat_.claim();
        syncPublication();
        nextRetryMs_ = 0;
        if (sda < 0 || scl < 0) {
            setStatus("set sda + scl pins", Severity::Warning);
            nextRetryMs_ = platform::millis() + kRetryMs;
            return;
        }
        startIoWorker(platform::millis());
    }

    void release() override {
        withdraw();
        MoonModule::release();
    }

    void tick1s() MM_NONBLOCKING override {
        const bool wasSeated = seat_.seated();
        seat_.claim();
        if (!wasSeated) syncPublication();

        const uint32_t now = platform::millis();
        consumeIoResult(now);

        if (!ioTask_.impl) {
            if (static_cast<int32_t>(now - nextRetryMs_) >= 0) startIoWorker(now);
            return;
        }
        if (ioBusy_.load(std::memory_order_acquire)) return;
        if (!inited_) {
            if (static_cast<int32_t>(now - nextRetryMs_) >= 0) queueIo(IoRequest::Open);
            return;
        }
        if (static_cast<int32_t>(now - readReadyAtMs_) < 0) return;
        queueIo(IoRequest::Read);
    }

    void onControlChanged(const char* name) override {
        if (std::strcmp(name, "automatic") == 0 ||
            std::strcmp(name, "darkLux") == 0 ||
            std::strcmp(name, "darkBrightness") == 0 ||
            std::strcmp(name, "brightLux") == 0 ||
            std::strcmp(name, "brightBrightness") == 0 ||
            std::strcmp(name, "hysteresisLux") == 0) {
            if (valid_) targetBrightness_ = brightnessForLux(
                acceptedLux_, darkLux, brightLux, darkBrightness, brightBrightness);
            updateReadouts();
            syncPublication();
        }
    }

    bool affectsPrepare(const char* name) const override {
        return std::strcmp(name, "bus") == 0 || std::strcmp(name, "sda") == 0 ||
               std::strcmp(name, "scl") == 0 || std::strcmp(name, "address") == 0;
    }

    /// The active service's transient output. False means Drivers uses its persisted manual
    /// brightness. The output value remains valid until the next revision.
    static bool automaticBrightness(uint8_t& output) {
        Bh1750Service* active = ActiveInstance<Bh1750Service>::active();
        if (!active || !active->automatic || !active->valid_) return false;
        output = active->targetBrightness_;
        return true;
    }

    static uint32_t brightnessRevision() { return brightnessRevision_; }

    static uint16_t rawToLux(uint16_t raw) {
        // raw / 1.2 = raw * 5 / 6. Add half the denominator for integer rounding.
        return static_cast<uint16_t>((static_cast<uint32_t>(raw) * 5u + 3u) / 6u);
    }

    static uint8_t brightnessForLux(uint16_t lux, uint16_t firstLux, uint16_t secondLux,
                                    uint8_t firstBrightness, uint8_t secondBrightness) {
        if (firstLux > secondLux) {
            const uint16_t luxSwap = firstLux;
            firstLux = secondLux;
            secondLux = luxSwap;
            const uint8_t brightnessSwap = firstBrightness;
            firstBrightness = secondBrightness;
            secondBrightness = brightnessSwap;
        }
        if (firstLux == secondLux)
            return lux < firstLux ? firstBrightness : secondBrightness;
        if (lux <= firstLux) return firstBrightness;
        if (lux >= secondLux) return secondBrightness;

        const int32_t span = static_cast<int32_t>(secondLux - firstLux);
        const int32_t offset = static_cast<int32_t>(lux - firstLux);
        const int32_t brightnessDelta = static_cast<int32_t>(secondBrightness) - firstBrightness;
        int32_t scaled = brightnessDelta * offset;
        scaled += scaled >= 0 ? span / 2 : -(span / 2);
        const int32_t result = static_cast<int32_t>(firstBrightness) + scaled / span;
        return static_cast<uint8_t>(result < 0 ? 0 : (result > 255 ? 255 : result));
    }

    uint16_t latestLux() const { return measuredLux_; }
    uint16_t acceptedLux() const { return acceptedLux_; }
    uint8_t targetBrightness() const { return targetBrightness_; }
    bool hasReading() const { return valid_; }

    /// Host-test entry: process a known lux value through the same hysteresis and publication
    /// path as a hardware read. Mirrors IrService::injectCodeForTest.
    void injectLuxForTest(uint16_t lux) {
        seat_.claim();
        acceptLux(lux);
    }

private:
    static constexpr uint8_t kPowerOn = 0x01;
    static constexpr uint8_t kContinuousHighResolution = 0x10;
    static constexpr uint32_t kBusFrequencyHz = 100000;
    static constexpr uint32_t kFirstMeasurementMs = 180;
    static constexpr uint32_t kRetryMs = 5000;
    // ESP32 HELIO builds use a 100 Hz FreeRTOS tick. Keep the timeout at two full ticks;
    // sub-tick values round down to an immediate timeout in ESP-IDF's I2C driver.
    static constexpr uint32_t kTransactionTimeoutMs = 20;
    static constexpr uint8_t kFailuresBeforeFallback = 3;

    enum class IoRequest : uint8_t { None, Open, Read };
    enum class IoResult : uint8_t { None, Opened, OpenFailed, Read, ReadFailed };

    static void ioTrampoline(void* user) {
        static_cast<Bh1750Service*>(user)->ioLoop();
    }

    void ioLoop() {
        while (!ioStop_.load(std::memory_order_acquire)) {
            if (!platform::waitNotify(ioTask_, 1000)) continue;
            if (ioStop_.load(std::memory_order_acquire)) break;

            const IoRequest request = ioRequest_.exchange(IoRequest::None,
                                                          std::memory_order_acq_rel);
            if (request == IoRequest::Open) {
                platform::i2cDeviceDeinit(device_);
                const uint8_t powerOn = kPowerOn;
                const uint8_t continuous = kContinuousHighResolution;
                const bool opened =
                    platform::i2cDeviceInit(device_, ioBus_, ioSda_, ioScl_, ioAddress_,
                                            kBusFrequencyHz) &&
                    platform::i2cDeviceWrite(device_, &powerOn, 1, kTransactionTimeoutMs) &&
                    platform::i2cDeviceWrite(device_, &continuous, 1, kTransactionTimeoutMs);
                if (!opened) platform::i2cDeviceDeinit(device_);
                ioResult_.store(opened ? IoResult::Opened : IoResult::OpenFailed,
                                std::memory_order_release);
            } else if (request == IoRequest::Read) {
                uint8_t bytes[2] = {};
                const bool read = platform::i2cDeviceRead(
                    device_, bytes, sizeof(bytes), kTransactionTimeoutMs);
                if (read) {
                    ioRaw_.store(static_cast<uint16_t>(
                        (static_cast<uint16_t>(bytes[0]) << 8u) |
                        static_cast<uint16_t>(bytes[1])), std::memory_order_relaxed);
                }
                ioResult_.store(read ? IoResult::Read : IoResult::ReadFailed,
                                std::memory_order_release);
            }
        }
        platform::i2cDeviceDeinit(device_);
    }

    void startIoWorker(uint32_t now) {
        if (ioTask_.impl) return;
        if (sda < 0 || scl < 0) {
            setStatus("set sda + scl pins", Severity::Warning);
            nextRetryMs_ = now + kRetryMs;
            return;
        }
        ioBus_ = bus;
        ioSda_ = static_cast<uint16_t>(sda);
        ioScl_ = static_cast<uint16_t>(scl);
        ioAddress_ = address;
        ioStop_.store(false, std::memory_order_release);
        ioRequest_.store(IoRequest::None, std::memory_order_release);
        ioResult_.store(IoResult::None, std::memory_order_release);
        ioBusy_.store(false, std::memory_order_release);
        if (!platform::spawnPinnedTask(ioTask_, "bh1750", &ioTrampoline, this,
                                       4096, 2, -1)) {
            std::snprintf(statusBuf_, sizeof(statusBuf_), "BH1750 worker unavailable");
            setStatus(statusBuf_, Severity::Warning);
            nextRetryMs_ = now + kRetryMs;
            return;
        }
        queueIo(IoRequest::Open);
    }

    void stopIoWorker() {
        ioStop_.store(true, std::memory_order_release);
        platform::stopPinnedTask(ioTask_);
        ioRequest_.store(IoRequest::None, std::memory_order_release);
        ioResult_.store(IoResult::None, std::memory_order_release);
        ioBusy_.store(false, std::memory_order_release);
        inited_ = false;
    }

    void queueIo(IoRequest request) {
        bool idle = false;
        if (!ioBusy_.compare_exchange_strong(idle, true, std::memory_order_acq_rel)) return;
        ioRequest_.store(request, std::memory_order_release);
        platform::notifyTask(ioTask_);
    }

    void consumeIoResult(uint32_t now) {
        const IoResult result = ioResult_.exchange(IoResult::None, std::memory_order_acq_rel);
        if (result != IoResult::None) ioBusy_.store(false, std::memory_order_release);
        switch (result) {
            case IoResult::None:
                return;
            case IoResult::Opened:
                inited_ = true;
                failures_ = 0;
                readReadyAtMs_ = now + kFirstMeasurementMs;
                std::snprintf(statusBuf_, sizeof(statusBuf_), "waiting for first measurement");
                setStatus(statusBuf_);
                return;
            case IoResult::OpenFailed:
                inited_ = false;
                std::snprintf(statusBuf_, sizeof(statusBuf_), "BH1750 0x%02X unavailable",
                              static_cast<unsigned>(address));
                setStatus(statusBuf_, Severity::Warning);
                nextRetryMs_ = now + kRetryMs;
                return;
            case IoResult::Read:
                failures_ = 0;
                acceptLux(rawToLux(ioRaw_.load(std::memory_order_relaxed)));
                return;
            case IoResult::ReadFailed:
                handleReadFailure(now);
                return;
        }
    }

    void handleReadFailure(uint32_t now) {
        if (failures_ < UINT8_MAX) failures_++;
        std::snprintf(statusBuf_, sizeof(statusBuf_), "BH1750 read failed (%u/%u)",
                      static_cast<unsigned>(failures_),
                      static_cast<unsigned>(kFailuresBeforeFallback));
        setStatus(statusBuf_, Severity::Warning);
        if (failures_ < kFailuresBeforeFallback) return;

        invalidate();
        inited_ = false;
        nextRetryMs_ = now + kRetryMs;
    }

    void acceptLux(uint16_t lux) {
        measuredLux_ = lux;
        const uint16_t difference = measuredLux_ > acceptedLux_
            ? static_cast<uint16_t>(measuredLux_ - acceptedLux_)
            : static_cast<uint16_t>(acceptedLux_ - measuredLux_);
        if (!valid_ || difference >= hysteresisLux) {
            acceptedLux_ = measuredLux_;
            targetBrightness_ = brightnessForLux(
                acceptedLux_, darkLux, brightLux, darkBrightness, brightBrightness);
        }
        valid_ = true;
        updateReadouts();
        syncPublication();
    }

    void invalidate() {
        valid_ = false;
        std::snprintf(luxStr_, sizeof(luxStr_), "--");
        std::snprintf(targetStr_, sizeof(targetStr_), "manual");
        syncPublication();
    }

    void updateReadouts() {
        if (!valid_) return;
        std::snprintf(luxStr_, sizeof(luxStr_), "%u lx", static_cast<unsigned>(measuredLux_));
        if (automatic) {
            std::snprintf(targetStr_, sizeof(targetStr_), "%u", static_cast<unsigned>(targetBrightness_));
            std::snprintf(statusBuf_, sizeof(statusBuf_), "%u lx -> brightness %u",
                          static_cast<unsigned>(measuredLux_),
                          static_cast<unsigned>(targetBrightness_));
        } else {
            std::snprintf(targetStr_, sizeof(targetStr_), "manual");
            std::snprintf(statusBuf_, sizeof(statusBuf_), "%u lx, manual brightness",
                          static_cast<unsigned>(measuredLux_));
        }
        setStatus(statusBuf_);
    }

    void syncPublication() {
        const bool available = seat_.seated() && automatic && valid_;
        if (available == published_ && (!available || publishedBrightness_ == targetBrightness_)) return;
        published_ = available;
        publishedBrightness_ = targetBrightness_;
        brightnessRevision_++;
    }

    void withdraw() {
        stopIoWorker();
        invalidate();
        seat_.vacate();
        published_ = false;
    }

    platform::I2cDeviceHandle device_{};
    platform::WorkerTask ioTask_{};
    std::atomic<bool> ioStop_{false};
    std::atomic<IoRequest> ioRequest_{IoRequest::None};
    std::atomic<IoResult> ioResult_{IoResult::None};
    std::atomic<bool> ioBusy_{false};
    std::atomic<uint16_t> ioRaw_{0};
    uint8_t ioBus_ = 0;
    uint8_t ioAddress_ = 0;
    uint16_t ioSda_ = 0;
    uint16_t ioScl_ = 0;
    ActiveInstance<Bh1750Service> seat_{*this};
    bool inited_ = false;
    bool valid_ = false;
    bool published_ = false;
    uint8_t failures_ = 0;
    uint8_t targetBrightness_ = 16;
    uint8_t publishedBrightness_ = 0;
    uint16_t measuredLux_ = 0;
    uint16_t acceptedLux_ = 0;
    uint32_t readReadyAtMs_ = 0;
    uint32_t nextRetryMs_ = 0;
    char luxStr_[16] = "--";
    char targetStr_[16] = "manual";
    char statusBuf_[48] = "waiting for sensor";

    static inline uint32_t brightnessRevision_ = 0;
};

} // namespace mm
