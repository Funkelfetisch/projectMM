// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/util/JsonSink.h"
#include "core/util/sha256.h"
#include "helio/BleSceneData.h"

#include <cstdint>
#include <cstring>
#include <new>

namespace mm {

/// Local, opt-in session durations; no persistence, identifiers or transport lives here.
class ProductUsage {
public:
    static constexpr uint8_t kMaxRecords = 16;
    static constexpr uint8_t kBrightnessBins = 16;
    static constexpr size_t kMaxSettingsBytes = 4096;

    ProductUsage() = default;
    ~ProductUsage() { platform::free(data_); }
    ProductUsage(const ProductUsage&) = delete;
    ProductUsage& operator=(const ProductUsage&) = delete;

    /// Samples close the previous state's monotonic interval. Opt-out erases the local session.
    /// Settings contain only caller-allowlisted controls; their textual SHA-256/128 is not JSON canonicalisation.
    /// Call at least once per uint32 millisecond wrap; UTC input has one-second resolution.
    void record(bool enabled, uint32_t monotonicMs, uint64_t unixSeconds, const char* sceneID,
                const char* settingsJSON, uint8_t effectiveBrightness, bool powerOn) {
        optin_ = enabled;
        if (!enabled) { platform::free(data_); data_ = nullptr; allocationFailed_ = false; return; }
        if (!data_) {
            void* memory = platform::alloc(sizeof(Storage));
            if (!memory) { allocationFailed_ = true; return; }
            data_ = new (memory) Storage{};
        }
        auto& state = *data_;
        const bool clockKnown = unixSeconds > 0 && unixSeconds <= (UINT64_MAX - UINT32_MAX) / 1000u;
        const uint64_t utcMs = clockKnown ? unixSeconds * 1000u : 0;
        if (state.sampled) {
            const uint32_t elapsedMs = monotonicMs - state.monotonicMs;
            const bool expectedFits = state.utcCursorMs <= UINT64_MAX - elapsedMs;
            const uint64_t expected = expectedFits ? state.utcCursorMs + elapsedMs : 0;
            const uint64_t difference = utcMs > expected ? utcMs - expected : expected - utcMs;
            const bool dated = state.clockKnown && clockKnown && unixSeconds >= state.unixSeconds && expectedFits && difference < 1000u;
            if (!dated) {
                add(state.undatedMs, elapsedMs);
                if (state.clockKnown && clockKnown) add(state.clockDiscontinuityMs, elapsedMs);
                accumulate(0, false, elapsedMs);
                state.utcCursorMs = utcMs;
            } else {
                uint32_t remaining = elapsedMs;
                while (remaining) {
                    const uint64_t untilMidnight = kDayMs - state.utcCursorMs % kDayMs;
                    const uint32_t part = untilMidnight < remaining ? static_cast<uint32_t>(untilMidnight) : remaining;
                    accumulate(state.utcCursorMs / kDayMs, true, part);
                    state.utcCursorMs += part; remaining -= part;
                }
            }
        } else state.utcCursorMs = utcMs;
        state.sampled = true; state.monotonicMs = monotonicMs; state.clockKnown = clockKnown; state.unixSeconds = unixSeconds;
        state.previous.scene = allowedScene(sceneID);
        state.previous.hasSettings = false;
        if (settingsJSON) {
            size_t length = 0;
            while (length <= kMaxSettingsBytes && settingsJSON[length]) ++length;
            if (length <= kMaxSettingsBytes) {
                sha256Hex(settingsJSON, length, state.previous.settingsHash);
                state.previous.hasSettings = true;
            } else state.settingsTooLong = true;
        }
        state.previous.brightness = effectiveBrightness; state.previous.powerOn = powerOn;
    }

