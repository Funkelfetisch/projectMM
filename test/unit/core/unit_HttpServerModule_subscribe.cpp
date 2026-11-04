/// @module HttpServerModule
///
/// A control client receives the modules it subscribes to in full and an outline of the rest, streamed as fragments that no board has to hold whole.
/// The tree here has two roots, so a subscription to one shows what the other leaves out; the loopback test reads the fragmented WebSocket message off a real socket.

#include "doctest.h"
#include "core/system/HttpServerModule.h"
#include "core/services/BleControlService.h"
#include "core/services/BatteryService.h"
#include "helio/BleAdvertisementRegistry.h"
#include "core/module/Scheduler.h"
#include "core/module/MoonModule.h"
#include "core/util/JsonSink.h"
#include "core/util/JsonUtil.h"
#include "core/util/ModuleFactory.h"
#include "core/util/PresetStore.h"
#include "http_loopback.h"
#include "platform/platform.h"

#include <algorithm> // std::max
#include <cstdio>    // std::snprintf, the wide module's control names
#include <cstring>
#include <functional>
#include <filesystem>
#include <string>    // std::string: named explicitly, GCC does not pull it in transitively
#include <vector>

namespace {

using namespace http_loopback;

struct Knob : public mm::MoonModule {
    uint8_t value = 10;
    void defineControls() override { controls_.addControl("value", value, 0, 100); }
};

// Forty controls, enough to need several fragments at the stream's fragment size.
struct Wide : public mm::MoonModule {
    static constexpr int kCount = 40;
    uint8_t values[kCount] = {};
    void defineControls() override {
        static char names[kCount][4];
        for (int i = 0; i < kCount; i++) {
            std::snprintf(names[i], sizeof(names[i]), "c%02d", i);
            controls_.addControl(names[i], values[i]);
        }
    }
};

struct Box : public mm::MoonModule {};

// Root A holds K; root B holds L and the wide module W.
struct Tree {
    mm::Scheduler s;
    Box* a = new Box();
    Box* b = new Box();
    Knob* k = new Knob();
    Knob* l = new Knob();
    Wide* w = new Wide();
    Tree() {
        a->setName("A"); b->setName("B"); k->setName("K"); l->setName("L"); w->setName("W");
        a->addChild(k);
        b->addChild(l);
        b->addChild(w);
        s.addModule(a);
        s.addModule(b);
        s.setup();
    }
    ~Tree() { mm::Scheduler::deleteTree(a); mm::Scheduler::deleteTree(b); }
};

std::string whole(mm::HttpServerModule& http, const mm::HttpServerModule::Subscription* sub) {
    mm::HttpServerModule::TreeCursor c;
    c.sub = sub;
    mm::JsonSink sink;
    http.writeTreeFragment(sink, c, 0);
    return std::string(sink.data(), sink.size());
}

// The JSON object a module starts, up to the first closing brace after its name: enough to see whether controls ride with it.
std::string objectOf(const std::string& doc, const char* name) {
    const std::string key = std::string("{\"name\":\"") + name + "\"";
    const size_t at = doc.find(key);
    if (at == std::string::npos) return "";
    return doc.substr(at, doc.find("\"children\"", at) == std::string::npos ? std::string::npos : doc.find("\"children\"", at) - at);
}

mm::HttpServerModule::Subscription subscribed(const char* body) {
    mm::HttpServerModule::Subscription sub;
    sub.parse(body);
    return sub;
}

}  // namespace

TEST_CASE("a full state written in fragments of any size equals the state written at once") {
    Tree t;
    mm::HttpServerModule http;
    http.setScheduler(&t.s);
    const std::string once = whole(http, nullptr);
    REQUIRE(once.rfind("{\"modules\":[", 0) == 0);
    for (size_t target : {size_t(1), size_t(7), size_t(64), size_t(2048)}) {
        mm::HttpServerModule::TreeCursor c;
        std::string joined;
        int fragments = 0;
        bool done = false;
        while (!done && fragments < 100000) {
            mm::JsonSink sink;
            done = http.writeTreeFragment(sink, c, target);
            joined.append(sink.data(), sink.size());
            fragments++;
        }
        CHECK(joined == once);
        if (target < once.size()) CHECK(fragments > 1);
    }
}

TEST_CASE("product profile reaches the real HTTP state serializer and clears for a custom model") {
    struct ProductModel : mm::MoonModule {
        char model[32] = "NEBULITE Croptop S3";
        void defineControls() override { controls_.addText("deviceModel", model, sizeof(model)); }
    };
    mm::Scheduler scheduler;
    auto* model = new ProductModel(); model->setName("System"); model->defineControls();
    auto* ble = new mm::BleControlService(); ble->setName("BLE"); ble->setScheduler(&scheduler);
    ble->setAdvertisingProductResolver(&mm::ble_advertisement::productCodeForModel);
    ble->defineControls(); scheduler.addModule(model); scheduler.addModule(ble);
    mm::HttpServerModule http; http.setScheduler(&scheduler);
    ble->tick1s();
    const auto selected = whole(http, nullptr);
    CHECK(selected.find("\"name\":\"productProfile\"") != std::string::npos);
    CHECK(selected.find("\\\"code\\\":20") != std::string::npos);
    CHECK(selected.find("\\\"verified\\\":false") != std::string::npos);
    std::snprintf(model->model, sizeof(model->model), "%s", "custom");
    const auto beforeTick = whole(http, nullptr);
    CHECK(beforeTick.find("\"name\":\"deviceModel\",\"type\":\"text\",\"value\":\"custom\"") != std::string::npos);
    CHECK(beforeTick.find("\\\"code\\\":20") != std::string::npos); // consumers must compare the two
    ble->tick1s();
    const auto unknown = whole(http, nullptr);
    CHECK(unknown.find("\\\"code\\\":20") == std::string::npos);
    CHECK(unknown.find("\"name\":\"productProfile\",\"type\":\"display\",\"value\":\"null\"") != std::string::npos);
    scheduler.release();
}

TEST_CASE("battery samples reach the subscribed dashboard and stale values replace percentages") {
    mm::Scheduler scheduler;
    auto* battery = new mm::BatteryService(); battery->setName("Battery"); battery->setTypeName("BatteryService");
    auto* pack = new mm::BatteryChannel(); pack->setName("Pack1"); pack->setTypeName("BatteryChannel");
    pack->presence = 1; pack->gainNumerator = 2; pack->gainDenominator = 1;
    pack->emptyMv = 3000; pack->fullMv = 4200; pack->estimate = true; pack->staleSeconds = 1;
    pack->defineControls(); battery->defineControls(); battery->addChild(pack); scheduler.addModule(battery);
    pack->configureReading(); pack->updateText(0);
    mm::HttpServerModule http; http.setScheduler(&scheduler);
    http.subscribeForTest(0, R"({"subscribe":["*"],"detail":"values"})");
    http.baselineLeafHashesForTest();
    pack->acceptAdc(1800, 1000); pack->updateText(1000);
    mm::JsonSink fresh[1]; http.buildClientPatchesForTest(fresh, 1);
    const std::string current(fresh[0].data(), fresh[0].size());
    CHECK(current.find("Pack1/sample") != std::string::npos);
    CHECK(current.find("\\\"state\\\":\\\"fresh\\\"") != std::string::npos);
    CHECK(current.find("\\\"percent\\\":50") != std::string::npos);
    pack->updateText(2001);
    mm::JsonSink stale[1]; http.buildClientPatchesForTest(stale, 1);
    const std::string expired(stale[0].data(), stale[0].size());
    CHECK(expired.find("Pack1/sample") != std::string::npos);
    CHECK(expired.find("\\\"state\\\":\\\"stale\\\"") != std::string::npos);
    CHECK(expired.find("\\\"percent\\\":null") != std::string::npos);
    scheduler.release();
}

