/// @module MiniLedPowerSequence
#include "doctest.h"
#include "light/drivers/MiniLedPowerSequence.h"
#include "light/drivers/Mini80PowerPort.h"
#include "light/drivers/Mini80PowerController.h"
#include "light/drivers/Mini80PinPreflight.h"
#include "light/drivers/RmtLedDriver.h"
#include "light/layers/Buffer.h"
#include "core/module/StateDocument.h"
#include "correction_presets.h"
#include <cstring>
#include <string>

namespace {
using Sequence = mm::MiniLedPowerSequence;

/// Real desktop RMT driver, with a simulated rail and GPIO handover. The
/// fixture records commands; it cannot prove an electrical pin level.
struct MiniPort : Sequence::Port {
    mm::RmtLedDriver driver;
    mm::Buffer source;
    mm::Correction correction;
    std::string calls;
    bool rail = false;
    bool failHold = false, failPreload = false, failRailOn = false, failRailOff = false, failAttach = false;

    MiniPort() {
        mm::platform::setTestRmtFailures(false, false);
        mm::platform::setTestRmtFailurePin(-1);
        REQUIRE(source.allocate(52, 4));
        std::memset(source.data(), 255, 52 * 4);
        // Four channels are an illustrative RGBW fixture, not a board-order claim.
        mm::test::rebuildFromPreset(correction, 255, mm::test::PresetOrder::RGBW);
        std::strcpy(driver.pins, "16");
        std::strcpy(driver.ledsPerPin, "52");
        driver.defineControls();
    }
    ~MiniPort() override {
        mm::platform::setTestRmtFailures(false, false);
        mm::platform::setTestRmtFailurePin(-1);
        mm::platform::setTestRmtDetachFailure(false);
        driver.release();
    }
    void holdVisible() override { calls += 'H'; driver.requestBlackFrame(); }
    bool detachAndHoldLow() override {
        calls += 'D';
        if (failHold) return false;
        // The RMT detach is real desktop driver logic; GPIO low is simulated.
        return driver.detachForPower();
    }
    bool preloadRailLow() override { calls += 'P'; return !failPreload; }
    bool setRail(bool enabled) override {
        calls += enabled ? '+' : '-';
        if ((enabled && failRailOn) || (!enabled && failRailOff)) return false;
        rail = enabled;
        return true;
    }
    bool attachOutput() override {
        calls += 'A';
        if (failAttach) return false;
        driver.setSourceBuffer(&source);
        driver.correctionForTest() = correction;
        return driver.attachForPower();
    }
    void requestBlack() override { calls += 'B'; driver.requestBlackFrame(); }
    Sequence::Black black() const override {
        const auto status = driver.blackFrameStatus();
        if (status == mm::RmtLedDriver::BlackFrameStatus::Latched) return Sequence::Black::Latched;
        if (status == mm::RmtLedDriver::BlackFrameStatus::Pending) return Sequence::Black::Pending;
        return Sequence::Black::Failed;
    }
    bool healthy() const override { return true; }
    bool releaseVisible() override { calls += 'V'; return driver.releaseBlackFrame(); }
};
} // namespace

TEST_CASE("mini power refuses unqualified timings without driving pins") {
    MiniPort port;
    Sequence seq;
    CHECK_FALSE(seq.configure(0, 100));
    seq.tick(port, true, 10);
    CHECK(seq.state() == Sequence::State::Off);
    CHECK(seq.error() == Sequence::Error::Unqualified);
    CHECK(port.calls.empty());
    CHECK_FALSE(port.rail);
    CHECK_FALSE(seq.configure(10, 0));
}

TEST_CASE("mini power lifecycle shutdown holds DIN and turns the rail off without another frame") {
    MiniPort port;
    Sequence seq;
    REQUIRE(seq.configure(2, 100));
    seq.tick(port, true, 10);
    REQUIRE(seq.state() == Sequence::State::Settling);
    REQUIRE(port.rail);
    const auto before = port.calls.size();
    REQUIRE(seq.shutdown(port));
    CHECK(port.calls.substr(before) == "HD-");
    CHECK_FALSE(port.rail);
    CHECK(seq.state() == Sequence::State::Off);
    CHECK(seq.safeOffAcknowledged());
}

TEST_CASE("mini power lifecycle shutdown reports a failed hold and permits a later retry") {
    MiniPort port;
    Sequence seq;
    REQUIRE(seq.configure(2, 100));
    seq.tick(port, true, 10);
    REQUIRE(port.rail);
    port.failHold = true;
    CHECK_FALSE(seq.shutdown(port));
    CHECK_FALSE(seq.safeOffAcknowledged());
    CHECK(seq.state() == Sequence::State::Fault);
    CHECK(seq.error() == Sequence::Error::Hold);
    CHECK(port.rail);
    port.failHold = false;
    REQUIRE(seq.shutdown(port));
    CHECK_FALSE(port.rail);
    CHECK(seq.state() == Sequence::State::Off);
}

TEST_CASE("mini80 port uses the resolved 52 RGBW output for power sequencing") {
    mm::RmtLedDriver driver;
    mm::Buffer source;
    mm::Correction correction;
    REQUIRE(source.allocate(52, 4));
    mm::test::rebuildFromPreset(correction, 255, mm::test::PresetOrder::RGBW);
    std::strcpy(driver.pins, "47");
    std::strcpy(driver.ledsPerPin, "52");
    driver.defineControls();
    driver.setSourceBuffer(&source);
    driver.correctionForTest() = correction;
    driver.applyState();
    mm::Mini80PowerPort port(driver);
    CHECK_FALSE(port.preloadRailLow());
    CHECK_FALSE(port.setRail(true));
    CHECK_FALSE(port.attachOutput());
    Sequence seq;
    REQUIRE(seq.configure(2, 100));
    mm::platform::setTestGpioLevel(17, false);
    mm::platform::setTestGpioLevel(47, true);
    seq.tick(port, true, 0);
    CHECK(seq.state() == Sequence::State::Settling);
    CHECK(mm::platform::gpioRead(17));
    CHECK_FALSE(mm::platform::gpioRead(47));
    seq.tick(port, true, 2);
    driver.tick();
    seq.tick(port, true, 3);
    REQUIRE(seq.state() == Sequence::State::On);
    seq.tick(port, false, 4);
    driver.tick();
    seq.tick(port, false, 5);
    CHECK(seq.state() == Sequence::State::Off);
    CHECK(seq.safeOffAcknowledged());
    CHECK_FALSE(mm::platform::gpioRead(17));
    CHECK_FALSE(mm::platform::gpioRead(47));
    driver.release();
    mm::platform::clearTestGpioLevel();
}

