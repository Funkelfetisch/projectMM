/// What the pin ownership map above is built from, combining two sources.
///
/// No chip type escapes this file: the module gets a plain capability.
///
/// @technical
///
/// ## Two sources, because one of the questions has no query
///
/// The SDK answers whether a pin is valid, output-capable or low-power; whether it is a strap or belongs to flash or external memory has no query, so a per-chip table mirroring the hardware reference page carries it.
///
/// ## Two lists per chip, one of them conditional
///
/// Reserved pins corrupt the device if used, and straps change the boot mode if driven at reset; the build's target picks the set, and a pin in neither list is neither.
///
/// The second list is the memory bus pins taken only when the module HAS external memory, a runtime fact, so one image adds them only when it is fitted.
///
/// ## The package is read at runtime, not from the build
///
/// The system-in-package parts wire their in-package memory differently under one firmware, so the package is read from the fuses: on one, muxing any of four unbonded pads wedges the flash cache with no panic.
///
/// ## Pins are configured for input and output together
///
/// A plain output disables the input buffer, so a read of a pin driving high returns nothing and the map's live column lies; the buffer costs nothing, as every other peripheral here uses it.
///
#include "platform/platform.h"
#include "platform/GpioPreload.h"

#include "sdkconfig.h"        // CONFIG_IDF_TARGET_*: selects the per-chip strap/reserved table
#include "soc/gpio_num.h"     // GPIO_IS_VALID_GPIO / GPIO_IS_VALID_OUTPUT_GPIO
#include "driver/gpio.h"      // gpio_get_level / gpio_get_drive_capability: the live-state reads
#include "driver/rtc_io.h"    // rtc_gpio_is_valid_gpio
#include "esp_efuse.h"        // esp_efuse_get_pkg_ver: the classic ESP32's PACKAGE decides its pin table
#include "soc/efuse_defs.h"   // EFUSE_RD_CHIP_VER_PKG_*: the package ids
#include "esp_heap_caps.h"    // heap_caps_get_total_size(MALLOC_CAP_SPIRAM): detect PSRAM without a new
                              // component dep (the heap component is always linked; esp_psram is not, and adding it to REQUIRES would switch main to strict mode, hiding the implicitly-available components the other platform files rely on)

#include <cstddef>
#include <cstdint>