namespace {
// A select, a palette and a list whose options and rows run to many kilobytes, as Drivers' palette and the fixture profile list do.
struct Lots : public mm::MoonModule, public mm::ListSource {
    static constexpr int kOptions = 200;
    uint8_t listRowCount() const override { return 60; }
    void writeListRow(mm::JsonSink& sink, uint8_t row) const override { sink.appendf("{\"id\":%u,\"name\":\"profile %02u\"}", row, row); }
    void writeListRowDetail(mm::JsonSink& sink, uint8_t row) const override {
        sink.appendf("{\"channels\":\"red green blue white amber uv dimmer strobe pan tilt\",\"row\":%u}", row);
    }
    void writeListOptionSets(mm::JsonSink& sink) const override { sink.append("\"kinds\":[\"rgb\",\"rgbw\"]"); }
    uint8_t pick = 0, palette = 0;
    static void paletteOptions(mm::JsonSink& sink) {
        const auto one = [&](int i) { sink.appendf("%s{\"name\":\"palette %03d\",\"colors\":\"ff0000 00ff00 0000ff ffffff\"}", i > 0 ? "," : "", i); };
        if (sink.optionIndex() >= 0) { if (sink.optionIndex() < kOptions) one(sink.optionIndex()); return; }
        for (int i = 0; i < kOptions; i++) one(i);
    }
    void defineControls() override {
        static char names[kOptions][40];
        static const char* options[kOptions];
        for (int i = 0; i < kOptions; i++) {
            std::snprintf(names[i], sizeof(names[i]), "an option with a long name, number %03d", i);
            options[i] = names[i];
        }
        controls_.addSelect("pick", pick, options, kOptions);
        controls_.addPalette("palette", palette, paletteOptions, kOptions);
        controls_.addList("profiles", *this);
    }
};
}  // namespace

// A classic ESP32's page never got its state: one control of 11 to 17 KB went into a single fragment, which no free block that size could hold.
TEST_CASE("a control larger than a fragment streams a row or an option at a time, and reassembles to the whole") {
    mm::Scheduler s;
    auto* lots = new Lots();
    lots->setName("Lots");
    s.addModule(lots);
    s.setup();
    mm::HttpServerModule http;
    http.setScheduler(&s);
    const std::string once = whole(http, nullptr);
    REQUIRE(once.size() > 4 * 2048);
    for (size_t target : {size_t(1), size_t(64), size_t(2048)}) {
        mm::HttpServerModule::TreeCursor c;
        std::string joined;
        size_t largest = 0;
        bool done = false;
        for (int fragments = 0; !done && fragments < 100000; fragments++) {
            mm::JsonSink sink;
            done = http.writeTreeFragment(sink, c, target);
            joined.append(sink.data(), sink.size());
            largest = std::max(largest, sink.size());
        }
        CHECK(joined == once);
        CHECK(largest < target + 200);   // the target and one option past it, never a whole control
    }
    mm::Scheduler::deleteTree(lots);
}

TEST_CASE("values detail keeps every canonical control value while excluding large rendering metadata") {
    struct Identity : mm::MoonModule {
        char sample[16] = "fresh", password[16] = "private";
        uint8_t hidden = 17;
        void defineControls() override {
            controls_.addReadOnly("sample", sample, sizeof(sample));
            controls_.addPassword("password", password, sizeof(password));
            controls_.addControl("hidden", hidden);
            controls_.setHidden(controls_.count() - 1, true);
            controls_.setReadOnly(controls_.count() - 1, true);
        }
    };
    mm::Scheduler scheduler;
    struct Cleanup { mm::Scheduler& scheduler; ~Cleanup() { scheduler.release(); } } cleanup{scheduler};
    auto* lots = new Lots;
    auto* identity = new Identity;
    REQUIRE(lots != nullptr); REQUIRE(identity != nullptr);
    lots->setName("Lots"); identity->setName("Identity");
    lots->pick = 123; lots->palette = 17;
    scheduler.addModule(lots); scheduler.addModule(identity); scheduler.setup();
    mm::HttpServerModule http;
    http.setScheduler(&scheduler);
    const auto fullSubscription = subscribed(R"({"subscribe":["*"]})");
    const auto valuesSubscription = subscribed(R"({"subscribe":["*"],"detail":"values"})");
    const auto full = whole(http, &fullSubscription), values = whole(http, &valuesSubscription);
    CHECK(full == whole(http, nullptr));
    CHECK(values.size() < 4096); // 200 select options, 200 palettes and 60 list details do not ride the bootstrap.
    CHECK(values.size() * 4 < full.size());
    CHECK(values.find("\"options\"") == std::string::npos);
    CHECK(values.find("\"optionSets\"") == std::string::npos);
    CHECK(values.find("\"detail\":[") == std::string::npos);
    CHECK(values.find("\"default\"") == std::string::npos);
    CHECK(values.find("\"min\"") == std::string::npos);
    CHECK(values.find("\"max\"") == std::string::npos);
    CHECK(values.find("private") == std::string::npos);
    CHECK(values.find("\"readonly\":true") != std::string::npos);
    CHECK(values.find("\"hidden\":true") != std::string::npos);
    mm::json::JsonDoc fullDoc, valuesDoc;
    REQUIRE(mm::json::parse(full.c_str(), fullDoc));
    REQUIRE(mm::json::parse(values.c_str(), valuesDoc));
    const auto* fullModules = mm::json::member(fullDoc, fullDoc.rootNode(), "modules");
    const auto* valueModules = mm::json::member(valuesDoc, valuesDoc.rootNode(), "modules");
    REQUIRE(mm::json::arraySize(fullDoc, fullModules) == mm::json::arraySize(valuesDoc, valueModules));
    const auto* detail = mm::json::member(valuesDoc, valuesDoc.rootNode(), "detail");
    REQUIRE(detail != nullptr); CHECK(std::strcmp(detail->str, "values") == 0);
    for (int module = 0; module < mm::json::arraySize(fullDoc, fullModules); ++module) {
        const auto* fullControls = mm::json::member(fullDoc, mm::json::element(fullDoc, fullModules, module), "controls");
        const auto* valueControls = mm::json::member(valuesDoc, mm::json::element(valuesDoc, valueModules, module), "controls");
        REQUIRE(mm::json::arraySize(fullDoc, fullControls) == mm::json::arraySize(valuesDoc, valueControls));
        for (int control = 0; control < mm::json::arraySize(fullDoc, fullControls); ++control) {
            const auto* fullControl = mm::json::element(fullDoc, fullControls, control);
            const auto* valueControl = mm::json::element(valuesDoc, valueControls, control);
            for (const char* field : {"name", "type", "value"}) {
                mm::JsonSink before, after;
                mm::json::write(before, fullDoc, mm::json::member(fullDoc, fullControl, field));
                mm::json::write(after, valuesDoc, mm::json::member(valuesDoc, valueControl, field));
                CHECK(std::string(before.data(), before.size()) == std::string(after.data(), after.size()));
            }
        }
    }
    for (size_t target : {size_t(1), size_t(7), size_t(64), size_t(2048)}) {
        mm::HttpServerModule::TreeCursor cursor;
        cursor.sub = &valuesSubscription;
        std::string joined;
        bool done = false;
        size_t largest = 0;
        for (int fragment = 0; !done && fragment < 10000; ++fragment) {
            mm::JsonSink sink;
            done = http.writeTreeFragment(sink, cursor, target);
            joined.append(sink.data(), sink.size());
            largest = std::max(largest, sink.size());
        }
        REQUIRE(done);
        CHECK(joined == values);
        CHECK(largest < target + 200);
    }
}

