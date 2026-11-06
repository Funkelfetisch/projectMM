#pragma once

#include "core/module/MoonModule.h"
#include "core/module/Scheduler.h"
#include "core/services/BatteryChannel.h"
#include "core/services/BatteryGateGuard.h"
#include "core/util/ActiveInstance.h"
#include "core/util/PinList.h"
#include "platform/platform.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <new>

namespace mm {

/// One background sampler for up to two independent, explicitly configured BatteryChannel children.
/// @card BatteryService.png
/// @guide A mains-only product has no battery children. Sampling never runs on the render thread, and charging remains unknown without a qualified detector.
class BatteryService : public MoonModule {
public:
    /// A service, so the scheduler runs it with the other background services.
    ModuleRole role() const MM_NONBLOCKING override { return ModuleRole::Service; }
    /// Only service children, which `collect` narrows to BatteryChannel.
    const char* acceptsChildRoles() const override { return "service"; }
    /// Interval between measurement batches; each channel sets its own sample lifetime.
    uint16_t pollSeconds = 3;

    /// Stops the sampler and releases its pins.
    ~BatteryService() override { stop(); }
    /// Structural edits and child control changes park the sampler until prepare.
    void quiesce() override { restartAfterGateFault_ = false; stop(); }
    /// Publish the poll interval and the configuration verdict.
    void defineControls() override {
        controls_.addControl("pollSeconds", pollSeconds, 1, 60);
        /// Whether the battery channels form a valid configuration.
        controls_.addReadOnly("configuration", configurationText_, sizeof(configurationText_));
        MoonModule::defineControls();
    }
    /// Validate the channels, reserve their pins and restart the sampler task when its inputs changed.
    void prepare() override {
        seat_.claim();
        pinConflict_ = false;
        std::array<BatteryChannel*, 2> channels{};
        if (!collect(channels)) { invalidateChannels(); stop(); return; }
        std::array<Input, 2> inputs{};
        for (size_t i = 0; i < channels.size(); ++i) {
            if (!channels[i]) continue;
            auto& channel = *channels[i];
            channel.configureReading();
            channel.updateText(platform::millis());
            inputs[i] = inputFor(channel);
        }
        if (io_ && !io_->stopped.load(std::memory_order_acquire) &&
            !io_->gateFailed.load(std::memory_order_acquire) && io_->inputs == inputs) return;
        if (!stop()) return;
        restartAfterGateFault_ = false;
        if (inputs[0].adcPin < 0 && inputs[1].adcPin < 0) { setStatus("no battery channels to sample"); return; }
        if (auto* scheduler = Scheduler::instance()) {
            uint16_t pins[4] = {}; size_t count = 0;
            for (const auto& input : inputs) {
                if (input.adcPin < 0) continue;
                for (int pin : {static_cast<int>(input.adcPin), static_cast<int>(input.gate)}) {
                    if (pin < 0) continue;
                    bool duplicate = false;
                    for (size_t i = 0; i < count; ++i) if (pins[i] == static_cast<uint16_t>(pin)) duplicate = true;
                    if (!duplicate) pins[count++] = static_cast<uint16_t>(pin);
                }
            }
            if (!pinsAvailable(*scheduler, pins, count) || !scheduler->reservePins(this, pins, count)) {
                for (auto* channel : channels) if (channel) channel->invalidateReading(platform::millis());
                pinConflict_ = true;
                std::snprintf(configurationText_, sizeof(configurationText_), "%s", "invalid");
                setStatus("battery pins already in use or unknown", Severity::Error); return;
            }
            pinScheduler_ = scheduler;
        }
        auto* io = new (std::nothrow) Io();
        if (!io) { invalidateChannels(); releasePins(); setStatus("battery worker memory unavailable", Severity::Warning); return; }
        io->inputs = inputs;
        io->owners.store(2, std::memory_order_relaxed);
        if (!platform::spawnPinnedTask(io->worker, "battery", &workerEntry, io, 4096, 2, -1)) {
            io->drop(); io->drop(); releasePins();
            invalidateChannels();
            setStatus("battery worker unavailable", Severity::Warning); return;
        }
        io_ = io;
        worker_ = io->worker;
        request(platform::millis());
        setStatus("waiting for battery measurement");
    }
    /// Stop the sampler and give up the active-service seat.
    void release() override {
        restartAfterGateFault_ = false;
        if (!stop()) return;
        seat_.vacate();
        MoonModule::release();
    }
    /// True while a stopped sampler still holds its worker, so an ancestor cannot be removed yet.
    bool blocksAncestorRemoval() const override {
        return io_ && io_->stopped.load(std::memory_order_acquire);
    }
    /// Reclaim a stopped sampler during shutdown.
    void shutdownStep(uint32_t now) MM_NONBLOCKING override {
        restartAfterGateFault_ = false;
        if (io_ && io_->stopped.load(std::memory_order_acquire)) stop();
        MoonModule::shutdownStep(now);
    }
    /// No control change needs a prepare.
    bool affectsPrepare(const char*) const override { return false; }
    /// A new poll interval schedules the next batch at once.
    void onControlChanged(const char* name) override {
        if (std::strcmp(name, "pollSeconds") == 0) nextSampleMs_ = platform::millis();
        MoonModule::onControlChanged(name);
    }

