#pragma once

#include "core/services/BleRemoteService.h"
#include "core/system/ControlModule.h"
#include "helio/BleSceneCatalog.h"
#include "helio/RemoteTimer.h"
#include "light/drivers/Drivers.h"
#include "core/util/color.h"

namespace mm {
/// HELIO and NEBULITE handset commands use native presets, the solid scene and a temporary output overlay. Explicit editable bindings take priority over the product commands.
/// @card HelioRemoteService.png
class HelioRemoteService : public BleRemoteService {
public:
    /// How long automatic mode holds a preset before stepping to the next.
    uint16_t autoIntervalSeconds = 30;
    /// Steps through the saved presets on a timer until a color, mode or OFF key stops it.
    bool automatic = false;
    /// The longest sleep timer; the press that would pass it clears the timer.
    uint16_t timerMaximumMinutes = 180;
    /// Which native preset groups the handset steps through: static, music, or both.
    uint8_t musicMode = 0;
    /// Add the auto-step, timer and music-mode controls to the handset's key list.
    void defineControls() override {
        BleRemoteService::defineControls();
        controls_.addControl("auto interval seconds", autoIntervalSeconds, 1, 65535);
        controls_.addControl("automatic", automatic);
        controls_.addControl("timer maximum minutes", timerMaximumMinutes, 0, 65535);
        static constexpr const char* modes[] = {"Static", "Music", "Static + music"};
        controls_.addSelect("music mode", musicMode, modes, 3);
        /// The minutes left on the sleep timer.
        controls_.addReadOnly("timer remaining", remaining_, sizeof(remaining_));
    }
    /// Re-check the preset group when the music mode changes.
    void onControlChanged(const char* name) override {
        BleRemoteService::onControlChanged(name);
        if (!std::strcmp(name, "music mode")) ensurePresetGroup();
    }
    /// Claim the timer seat and re-check the saved preset selection before the observer starts.
    void prepare() override { timer_.seat.claim(); selectionChecked_ = false; BleRemoteService::prepare(); }
    /// Cancel the timer and give up its seat before the observer stops.
    void release() override { timer_.cancel(); timer_.seat.vacate(); BleRemoteService::release(); }
    /// Run the timer and the auto-step, handle the queued keys, and refresh the remaining readout.
    void tick() MM_NONBLOCKING override {
        uint32_t now = platform::millis();
        if (timer_.advance(now)) {
            automatic = false;
            if (auto* scheduler = Scheduler::instance()) scheduler->setControl("Control", "switch1", "{\"value\":false}", true);
        }
        if (auto* drivers = Drivers::active(); drivers && !drivers->on && !timer_.visible(now)) automatic = false;
        timer_.seat.claim();
        BleRemoteService::tick();
        now = platform::millis();
        if (!selectionChecked_ && ControlModule::active()) {
            selectionChecked_ = true;
            ensurePresetGroup();
        }
        if (automatic && !timer_.visible(now) && !timer_.indicating(now) && now - autoAt_ >= uint32_t(autoIntervalSeconds) * 1000) {
            stepPreset(1); autoAt_ = now;
        }
        formatTo(remaining_, sizeof(remaining_), "%llu min", static_cast<unsigned long long>(timer_.minutes()));
    }
protected:
    void acceptedKey(RsKey key, uint32_t) override {
        if (key == RsKey::Off) { automatic = false; timer_.cancel(); }
    }
    InputAction keyAction(RsKey key, InputAction action, uint32_t now) override {
        if ((key == RsKey::Brighter || key == RsKey::Dimmer) && action.kind == InputAction::Kind::Delta)
            action.value = std::clamp(int(action.value) * repeatMultiplier(key, now), kMinActionValue, kMaxActionValue);
        return action;
    }
    bool handleKey(RsKey key, uint32_t now) override {
        auto* scheduler = Scheduler::instance();
        if (!scheduler) return false;
        switch (key) {
            case RsKey::Timer:
                if (!timer_.press(now, timerMaximumMinutes)) setStatus("timer capacity reached", Severity::Warning);
                else setStatus("timer: 18 red-to-green segments, 10 min each; 180 min then unset");
                return true;
            case RsKey::Music:
                musicMode = (musicMode + 1) % 3;
                ensurePresetGroup(); autoAt_ = now;
                if (musicMode == 0) {
                    const uint8_t rgb = needsRgbWhite() ? 255 : 0;
                    timer_.indicate(now, rgb, rgb, rgb, 255);
                } else timer_.indicate(now, musicMode == 2 ? 160 : 0, 0, 255, 0);
                return true;
            case RsKey::Auto:
                automatic = !automatic; autoAt_ = now;
                if (automatic) ensurePresetGroup();
                timer_.indicate(now, automatic ? 0 : 255, automatic ? 255 : 0, 0, 0);
                return true;
            case RsKey::ModeUp: case RsKey::ModeDown:
                automatic = false; stepPreset(key == RsKey::ModeUp ? 1 : -1); return true;
            case RsKey::SpeedUp: case RsKey::SpeedDown: {
                auto* effect = scheduler->firstByName("BLE Scene");
                if (!effect) return true;
                const ControlDescriptor* speed = nullptr;
                for (uint8_t i = 0; i < effect->controls().count(); ++i)
                    if (!std::strcmp(effect->controls()[i].name, "speed")) speed = &effect->controls()[i];
                if (!speed) { setStatus("this preset has no speed control"); return true; }
                int32_t current = 0; scheduler->getControlWide("BLE Scene", "speed", current);
                char value[32];
                formatTo(value, sizeof(value), "{\"value\":%d}", std::clamp(int(current) + (key == RsKey::SpeedUp ? 16 : -16) * repeatMultiplier(key, now), int(speed->min), int(speed->max)));
                scheduler->setControl("BLE Scene", "speed", value, true); return true;
            }
            case RsKey::ColorUp: case RsKey::ColorDown: {
                auto* effect = scheduler->firstByName("BLE Scene");
                if (effect && std::strcmp(effect->typeName(), "SolidEffect")) {
                    stepPalette(*scheduler, *effect, key == RsKey::ColorUp ? 1 : -1);
                    return true;
                }
                // Only accepted counter changes advance hue; retransmitted bursts are not fabricated holds.
                const int step = 3 * repeatMultiplier(key, now);
                hue_ = static_cast<uint8_t>(hue_ + (key == RsKey::ColorUp ? step : -step));
                const auto rgb = hsvToRgb(hue_, 255, 255);
                solid(rgb.r, rgb.g, rgb.b, 0); return true;
            }
            case RsKey::White: {
                const uint8_t rgb = needsRgbWhite() ? 255 : 0;
                solid(rgb, rgb, rgb, 255); return true;
            }
            case RsKey::Yellow: hue_ = 43; solid(255, 255, 0, 0); return true;
            case RsKey::Cyan: hue_ = 128; solid(0, 255, 255, 0); return true;
            case RsKey::Pink: hue_ = 213; solid(255, 0, 255, 0); return true;
            case RsKey::Red: hue_ = 0; solid(255, 0, 0, 0); return true;
            case RsKey::Green: hue_ = 85; solid(0, 255, 0, 0); return true;
            case RsKey::Blue: hue_ = 171; solid(0, 0, 255, 0); return true;
            default: return false;
        }
    }
private:
    /// True when white is mixed from RGB: garments combine RGB strips and a separate white accent in one four-channel layer, so it resolves from output fixtures, which layer width alone cannot tell from RGBW strips.
    bool needsRgbWhite() const {
        bool physicalColor = false;
        if (auto* drivers = Drivers::active()) {
            for (uint8_t i = 0; i < drivers->childCount(); ++i) {
                auto* child = drivers->child(i);
                if (child->role() != ModuleRole::Driver || !child->enabled()) continue;
                auto* output = static_cast<DriverBase*>(child);
                if (!output->hasCorrectionControls()) continue;
                const auto& c = output->correction();
                if (c.offRed == Correction::kAbsent && c.offGreen == Correction::kAbsent && c.offBlue == Correction::kAbsent) continue;
                physicalColor = true;
                if (c.offWhite == Correction::kAbsent) return true;
            }
        }
        if (physicalColor) return false;
        auto* scheduler = Scheduler::instance();
        auto* effects = scheduler ? scheduler->firstByName("Effects") : nullptr;
        if (effects) for (uint8_t i = 0; i < effects->childCount(); ++i) {
            auto* child = effects->child(i);
            if (child->role() == ModuleRole::Layer && child->enabled() && static_cast<Layer*>(child)->channelsPerLight() == 3) return true;
        }
        return false;
    }
    RemoteTimer timer_;
    uint64_t audioRows_ = 0, lookRows_ = 0;
    uint32_t presetRevision_ = UINT32_MAX;
    bool selectionChecked_ = false;
    void refreshGroups(ControlModule& control) {
        if (presetRevision_ == control.presetsRevision()) return;
        audioRows_ = lookRows_ = 0;
        for (uint8_t row = 0; row < control.presetCount(); ++row) {
            if (!control.isLookOnly(row)) continue;
            PresetStore::File file;
            if (!PresetStore::read(control.presetName(row), file)) continue;
            json::JsonDoc doc;
            if (!json::parse(file.text, doc)) continue;
            lookRows_ |= uint64_t(1) << row;
            for (int i = 0; i < doc.count; ++i) {
                const auto& node = doc.nodes[i];
                if (!node.key || std::strcmp(node.key, "type") || node.type != json::JsonType::String) continue;
                for (const auto& entry : product_scenes::kEntries)
                    if (entry.audio && !std::strcmp(entry.type, node.str)) audioRows_ |= uint64_t(1) << row;
            }
        }
        presetRevision_ = control.presetsRevision();
    }
    bool eligible(uint8_t row) const {
        const uint64_t bit = uint64_t(1) << row;
        return (lookRows_ & bit) && (musicMode == 2 || bool(audioRows_ & bit) == (musicMode == 1));
    }
    void ensurePresetGroup() {
        auto* control = ControlModule::active();
        if (!control) return;
        refreshGroups(*control);
        for (uint8_t row = 0; row < control->presetCount(); ++row)
            if (!std::strcmp(control->presetName(row), control->currentLook()) && eligible(row)) return;
        stepPreset(1);
    }
    void stepPalette(Scheduler& scheduler, MoonModule& effect, int direction) {
        const ControlDescriptor* palette = nullptr;
        for (uint8_t i = 0; i < effect.controls().count(); ++i)
            if (!std::strcmp(effect.controls()[i].name, "palette")) palette = &effect.controls()[i];
        const char* target = "BLE Scene";
        if (!palette) {
            auto* drivers = Drivers::active();
            if (!drivers) return;
            for (uint8_t i = 0; i < drivers->controls().count(); ++i)
                if (!std::strcmp(drivers->controls()[i].name, "palette")) palette = &drivers->controls()[i];
            target = "Drivers";
        }
        if (!palette || palette->max <= palette->min) { setStatus("this preset has no palette control"); return; }
        int32_t current = 0; scheduler.getControlWide(target, "palette", current);
        const int first = palette->min, count = palette->max - first;
        char value[32];
        formatTo(value, sizeof(value), "{\"value\":%d}", first + (int(current) - first + direction + count) % count);
        scheduler.setControl(target, "palette", value, true);
    }
    uint32_t autoAt_ = 0, repeatAt_ = 0;
    uint8_t hue_ = 0;
    int repeatRun_ = 0;
    RsKey lastRepeatKey_ = RsKey::Unknown;
    int repeatMultiplier(RsKey key, uint32_t now) {
        repeatRun_ = lastRepeatKey_ == key && now - repeatAt_ < 350 ? std::min(repeatRun_ + 1, 20) : 0;
        lastRepeatKey_ = key; repeatAt_ = now;
        return repeatRun_ >= 3 ? 4 : 1;
    }
    char remaining_[40] = "0 min";
    void stepPreset(int direction) {
        auto* control = ControlModule::active();
        if (!control || !control->presetCount()) { setStatus("no presets available"); return; }
        int row = direction > 0 ? -1 : 0;
        for (uint8_t i = 0; i < control->presetCount(); ++i) {
            if (std::strcmp(control->presetName(i), control->currentLook()) == 0) { row = i; break; }
        }
        refreshGroups(*control);
        for (uint8_t tried = 0; tried < control->presetCount(); ++tried) {
            row = (row + direction + control->presetCount()) % control->presetCount();
            if (!eligible(static_cast<uint8_t>(row))) continue;
            control->applyLookByName(control->presetName(static_cast<uint8_t>(row)));
            return;
        }
        setStatus("no saved presets in this music mode");
    }
    void solid(uint8_t red, uint8_t green, uint8_t blue, uint8_t white) {
        automatic = false;
        auto* scheduler = Scheduler::instance(); if (!scheduler) return;
        char values[120];
        formatTo(values, sizeof(values), "{\"red\":%u,\"green\":%u,\"blue\":%u,\"white\":%u,\"brightness\":255}", red, green, blue, white);
        const auto result = BleSceneCatalog::apply(*scheduler, "solid", BleSceneCatalog::revision(), values, true);
        if (!result.ok) setStatus(result.error, Severity::Warning);
    }
};
} // namespace mm