TEST_CASE("values detail preserves subscription outlines and participates in subscription identity") {
    Tree tree;
    mm::HttpServerModule http;
    http.setScheduler(&tree.s);
    const auto values = subscribed(R"({"subscribe":["A"],"detail":"values"})");
    const auto full = subscribed(R"({"subscribe":["A"]})");
    const auto explicitFull = subscribed(R"({"subscribe":["A"],"detail":"full"})");
    CHECK(values.valuesOnly);
    CHECK_FALSE(full.valuesOnly);
    CHECK_FALSE(values == full);
    CHECK(full == explicitFull);
    const auto doc = whole(http, &values);
    CHECK(objectOf(doc, "K").find("\"controls\"") != std::string::npos);
    CHECK(objectOf(doc, "L").find("\"controls\"") == std::string::npos);
    CHECK(doc.find("{\"name\":\"W\"") != std::string::npos);
    CHECK(whole(http, &full) == whole(http, &explicitFull));
    CHECK(whole(http, &full).find("\"detail\":\"values\"") == std::string::npos);
}

// A level marked percent tells the UI so in the state it reads; a value that counts something carries no such flag.
TEST_CASE("a percent level carries its flag in the state, and a counted value does not") {
    struct Lamp : mm::MoonModule {
        uint8_t level = 128, count = 3;
        void defineControls() override {
            controls_.addControl("level", level, 0, 255);
            controls_.setPercent(controls_.count() - 1);
            controls_.addControl("count", count, 0, 255);
        }
    };
    mm::Scheduler s;
    auto* lamp = new Lamp(); lamp->setName("Lamp");
    s.addModule(lamp); s.setup();
    mm::HttpServerModule http;
    http.setScheduler(&s);
    const std::string state = whole(http, nullptr);
    const auto level = state.find("\"name\":\"level\""), count = state.find("\"name\":\"count\"");
    REQUIRE(level != std::string::npos); REQUIRE(count != std::string::npos); REQUIRE(level < count);
    CHECK(state.substr(level, count - level).find("\"percent\":true") != std::string::npos);
    CHECK(state.find("\"percent\":true", count) == std::string::npos);
    mm::Scheduler::deleteTree(lamp);
}

TEST_CASE("one module's JSON is its entry in the full state, children included") {
    Tree t;
    mm::HttpServerModule http;
    http.setScheduler(&t.s);
    mm::HttpServerModule::TreeCursor c;
    c.single = t.b;
    mm::JsonSink sink;
    CHECK(http.writeTreeFragment(sink, c, 0));
    const std::string b(sink.data(), sink.size());
    CHECK(whole(http, nullptr).find(b) != std::string::npos);
}

TEST_CASE("a module outside the subscription is sent as its outline, its shape kept and its controls left out") {
    Tree t;
    mm::HttpServerModule http;
    http.setScheduler(&t.s);
    const auto sub = subscribed("{\"subscribe\":[\"A\"]}");
    const std::string doc = whole(http, &sub);
    CHECK(objectOf(doc, "K").find("\"controls\"") != std::string::npos);
    CHECK(objectOf(doc, "B").find("\"controls\"") == std::string::npos);
    CHECK(objectOf(doc, "L").find("\"controls\"") == std::string::npos);
    CHECK(doc.find("{\"name\":\"W\"") != std::string::npos);   // the outline keeps every module
    CHECK(doc.size() < whole(http, nullptr).size());
}

TEST_CASE("a subscribed module deep in the tree brings its own controls and not its parent's") {
    Tree t;
    mm::HttpServerModule http;
    http.setScheduler(&t.s);
    const auto sub = subscribed("{\"subscribe\":[\"L\"]}");
    const std::string doc = whole(http, &sub);
    CHECK(objectOf(doc, "L").find("\"controls\"") != std::string::npos);
    CHECK(objectOf(doc, "B").find("\"controls\"") == std::string::npos);
    CHECK(objectOf(doc, "W").find("\"controls\"") == std::string::npos);
}

TEST_CASE("the subscribe message names modules, and a star subscribes to the whole tree") {
    mm::HttpServerModule::Subscription sub;
    CHECK(sub.parse("{\"subscribe\":[\"A\",\"System\"]}"));
    CHECK(sub.count == 2);
    CHECK(!sub.all);
    CHECK(sub.parse("{\"subscribe\":[\"*\"]}"));
    CHECK(sub.all);
    CHECK(sub.count == 0);
    CHECK(!sub.parse("{\"subscribe\":\"A\"}"));   // not an array: no subscription read
    CHECK(sub.parse("{\"subscribe\":[\"1\",\"2\",\"3\",\"4\",\"5\",\"6\",\"7\",\"8\",\"9\",\"10\"]}"));
    CHECK(sub.count == mm::HttpServerModule::Subscription::kMaxModules);
    CHECK(sub.parse("{\"subscribe\":[]}"));   // an empty list is the outline alone
    CHECK(sub.count == 0);
}

TEST_CASE("a client is patched only with the modules it subscribes to") {
    Tree t;
    mm::HttpServerModule http;
    http.setScheduler(&t.s);
    http.subscribeForTest(0, "{\"subscribe\":[\"A\"]}");
    http.subscribeForTest(1, "{\"subscribe\":[\"B\"]}");
    http.subscribeForTest(2, "{\"subscribe\":[]}");
    http.baselineLeafHashesForTest();
    t.k->value = 70;
    t.l->value = 71;
    mm::JsonSink outs[3];
    http.buildClientPatchesForTest(outs, 3);
    const std::string a(outs[0].data()), b(outs[1].data()), none(outs[2].data());
    CHECK(a.find("\"path\":\"K/value\"") != std::string::npos);
    CHECK(a.find("L/value") == std::string::npos);
    CHECK(b.find("\"path\":\"L/value\"") != std::string::npos);
    CHECK(b.find("K/value") == std::string::npos);
    CHECK(none == "{\"patch\":[]}");
}

TEST_CASE("a subscription entry matches a module's type as well as its name") {
    Tree t;
    mm::HttpServerModule http;
    http.setScheduler(&t.s);
    t.l->setTypeName("Dial");
    const auto sub = subscribed("{\"subscribe\":[\"Dial\"]}");
    const std::string doc = whole(http, &sub);
    CHECK(objectOf(doc, "L").find("\"controls\"") != std::string::npos);
    CHECK(objectOf(doc, "K").find("\"controls\"") == std::string::npos);
}

TEST_CASE("a subscription change resyncs that client alone, and the same subscription again does not") {
    Tree t;
    mm::HttpServerModule http;
    http.setScheduler(&t.s);
    http.clearFullResyncForTest();
    http.subscribeForTest(0, "{\"subscribe\":[\"A\"]}");
    CHECK(http.clientResyncPendingForTest(0));
    CHECK(!http.clientResyncPendingForTest(1));
    CHECK(!http.fullResyncPendingForTest());
    http.subscribeForTest(1, "{\"subscribe\":[\"A\"]}");
    http.subscribeForTest(1, "{\"subscribe\":[\"A\"]}");
    CHECK(http.clientResyncPendingForTest(1));
}

namespace {

constexpr uint16_t kTestPort = 18092;   // 18090 and 18091 belong to the connections and SD card tests

// One masked client frame, as a browser sends it (RFC 6455 §5.3): text by default, or a control opcode.
std::vector<uint8_t> clientFrame(const char* text, uint8_t opcode = 0x1) {
    const size_t n = std::strlen(text);
    std::vector<uint8_t> f = {static_cast<uint8_t>(0x80 | opcode), static_cast<uint8_t>(0x80 | n), 1, 2, 3, 4};
    for (size_t i = 0; i < n; i++) f.push_back(static_cast<uint8_t>(text[i]) ^ f[2 + (i & 3)]);
    return f;
}

// A WebSocket client over loopback, reading the server's unmasked frames as the server ticks.
struct WsClient {
    mm::HttpServerModule& http;
    mm::platform::TcpConnection conn;
    std::vector<uint8_t> in;