TEST_CASE("mini80 port detects changed wiring while settling and holds the original output low") {
    mm::RmtLedDriver driver;
    mm::Buffer source;
    mm::Correction correction;
    REQUIRE(source.allocate(52, 4));
    mm::test::rebuildFromPreset(correction, 255, mm::test::PresetOrder::RGBW);
    std::strcpy(driver.pins, "47");
    std::strcpy(driver.ledsPerPin, "52");
    driver.defineControls();
    driver.setSourceBuffer(&source);
    driver.correctionForTest() = correction;
    driver.applyState();
    mm::Mini80PowerPort port(driver);
    Sequence seq;
    REQUIRE(seq.configure(2, 100));
    seq.tick(port, true, 0);
    REQUIRE(seq.state() == Sequence::State::Settling);
    std::strcpy(driver.pins, "16"); // emulate a changed text control before prepare
    seq.tick(port, true, 2);
    CHECK(seq.state() == Sequence::State::Fault);
    CHECK(seq.safeOffAcknowledged());
    CHECK_FALSE(mm::platform::gpioRead(17));
    CHECK_FALSE(mm::platform::gpioRead(47));
    driver.release();
    mm::platform::clearTestGpioLevel();
}

TEST_CASE("mini80 port can cut power safely after the source disappears") {
    mm::RmtLedDriver driver;
    mm::Buffer source;
    mm::Correction correction;
    REQUIRE(source.allocate(52, 4));
    mm::test::rebuildFromPreset(correction, 255, mm::test::PresetOrder::RGBW);
    std::strcpy(driver.pins, "47");
    std::strcpy(driver.ledsPerPin, "52");
    driver.defineControls();
    driver.setSourceBuffer(&source);
    driver.correctionForTest() = correction;
    driver.applyState();
    mm::Mini80PowerPort port(driver);
    Sequence seq;
    REQUIRE(seq.configure(2, 100));
    seq.tick(port, true, 0);
    seq.tick(port, true, 2);
    driver.tick();
    seq.tick(port, true, 3);
    REQUIRE(seq.state() == Sequence::State::On);
    driver.setSourceBuffer(nullptr);
    seq.tick(port, true, 4);
    CHECK(seq.state() == Sequence::State::Fault);
    CHECK(seq.error() == Sequence::Error::Hold);
    CHECK(seq.safeOffAcknowledged());
    CHECK_FALSE(mm::platform::gpioRead(17));
    CHECK_FALSE(mm::platform::gpioRead(47));
    driver.release();
    mm::platform::clearTestGpioLevel();
}

TEST_CASE("mini80 port refuses a different output pin before enabling power") {
    mm::RmtLedDriver driver;
    mm::Buffer source;
    mm::Correction correction;
    REQUIRE(source.allocate(52, 4));
    mm::test::rebuildFromPreset(correction, 255, mm::test::PresetOrder::RGBW);
    std::strcpy(driver.pins, "16");
    std::strcpy(driver.ledsPerPin, "52");
    driver.defineControls();
    driver.setSourceBuffer(&source);
    driver.correctionForTest() = correction;
    driver.applyState();
    mm::Mini80PowerPort port(driver);
    Sequence seq;
    REQUIRE(seq.configure(2, 100));
    mm::platform::setTestGpioLevel(17, false);
    seq.tick(port, true, 0);
    CHECK(seq.state() == Sequence::State::Fault);
    CHECK_FALSE(seq.safeOffAcknowledged());
    CHECK_FALSE(mm::platform::gpioRead(17));
    driver.release();
    mm::platform::clearTestGpioLevel();
}

TEST_CASE("mini power holds DIN low, waits, latches black, then permits output") {
    MiniPort port;
    Sequence seq;
    REQUIRE(seq.configure(10, 100));
    seq.tick(port, true, 100);
    CHECK(port.calls == "HDP+");
    CHECK(port.rail);
    CHECK(seq.state() == Sequence::State::Settling);
    seq.tick(port, true, 109);
    CHECK(port.calls == "HDP+");
    seq.tick(port, true, 110);
    CHECK(port.calls == "HDP+AB");
    CHECK(seq.state() == Sequence::State::StartingBlack);
    seq.tick(port, true, 111);
    CHECK(seq.state() == Sequence::State::StartingBlack);
    port.driver.tick();
    REQUIRE(port.driver.blackFrameLatched());
    for (size_t i = 0; i < 52 * 4; ++i) CHECK(port.driver.frameBuffer()[i] == 0);
    seq.tick(port, true, 112);
    CHECK(port.calls == "HDP+ABV");
    CHECK(seq.state() == Sequence::State::On);
    port.driver.tick();
    CHECK(port.driver.frameBuffer()[0] != 0);
}

TEST_CASE("mini power waits for shutdown black and restarts from the beginning") {
    MiniPort port;
    Sequence seq;
    REQUIRE(seq.configure(2, 100));
    seq.tick(port, true, 0);
    seq.tick(port, true, 2);
    port.driver.tick();
    seq.tick(port, true, 3);
    REQUIRE(seq.state() == Sequence::State::On);
    seq.tick(port, false, 4);
    REQUIRE(seq.state() == Sequence::State::StoppingBlack);
    CHECK(port.rail);
    seq.tick(port, true, 5);
    CHECK(port.rail); // a renewed ON cannot bypass the pending shutdown frame
    port.driver.tick();
    seq.tick(port, true, 6);
    CHECK(seq.state() == Sequence::State::Off);
    CHECK_FALSE(port.rail);
    CHECK(seq.safeOffAcknowledged());
    const auto calls = port.calls;
    seq.tick(port, true, 7);
    CHECK(port.calls == calls + "HDP+");
    CHECK(seq.state() == Sequence::State::Settling);
}

