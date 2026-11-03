/// @module BatteryService
/// @also BatteryChannel

#include "doctest.h"
#include "core/services/BatteryService.h"
#include "core/module/StateDocument.h"
#include "platform/platform.h"

#include <cstdint>
#include <atomic>
#include <cstring>
#include <string>
#include <thread>

namespace {
struct RestoreBatteryInputs {
    ~RestoreBatteryInputs() { mm::platform::clearTestAdcValue(); mm::platform::clearTestGpioLevel(); mm::platform::setTestNowMs(0); }
};
void drain(mm::BatteryService& service) {
    for (int i = 0; i < 200 && service.samplePending(); ++i) {
        mm::platform::delayMs(2); service.tick20ms();
    }
    REQUIRE_FALSE(service.samplePending());
}
void configure(mm::BatteryChannel& channel, uint8_t slot, int8_t adc) {
    channel.setTypeName("BatteryChannel"); channel.slot = slot; channel.adcPin = adc;
    channel.presence = 1; channel.gainNumerator = 2; channel.gainDenominator = 1;
    channel.emptyMv = 3000; channel.fullMv = 4200; channel.estimate = true;
    channel.staleSeconds = 1;
}
std::string document(mm::BatteryService& service) {
    char buffer[512]; mm::JsonSink sink(buffer, sizeof(buffer));
    service.writeJson(sink, mm::platform::millis());
    REQUIRE_FALSE(sink.overflowed()); return buffer;
}
}

TEST_CASE("battery service: a mains-only configuration neither samples nor invents a battery") {
    mm::BatteryService service; service.applyState();
    CHECK(mm::BatteryService::active() == &service);
    CHECK_FALSE(service.samplePending());
    CHECK(document(service) == "{\"sampling\":false,\"packs\":[]}");
    service.release(); CHECK(mm::BatteryService::active() == nullptr);
}

TEST_CASE("battery service: unqualified gated sensing and declared absence never drive GPIO") {
    RestoreBatteryInputs restore;
    mm::platform::setTestGpioLevel(2, true);
    mm::BatteryChannel channel; configure(channel, 1, 1); channel.senseGate = 2;
    mm::BatteryService service; service.addChild(&channel); service.applyState();
    CHECK_FALSE(service.samplePending()); CHECK(mm::platform::gpioRead(2));
    CHECK(document(service).find("\"voltageMv\":null") != std::string::npos);
    channel.settleMs = 20; channel.presence = 2; service.applyState();
    CHECK_FALSE(service.samplePending()); CHECK(mm::platform::gpioRead(2));
    CHECK(document(service).find("\"presence\":\"absent\"") != std::string::npos);
    service.release(); CHECK(mm::platform::gpioRead(2));
}

TEST_CASE("battery service: independent channels preserve their ages and stale failures never become zero") {
    RestoreBatteryInputs restore; mm::platform::setTestNowMs(1000);
    mm::platform::setTestAdcMv(15, 1800); mm::platform::setTestAdcMv(16, 2000);
    mm::BatteryChannel first, second; configure(first, 1, 15); configure(second, 2, 16);
    mm::BatteryService service; service.pollSeconds = 1; service.addChild(&first); service.addChild(&second);
    service.applyState(); drain(service);
    uint8_t percent = 0; REQUIRE(first.reading().percent(1000, percent)); CHECK(percent == 50);
    REQUIRE(second.reading().percent(1000, percent)); CHECK(percent == 83);
    CHECK(document(service).find("\"id\":1") != std::string::npos);
    CHECK(document(service).find("\"id\":2") != std::string::npos);
    mm::platform::setTestNowMs(1500); service.applyState();
    uint32_t age = 0; REQUIRE(first.reading().age(1500, age)); CHECK(age == 500);
    mm::platform::clearTestAdcValue(); mm::platform::setTestNowMs(2101);
    service.tick1s(); drain(service);
    CHECK_FALSE(first.reading().percent(2101, percent));
    CHECK_FALSE(second.reading().percent(2101, percent));
    uint16_t mv = 0; REQUIRE(first.reading().lastVoltage(mv)); CHECK(mv == 3600);
    CHECK(document(service).find("\"state\":\"stale\"") != std::string::npos);
    CHECK(document(service).find("\"percent\":0") == std::string::npos);
    service.release();
}

