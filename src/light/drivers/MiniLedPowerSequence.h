#pragma once

#include <cstdint>

namespace mm {

/// Explicit power ordering for a switched LED rail. A board adapter must own the pins and make detachAndHoldLow drain or cancel the peripheral before it hands DIN to GPIO. No product wires this policy in by default.
class MiniLedPowerSequence {
public:
    enum class Black : uint8_t { Pending, Latched, Failed };
    enum class State : uint8_t { Off, Settling, StartingBlack, On, StoppingBlack, Fault };
    enum class Error : uint8_t { None, Unqualified, Hold, Rail, Attach, Black, Timeout, Release };

    struct Port {
        /// Lets an adapter be destroyed through a Port reference.
        virtual ~Port() = default;
        /// Prevents new visible frames; detachAndHoldLow must drain or cancel an in-flight frame before DIN changes ownership.
        virtual void holdVisible() = 0;
        /// Detach/drain the data peripheral and establish DIN low.
        virtual bool detachAndHoldLow() = 0;
        /// Preload the rail-enable latch low before enabling its output driver.
        virtual bool preloadRailLow() = 0;
        /// OFF must be safe even after a failed preload; report failure if it cannot be guaranteed.
        virtual bool setRail(bool enabled) = 0;
        /// Attach the output peripheral without permitting a visible frame.
        virtual bool attachOutput() = 0;
        /// Requests one black frame over the whole output, which black() then reports.
        virtual void requestBlack() = 0;
        /// The requested black frame's status: Pending, Latched, or Failed.
        virtual Black black() const = 0;
        /// Verify the owned output still has its qualified configuration.
        virtual bool healthy() const = 0;
        /// Release the hold only after the just-requested black frame latched.
        virtual bool releaseVisible() = 0;
    };

    /// Zero or half-range-exceeding times are unqualified and cannot energize the rail.
    bool configure(uint32_t settleMs, uint32_t blackTimeoutMs) {
        if (state_ != State::Off) return false;
        if (!settleMs || !blackTimeoutMs || settleMs > 0x7fffffffu || blackTimeoutMs > 0x7fffffffu) {
            qualified_ = false;
            error_ = Error::Unqualified;
            return false;
        }
        settleMs_ = settleMs;
        blackTimeoutMs_ = blackTimeoutMs;
        qualified_ = true;
        error_ = Error::None;
        return true;
    }

    /// Where the sequence stands: Off, Settling, StartingBlack, On, StoppingBlack or Fault.
    State state() const { return state_; }
    /// The reason for the last refusal or fault, None once cleared.
    Error error() const { return error_; }
    /// Software acknowledgement only: physical rail and DIN need measurement.
    bool safeOffAcknowledged() const { return safeOff_; }

    /// Lifecycle teardown cannot wait for another frame tick: holds and drains DIN before dropping the rail, and false means software could not confirm that handover, so the owner retains the port until it can retry.
    bool shutdown(Port& port) {
        stop(port);
        return safeOff_;
    }

    /// Advances the sequence one step toward `requestedOn`, `now` being the millisecond clock the settle and black timeouts count against.
    void tick(Port& port, bool requestedOn, uint32_t now) {
        switch (state_) {
        case State::Off:
            if (!requestedOn) return;
            if (!qualified_) { error_ = Error::Unqualified; return; }
            safeOff_ = false;
            port.holdVisible();
            if (!port.detachAndHoldLow()) { fail(port, Error::Hold); return; }
            if (!port.preloadRailLow()) { fail(port, Error::Rail); return; }
            if (!port.setRail(true)) { fail(port, Error::Rail); return; }
            phaseAt_ = now;
            state_ = State::Settling;
            return;
        case State::Settling:
            if (!requestedOn) { stop(port); return; }
            if (now - phaseAt_ < settleMs_) return;
            if (!port.attachOutput()) { fail(port, Error::Attach); return; }
            port.requestBlack();
            phaseAt_ = now;
            state_ = State::StartingBlack;
            return;
        case State::StartingBlack:
            if (!requestedOn) state_ = State::StoppingBlack;
            finishBlack(port, requestedOn, now);
            return;
        case State::On:
            if (!port.healthy()) { fail(port, Error::Hold); return; }
            if (requestedOn) return;
            port.holdVisible();
            port.requestBlack();
            phaseAt_ = now;
            state_ = State::StoppingBlack;
            return;
        case State::StoppingBlack:
            // Complete the stop even if ON was requested again; the next start repeats the full rail-settle and black-frame sequence.
            finishBlack(port, false, now);
            return;
        case State::Fault:
            return;
        }
    }

    /// Recovery must re-establish a held DIN and a successful rail-off command.
    bool clearFault(Port& port) {
        if (state_ != State::Fault) return false;
        port.holdVisible();
        if (!port.detachAndHoldLow() || !port.setRail(false)) { safeOff_ = false; return false; }
        safeOff_ = true;
        error_ = Error::None;
        state_ = State::Off;
        return true;
    }

private:
    void stop(Port& port) {
        port.holdVisible();
        if (!port.detachAndHoldLow()) { state_ = State::Fault; error_ = Error::Hold; safeOff_ = false; return; }
        if (!port.setRail(false)) { state_ = State::Fault; error_ = Error::Rail; safeOff_ = false; return; }
        safeOff_ = true;
        error_ = Error::None;
        state_ = State::Off;
    }
    void fail(Port& port, Error error) {
        port.holdVisible();
        const bool held = port.detachAndHoldLow();
        safeOff_ = held && port.setRail(false);
        state_ = State::Fault;
        error_ = error;
    }
    void finishBlack(Port& port, bool requestedOn, uint32_t now) {
        const Black result = port.black();
        if (result == Black::Failed) { fail(port, Error::Black); return; }
        if (result == Black::Pending) {
            if (now - phaseAt_ >= blackTimeoutMs_) fail(port, Error::Timeout);
            return;
        }
        if (result != Black::Latched) { fail(port, Error::Black); return; }
        if (!requestedOn) { stop(port); return; }
        if (!port.releaseVisible()) { fail(port, Error::Release); return; }
        state_ = State::On;
    }

    State state_ = State::Off;
    Error error_ = Error::Unqualified;
    uint32_t settleMs_ = 0, blackTimeoutMs_ = 0, phaseAt_ = 0;
    bool qualified_ = false;
    bool safeOff_ = false;
};
} // namespace mm