TEST_CASE("mini power refuses visible output on a failed black transfer") {
    MiniPort port;
    Sequence seq;
    REQUIRE(seq.configure(1, 20));
    seq.tick(port, true, 0);
    seq.tick(port, true, 1);
    mm::platform::setTestRmtFailurePin(16);
    port.driver.tick();
    seq.tick(port, true, 2);
    CHECK(seq.state() == Sequence::State::Fault);
    CHECK(seq.error() == Sequence::Error::Black);
    CHECK_FALSE(port.rail);
    CHECK(seq.safeOffAcknowledged());
    CHECK(port.calls.find('V') == std::string::npos);
    mm::platform::setTestRmtFailurePin(-1);
    REQUIRE(seq.clearFault(port));
    CHECK(seq.state() == Sequence::State::Off);
    seq.tick(port, true, 3);
    CHECK(seq.state() == Sequence::State::Settling);
}

TEST_CASE("mini power timeout and failed low handover never acknowledge a safe off") {
    MiniPort port;
    Sequence seq;
    REQUIRE(seq.configure(1, 5));
    seq.tick(port, true, 0);
    seq.tick(port, true, 1);
    mm::platform::setTestRmtFailures(false, true);
    port.driver.tick();
    port.failHold = true;
    seq.tick(port, true, 6);
    CHECK(seq.state() == Sequence::State::Fault);
    CHECK(seq.error() == Sequence::Error::Timeout);
    CHECK_FALSE(seq.safeOffAcknowledged());
    CHECK(port.rail); // no rail-off claim while DIN could not be held low
    CHECK(port.calls.find('V') == std::string::npos);
    CHECK_FALSE(seq.clearFault(port));
    port.failHold = false;
    mm::platform::setTestRmtFailures(false, false);
    CHECK(seq.clearFault(port));
    CHECK_FALSE(port.rail);
}

TEST_CASE("mini power handles clock wrap and cancellation during settling") {
    MiniPort port;
    Sequence seq;
    REQUIRE(seq.configure(10, 20));
    seq.tick(port, true, 0xfffffffau);
    seq.tick(port, true, 3);
    CHECK(seq.state() == Sequence::State::Settling);
    seq.tick(port, true, 4);
    CHECK(seq.state() == Sequence::State::StartingBlack);
    seq.tick(port, false, 5);
    CHECK(seq.state() == Sequence::State::StoppingBlack);
    port.driver.tick();
    seq.tick(port, false, 6);
    CHECK(seq.state() == Sequence::State::Off);
    CHECK_FALSE(port.rail);
}

TEST_CASE("mini power cancels during rail settling without starting a data frame") {
    MiniPort port;
    Sequence seq;
    REQUIRE(seq.configure(10, 20));
    mm::platform::resetTestRmtTransmitCount();
    seq.tick(port, true, 100);
    REQUIRE(port.rail);
    seq.tick(port, false, 101);
    CHECK(seq.state() == Sequence::State::Off);
    CHECK(seq.safeOffAcknowledged());
    CHECK_FALSE(port.rail);
    CHECK(port.calls == "HDP+HD-");
    CHECK(mm::platform::testRmtTransmitCount() == 0);
}

TEST_CASE("mini power refuses an output-attach failure and acknowledges only a safe off") {
    MiniPort port;
    Sequence seq;
    REQUIRE(seq.configure(1, 20));
    seq.tick(port, true, 0);
    port.failAttach = true;
    seq.tick(port, true, 1);
    CHECK(seq.state() == Sequence::State::Fault);
    CHECK(seq.error() == Sequence::Error::Attach);
    CHECK(seq.safeOffAcknowledged());
    CHECK_FALSE(port.rail);
    CHECK(port.calls.find('V') == std::string::npos);
}

TEST_CASE("mini power preserves the rail when checked RMT detach fails") {
    MiniPort port;
    Sequence seq;
    REQUIRE(seq.configure(1, 20));
    seq.tick(port, true, 0);
    seq.tick(port, true, 1);
    port.driver.tick();
    seq.tick(port, true, 2);
    REQUIRE(seq.state() == Sequence::State::On);
    seq.tick(port, false, 3);
    port.driver.tick();
    REQUIRE(port.driver.blackFrameLatched());
    mm::platform::setTestRmtDetachFailure(true);
    seq.tick(port, false, 4);
    CHECK(seq.state() == Sequence::State::Fault);
    CHECK(seq.error() == Sequence::Error::Hold);
    CHECK_FALSE(seq.safeOffAcknowledged());
    CHECK(port.rail);
    CHECK_FALSE(seq.clearFault(port));
    mm::platform::setTestRmtDetachFailure(false);
    CHECK(seq.clearFault(port));
    CHECK_FALSE(port.rail);
}

struct MiniTree {
    mm::MoonModule owner;
    mm::Scheduler scheduler;
    MiniTree() { owner.setName("Mini"); scheduler.addModule(&owner); }
};

TEST_CASE("cooperative pin reservations are exclusive, atomic and owner scoped") {
    mm::PinReservations<3> pins; int a, b;
    const uint16_t first[] = {17, 47}, overlap[] = {5, 47}, other[] = {5}, altered[] = {17};
    REQUIRE(pins.reserve(&a, first, 2)); CHECK(pins.reserve(&a, first, 2));
    CHECK_FALSE(pins.reserve(&a, altered, 1)); CHECK_FALSE(pins.reserve(&b, overlap, 2));
    CHECK_FALSE(pins.heldBy(&b, 5)); CHECK(pins.heldBy(&a, 17)); CHECK(pins.heldBy(&a, 47));
    CHECK(pins.reserve(&b, other, 1)); CHECK_FALSE(pins.release(nullptr));
    REQUIRE(pins.release(&a)); CHECK_FALSE(pins.heldBy(&a, 17)); CHECK(pins.heldBy(&b, 5));
    CHECK_FALSE(pins.release(&a)); REQUIRE(pins.release(&b));
}