TEST_CASE("battery service: one lost input becomes stale without hiding the other pack") {
    RestoreBatteryInputs restore; mm::platform::setTestNowMs(1000);
    mm::platform::setTestAdcMv(15, 1800); mm::platform::setTestAdcMv(16, 2000);
    mm::BatteryChannel first, second; configure(first, 1, 15); configure(second, 2, 16);
    mm::BatteryService service; service.pollSeconds = 1; service.addChild(&first); service.addChild(&second);
    service.applyState(); drain(service);
    mm::platform::setTestAdcMv(15, 0); // no usable input for the first pack only
    mm::platform::setTestAdcMv(16, 1900);
    mm::platform::setTestNowMs(2101); service.tick1s(); drain(service);
    uint8_t level = 99;
    CHECK_FALSE(first.reading().percent(2101, level));
    REQUIRE(second.reading().percent(2101, level)); CHECK(level == 67);
    const auto snapshot = service.snapshot(2101);
    CHECK_FALSE(snapshot.slots[0].levelValid);
    CHECK(snapshot.slots[1].levelValid); CHECK(snapshot.slots[1].level == 67);
    const auto json = document(service);
    CHECK(json.find("\"state\":\"stale\"") != std::string::npos);
    CHECK(json.find("\"percent\":67") != std::string::npos);
    service.release();
}

TEST_CASE("battery service: duplicate slot identities cannot silently mix two batteries") {
    mm::BatteryChannel first, second; configure(first, 1, 15); configure(second, 1, 16);
    mm::BatteryService service; service.addChild(&first); service.addChild(&second); service.applyState();
    CHECK_FALSE(service.samplePending());
    CHECK(service.severity() == mm::MoonModule::Severity::Warning);
    CHECK(document(service).find("\"packs\":[]") != std::string::npos);
    CHECK(document(service).find("\"error\":\"invalid-configuration\"") != std::string::npos);
    service.release();
}

TEST_CASE("battery service: invalid configuration cannot revive an earlier measurement") {
    RestoreBatteryInputs restore;
    mm::platform::setTestNowMs(1000);
    mm::platform::setTestAdcMv(15, 1800);
    mm::platform::setTestAdcMv(16, 2000);
    mm::BatteryChannel first, second; configure(first, 1, 15); configure(second, 2, 16);
    mm::BatteryService service; service.addChild(&first); service.addChild(&second);
    service.applyState(); drain(service);
    uint8_t level = 0; REQUIRE(first.reading().percent(1000, level));
    second.slot = 1; service.applyState();
    CHECK_FALSE(first.reading().percent(1000, level));
    CHECK(document(service).find("invalid-configuration") != std::string::npos);
    second.slot = 2; service.applyState();
    CHECK_FALSE(first.reading().percent(1000, level));
    drain(service);
    CHECK(first.reading().percent(1000, level));
    service.release();
}

TEST_CASE("battery service: two independent packs cannot share one ADC input") {
    RestoreBatteryInputs restore;
    mm::platform::setTestAdcMv(15, 1800);
    mm::BatteryChannel first, second; configure(first, 1, 15); configure(second, 2, 15);
    mm::BatteryService service; service.addChild(&first); service.addChild(&second); service.defineControls(); service.applyState();
    CHECK_FALSE(service.samplePending());
    CHECK(service.severity() == mm::MoonModule::Severity::Warning);
    CHECK(document(service).find("\"packs\":[]") != std::string::npos);
    CHECK(document(service).find("\"error\":\"invalid-configuration\"") != std::string::npos);
    CHECK_FALSE(first.reading().fresh(mm::platform::millis()));
    const auto& controls = service.controls();
    const char* configuration = nullptr;
    for (uint8_t i = 0; i < controls.count(); ++i)
        if (std::strcmp(controls[i].name, "configuration") == 0) configuration = static_cast<const char*>(controls[i].ptr);
    REQUIRE(configuration != nullptr);
    CHECK(std::string(configuration) == "invalid");
    second.adcPin = 16; service.applyState(); drain(service);
    CHECK(std::string(configuration) == "ok");
    service.release();
}

TEST_CASE("battery service: an ADC cannot be driven as a sense gate") {
    RestoreBatteryInputs restore;
    mm::platform::setTestGpioLevel(15, false);
    mm::BatteryChannel channel; configure(channel, 1, 15); channel.senseGate = 15; channel.settleMs = 1;
    mm::BatteryService service; service.addChild(&channel); service.applyState();
    CHECK_FALSE(service.samplePending());
    CHECK(service.severity() == mm::MoonModule::Severity::Warning);
    CHECK_FALSE(mm::platform::gpioRead(15));
    service.release();
}

TEST_CASE("battery service: a gate cannot drive an incompletely configured channel's ADC") {
    RestoreBatteryInputs restore;
    for (uint8_t activeSlot : {uint8_t{1}, uint8_t{2}}) {
        mm::platform::setTestGpioLevel(16, true);
        mm::BatteryChannel active, incomplete;
        configure(active, activeSlot, 15);
        configure(incomplete, activeSlot == 1 ? 2 : 1, 16);
        active.senseGate = 16; active.gateHigh = false; active.settleMs = 1;
        incomplete.gainDenominator = 0; // ADC is declared even though sampling is disabled.
        mm::BatteryService service; service.addChild(&active); service.addChild(&incomplete);
        service.applyState();
        CHECK_FALSE(service.samplePending());
        CHECK(service.severity() == mm::MoonModule::Severity::Warning);
        CHECK(mm::platform::gpioRead(16));
        CHECK(document(service).find("\"error\":\"invalid-configuration\"") != std::string::npos);
        service.release(); CHECK(mm::platform::gpioRead(16));
    }
}

