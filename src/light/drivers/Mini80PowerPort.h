#pragma once

#include "light/drivers/MiniLedPowerSequence.h"
#include "light/drivers/RmtLedDriver.h"
#include "platform/platform.h"

namespace mm {

/// Unbound adapter for the source-identified MINI80 pins. A product must own the driver and both GPIOs exclusively, and serialize this port with tick and all configuration changes. Electrical qualification is still required.
class Mini80PowerPort : public MiniLedPowerSequence::Port {
public:
    /// The GPIO carrying the LED data line (DIN).
    static constexpr uint8_t kDataPin = 47;
    /// The GPIO switching the LED power rail.
    static constexpr uint8_t kEnablePin = 17;
    /// The lights on the one output, matched against the driver's configuration.
    static constexpr nrOfLightsType kLights = 52;
    /// The channels per light the driver must be configured for.
    static constexpr uint8_t kChannels = 4;

    /// Binds the RMT driver this port owns.
    explicit Mini80PowerPort(RmtLedDriver& driver) : driver_(driver) {}

    /// Cold init has independently held DIN and the rail low before any RMT pin or frame exists, which permits teardown in that narrow unconfigured state.
    void noteColdHeld() { coldHeld_ = true; }

    /// Stops visible frames by requesting a black frame.
    void holdVisible() override { driver_.requestBlackFrame(kLights, kChannels); }
    /// Detaches the RMT output and preloads DIN low, true when both succeed, also from the cold state.
    bool detachAndHoldLow() override {
        dinHeld_ = false;
        // Safe shutdown must still work after the source disappears or its dimensions become invalid, so only the resolved pin is needed here.
        const bool cold = coldHeld_ && driver_.pinCount() == 0 && driver_.frameCapacity() == 0;
        if ((!cold && !driver_.hasPowerPin(kDataPin)) || !driver_.detachForPower()) return false;
        dinHeld_ = platform::gpioPreloadLowOutput(kDataPin);
        return dinHeld_;
    }
    /// Preloads the enable latch low once DIN is held, before its output driver turns on.
    bool preloadRailLow() override {
        if (!dinHeld_) return false;
        railPreloaded_ = platform::gpioPreloadLowOutput(kEnablePin);
        if (railPreloaded_) railOn_ = false;
        return railPreloaded_;
    }
    /// Drops the rail, safe even after a failed preload, or raises it once DIN, the preload and the output all match.
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
    /// Gives DIN back to RMT once the rail is on, without letting a visible frame out.
    bool attachOutput() override {
        if (!dinHeld_ || !railOn_ || !matches()) return false;
        dinHeld_ = false; // attaching may partially take ownership even on failure
        coldHeld_ = false;
        return driver_.attachForPower() && matches();
    }
    /// Requests the black frame whose latch black() reports.
    void requestBlack() override { driver_.requestBlackFrame(kLights, kChannels); }
    /// The requested black frame's status: Latched, Pending, or Failed when the output no longer matches.
    MiniLedPowerSequence::Black black() const override {
        using Status = RmtLedDriver::BlackFrameStatus;
        const Status status = driver_.blackFrameStatus();
        if (!matches()) return MiniLedPowerSequence::Black::Failed;
        if (status == Status::Latched) return MiniLedPowerSequence::Black::Latched;
        if (status == Status::Pending) return MiniLedPowerSequence::Black::Pending;
        return MiniLedPowerSequence::Black::Failed;
    }
    /// True while the rail is on, DIN belongs to RMT and the output keeps its qualified configuration.
    bool healthy() const override { return railOn_ && !dinHeld_ && matches(); }
    /// Releases the black-frame hold once the rail is on and the output still matches.
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