    bool open(uint16_t port, const char* path = "/ws") {
        if (!connectAndAccept(http, conn, port)) return false;
        const std::string upgrade = std::string("GET ") + path + " HTTP/1.1\r\nHost: test\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                                    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";
        if (!conn.write(reinterpret_cast<const uint8_t*>(upgrade.data()), upgrade.size())) return false;
        std::string handshake;
        for (int i = 0; i < 300 && handshake.find("\r\n\r\n") == std::string::npos; i++) {
            http.tick20ms();
            char buf[512];
            const int n = conn.read(reinterpret_cast<uint8_t*>(buf), sizeof(buf));
            if (n > 0) handshake.append(buf, n);
            mm::platform::delayMs(1);
        }
        return handshake.rfind("HTTP/1.1 101", 0) == 0;
    }

    bool send(const char* text, uint8_t opcode = 0x1) {
        const auto f = clientFrame(text, opcode);
        return conn.write(f.data(), f.size());
    }

    // Subscribe and take the whole first document until its stream ends: a client waiting for patches.
    bool settleOn(const char* subscribe) {
        std::vector<uint8_t> opcodes;
        return send(subscribe) && message(opcodes).rfind("{\"modules\":[", 0) == 0 && message(opcodes, 20).empty();
    }

    // The next whole message and the opcode of each of its frames, empty when none arrives within `ticks`.
    std::string message(std::vector<uint8_t>& opcodes, int ticks = 2000,
                        std::vector<uint8_t>* frameHeaders = nullptr, const std::function<void()>& beforeTick = {}) {
        std::string out;
        opcodes.clear();
        for (int i = 0; i < ticks; i++) {
            if (beforeTick) beforeTick();
            http.tick20ms();
            uint8_t buf[4096];
            const int n = conn.read(buf, sizeof(buf));
            if (n > 0) in.insert(in.end(), buf, buf + n);
            while (in.size() >= 2) {
                size_t len = in[1] & 0x7F, hdr = 2;
                if (len == 126) { if (in.size() < 4) break; len = (size_t(in[2]) << 8) | in[3]; hdr = 4; }
                if (in.size() < hdr + len) break;
                opcodes.push_back(in[0] & 0x0F);
                if (frameHeaders) frameHeaders->push_back(in[0]);
                const bool fin = (in[0] & 0x80) != 0;
                out.append(reinterpret_cast<const char*>(in.data() + hdr), len);
                in.erase(in.begin(), in.begin() + static_cast<long>(hdr + len));
                if (fin) return out;
            }
            if (n <= 0) mm::platform::delayMs(1);
        }
        return "";
    }
};

// Released however the test ends: a live server left behind keeps the static hooks pointing at it.
struct Served {
    Tree t;
    mm::HttpServerModule http;
    Served() { http.setScheduler(&t.s); http.port = kTestPort; http.setup(); }
    ~Served() { http.release(); }
};

struct PressureNode : mm::MoonModule {
    bool armed = false;
    mutable bool triggered = false;
    const char* tags() const override {
        if (armed && !triggered) {
            triggered = true;
            mm::platform::setTestFreeInternalHeap(mm::platform::HEAP_RESERVE);
        }
        return "";
    }
};

struct BackgroundFiles {
    std::string previous = mm::platform::fsRootPath();
    std::string root;
    BackgroundFiles() {
        static unsigned sequence = 0;
        root = (std::filesystem::temp_directory_path() / ("mm_http_background_" + std::to_string(++sequence))).string();
        std::filesystem::remove_all(root);
        mm::platform::fsSetRoot(root.c_str());
        REQUIRE(mm::platform::fsMount());
        REQUIRE(mm::platform::fsMkdir(mm::PresetStore::kDirectory));
        for (unsigned i = 0; i < 64; ++i) {
            char path[96];
            std::snprintf(path, sizeof(path), "%s/Guard%02u.json", mm::PresetStore::kDirectory, i);
            const std::string document = "{\"$slot\":" + std::to_string(i) + ",\"Effects\":{},\"$padding\":\"" +
                std::string(i == 0 ? 1250 : 0, 'p') + "\"}";
            REQUIRE(mm::platform::fsWriteAtomic(path, document.data(), document.size()));
        }
        const std::string file(8000, 'f');
        REQUIRE(mm::platform::fsWriteAtomic("/guard.bin", file.data(), file.size()));
        REQUIRE(mm::ModuleFactory::registerType<Wide>("BackgroundWide"));
    }
    ~BackgroundFiles() {
        mm::platform::fsSetRoot(previous.c_str());
        std::filesystem::remove_all(root);
    }
};

struct BackgroundClock {
    BackgroundClock() { mm::platform::setTestNowMs(1000); }
    ~BackgroundClock() { mm::platform::setTestNowMs(0); mm::platform::setTestFreeInternalHeap(0); }
};

std::string beginBackgroundHttp(Served& served, mm::platform::TcpConnection& client, const char* path) {
    REQUIRE(connectAndAccept(served.http, client, kTestPort));
    const std::string request = std::string("GET ") + path + " HTTP/1.1\r\nHost: test\r\n\r\n";
    REQUIRE(client.write(reinterpret_cast<const uint8_t*>(request.data()), request.size()));
    const auto reply = pumpUntil(served.http, client, 1);
    REQUIRE(reply.rfind("HTTP/1.1 200", 0) == 0);
    return reply;
}

}  // namespace

TEST_CASE("low-headroom generated HTTP holds background patch allocation and preserves the changed value until EOF") {
    for (const char* path : {"/api/state", "/api/types", "/api/presets"}) {
        for (bool announce : {false, true}) {
            INFO("path=" << path << " early notification=" << announce);
            BackgroundClock clock;
            BackgroundFiles files;
            Served served;
            WsClient client{served.http, {}, {}};
            REQUIRE(client.open(kTestPort));
            REQUIRE(client.settleOn("{\"subscribe\":[\"B\"]}"));
            served.http.setSendBudget(mm::platform::HEAP_RESERVE, 1);
            mm::platform::setTestFreeInternalHeap(mm::HttpServerModule::kSendHeadroom - 1);
            mm::platform::TcpConnection foreground;
            std::string response = beginBackgroundHttp(served, foreground, path);
            mm::platform::setTestFreeInternalHeap(mm::platform::HEAP_RESERVE);
            served.t.l->value = 73;
            served.http.startValueWalkForTest();
            if (announce) served.t.l->notifyValuesChanged();
            const auto held = mm::platform::allocatedBytes();
            for (unsigned tick = 0; tick < 20; ++tick) {
                served.http.tick20ms();
                CHECK(mm::platform::allocatedBytes() == held); // no background frame queued behind an undrainable source
                uint8_t pending[4096];
                CHECK(client.conn.read(pending, sizeof(pending)) == -1);
            }
            mm::platform::setTestFreeInternalHeap(mm::HttpServerModule::kSendHeadroom - 1);
            served.http.setSendBudget(mm::platform::HEAP_RESERVE, 256);
            response += readUntilClosed(served.http, foreground);
            CHECK(readAfterClose(foreground) == 0);
            const auto header = response.find("\r\n\r\n");
            REQUIRE(header != std::string::npos);
            mm::json::JsonDoc complete;
            CHECK(mm::json::parse(response.c_str() + header + 4, complete));
            std::vector<uint8_t> opcodes;
            const auto patch = client.message(opcodes);
            CHECK(patch.find("\"path\":\"L/value\",\"value\":73") != std::string::npos);
            CHECK(client.message(opcodes, 20).empty()); // the hash baseline did not lose or duplicate the pending value
        }
    }
}

