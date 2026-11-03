#pragma once

#include "light/drivers/Mini80PowerPort.h"
#include "light/drivers/Mini80PinPreflight.h"
#include "core/util/PinList.h"

namespace mm {

/// Drives one already configured MINI80 RMT output through the power sequence.
///
/// The future module must own the driver and GPIOs exclusively, serialize every operation with source/configuration changes, and call shutdown before release. This controller is not registered as a selectable product until the board's timings, pin ownership and hardware behavior are qualified.
class Mini80PowerController {
public:
    enum class StartupError : uint8_t { None, Unqualified, AlreadyInitialized, DriverInUse, DriverOwned, PinsReserved, InvalidRelayConfig, RelayConflict, PinConflict, PinClaimsUnknown, Detach, DataHold, RailOff };
    /// Bind the controller to the one RMT driver it sequences.
    explicit Mini80PowerController(RmtLedDriver& driver) : driver_(driver), port_(driver) {}

    /// Set the rail settle and black-frame timeouts; false once initialized, or when the timings are unqualified.
    bool configure(uint32_t settleMs, uint32_t blackTimeoutMs) {
        if (initialized_) { startupError_ = StartupError::AlreadyInitialized; return false; }
        configured_ = sequence_.configure(settleMs, blackTimeoutMs);
        startupError_ = configured_ ? StartupError::None : StartupError::Unqualified;
        return configured_;
    }
    /// Scan declared pin users and pass the current relay list while the tree is quiesced; the owner prevents later conflicts, and GPIO API success is no measurement of the board's electrical levels.
    bool initializeCold(Scheduler& scheduler, const MoonModule* owner, const char* relayPins) {
        if (!configured_) { startupError_ = StartupError::Unqualified; return false; }
        if (initialized_) { startupError_ = StartupError::AlreadyInitialized; return false; }
        if (driver_.pinCount() || driver_.frameCapacity()) { startupError_ = StartupError::DriverInUse; return false; }
        const auto pins = Mini80PinPreflight::check(scheduler, owner, &driver_);
        if (pins == Mini80PinPreflight::Result::Conflict) { startupError_ = StartupError::PinConflict; return false; }
        if (pins != Mini80PinPreflight::Result::Clear) { startupError_ = StartupError::PinClaimsUnknown; return false; }
        if (!relayPins) { startupError_ = StartupError::InvalidRelayConfig; return false; }
        if (*relayPins) {
            uint16_t parsed[8] = {}; uint8_t count = 0; // Drivers::kMaxRelays
            if (parsePinList(relayPins, parsed, 8, count)) { startupError_ = StartupError::InvalidRelayConfig; return false; }
            for (uint8_t i = 0; i < count; ++i)
                if (parsed[i] == Mini80PowerPort::kEnablePin || parsed[i] == Mini80PowerPort::kDataPin) {
                    startupError_ = StartupError::RelayConflict; return false;
                }
        }
        // Retain ownership even on a partial cold-start failure; a retry by this same controller remains permitted until safe shutdown.
        if (scheduler_ && scheduler_ != &scheduler) { startupError_ = StartupError::PinsReserved; return false; }
        const bool alreadyOwned = driver_.powerOwnedBy(this);
        if (!driver_.claimPowerOwner(this)) { startupError_ = StartupError::DriverOwned; return false; }
        const uint16_t reservedPins[] = {Mini80PowerPort::kDataPin, Mini80PowerPort::kEnablePin};
        if (!scheduler.reservePins(this, reservedPins, 2)) {
            if (!alreadyOwned) driver_.releasePowerOwner(this);
            startupError_ = StartupError::PinsReserved; return false;
        }
        scheduler_ = &scheduler;
        // Keep prepare/tick from attaching RMT while the LED rail is off; the later sequenced attach retains this black hold until it latches.
        driver_.requestBlackFrame(Mini80PowerPort::kLights, Mini80PowerPort::kChannels);
        if (!driver_.detachForPower()) { startupError_ = StartupError::Detach; return false; }
        if (!platform::gpioPreloadLowOutput(Mini80PowerPort::kDataPin)) { startupError_ = StartupError::DataHold; return false; }
        if (!platform::gpioPreloadLowOutput(Mini80PowerPort::kEnablePin)) { startupError_ = StartupError::RailOff; return false; }
        port_.noteColdHeld();
        initialized_ = true;
        startupError_ = StartupError::None;
        return true;
    }
    /// requestedOn is the owner's power decision, including any brightness-zero policy.
    void tick(bool requestedOn, uint32_t now) {
        if (!initialized_) return;
        sequence_.tick(port_, requestedOn, now);
        const auto phase = sequence_.state();
        if (phase == MiniLedPowerSequence::State::StartingBlack ||
            phase == MiniLedPowerSequence::State::StoppingBlack ||
            phase == MiniLedPowerSequence::State::On) driver_.tick();
    }
    /// Called by a future owner during nonblocking tree shutdown, without normal ticks: completes a black-frame stop, else attempts the checked safe-off handover and keeps ownership when it cannot complete.
    bool shutdownStep(uint32_t now) {
        if (!initialized_) {
            if (driver_.powerOwnedBy(this)) return abortColdStart();
            return scheduler_ == nullptr;
        }
        tick(false, now);
        const auto phase = sequence_.state();
        if (phase != MiniLedPowerSequence::State::Off && phase != MiniLedPowerSequence::State::Fault) return false;
        return shutdown();
    }
    /// Retain this controller and its driver if the handover cannot be confirmed; once released, ticks cannot restart the rail without a new cold preflight.
    bool shutdown() {
        if (!initialized_ || !sequence_.shutdown(port_)) return false;
        if (!sequence_.safeOffAcknowledged() || !driver_.finishPowerHandover(this)) return false;
        if (!releaseOwnership()) return false;
        initialized_ = false;
        return true;
    }
    /// Abort a partial cold start without releasing ownership until DIN and the rail are commanded low; a failed attempt keeps the lock for retry.
    bool abortColdStart() {
        if (initialized_ || !driver_.powerOwnedBy(this)) return false;
        driver_.requestBlackFrame(Mini80PowerPort::kLights, Mini80PowerPort::kChannels);
        if (!driver_.detachForPower() || !platform::gpioPreloadLowOutput(Mini80PowerPort::kDataPin) ||
            !platform::gpioPreloadLowOutput(Mini80PowerPort::kEnablePin)) return false;
        if (!driver_.finishPowerHandover(this)) return false;
        return releaseOwnership();
    }
    /// The power sequence's current phase.
    MiniLedPowerSequence::State state() const { return sequence_.state(); }
    /// The power sequence's fault, if any.
    MiniLedPowerSequence::Error error() const { return sequence_.error(); }
    /// Why the last configure or cold start was refused; None after a success.
    StartupError startupError() const { return startupError_; }
    /// A future owning DriverBase must expose this reservation before startup.
    bool reservesPowerPin(uint8_t pin) const { return pin == Mini80PowerPort::kEnablePin; }
    /// Whether the sequence acknowledged the rail safely off.
    bool safeOffAcknowledged() const { return sequence_.safeOffAcknowledged(); }
private:
    bool releaseOwnership() {
        if (!scheduler_ || !driver_.powerOwnedBy(this) ||
            !scheduler_->pinReservedBy(this, Mini80PowerPort::kDataPin) ||
            !scheduler_->pinReservedBy(this, Mini80PowerPort::kEnablePin)) return false;
        if (!scheduler_->releasePins(this) || !driver_.releasePowerOwner(this)) return false;
        scheduler_ = nullptr;
        return true;
    }
    RmtLedDriver& driver_;
    Mini80PowerPort port_;
    MiniLedPowerSequence sequence_;
    Scheduler* scheduler_ = nullptr;
    bool configured_ = false;
    bool initialized_ = false;
    StartupError startupError_ = StartupError::Unqualified;
};
} // namespace mm