TEST_CASE("battery service: invalid live wiring does not schedule the old ADC configuration") {
    RestoreBatteryInputs restore; mm::platform::setTestNowMs(1000);
    mm::platform::setTestAdcMv(15, 1800); mm::platform::setTestAdcMv(16, 2000);
    mm::BatteryChannel first, second; configure(first, 1, 15); configure(second, 2, 16);
    mm::BatteryService service; service.pollSeconds = 1;
    service.addChild(&first); service.addChild(&second); service.applyState(); drain(service);
    second.adcPin = 15; second.onControlChanged("adcPin");
    mm::platform::setTestNowMs(2500); service.tick1s();
    CHECK_FALSE(service.samplePending());
    CHECK(service.severity() == mm::MoonModule::Severity::Warning);
    CHECK_FALSE(second.reading().fresh(2500));
    service.release();
}

TEST_CASE("battery service: a shared sense gate requires matching polarity") {
    RestoreBatteryInputs restore; mm::platform::setTestNowMs(1000);
    mm::platform::setTestAdcMv(15, 1800); mm::platform::setTestAdcMv(16, 2000);
    mm::BatteryChannel first, second; configure(first, 1, 15); configure(second, 2, 16);
    first.senseGate = second.senseGate = 2; first.settleMs = second.settleMs = 1;
    second.gateHigh = false;
    mm::BatteryService service; service.addChild(&first); service.addChild(&second); service.applyState();
    CHECK_FALSE(service.samplePending());
    CHECK(service.severity() == mm::MoonModule::Severity::Warning);
    second.gateHigh = true;
    service.applyState(); drain(service);
    uint16_t mv = 0;
    REQUIRE(first.reading().lastVoltage(mv)); CHECK(mv == 3600);
    REQUIRE(second.reading().lastVoltage(mv)); CHECK(mv == 4000);
    CHECK_FALSE(mm::platform::gpioRead(2));
    service.release();
}

TEST_CASE("battery service: active-low sense gate is inactive after sampling and release") {
    RestoreBatteryInputs restore; mm::platform::setTestNowMs(1000);
    mm::platform::setTestAdcMv(1, 1800);
    mm::BatteryChannel channel; configure(channel, 1, 1);
    channel.senseGate = 2; channel.gateHigh = false; channel.settleMs = 1;
    mm::BatteryService service; service.addChild(&channel); service.applyState(); drain(service);
    uint16_t mv = 0; REQUIRE(channel.reading().lastVoltage(mv)); CHECK(mv == 3600);
    CHECK(mm::platform::gpioRead(2));
    service.release(); CHECK(mm::platform::gpioRead(2));
}

TEST_CASE("battery service: changing the ADC source invalidates the previous pack reading") {
    RestoreBatteryInputs restore; mm::platform::setTestNowMs(1000);
    mm::BatteryChannel channel; configure(channel, 1, 15); channel.prepare();
    channel.acceptAdc(1800, 1000);
    REQUIRE(channel.reading().fresh(1000));
    channel.adcPin = 16; channel.prepare();
    CHECK_FALSE(channel.reading().fresh(1000));
    uint8_t percent = 77; CHECK_FALSE(channel.reading().percent(1000, percent)); CHECK(percent == 77);
}

TEST_CASE("battery service: pre-prepare discovery declares a second-only pack without ADC access") {
    RestoreBatteryInputs restore; mm::platform::setTestGpioLevel(2, true);
    auto& channel = *new mm::BatteryChannel(); configure(channel, 2, -1); channel.presence = 2;
    auto& service = *new mm::BatteryService(); service.setTypeName("BatteryService"); service.addChild(&channel);
    mm::Scheduler scheduler; scheduler.addModule(&service);
    CHECK(mm::BatteryService::discover(scheduler) == &service);
    CHECK((service.batteryConfig() == mm::platform::BleBatteryConfig{1, {2, 0}}));
    CHECK_FALSE(service.samplePending()); CHECK(mm::platform::gpioRead(2));
    channel.prepare(); const auto snapshot = service.snapshot(mm::platform::millis());
    uint8_t level = 77; REQUIRE(snapshot.slots[1].readLevel(level)); CHECK(level == 0);
    CHECK_FALSE(snapshot.slots[0].readLevel(level));
    scheduler.release();
}

TEST_CASE("battery service: an in-flight sample cannot cross into a reconfigured ADC source") {
    RestoreBatteryInputs restore; mm::platform::setTestNowMs(1000);
    mm::platform::setTestAdcMv(15, 1800); mm::platform::setTestAdcMv(16, 2000);
    mm::BatteryChannel channel; configure(channel, 1, 15);
    mm::BatteryService service; service.addChild(&channel); service.applyState();
    channel.adcPin = 16; channel.onControlChanged("adcPin");
    drain(service); CHECK_FALSE(channel.reading().fresh(1000));
    service.applyState(); drain(service);
    uint16_t mv = 0; REQUIRE(channel.reading().lastVoltage(mv)); CHECK(mv == 4000);
    service.release();
}