TEST_CASE("two MINI80 controllers with different drivers cannot reserve the same pins") {
    mm::RmtLedDriver first, second; MiniTree tree;
    mm::Mini80PowerController a(first), b(second);
    REQUIRE(a.configure(2, 100)); REQUIRE(b.configure(2, 100));
    REQUIRE(a.initializeCold(tree.scheduler, &tree.owner, ""));
    CHECK(tree.scheduler.pinReservedBy(&a, 17)); CHECK(tree.scheduler.pinReservedBy(&a, 47));
    CHECK_FALSE(b.initializeCold(tree.scheduler, &tree.owner, ""));
    CHECK(b.startupError() == mm::Mini80PowerController::StartupError::PinsReserved);
    CHECK_FALSE(second.powerOwnedBy(&b));
    REQUIRE(a.shutdown()); CHECK_FALSE(tree.scheduler.pinReservedBy(&a, 17));
    REQUIRE(b.initializeCold(tree.scheduler, &tree.owner, ""));
    REQUIRE(b.shutdown()); first.release(); second.release(); mm::platform::clearTestGpioLevel();
}

TEST_CASE("mini80 cold preflight accepts its own unstarted driver but not a second owner") {
    mm::RmtLedDriver driver;
    driver.setName("RmtMini"); std::strcpy(driver.pins, "47"); driver.defineControls();
    MiniTree tree; REQUIRE(tree.owner.addChild(&driver));
    mm::Mini80PowerController controller(driver), competing(driver);
    REQUIRE(controller.configure(2, 100)); REQUIRE(competing.configure(2, 100));
    REQUIRE(controller.initializeCold(tree.scheduler, &tree.owner, ""));
    CHECK_FALSE(driver.userEditable()); CHECK_FALSE(driver.acceptsControlWrite("pins"));
    CHECK_FALSE(competing.initializeCold(tree.scheduler, &tree.owner, ""));
    CHECK(competing.startupError() == mm::Mini80PowerController::StartupError::DriverOwned);
    REQUIRE(controller.shutdown());
    CHECK_FALSE(mm::platform::gpioRead(17)); CHECK_FALSE(mm::platform::gpioRead(47));
    driver.release(); mm::platform::clearTestGpioLevel();
}

TEST_CASE("mini80 controller sends and latches black before visible output and before switching off") {
    mm::RmtLedDriver driver;
    mm::Buffer source;
    mm::Correction correction;
    REQUIRE(source.allocate(52, 4));
    std::memset(source.data(), 255, 52 * 4);
    mm::Mini80PowerController controller(driver); MiniTree tree;
    mm::platform::setTestGpioLevel(17, true);
    mm::platform::setTestGpioLevel(47, true);
    CHECK_FALSE(controller.initializeCold(tree.scheduler, &tree.owner, ""));
    CHECK(controller.startupError() == mm::Mini80PowerController::StartupError::Unqualified);
    controller.tick(true, 0); // no timing qualification means no GPIO changes
    CHECK(controller.error() == Sequence::Error::Unqualified);
    CHECK(mm::platform::gpioRead(17)); CHECK(mm::platform::gpioRead(47));
    REQUIRE(controller.configure(2, 100));
    REQUIRE(controller.initializeCold(tree.scheduler, &tree.owner, ""));
    CHECK_FALSE(mm::platform::gpioRead(17)); CHECK_FALSE(mm::platform::gpioRead(47));
    CHECK_FALSE(controller.configure(3, 100));
    mm::test::rebuildFromPreset(correction, 255, mm::test::PresetOrder::RGBW);
    std::strcpy(driver.pins, "47");
    std::strcpy(driver.ledsPerPin, "52");
    driver.defineControls(); driver.setSourceBuffer(&source);
    driver.correctionForTest() = correction; driver.applyState();
    // An accidental scheduler tick before the power sequence must not drive
    // the unpowered strip, even though source data is already available.
    driver.tick();
    CHECK_FALSE(driver.lastFrameLatched());
    CHECK_FALSE(driver.frameInFlight());
    CHECK_FALSE(mm::platform::gpioRead(47)); CHECK_FALSE(mm::platform::gpioRead(17));
    controller.tick(true, 10);
    REQUIRE(controller.state() == Sequence::State::Settling);
    CHECK(mm::platform::gpioRead(17));
    controller.tick(true, 11);
    CHECK(controller.state() == Sequence::State::Settling);
    controller.tick(true, 12);
    REQUIRE(controller.state() == Sequence::State::StartingBlack);
    REQUIRE(driver.blackFrameLatched());
    for (size_t i = 0; i < 52 * 4; ++i) CHECK(driver.frameBuffer()[i] == 0);
    controller.tick(true, 13);
    REQUIRE(controller.state() == Sequence::State::On);
    CHECK(driver.frameBuffer()[0] != 0);
    // The rail owner still holds the driver after the black startup hold is
    // released. Neither a live request nor a stored file may alter its pins.
    CHECK_FALSE(driver.userEditable()); CHECK(driver.blocksAncestorRemoval());
    driver.setName("RmtMini"); REQUIRE(tree.owner.addChild(&driver));
    CHECK(tree.scheduler.setControl("RmtMini", "pins", R"("48")") == mm::Scheduler::SetControlResult::ReadOnly);
    CHECK(std::strcmp(driver.pins, "47") == 0);
    auto blocked = mm::applyStateDocument(tree.scheduler, R"({"Mini":{"RmtMini":{"pins":"48","enabled":false}}})", mm::StateSource::Stored);
    CHECK(blocked.skipped > 0); CHECK(std::strcmp(driver.pins, "47") == 0); CHECK(driver.enabledSetting());
    CHECK_FALSE(controller.shutdownStep(14));
    REQUIRE(controller.state() == Sequence::State::StoppingBlack);
    REQUIRE(driver.blackFrameLatched());
    REQUIRE(controller.shutdownStep(15));
    CHECK(controller.state() == Sequence::State::Off);
    CHECK(controller.safeOffAcknowledged());
    CHECK(controller.shutdownStep(16)); // repeated shutdown is harmless
    // The confirmed rail-off and detached peripheral complete the handover.
    CHECK(driver.acceptsControlWrite("pins")); CHECK(driver.userEditable());
    CHECK_FALSE(mm::platform::gpioRead(17));
    CHECK_FALSE(mm::platform::gpioRead(47));
    driver.release();
    mm::platform::clearTestGpioLevel();
}