    /// Take in a finished batch, and request a prepare after a sense-gate fault.
    void tick20ms() MM_NONBLOCKING override {
        seat_.claim();
        if (io_ && io_->stopped.load(std::memory_order_acquire)) { MoonModule::tick20ms(); return; }
        const uint8_t failedGate = io_ ? io_->gateFailed.load(std::memory_order_acquire) : 0;
        if (failedGate) {
            std::array<BatteryChannel*, 2> channels{};
            if (collect(channels)) for (size_t i = 0; i < channels.size(); ++i)
                if (channels[i] && (failedGate & (1u << i))) channels[i]->invalidateReading(platform::millis());
            restartAfterGateFault_ = true;
            if (auto* scheduler = Scheduler::instance()) scheduler->requestPrepare(this);
            MoonModule::tick20ms(); return;
        }
        if (io_ && io_->completed.exchange(false, std::memory_order_acq_rel)) {
            std::array<BatteryChannel*, 2> channels{};
            if (collect(channels)) {
                for (size_t i = 0; i < channels.size(); ++i)
                    if (channels[i] && io_->results[i].valid && inputFor(*channels[i]) == io_->inputs[i])
                        channels[i]->acceptAdc(io_->results[i].millivolts, io_->results[i].sampledAt);
                refresh(channels, platform::millis());
            }
            busy_ = false;
        }
        MoonModule::tick20ms();
    }
    /// Refresh the channels and request the next batch once the poll interval has passed.
    void tick1s() MM_NONBLOCKING override {
        seat_.claim();
        if (restartAfterGateFault_) {
            if (auto* scheduler = Scheduler::instance()) scheduler->requestPrepare(this);
            MoonModule::tick1s();
            return;
        }
        if (io_ && io_->stopped.load(std::memory_order_acquire)) {
            stop();
            MoonModule::tick1s();
            return;
        }
        const uint32_t now = platform::millis();
        std::array<BatteryChannel*, 2> channels{};
        if (collect(channels) && !pinConflict_) {
            refresh(channels, now);
            if (io_ && !busy_ && static_cast<int32_t>(now - nextSampleMs_) >= 0) request(now);
        }
        MoonModule::tick1s();
    }