TEST_CASE("normal headroom and non-generated HTTP sources keep background changed-value work enabled") {
    struct Scenario { const char* path; bool headroom; };
    for (const auto scenario : {Scenario{"/api/presets", true},
                                Scenario{"/api/file?path=/guard.bin", false}, Scenario{"/api/presets/Guard00", false}}) {
        INFO("path=" << scenario.path << " normal headroom=" << scenario.headroom);
        BackgroundClock clock;
        BackgroundFiles files;
        Served served;
        WsClient client{served.http, {}, {}};
        REQUIRE(client.open(kTestPort));
        REQUIRE(client.settleOn("{\"subscribe\":[\"B\"]}"));
        served.http.setSendBudget(mm::platform::HEAP_RESERVE, 1);
        mm::platform::setTestFreeInternalHeap(scenario.headroom ? mm::HttpServerModule::kSendHeadroom + 1
                                                             : mm::HttpServerModule::kSendHeadroom - 1);
        mm::platform::TcpConnection foreground;
        std::string response = beginBackgroundHttp(served, foreground, scenario.path);
        if (!scenario.headroom) mm::platform::setTestFreeInternalHeap(mm::platform::HEAP_RESERVE);
        served.t.l->value = 73;
        served.t.l->notifyValuesChanged();
        std::vector<uint8_t> opcodes;
        std::string patch;
        if (scenario.headroom) {
            patch = client.message(opcodes, 1000);
            uint8_t pending[4096];
            const int count = foreground.read(pending, sizeof(pending));
            REQUIRE(count != 0); // with headroom the patch leaves before the generated answer finishes
            if (count > 0) response.append(reinterpret_cast<const char*>(pending), static_cast<size_t>(count));
        } else {
            const auto held = mm::platform::allocatedBytes();
            served.http.tick20ms();
            CHECK(mm::platform::allocatedBytes() > held); // File/None permit background queueing even while transport waits
            mm::platform::setTestFreeInternalHeap(mm::HttpServerModule::kSendHeadroom - 1);
        }
        served.http.setSendBudget(mm::platform::HEAP_RESERVE, 256);
        response += readUntilClosed(served.http, foreground);
        CHECK(readAfterClose(foreground) == 0);
        if (!scenario.headroom) patch = client.message(opcodes);
        REQUIRE(patch.find("\"path\":\"L/value\",\"value\":73") != std::string::npos);
        const auto header = response.find("\r\n\r\n");
        REQUIRE(header != std::string::npos);
        CHECK(response.size() > header + 4);
    }
}

TEST_CASE("low-headroom generated HTTP preserves a requested WebSocket resync until its answer finishes") {
    BackgroundClock clock;
    BackgroundFiles files;
    Served served;
    WsClient client{served.http, {}, {}};
    REQUIRE(client.open(kTestPort));
    REQUIRE(client.settleOn("{\"subscribe\":[\"B\"]}"));
    served.http.setSendBudget(mm::platform::HEAP_RESERVE, 1);
    mm::platform::setTestFreeInternalHeap(mm::HttpServerModule::kSendHeadroom - 1);
    mm::platform::TcpConnection foreground;
    std::string response = beginBackgroundHttp(served, foreground, "/api/presets");
    mm::platform::setTestFreeInternalHeap(mm::platform::HEAP_RESERVE);
    served.t.l->value = 73;
    served.http.requestFullResyncForTest();
    const auto held = mm::platform::allocatedBytes();
    for (unsigned tick = 0; tick < 20; ++tick) {
        served.http.tick20ms();
        CHECK(served.http.fullResyncPendingForTest());
        CHECK(mm::platform::allocatedBytes() == held);
        uint8_t pending[4096];
        CHECK(client.conn.read(pending, sizeof(pending)) == -1);
    }
    mm::platform::setTestFreeInternalHeap(mm::HttpServerModule::kSendHeadroom - 1);
    served.http.setSendBudget(mm::platform::HEAP_RESERVE, 256);
    response += readUntilClosed(served.http, foreground);
    CHECK(readAfterClose(foreground) == 0);
    std::vector<uint8_t> opcodes;
    const auto message = client.message(opcodes);
    const auto subscription = subscribed("{\"subscribe\":[\"B\"]}");
    CHECK(message == whole(served.http, &subscription));
    CHECK(objectOf(message, "L").find("\"value\":73") != std::string::npos);
    CHECK_FALSE(served.http.fullResyncPendingForTest());
}

TEST_CASE("WebSocket tree frames preserve JSON FIN and cumulative admitted budgets down to one byte") {
    struct Reset {
        ~Reset() { mm::platform::setTestFreeInternalHeap(0); mm::platform::setTestNowMs(0); }
    } reset;
    mm::platform::setTestNowMs(1000);
    for (const char* request : {R"({"subscribe":["A"]})", R"({"subscribe":["A"],"detail":"values"})"}) {
        for (size_t quantum : {size_t{1}, size_t{2}, size_t{3}, size_t{10}, size_t{11},
                               size_t{63}, size_t{64}, size_t{125}, size_t{126}, size_t{1024}}) {
            CAPTURE(request);
            CAPTURE(quantum);
            Served served;
            WsClient client{served.http, {}, {}};
            REQUIRE(client.open(kTestPort));
            served.http.setSendBudget(mm::platform::HEAP_RESERVE, quantum);
            mm::platform::setTestFreeInternalHeap(mm::platform::HEAP_RESERVE + 8192);
            const auto subscription = subscribed(request);
            const auto expected = whole(served.http, &subscription);
            REQUIRE(client.send(request));
            std::string document;
            bool finished = false;
            unsigned frameCount = 0;
            size_t totalReceived = 0;
            for (unsigned tick = 0; tick < 5000 && !finished; ++tick) {
                served.http.tick20ms();
                // TCP can combine earlier ticks' writes, so compare arrivals with the accumulated send allowance.
                mm::platform::delayMs(1);
                size_t received = 0;
                uint8_t bytes[4096];
                for (int n; (n = client.conn.read(bytes, sizeof(bytes))) > 0;) {
                    received += static_cast<size_t>(n);
                    client.in.insert(client.in.end(), bytes, bytes + n);
                }
                totalReceived += received;
                REQUIRE(totalReceived <= (tick + 1) * quantum);
                while (client.in.size() >= 2) {
                    REQUIRE((client.in[1] & 0x80) == 0); // Server frames are unmasked.
                    const uint8_t lengthCode = client.in[1] & 0x7f;
                    REQUIRE(lengthCode != 127); // This fixture has no 64-bit-length frames.
                    size_t payload = lengthCode, header = 2;
                    if (payload == 126) {
                        if (client.in.size() < 4) break;
                        payload = (size_t(client.in[2]) << 8) | client.in[3];
                        header = 4;
                        CHECK(payload >= 126);
                    }
                    if (client.in.size() < header + payload) break;
                    CHECK((client.in[0] & 0x0f) == (frameCount == 0 ? 0x1 : 0x0));
                    REQUIRE(payload > 0); // A fragment target below the reserved prefix still advances the document.
                    document.append(reinterpret_cast<const char*>(client.in.data() + header), payload);
                    finished = (client.in[0] & 0x80) != 0;
                    ++frameCount;
                    client.in.erase(client.in.begin(), client.in.begin() + static_cast<long>(header + payload));
                    if (finished) break;
                }
            }
            REQUIRE(finished);
            CHECK(document == expected);
            CHECK(client.in.empty());
            CHECK(frameCount > 0);
        }
    }
}