    /// Full session export; UTC days count from 1970-01-01, and a null day is explicitly undated.
    void report(JsonSink& sink) const {
        sink.append(optin_ ? "{\"optin\":true" : "{\"optin\":false");
        sink.append(",\"scope\":\"session\",\"utcDayUnit\":\"days-since-1970-01-01\",\"clockResolutionMs\":1000");
        sink.append(",\"settingsHashAlgorithm\":\"sha256-128-text\",\"brightnessBinWidth\":16,\"recordLimit\":16");
        sink.append(allocationFailed_ ? ",\"allocationFailed\":true" : ",\"allocationFailed\":false");
        sink.append(data_ && data_->saturated ? ",\"saturated\":true" : ",\"saturated\":false");
        sink.append(data_ && data_->settingsTooLong ? ",\"settingsTooLong\":true" : ",\"settingsTooLong\":false");
        sink.appendf(",\"onMs\":%lu,\"offMs\":%lu,\"undatedMs\":%lu,\"clockDiscontinuityMs\":%lu,\"overflowMs\":%lu,\"records\":[",
                     value(data_ ? data_->onMs : 0), value(data_ ? data_->offMs : 0), value(data_ ? data_->undatedMs : 0),
                     value(data_ ? data_->clockDiscontinuityMs : 0), value(data_ ? data_->overflowMs : 0));
        for (uint8_t i = 0; data_ && i < data_->count; ++i) {
            const auto& entry = data_->records[i];
            if (i) sink.append(",");
            sink.append("{\"utcDay\":");
            if (entry.dated) sink.appendf("%llu", static_cast<unsigned long long>(entry.day));
            else sink.append("null");
            sink.append(",\"scene\":"); if (entry.scene) sink.writeJsonString(entry.scene); else sink.append("null");
            sink.append(",\"settingsHash\":");
            if (entry.hasSettings) sink.writeJsonString(entry.settingsHash); else sink.append("null");
            sink.appendf(",\"onMs\":%lu,\"offMs\":%lu,\"brightnessHistogramMs\":[", value(entry.onMs), value(entry.offMs));
            for (uint8_t bin = 0; bin < kBrightnessBins; ++bin) {
                if (bin) sink.append(",");
                sink.appendf("%lu", value(entry.brightness[bin]));
            }
            sink.append("]}");
        }
        sink.append("]}");
    }

private:
    static constexpr uint64_t kDayMs = 86400000;
    struct Settings {
        const char* scene = nullptr;
        char settingsHash[33] = {};
        bool hasSettings = false;
    };
    struct Sample : Settings { uint8_t brightness = 0; bool powerOn = false; };
    struct Record : Settings {
        uint64_t day = 0;
        bool dated = false;
        uint32_t onMs = 0, offMs = 0;
        uint32_t brightness[kBrightnessBins] = {};
    };
    struct Storage {
        Sample previous;
        Record records[kMaxRecords];
        uint64_t utcCursorMs = 0, unixSeconds = 0;
        uint32_t monotonicMs = 0, onMs = 0, offMs = 0, undatedMs = 0, clockDiscontinuityMs = 0, overflowMs = 0;
        uint8_t count = 0;
        bool sampled = false, clockKnown = false, saturated = false, settingsTooLong = false;
    };
    Storage* data_ = nullptr;
    bool optin_ = false;
    bool allocationFailed_ = false;

    static unsigned long value(uint32_t number) { return static_cast<unsigned long>(number); }
    static const char* allowedScene(const char* id) {
        for (const auto& entry : product_scenes::kEntries)
            if (id && std::strcmp(id, entry.id) == 0) return entry.id;
        return nullptr;
    }
    void add(uint32_t& counter, uint32_t increment) {
        if (increment > UINT32_MAX - counter) { counter = UINT32_MAX; data_->saturated = true; }
        else counter += increment;
    }
    void accumulate(uint64_t day, bool dated, uint32_t duration) {
        if (!duration) return;
        const auto& previous = data_->previous;
        add(previous.powerOn ? data_->onMs : data_->offMs, duration);
        Record* destination = nullptr;
        for (uint8_t i = 0; i < data_->count; ++i) {
            auto& entry = data_->records[i];
            if (entry.dated == dated && (!dated || entry.day == day) && entry.scene == previous.scene &&
                entry.hasSettings == previous.hasSettings && (!entry.hasSettings || !std::strcmp(entry.settingsHash, previous.settingsHash))) {
                destination = &entry; break;
            }
        }
        if (!destination && data_->count < kMaxRecords) {
            destination = &data_->records[data_->count++];
            static_cast<Settings&>(*destination) = previous;
            destination->day = day; destination->dated = dated;
        }
        if (!destination) { add(data_->overflowMs, duration); return; }
        add(previous.powerOn ? destination->onMs : destination->offMs, duration);
        add(destination->brightness[previous.brightness >> 4u], duration);
    }
};

} // namespace mm