TEST_CASE("mini80 cold start refuses a driver that already owns an output") {
    mm::RmtLedDriver driver;
    mm::Buffer source; REQUIRE(source.allocate(52, 4));
    std::strcpy(driver.pins, "47"); std::strcpy(driver.ledsPerPin, "52");
    driver.defineControls(); driver.setSourceBuffer(&source); driver.applyState();
    REQUIRE(driver.pinCount() == 1);
    mm::platform::setTestGpioLevel(17, true);
    mm::platform::setTestGpioLevel(47, true);
    mm::Mini80PowerController controller(driver); MiniTree tree;
    REQUIRE(controller.configure(2, 100));
    CHECK_FALSE(controller.initializeCold(tree.scheduler, &tree.owner, ""));
    CHECK(controller.startupError() == mm::Mini80PowerController::StartupError::DriverInUse);
    CHECK(mm::platform::gpioRead(17)); CHECK(mm::platform::gpioRead(47));
    driver.release(); mm::platform::clearTestGpioLevel();
}

TEST_CASE("mini80 cold start refuses relay conflicts before touching either pin") {
    struct Restore { ~Restore() { mm::platform::clearTestGpioLevel(); } } restore;
    mm::RmtLedDriver driver; mm::Mini80PowerController controller(driver); MiniTree tree;
    REQUIRE(controller.configure(2, 100));
    mm::platform::setTestGpioLevel(17, true); mm::platform::setTestGpioLevel(47, true);
    CHECK_FALSE(controller.initializeCold(tree.scheduler, &tree.owner, nullptr));
    CHECK(controller.startupError() == mm::Mini80PowerController::StartupError::InvalidRelayConfig);
    CHECK_FALSE(controller.initializeCold(tree.scheduler, &tree.owner, "17,,bad"));
    CHECK(controller.startupError() == mm::Mini80PowerController::StartupError::InvalidRelayConfig);
    for (const char* relay : {"17", "12,17,18", "47"}) {
        CHECK_FALSE(controller.initializeCold(tree.scheduler, &tree.owner, relay));
        CHECK(controller.startupError() == mm::Mini80PowerController::StartupError::RelayConflict);
        CHECK(mm::platform::gpioRead(17)); CHECK(mm::platform::gpioRead(47));
    }
    REQUIRE(controller.initializeCold(tree.scheduler, &tree.owner, "12,18"));
    CHECK_FALSE(mm::platform::gpioRead(17)); CHECK_FALSE(mm::platform::gpioRead(47));
    REQUIRE(controller.shutdown()); driver.release();
}

TEST_CASE("mini80 cold start identifies pin failures and can retry without powering the strip") {
    struct Restore { ~Restore() { mm::platform::clearTestGpioCapability(); mm::platform::clearTestGpioLevel(); } } restore;
    mm::RmtLedDriver driver;
    mm::Mini80PowerController controller(driver); MiniTree tree;
    REQUIRE(controller.configure(2, 100));
    mm::platform::setTestGpioLevel(17, true);
    mm::platform::setTestGpioLevel(47, true);
    mm::platform::GpioCapability cap; cap.reserved = true;
    mm::platform::setTestGpioCapability(47, cap);
    CHECK_FALSE(controller.initializeCold(tree.scheduler, &tree.owner, ""));
    CHECK(controller.startupError() == mm::Mini80PowerController::StartupError::DataHold);
    CHECK(mm::platform::gpioRead(17)); CHECK(mm::platform::gpioRead(47));
    mm::Mini80PowerController competing(driver); REQUIRE(competing.configure(2, 100));
    CHECK_FALSE(competing.initializeCold(tree.scheduler, &tree.owner, ""));
    CHECK(competing.startupError() == mm::Mini80PowerController::StartupError::DriverOwned);
    mm::platform::clearTestGpioCapability();
    mm::platform::setTestGpioCapability(17, cap);
    CHECK_FALSE(controller.initializeCold(tree.scheduler, &tree.owner, ""));
    CHECK(controller.startupError() == mm::Mini80PowerController::StartupError::RailOff);
    CHECK(mm::platform::gpioRead(17)); CHECK_FALSE(mm::platform::gpioRead(47));
    mm::platform::clearTestGpioCapability();
    REQUIRE(controller.initializeCold(tree.scheduler, &tree.owner, ""));
    CHECK(controller.startupError() == mm::Mini80PowerController::StartupError::None);
    CHECK_FALSE(mm::platform::gpioRead(17)); CHECK_FALSE(mm::platform::gpioRead(47));
    REQUIRE(controller.shutdown()); driver.release();
}

TEST_CASE("mini80 abort retains its owner until partial cold start is safely held off") {
    struct Restore { ~Restore() { mm::platform::clearTestGpioCapability(); mm::platform::clearTestGpioLevel(); } } restore;
    mm::RmtLedDriver driver; MiniTree tree;
    mm::Mini80PowerController controller(driver), competing(driver);
    REQUIRE(controller.configure(2, 100)); REQUIRE(competing.configure(2, 100));
    mm::platform::setTestGpioLevel(17, true); mm::platform::setTestGpioLevel(47, true);
    mm::platform::GpioCapability cap; cap.reserved = true; mm::platform::setTestGpioCapability(47, cap);
    CHECK_FALSE(controller.initializeCold(tree.scheduler, &tree.owner, ""));
    CHECK_FALSE(controller.abortColdStart()); CHECK(driver.powerOwnedBy(&controller));
    CHECK(tree.scheduler.pinReservedBy(&controller, 17)); CHECK(tree.scheduler.pinReservedBy(&controller, 47));
    CHECK_FALSE(competing.abortColdStart()); CHECK_FALSE(competing.initializeCold(tree.scheduler, &tree.owner, ""));
    mm::platform::clearTestGpioCapability();
    REQUIRE(controller.shutdownStep(1)); // shutdown can also recover a partial cold start
    CHECK_FALSE(tree.scheduler.pinReservedBy(&controller, 17)); CHECK_FALSE(tree.scheduler.pinReservedBy(&controller, 47));
    CHECK_FALSE(driver.powerOwnedBy(&controller)); CHECK_FALSE(mm::platform::gpioRead(17)); CHECK_FALSE(mm::platform::gpioRead(47));
    CHECK(driver.userEditable()); // confirmed safe handover releases the temporary hold
    CHECK_FALSE(driver.blocksAncestorRemoval());
    REQUIRE(competing.initializeCold(tree.scheduler, &tree.owner, ""));
    REQUIRE(competing.shutdown()); driver.release();
}