TEST_CASE("battery service: stopping a settling worker leaves the divider disabled") {
    RestoreBatteryInputs restore;
    mm::platform::setTestAdcMv(1, 900);
    mm::BatteryChannel channel; configure(channel, 1, 1); channel.senseGate = 2; channel.settleMs = 2000;
    mm::BatteryService service; service.addChild(&channel); service.applyState();
    channel.acceptAdc(1800, mm::platform::millis());
    REQUIRE(service.snapshot(mm::platform::millis()).slots[0].levelValid);
    REQUIRE(service.samplePending());
    service.release(); CHECK_FALSE(service.samplePending()); CHECK_FALSE(mm::platform::gpioRead(2));
}

TEST_CASE("battery service: changing a channel stops a settling worker before reprepare") {
    RestoreBatteryInputs restore;
    mm::platform::setTestAdcMv(1, 900);
    mm::BatteryChannel channel; configure(channel, 1, 1); channel.setName("Cell"); channel.senseGate = 2; channel.settleMs = 2000;
    mm::BatteryService service; service.setTypeName("BatteryService"); service.addChild(&channel); service.defineControls();
    mm::Scheduler scheduler; scheduler.addModule(&service); service.applyState();
    for (int i = 0; i < 500 && !mm::platform::gpioRead(2); ++i) mm::platform::delayMs(2);
    REQUIRE(mm::platform::gpioRead(2)); REQUIRE(service.samplePending());
    CHECK(scheduler.setControl("Cell", "adcPin", "{\"value\":3}") == mm::Scheduler::SetControlResult::Ok);
    CHECK_FALSE(service.samplePending()); CHECK_FALSE(mm::platform::gpioRead(2));
    CHECK_FALSE(channel.reading().fresh(mm::platform::millis()));
    service.release(); CHECK_FALSE(mm::platform::gpioRead(2));
}

TEST_CASE("battery service: failed gate-off is retained and retried before restart") {
    struct Reset { ~Reset() { mm::platform::clearTestGpioCapability(); mm::platform::clearTestGpioLevel(); mm::platform::clearTestAdcValue(); } } reset;
    mm::platform::setTestAdcMv(1, 900);
    mm::BatteryChannel channel; configure(channel, 1, 1); channel.senseGate = 2; channel.settleMs = 2000;
    mm::BatteryService service; service.addChild(&channel); service.applyState();
    for (int i = 0; i < 500 && !mm::platform::gpioRead(2); ++i) mm::platform::delayMs(2);
    REQUIRE(mm::platform::gpioRead(2)); REQUIRE(service.samplePending());
    mm::platform::GpioCapability denied; denied.reserved = true; mm::platform::setTestGpioCapability(2, denied);
    service.quiesce();
    CHECK(service.blocksAncestorRemoval()); CHECK_FALSE(service.samplePending());
    CHECK_FALSE(service.snapshot(mm::platform::millis()).slots[0].levelValid);
    CHECK(service.severity() == mm::MoonModule::Severity::Error); CHECK(mm::platform::gpioRead(2));
    service.applyState(); // must not launch a replacement worker while gate-off fails
    CHECK(service.blocksAncestorRemoval()); CHECK_FALSE(service.samplePending()); CHECK(mm::platform::gpioRead(2));
    mm::platform::clearTestGpioCapability(); service.shutdownStep(mm::platform::millis());
    CHECK_FALSE(service.blocksAncestorRemoval()); CHECK_FALSE(mm::platform::gpioRead(2));
    service.release();
}

TEST_CASE("battery service: measurement gate-off failure is surfaced without a valid sample") {
    struct Reset { ~Reset() { mm::platform::clearTestGpioCapability(); mm::platform::clearTestGpioLevel(); mm::platform::clearTestAdcValue(); } } reset;
    mm::platform::setTestAdcMv(1, 1800);
    mm::BatteryChannel channel; configure(channel, 1, 1); channel.senseGate = 2; channel.settleMs = 100;
    mm::BatteryChannel other; configure(other, 2, 3);
    mm::BatteryService service; service.addChild(&channel); service.addChild(&other); service.applyState();
    channel.acceptAdc(1800, mm::platform::millis());
    other.acceptAdc(1800, mm::platform::millis());
    REQUIRE(channel.reading().fresh(mm::platform::millis()));
    REQUIRE(service.snapshot(mm::platform::millis()).slots[0].levelValid);
    for (int i = 0; i < 500 && !mm::platform::gpioRead(2); ++i) mm::platform::delayMs(2);
    REQUIRE(mm::platform::gpioRead(2));
    mm::platform::GpioCapability denied; denied.reserved = true; mm::platform::setTestGpioCapability(2, denied);
    for (int i = 0; i < 500 && !service.blocksAncestorRemoval(); ++i) { mm::platform::delayMs(2); service.tick20ms(); service.prepare(); }
    CHECK(service.blocksAncestorRemoval()); CHECK(service.severity() == mm::MoonModule::Severity::Error);
    CHECK_FALSE(channel.reading().fresh(mm::platform::millis()));
    CHECK_FALSE(service.snapshot(mm::platform::millis()).slots[0].levelValid);
    CHECK(service.snapshot(mm::platform::millis()).slots[1].levelValid);
    mm::platform::clearTestGpioCapability(); service.prepare();
    CHECK_FALSE(service.blocksAncestorRemoval()); REQUIRE(service.samplePending());
    drain(service);
    uint16_t measured = 0; REQUIRE(channel.reading().lastVoltage(measured)); CHECK(measured == 3600);
    CHECK_FALSE(mm::platform::gpioRead(2));
    service.release();
}

