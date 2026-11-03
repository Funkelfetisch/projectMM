#pragma once

#include "core/module/Scheduler.h"
#include "core/util/PinList.h"
#include "light/drivers/Mini80PowerPort.h"

namespace mm {
/// Conservative scan of declared GPIO controls and fixed pins before MINI80 cold initialization.
///
/// It is an observation, not a global pin lock: the owner must wait for every module to define its controls, serialize the scan with mutations and prevent later conflicting changes. Undeclared hardware use and physical electrical state are unknown.
class Mini80PinPreflight {
public:
    enum class Result : uint8_t { Clear, Conflict, Unknown };
    /// Scan every module's pin controls and fixed pins for the MINI80 data and enable pins: Conflict on a hit, Unknown when absence cannot be proven, Clear otherwise.
    static Result check(const Scheduler& scheduler, const MoonModule* owner, const MoonModule* ownedDriver = nullptr) {
        if (!owner) return Result::Unknown;
        bool found = false;
        Scratch scratch{};
        for (uint8_t i = 0; i < scheduler.moduleCount(); ++i) {
            const Result result = walk(scheduler.module(i), owner, ownedDriver, found, 0, scratch);
            if (result != Result::Clear) return result;
        }
        return found ? Result::Clear : Result::Unknown;
    }
private:
    struct Scratch { uint16_t pins[64] = {}; MoonModule::FixedPin fixed[16] = {}; };
    static bool target(uint16_t pin) {
        return pin == Mini80PowerPort::kDataPin || pin == Mini80PowerPort::kEnablePin;
    }
    static Result walk(MoonModule* module, const MoonModule* owner, const MoonModule* ownedDriver, bool& found, uint8_t depth, Scratch& scratch) {
        if (!module || depth >= 32) return Result::Unknown;
        if (module == owner) found = true;
        if (module != owner && module != ownedDriver) {
            // Include disabled and hidden controls: they could be enabled later.
            const ControlList& controls = module->controls();
            for (uint8_t i = 0; i < controls.count(); ++i) {
                const ControlDescriptor& control = controls[i];
                const bool list = control.type == ControlType::Text && isPinListControl(control.name);
                if (control.type != ControlType::Pin && !list) continue;
                if (!control.ptr) return Result::Unknown;
                if (control.type == ControlType::Pin) {
                    const int8_t pin = *static_cast<const int8_t*>(control.ptr);
                    if (pin >= 0 && target(static_cast<uint8_t>(pin))) return Result::Conflict;
                } else {
                    const char* text = static_cast<const char*>(control.ptr);
                    if (!*text) continue;
                    uint8_t count = 0;
                    if (parsePinList(text, scratch.pins, 64, count)) return Result::Unknown;
                    for (uint8_t p = 0; p < count; ++p) if (target(scratch.pins[p])) return Result::Conflict;
                }
            }
            const uint8_t count = module->fixedPins(scratch.fixed, 16);
            if (count >= 16) return Result::Unknown; // truncation cannot prove absence
            for (uint8_t p = 0; p < count; ++p) if (target(scratch.fixed[p].gpio)) return Result::Conflict;
        }
        for (uint8_t i = 0; i < module->childCount(); ++i) {
            const Result result = walk(module->child(i), owner, ownedDriver, found, static_cast<uint8_t>(depth + 1), scratch);
            if (result != Result::Clear) return result;
        }
        return Result::Clear;
    }
};
} // namespace mm