    /// The active sampler; an absent service represents a product without configured battery monitoring.
    static BatteryService* active() MM_NONBLOCKING {
        auto* service = ActiveInstance<BatteryService>::active();
        return service && service->effectivelyEnabled() ? service : nullptr;
    }
    /// Whether the background task owes this service a measurement batch.
    bool samplePending() const MM_NONBLOCKING { return busy_; }
    /// Discovery also works before prepare, so GATT declarations do not depend on service preparation order.
    static BatteryService* discover(Scheduler& scheduler, bool* ambiguous = nullptr) {
        BatteryService* result = nullptr;
        bool multiple = false;
        for (uint8_t i = 0; i < scheduler.moduleCount(); ++i)
            find(scheduler.module(i), result, multiple);
        if (ambiguous) *ambiguous = multiple;
        return multiple ? nullptr : result;
    }
    /// The battery document BLE `battery` and HTTP `/api/system` both answer: the one battery service's packs, an invalid configuration when several claim the role, and no packs without one.
    static void writeDiscovered(JsonSink& sink, Scheduler* scheduler, uint32_t now) {
        bool ambiguous = false;
        BatteryService* battery = scheduler ? discover(*scheduler, &ambiguous) : nullptr;
        if (battery) battery->writeJson(sink, now);
        else if (ambiguous) sink.append("{\"sampling\":false,\"packs\":[],\"error\":\"invalid-configuration\"}");
        else sink.append("{\"sampling\":false,\"packs\":[]}");
    }
    /// Declare standard service identities before radio startup, without reading or driving hardware.
    platform::BleBatteryConfig batteryConfig() {
        platform::BleBatteryConfig config;
        std::array<BatteryChannel*, 2> channels{};
        if (collect(channels)) for (size_t i = 0; i < channels.size(); ++i)
            if (channels[i]) config.slots[config.count++] = static_cast<uint8_t>(i + 1);
        return config;
    }
    /// Copy the model into the radio's bounded snapshot; stale/unknown values never become an empty level.
    platform::BleBatterySnapshot snapshot(uint32_t now) {
        platform::BleBatterySnapshot snapshot;
        std::array<BatteryChannel*, 2> channels{};
        if (!collect(channels)) return snapshot;
        for (size_t i = 0; i < channels.size(); ++i) {
            if (!channels[i]) continue;
            const auto& reading = channels[i]->reading();
            auto& value = snapshot.slots[i];
            value.presenceKnown = reading.presence() != BatteryReading::Presence::Unknown;
            value.present = reading.presence() == BatteryReading::Presence::Present;
            value.levelValid = reading.percent(now, value.level);
        }
        return snapshot;
    }
    /// Current configured pack identities, not an invented aggregate battery.
    void writeJson(JsonSink& sink, uint32_t now) {
        std::array<BatteryChannel*, 2> channels{};
        const bool valid = collect(channels);
        sink.append(busy_ ? "{\"sampling\":true,\"packs\":[" : "{\"sampling\":false,\"packs\":[");
        bool comma = false;
        if (valid) for (const auto* channel : channels) {
            if (!channel) continue;
            if (comma) sink.append(",");
            channel->writeJson(sink, now); comma = true;
        }
        sink.append("]");
        if (!valid || pinConflict_) sink.append(",\"error\":\"invalid-configuration\"");
        sink.append("}");
    }

private:
    void invalidateChannels() {
        const uint32_t now = platform::millis();
        for (uint8_t i = 0; i < childCount(); ++i) {
            auto* node = child(i);
            if (node->enabled() && std::strcmp(node->typeName(), "BatteryChannel") == 0)
                static_cast<BatteryChannel*>(node)->invalidateReading(now);
        }
    }
    struct PinScratch { uint16_t declared[64] = {}; MoonModule::FixedPin fixed[16] = {}; };
    bool pinsAvailable(const Scheduler& scheduler, const uint16_t* pins, size_t count) const {
        bool found = false; PinScratch scratch{};
        for (uint8_t i = 0; i < scheduler.moduleCount(); ++i)
            if (!pinsAvailableIn(scheduler.module(i), pins, count, found, 0, scratch)) return false;
        return found;
    }
    bool pinsAvailableIn(const MoonModule* node, const uint16_t* pins, size_t count, bool& found, uint8_t depth, PinScratch& scratch) const {
        if (!node || depth >= 32) return false;
        if (node == this) { found = true; return true; }
        auto matches = [&](uint16_t pin) { for (size_t i = 0; i < count; ++i) if (pins[i] == pin) return true; return false; };
        const auto& controls = node->controls();
        for (uint8_t i = 0; i < controls.count(); ++i) {
            const auto& control = controls[i];
            const bool list = control.type == ControlType::Text && isPinListControl(control.name);
            if (control.type != ControlType::Pin && !list) continue;
            if (!control.ptr) return false;
            if (control.type == ControlType::Pin) {
                const int8_t pin = *static_cast<const int8_t*>(control.ptr);
                if (pin >= 0 && matches(static_cast<uint8_t>(pin))) return false;
            } else {
                const char* text = static_cast<const char*>(control.ptr);
                if (!*text) continue;
                uint8_t n = 0;
                if (parsePinList(text, scratch.declared, 64, n)) return false;
                for (uint8_t p = 0; p < n; ++p) if (matches(scratch.declared[p])) return false;
            }
        }
        const auto n = node->fixedPins(scratch.fixed, 16);
        if (n >= 16) return false;
        for (uint8_t i = 0; i < n; ++i) if (matches(scratch.fixed[i].gpio)) return false;
        for (uint8_t i = 0; i < node->childCount(); ++i)
            if (!pinsAvailableIn(node->child(i), pins, count, found, static_cast<uint8_t>(depth + 1), scratch)) return false;
        return true;
    }
    bool releasePins() {
        if (!pinScheduler_) return true;
        if (Scheduler::instance() != pinScheduler_ || !pinScheduler_->releasePins(this)) return false;
        pinScheduler_ = nullptr; return true;
    }
    static void find(MoonModule* node, BatteryService*& result, bool& multiple) {
        if (!node || !node->effectivelyEnabled() || multiple) return;
        if (std::strcmp(node->typeName(), "BatteryService") == 0) {
            if (result) { multiple = true; return; }
            result = static_cast<BatteryService*>(node);
        }
        for (uint8_t i = 0; i < node->childCount(); ++i)
            find(node->child(i), result, multiple);
    }
    struct Input {
        int8_t adcPin = -1;
        int8_t gate = -1;
        bool gateHigh = true;
        uint16_t settleMs = 0;
        bool operator==(const Input&) const = default;
    };
    struct Result { uint16_t millivolts = 0; uint32_t sampledAt = 0; bool valid = false; };
    static Input inputFor(const BatteryChannel& channel) {
        return channel.canSample() ? Input{channel.adcPin, channel.senseGate, channel.gateHigh, channel.settleMs} : Input{};
    }
    // The worker owns its context independently, including when the platform's bounded stop detaches a stalled task.
    struct Io {
        platform::WorkerTask worker{};
        std::array<Input, 2> inputs{};
        std::array<Result, 2> results{};
        std::atomic<unsigned> owners{1};
        std::atomic<bool> stopped{false};
        std::atomic<bool> requested{false};
        std::atomic<bool> completed{false};
        // Bit per slot: a failed gate makes that channel's last sample unreliable.
        std::atomic<uint8_t> gateFailed{0};
        bool workerStopped = false; // service thread only
        BatteryGateGuard gate;
        void drop() { if (owners.fetch_sub(1, std::memory_order_acq_rel) == 1) delete this; }
    };