TEST_CASE("mini80 controller can shut down after its source is removed") {
    mm::RmtLedDriver driver; mm::Buffer source; mm::Correction correction;
    REQUIRE(source.allocate(52, 4));
    mm::Mini80PowerController controller(driver); MiniTree tree;
    REQUIRE(controller.configure(2, 100)); REQUIRE(controller.initializeCold(tree.scheduler, &tree.owner, ""));
    mm::test::rebuildFromPreset(correction, 255, mm::test::PresetOrder::RGBW);
    std::strcpy(driver.pins, "47"); std::strcpy(driver.ledsPerPin, "52");
    driver.defineControls(); driver.setSourceBuffer(&source);
    driver.correctionForTest() = correction; driver.applyState();
    controller.tick(true, 0); controller.tick(true, 2); controller.tick(true, 3);
    REQUIRE(controller.state() == Sequence::State::On);
    REQUIRE(mm::platform::gpioRead(17));
    driver.setSourceBuffer(nullptr);
    REQUIRE(controller.shutdown());
    CHECK(controller.safeOffAcknowledged());
    CHECK_FALSE(mm::platform::gpioRead(17)); CHECK_FALSE(mm::platform::gpioRead(47));
    driver.release(); mm::platform::clearTestGpioLevel();
}

TEST_CASE("mini80 direct driver release cannot bypass the powered rail handover") {
    struct Restore { ~Restore() { mm::platform::clearTestGpioLevel(); } } restore;
    mm::RmtLedDriver driver; mm::Buffer source; mm::Correction correction;
    REQUIRE(source.allocate(52, 4));
    mm::Mini80PowerController controller(driver); MiniTree tree;
    REQUIRE(controller.configure(2, 100)); REQUIRE(controller.initializeCold(tree.scheduler, &tree.owner, ""));
    mm::test::rebuildFromPreset(correction, 255, mm::test::PresetOrder::RGBW);
    std::strcpy(driver.pins, "47"); std::strcpy(driver.ledsPerPin, "52");
    driver.defineControls(); driver.setSourceBuffer(&source); driver.correctionForTest() = correction; driver.applyState();
    controller.tick(true, 0); controller.tick(true, 2); controller.tick(true, 3);
    REQUIRE(controller.state() == Sequence::State::On); REQUIRE(mm::platform::gpioRead(17));
    const auto capacity = driver.frameCapacity(); REQUIRE(capacity > 0);
    driver.release();
    CHECK(driver.powerOwnedBy(&controller)); CHECK(driver.frameCapacity() == capacity);
    CHECK(mm::platform::gpioRead(17)); CHECK(driver.blocksAncestorRemoval());
    CHECK_FALSE(controller.shutdownStep(4)); REQUIRE(driver.blackFrameLatched());
    REQUIRE(controller.shutdownStep(5)); CHECK_FALSE(mm::platform::gpioRead(17));
    CHECK_FALSE(driver.powerOwnedBy(&controller)); driver.release();
}

TEST_CASE("mini80 generic prepare cannot rewire the powered output") {
    struct Restore { ~Restore() { mm::platform::clearTestGpioLevel(); } } restore;
    mm::RmtLedDriver driver; mm::Buffer source; mm::Correction correction;
    REQUIRE(source.allocate(52, 4));
    mm::Mini80PowerController controller(driver); MiniTree tree;
    REQUIRE(controller.configure(2, 100)); REQUIRE(controller.initializeCold(tree.scheduler, &tree.owner, ""));
    mm::test::rebuildFromPreset(correction, 255, mm::test::PresetOrder::RGBW);
    std::strcpy(driver.pins, "47"); std::strcpy(driver.ledsPerPin, "52");
    driver.defineControls(); driver.setSourceBuffer(&source); driver.correctionForTest() = correction; driver.applyState();
    controller.tick(true, 0); controller.tick(true, 2); controller.tick(true, 3);
    REQUIRE(controller.state() == Sequence::State::On); REQUIRE(driver.hasPowerPin(47));
    std::strcpy(driver.pins, "18"); // direct mutation simulates a noncooperating caller
    driver.prepare();
    CHECK(driver.hasPowerPin(47)); CHECK_FALSE(driver.hasPowerPin(18)); CHECK(mm::platform::gpioRead(17));
    std::strcpy(driver.pins, "47");
    CHECK_FALSE(controller.shutdownStep(4)); REQUIRE(driver.blackFrameLatched());
    REQUIRE(controller.shutdownStep(5)); CHECK_FALSE(mm::platform::gpioRead(17)); driver.release();
}

TEST_CASE("mini80 stops safely when its live source changes") {
    struct Restore { ~Restore() { mm::platform::clearTestGpioLevel(); } } restore;
    mm::RmtLedDriver driver; mm::Buffer source, replacement; mm::Correction correction;
    REQUIRE(source.allocate(52, 4)); REQUIRE(replacement.allocate(52, 4));
    mm::Mini80PowerController controller(driver); MiniTree tree;
    REQUIRE(controller.configure(2, 100)); REQUIRE(controller.initializeCold(tree.scheduler, &tree.owner, ""));
    mm::test::rebuildFromPreset(correction, 255, mm::test::PresetOrder::RGBW);
    std::strcpy(driver.pins, "47"); std::strcpy(driver.ledsPerPin, "52");
    driver.defineControls(); driver.setSourceBuffer(&source); driver.correctionForTest() = correction; driver.applyState();
    controller.tick(true, 0); controller.tick(true, 2); controller.tick(true, 3);
    REQUIRE(controller.state() == Sequence::State::On); REQUIRE(mm::platform::gpioRead(17));
    driver.setSourceBuffer(&source); // an unchanged buffer during a tree rebuild is harmless
    controller.tick(true, 4); CHECK(controller.state() == Sequence::State::On);
    mm::platform::resetTestRmtTransmitCount();
    driver.setSourceBuffer(&replacement); driver.tick();
    CHECK(mm::platform::testRmtTransmitCount() == 0);
    controller.tick(true, 5);
    CHECK(controller.state() == Sequence::State::Fault); CHECK(controller.safeOffAcknowledged());
    CHECK_FALSE(mm::platform::gpioRead(17)); CHECK_FALSE(mm::platform::gpioRead(47));
    REQUIRE(controller.shutdown()); driver.release();
}