TEST_CASE("WebSocket tree prefix growth failure closes before a partial first frame and releases ownership") {
    struct Reset { ~Reset() { mm::platform::setTestAllocRefusedFrom(0); } } reset;
    struct Boundary : mm::MoonModule {
        std::string tag = "x";
        bool armed = false;
        mutable bool triggered = false;
        const char* tags() const override {
            if (armed && !triggered) {
                triggered = true;
                // The prefix's 256-byte first block already exists; refuse every larger growth attempt.
                mm::platform::setTestAllocRefusedFrom(257);
            }
            return tag.c_str();
        }
    };
    mm::Scheduler scheduler;
    mm::HttpServerModule http;
    struct Cleanup {
        mm::HttpServerModule& http;
        mm::Scheduler& scheduler;
        ~Cleanup() { http.release(); scheduler.release(); }
    } cleanup{http, scheduler};
    auto* root = new Boundary;
    REQUIRE(root != nullptr);
    root->setName("Boundary");
    scheduler.addModule(root);
    scheduler.setup();
    http.setScheduler(&scheduler);
    http.port = kTestPort;
    http.setSendBudget(mm::platform::HEAP_RESERVE, 1024);
    http.setup();
    std::string expected = whole(http, nullptr);
    REQUIRE(expected.size() < 250);
    root->tag.append(250 - expected.size(), 'x');
    expected = whole(http, nullptr);
    REQUIRE(expected.size() == 250); // Payload alone fits 256; payload plus the ten-byte prefix does not.
    WsClient client{http, {}, {}};
    REQUIRE(client.open(kTestPort));
    http.baselineLeafHashesForTest(); // Keep the legitimate subscription hash cache outside the fragment ownership measurement.
    const auto held = mm::platform::allocatedBytes();
    root->armed = true;
    REQUIRE(client.send(R"({"subscribe":["*"]})"));
    bool closed = false;
    size_t received = 0;
    for (unsigned tick = 0; tick < 100 && !closed; ++tick) {
        http.tick20ms();
        uint8_t bytes[512];
        const int n = client.conn.read(bytes, sizeof(bytes));
        if (n > 0) received += static_cast<size_t>(n);
        closed = n == 0;
        if (n < 0) mm::platform::delayMs(1);
    }
    REQUIRE(root->triggered);
    CHECK(closed);
    CHECK(received == 0);
    CHECK(mm::platform::allocatedBytes() <= held);
    http.release();
    CHECK(mm::platform::allocatedBytes() < held);
}

TEST_CASE("WebSocket state retries pressure without losing first continuation or final frame state") {
    struct Reset {
        ~Reset() { mm::platform::setTestFreeInternalHeap(0); mm::platform::setTestNowMs(0); }
    } reset;
    mm::platform::setTestNowMs(1000);
    mm::Scheduler scheduler;
    mm::HttpServerModule http;
    struct Cleanup {
        mm::HttpServerModule& http;
        mm::Scheduler& scheduler;
        ~Cleanup() { http.release(); scheduler.release(); }
    } cleanup{http, scheduler};
    auto* root = new PressureNode;
    auto* late = new PressureNode;
    auto* wide = new Wide;
    REQUIRE(root != nullptr); REQUIRE(late != nullptr); REQUIRE(wide != nullptr);
    root->setName("Root"); root->setTypeName("PressureNode");
    wide->setName("Wide"); wide->setTypeName("Wide");
    late->setName("Late"); late->setTypeName("PressureNode");
    root->addChild(wide);
    root->addChild(late);
    scheduler.addModule(root);
    scheduler.setup();
    http.setScheduler(&scheduler);
    http.port = kTestPort;
    http.setSendBudget(mm::platform::HEAP_RESERVE, 128);
    http.setup();
    WsClient client{http, {}, {}};
    REQUIRE(client.open(kTestPort));
    PressureNode* target = root;
    const char* request = R"({"subscribe":["Root"]})";
    SUBCASE("the first frame is retried with the text opcode") {}
    SUBCASE("a late continuation carrying the footer is retried with its original cursor") { target = late; }
    SUBCASE("a values-only snapshot retries without committing its cursor") {
        request = R"({"subscribe":["Root"],"detail":"values"})";
        target = late;
    }
    const auto subscription = subscribed(request);
    const auto expected = whole(http, &subscription);
    mm::platform::setTestFreeInternalHeap(mm::platform::HEAP_RESERVE + 8192);
    target->armed = true;
    REQUIRE(client.send(request));
    unsigned paused = 0;
    std::vector<uint8_t> opcodes, frames;
    const auto message = client.message(opcodes, 4000, &frames, [&] {
        if (!target->triggered || paused >= 20) return;
        // Preserve bytes already sent before pressure, while detecting a close.
        uint8_t pending[4096];
        const int count = client.conn.read(pending, sizeof(pending));
        CHECK(count != 0);
        if (count > 0) client.in.insert(client.in.end(), pending, pending + count);
        if (++paused == 20) mm::platform::setTestFreeInternalHeap(mm::platform::HEAP_RESERVE + 8192);
    });
    REQUIRE(target->triggered);
    CHECK(paused == 20);
    CHECK(message == expected);
    REQUIRE(frames.size() > 1);
    REQUIRE(opcodes.size() == frames.size());
    CHECK(opcodes.front() == 0x1);
    for (size_t i = 1; i < opcodes.size(); ++i) CHECK(opcodes[i] == 0x0);
    for (size_t i = 0; i + 1 < frames.size(); ++i) CHECK((frames[i] & 0x80) == 0);
    CHECK((frames.back() & 0x80) != 0);
}

TEST_CASE("WebSocket values detail survives mode changes schema resync and reconnect without changing frame semantics") {
    Served served;
    served.http.setSendBudget(mm::platform::HEAP_RESERVE, 64);
    WsClient client{served.http, {}, {}};
    REQUIRE(client.open(kTestPort));
    REQUIRE(client.settleOn(R"({"subscribe":["B"]})"));
    const char* request = R"({"subscribe":["B"],"detail":"values"})";
    const auto values = subscribed(request);
    REQUIRE(client.send(request));
    std::vector<uint8_t> opcodes, frames;
    const auto snapshot = client.message(opcodes, 2000, &frames);
    CHECK(snapshot == whole(served.http, &values));
    REQUIRE(frames.size() > 1);
    CHECK(opcodes.front() == 0x1);
    for (size_t i = 1; i < opcodes.size(); ++i) CHECK(opcodes[i] == 0x0);
    for (size_t i = 0; i + 1 < frames.size(); ++i) CHECK((frames[i] & 0x80) == 0);
    CHECK((frames.back() & 0x80) != 0);
    REQUIRE(client.send(request));
    CHECK(client.message(opcodes, 20).empty()); // The same mode and scope do not request another document.
    REQUIRE(client.send(R"({"resync":27})"));
    const auto resynced = client.message(opcodes);
    CHECK(resynced.find("\"detail\":\"values\"") != std::string::npos);
    CHECK(resynced.find("\"resync\":27") != std::string::npos);
    uint8_t extraValue = 73;
    served.t.l->controls().addControl("added", extraValue);
    served.http.requestFullResyncForTest();
    const auto changed = client.message(opcodes);
    CHECK(changed == whole(served.http, &values));
    CHECK(changed.find("\"name\":\"added\",\"type\":\"uint8\",\"value\":73}") != std::string::npos);
    REQUIRE(client.send(R"({"subscribe":["B"]})"));
    const auto full = subscribed(R"({"subscribe":["B"]})");
    CHECK(client.message(opcodes) == whole(served.http, &full));
    client.conn.close();
    for (int tick = 0; tick < 5; ++tick) served.http.tick20ms();
    WsClient reconnected{served.http, {}, {}};
    REQUIRE(reconnected.open(kTestPort));
    REQUIRE(reconnected.send(request));
    CHECK(reconnected.message(opcodes) == whole(served.http, &values));
}