    bool collect(std::array<BatteryChannel*, 2>& channels) {
        std::snprintf(configurationText_, sizeof(configurationText_), "%s", pinConflict_ ? "invalid" : "ok");
        for (uint8_t i = 0; i < childCount(); ++i) {
            auto* node = child(i);
            if (!node->enabled()) continue;
            if (std::strcmp(node->typeName(), "BatteryChannel") != 0) {
                return invalidConfiguration("BatteryService accepts BatteryChannel children");
            }
            auto* channel = static_cast<BatteryChannel*>(node);
            if (channel->slot < 1 || channel->slot > 2 || channels[channel->slot - 1]) {
                return invalidConfiguration("battery slots must be unique: 1 or 2");
            }
            channels[channel->slot - 1] = channel;
        }
        for (const auto* channel : channels) {
            if (!channel || !channel->canSample()) continue;
            for (const auto* other : channels) {
                if (!other) continue;
                if (channel != other && channel->adcPin == other->adcPin) {
                    return invalidConfiguration("independent batteries require distinct ADC inputs");
                }
                // A partially configured channel still declares its ADC pin: never drive it as another channel's switched divider gate.
                if (channel->senseGate >= 0 && channel->senseGate == other->adcPin) {
                    return invalidConfiguration("battery ADC and sense gate cannot share a pin");
                }
                if (channel != other && other->canSample() && channel->senseGate >= 0 && channel->senseGate == other->senseGate &&
                    channel->gateHigh != other->gateHigh) {
                    return invalidConfiguration("shared battery sense gate has conflicting polarity");
                }
            }
        }
        return true;
    }
    bool invalidConfiguration(const char* reason) {
        std::snprintf(configurationText_, sizeof(configurationText_), "%s", "invalid");
        setStatus(reason, Severity::Warning);
        return false;
    }
    void refresh(const std::array<BatteryChannel*, 2>& channels, uint32_t now) {
        bool any = false, unavailable = false;
        for (auto* channel : channels) {
            if (!channel) continue;
            any = true; channel->updateText(now);
            if (channel->presence != 2 && !channel->reading().fresh(now)) unavailable = true;
        }
        setStatus(!any ? "no battery channels" : unavailable ? "battery measurement unavailable or stale" : "battery monitoring",
                  unavailable ? Severity::Warning : Severity::Status);
    }
    void request(uint32_t now) {
        busy_ = true;
        nextSampleMs_ = now + static_cast<uint32_t>(pollSeconds) * 1000;
        io_->requested.store(true, std::memory_order_release);
        platform::notifyTask(worker_);
    }
    static void workerEntry(void* user) {
        auto* io = static_cast<Io*>(user);
        while (!io->stopped.load(std::memory_order_acquire)) {
            if (!platform::waitNotify(io->worker, 1000)) continue;
            if (io->stopped.load(std::memory_order_acquire)) break;
            if (!io->requested.exchange(false, std::memory_order_acq_rel)) continue;
            for (size_t i = 0; i < io->inputs.size(); ++i) {
                io->results[i] = {};
                const auto& input = io->inputs[i];
                if (input.adcPin < 0 || io->stopped.load(std::memory_order_acquire)) continue;
                bool ready = input.gate < 0 || io->gate.whileActive([&] {
                    return platform::gpioPreloadOutput(static_cast<uint8_t>(input.gate), input.gateHigh);
                });
                for (uint16_t waited = 0; ready && waited < input.settleMs && !io->stopped.load(std::memory_order_acquire);) {
                    const uint16_t left = input.settleMs - waited;
                    const uint16_t step = left < 10 ? left : 10;
                    platform::delayMs(step); waited += step;
                }
                uint16_t millivolts = 0;
                ready = ready && !io->stopped.load(std::memory_order_acquire) &&
                        platform::adcReadMv(static_cast<uint8_t>(input.adcPin), millivolts);
                if (input.gate >= 0) {
                    const bool gateOff = io->gate.whileActive([&] {
                        return platform::gpioPreloadOutput(static_cast<uint8_t>(input.gate), !input.gateHigh);
                    });
                    if (!gateOff && !io->stopped.load(std::memory_order_acquire)) {
                        uint8_t affected = 0;
                        for (size_t j = 0; j < io->inputs.size(); ++j)
                            if (io->inputs[j].adcPin >= 0 && io->inputs[j].gate == input.gate)
                                affected |= static_cast<uint8_t>(1u << j);
                        io->gateFailed.store(affected, std::memory_order_release);
                    }
                    ready = gateOff && ready;
                }
                if (ready) io->results[i] = {millivolts, platform::millis(), true};
                if (io->gateFailed.load(std::memory_order_acquire)) break;
            }
            io->completed.store(true, std::memory_order_release);
        }
        io->drop();
    }
    bool stop() {
        auto* io = io_;
        if (!io) return releasePins();
        uint8_t failedGates = 0;
        const bool off = io->gate.stop([&] {
            io->stopped.store(true, std::memory_order_release);
            bool success = true;
            for (size_t i = 0; i < io->inputs.size(); ++i) {
                const auto& input = io->inputs[i];
                bool alreadyAttempted = false;
                for (size_t j = 0; j < i; ++j)
                    if (io->inputs[j].adcPin >= 0 && io->inputs[j].gate == input.gate) alreadyAttempted = true;
                if (alreadyAttempted) continue;
                if (input.adcPin >= 0 && input.gate >= 0 &&
                    !platform::gpioPreloadOutput(static_cast<uint8_t>(input.gate), !input.gateHigh)) {
                    for (size_t j = 0; j < io->inputs.size(); ++j)
                        if (io->inputs[j].adcPin >= 0 && io->inputs[j].gate == input.gate)
                            failedGates |= static_cast<uint8_t>(1u << j);
                    success = false;
                }
            }
            return success;
        });
        if (!io->workerStopped) { platform::stopPinnedTask(worker_); io->workerStopped = true; }
        busy_ = false;
        if (!off) {
            std::array<BatteryChannel*, 2> channels{};
            if (collect(channels)) for (size_t i = 0; i < channels.size(); ++i)
                if (channels[i] && (failedGates & (1u << i))) channels[i]->invalidateReading(platform::millis());
            setStatus("battery sense gate could not be switched off", Severity::Error);
            return false;
        }
        if (!releasePins()) { setStatus("battery pin release unavailable", Severity::Error); return false; }
        io_ = nullptr;
        io->drop();
        return true;
    }

    Io* io_ = nullptr;
    Scheduler* pinScheduler_ = nullptr;
    platform::WorkerTask worker_{};  // stop clears the owner's copy; a detached worker keeps its immutable handle
    ActiveInstance<BatteryService> seat_{*this};
    bool busy_ = false;
    bool pinConflict_ = false;
    bool restartAfterGateFault_ = false;
    uint32_t nextSampleMs_ = 0;
    char configurationText_[12] = "unknown";
};

} // namespace mm
