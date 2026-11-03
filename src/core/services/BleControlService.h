#pragma once

#include "core/module/MoonModule.h"
#include "core/module/Scheduler.h"
#include "core/module/StateDocument.h"
#include "core/util/BleMessage.h"
#include "core/util/BleAdvertisement.h"
#include "core/util/BleRpcSession.h"
#include "core/util/BleRpcPeers.h"
#include "core/util/BleSceneApi.h"
#include "core/util/JsonSink.h"
#include "core/util/JsonUtil.h"
#include "core/util/LightOutput.h"
#include "core/util/ScratchBuffer.h"
#include "core/services/Bh1750Service.h"
#include "core/services/BatteryService.h"
#include "platform/platform.h"

#include <charconv>
#include <cstdio>
#include <cstring>
#include <new>

namespace mm {

/// Epoch-scoped BLE requests with explicit live state and named scheduled operations.
/// @technical The framing, session and complete-scene contract lives in reference/product-ble.md.
class BleControlService : public MoonModule {
public:
    ModuleRole role() const MM_NONBLOCKING override { return ModuleRole::Service; }
    /// Persisted group membership; empty leaves the group.
    char group[33] = {};
    ~BleControlService() override {
        if (!platform::bleControlStop(transport_)) platform::bleControlAbandon(transport_);
        clearRpc();
    }
    void setScheduler(Scheduler* scheduler) { scheduler_ = scheduler; }
    void setIdentity(const char* firmware, const char* build, const char* version = "", const char* builtAt = "") {
        firmware_ = firmware; build_ = build; version_ = version; builtAt_ = builtAt;
    }
    /// Product callbacks supply scenes without making the core depend on a light-domain catalog.
    void setScenes(const BleSceneApi* scenes) { scenes_ = scenes; }
    /// Product-supplied, read-only page of configured output coordinates.
    void setPixelMapReader(const char* (*reader)(JsonSink&, Scheduler&, uint16_t, const char*)) { pixelMapReader_ = reader; }
    /// Product-supplied, read-only configured output windows.
    void setOutputReader(const char* (*reader)(JsonSink&, Scheduler&, uint16_t, const char*)) { outputReader_ = reader; }
    /// Product-supplied, bounded snapshot of rendered output colors.
    void setPreviewFrameReader(const char* (*reader)(JsonSink&, Scheduler&, uint16_t, const char*, uint32_t)) { previewFrameReader_ = reader; }
    /// The product owner supplies installed profile metadata; an absent profile remains unknown.
    void setAdvertisingProduct(BleAdvertisementProduct product) { advertisingProduct_ = product; }
    void setAdvertisingProductResolver(uint8_t (*resolve)(const char*)) { advertisingProductResolver_ = resolve; }
    void setAdvertisingScene(void (*observe)(BleAdvertisementSnapshot&, Scheduler&)) { advertisingScene_ = observe; }