TEST_CASE("values and full subscriptions receive identical scoped value patches") {
    Tree tree;
    mm::HttpServerModule http;
    http.setScheduler(&tree.s);
    http.subscribeForTest(0, R"({"subscribe":["B"]})");
    http.subscribeForTest(1, R"({"subscribe":["B"],"detail":"values"})");
    http.baselineLeafHashesForTest();
    tree.l->value = 73;
    tree.k->value = 72;
    mm::JsonSink patches[2];
    http.buildClientPatchesForTest(patches, 2);
    const std::string full(patches[0].data(), patches[0].size()), values(patches[1].data(), patches[1].size());
    CHECK(full == values);
    CHECK(values.find("\"path\":\"L/value\",\"value\":73") != std::string::npos);
    CHECK(values.find("K/value") == std::string::npos);
}

TEST_CASE("the full state streams as one fragmented message that reassembles to the subscribed document") {
    // A board whose largest block is 16 KB takes 2 KB fragments; a desktop's would hold this tree in one.
    struct Block { Block() { mm::platform::setTestMaxAllocBlock(16384); } ~Block() { mm::platform::setTestMaxAllocBlock(0); } } block;
    Served sv;
    WsClient c{sv.http, {}, {}};
    REQUIRE(c.open(kTestPort));
    REQUIRE(c.send("{\"subscribe\":[\"B\"]}"));
    std::vector<uint8_t> opcodes;
    const std::string message = c.message(opcodes);
    REQUIRE(opcodes.size() > 1);   // W's forty controls take more than one fragment
    CHECK(opcodes.front() == 0x1);
    for (size_t i = 1; i < opcodes.size(); i++) CHECK(opcodes[i] == 0x0);
    const auto expected = subscribed("{\"subscribe\":[\"B\"]}");
    CHECK(message == whole(sv.http, &expected));
}

// Below the headroom the stream once waited for room a classic ESP32 never has, so its page got no state. It now takes its turn with the other sends.
TEST_CASE("the state stream still arrives whole while internal RAM is short") {
    struct Heap { ~Heap() { mm::platform::setTestFreeInternalHeap(0); } } heap;   // restored however the test ends
    Served sv;
    WsClient c{sv.http, {}, {}};
    REQUIRE(c.open(kTestPort));
    mm::platform::setTestFreeInternalHeap(mm::HttpServerModule::kSendHeadroom - 1);
    REQUIRE(c.send("{\"subscribe\":[\"B\"]}"));
    std::vector<uint8_t> opcodes;
    const std::string message = c.message(opcodes);
    const auto expected = subscribed("{\"subscribe\":[\"B\"]}");
    CHECK(message == whole(sv.http, &expected));
}

TEST_CASE("a client that subscribes is sent a full state, and the client already there is not") {
    Served sv;
    WsClient first{sv.http, {}, {}}, second{sv.http, {}, {}};
    REQUIRE(first.open(kTestPort));
    REQUIRE(first.send("{\"subscribe\":[\"A\"]}"));
    std::vector<uint8_t> opcodes;
    REQUIRE(first.message(opcodes).rfind("{\"modules\":[", 0) == 0);

    REQUIRE(second.open(kTestPort));
    REQUIRE(second.send("{\"subscribe\":[\"B\"]}"));
    CHECK(second.message(opcodes).rfind("{\"modules\":[", 0) == 0);
    CHECK(first.message(opcodes, 50).empty());   // no patch is due and no document is owed
}

TEST_CASE("a Ping is answered with a Pong carrying the same payload") {
    Served sv;
    WsClient c{sv.http, {}, {}};
    REQUIRE(c.open(kTestPort));
    REQUIRE(c.send("are you there", 0x9));
    std::vector<uint8_t> opcodes;
    CHECK(c.message(opcodes) == "are you there");
    REQUIRE(opcodes.size() == 1);
    CHECK(opcodes[0] == 0xA);
}

TEST_CASE("a Close is answered with a Close echoing its status code, and the connection ends") {
    Served sv;
    WsClient c{sv.http, {}, {}};
    REQUIRE(c.open(kTestPort));
    REQUIRE(c.send("\x03\xe8" "bye", 0x8));   // 1000, normal closure, and a reason
    std::vector<uint8_t> opcodes;
    CHECK(c.message(opcodes) == "\x03\xe8");
    REQUIRE(opcodes.size() == 1);
    CHECK(opcodes[0] == 0x8);
    uint8_t buf[16];
    int n = -1;
    for (int i = 0; i < 300 && n != 0; i++) { sv.http.tick20ms(); n = c.conn.read(buf, sizeof(buf)); mm::platform::delayMs(1); }
    CHECK(n == 0);   // the server closed its side
}

TEST_CASE("a client asking for its view again gets it, carrying the id it asked with") {
    Served sv;
    WsClient c{sv.http, {}, {}};
    REQUIRE(c.open(kTestPort));
    REQUIRE(c.send("{\"subscribe\":[\"A\"]}"));
    std::vector<uint8_t> opcodes;
    const std::string first = c.message(opcodes);
    CHECK(first.find("\"resync\"") == std::string::npos);   // the subscription's answer carries no id
    REQUIRE(c.send("{\"resync\":7}"));
    const std::string again = c.message(opcodes);
    CHECK(again.rfind("{\"modules\":[", 0) == 0);
    const std::string tail = ",\"resync\":7}";
    REQUIRE(again.size() > tail.size());
    CHECK(again.compare(again.size() - tail.size(), tail.size(), tail) == 0);
}

TEST_CASE("a Ping while the full state streams is answered between frames, and the document stays whole") {
    Served sv;
    WsClient c{sv.http, {}, {}};
    REQUIRE(c.open(kTestPort));
    REQUIRE(c.send("{\"subscribe\":[\"B\"]}"));
    REQUIRE(c.send("mid", 0x9));
    // Every frame read whole: a Pong landing inside a data frame would break the framing the reader walks.
    std::string document, pong;
    bool fin = false;
    std::vector<uint8_t> opcodes;
    for (int i = 0; i < 2000 && (!fin || pong.empty()); i++) {
        sv.http.tick20ms();
        uint8_t buf[4096];
        const int n = c.conn.read(buf, sizeof(buf));
        if (n > 0) c.in.insert(c.in.end(), buf, buf + n);
        while (c.in.size() >= 2) {
            size_t len = c.in[1] & 0x7F, hdr = 2;
            if (len == 126) { if (c.in.size() < 4) break; len = (size_t(c.in[2]) << 8) | c.in[3]; hdr = 4; }
            if (c.in.size() < hdr + len) break;
            const uint8_t op = c.in[0] & 0x0F;
            const std::string body(reinterpret_cast<const char*>(c.in.data() + hdr), len);
            if (op == 0xA) pong = body;
            else { opcodes.push_back(op); document += body; if (c.in[0] & 0x80) fin = true; }
            c.in.erase(c.in.begin(), c.in.begin() + static_cast<long>(hdr + len));
        }
        if (n <= 0) mm::platform::delayMs(1);
    }
    CHECK(pong == "mid");
    REQUIRE(fin);
    const auto expected = subscribed("{\"subscribe\":[\"B\"]}");
    CHECK(document == whole(sv.http, &expected));
}

TEST_CASE("a tree changed between fragments still ends the document in valid JSON") {
    Tree t;
    mm::HttpServerModule http;
    http.setScheduler(&t.s);
    mm::HttpServerModule::TreeCursor c;
    std::string joined;
    bool done = false;
    int fragments = 0;
    while (!done && fragments < 100000) {
        mm::JsonSink sink;
        done = http.writeTreeFragment(sink, c, 16);
        joined.append(sink.data(), sink.size());
        // Mid-way through B, its wide child goes: the cursor's path now names a module that is not there.
        if (++fragments == 40) { t.b->removeChild(t.w); delete t.w; t.w = nullptr; }
    }
    REQUIRE(done);
    mm::json::JsonDoc doc;
    CHECK(mm::json::parse(joined.c_str(), doc));
}

