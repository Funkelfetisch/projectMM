/// HttpServerModule's WLED shim: the routes and the WebSocket frame that make a MoonLight device appear in the WLED apps and Home Assistant's WLED integration.
///
/// @technical
///
/// ## Why the WLED shim reports a sentinel version
///
/// The `ver` field is a sentinel, not the MoonLight version.
///
/// Home Assistant parses WLED tags as CalVer, so a real `2.1.0-dev` compares lower than `16.0.1` and HA offers an update whose `.bin` would brick the device.
///
/// Under CalVer `99.0.0` outranks any WLED tag, so HA's update check stays silent.
///
/// The real version belongs on the MQTT update topic, where the question is whether MoonLight shipped a release.
///
/// ## Why an Ethernet device sends no wifi object
///
/// The `wifi` block feeds HA's diagnostic sensors, and an Ethernet device has no AP, so `info.wifi` (optional in python-wled) is omitted.
///
/// Omitting it makes HA create no WiFi sensors, as a real WLED on Ethernet does, instead of greyed-out zero rows.

#include "core/system/HttpServerModule.h"

#include "core/module/Scheduler.h"
#include "core/util/JsonUtil.h"
#include "core/util/parse.h"             // parseIntStr: the app's numeric fields
#include "core/util/JsonSink.h"
#include "core/util/format.h"            // formatTo: nonblocking formatting into a fixed buffer
#include "core/system/ControlModule.h"   // look presets on /presets.json (HA WLED integration)
#include "core/system/SystemModule.h"      // deviceName() for the WLED /json/info shim
#include "core/util/LightOutput.h"   // the device shape and the palettes as colors, for the WLED shim
#include "platform/platform.h"

#include <cstdio>
#include <cstring>
#include <cstdint>