TEST_CASE("mini80 stops safely when output channel geometry changes") {
    struct Restore { ~Restore() { mm::platform::clearTestGpioLevel(); } } restore;
    mm::RmtLedDriver driver; mm::Buffer source; mm::Correction correction;
    REQUIRE(source.allocate(52, 4));
    mm::Mini80PowerController controller(driver); MiniTree tree;
    REQUIRE(controller.configure(2, 100)); REQUIRE(controller.initializeCold(tree.scheduler, &tree.owner, ""));
    mm::test::rebuildFromPreset(correction, 255, mm::test::PresetOrder::RGBW);
    std::strcpy(driver.pins, "47"); std::strcpy(driver.ledsPerPin, "52");
    driver.defineControls(); driver.setSourceBuffer(&source); driver.correctionForTest() = correction; driver.applyState();
    controller.tick(true, 0); controller.tick(true, 2); controller.tick(true, 3);
    REQUIRE(controller.state() == Sequence::State::On);
    mm::platform::resetTestRmtTransmitCount();
    driver.correctionForTest().outChannels = 3; driver.onCorrectionChanged(); driver.tick();
    CHECK(mm::platform::testRmtTransmitCount() == 0);
    controller.tick(true, 4);
    CHECK(controller.state() == Sequence::State::Fault); CHECK(controller.safeOffAcknowledged());
    CHECK_FALSE(mm::platform::gpioRead(17)); CHECK_FALSE(mm::platform::gpioRead(47));
    REQUIRE(controller.shutdown()); driver.release();
}

TEST_CASE("mini80 controller can safely shut down immediately after cold initialization") {
    mm::RmtLedDriver driver; mm::Mini80PowerController controller(driver); MiniTree tree;
    REQUIRE(controller.configure(2, 100));
    REQUIRE(controller.initializeCold(tree.scheduler, &tree.owner, ""));
    REQUIRE(controller.shutdown());
    CHECK(controller.safeOffAcknowledged());
    CHECK_FALSE(mm::platform::gpioRead(17));
    CHECK_FALSE(mm::platform::gpioRead(47));
    controller.tick(true, 10); // a late tick cannot reacquire or power a released rail
    CHECK(controller.state() == Sequence::State::Off);
    CHECK_FALSE(mm::platform::gpioRead(17));
    REQUIRE(controller.initializeCold(tree.scheduler, &tree.owner, ""));
    REQUIRE(controller.shutdown());
    driver.release(); mm::platform::clearTestGpioLevel();
}

TEST_CASE("mini80 controller retains ownership when a live output cannot detach") {
    struct ResetFault { ~ResetFault() { mm::platform::setTestRmtDetachFailure(false); mm::platform::clearTestGpioLevel(); } } reset;
    mm::RmtLedDriver driver; mm::Buffer source; mm::Correction correction;
    REQUIRE(source.allocate(52, 4));
    mm::Mini80PowerController controller(driver); MiniTree tree;
    REQUIRE(controller.configure(2, 100)); REQUIRE(controller.initializeCold(tree.scheduler, &tree.owner, ""));
    mm::test::rebuildFromPreset(correction, 255, mm::test::PresetOrder::RGBW);
    std::strcpy(driver.pins, "47"); std::strcpy(driver.ledsPerPin, "52");
    driver.defineControls(); driver.setSourceBuffer(&source); driver.correctionForTest() = correction; driver.applyState();
    controller.tick(true, 0); controller.tick(true, 2); controller.tick(true, 3);
    REQUIRE(controller.state() == Sequence::State::On);
    int wrongOwner = 0;
    CHECK_FALSE(driver.finishPowerHandover(&wrongOwner));
    CHECK_FALSE(driver.finishPowerHandover(&controller)); // output is still attached
    CHECK(driver.blocksAncestorRemoval());
    mm::platform::setTestRmtDetachFailure(true);
    CHECK_FALSE(controller.shutdownStep(4)); // request and physically send black first
    REQUIRE(driver.blackFrameLatched());
    CHECK_FALSE(controller.shutdownStep(5)); CHECK_FALSE(controller.safeOffAcknowledged());
    CHECK(driver.powerOwnedBy(&controller)); CHECK(driver.blocksAncestorRemoval());
    CHECK(tree.scheduler.pinReservedBy(&controller, 17)); CHECK(tree.scheduler.pinReservedBy(&controller, 47));
    CHECK_FALSE(driver.finishPowerHandover(&wrongOwner));
    CHECK_FALSE(controller.initializeCold(tree.scheduler, &tree.owner, ""));
    CHECK(controller.startupError() == mm::Mini80PowerController::StartupError::AlreadyInitialized);
    mm::platform::setTestRmtDetachFailure(false);
    REQUIRE(controller.shutdownStep(6)); CHECK(controller.safeOffAcknowledged());
    CHECK_FALSE(driver.powerOwnedBy(&controller)); CHECK_FALSE(driver.blocksAncestorRemoval());
    CHECK_FALSE(tree.scheduler.pinReservedBy(&controller, 17)); CHECK_FALSE(tree.scheduler.pinReservedBy(&controller, 47));
    controller.tick(true, 10);
    CHECK(controller.state() == Sequence::State::Off); CHECK_FALSE(mm::platform::gpioRead(17));
    driver.release();
}