TEST_CASE("battery service: stopping a failed shared sense gate invalidates both channels") {
    struct Reset { ~Reset() { mm::platform::clearTestGpioCapability(); mm::platform::clearTestGpioLevel(); mm::platform::clearTestAdcValue(); } } reset;
    mm::platform::setTestAdcMv(1, 1800); mm::platform::setTestAdcMv(3, 1900);
    mm::BatteryChannel first, second; configure(first, 1, 1); configure(second, 2, 3);
    first.senseGate = second.senseGate = 2; first.settleMs = second.settleMs = 100;
    mm::BatteryService service; service.addChild(&first); service.addChild(&second); service.applyState();
    first.acceptAdc(1800, mm::platform::millis()); second.acceptAdc(1900, mm::platform::millis());
    REQUIRE(service.snapshot(mm::platform::millis()).slots[0].levelValid);
    REQUIRE(service.snapshot(mm::platform::millis()).slots[1].levelValid);
    for (int i = 0; i < 500 && !mm::platform::gpioRead(2); ++i) mm::platform::delayMs(2);
    REQUIRE(mm::platform::gpioRead(2));
    mm::platform::GpioCapability denied; denied.reserved = true; mm::platform::setTestGpioCapability(2, denied);
    service.quiesce();
    CHECK(service.blocksAncestorRemoval());
    CHECK_FALSE(service.snapshot(mm::platform::millis()).slots[0].levelValid);
    CHECK_FALSE(service.snapshot(mm::platform::millis()).slots[1].levelValid);
    mm::platform::clearTestGpioCapability(); service.prepare(); drain(service);
    CHECK(service.snapshot(mm::platform::millis()).slots[0].levelValid);
    CHECK(service.snapshot(mm::platform::millis()).slots[1].levelValid);
    service.release();
}

TEST_CASE("battery service: a failed shared sense gate invalidates both channels") {
    struct Reset { ~Reset() { mm::platform::clearTestGpioCapability(); mm::platform::clearTestGpioLevel(); mm::platform::clearTestAdcValue(); } } reset;
    mm::platform::setTestAdcMv(1, 1800); mm::platform::setTestAdcMv(3, 1900);
    mm::BatteryChannel first, second; configure(first, 1, 1); configure(second, 2, 3);
    first.senseGate = second.senseGate = 2; first.settleMs = second.settleMs = 100;
    mm::BatteryService service; service.addChild(&first); service.addChild(&second); service.applyState();
    first.acceptAdc(1800, mm::platform::millis()); second.acceptAdc(1900, mm::platform::millis());
    REQUIRE(service.snapshot(mm::platform::millis()).slots[0].levelValid);
    REQUIRE(service.snapshot(mm::platform::millis()).slots[1].levelValid);
    for (int i = 0; i < 500 && !mm::platform::gpioRead(2); ++i) mm::platform::delayMs(2);
    REQUIRE(mm::platform::gpioRead(2));
    mm::platform::GpioCapability denied; denied.reserved = true; mm::platform::setTestGpioCapability(2, denied);
    for (int i = 0; i < 500 && first.reading().fresh(mm::platform::millis()); ++i) { mm::platform::delayMs(2); service.tick20ms(); }
    CHECK_FALSE(service.snapshot(mm::platform::millis()).slots[0].levelValid);
    CHECK_FALSE(service.snapshot(mm::platform::millis()).slots[1].levelValid);
    service.prepare(); REQUIRE(service.blocksAncestorRemoval());
    mm::platform::clearTestGpioCapability(); service.prepare(); drain(service);
    CHECK(service.snapshot(mm::platform::millis()).slots[0].levelValid);
    CHECK(service.snapshot(mm::platform::millis()).slots[1].levelValid);
    service.release();
}