namespace mm::platform {

namespace {

// The per-chip strap and reserved pins, from the hardware reference page: @xref{two-lists-per-chip-one-of-them-conditional|what each list means}. Both helpers are plain linear scans over tiny fixed arrays.
bool inList(uint8_t gpio, const uint8_t* list, size_t n) {
    for (size_t i = 0; i < n; i++) if (list[i] == gpio) return true;
    return false;
}

#if defined(CONFIG_IDF_TARGET_ESP32)
// Classic ESP32 in a plain module (WROOM / WROVER, a D0WD die): flash 6-11 always; 16/17 are the extra flash/PSRAM bus on WROVER modules only.
constexpr uint8_t kReserved[]        = {6, 7, 8, 9, 10, 11};
constexpr uint8_t kReservedIfPsram[] = {16, 17};
constexpr uint8_t kStrap[]           = {0, 2, 5, 12, 15};
// The package is read from the chip's own fuses at runtime: @xref{the-package-is-read-at-runtime-not-from-the-build|why, and what an unbonded pad costs}.
constexpr uint8_t kReservedPicoV302[] = {6, 9, 10, 11};
constexpr uint8_t kAbsentPicoV302[]   = {16, 17, 18, 23};
constexpr uint8_t kReservedPicoD4[]   = {6, 7, 8, 9, 10, 11, 16, 17};

/// The eFuse package id, read once (it cannot change after boot).
uint32_t packageId() {
    static const uint32_t pkg = esp_efuse_get_pkg_ver();
    return pkg;
}
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
// The octal memory bus takes a second range only in an image built for it, so a quad build leaves those pins free; debug and serial pins stay usable, conflicts rather than reservations.
constexpr uint8_t kReserved[]        = {26, 27, 28, 29, 30, 31, 32};
#if CONFIG_SPIRAM_MODE_OCT
constexpr uint8_t kReservedIfPsram[] = {33, 34, 35, 36, 37};
#else
constexpr uint8_t kReservedIfPsram[] = {};
#endif
constexpr uint8_t kStrap[]           = {0, 3, 45, 46};
#elif defined(CONFIG_IDF_TARGET_ESP32P4)
// ESP32-P4: flash/PSRAM are module-internal (the SDK's valid-GPIO query already excludes the bonded ones on a given package); straps 34-38.
constexpr uint8_t kReserved[]        = {};
constexpr uint8_t kReservedIfPsram[] = {};
constexpr uint8_t kStrap[]           = {34, 35, 36, 37, 38};
#elif defined(CONFIG_IDF_TARGET_ESP32S31)
// ESP32-S31: flash/PSRAM module-internal (SDK query excludes them). Board-wired peripheral pins (RGMII/codec/SD) are a per-board concern the catalog owns, not a chip strap/reserved fact.
constexpr uint8_t kReserved[]        = {};
constexpr uint8_t kReservedIfPsram[] = {};
constexpr uint8_t kStrap[]           = {};
#else
constexpr uint8_t kReserved[]        = {};
constexpr uint8_t kReservedIfPsram[] = {};
constexpr uint8_t kStrap[]           = {};
#endif

// Queried once and cached: PSRAM presence cannot change after boot. The heap-caps total for the SPIRAM region is the cheap read, 0 meaning none (the heap component is always linked, unlike esp_psram, see the include note).
bool psramPresent() {
    static const bool present = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0;
    return present;
}

}  // namespace

GpioCapability gpioCapability(uint8_t gpio) {
    GpioCapability c;
    c.validGpio     = GPIO_IS_VALID_GPIO(gpio);
    c.outputCapable = GPIO_IS_VALID_OUTPUT_GPIO(gpio);
    c.rtc           = rtc_gpio_is_valid_gpio(static_cast<gpio_num_t>(gpio));
    c.strap         = inList(gpio, kStrap, sizeof(kStrap));
    // kReservedIfPsram needs PSRAM present, except where the image fixes the pins: an octal-PSRAM S3 build wires 33-37 to it whatever the runtime finds, so a failed PSRAM init frees none.
#if defined(CONFIG_IDF_TARGET_ESP32S3) && CONFIG_SPIRAM_MODE_OCT
    constexpr bool kPsramPinsFixedByImage = true;
#else
    constexpr bool kPsramPinsFixedByImage = false;
#endif
    c.reserved      = inList(gpio, kReserved, sizeof(kReserved)) ||
                      ((kPsramPinsFixedByImage || psramPresent()) &&
                       inList(gpio, kReservedIfPsram, sizeof(kReservedIfPsram)));
#if defined(CONFIG_IDF_TARGET_ESP32)
    // The die's valid-GPIO mask does not know the package; the tables above do (see their note).
    switch (packageId()) {
    case EFUSE_RD_CHIP_VER_PKG_ESP32PICOV302:
        c.reserved  = inList(gpio, kReservedPicoV302, sizeof(kReservedPicoV302));
        c.validGpio = c.validGpio && !inList(gpio, kAbsentPicoV302, sizeof(kAbsentPicoV302));
        break;
    case EFUSE_RD_CHIP_VER_PKG_ESP32PICOD4:
        c.reserved  = inList(gpio, kReservedPicoD4, sizeof(kReservedPicoD4));
        break;
    default:
        break;
    }
#endif
    return c;
}

const char* gpioRefusal(uint8_t gpio) {
    const GpioCapability c = gpioCapability(gpio);
    if (!c.validGpio) return "does not exist on this chip package";
    if (c.reserved)   return "is wired to flash/PSRAM on this chip";
    return nullptr;
}

GpioLiveState gpioLiveState(uint8_t gpio) {
    GpioLiveState s;
    if (!GPIO_IS_VALID_GPIO(gpio)) return s;   // valid=false → the map omits the live columns
    s.valid = true;
    s.level = gpio_get_level(static_cast<gpio_num_t>(gpio)) != 0;   // reads the pad, see the wire
    gpio_drive_cap_t cap = GPIO_DRIVE_CAP_DEFAULT;
    gpio_get_drive_capability(static_cast<gpio_num_t>(gpio), &cap);
    s.driveCap = static_cast<uint8_t>(cap);    // 0..3 = WEAK / MEDIUM / STRONG / STRONGEST
    // The live direction off the pad config, not the role's intent: a role that should drive but reads back no output driver is the dead-driver mismatch the map flags.
    gpio_io_config_t io = {};
    if (gpio_get_io_config(static_cast<gpio_num_t>(gpio), &io) == ESP_OK) {
        s.output   = io.oe;
        s.input    = io.ie;
        s.pullUp   = io.pu;
        s.pullDown = io.pd;
    }
    return s;
}

// --- GPIO as a working input/output -----------------------------------------------------------
// The pin map's diagnostics are above; a module drives a line or reads a switch through these: gpio_config once, then register get/set, so a poll costs one read.

bool gpioInputBegin(uint8_t gpio, GpioPull pull) {
    if (!GPIO_IS_VALID_GPIO(gpio)) return false;
    // A RESERVED pin is wired to flash, PSRAM or native USB, and routing I/O there corrupts the device (gpioCapability's own words). Refused rather than configured: the caller reports a pin it could not open, where a corrupted flash reports nothing at all.
    if (gpioCapability(gpio).reserved) return false;
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << gpio;
    cfg.mode         = GPIO_MODE_INPUT;
    cfg.pull_up_en   = pull == GpioPull::Up   ? GPIO_PULLUP_ENABLE   : GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = pull == GpioPull::Down ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
    cfg.intr_type    = GPIO_INTR_DISABLE;      // polled, not interrupt-driven: see the seam's docs
    return gpio_config(&cfg) == ESP_OK;
}

bool gpioRead(uint8_t gpio) {
    if (!GPIO_IS_VALID_GPIO(gpio)) return false;
    return gpio_get_level(static_cast<gpio_num_t>(gpio)) != 0;
}

bool gpioWrite(uint8_t gpio, bool high) {
    // Output-capable, not merely valid: the classic ESP32's 34-39 are input-only, and driving one silently does nothing. Refusing here is what lets a caller report the pin rather than wonder.
    if (!GPIO_IS_VALID_OUTPUT_GPIO(gpio)) return false;
    // And not RESERVED, the same policy gpioInputBegin applies: a pin wired to flash, PSRAM or native USB corrupts the device when driven, and a relay list is exactly where a wrong number gets typed.
    if (gpioCapability(gpio).reserved) return false;
    // Configured on first use so a caller that owns the pin just writes it. gpio_config is idempotent, and this runs on a control change, never per frame.
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << gpio;
    // Input and output together: @xref{pins-are-configured-for-input-and-output-together|why a plain output makes the map lie}.
    cfg.mode         = GPIO_MODE_INPUT_OUTPUT;
    cfg.intr_type    = GPIO_INTR_DISABLE;
    if (gpio_config(&cfg) != ESP_OK) return false;
    return gpio_set_level(static_cast<gpio_num_t>(gpio), high ? 1 : 0) == ESP_OK;
}

bool gpioPreloadOutput(uint8_t gpio, bool high) {
    if (!GPIO_IS_VALID_OUTPUT_GPIO(gpio) || gpioCapability(gpio).reserved) return false;
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << gpio;
    cfg.mode = GPIO_MODE_INPUT_OUTPUT;
    cfg.intr_type = GPIO_INTR_DISABLE;
    return preloadThenEnable(
        [&]() { return gpio_set_level(static_cast<gpio_num_t>(gpio), high ? 1 : 0) == ESP_OK; },
        [&]() { return gpio_config(&cfg) == ESP_OK; });
}

bool gpioPreloadLowOutput(uint8_t gpio) { return gpioPreloadOutput(gpio, false); }

bool gpioHold(uint8_t gpio) {
    // The same refusals as gpioWrite: only a pin it may drive is held.
    if (!GPIO_IS_VALID_OUTPUT_GPIO(gpio) || gpioCapability(gpio).reserved) return false;
    // An earlier image's hold freezes its pad config (input enable too); released, the pad takes the registers' config and level, the same level, then holds again.
    const auto pad = static_cast<gpio_num_t>(gpio);
    return gpio_hold_dis(pad) == ESP_OK && gpio_hold_en(pad) == ESP_OK;
}

void setTestGpioLevel(uint8_t, bool) {}
void clearTestGpioLevel() {}
bool testGpioInputPull(uint8_t, GpioPull&) { return false; }

}  // namespace mm::platform