TEST_CASE("mini80 scheduler handover retains live tree on detach failure and retries") {
    struct ResetFault { ~ResetFault() { mm::platform::setTestRmtDetachFailure(false); mm::platform::clearTestGpioLevel(); } } reset;
    struct Owner final : mm::MoonModule {
        mm::RmtLedDriver* output = new mm::RmtLedDriver();
        mm::Mini80PowerController power{*output};
        mm::Buffer source;
        void shutdownStep(uint32_t now) MM_NONBLOCKING override {
            power.shutdownStep(now);
            mm::MoonModule::shutdownStep(now);
        }
    };
    mm::Scheduler scheduler;
    auto* owner = new Owner(); owner->setName("MiniOwner");
    owner->output->setName("MiniOutput");
    REQUIRE(owner->addChild(owner->output)); scheduler.addModule(owner);
    REQUIRE(owner->source.allocate(52, 4));
    REQUIRE(owner->power.configure(2, 100));
    REQUIRE(owner->power.initializeCold(scheduler, owner, ""));
    mm::Correction correction; mm::test::rebuildFromPreset(correction, 255, mm::test::PresetOrder::RGBW);
    std::strcpy(owner->output->pins, "47"); std::strcpy(owner->output->ledsPerPin, "52");
    owner->output->defineControls(); owner->output->setSourceBuffer(&owner->source);
    owner->output->correctionForTest() = correction; owner->output->applyState();
    owner->power.tick(true, 0); owner->power.tick(true, 2); owner->power.tick(true, 3);
    REQUIRE(owner->power.state() == Sequence::State::On);
    CHECK_FALSE(scheduler.release());
    scheduler.shutdownStep(4); REQUIRE(owner->output->blackFrameLatched());
    CHECK_FALSE(scheduler.release());
    mm::platform::setTestRmtDetachFailure(true);
    scheduler.shutdownStep(5);
    CHECK_FALSE(scheduler.release());
    CHECK(scheduler.firstByName("MiniOutput") == owner->output);
    CHECK(scheduler.pinReservedBy(&owner->power, 17)); CHECK(scheduler.pinReservedBy(&owner->power, 47));
    mm::platform::setTestRmtDetachFailure(false);
    scheduler.shutdownStep(6);
    CHECK_FALSE(mm::platform::gpioRead(17)); CHECK_FALSE(mm::platform::gpioRead(47));
    CHECK_FALSE(scheduler.pinReservedBy(&owner->power, 17)); CHECK_FALSE(scheduler.pinReservedBy(&owner->power, 47));
    CHECK(scheduler.release());
    CHECK(scheduler.firstByName("MiniOutput") == nullptr);
}

TEST_CASE("mini80 pin preflight refuses declared conflicts including disabled modules") {
    struct Claims : mm::MoonModule {
        int8_t pin = -1;
        char relayPins[32] = {};
        uint8_t fixedCount = 0;
        uint8_t fixedGpio = 0;
        void defineControls() override {
            controls_.addPin("pin", pin);
            controls_.addText("relayPins", relayPins, sizeof(relayPins));
        }
        uint8_t fixedPins(FixedPin* out, uint8_t max) const override {
            const uint8_t n = fixedCount < max ? fixedCount : max;
            for (uint8_t i = 0; i < n; ++i) out[i] = {fixedGpio, "test"};
            return n;
        }
    } owner, other;
    using Check = mm::Mini80PinPreflight;
    mm::Scheduler scheduler;
    owner.setName("Owner"); other.setName("Other");
    owner.defineControls(); other.defineControls();
    scheduler.addModule(&owner); scheduler.addModule(&other);
    CHECK(Check::check(scheduler, nullptr) == Check::Result::Unknown);
    CHECK(Check::check(scheduler, &owner) == Check::Result::Clear);
    owner.pin = 47; CHECK(Check::check(scheduler, &owner) == Check::Result::Clear);
    other.pin = 17; CHECK(Check::check(scheduler, &owner) == Check::Result::Conflict);
    other.setEnabled(false); CHECK(Check::check(scheduler, &owner) == Check::Result::Conflict);
    other.pin = -1; std::strcpy(other.relayPins, "12,47");
    CHECK(Check::check(scheduler, &owner) == Check::Result::Conflict);
    std::strcpy(other.relayPins, "12,,bad");
    CHECK(Check::check(scheduler, &owner) == Check::Result::Unknown);
    other.relayPins[0] = 0; other.fixedCount = 1; other.fixedGpio = 17;
    CHECK(Check::check(scheduler, &owner) == Check::Result::Conflict);
    other.fixedGpio = 12; other.fixedCount = 16;
    CHECK(Check::check(scheduler, &owner) == Check::Result::Unknown);
    other.fixedCount = 1;
    CHECK(Check::check(scheduler, &owner) == Check::Result::Clear);
}

TEST_CASE("mini80 controller refuses declared pin conflicts before GPIO changes") {
    struct Restore { ~Restore() { mm::platform::clearTestGpioLevel(); } } restore;
    struct Other : mm::MoonModule {
        int8_t pin = 17;
        void defineControls() override { controls_.addPin("pin", pin); }
    } other;
    mm::RmtLedDriver driver; mm::Mini80PowerController controller(driver); MiniTree tree;
    other.setName("Other"); other.defineControls(); other.setEnabled(false); tree.scheduler.addModule(&other);
    REQUIRE(controller.configure(2, 100));
    mm::platform::setTestGpioLevel(17, true); mm::platform::setTestGpioLevel(47, true);
    mm::MoonModule missing;
    CHECK_FALSE(controller.initializeCold(tree.scheduler, &missing, ""));
    CHECK(controller.startupError() == mm::Mini80PowerController::StartupError::PinConflict);
    other.pin = -1;
    CHECK_FALSE(controller.initializeCold(tree.scheduler, &missing, ""));
    CHECK(controller.startupError() == mm::Mini80PowerController::StartupError::PinClaimsUnknown);
    other.pin = 17;
    CHECK_FALSE(controller.initializeCold(tree.scheduler, &tree.owner, ""));
    CHECK(controller.startupError() == mm::Mini80PowerController::StartupError::PinConflict);
    CHECK(mm::platform::gpioRead(17)); CHECK(mm::platform::gpioRead(47));
    other.pin = -1;
    REQUIRE(controller.initializeCold(tree.scheduler, &tree.owner, ""));
    REQUIRE(controller.shutdown()); driver.release();
}