TEST_CASE("battery service: scheduler retries a failed sampling gate-off before resuming") {
    struct Reset { ~Reset() { mm::platform::clearTestGpioCapability(); mm::platform::clearTestGpioLevel(); mm::platform::clearTestAdcValue(); } } reset;
    mm::platform::setTestAdcMv(1, 1800);
    mm::Scheduler scheduler;
    auto* service = new mm::BatteryService(); service->setName("Battery"); service->setTypeName("BatteryService");
    auto* channel = new mm::BatteryChannel(); configure(*channel, 1, 1); channel->senseGate = 2; channel->settleMs = 100;
    REQUIRE(service->addChild(channel)); scheduler.addModule(service); scheduler.setup();
    for (int i = 0; i < 500 && !mm::platform::gpioRead(2); ++i) mm::platform::delayMs(2);
    REQUIRE(mm::platform::gpioRead(2));
    mm::platform::GpioCapability denied; denied.reserved = true; mm::platform::setTestGpioCapability(2, denied);
    for (int i = 0; i < 500 && !scheduler.preparationPending(); ++i) { mm::platform::delayMs(2); service->tick20ms(); }
    REQUIRE(scheduler.preparationPending()); scheduler.preparePending();
    CHECK(service->blocksAncestorRemoval()); CHECK_FALSE(scheduler.release());
    CHECK_FALSE(channel->reading().fresh(mm::platform::millis()));
    mm::platform::clearTestGpioCapability(); service->tick1s();
    REQUIRE(scheduler.preparationPending()); scheduler.preparePending();
    CHECK_FALSE(service->blocksAncestorRemoval()); REQUIRE(service->samplePending());
    drain(*service);
    uint16_t measured = 0; REQUIRE(channel->reading().lastVoltage(measured)); CHECK(measured == 3600);
    CHECK_FALSE(mm::platform::gpioRead(2)); CHECK(scheduler.release());
}

TEST_CASE("battery service: scheduler retains a tree when live shutdown cannot turn the gate off") {
    struct Reset { ~Reset() { mm::platform::clearTestGpioCapability(); mm::platform::clearTestGpioLevel(); mm::platform::clearTestAdcValue(); } } reset;
    mm::platform::setTestAdcMv(1, 1800);
    mm::Scheduler scheduler;
    auto* service = new mm::BatteryService(); service->setName("Battery"); service->setTypeName("BatteryService");
    auto* channel = new mm::BatteryChannel(); configure(*channel, 1, 1); channel->setName("Cell"); channel->senseGate = 2; channel->settleMs = 2000;
    REQUIRE(service->addChild(channel)); scheduler.addModule(service); scheduler.setup();
    for (int i = 0; i < 500 && !mm::platform::gpioRead(2); ++i) mm::platform::delayMs(2);
    REQUIRE(mm::platform::gpioRead(2));
    mm::platform::GpioCapability denied; denied.reserved = true; mm::platform::setTestGpioCapability(2, denied);
    CHECK_FALSE(scheduler.release());
    CHECK(scheduler.firstByName("Battery") == service); CHECK(scheduler.firstByName("Cell") == channel);
    CHECK(service->blocksAncestorRemoval()); CHECK(mm::platform::gpioRead(2));
    CHECK(scheduler.pinReservedBy(service, 1)); CHECK(scheduler.pinReservedBy(service, 2));
    mm::platform::clearTestGpioCapability(); scheduler.shutdownStep(mm::platform::millis());
    CHECK_FALSE(service->blocksAncestorRemoval()); CHECK_FALSE(mm::platform::gpioRead(2));
    CHECK(scheduler.release()); CHECK(scheduler.firstByName("Battery") == nullptr);
}