    void defineControls() override {
        controls_.addText("group", group, sizeof(group));
        controls_.addReadOnly("connection", connection_, sizeof(connection_));
        controls_.addReadOnly("batteryServices", batteryServices_, sizeof(batteryServices_));
        controls_.addReadOnly("productProfile", profileText_, sizeof(profileText_));
        controls_.addReadOnly("pairing", pairing_, sizeof(pairing_));
        controls_.addReadOnly("link diagnostics", linkDiagnostics_, sizeof(linkDiagnostics_));
        /// Authorize one stale-bond replacement for three minutes; other phones keep their bonds.
        controls_.addButton("repairPairing");
        MoonModule::defineControls();
    }
    void prepare() override {
        if (transport_.impl) return;
        if (!scheduler_) scheduler_ = Scheduler::instance();
        auto* battery = scheduler_ ? BatteryService::discover(*scheduler_) : nullptr;
        const auto batteries = battery ? battery->batteryConfig() : platform::BleBatteryConfig{};
        char identity[256]; JsonSink sink(identity, sizeof(identity));
        sink.append("{\"v\":1,\"rpc\":2,\"firmware\":"); sink.writeJsonString(firmware_);
        sink.append(",\"build\":"); sink.writeJsonString(build_);
        sink.append(",\"id\":"); sink.writeJsonString(platform::macString());
        sink.append(",\"maxRequest\":512,\"wifiRequired\":false,\"wifiToggle\":true,\"bleOta\":false,\"groups\":\"scheduled-patch\",\"ota\":\"http-app\",\"batteryOp\":true,\"security\":\"open\"");
        sink.appendf(",\"bas\":%u}", static_cast<unsigned>(batteries.count));
        if (sink.overflowed()) { setStatus("BLE identity too large", Severity::Error); return; }
        if (!platform::bleControlStart(transport_, "NEBULITE HELIO", identity, batteries)) {
            startedBatteries_ = {}; setStatus("BLE unavailable", Severity::Warning); return;
        }
        std::array<uint8_t, 16> bytes{};
        if (!platform::bleControlEpoch(transport_, bytes)) {
            platform::bleControlStop(transport_); setStatus("BLE epoch unavailable", Severity::Error); return;
        }
        static constexpr char hex[] = "0123456789abcdef";
        for (size_t i = 0; i < bytes.size(); ++i) {
            epoch_[2*i] = hex[bytes[i] >> 4]; epoch_[2*i+1] = hex[bytes[i] & 15];
        }
        epoch_[32] = '\0'; epochReady_ = true;
        clearRpc(); pendingText_.resize(0); job_ = {}; terminal_ = {}; activeOwner_ = 0;
        startedBatteries_ = batteries; revision_ = 0;
        setStatus("BLE RPC2 ready");
        replySession_ = 0; replyGeneration_ = 0; std::snprintf(replyOperation_, sizeof(replyOperation_), "%s", "ready");
        result(0, "session-required");
    }
    void release() override {
        if (!platform::bleControlStop(transport_)) {
            setStatus("BLE shutdown incomplete; radio context retained", Severity::Error);
            return;
        }
        clearRpc(); pendingText_.resize(0); job_ = {}; terminal_ = {}; epochReady_ = false; activeOwner_ = 0;
        MoonModule::release();
    }
    bool affectsPrepare(const char*) const override { return false; }
    void onControlChanged(const char* name) override {
        if (std::strcmp(name, "repairPairing") == 0 && !platform::bleControlAllowRepair(transport_))
            setStatus("BLE unavailable for pairing repair", Severity::Warning);
        MoonModule::onControlChanged(name);
    }
    void tick() MM_NONBLOCKING override {
        std::array<uint64_t, platform::kBleControlPeerCapacity> owners{}; size_t ownerCount = 0;
        if (rpc_ && platform::bleControlOwners(transport_, owners.data(), owners.size(), ownerCount))
            rpc_[0].releaseInactive(owners.data(), ownerCount);
        // A contended transport must not let a later request or timer overwrite an unpublished reply.
        if (!flushReply()) { MoonModule::tick(); return; }
        // One clock sample defines the ordering boundary for this entire tick.
        // A deadline crossed while receiving is handled on the next tick, so
        // handle() cannot create an unpublished terminal reply and overwrite it.
        const uint32_t now = platform::millis();
        applyDue(now);
        if (!flushReply()) { MoonModule::tick(); return; }
        char request[BleMessage::kCapacity + 1];
        if (platform::bleControlReceive(transport_, request, sizeof(request), replyGeneration_)) {
            handle(request, now, true);
        }
        flushReply();
        MoonModule::tick();
    }
    /// Resolve the same explicitly configured product hint for scan and RPC.
    BleAdvertisementProduct productHint() {
        auto product = advertisingProduct_;
        if (scheduler_) {
            if (advertisingProductResolver_) {
                const char* model = "";
                for (uint8_t i = 0; i < scheduler_->moduleCount(); ++i) {
                    auto* root = scheduler_->module(i);
                    if (std::strcmp(root->name(), "System") != 0) continue;
                    for (uint8_t c = 0; c < root->controls().count(); ++c) {
                        const auto& control = root->controls()[c];
                        if (std::strcmp(control.name, "deviceModel") == 0 && control.type == ControlType::Text)
                            model = static_cast<const char*>(control.ptr);
                    }
                }
                product.code = advertisingProductResolver_(model);
            }
            int32_t diameter = 0;
            if ((product.code == 1 || product.code == 2) &&
                scheduler_->getControlWide("System", "physicalDiameterCm", diameter) && diameter >= 0 && diameter <= 255)
                product.diameterCm = static_cast<uint8_t>(diameter);
        }
        return product;
    }
    BleAdvertisementSnapshot advertisementSnapshot() {
        BleAdvertisementSnapshot scan;
        scan.product = productHint();
        if (const auto* output = LightOutput::active()) {
            scan.ledCount = output->summary().lightCount;
            const uint8_t channels = output->summary().channelsPerLight;
            scan.rgbwKnown = channels == 3 || channels == 4;
            scan.rgbw = channels == 4;
        }
        if (scheduler_) {
            int32_t power = 0;
            scan.powerKnown = scheduler_->getControlWide("Drivers", "on", power);
            scan.powerOn = power != 0;
            if (advertisingScene_) advertisingScene_(scan, *scheduler_);
        }
        return scan;
    }
    void tick1s() MM_NONBLOCKING override {
        const auto advertisement = advertisementSnapshot();
        JsonSink profileSink(profileText_, sizeof(profileText_));
        writeProductProfile(profileSink, advertisement.product);
        if (profileSink.overflowed()) std::snprintf(profileText_, sizeof(profileText_), "%s", "null");
        platform::bleControlPublishAdvertisement(transport_, encodeBleAdvertisement(advertisement));
        platform::BlePairingState pairing;
        if (platform::bleControlPairingState(transport_, pairing))
            std::snprintf(pairing_, sizeof(pairing_), "repeat %lu; deleted %lu; rejected %lu; repair %lus; full %lu; denied %lu; overflow %lu; unknown %lu",
                          static_cast<unsigned long>(pairing.requests), static_cast<unsigned long>(pairing.deleted),
                          static_cast<unsigned long>(pairing.rejected),
                          static_cast<unsigned long>((pairing.repairWindowMs + 999u) / 1000u),
                          static_cast<unsigned long>(pairing.fullAllowed), static_cast<unsigned long>(pairing.fullRejected),
                          static_cast<unsigned long>(pairing.overflows), static_cast<unsigned long>(pairing.fullUnknown));
        else std::snprintf(pairing_, sizeof(pairing_), "%s", "unavailable");
        platform::BleLinkState link;
        if (platform::bleControlLinkState(transport_, link))
            std::snprintf(linkDiagnostics_, sizeof(linkDiagnostics_), "active %u/%u; accepted %lu; failed %lu; disconnected %lu; reason %ld; update %ld; no peer %lu; bad op %lu",
                          static_cast<unsigned>(link.activeConnections), static_cast<unsigned>(platform::kBleControlPeerCapacity),
                          static_cast<unsigned long>(link.accepted), static_cast<unsigned long>(link.failed),
                          static_cast<unsigned long>(link.disconnected), static_cast<long>(link.lastDisconnectReason),
                          static_cast<long>(link.lastUpdateStatus), static_cast<unsigned long>(link.missingPeerReads),
                          static_cast<unsigned long>(link.unsupportedAccess));
        else std::snprintf(linkDiagnostics_, sizeof(linkDiagnostics_), "%s", "unavailable");
        std::snprintf(connection_, sizeof(connection_), "%s", !transport_.impl ? "unavailable" :
                      platform::bleControlConnected(transport_) ? "connected" : "advertising");
        auto* battery = scheduler_ ? BatteryService::discover(*scheduler_) : nullptr;
        platform::bleControlPublishBattery(transport_, battery ? battery->snapshot(platform::millis()) : platform::BleBatterySnapshot{});
        const auto current = battery ? battery->batteryConfig() : platform::BleBatteryConfig{};
        std::snprintf(batteryServices_, sizeof(batteryServices_), current == startedBatteries_ ? "%u BAS channels" : "%u BAS channels; restart required",
                      static_cast<unsigned>(startedBatteries_.count));
        MoonModule::tick1s();
    }
#ifndef ESP_PLATFORM
    void setEpochForTest(const char* epoch) {
        BleRpcSession probe;
        if (!probe.beginEpoch(epoch)) return;
        std::snprintf(epoch_, sizeof(epoch_), "%s", epoch); epochReady_ = true;
        clearRpc(); pendingText_.resize(0); job_ = {}; terminal_ = {}; activeOwner_ = 0;
    }
    void setReplyGenerationForTest(uint64_t generation) {
        if (rpc_ && activeOwner_ && activeOwner_ != generation) rpc_[0].release(activeOwner_);
        replyGeneration_ = generation;
    }
    void setConcurrentGenerationForTest(uint64_t generation) { replyGeneration_ = generation; }
    void reconcileOwnersForTest(const uint64_t* owners, size_t count) { if (rpc_) rpc_[0].releaseInactive(owners, count); }
    uint64_t replyGenerationForTest() const { return replyGeneration_; }
#endif
    /// All reads and writes traverse the same correlation gate before domain dispatch.
    void handle(const char* request, uint32_t now, bool dueAlreadyProcessed = false) {
        const uint64_t requestGeneration = replyGeneration_;
        if (!dueAlreadyProcessed) applyDue(now);
        replyGeneration_ = requestGeneration;
        const uint64_t owner = requestGeneration ? requestGeneration : UINT64_MAX;
        activeOwner_ = owner;
        replySession_ = 0; std::snprintf(replyOperation_, sizeof(replyOperation_), "%s", "error");
        json::JsonDoc doc;
        if (!request || std::strlen(request) > BleMessage::kCapacity || !json::parse(request, doc) ||
            !doc.rootNode() || doc.rootNode()->type != json::JsonType::Object || !uniqueMetadata(doc)) {
            result(0, "malformed"); return;
        }
        uint32_t version = 0, id = 0;
        number(doc, "$s", replySession_);
        const char* op = text(doc, "$op");
        if (op && std::strlen(op) < sizeof(replyOperation_)) std::snprintf(replyOperation_, sizeof(replyOperation_), "%s", op);
        if (!number(doc, "$v", version) || version != 1 || !number(doc, "$id", id) || id == 0 || id > 65535) {
            result(0, "version-or-id"); return;
        }
        if (!op || std::strlen(op) >= sizeof(replyOperation_)) { result(id, "operation"); return; }
        if (std::strcmp(op, "hello") == 0) { hello(doc, id); return; }
        auto* session = rpc_ ? rpc_[0].find(activeOwner_) : nullptr;
        if (!session || !text(doc, "$e") || !replySession_) { result(id, "session-required"); return; }
        const auto decision = session->accept(text(doc, "$e"), replySession_, static_cast<uint16_t>(id), request);
        if (decision == BleRpcSession::Decision::Retry) {
            if (!session->cachedReply()[0]) { result(id, "receipt-unavailable"); return; }
            std::snprintf(response_, sizeof(response_), "%s", session->cachedReply()); replyDirty_ = true; return;
        }
        if (decision != BleRpcSession::Decision::Accept) {
            const char* error = decision == BleRpcSession::Decision::SessionExpired ? "session-expired" :
                decision == BleRpcSession::Decision::IdReused ? "id-reused" :
                decision == BleRpcSession::Decision::IdExpired ? "id-expired" : "correlation";
            result(id, error); return;
        }
        dispatch(doc, request, static_cast<uint16_t>(id), now);
        session->cacheReply(response_);
    }
    /// Apply a due operation before dispatching a cancel at the same instant, and observe completed frame preparation.
    void applyDue(uint32_t now) {
        if (job_.phase == Phase::Applying && scheduler_ && scheduler_->preparationRevision() != job_.preparedAt) {
            if (scheduler_->failedPreparationRevision() != job_.failedAt) finish("preparation-failed");
            else if (!scheduler_->preparationPending()) finish(nullptr);
        }
        if (job_.phase != Phase::Queued || static_cast<int32_t>(now - job_.due) < 0) return;
        job_.phase = Phase::Applying;
        apply(pendingText_.data()); pendingText_.resize(0);
    }
    const char* response() const { return response_; }
    bool scheduled() const { return job_.phase == Phase::Queued; }

private:
    bool flushReply() {
        if (!replyDirty_) return true;
        if (!platform::bleControlPublish(transport_, response_, revision_, replyGeneration_)) return false;
        replyDirty_ = false;
        return true;
    }
    enum class Phase : uint8_t { None, Queued, Applying, Applied, Cancelled, Failed };
    struct Job {
        uint64_t generation = 0;
        uint32_t session = 0, due = 0, preparedAt = 0, failedAt = 0;
        uint16_t id = 0;
        Phase phase = Phase::None;
        bool live = true;
        char operation[16] = {}, error[64] = {};
    };
    static const char* phaseName(Phase phase) {
        switch (phase) {
            case Phase::Queued: return "queued";
            case Phase::Applying: return "applying";
            case Phase::Applied: return "applied";
            case Phase::Cancelled: return "cancelled";
            case Phase::Failed: return "failed";
            default: return "not-retained";
        }
    }
    static const char* text(const json::JsonDoc& doc, const char* key) {
        const auto* node = json::member(doc, doc.rootNode(), key);
        return node && node->type == json::JsonType::String ? node->str : nullptr;
    }
    static bool uniqueMetadata(const json::JsonDoc& doc) {
        for (int a = doc.rootNode()->firstChild; a >= 0; a = doc.nodes[a].next) {
            if (!doc.nodes[a].key || doc.nodes[a].key[0] != '$') continue;
            for (int b = doc.nodes[a].next; b >= 0; b = doc.nodes[b].next)
                if (doc.nodes[b].key && std::strcmp(doc.nodes[a].key, doc.nodes[b].key) == 0) return false;
        }
        return true;
    }
    bool resumeFields(const json::JsonDoc& doc, const char*& epoch, uint32_t& session) {
        const auto* resume = json::member(doc, doc.rootNode(), "$resume");
        if (!resume) return true;
        if (resume->type != json::JsonType::Object) return false;
        const auto* e = json::member(doc, resume, "e"); const auto* s = json::member(doc, resume, "s");
        if (!e || e->type != json::JsonType::String || !s || s->type != json::JsonType::Int || s->decimal || !s->str) return false;
        const auto parsed = std::from_chars(s->str, s->str + s->numLen, session);
        epoch = e->str;
        return parsed.ec == std::errc{} && parsed.ptr == s->str + s->numLen && session != 0;
    }
    void clearRpc() {
        using Peers = BleRpcPeers<platform::kBleControlPeerCapacity>;
        if (rpc_) rpc_[0].~Peers();
        rpc_.resize(0);
    }
    void hello(const json::JsonDoc& doc, uint16_t id) {
        const char* nonce = text(doc, "$nonce"); const char* resumeEpoch = nullptr; uint32_t resumeSession = 0;
        if (!fields(doc, "hello")) { helloResult(id, "fields", nonce); return; }
        if (!epochReady_) { helloResult(id, "epoch-unavailable", nonce); return; }
        if (!resumeFields(doc, resumeEpoch, resumeSession)) { helloResult(id, "resume", nonce); return; }
        if (!BleRpcSession::validChallenge(nonce)) { helloResult(id, "nonce", nonce); return; }
        if (!rpc_) {
            if (!rpc_.resize(1)) { helloResult(id, "memory", nonce); return; }
            new (rpc_.data()) BleRpcPeers<platform::kBleControlPeerCapacity>(); rpc_[0].beginEpoch(epoch_);
        }
        auto* session = rpc_[0].find(activeOwner_);
        if (!session && resumeEpoch) session = rpc_[0].resume(activeOwner_, resumeEpoch, resumeSession, nonce);
        else if (!session) {
            session = rpc_[0].retryChallenge(activeOwner_, nonce);
            if (!session) session = rpc_[0].create(activeOwner_);
        }
        if (!session) { helloResult(id, resumeEpoch ? "session-expired" : "memory", nonce); return; }
        const char* error = session->hello(nonce, resumeEpoch, resumeSession, rpc_[0].counter());
        replySession_ = error ? resumeSession : session->session();
        if (error) { helloResult(id, error, nonce); return; }
        JsonSink sink(response_, sizeof(response_)); begin(sink, id, nullptr);
        sink.append(",\"nonce\":"); sink.writeJsonString(session->nonce());
        sink.appendf(",\"hi\":%u", static_cast<unsigned>(session->highWater())); end(sink, id);
    }
    void helloResult(uint16_t id, const char* error, const char* nonce) {
        JsonSink sink(response_, sizeof(response_)); begin(sink, id, error);
        if (nonce && std::strlen(nonce) == 32) { sink.append(",\"nonce\":"); sink.writeJsonString(nonce); }
        end(sink, id);
    }
    void dispatch(const json::JsonDoc& doc, const char* request, uint16_t id, uint32_t now) {
        const char* op = replyOperation_;
        if (!fields(doc, op)) { result(id, "fields"); return; }
        if (std::strcmp(op, "get") == 0 || std::strcmp(op, "clock") == 0) { get(id); return; }
        if (std::strcmp(op, "battery") == 0) { battery(id); return; }
        if (std::strcmp(op, "profile") == 0) { profile(id); return; }
        if (std::strcmp(op, "info") == 0 || std::strcmp(op, "effect") == 0) { info(id, std::strcmp(op, "effect") == 0); return; }
        if (std::strcmp(op, "preview-frame") == 0) {
            if (!previewFrameReader_ || !scheduler_) { result(id, "frame-unavailable"); return; }
            uint32_t offset = 0;
            if (json::member(doc, doc.rootNode(), "$offset") && (!number(doc, "$offset", offset) || offset > UINT16_MAX)) { result(id, "frame-offset"); return; }
            const auto* revisionNode = json::member(doc, doc.rootNode(), "$rev");
            const char* revision = text(doc, "$rev");
            if (revisionNode && (!revision || !BleRpcSession::validChallenge(revision))) { result(id, "frame-revision"); return; }
            JsonSink sink(response_, sizeof(response_)); begin(sink, id, nullptr); sink.append(",");
            const char* error = previewFrameReader_(sink, *scheduler_, static_cast<uint16_t>(offset), revision, now);
            if (error) result(id, error); else end(sink, id);
            return;
        }
        if (std::strcmp(op, "pixel-map") == 0 || std::strcmp(op, "outputs") == 0) {
            const bool outputs = std::strcmp(op, "outputs") == 0;
            auto reader = outputs ? outputReader_ : pixelMapReader_;
            if (!reader || !scheduler_) { result(id, outputs ? "outputs-unavailable" : "map-unavailable"); return; }
            uint32_t offset = 0;
            if (json::member(doc, doc.rootNode(), "$offset") && (!number(doc, "$offset", offset) || offset > UINT16_MAX)) { result(id, outputs ? "outputs-offset" : "map-offset"); return; }
            const auto* revisionNode = json::member(doc, doc.rootNode(), "$rev");
            const char* revision = text(doc, "$rev");
            if (revisionNode && (!revision || !BleRpcSession::validChallenge(revision))) { result(id, outputs ? "outputs-revision" : "map-revision"); return; }
            JsonSink sink(response_, sizeof(response_)); begin(sink, id, nullptr); sink.append(",");
            const char* error = reader(sink, *scheduler_, static_cast<uint16_t>(offset), revision);
            if (error) result(id, error); else end(sink, id);
            return;
        }
        if (std::strcmp(op, "status") == 0 || std::strcmp(op, "cancel") == 0) { target(doc, id, std::strcmp(op, "cancel") == 0); return; }
        if (std::strcmp(op, "scenes") == 0 || std::strcmp(op, "scene-info") == 0 || std::strcmp(op, "scene-state") == 0) {
            if (!scenes_ || !scheduler_) { result(id, "scenes-unavailable"); return; }
            uint32_t offset = 0;
            if (json::member(doc, doc.rootNode(), "$offset") && (!number(doc, "$offset", offset) || offset > UINT16_MAX)) { result(id, "offset"); return; }
            JsonSink sink(response_, sizeof(response_)); begin(sink, id, nullptr); sink.append(",");
            const char* error = nullptr;
            if (std::strcmp(op, "scenes") == 0) error = scenes_->page(sink, static_cast<uint16_t>(offset));
            else if (std::strcmp(op, "scene-info") == 0) error = scenes_->info(sink, text(doc, "$scene"), static_cast<uint16_t>(offset));
            else scenes_->state(sink, *scheduler_);
            if (error) result(id, error); else end(sink, id);
            return;
        }
        if (std::strcmp(op, "group") == 0) {
            const char* requested = text(doc, "$group");
            if (!requested || std::strlen(requested) >= sizeof(group)) { result(id, "group"); return; }
            std::snprintf(group, sizeof(group), "%s", requested); markDirty(); if (scheduler_) scheduler_->noteDirty(); get(id); return;
        }
        const bool scene = std::strcmp(op, "scene") == 0;
        if (!scene && std::strcmp(op, "patch") != 0) { result(id, "operation"); return; }
        if (job_.phase != Phase::None) { result(id, "busy"); return; }
        const auto* live = json::member(doc, doc.rootNode(), "$live");
        if (!live || live->type != json::JsonType::Bool) { result(id, "live-required"); return; }
        const auto* requested = json::member(doc, doc.rootNode(), "$group");
        if (requested && (requested->type != json::JsonType::String || !group[0] || std::strcmp(group, requested->str))) { result(id, "group-mismatch"); return; }
        if (scene) {
            if (!scenes_ || !scheduler_) { result(id, "scenes-unavailable"); return; }
            char controls[513]; if (!sceneControls(doc, controls)) { result(id, "controls"); return; }
            const auto checked = scenes_->validate(*scheduler_, text(doc, "$scene"), text(doc, "$catalog"), controls);
            if (!checked.ok) { result(id, checked.error); return; }
        } else if (!scope(doc)) { result(id, "scope"); return; }
        uint32_t due = now;
        if (json::member(doc, doc.rootNode(), "$at") && !number(doc, "$at", due)) { result(id, "time"); return; }
        const int32_t delay = static_cast<int32_t>(due - now);
        if (delay > 60000 || delay < -250) { result(id, "time-window"); return; }
        if (delay > 0) {
            if (!pendingText_.resize(std::strlen(request) + 1)) { result(id, "memory"); return; }
            std::strcpy(pendingText_.data(), request);
        }
        job_.session = replySession_; job_.generation = replyGeneration_; job_.id = id; job_.due = due; job_.live = json::readBool(live);
        std::snprintf(job_.operation, sizeof(job_.operation), "%s", op);
        job_.phase = delay > 0 ? Phase::Queued : Phase::Applying;
        if (delay > 0) get(id); else apply(request);
    }
    bool sceneControls(const json::JsonDoc& doc, char (&out)[513]) {
        const auto* node = json::member(doc, doc.rootNode(), "controls");
        if (!node) { std::strcpy(out, "{}"); return true; }
        if (node->type != json::JsonType::Object) return false;
        JsonSink sink(out, sizeof(out)); json::write(sink, doc, node); return !sink.overflowed();
    }
    static bool fields(const json::JsonDoc& doc, const char* op) {
        for (int i = doc.rootNode()->firstChild; i >= 0; i = doc.nodes[i].next) {
            const char* key = doc.nodes[i].key;
            if (!key) return false;
            if (!std::strcmp(key, "$v") || !std::strcmp(key, "$e") || !std::strcmp(key, "$s") ||
                !std::strcmp(key, "$id") || !std::strcmp(key, "$op")) continue;
            const bool patch = !std::strcmp(op, "patch"), scene = !std::strcmp(op, "scene");
            if ((patch || scene) && (!std::strcmp(key, "$live") || !std::strcmp(key, "$at") || !std::strcmp(key, "$group"))) continue;
            if (scene && (!std::strcmp(key, "$scene") || !std::strcmp(key, "$catalog") || !std::strcmp(key, "controls"))) continue;
            if (patch && key[0] != '$') continue;
            if (!std::strcmp(op, "group") && !std::strcmp(key, "$group")) continue;
            if ((!std::strcmp(op, "cancel") || !std::strcmp(op, "status")) && !std::strcmp(key, "$target")) continue;
            if ((!std::strcmp(op, "scenes") || !std::strcmp(op, "scene-info") || !std::strcmp(op, "pixel-map") || !std::strcmp(op, "outputs") || !std::strcmp(op, "preview-frame")) && !std::strcmp(key, "$offset")) continue;
            if ((!std::strcmp(op, "pixel-map") || !std::strcmp(op, "outputs") || !std::strcmp(op, "preview-frame")) && !std::strcmp(key, "$rev")) continue;
            if (!std::strcmp(op, "scene-info") && !std::strcmp(key, "$scene")) continue;
            if (!std::strcmp(op, "hello") && (!std::strcmp(key, "$nonce") || !std::strcmp(key, "$resume"))) continue;
            return false;
        }
        return true;
    }
    bool scope(const json::JsonDoc& doc) const {
        for (int child = doc.rootNode()->firstChild; child >= 0; child = doc.nodes[child].next) {
            const char* key = doc.nodes[child].key;
            if (!key || key[0] == '$') continue;
            if (std::strcmp(key, "Services") == 0) { if (!serviceControls(doc, &doc.nodes[child])) return false; continue; }
            if (std::strcmp(key, "Network") == 0) {
                const auto& object = doc.nodes[child]; const int first = object.firstChild;
                if (object.type != json::JsonType::Object || first < 0 || doc.nodes[first].next >= 0 || !doc.nodes[first].key ||
                    std::strcmp(doc.nodes[first].key, "wifiEnabled") || doc.nodes[first].type != json::JsonType::Bool) return false;
                continue;
            }
            if (std::strcmp(key, "Drivers") && std::strcmp(key, "Effects")) return false;
        }
        return true;
    }
    void apply(const char* document) {
        replySession_ = job_.session; replyGeneration_ = job_.generation; std::snprintf(replyOperation_, sizeof(replyOperation_), "%s", job_.operation);
        if (!scheduler_) { finish("scheduler"); return; }
        const auto before = scheduler_->preparationRevision();
        job_.failedAt = scheduler_->failedPreparationRevision();
        StateDocumentResult applied;
        if (std::strcmp(job_.operation, "scene") == 0) {
            json::JsonDoc doc; char controls[513];
            if (!json::parse(document, doc) || !sceneControls(doc, controls)) { finish("controls"); return; }
            applied = scenes_->apply(*scheduler_, text(doc, "$scene"), text(doc, "$catalog"), controls, job_.live);
        } else applied = applyStateDocument(*scheduler_, document, job_.live ? StateSource::Live : StateSource::Request);
        if (!applied.ok) { finish(applied.error); return; }
        if (scheduler_->preparationPending()) { job_.phase = Phase::Applying; job_.preparedAt = before; get(job_.id); }
        else finish(nullptr);
    }
    void finish(const char* error) {
        terminal_ = job_; terminal_.phase = error ? Phase::Failed : Phase::Applied;
        std::snprintf(terminal_.error, sizeof(terminal_.error), "%s", error ? error : "");
        job_ = {};
        replySession_ = terminal_.session; replyGeneration_ = terminal_.generation; std::snprintf(replyOperation_, sizeof(replyOperation_), "%s", terminal_.operation);
        if (error) result(terminal_.id, terminal_.error); else get(terminal_.id);
        auto* session = rpc_ ? rpc_[0].findSession(terminal_.session) : nullptr;
        if (session && session->highWater() == terminal_.id) session->cacheReply(response_);
    }
    void writeJob(JsonSink& sink, const Job& job) {
        if (job.phase == Phase::None) { sink.append("null"); return; }
        sink.appendf("{\"s\":%u,\"id\":%u,\"state\":", static_cast<unsigned>(job.session), static_cast<unsigned>(job.id));
        sink.writeJsonString(phaseName(job.phase));
        if (job.phase == Phase::Queued || job.phase == Phase::Applying) sink.appendf(",\"due\":%u", static_cast<unsigned>(job.due));
        if (job.error[0]) { sink.append(",\"error\":"); sink.writeJsonString(job.error); }
        sink.append("}");
    }
    void target(const json::JsonDoc& doc, uint16_t id, bool cancel) {
        const auto* node = json::member(doc, doc.rootNode(), "$target");
        if (!node || node->type != json::JsonType::Object) { result(id, "target-required"); return; }
        const auto* s = json::member(doc, node, "s"); const auto* i = json::member(doc, node, "id");
        uint32_t session = 0, targetId = 0;
        if (!s || !i || s->type != json::JsonType::Int || i->type != json::JsonType::Int || s->decimal || i->decimal ||
            !parseNumber(*s, session) || !parseNumber(*i, targetId) || !session || !targetId || targetId > UINT16_MAX) { result(id, "target"); return; }
        const Job* found = nullptr;
        if (job_.phase != Phase::None && job_.session == session && job_.id == targetId) {
            if (cancel && job_.phase == Phase::Queued) {
                terminal_ = job_; terminal_.phase = Phase::Cancelled; terminal_.error[0] = '\0'; job_ = {}; pendingText_.resize(0); found = &terminal_;
            } else found = &job_;
        } else if (terminal_.phase != Phase::None && terminal_.session == session && terminal_.id == targetId) found = &terminal_;
        JsonSink sink(response_, sizeof(response_)); begin(sink, id, nullptr);
        sink.appendf(",\"target\":{\"s\":%u,\"id\":%u,\"state\":", static_cast<unsigned>(session), static_cast<unsigned>(targetId));
        sink.writeJsonString(!found ? "not-retained" : cancel && found->phase == Phase::Applied ? "already-applied" : phaseName(found->phase));
        if (found && (found->phase == Phase::Queued || found->phase == Phase::Applying)) sink.appendf(",\"due\":%u", static_cast<unsigned>(found->due));
        if (found && found->error[0]) { sink.append(",\"error\":"); sink.writeJsonString(found->error); }
        sink.append("}"); end(sink, id);
    }
    static bool parseNumber(const json::JsonNode& node, uint32_t& out) {
        if (!node.str || node.decimal || node.type != json::JsonType::Int) return false;
        const auto p = std::from_chars(node.str, node.str + node.numLen, out);
        return p.ec == std::errc{} && p.ptr == node.str + node.numLen;
    }
    void begin(JsonSink& sink, uint16_t id, const char* error) {
        sink.appendf("{\"v\":1,\"id\":%u,\"ok\":%s,\"now\":%u,\"e\":", static_cast<unsigned>(id), error ? "false" : "true", static_cast<unsigned>(platform::millis()));
        sink.writeJsonString(epoch_);
        sink.appendf(",\"s\":%u,\"r\":%u,\"op\":", static_cast<unsigned>(replySession_), static_cast<unsigned>(revision_ + 1));
        sink.writeJsonString(replyOperation_);
        if (error) { sink.append(",\"error\":"); sink.writeJsonString(error); }
    }
    void end(JsonSink& sink, uint16_t id) {
        sink.append("}");
        if (sink.overflowed()) {
            JsonSink fallback(response_, sizeof(response_)); begin(fallback, id, "response-too-large"); fallback.append("}");
        }
        ++revision_; replyDirty_ = true;
    }
    void result(uint16_t id, const char* error) { JsonSink sink(response_, sizeof(response_)); begin(sink, id, error); end(sink, id); }
    void get(uint16_t id) {
        JsonSink sink(response_, sizeof(response_)); begin(sink, id, nullptr);
        sink.append(",\"state\":{"); bool comma = false;
        if (scheduler_) for (const char* name : {"on", "brightness", "palette"}) {
            int32_t value = 0; if (!scheduler_->getControlWide("Drivers", name, value)) continue;
            if (comma) sink.append(",");
            sink.writeJsonString(name);
            if (std::strcmp(name, "on") == 0) sink.append(value ? ":true" : ":false"); else sink.appendf(":%d", static_cast<int>(value)); comma = true;
        }
        int32_t wifi = 0;
        if (scheduler_ && scheduler_->getControlWide("Network", "wifiEnabled", wifi)) { if (comma) sink.append(","); sink.append(wifi ? "\"wifiEnabled\":true" : "\"wifiEnabled\":false"); comma = true; }
        if (comma) sink.append(",");
        uint8_t ambient = 0;
        if (Bh1750Service::automaticBrightness(ambient)) sink.appendf("\"ambientTarget\":%u", ambient); else sink.append("\"ambientTarget\":null");
        sink.append("},\"queued\":"); sink.append(scheduled() ? "true" : "false");
        sink.append(",\"group\":"); sink.writeJsonString(group);
        sink.append(",\"pending\":"); writeJob(sink, job_);
        sink.append(",\"terminal\":"); writeJob(sink, terminal_); end(sink, id);
    }
    void battery(uint16_t id) {
        JsonSink sink(response_, sizeof(response_)); begin(sink, id, nullptr); sink.append(",\"battery\":");
        bool ambiguous = false;
        auto* battery = scheduler_ ? BatteryService::discover(*scheduler_, &ambiguous) : nullptr;
        if (battery) battery->writeJson(sink, platform::millis());
        else if (ambiguous) sink.append("{\"sampling\":false,\"packs\":[],\"error\":\"invalid-configuration\"}");
        else sink.append("{\"sampling\":false,\"packs\":[]}");
        end(sink, id);
    }
    void profile(uint16_t id) {
        const auto product = productHint();
        JsonSink sink(response_, sizeof(response_)); begin(sink, id, nullptr);
        sink.append(",\"profile\":");
        writeProductProfile(sink, product);
        end(sink, id);
    }
    static void writeProductProfile(JsonSink& sink, const BleAdvertisementProduct& product) {
        const bool sphere = product.code == 1 || product.code == 2;
        const bool garment = product.code == 20 || product.code == 22;
        if (!sphere && !garment) sink.append("null");
        else {
            sink.appendf("{\"schema\":1,\"code\":%u,\"source\":\"%s\",\"form\":\"%s\",\"verified\":false,\"revision\":null,\"mapping\":null,\"diameterMm\":",
                static_cast<unsigned>(product.code), sphere ? "firmware-target" : "selected-model", sphere ? "sphere" : "garment");
            if (sphere && product.diameterCm) sink.appendf("%u}", static_cast<unsigned>(product.diameterCm) * 10u);
            else sink.append("null}");
        }
    }
    void info(uint16_t id, bool effect) {
        JsonSink sink(response_, sizeof(response_)); begin(sink, id, nullptr);
        if (effect) {
            MoonModule* found = nullptr;
            if (scheduler_) for (uint8_t i = 0; i < scheduler_->moduleCount(); ++i)
                if (std::strcmp(scheduler_->module(i)->name(), "Effects") == 0) found = firstEffect(scheduler_->module(i));
            sink.append(",\"effect\":");
            if (!found) sink.append("null");
            else {
                sink.append("{\"name\":"); sink.writeJsonString(found->name()); sink.append(",\"type\":"); sink.writeJsonString(found->typeName());
                for (const char* key : {"red", "green", "blue", "white", "speed", "brightness"}) {
                    int32_t value = 0;
                    if (scheduler_->getControlWide(found->name(), key, value)) { sink.append(","); sink.writeJsonString(key); sink.appendf(":%d", static_cast<int>(value)); }
                }
                sink.append("}");
            }
        } else {
            const auto* output = LightOutput::active();
            sink.appendf(",\"ledCount\":%u,\"channels\":%u", output ? static_cast<unsigned>(output->summary().lightCount) : 0, output ? output->summary().channelsPerLight : 0);
            sink.append(",\"model\":"); const char* model = "";
            if (scheduler_) for (uint8_t i = 0; i < scheduler_->moduleCount(); ++i) {
                auto* root = scheduler_->module(i); if (std::strcmp(root->name(), "System")) continue;
                for (uint8_t c = 0; c < root->controls().count(); ++c) if (std::strcmp(root->controls()[c].name, "deviceModel") == 0 && root->controls()[c].type == ControlType::Text) model = static_cast<const char*>(root->controls()[c].ptr);
            }
            sink.writeJsonString(model);
            sink.append(",\"firmwareVersion\":"); sink.writeJsonString(version_);
            sink.append(",\"builtAt\":"); sink.writeJsonString(builtAt_);
            if (const size_t capacity = platform::otaNextAppCapacity())
                sink.appendf(",\"otaSlotBytes\":%u", static_cast<unsigned>(capacity));
            auto* battery = scheduler_ ? BatteryService::discover(*scheduler_) : nullptr;
            const auto current = battery ? battery->batteryConfig() : platform::BleBatteryConfig{};
            sink.appendf(",\"basChannels\":%u,\"basRestartRequired\":%s,\"profileOp\":true,\"pixelMapOp\":%s", static_cast<unsigned>(startedBatteries_.count), current == startedBatteries_ ? "false" : "true", pixelMapReader_ ? "true" : "false");
            if (outputReader_) sink.append(",\"outputsOp\":true");
            if (previewFrameReader_) sink.append(",\"previewFrameOp\":true");
        }
        end(sink, id);
    }
    bool serviceControls(const json::JsonDoc& doc, const json::JsonNode* node) const {
        if (!scheduler_ || node->type != json::JsonType::Object) return false;
        MoonModule* services = nullptr;
        for (uint8_t i = 0; i < scheduler_->moduleCount(); ++i)
            if (std::strcmp(scheduler_->module(i)->name(), "Services") == 0) services = scheduler_->module(i);
        if (!services) return false;
        for (int child = node->firstChild; child >= 0; child = doc.nodes[child].next) {
            const auto& value = doc.nodes[child];
            if (!value.key || value.type != json::JsonType::Object) return false;
            MoonModule* target = nullptr;
            for (uint8_t i = 0; i < services->childCount(); ++i)
                if (std::strcmp(services->child(i)->name(), value.key) == 0) target = services->child(i);
            if (!target || (std::strcmp(target->typeName(), "AudioService") && std::strcmp(target->typeName(), "Bh1750Service"))) return false;
            for (int field = value.firstChild; field >= 0; field = doc.nodes[field].next) {
                const auto& control = doc.nodes[field];
                if (!control.key || control.key[0] == '$' || std::strcmp(control.key, "type") == 0 ||
                    control.type == json::JsonType::Object || control.type == json::JsonType::Array || control.type == json::JsonType::Null) return false;
            }
        }
        return true;
    }