namespace mm {

namespace {
// A JSON integer or boolean member of `obj`, as WLED sends them; false when absent or another type.
bool wledNumber(const json::JsonDoc& doc, const json::JsonNode* obj, const char* key, long& out) {
    const auto* node = obj ? json::member(doc, obj, key) : nullptr;
    if (!node || (node->type != json::JsonType::Int && node->type != json::JsonType::Bool)) return false;
    out = node->intValue;
    return true;
}
long clampByte(long v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }
} // namespace


// WLED-compatibility `/json/info`: the field set the WLED apps and HA validate after `_wled._tcp` discovery, with `brand:"WLED"` to interoperate and `product:"MoonModules"` to say what this is.
void HttpServerModule::serveWledInfo(platform::TcpConnection& conn) {
    // Identity: the deviceName (from SystemModule), the live IP, the MAC.
    const char* name; uint8_t mac[6];
    resolveWledIdentity(name, mac, nullptr);

    // The WLED-Android app requires only `name`, `leds{}`, `wifi{}` and a non-empty `mac` (model/wledapi/Info.kt), else it drops the device silently, so these are the smallest shapes.
    JsonSink sink;
    writeWledInfoBody(sink, name, mac);
    answerJson(conn, sink);
}

// /presets.json: look presets in WLED's format, keyed by 1-based slot (python-wled discards 0) with a `{"0":{}}` floor; Drivers and Layouts presets stay out since they rewire pins or geometry.
void HttpServerModule::serveWledPresets(platform::TcpConnection& conn) {
    auto* control = static_cast<ControlModule*>(findModuleByName("Control"));
    if (!control) { sendResponse(conn, 200, "application/json", "{\"0\":{}}"); return; }

    JsonSink sink;
    sink.append("{");
    bool any = false;
    for (uint8_t i = 0; i < control->presetCount(); i++) {
        if (!control->isLookOnly(i)) continue;
        const char* name = control->presetName(i);
        if (!name || !name[0]) continue;
        // Slot+1: WLED slot 0 is reserved, and python-wled drops it.
        sink.appendf("%s\"%u\":{\"n\":", any ? "," : "", static_cast<unsigned>(i + 1));
        sink.writeJsonString(name);
        sink.append("}");
        any = true;
    }
    if (!any) sink.append("\"0\":{}");   // non-empty, or python-wled reads it as no response at all
    sink.append("}");
    answerJson(conn, sink);
}

// The deviceName / IP / MAC lookup the WLED shim needs at four call sites, so a change to how identity is discovered updates one place.
void HttpServerModule::resolveWledIdentity(const char*& name, uint8_t mac[6], uint8_t* ip,
                                           const char* nameFallback) {
    name = nameFallback;
    if (MoonModule* sys = findModuleByName("System")) {
        const char* dn = static_cast<SystemModule*>(sys)->deviceName();
        if (dn && dn[0]) name = dn;
    }
    for (int i = 0; i < 6; i++) mac[i] = 0;
    platform::getMacAddress(mac);
    if (!ip) return;
    for (int i = 0; i < 4; i++) ip[i] = 0;
    platform::localIPv4(ip);
}

// The WLED info object (no HTTP header), shared by /json/info and /json/si; the 💫 marker prefixes only the WLED-compat name HA reads, never the real deviceName.
void HttpServerModule::writeWledName(JsonSink& sink, const char* name) {
    char prefixed[80];
    std::snprintf(prefixed, sizeof(prefixed), "\xF0\x9F\x92\xAB %s", name ? name : "");
    sink.writeJsonString(prefixed);
}

void HttpServerModule::writeWledInfoBody(JsonSink& sink, const char* name, const uint8_t mac[6]) {
    sink.appendf("{\"name\":");
    writeWledName(sink, name);
    // The real light count and the wifi signal, so the WLED app card shows the true device shape; signal maps rssi to 0-100 as WLED does.
    const unsigned ledCount = lightSummary().lightCount;
    const int rssi = platform::wifiStaRssi();
    int signal = (rssi == 0) ? 0 : (2 * (rssi + 100));
    if (signal < 0) signal = 0; else if (signal > 100) signal = 100;
    sink.appendf(",\"mac\":\"%02x%02x%02x%02x%02x%02x\","
                 "\"leds\":{\"count\":%u},\"wifi\":{\"rssi\":%d,\"signal\":%d},"
                 "\"brand\":\"WLED\",\"product\":\"MoonModules\"}",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                 ledCount, rssi, signal);
}

// The WLED state object: `on`+`bri` mirror Drivers, and `seg[0].col[0]` is the active palette's full-value color, stable across consumers and never double-dimmed with HA's `state.bri × seg.bri`.
void HttpServerModule::writeWledStateBody(JsonSink& sink) {
    const uint8_t bri = driversBrightness(scheduler_);
    const LightOutput* out = LightOutput::active();
    // seg[0] describes the running scene: its catalog index as fx, its own speed and intensity, and the palette it reads, the effect's own or the Drivers one.
    int fx = 0;
    if (scenes_ && scenes_->current && scheduler_) { const int current = scenes_->current(*scheduler_); if (current >= 0) fx = current; }
    int32_t sx = 128, ix = 128, palValue = driversPalette(scheduler_);
    if (MoonModule* effect = runningEffect(scheduler_)) {
        scheduler_->getControlWide(effect->name(), "speed", sx);
        scheduler_->getControlWide(effect->name(), "effect intensity", ix);
        scheduler_->getControlWide(effect->name(), "palette", palValue);
    }
    const uint8_t pal = static_cast<uint8_t>(clampByte(palValue));
    const RGB pc = out ? out->paletteRgb(pal) : RGB{0, 0, 0};
    // python-wled parses this response through State.from_dict, so nl/udpn/lor/transition/ps/pl/mainseg are required, and seg[0].on must be present since HA reads segment .on, not top-level state.on.
    const char* onStr = driversOn(scheduler_) ? "true" : "false";
    // The applied look as a WLED preset slot (1-based, as /presets.json), or -1; without it HA's preset dropdown reads unknown even while a preset is active.
    int currentPs = -1;
    if (auto* control = static_cast<ControlModule*>(findModuleByName("Control"))) {
        const char* look = control->currentLook();
        if (look && look[0]) {
            for (uint8_t i = 0; i < control->presetCount(); i++) {
                if (!control->isLookOnly(i)) continue;
                const char* n = control->presetName(i);
                if (n && std::strcmp(n, look) == 0) { currentPs = i + 1; break; }
            }
        }
    }
    sink.appendf("{\"on\":%s,\"bri\":%u,\"transition\":7,\"ps\":%d,\"pl\":-1,"
                 "\"nl\":{},\"udpn\":{},\"lor\":0,\"mainseg\":0,"
                 // bri 255 lets HA show the master value, fx indexes /json's effects, pal and col[0] name one palette, and lc is the capability WLED 16+ clients read here.
                 "\"seg\":[{\"id\":0,\"on\":%s,\"bri\":255,\"lc\":1,\"fx\":%d,\"sx\":%ld,\"ix\":%ld,\"pal\":%u,\"col\":[[%u,%u,%u]]}]}",
                 onStr, bri, currentPs, onStr, fx, clampByte(sx), clampByte(ix), pal, pc.r, pc.g, pc.b);
}

void HttpServerModule::serveWledState(platform::TcpConnection& conn) {
    JsonSink sink;
    writeWledStateBody(sink);
    answerJson(conn, sink);
}

// /json: the full blob HA's python-wled fetches, requiring `info.fs`, `state.nl`, `state.udpn`, `state.lor` and `ver` >= 0.14.0; separate from /json/info and /json/state, which stay the Android-validated minimum.
void HttpServerModule::serveWledDeviceJson(platform::TcpConnection& conn) {
    const char* name; uint8_t mac[6]; uint8_t ip[4];
    resolveWledIdentity(name, mac, ip);

    JsonSink sink;
    // state: reuse writeWledStateBody under "state" so one writer decides which seg[0] fields HA reads.
    sink.appendf("{\"state\":");
    writeWledStateBody(sink);
    // `ver` is a sentinel and an Ethernet device omits `wifi` entirely. @xref{why-the-wled-shim-reports-a-sentinel-version} @xref{why-an-ethernet-device-sends-no-wifi-object}
    const bool onEth = platform::ethConnected();
    const int rssi = platform::wifiStaRssi();
    uint8_t bssid[6] = {};
    platform::wifiStaBssid(bssid);
    const int channel = platform::wifiStaChannel();
    int signal = (rssi == 0) ? 0 : (2 * (rssi + 100));
    if (signal < 0) signal = 0; else if (signal > 100) signal = 100;
    // The real pipeline shape and render rate.
    const LightSummary& ls = lightSummary();
    const LightOutput* out = LightOutput::active();
    const unsigned ledCount = ls.lightCount;
    const unsigned renderFps = scheduler_ ? scheduler_->fps() : 0;
    const char* rgbw = (ls.channelsPerLight >= 4) ? "true" : "false";
    sink.appendf(",\"info\":{\"ver\":\"99.0.0\",\"vid\":2410150,\"name\":");
    writeWledName(sink, name);
    sink.appendf(",\"mac\":\"%02x%02x%02x%02x%02x%02x\","
                 "\"ip\":\"%u.%u.%u.%u\",\"arch\":\"esp32\","
                 "\"brand\":\"WLED\",\"product\":\"MoonModules\",\"release\":\"MoonModules\","
                 // lc and seglc are capability codes, not counts, kept for clients before WLED 16: 1 (RGB_COLOR) gives HA a brightness slider and color picker, and a LED count would leave the light entity without color modes.
                 "\"leds\":{\"count\":%u,\"fps\":%u,\"rgbw\":%s,\"wv\":false,\"cct\":false,"
                 "\"maxpwr\":0,\"maxseg\":1,\"pwr\":0,\"lc\":1,\"seglc\":[1]},",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                 ip[0], ip[1], ip[2], ip[3],
                 ledCount, renderFps, rgbw);
    // wifi: only for a Wi-Fi device (omitted on Ethernet; see the comment above the getters).
    if (!onEth) {
        sink.appendf("\"wifi\":{\"bssid\":\"%02x:%02x:%02x:%02x:%02x:%02x\","
                     "\"rssi\":%d,\"channel\":%d,\"signal\":%d},",
                     bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
                     rssi, channel, signal);
    }
    // pmt is the presets-modified time HA uses to re-fetch /presets.json, stamped by ControlModule on every preset change, with 1 as the no-ControlModule fallback.
    unsigned pmt = 1;
    if (auto* control = static_cast<ControlModule*>(findModuleByName("Control")))
        pmt = static_cast<unsigned>(control->presetsRevision());   // >= 1 once setup's rescan ran
    if (pmt == 0) pmt = 1;   // python-wled treats 0 as "no presets support"
    sink.appendf("\"fs\":{\"t\":256,\"u\":32,\"pmt\":%u},"
                 // uptime + pmt drive python-wled's presets change-detect: both non-zero and stable stop HA refetching /presets.json on every state update.
                 "\"freeheap\":%u,\"uptime\":%u,\"udpport\":21324,\"live\":false,"
                 // ws=-1 tells python-wled WebSocket updates are unsupported, so HA polls over HTTP instead of opening /ws, whose MoonLight-native frames flood its log with `MissingField: filesystem`.
                 "\"lm\":\"\",\"lip\":\"\",\"ws\":-1,"
                 // palcount is the real count, built-ins plus the scripted tail, matching palettes[] below; fxcount matches effects[], and cpal/umpal are 0.
                 "\"fxcount\":%u,\"palcount\":%u,\"cpalcount\":0,\"umpalcount\":0,\"str\":false}",
                 // Non-zero or HA rejects the device: the desktop's freeHeap() reports 0, so a nominal figure stands in.
                 pmt,
                 static_cast<unsigned>(platform::freeHeap() ? platform::freeHeap() : 32768u),
                 static_cast<unsigned>(platform::millis() / 1000u),
                 static_cast<unsigned>(wledEffectCount()),
                 static_cast<unsigned>(out ? out->paletteCount() : 0));
    // effects are the product's scenes, which seg[0].fx indexes, or one "Solid" without a catalog; palettes is the real list, indexed to match seg[0].pal.
    sink.appendf(",\"effects\":[");
    const char* id = nullptr; const char* sceneName = nullptr;
    if (scenes_ && scenes_->at && scenes_->at(0, id, sceneName))
        for (uint16_t i = 0; scenes_->at(i, id, sceneName); ++i) { if (i) sink.append(","); sink.writeJsonString(sceneName); }
    else sink.append("\"Solid\"");
    sink.appendf("],\"palettes\":[");
    if (out) out->writePaletteNames(sink);
    sink.appendf("]}");
    answerJson(conn, sink);
}

// The combined {state, info} object, which /json/si answers and the WebSocket pushes.
void HttpServerModule::writeWledStateInfo(JsonSink& sink) {
    const char* name; uint8_t mac[6];
    resolveWledIdentity(name, mac, nullptr);
    sink.appendf("{\"state\":");
    writeWledStateBody(sink);
    sink.appendf(",\"info\":");
    writeWledInfoBody(sink, name, mac);
    sink.appendf("}");
}

// /json/si: the combined {state, info} the WLED app reads in one call for its card.
void HttpServerModule::serveWledStateInfo(platform::TcpConnection& conn) {
    JsonSink sink;
    writeWledStateInfo(sink);
    answerJson(conn, sink);
}


// How many effects /json lists: the product's scenes, or the one "Solid" without a catalog.
unsigned HttpServerModule::wledEffectCount() const {
    const char* id = nullptr; const char* name = nullptr;
    unsigned count = 0;
    if (scenes_ && scenes_->at) while (scenes_->at(static_cast<uint16_t>(count), id, name)) ++count;
    return count ? count : 1;
}

// The module whose `palette` a WLED palette names: the running effect when it carries its own, else Drivers, whose palette the global-palette scenes read.
const char* HttpServerModule::wledPaletteOwner() {
    MoonModule* effect = runningEffect(scheduler_);
    int32_t ignored = 0;
    return effect && scheduler_->getControlWide(effect->name(), "palette", ignored) ? effect->name() : "Drivers";
}

// Apply a WLED state body as python-wled sends it: top-level `on`, `bri`, `ps` and segment 0's `on`, `fx`, `sx`, `ix`, `pal`, `col`; `on` is master power, so off keeps the level.
void HttpServerModule::applyWledState(const char* body) {
    json::JsonDoc doc;
    if (!body || !json::parse(body, doc) || !doc.rootNode() || doc.rootNode()->type != json::JsonType::Object) return;
    const json::JsonNode* root = doc.rootNode();
    // seg is an array of segment objects or one object; the shim drives one segment, so the first is the one.
    const json::JsonNode* seg = json::member(doc, root, "seg");
    if (seg && seg->type == json::JsonType::Array) seg = json::element(doc, seg, 0);
    if (seg && seg->type != json::JsonType::Object) seg = nullptr;

    long value = 0;
    // ps is a preset slot from HA's dropdown (1-based, as /presets.json); applyLookByName re-checks look-only, so a crafted request naming a Drivers or Layouts preset is refused here, not hidden.
    if (wledNumber(doc, root, "ps", value)) {
        auto* control = static_cast<ControlModule*>(findModuleByName("Control"));
        if (control && value > 0 && value <= control->presetCount()) {
            const char* name = control->presetName(static_cast<uint8_t>(value - 1));
            if (name) control->applyLookByName(name);
        }
    }
    // on is a boolean, or "t" to toggle; segment 0's on is the same switch, since the shim drives one segment.
    const json::JsonNode* on = json::member(doc, root, "on");
    if (!on && seg) on = json::member(doc, seg, "on");
    if (on && (on->type == json::JsonType::Bool || (on->type == json::JsonType::String && on->str && std::strcmp(on->str, "t") == 0))) {
        const bool next = on->type == json::JsonType::Bool ? on->intValue != 0 : !driversOn(scheduler_);
        applySetControl("Drivers", "on", next ? "{\"value\":true}" : "{\"value\":false}");
    }
    char valueJson[24];
    if (wledNumber(doc, root, "bri", value)) {
        std::snprintf(valueJson, sizeof(valueJson), "{\"value\":%ld}", clampByte(value));
        applySetControl("Drivers", "brightness", valueJson);
    }
    if (!seg) return;
    // fx picks a catalog scene at its defaults, before the speed, intensity and palette below land on it.
    if (wledNumber(doc, seg, "fx", value) && scenes_ && scenes_->at && scenes_->apply && scenes_->revision && scheduler_) {
        const char* id = nullptr; const char* name = nullptr;
        if (value >= 0 && value <= UINT16_MAX && scenes_->at(static_cast<uint16_t>(value), id, name))
            scenes_->apply(*scheduler_, id, scenes_->revision(), "{}", false);
    }
    // sx and ix are the running effect's speed and intensity, where it has them.
    if (MoonModule* effect = runningEffect(scheduler_)) {
        for (const auto& [key, control] : {std::pair{"sx", "speed"}, std::pair{"ix", "effect intensity"}}) {
            int32_t present = 0;
            if (!wledNumber(doc, seg, key, value) || !scheduler_->getControlWide(effect->name(), control, present)) continue;
            std::snprintf(valueJson, sizeof(valueJson), "{\"value\":%ld}", clampByte(value));
            applySetControl(effect->name(), control, valueJson);
        }
    }
    // pal is a palette index; col[0] is [r,g,b] from HA's color picker, mapped to the nearest palette. Both go to the palette the running scene reads.
    const LightOutput* out = LightOutput::active();
    if (!out) return;   // no palettes without a light output
    long pal = -1;
    if (wledNumber(doc, seg, "pal", value)) pal = value < 0 ? 0 : (value >= out->paletteCount() ? out->paletteCount() - 1 : value);
    const json::JsonNode* col = json::member(doc, seg, "col");
    const json::JsonNode* primary = col && col->type == json::JsonType::Array ? json::element(doc, col, 0) : nullptr;
    if (pal < 0 && primary && primary->type == json::JsonType::Array) {
        long rgb[3] = {};
        bool whole = true;
        for (int i = 0; i < 3; ++i) {
            const auto* channel = json::element(doc, primary, i);
            whole = whole && channel && channel->type == json::JsonType::Int;
            if (whole) rgb[i] = clampByte(channel->intValue);
        }
        if (whole) pal = out->nearestPalette(RGB{static_cast<uint8_t>(rgb[0]), static_cast<uint8_t>(rgb[1]), static_cast<uint8_t>(rgb[2])});
    }
    if (pal < 0) return;
    std::snprintf(valueJson, sizeof(valueJson), "{\"value\":%ld}", pal);
    applySetControl(wledPaletteOwner(), "palette", valueJson);
}

// POST /json/state: the WLED app's HTTP control channel; apply, then echo the resulting state, which the app expects.
void HttpServerModule::handleWledState(platform::TcpConnection& conn, const char* body) {
    applyWledState(body);
    serveWledState(conn);
}

// Build and push the WLED {state, info} object to every WS client, the same object /json/si answers.
void HttpServerModule::pushWledStateToWebSockets() {
    if (anyStreamActive()) return;   // never interleave with a streamed full state
    bool hasClients = false;
    for (int i = 0; i < kMaxWsClients; i++) if (wsClients_[i].valid() && !subs_[i].received) { hasClients = true; break; }
    if (!hasClients) return;   // only the web interface is connected: the frame is built for nobody

    JsonSink sink;
    writeWledStateInfo(sink);

    for (int i = 0; i < kMaxWsClients; i++) {
        auto& ws = wsClients_[i];
        // A client that subscribed is the web interface, which reads module state rather than this frame.
        if (!ws.valid() || subs_[i].received) continue;
        queueFrame(i, 0x81, sink.data(), sink.size());
    }
}

} // namespace mm
