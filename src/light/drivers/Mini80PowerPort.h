#pragma once

#include "light/drivers/MiniLedPowerSequence.h"
#include "light/drivers/RmtLedDriver.h"
#include "platform/platform.h"

namespace mm {

/// Unbound adapter for the source-identified MINI80 pins. A product must own
/// the driver and both GPIOs exclusively, and serialize this port with tick
/// and all configuration changes. Electrical qualification is still required.
class Mini80PowerPort : public MiniLedPowerSequence::Port {
public:
    static constexpr uint8_t kDataPin = 47;
    static constexpr uint8_t kEnablePin = 17;
    static constexpr nrOfLightsType kLights = 52;
    static constexpr uint8_t kChannels = 4;

    explicit Mini80PowerPort(RmtLedDriver& driver) : driver_(driver) {}

    /// Cold init has independently held DIN and the rail low before any RMT
    /// pin or frame exists. Permit teardown in that narrow unconfigured state.
    void noteColdHeld() { coldHeld_ = true; }

    void holdVisible() override { driver_.requestBlackFrame(kLights, kChannels); }
    bool detachAndHoldLow() override {
        dinHeld_ = false;
        // Safe shutdown must still work after the source disappears or its
        // dimensions become invalid. Only the resolved pin is needed here.
        const bool cold = coldHeld_ && driver_.pinCount() == 0 && driver_.frameCapacity() == 0;
        if ((!cold && !driver_.hasPowerPin(kDataPin)) || !driver_.detachForPower()) return false;
        dinHeld_ = platform::gpioPreloadLowOutput(kDataPin);
        return dinHeld_;
    }
    bool preloadRailLow() override {
        if (!dinHeld_) return false;
        railPreloaded_ = platform::gpioPreloadLowOutput(kEnablePin);
        if (railPreloaded_) railOn_ = false;
        return railPreloaded_;
    }
    bool setRail(bool enabled) override {
        if (!enabled) {
            if (!dinHeld_) return false;
            railPreloaded_ = platform::gpioPreloadLowOutput(kEnablePin);
            if (railPreloaded_) railOn_ = false;
            return railPreloaded_;
        }
        if (!dinHeld_ || !railPreloaded_ || !matches()) return false;
        // Even a failed GPIO call may have changed the pin; recover via OFF.
        railOn_ = true;
        return platform::gpioWrite(kEnablePin, true);
    }
    bool attachOutput() override {
        if (!dinHeld_ || !railOn_ || !matches()) return false;
        dinHeld_ = false; // attaching may partially take ownership even on failure
        coldHeld_ = false;
        return driver_.attachForPower() && matches();
    }
    void requestBlack() override { driver_.requestBlackFrame(kLights, kChannels); }
    MiniLedPowerSequence::Black black() const override {
        using Status = RmtLedDriver::BlackFrameStatus;
        const Status status = driver_.blackFrameStatus();
        if (!matches()) return MiniLedPowerSequence::Black::Failed;
        if (status == Status::Latched) return MiniLedPowerSequence::Black::Latched;
        if (status == Status::Pending) return MiniLedPowerSequence::Black::Pending;
        return MiniLedPowerSequence::Black::Failed;
    }
    bool healthy() const override { return railOn_ && !dinHeld_ && matches(); }
    bool releaseVisible() override { return railOn_ && !dinHeld_ && matches() && driver_.releaseBlackFrame(); }

private:
    bool matches() const { return driver_.matchesPowerOutput(kDataPin, kLights, kChannels); }
    RmtLedDriver& driver_;
    bool railPreloaded_ = false;
    bool dinHeld_ = false;
    bool railOn_ = false;
    bool coldHeld_ = false;
};
} // namespace mm