TEST_CASE("battery service: declared pin conflicts are refused and held pins block other controls") {
    struct Reset { ~Reset() { mm::platform::clearTestGpioLevel(); mm::platform::clearTestAdcValue(); } } reset;
    struct Pins final : mm::MoonModule {
        int8_t gpio = 2;
        void defineControls() override { controls_.addPin("gpio", gpio); }
    };
    mm::platform::setTestAdcMv(1, 1800); mm::platform::setTestAdcMv(3, 1800);
    mm::Scheduler scheduler;
    auto* other = new Pins(); other->setName("Other"); scheduler.addModule(other);
    auto* service = new mm::BatteryService(); service->setName("Battery"); service->setTypeName("BatteryService");
    auto* channel = new mm::BatteryChannel(); configure(*channel, 1, 1); channel->setName("Cell"); channel->senseGate = 2; channel->settleMs = 10;
    REQUIRE(service->addChild(channel)); scheduler.addModule(service); scheduler.setup();
    auto configuration = [&]() -> const char* {
        for (uint8_t i = 0; i < service->controls().count(); ++i) {
            const auto& control = service->controls()[i];
            if (std::strcmp(control.name, "configuration") == 0)
                return static_cast<const char*>(control.ptr);
        }
        return "missing";
    };
    CHECK_FALSE(service->samplePending()); CHECK(service->severity() == mm::MoonModule::Severity::Error);
    CHECK_FALSE(mm::platform::gpioRead(2));
    // A previously valid sample must disappear immediately when re-preparing
    // with conflicting pin ownership, rather than looking fresh until timeout.
    channel->configureReading(); channel->acceptAdc(1800, mm::platform::millis());
    uint8_t percentage = 0; REQUIRE(channel->reading().percent(mm::platform::millis(), percentage));
    scheduler.prepareSubtree(service);
    CHECK_FALSE(channel->reading().percent(mm::platform::millis(), percentage));
    CHECK(document(*service).find("\"percent\":null") != std::string::npos);
    CHECK_FALSE(service->snapshot(mm::platform::millis()).slots[0].levelValid);
    CHECK(document(*service).find("\"error\":\"invalid-configuration\"") != std::string::npos);
    service->tick1s();
    CHECK(service->severity() == mm::MoonModule::Severity::Error);
    CHECK(std::strcmp(configuration(), "invalid") == 0);
    REQUIRE(scheduler.setControl("Other", "gpio", "{\"value\":5}") == mm::Scheduler::SetControlResult::Ok);
    scheduler.prepareSubtree(service); REQUIRE(service->samplePending());
    CHECK(document(*service).find("\"invalid-configuration\"") == std::string::npos);
    CHECK(std::strcmp(configuration(), "ok") == 0);
    CHECK(scheduler.pinReservedBy(service, 1)); CHECK(scheduler.pinReservedBy(service, 2));
    CHECK(scheduler.setControl("Other", "gpio", "{\"value\":2}") == mm::Scheduler::SetControlResult::ReadOnly);
    CHECK(other->gpio == 5);
    REQUIRE(scheduler.setControl("Cell", "adcPin", "{\"value\":3}") == mm::Scheduler::SetControlResult::Ok);
    scheduler.preparePending(); REQUIRE(service->samplePending());
    CHECK(scheduler.pinReservedBy(service, 3)); CHECK(scheduler.pinReservedBy(service, 2));
    CHECK_FALSE(scheduler.pinReservedBy(service, 1));
    drain(*service); CHECK(scheduler.release());
}

TEST_CASE("battery service: pin lists and fixed claims are checked even on disabled modules") {
    struct Reset { ~Reset() { mm::platform::clearTestGpioLevel(); mm::platform::clearTestAdcValue(); } } reset;
    struct Claims final : mm::MoonModule {
        char pins[24] = {}; bool fixed = false;
        void defineControls() override { controls_.addText("lanePins", pins, sizeof(pins)); }
        uint8_t fixedPins(FixedPin* out, uint8_t max) const override {
            if (!fixed || !max) return 0;
            out[0] = {2, "test"}; return 1;
        }
    };
    for (int kind = 0; kind < 3; ++kind) {
        mm::platform::setTestGpioLevel(2, false);
        mm::Scheduler scheduler;
        auto* other = new Claims(); other->setName("Claims"); other->setEnabled(false);
        if (kind == 0) std::strcpy(other->pins, "4-6,2");
        if (kind == 1) other->fixed = true;
        if (kind == 2) std::strcpy(other->pins, "bad");
        scheduler.addModule(other);
        auto* service = new mm::BatteryService(); service->setName("Battery"); service->setTypeName("BatteryService");
        auto* channel = new mm::BatteryChannel(); configure(*channel, 1, 1); channel->senseGate = 2; channel->settleMs = 10;
        REQUIRE(service->addChild(channel)); scheduler.addModule(service); scheduler.setup();
        CHECK_FALSE(service->samplePending()); CHECK(service->severity() == mm::MoonModule::Severity::Error);
        CHECK_FALSE(mm::platform::gpioRead(2)); CHECK(scheduler.release());
    }
}