TEST_CASE("patches and the WLED frame wait while a client's document is under way") {
    Served sv;
    WsClient c{sv.http, {}, {}};
    REQUIRE(c.open(kTestPort));
    REQUIRE(c.send("{\"subscribe\":[\"A\"]}"));
    std::vector<uint8_t> opcodes;
    REQUIRE(c.message(opcodes).rfind("{\"modules\":[", 0) == 0);
    sv.http.holdStreamForTest(0);   // a document under way, as a slow client's would be
    sv.t.k->value = 99;
    sv.http.tick1s();               // the per-second push: the patch and the WLED frame both due
    uint8_t buf[256];
    mm::platform::delayMs(20);
    CHECK(c.conn.read(buf, sizeof(buf)) == -1);   // nothing written into the open message
}

TEST_CASE("a client that never subscribes is sent the WLED frame and no module state") {
    Served sv;
    WsClient c{sv.http, {}, {}};
    REQUIRE(c.open(kTestPort));
    for (int i = 0; i < 3; i++) { sv.http.tick20ms(); sv.http.tick1s(); }
    std::vector<uint8_t> opcodes;
    const std::string first = c.message(opcodes, 200);
    REQUIRE(!first.empty());
    CHECK(first.rfind("{\"state\":", 0) == 0);
    CHECK(first.find("\"modules\"") == std::string::npos);
    CHECK(first.find("\"patch\"") == std::string::npos);
}


TEST_CASE("a product state stream progresses with persistent BLE's internal RAM budget") {
    struct Heap { ~Heap() { mm::platform::setTestFreeInternalHeap(0); } } heap;
    Served sv;
    sv.http.setSendBudget(mm::platform::HEAP_RESERVE, 1024);
    WsClient client{sv.http, {}, {}};
    REQUIRE(client.open(kTestPort));
    mm::platform::setTestFreeInternalHeap(35 * 1024);
    REQUIRE(client.send("{\"subscribe\":[\"B\"]}"));
    std::vector<uint8_t> opcodes;
    const std::string message = client.message(opcodes);
    const auto expected = subscribed("{\"subscribe\":[\"B\"]}");
    CHECK(message == whole(sv.http, &expected));
}

// Under memory pressure a patch was sent as far as its buffer got: invalid JSON the page rejected, the change lost until the value moved again.
TEST_CASE("a patch memory cuts short is never sent, and the client takes its whole state again") {
    struct Refuse { ~Refuse() { mm::platform::setTestAllocRefusedFrom(0); } } refuse;   // restored however the test ends
    Served sv;
    WsClient c{sv.http, {}, {}};
    REQUIRE(c.open(kTestPort));
    REQUIRE(c.settleOn("{\"subscribe\":[\"B\"]}"));
    std::vector<uint8_t> opcodes;

    sv.t.l->value = 3;
    sv.t.l->notifyValuesChanged();
    mm::platform::delayMs(50);                   // past the early patch's pacing
    mm::platform::setTestAllocRefusedFrom(256);   // refuse the patch sink's current first block after subscription setup
    sv.http.tick20ms();
    mm::platform::setTestAllocRefusedFrom(0);
    const std::string next = c.message(opcodes);
    CHECK(next.rfind("{\"modules\":[", 0) == 0);   // the whole state again, not a cut patch
    CHECK(objectOf(next, "L").find("\"value\":3") != std::string::npos);
}

// The second's walk holds what it gathered until it ends, so a value that goes out early meanwhile would be overtaken by the older one the walk took.
TEST_CASE("a value sent early while the second's walk is under way is the last one the client sees") {
    Served sv;
    WsClient c{sv.http, {}, {}};
    REQUIRE(c.open(kTestPort));
    REQUIRE(c.settleOn("{\"subscribe\":[\"B\"]}"));
    std::vector<uint8_t> opcodes;

    sv.t.l->value = 1;
    sv.http.tick1s();                      // the walk starts
    REQUIRE(!sv.http.stepValueWalkForTest(9));   // past B's four leaves and L's five: the walk holds L at 1
    sv.t.l->value = 2;
    sv.t.l->notifyValuesChanged();         // and 2 goes out on the next tick

    std::string last;
    const std::string key = "\"path\":\"L/value\",\"value\":";
    for (std::string m = c.message(opcodes, 200); !m.empty(); m = c.message(opcodes, 200))
        for (size_t at = m.find(key); at != std::string::npos; at = m.find(key, at + 1))
            last = m.substr(at + key.size(), 1);
    CHECK(last == "2");
}

TEST_CASE("a preview frame arrives whole as one binary message, and an offer while it leaves is refused") {
    Served sv;
    WsClient c{sv.http, {}, {}};
    REQUIRE(c.open(kTestPort, "/wsp"));
    const uint8_t head[3] = {1, 2, 3};
    const std::string body(20000, 'p');   // more than one tick's chunk on a small board
    REQUIRE(sv.http.sendBufferedFrame(head, sizeof(head), reinterpret_cast<const uint8_t*>(body.data()), body.size()));
    CHECK_FALSE(sv.http.sendBufferedFrame(head, sizeof(head), reinterpret_cast<const uint8_t*>(body.data()), body.size()));
    std::vector<uint8_t> opcodes;
    const std::string message = c.message(opcodes);
    REQUIRE(opcodes.size() == 1);
    CHECK(opcodes[0] == 0x2);
    CHECK(message == std::string("\x01\x02\x03", 3) + body);
    CHECK(sv.http.bufferedSendIdle());
}

TEST_CASE("GET /api/state answers the whole state, written a fragment at a time") {
    Served sv;
    mm::platform::TcpConnection client;
    REQUIRE(connectAndAccept(sv.http, client, kTestPort));
    const char* req = "GET /api/state HTTP/1.1\r\nHost: test\r\n\r\n";
    REQUIRE(client.write(reinterpret_cast<const uint8_t*>(req), std::strlen(req)));
    std::string reply;
    char buf[4096];
    for (int i = 0; i < 3000; i++) {
        sv.http.tick20ms();
        const int n = client.read(reinterpret_cast<uint8_t*>(buf), sizeof(buf));
        if (n > 0) reply.append(buf, static_cast<size_t>(n));
        else if (n == 0) break;   // the server closed: the answer is complete
        else mm::platform::delayMs(1);
    }
    const size_t at = reply.find("\r\n\r\n");
    REQUIRE(at != std::string::npos);
    CHECK(reply.rfind("HTTP/1.1 200", 0) == 0);
    CHECK(reply.substr(at + 4) == whole(sv.http, nullptr));   // W's forty controls take more than one fragment
}

// One buffer once held the cut frame of one client at a time, so a second client cut in the same tick lost the first one's frame.
TEST_CASE("two clients whose frames are cut in the same tick each have their frame applied") {
    Served sv;
    WsClient a{sv.http, {}, {}}, b{sv.http, {}, {}};
    REQUIRE(a.open(kTestPort));
    REQUIRE(b.open(kTestPort));
    const auto fa = clientFrame("{\"module\":\"K\",\"control\":\"value\",\"value\":42}");
    const auto fb = clientFrame("{\"module\":\"L\",\"control\":\"value\",\"value\":43}");
    REQUIRE(a.conn.write(fa.data(), 10));
    REQUIRE(b.conn.write(fb.data(), 10));
    mm::platform::delayMs(5);
    sv.http.tick20ms();   // both halves read: each client keeps its own
    REQUIRE(a.conn.write(fa.data() + 10, fa.size() - 10));
    REQUIRE(b.conn.write(fb.data() + 10, fb.size() - 10));
    for (int i = 0; i < 50 && (sv.t.k->value != 42 || sv.t.l->value != 43); i++) {
        mm::platform::delayMs(1);
        sv.http.tick20ms();
    }
    CHECK(sv.t.k->value == 42);
    CHECK(sv.t.l->value == 43);
}
