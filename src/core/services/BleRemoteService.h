#pragma once

#include "core/module/MoonModule.h"
#include "core/util/InputMapping.h"
#include "core/util/RsRemote.h"
#include "core/system/FilesystemModule.h"
#include "platform/platform.h"
#include <memory>
#include <new>

namespace mm {

/// An SP648E RS-protocol handset driving the existing control surface through passive BLE observation.
/// @guide
/// Add this service to Services. ON and OFF set Control.switch1, while brightness nudges Control.fader1.
/// Every recognized key has a configurable binding; the other keys start unassigned and can fire pads or change surface controls.
/// A color key names the handset key; it does not force a light effect or edit a palette behind the control surface.
/// @technical
/// Reception shares the platform's NimBLE host with GATT. Raw advertisements cross a bounded queue before decoding;
/// neither the BLE callback nor control dispatch depends on USB. Duplicate bursts produce one action, including bursts over 800 ms.
/// Frames are structurally recognized without authentication or a decoded application checksum. Long-hold behavior is not established.
class BleRemoteService : public MoonModule, public ListSource {
public:
    BleRemoteService() {
        actions_[0] = {1, 1, InputAction::Kind::Set, 1};
        actions_[1] = {1, 1, InputAction::Kind::Set, 0};
        actions_[2] = {3, 1, InputAction::Kind::Delta, 16};
        actions_[3] = {3, 1, InputAction::Kind::Delta, -16};
    }
    ModuleRole role() const MM_NONBLOCKING override { return ModuleRole::Service; }
    void defineControls() override {
        controls_.addList("keys", *this);
        controls_.addReadOnly("reception", reception_, sizeof(reception_));
        MoonModule::defineControls();
    }
    void prepare() override {
        if (!platform::bleObserverStart(receiver_)) { setStatus("BLE observer unavailable or already in use", Severity::Warning); return; }
        if (!decoder_) decoder_.reset(new (std::nothrow) RsRemoteDecoder());
        if (!decoder_) { platform::bleObserverStop(receiver_); setStatus("receiver allocation failed", Severity::Error); return; }
        setStatus("waiting for a remote key");
    }
    void release() override {
        if (!platform::bleObserverStop(receiver_)) { setStatus("BLE host stop failed; cleanup retained for retry", Severity::Error); return; }
        decoder_.reset();
    }
    void tick() MM_NONBLOCKING override {
        if (!decoder_) prepare();
        else if (!platform::bleObserverStart(receiver_)) return; // reacquire after the shared GATT host restarts
        platform::BleAdvertisement report;
        for (unsigned i = 0; i < 32 && platform::bleObserverReceive(receiver_, report); ++i)
            process({report.data, report.length}, report.receivedMs, platform::millis());
        formatTo(reception_, sizeof(reception_), "%lu events, %lu queue drops", static_cast<unsigned long>(events_),
                 static_cast<unsigned long>(platform::bleObserverDropped(receiver_)));
    }
    uint8_t listRowCount() const override { return static_cast<uint8_t>(actions_.size()); }
    bool isEditableList() const override { return true; }
    bool listRowsFixed() const override { return true; }
    void writeListRow(JsonSink& sink, uint8_t row) const override {
        if (row >= actions_.size()) { sink.append("{}"); return; }
        sink.appendf("{\"id\":%u,\"key\":\"%s\"", static_cast<unsigned>(row + 1), kRsKeyNames[row]);
        writeInputActionFields(sink, actions_[row]); sink.append("}");
    }
    void writeListRowDetail(JsonSink& sink, uint8_t row) const override {
        if (row >= actions_.size()) { sink.append("{}"); return; }
        sink.append("{\"fields\":[");
        // These are absolute commands: Set intentionally persists, with no fabricated release.
        writeInputActionDetailFields(sink, actions_[row]); sink.append("]}");
    }
    void writeListOptionSets(JsonSink& sink) const override { writeInputTargetOptions(sink); }
    bool setListRowField(uint32_t id, const char* field, const char* valueJson) override {
        if (id < 1 || id > actions_.size() || !setInputActionField(actions_[id - 1], field, valueJson)) return false;
        if (auto* fs = FilesystemModule::instance()) fs->noteDirty();
        return true;
    }
    bool restoreList(const char* text, const char* key) override {
        unsigned dropped = 0;
        const bool ok = json::forEachListElement(text, key, [&](const json::JsonDoc& doc, const json::JsonNode* row) {
            char name[20] = {};
            json::readString(json::member(doc, row, "key"), name, sizeof(name));
            for (size_t i = 0; i < kRsKeyNames.size(); ++i)
                if (std::strcmp(name, kRsKeyNames[i]) == 0 && !readInputAction(doc, row, actions_[i])) ++dropped;
        });
        reportOffSurface(*this, status_, sizeof(status_), dropped);
        return ok;
    }
    /// Feed captured payloads with their original timing on the desktop, where no physical radio exists.
    void injectAdvertisementForTest(std::span<const uint8_t> data, uint32_t nowMs) {
        injectAdvertisementForTest(data, nowMs, nowMs);
    }
    /// Exercise radio queue latency independently from the received packet's timestamp.
    void injectAdvertisementForTest(std::span<const uint8_t> data, uint32_t receivedMs, uint32_t processedMs) {
        if (!decoder_) decoder_ = std::make_unique<RsRemoteDecoder>();
        process(data, receivedMs, processedMs);
    }
protected:
    virtual void acceptedKey(RsKey, uint32_t) {}
    virtual bool handleKey(RsKey, uint32_t) { return false; }
    virtual InputAction keyAction(RsKey, InputAction action, uint32_t) { return action; }
private:
    void process(std::span<const uint8_t> data, uint32_t nowMs, uint32_t actionMs) {
        RsRemoteEvent event;
        if (!decoder_ || !decoder_->receive(data, nowMs, event)) return;
        ++events_;
        acceptedKey(event.key, actionMs);
        const size_t index = static_cast<size_t>(event.key);
        if (index >= actions_.size()) { setStatus("unmapped RS command received"); return; }
        if (!actions_[index].assigned() && handleKey(event.key, actionMs)) return;
        if (!actions_[index].assigned()) {
            formatTo(status_, sizeof(status_), "%s (unassigned)", kRsKeyNames[index]); setStatus(status_); return;
        }
        status_[0] = 0;
        if (runInputAction(keyAction(event.key, actions_[index], nowMs), true, status_, sizeof(status_)) || status_[0]) setStatus(status_);
    }
    std::array<InputAction, 20> actions_{};
    platform::BleObserverHandle receiver_;
    std::unique_ptr<RsRemoteDecoder> decoder_;
    uint32_t events_ = 0;
    char status_[80] = {};
    char reception_[64] = {};
};
}  // namespace mm
