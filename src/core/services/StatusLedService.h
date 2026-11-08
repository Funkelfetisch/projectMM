#pragma once

#include "core/module/MoonModule.h"
#include "core/module/Scheduler.h"            // Scheduler::instance: where the BLE service is found
#include "core/system/FirmwareUpdateModule.h" // otaInFlight: the one rule for an install in progress
#include "platform/platform.h"                // pwmStart / pwmAttach / pwmWrite, the WiFi and Ethernet state

#include <cstdint>
#include <cstring>

namespace mm {

/// One LED on a GPIO that shows whether the device is reachable: steady while a phone or a network reaches it, slow blink while it waits, fast blink while it updates.
/// @card StatusLedService.png
///
/// @guide
///
/// ## What it shows
///
/// Steady while a phone holds a Bluetooth connection, the WiFi station or Ethernet is up, or a client is on the access point. A slow blink, once a second, while none is. A fast blink while a firmware update installs. Switching the lights off leaves it showing.
class StatusLedService : public MoonModule {
public:
    /// What the LED is showing.
    enum class Showing : uint8_t { Waiting, Connected, Updating };

    /// A service, so the container accepts it as a child.
    ModuleRole role() const MM_NONBLOCKING override { return ModuleRole::Service; }

    /// The GPIO of the LED; -1 drives none.
    int8_t pin = -1;
    /// How bright the LED is when lit, so it can sit dim beside a garment's strip.
    uint8_t level = 64;

    /// The pin, the brightness and what it shows.
    void defineControls() override {
        controls_.addPin("pin", pin);
        controls_.addControl("level", level, 1, 255);
        controls_.setPercent(controls_.count() - 1);   // a brightness, which counts nothing
        /// What the LED shows now: connected, waiting or updating.
        controls_.addReadOnly("showing", showingText_, sizeof(showingText_));
        MoonModule::defineControls();
    }

    /// Open the restored pin.
    void setup() override {
        MoonModule::setup();
        open();
    }

    /// A new pin is opened now; a new level shows on the next blink step.
    void onControlChanged(const char* name) override {
        if (std::strcmp(name, "pin") == 0) open();
    }

    /// Disabled, the LED goes dark; enabled, it shows again.
    void onEnabled(bool enabled) override {
        if (enabled) open(); else close();
    }

    /// Sample what to show, once a second: the rate the BLE service refreshes its `connection` readout.
    void tick1s() MM_NONBLOCKING override {
        showing_ = showingNow(Scheduler::instance());
        std::snprintf(showingText_, sizeof(showingText_), "%s", name(showing_));
    }

    /// Step the blink and write the duty that goes with it.
    void tick20ms() MM_NONBLOCKING override {
        if (channel_ < 0) return;
        platform::pwmWrite(channel_, lit(showing_, platform::millis()) ? duty(level) : 0);
    }

    /// Drive the pin low and free its PWM timer.
    void release() override {
        close();
        MoonModule::release();
    }

    /// What a device in this state shows: an update beats a connection, a connection beats waiting.
    static Showing showingNow(Scheduler* scheduler) MM_NONBLOCKING {
        if (otaInFlight()) return Showing::Updating;
        MoonModule* ble = scheduler ? scheduler->firstByName("BLE") : nullptr;
        const bool phone = ble && std::strcmp(ble->readText("connection", ""), "connected") == 0;
        const bool network = platform::wifiStaConnected() || platform::ethConnected() || platform::wifiApClientCount() > 0;
        return phone || network ? Showing::Connected : Showing::Waiting;
    }

    /// Whether the LED is lit at `ms`: steady when connected, 500 ms on and off while waiting, 100 ms while updating.
    static bool lit(Showing showing, uint32_t ms) MM_NONBLOCKING {
        switch (showing) {
            case Showing::Connected: return true;
            case Showing::Updating:  return ms / 100 % 2 == 0;
            case Showing::Waiting:   return ms / 500 % 2 == 0;
        }
        return false;
    }

    /// The readout's word for a state.
    static const char* name(Showing showing) {
        switch (showing) {
            case Showing::Connected: return "connected";
            case Showing::Updating:  return "updating";
            case Showing::Waiting:   return "waiting";
        }
        return "";
    }

    /// The PWM duty `level` gives at the timer's resolution, 255 fully on.
    static uint32_t duty(uint8_t level) MM_NONBLOCKING {
        constexpr uint32_t kMax = (uint32_t{1} << kBits) - 1;
        return (uint32_t{level} * kMax + 127) / 255;
    }

    /// The PWM channel the LED is on, -1 when none, for the tests.
    int channelForTest() const { return channel_; }

private:
    /// 12 bits pulse at 19.5 kHz, past what a camera or an eye sees flicker in.
    static constexpr uint8_t kBits = 12;

    int timer_ = -1;
    int channel_ = -1;
    Showing showing_ = Showing::Waiting;
    char showingText_[12] = "waiting";

    /// Attach the pin to a fresh timer, or report why it cannot be.
    void open() {
        close();
        if (pin < 0 || !enabled()) { setStatus(nullptr); return; }
        timer_ = platform::pwmStart(kBits);
        if (timer_ >= 0) channel_ = platform::pwmAttach(timer_, static_cast<uint8_t>(pin), 0);
        if (channel_ < 0) { close(); setStatus("that pin takes no PWM channel, or none is free", Severity::Warning); return; }
        setStatus(nullptr);
    }

    /// Stop the timer, which drives the pin low.
    void close() {
        if (timer_ >= 0) platform::pwmStop(timer_);
        timer_ = channel_ = -1;
    }
};

}  // namespace mm