    static MoonModule* firstEffect(MoonModule* module) {
        if (!module->effectivelyEnabled()) return nullptr;
        if (module->role() == ModuleRole::Effect) return module;
        for (uint8_t i = 0; i < module->childCount(); ++i)
            if (auto* found = firstEffect(module->child(i))) return found;
        return nullptr;
    }

    static bool number(const json::JsonDoc& doc, const char* key, uint32_t& out) {
        const auto* node = json::member(doc, doc.rootNode(), key);
        if (!node || node->type != json::JsonType::Int || node->decimal || !node->str) return false;
        const auto result = std::from_chars(node->str, node->str + node->numLen, out);
        return result.ec == std::errc{} && result.ptr == node->str + node->numLen;
    }


    Scheduler* scheduler_ = nullptr;
    const BleSceneApi* scenes_ = nullptr;
    BleAdvertisementProduct advertisingProduct_;
    uint8_t (*advertisingProductResolver_)(const char*) = nullptr;
    void (*advertisingScene_)(BleAdvertisementSnapshot&, Scheduler&) = nullptr;
    const char* (*previewFrameReader_)(JsonSink&, Scheduler&, uint16_t, const char*, uint32_t) = nullptr;
    const char* (*outputReader_)(JsonSink&, Scheduler&, uint16_t, const char*) = nullptr;
    const char* (*pixelMapReader_)(JsonSink&, Scheduler&, uint16_t, const char*) = nullptr;
    const char* firmware_ = "MoonLight";
    const char* build_ = "development";
    const char* version_ = "";
    const char* builtAt_ = "";
    platform::BleControlHandle transport_;
    ScratchBuffer<BleRpcPeers<platform::kBleControlPeerCapacity>> rpc_{*this};
    ScratchBuffer<char> pendingText_{*this};
    Job job_{}, terminal_{};
    char epoch_[33] = "00000000000000000000000000000000";
    bool epochReady_ = false;
    char response_[513] = {};
    char replyOperation_[16] = {};
    uint32_t replySession_ = 0, revision_ = 0;
    uint64_t replyGeneration_ = 0;
    uint64_t activeOwner_ = 0;
    bool replyDirty_ = false;
    /// Radio state, independent of Wi-Fi association.
    char connection_[16] = "offline";
    platform::BleBatteryConfig startedBatteries_{};
    /// BAS declarations remain fixed until the next real service startup.
    char batteryServices_[48] = "not started";
    /// Repeat-pairing counters and the remaining explicit repair window.
    char pairing_[160] = "not started";
    char linkDiagnostics_[192] = "not started";
    char profileText_[192] = "null";
};
} // namespace mm