TEST_CASE("battery service: live removal and disable retain the tree on gate-off failure") {
    struct Reset { ~Reset() { mm::platform::clearTestGpioCapability(); mm::platform::clearTestGpioLevel(); mm::platform::clearTestAdcValue(); } } reset;
    for (int action = 0; action < 4; ++action) {
        mm::platform::setTestAdcMv(1, 1800); mm::platform::setTestGpioLevel(2, false);
        mm::Scheduler scheduler;
        auto* root = new mm::MoonModule(); root->setName("Root");
        auto* service = new mm::BatteryService(); service->setName("Battery"); service->setTypeName("BatteryService");
        auto* channel = new mm::BatteryChannel(); configure(*channel, 1, 1); channel->setName("Cell"); channel->senseGate = 2; channel->settleMs = 2000;
        REQUIRE(service->addChild(channel)); REQUIRE(root->addChild(service)); scheduler.addModule(root); scheduler.setup();
        for (int i = 0; i < 500 && !mm::platform::gpioRead(2); ++i) mm::platform::delayMs(2);
        REQUIRE(mm::platform::gpioRead(2));
        mm::platform::GpioCapability denied; denied.reserved = true; mm::platform::setTestGpioCapability(2, denied);
        if (action == 0) {
            const auto result = mm::applyStateDocument(scheduler, R"({"Root":{"Battery":null}})", mm::StateSource::Request);
            CHECK_FALSE(result.ok);
        } else if (action == 1) {
            const auto result = mm::applyStateDocument(scheduler, R"({"Root":{"Battery":{"Cell":null}}})", mm::StateSource::Request);
            CHECK_FALSE(result.ok);
        } else if (action == 2) {
            const auto result = mm::applyStateDocument(scheduler, R"({"Root":{"Battery":{"enabled":false}}})", mm::StateSource::Request);
            CHECK_FALSE(result.ok);
        } else {
            CHECK(scheduler.setControl("Cell", "enabled", "{\"value\":false}") == mm::Scheduler::SetControlResult::ReadOnly);
        }
        CHECK(scheduler.firstByName("Battery") == service); CHECK(scheduler.firstByName("Cell") == channel);
        CHECK(service->enabledSetting()); CHECK(channel->enabledSetting()); CHECK(service->blocksAncestorRemoval());
        mm::platform::clearTestGpioCapability(); scheduler.shutdownStep(mm::platform::millis());
        CHECK_FALSE(mm::platform::gpioRead(2)); CHECK(scheduler.release());
    }
}

TEST_CASE("battery service: failed gate-off prevents changing live battery pins") {
    struct Reset { ~Reset() { mm::platform::clearTestGpioCapability(); mm::platform::clearTestGpioLevel(); mm::platform::clearTestAdcValue(); } } reset;
    for (int action = 0; action < 2; ++action) {
        mm::platform::setTestAdcMv(1, 1800); mm::platform::setTestGpioLevel(2, false);
        mm::Scheduler scheduler;
        auto* service = new mm::BatteryService(); service->setName("Battery"); service->setTypeName("BatteryService");
        auto* channel = new mm::BatteryChannel(); configure(*channel, 1, 1); channel->setName("Cell"); channel->senseGate = 2; channel->settleMs = 2000;
        REQUIRE(service->addChild(channel)); scheduler.addModule(service); scheduler.setup();
        for (int i = 0; i < 500 && !mm::platform::gpioRead(2); ++i) mm::platform::delayMs(2);
        REQUIRE(mm::platform::gpioRead(2));
        mm::platform::GpioCapability denied; denied.reserved = true; mm::platform::setTestGpioCapability(2, denied);
        if (action == 0) {
            CHECK(scheduler.setControl("Cell", "senseGate", "{\"value\":3}") == mm::Scheduler::SetControlResult::ReadOnly);
        } else {
            const auto result = mm::applyStateDocument(scheduler, R"({"Battery":{"Cell":{"adcPin":3}}})", mm::StateSource::Request);
            CHECK_FALSE(result.ok);
        }
        CHECK(channel->adcPin == 1); CHECK(channel->senseGate == 2);
        CHECK(service->blocksAncestorRemoval()); CHECK(mm::platform::gpioRead(2));
        mm::platform::clearTestGpioCapability(); scheduler.shutdownStep(mm::platform::millis());
        CHECK_FALSE(mm::platform::gpioRead(2)); CHECK(scheduler.release());
    }
}

TEST_CASE("battery service: an unchanged document restarts a quiesced sampler") {
    RestoreBatteryInputs restore; mm::platform::setTestAdcMv(1, 1800);
    mm::Scheduler scheduler;
    auto* service = new mm::BatteryService(); service->setName("Battery"); service->setTypeName("BatteryService");
    auto* channel = new mm::BatteryChannel(); configure(*channel, 1, 1); channel->setName("Cell");
    REQUIRE(service->addChild(channel)); scheduler.addModule(service); scheduler.setup(); drain(*service);
    const auto result = mm::applyStateDocument(scheduler, R"({"Battery":{"Cell":{"adcPin":1}}})", mm::StateSource::Request);
    REQUIRE(result.ok); scheduler.preparePending();
    REQUIRE(service->samplePending()); drain(*service);
    uint16_t measured = 0; REQUIRE(channel->reading().lastVoltage(measured)); CHECK(measured == 3600);
    CHECK(scheduler.release());
}

TEST_CASE("battery gate: a late worker cannot disable a new owner's divider") {
    mm::BatteryGateGuard gate;
    std::atomic<bool> output{false}, ready{false}, resume{false}, lateWrite{false};
    std::thread worker([&] {
        gate.whileActive([&] { output.store(true); return true; });
        ready.store(true); ready.notify_one();
        resume.wait(false);
        lateWrite.store(gate.whileActive([&] { output.store(false); return true; }));
    });
    ready.wait(false);
    gate.stop([&] { output.store(false); });
    output.store(true); // a replacement owner has acquired the same pin
    resume.store(true); resume.notify_one();
    worker.join();
    CHECK_FALSE(lateWrite.load());
    CHECK(output.load());
}
