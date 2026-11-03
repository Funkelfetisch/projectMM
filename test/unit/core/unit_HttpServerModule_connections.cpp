/// @module HttpServerModule
///
/// A connection whose request arrives late, as one whose first packet was lost and resent does, is parked and served rather than closed with no reply.
///
/// An embedded UI file streams across ticks from a send slot, so a slow client never holds the render loop for the whole file. Driven over loopback against the real server on a test port, since a running desktop app holds 8080.

#include "doctest.h"
#include "core/system/HttpServerModule.h"
#include "core/system/ControlModule.h"
#include "core/module/Scheduler.h"
#include "core/util/JsonUtil.h"
#include "core/util/ModuleFactory.h"
#include "core/util/PresetStore.h"
#include "light/layers/Layer.h"
#include "light/effects/SolidEffect.h"
#include "http_loopback.h"
#include "platform/platform.h"
#include "ui/ui_embedded.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using namespace http_loopback;

constexpr uint16_t kTestPort = 18090;

// Tick the server until the client reads a reply or sees the connection closed; the bytes read, 0 when closed.
int readReply(mm::HttpServerModule& http, mm::platform::TcpConnection& client, char* out, size_t len) {
    for (int i = 0; i < 300; i++) {
        http.tick20ms();
        const int n = client.read(reinterpret_cast<uint8_t*>(out), len - 1);
        if (n >= 0) { out[n > 0 ? n : 0] = 0; return n; }
        mm::platform::delayMs(1);
    }
    return -1;
}

}  // namespace

TEST_CASE("HttpServer: a request that arrives after the connection was accepted is served, not dropped") {
    mm::HttpServerModule http;
    http.port = kTestPort;
    http.setup();
    mm::platform::TcpConnection client;
    REQUIRE(connectAndAccept(http, client, kTestPort));

    mm::platform::delayMs(50);   // the request arrives a resend later
    const char* req = "OPTIONS / HTTP/1.1\r\nHost: test\r\n\r\n";
    REQUIRE(client.write(reinterpret_cast<const uint8_t*>(req), std::strlen(req)));
    char reply[256];
    CHECK(readReply(http, client, reply, sizeof(reply)) > 0);
    CHECK(std::strncmp(reply, "HTTP/1.1 204", 12) == 0);

    client.close();
    http.release();
}

// Four parking slots dropped the rest of a burst, and a client fetching eight HLS segments at once saw a third of them reset.
TEST_CASE("HttpServer: eight connections opened before their requests are each answered") {
    mm::HttpServerModule http;
    http.port = kTestPort;
    http.setup();
    mm::platform::TcpConnection clients[8];
    for (auto& c : clients) REQUIRE(connectAndAccept(http, c, kTestPort));   // each accepted with nothing to read: parked
    const char* req = "OPTIONS / HTTP/1.1\r\nHost: test\r\n\r\n";
    for (auto& c : clients) REQUIRE(c.write(reinterpret_cast<const uint8_t*>(req), std::strlen(req)));
    for (auto& c : clients) {
        char reply[256];
        CHECK(readReply(http, c, reply, sizeof(reply)) > 0);
        CHECK(std::strncmp(reply, "HTTP/1.1 204", 12) == 0);
        c.close();
    }
    http.release();
}

TEST_CASE("HttpServer: a connection that never sends a request is closed once its parking time is up") {
    struct Clock { ~Clock() { mm::platform::setTestNowMs(0); } } clock;   // reset however the test ends
    mm::platform::setTestNowMs(1000);
    mm::HttpServerModule http;
    http.port = kTestPort;
    http.setup();
    mm::platform::TcpConnection client;
    REQUIRE(connectAndAccept(http, client, kTestPort));

    http.tick20ms();
    char reply[64];
    CHECK(client.read(reinterpret_cast<uint8_t*>(reply), sizeof(reply)) == -1);   // still open, waiting
    mm::platform::setTestNowMs(1000 + 3000);
    CHECK(readReply(http, client, reply, sizeof(reply)) == 0);                    // closed, with no reply

    client.close();
    http.release();
}

namespace {

// The platform's UI folder for one test, the checkout's again however the test ends.
struct UiFolder {
    explicit UiFolder(const char* path) { mm::platform::setUiFolder(path); }
    ~UiFolder() { mm::platform::setUiFolder("src/ui"); }
};

// The embedded app.js, the largest UI file, the one a slow link spends seconds on.
const mm::ui::EmbeddedFile& embeddedAppJs() {
    for (const auto& f : mm::ui::kFiles)
        if (std::strcmp(f.name, "app.js") == 0) return f;
    return mm::ui::kFiles[0];
}

// Request `path`, with `header` as one more request line when given, then tick and read until the server closes the connection; everything read.
std::string getUntilClosed(mm::HttpServerModule& http, mm::platform::TcpConnection& client, const char* path, const std::string& header = "") {
    char req[192];
    std::snprintf(req, sizeof(req), "GET %s HTTP/1.1\r\nHost: test\r\n%s%s\r\n", path, header.c_str(), header.empty() ? "" : "\r\n");
    if (!client.write(reinterpret_cast<const uint8_t*>(req), std::strlen(req))) return {};
    return readUntilClosed(http, client);
}

}  // namespace

TEST_CASE("HttpServer: a checkout's UI folder serves its script modules and nothing else in it") {
    const auto dir = std::filesystem::temp_directory_path() / "mm_ui_folder_test";
    const std::string dirName = dir.string();   // the platform keeps the pointer, so the string outlives the server
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "panel.js") << "export const x = 1;";
    std::ofstream(dir / "ui_embedded.h") << "secret";
    mm::HttpServerModule http;
    http.port = kTestPort;
    const UiFolder folder(dirName.c_str());
    http.setup();

    const auto statusOf = [&](const char* path) {
        mm::platform::TcpConnection client;
        REQUIRE(connectAndAccept(http, client, kTestPort));
        const std::string reply = getUntilClosed(http, client, path);
        client.close();
        return reply.substr(0, 12);
    };
    CHECK(statusOf("/panel.js") == "HTTP/1.1 200");
    CHECK(statusOf("/ui_embedded.h") == "HTTP/1.1 404");
#ifndef _WIN32
    // On Windows a colon names a drive or a data stream, a way out of the folder, so a name holding one is refused. A file here can carry one, which shows the refusal.
    std::ofstream(dir / "c:panel.js") << "export const y = 2;";
    CHECK(statusOf("/c:panel.js") == "HTTP/1.1 404");
#endif
    http.release();
    std::filesystem::remove_all(dir);
}

#ifndef MM_UI_NO_WEARABLE_ASSETS
TEST_CASE("HttpServer: embedded product images keep their URL directory") {
    mm::HttpServerModule http;
    http.port = kTestPort;
    const UiFolder embeddedOnly(nullptr);
    http.setup();
    mm::platform::TcpConnection client;
    REQUIRE(connectAndAccept(http, client, kTestPort));
    const std::string reply = getUntilClosed(http, client, "/products/croptop_leds.png");
    CHECK(reply.find("HTTP/1.1 200 OK") == 0);
    CHECK(reply.find("Content-Type: image/webp") != std::string::npos);
    CHECK(reply.find("RIFF") != std::string::npos);
    CHECK(reply.find("WEBP") != std::string::npos);
    client.close();
    REQUIRE(connectAndAccept(http, client, kTestPort));
    const std::string direct = getUntilClosed(http, client, "/products/croptop_leds.webp");
    CHECK(direct.find("HTTP/1.1 200 OK") == 0);
    CHECK(direct.find("Content-Type: image/webp") != std::string::npos);
    const auto oldBody = reply.find("\r\n\r\n");
    const auto newBody = direct.find("\r\n\r\n");
    REQUIRE(oldBody != std::string::npos); REQUIRE(newBody != std::string::npos);
    CHECK(reply.substr(oldBody + 4) == direct.substr(newBody + 4));
    client.close();
    http.release();
}
#endif

TEST_CASE("HttpServer: an embedded UI file streams across ticks, arrives whole, and its connection then closes") {
    mm::HttpServerModule http;
    http.port = kTestPort;
    const UiFolder embeddedOnly(nullptr);   // no files on disk: the embedded copy is served, as on a device
    http.setup();
    mm::platform::TcpConnection client;
    REQUIRE(connectAndAccept(http, client, kTestPort));

    const auto& file = embeddedAppJs();
    const std::string reply = getUntilClosed(http, client, "/app.js");
    const auto bodyStart = reply.find("\r\n\r\n");
    REQUIRE(bodyStart != std::string::npos);
    CHECK(std::strncmp(reply.c_str(), "HTTP/1.1 200", 12) == 0);
    REQUIRE(reply.size() - (bodyStart + 4) == file.len);
    CHECK(std::memcmp(reply.data() + bodyStart + 4, file.data, file.len) == 0);

    client.close();
    http.release();
}

TEST_CASE("HttpServer: a page load past the send slots gets every file whole or a 503, never a write that holds the loop") {
    mm::HttpServerModule http;
    http.port = kTestPort;
    const UiFolder embeddedOnly(nullptr);   // no files on disk: the embedded copy is served, as on a device
    http.setup();
    constexpr int kClients = 18;   // past the sixteen slots
    mm::platform::TcpConnection clients[kClients];
    // Each connects and sends its request at once, as a browser's burst does; a tick between them keeps the listen backlog from filling.
    const char* req = "GET /app.js HTTP/1.1\r\nHost: test\r\n\r\n";
    for (auto& c : clients) {
        REQUIRE(c.connectStart("127.0.0.1", kTestPort));
        CR r = CR::Pending;
        for (int i = 0; i < 500 && r == CR::Pending; i++) {
            r = c.connectPoll();
            if (r == CR::Pending) mm::platform::delayMs(1);
        }
        REQUIRE(r == CR::Connected);
        REQUIRE(c.write(reinterpret_cast<const uint8_t*>(req), std::strlen(req)));
        http.tick20ms();
    }

    const auto& file = embeddedAppJs();
    std::string got[kClients];
    bool closed[kClients] = {};
    char buf[4096];
    for (int i = 0; i < 5000; i++) {
        http.tick20ms();
        bool all = true;
        for (int k = 0; k < kClients; k++) {
            if (closed[k]) continue;
            const int n = clients[k].read(reinterpret_cast<uint8_t*>(buf), sizeof(buf));
            if (n > 0) got[k].append(buf, static_cast<size_t>(n));
            else if (n == 0) closed[k] = true;
            all = all && closed[k];
        }
        if (all) break;
    }
    int whole = 0;
    for (int k = 0; k < kClients; k++) {
        CAPTURE(k);
        const auto bodyStart = got[k].find("\r\n\r\n");
        REQUIRE(bodyStart != std::string::npos);
        if (got[k].compare(0, 12, "HTTP/1.1 503") == 0) continue;   // every slot was busy when it asked
        CHECK(got[k].size() - (bodyStart + 4) == file.len);
        whole++;
    }
    CHECK(whole >= 16);   // every slot served
    for (auto& c : clients) c.close();
    http.release();
}

// The preview drains first every tick and offers a frame after each one ends, so it took the lease again at once and a page file below the headroom crawled.
TEST_CASE("HttpServer: below the headroom a send that waited goes before the one whose turn just ended") {
    struct Restore { ~Restore() { mm::platform::setTestFreeInternalHeap(0); } } restore;
    mm::HttpServerModule http;
    int previewSend = 0, fileSend = 0;
    mm::platform::setTestFreeInternalHeap(mm::HttpServerModule::kSendHeadroom - 1);
    CHECK(http.maySendForTest(&previewSend));
    CHECK_FALSE(http.maySendForTest(&fileSend));   // waits its turn
    http.endSendForTest(&previewSend);
    CHECK_FALSE(http.maySendForTest(&previewSend));   // its next frame waits for the file
    CHECK(http.maySendForTest(&fileSend));
    http.endSendForTest(&fileSend);
    CHECK(http.maySendForTest(&previewSend));
}

// Below the headroom files, state streams and preview frames take turns: one holds the lease until it ends, and one stalled past the stall guard hands it on. The queue stays one socket's buffer, and none waits forever.
TEST_CASE("HttpServer: below the headroom one send queues at a time, and a stalled one hands its turn on") {
    struct Restore { ~Restore() { mm::platform::setTestFreeInternalHeap(0); mm::platform::setTestNowMs(0); } } restore;
    mm::HttpServerModule http;
    int fileSend = 0, previewSend = 0;
    mm::platform::setTestNowMs(1000);
    CHECK(http.maySendForTest(&fileSend));     // room enough: every send goes
    CHECK(http.maySendForTest(&previewSend));
    mm::platform::setTestFreeInternalHeap(mm::HttpServerModule::kSendHeadroom - 1);
    CHECK(http.maySendForTest(&fileSend));     // short of room: the first to ask holds the turn
    CHECK_FALSE(http.maySendForTest(&previewSend));
    http.endSendForTest(&fileSend);
    CHECK(http.maySendForTest(&previewSend));  // its send ended: the next one goes
    CHECK_FALSE(http.maySendForTest(&fileSend));
    mm::platform::setTestNowMs(1000 + 11000);  // the holder moved nothing past the stall guard
    CHECK(http.maySendForTest(&fileSend));
}

// A page reload downloaded the whole UI again: each file now carries a tag, and a browser holding it gets a 304 with no body (RFC 9110 section 13.1.2).
TEST_CASE("HttpServer: an embedded file carries its tag, and a request naming that tag is answered 304 with no body") {
    mm::HttpServerModule http;
    http.port = kTestPort;
    const UiFolder embeddedOnly(nullptr);   // no files on disk: the embedded copy is served, as on a device
    http.setup();
    mm::platform::TcpConnection first;
    REQUIRE(connectAndAccept(http, first, kTestPort));
    const std::string full = getUntilClosed(http, first, "/app.js");
    REQUIRE(full.rfind("HTTP/1.1 200", 0) == 0);
    const auto at = full.find("ETag: ");
    REQUIRE(at != std::string::npos);
    const std::string tag = full.substr(at + 6, full.find("\r\n", at) - at - 6);
    CHECK(tag == embeddedAppJs().etag);

    mm::platform::TcpConnection again;
    REQUIRE(connectAndAccept(http, again, kTestPort));
    const std::string reply = getUntilClosed(http, again, "/app.js", "If-None-Match: " + tag);
    CHECK(reply.rfind("HTTP/1.1 304", 0) == 0);
    CHECK(reply.find("\r\n\r\n") + 4 == reply.size());   // headers only
    http.release();
}

// A desktop running from a checkout loads the UI's own modules, which the embedded bundle holds inside app.js, from the folder; nothing outside it.
TEST_CASE("HttpServer: a desktop serves a UI module from its folder, and no path out of it") {
    const std::string dir = (std::filesystem::temp_directory_path() / "mm_ui_disk").string();
    std::filesystem::create_directories(dir);
    { std::ofstream(dir + "/extra.js") << "export const x = 1;"; }
    mm::HttpServerModule http;
    http.port = kTestPort;
    const UiFolder folder(dir.c_str());
    http.setup();
    mm::platform::TcpConnection client;
    REQUIRE(connectAndAccept(http, client, kTestPort));
    const std::string got = getUntilClosed(http, client, "/extra.js");
    CHECK(got.rfind("HTTP/1.1 200", 0) == 0);
    CHECK(got.find("export const x = 1;") != std::string::npos);
    mm::platform::TcpConnection out;
    REQUIRE(connectAndAccept(http, out, kTestPort));
    CHECK(getUntilClosed(http, out, "/..%2Fextra.js").rfind("HTTP/1.1 404", 0) == 0);
    http.release();
    std::filesystem::remove_all(dir);
}

namespace {

// A GET of the embedded app.js answered while internal RAM sits at the floor, so its header goes and its body waits.
struct FloorGet {
    struct Heap { ~Heap() { mm::platform::setTestFreeInternalHeap(0); } } heap;   // restored however the test ends
    const UiFolder embeddedOnly{nullptr};
    mm::HttpServerModule http;
    mm::platform::TcpConnection client;
    char buf[4096];
    FloorGet() {
        http.port = kTestPort;
        http.setup();
        REQUIRE(connectAndAccept(http, client, kTestPort));
        mm::platform::setTestFreeInternalHeap(mm::HttpServerModule::kSendFloor);
        const char* req = "GET /app.js HTTP/1.1\r\nHost: test\r\n\r\n";
        REQUIRE(client.write(reinterpret_cast<const uint8_t*>(req), std::strlen(req)));
    }
    ~FloorGet() { http.release(); }
};

struct PresetIndexRoot {
    std::string previous = mm::platform::fsRootPath();
    std::string root = (std::filesystem::temp_directory_path() / "mm_http_connections_presets").string();
    PresetIndexRoot() {
        // isLook validates real module roles; a filtered run cannot borrow
        // the production registrations as a side effect of another test.
        REQUIRE(mm::ModuleFactory::registerType<mm::Layer>("Layer"));
        REQUIRE(mm::ModuleFactory::registerType<mm::SolidEffect>("SolidEffect"));
        std::filesystem::remove_all(root);
        mm::platform::fsSetRoot(root.c_str());
        REQUIRE(mm::platform::fsMount());
        REQUIRE(mm::platform::fsMkdir(mm::PresetStore::kDirectory));
        constexpr char preset[] = R"({"$slot":0,"Effects":{"Layer":{"type":"Layer","Scene":{"type":"SolidEffect","red":1}}}})";
        REQUIRE(mm::platform::fsWriteAtomic("/.config/presets/QA.json", preset, sizeof(preset) - 1));
    }
    ~PresetIndexRoot() {
        mm::platform::fsSetRoot(previous.c_str());
        std::filesystem::remove_all(root);
    }
    void replaceWith(unsigned count) {
        std::filesystem::remove_all(root + mm::PresetStore::kDirectory);
        REQUIRE(mm::platform::fsMkdir(mm::PresetStore::kDirectory));
        for (unsigned i = 0; i < count; ++i) {
            char path[96];
            std::snprintf(path, sizeof(path), "%s/Look %02u.json", mm::PresetStore::kDirectory, i);
            const std::string doc = "{\"$slot\":" + std::to_string(i) +
                R"(,"$origin":{"scene":"sparkles","catalog":"test"},"Effects":{"Layer":{"type":"Layer","Scene":{"type":"SolidEffect","red":1}}}})";
            REQUIRE(mm::platform::fsWriteAtomic(path, doc.data(), doc.size()));
        }
    }
};

struct PresetAllocationLimit {
    ~PresetAllocationLimit() { mm::platform::setTestAllocRefusedFrom(0); }
};

std::string nativeReadDocument() {
    std::string document = "{\"Effects\":{},\"$padding\":\"";
    document.append(1277 - document.size() - 2, 'p');
    document += "\"}";
    return document;
}

struct ConnectionLimits {
    ConnectionLimits() { mm::platform::setTestNowMs(1000); }
    ~ConnectionLimits() {
        mm::platform::setTestFreeInternalHeap(0);
        mm::platform::setTestNowMs(0);
    }
};

size_t responseBytes(const std::string& response) {
    const auto end = response.find("\r\n\r\n");
    if (end == std::string::npos) return 0;
    const auto length = response.find("Content-Length: ");
    REQUIRE(length != std::string::npos);
    return end + 4 + std::strtoul(response.c_str() + length + 16, nullptr, 10);
}

bool wholeJsonResponse(const std::string& response) {
    const auto end = response.find("\r\n\r\n");
    if (end == std::string::npos) return false;
    mm::json::JsonDoc document;
    return mm::json::parse(response.c_str() + end + 4, document);
}

struct MetadataDriver : mm::MoonModule {
    char note[2048];
    MetadataDriver() {
        std::memset(note, 'm', sizeof(note) - 1);
        note[sizeof(note) - 1] = 0;
    }
    void defineControls() override { controls_.addText("note", note, sizeof(note)); }
    const char* acceptsChildRoles() const override { return "generic"; }
};

struct MetadataReplacement : mm::MoonModule {};

struct HeaderBudgetModule : mm::MoonModule {
    bool exhaustDuringHeader = false;
    bool oneShot = false;
    mutable unsigned headerObservations = 0;
    mutable bool pressureTriggered = false;
    mutable size_t allocationAtPressure = 0;
    const char* tags() const override {
        ++headerObservations;
        // The serializer calls this after its initial header append. Model a
        // concurrent loss of send room, rather than pretending the host accounts
        // platform allocations against setTestFreeInternalHeap's fixed value.
        if (exhaustDuringHeader && (!oneShot || !pressureTriggered)) {
            pressureTriggered = true;
            allocationAtPressure = mm::platform::allocatedBytes();
            mm::platform::setTestFreeInternalHeap(mm::platform::HEAP_RESERVE);
        }
        return "";
    }
};

struct TypeBudgetModule : mm::MoonModule {
    static inline bool pressureArmed = false;
    static inline unsigned pressureCount = 0;
    bool marker = false;
    void defineControls() override {
        mm::MoonModule::defineControls();
        controls_.addControl("marker", marker);
        if (pressureArmed) {
            pressureArmed = false;
            ++pressureCount;
            mm::platform::setTestFreeInternalHeap(mm::platform::HEAP_RESERVE);
        }
    }
};

struct MetadataHttp {
    mm::Scheduler scheduler;
    MetadataDriver* drivers = new MetadataDriver();
    MetadataDriver* strip = new MetadataDriver();
    mm::HttpServerModule http;
    MetadataHttp() {
        drivers->setName("Drivers");
        drivers->setTypeName("AsyncMetadataDriver");
        strip->setName("Strip");
        strip->setTypeName("AsyncMetadataStrip");
        drivers->addChild(strip);
        drivers->rebuildControls();
        strip->rebuildControls();
        scheduler.addModule(drivers);
        http.port = kTestPort;
        http.setScheduler(&scheduler);
        http.setup();
    }
    ~MetadataHttp() {
        http.release();
        scheduler.deleteTree(drivers);
    }
};

// Many short values require repeated small fragments rather than one indivisible text value.
struct QuantumMetadata : mm::MoonModule {
    uint8_t values[96] = {};
    void defineControls() override {
        static char names[96][4];
        for (unsigned i = 0; i < 96; ++i) {
            std::snprintf(names[i], sizeof(names[i]), "q%02u", i);
            controls_.addControl(names[i], values[i]);
        }
    }
};

struct QuantumHttp {
    mm::Scheduler scheduler;
    QuantumMetadata* root = new QuantumMetadata;
    mm::HttpServerModule http;
    QuantumHttp() {
        REQUIRE(root != nullptr);
        root->setName("Quantum");
        root->rebuildControls();
        scheduler.addModule(root);
        http.port = kTestPort;
        http.setScheduler(&scheduler);
        http.setup();
    }
    ~QuantumHttp() { http.release(); scheduler.deleteTree(root); }
};

// Allow loopback delivery, then read only bytes already sent: no extra server tick can hide a quantum boundary.
std::string readQueued(mm::platform::TcpConnection& client) {
    mm::platform::delayMs(2);
    std::string result;
    char bytes[4096];
    for (;;) {
        const int count = client.read(reinterpret_cast<uint8_t*>(bytes), sizeof(bytes));
        if (count <= 0) return result;
        result.append(bytes, static_cast<size_t>(count));
    }
}

}  // namespace

TEST_CASE("HttpServer: one low-headroom sender reuses its remaining byte quantum across freed fragments") {
    ConnectionLimits limits;
    QuantumHttp fixture;
    fixture.http.setSendBudget(mm::platform::HEAP_RESERVE, 1024);
    mm::platform::setTestFreeInternalHeap(mm::platform::HEAP_RESERVE + 1024); // 256-byte fragment target.
    mm::platform::TcpConnection client;
    REQUIRE(connectAndAccept(fixture.http, client, kTestPort));
    const char* request = "GET /api/modules/Quantum HTTP/1.1\r\nHost: test\r\n\r\n";
    REQUIRE(client.write(reinterpret_cast<const uint8_t*>(request), std::strlen(request)));
    std::string response = pumpUntil(fixture.http, client, 1);
    response += readQueued(client);
    REQUIRE(response.rfind("HTTP/1.1 200", 0) == 0);
    REQUIRE_FALSE(wholeJsonResponse(response));
    fixture.http.tick20ms();
    const auto quantum = readQueued(client);
    CHECK(quantum.size() > 768); // Several generated fragments, rather than one fragment per 20 ms poll.
    CHECK(quantum.size() <= 1024);
    response += quantum;
    response += readUntilClosed(fixture.http, client);
    CHECK(wholeJsonResponse(response));
    CHECK(response.find("\"name\":\"q95\"") != std::string::npos);
    CHECK(readAfterClose(client) == 0);
}

TEST_CASE("HttpServer: a waiting peer takes the next freed fragment before the sender reuses its quantum") {
    ConnectionLimits limits;
    QuantumHttp fixture;
    fixture.http.setSendBudget(mm::platform::HEAP_RESERVE, 1);
    mm::platform::setTestFreeInternalHeap(mm::platform::HEAP_RESERVE + 1024);
    mm::platform::TcpConnection first, second;
    const char* request = "GET /api/modules/Quantum HTTP/1.1\r\nHost: test\r\n\r\n";
    REQUIRE(connectAndAccept(fixture.http, first, kTestPort));
    REQUIRE(first.write(reinterpret_cast<const uint8_t*>(request), std::strlen(request)));
    std::string firstResponse = pumpUntil(fixture.http, first, 1);
    REQUIRE(connectAndAccept(fixture.http, second, kTestPort));
    REQUIRE(second.write(reinterpret_cast<const uint8_t*>(request), std::strlen(request)));
    std::string secondResponse = pumpUntil(fixture.http, second, 1);
    fixture.http.tick20ms(); // The second sender requests a turn while the first still owns its initial fragment.
    firstResponse += readQueued(first);
    secondResponse += readQueued(second);
    const auto header = secondResponse.find("\r\n\r\n");
    REQUIRE(header != std::string::npos);
    REQUIRE(secondResponse.size() == header + 4);
    fixture.http.setSendBudget(mm::platform::HEAP_RESERVE, 1024);
    fixture.http.tick20ms();
    const auto firstQuantum = readQueued(first), secondQuantum = readQueued(second);
    CHECK(firstQuantum.size() < 512); // It yields at the old fragment boundary, before consuming the rest of 1024.
    CHECK(secondQuantum.size() <= 1024);
    firstResponse += firstQuantum;
    secondResponse += secondQuantum;
    fixture.http.tick20ms();
    firstResponse += readQueued(first);
    const auto nextPeerQuantum = readQueued(second);
    CHECK(nextPeerQuantum.size() <= 1024);
    secondResponse += nextPeerQuantum;
    CHECK(secondResponse.size() > header + 4);
    CHECK_FALSE(wholeJsonResponse(firstResponse));
    CHECK_FALSE(wholeJsonResponse(secondResponse));
    firstResponse += readUntilClosed(fixture.http, first);
    secondResponse += readUntilClosed(fixture.http, second);
    CHECK(wholeJsonResponse(firstResponse));
    CHECK(wholeJsonResponse(secondResponse));
    CHECK(readAfterClose(first) == 0);
    CHECK(readAfterClose(second) == 0);
}

TEST_CASE("HttpServer: Drivers metadata starts with a bounded header tick and streams its unchanged single-module shape") {
    ConnectionLimits limits;
    MetadataHttp fixture;
    mm::platform::TcpConnection client;
    REQUIRE(connectAndAccept(fixture.http, client, kTestPort));
    const char* request = "GET /api/modules/Drivers HTTP/1.1\r\nHost: test\r\n\r\n";
    REQUIRE(client.write(reinterpret_cast<const uint8_t*>(request), std::strlen(request)));
    mm::platform::delayMs(5);
    fixture.http.tick20ms();
    std::string reply;
    char buffer[4096];
    for (int i = 0; i < 100; ++i) {
        const int count = client.read(reinterpret_cast<uint8_t*>(buffer), sizeof(buffer));
        if (count > 0) reply.append(buffer, static_cast<size_t>(count));
        else if (reply.find("\r\n\r\n") != std::string::npos) break;
        else mm::platform::delayMs(1);
    }
    REQUIRE(reply.rfind("HTTP/1.1 200", 0) == 0);
    REQUIRE(reply.find("\r\n\r\n") != std::string::npos);
    CHECK(reply.size() == reply.find("\r\n\r\n") + 4);
    reply += readUntilClosed(fixture.http, client);
    REQUIRE(wholeJsonResponse(reply));
    mm::json::JsonDoc document;
    REQUIRE(mm::json::parse(reply.c_str() + reply.find("\r\n\r\n") + 4, document));
    const auto* name = mm::json::member(document, document.rootNode(), "name");
    REQUIRE(name != nullptr);
    CHECK(std::strcmp(name->str, "Drivers") == 0);
    CHECK(reply.find("AsyncMetadataStrip") != std::string::npos);
    CHECK(reply.find(fixture.drivers->note) != std::string::npos);
    CHECK(readAfterClose(client) == 0);
    mm::platform::TcpConnection native;
    REQUIRE(connectAndAccept(fixture.http, native, kTestPort));
    const auto nativeReply = getUntilClosed(fixture.http, native, "/api/modules/Drivers/document");
    REQUIRE(nativeReply.rfind("HTTP/1.1 200", 0) == 0);
    CHECK(wholeJsonResponse(nativeReply));
    CHECK(nativeReply.find("\"Drivers\":{") != std::string::npos);
    CHECK(nativeReply.find("\"controls\":") == std::string::npos);
}

TEST_CASE("HttpServer: tiny metadata retains less than the measured 1944-byte send headroom") {
    ConnectionLimits limits;
    MetadataHttp fixture;
    auto* tiny = new HeaderBudgetModule;
    REQUIRE(tiny != nullptr);
    tiny->setName("Tiny"); tiny->setTypeName("MoonModule");
    REQUIRE(fixture.drivers->addChild(tiny));
    fixture.http.setSendBudget(mm::platform::HEAP_RESERVE, 1);
    mm::platform::setTestFreeInternalHeap(mm::platform::HEAP_RESERVE + 1944);
    mm::platform::TcpConnection client;
    REQUIRE(connectAndAccept(fixture.http, client, kTestPort));
    const auto baseline = mm::platform::allocatedBytes();
    const char* request = "GET /api/modules/Tiny HTTP/1.1\r\nHost: test\r\n\r\n";
    REQUIRE(client.write(reinterpret_cast<const uint8_t*>(request), std::strlen(request)));
    std::string reply = pumpUntil(fixture.http, client, 1);
    REQUIRE(reply.rfind("HTTP/1.1 200", 0) == 0);
    REQUIRE_FALSE(wholeJsonResponse(reply));
    REQUIRE(mm::platform::allocatedBytes() > baseline);
    CHECK(mm::platform::allocatedBytes() - baseline < 1944);
    fixture.http.setSendBudget(mm::platform::HEAP_RESERVE, 128);
    reply += readUntilClosed(fixture.http, client);
    CHECK(wholeJsonResponse(reply));
    CHECK(readAfterClose(client) == 0);
    CHECK(mm::platform::allocatedBytes() == baseline);
}

TEST_CASE("HttpServer: loss of send room while priming metadata refuses before 200 and frees the source and fragment") {
    ConnectionLimits limits;
    MetadataHttp fixture;
    auto* tiny = new HeaderBudgetModule;
    REQUIRE(tiny != nullptr);
    tiny->setName("Tiny"); tiny->setTypeName("MoonModule");
    REQUIRE(fixture.drivers->addChild(tiny));
    fixture.http.setSendBudget(mm::platform::HEAP_RESERVE, 1);
    mm::platform::setTestFreeInternalHeap(mm::platform::HEAP_RESERVE + 1944);
    mm::platform::TcpConnection client;
    REQUIRE(connectAndAccept(fixture.http, client, kTestPort));
    const auto baseline = mm::platform::allocatedBytes();
    tiny->exhaustDuringHeader = true;
    const auto reply = getUntilClosed(fixture.http, client, "/api/modules/Tiny");
    REQUIRE(tiny->headerObservations > 0);
    REQUIRE(reply.rfind("HTTP/1.1 503", 0) == 0);
    CHECK(reply.find("HTTP/1.1 200") == std::string::npos);
    CHECK(responseBytes(reply) == reply.size());
    CHECK(wholeJsonResponse(reply));
    CHECK(readAfterClose(client) == 0);
    CHECK(mm::platform::allocatedBytes() == baseline);
    CHECK(mm::platform::millis() == 1000); // no stall timeout or clock advance was needed
}

TEST_CASE("HttpServer: late metadata pressure discards only the candidate fragment and resumes the exact cursor") {
    ConnectionLimits limits;
    MetadataHttp fixture;
    auto* late = new HeaderBudgetModule;
    REQUIRE(late != nullptr);
    late->setName("Late"); late->setTypeName("MoonModule");
    REQUIRE(fixture.drivers->addChild(late));
    mm::platform::TcpConnection reference;
    REQUIRE(connectAndAccept(fixture.http, reference, kTestPort));
    const auto expected = getUntilClosed(fixture.http, reference, "/api/modules/Drivers");
    REQUIRE(wholeJsonResponse(expected));
    const auto expectedBody = expected.substr(expected.find("\r\n\r\n") + 4);
    fixture.http.setSendBudget(mm::platform::HEAP_RESERVE, 128);
    mm::platform::setTestFreeInternalHeap(mm::platform::HEAP_RESERVE + 8192);
    mm::platform::TcpConnection client;
    REQUIRE(connectAndAccept(fixture.http, client, kTestPort));
    late->oneShot = true;
    late->exhaustDuringHeader = true;
    const char* request = "GET /api/modules/Drivers HTTP/1.1\r\nHost: test\r\n\r\n";
    REQUIRE(client.write(reinterpret_cast<const uint8_t*>(request), std::strlen(request)));
    std::string reply;
    for (unsigned tick = 0; tick < 3000 && !late->pressureTriggered; ++tick) {
        fixture.http.tick20ms();
        char buffer[4096];
        const int count = client.read(reinterpret_cast<uint8_t*>(buffer), sizeof(buffer));
        REQUIRE(count != 0);
        if (count > 0) reply.append(buffer, static_cast<size_t>(count));
        else mm::platform::delayMs(1);
    }
    REQUIRE(late->pressureTriggered);
    REQUIRE(reply.rfind("HTTP/1.1 200", 0) == 0);
    REQUIRE_FALSE(wholeJsonResponse(reply));
    // The getter ran after the local sink allocated. That candidate is now gone;
    // the live source remains so the old cursor can be retried.
    CHECK(mm::platform::allocatedBytes() + 256 <= late->allocationAtPressure);
    for (unsigned tick = 0; tick < 20; ++tick) {
        fixture.http.tick20ms();
        char buffer[4096];
        const int count = client.read(reinterpret_cast<uint8_t*>(buffer), sizeof(buffer));
        CHECK(count != 0);
        if (count > 0) reply.append(buffer, static_cast<size_t>(count));
        else mm::platform::delayMs(1);
    }
    mm::platform::setTestFreeInternalHeap(mm::platform::HEAP_RESERVE + 8192);
    reply += readUntilClosed(fixture.http, client);
    CHECK(readAfterClose(client) == 0);
    REQUIRE(wholeJsonResponse(reply));
    CHECK(reply.substr(reply.find("\r\n\r\n") + 4) == expectedBody);
}

TEST_CASE("HttpServer: late type-default pressure keeps the current row for a bounded retry") {
    ConnectionLimits limits;
    struct Reset { ~Reset() { TypeBudgetModule::pressureArmed = false; } } reset;
    REQUIRE(mm::ModuleFactory::registerType<TypeBudgetModule>("TypeBudgetModule"));
    std::vector<std::string> expected;
    for (uint8_t i = 0; i < mm::ModuleFactory::typeCount(); ++i)
        if (const char* name = mm::ModuleFactory::typeName(i)) expected.emplace_back(name);
    mm::HttpServerModule http;
    http.port = kTestPort;
    http.setSendBudget(mm::platform::HEAP_RESERVE, 4096);
    http.setup();
    mm::platform::setTestFreeInternalHeap(mm::platform::HEAP_RESERVE + 65536);
    mm::platform::TcpConnection client;
    REQUIRE(connectAndAccept(http, client, kTestPort));
    TypeBudgetModule::pressureCount = 0;
    TypeBudgetModule::pressureArmed = true;
    const char* request = "GET /api/types HTTP/1.1\r\nHost: test\r\n\r\n";
    REQUIRE(client.write(reinterpret_cast<const uint8_t*>(request), std::strlen(request)));
    std::string reply;
    for (unsigned tick = 0; tick < 3000 && !TypeBudgetModule::pressureCount; ++tick) {
        http.tick20ms();
        char buffer[4096];
        const int count = client.read(reinterpret_cast<uint8_t*>(buffer), sizeof(buffer));
        REQUIRE(count != 0);
        if (count > 0) reply.append(buffer, static_cast<size_t>(count));
        else mm::platform::delayMs(1);
    }
    REQUIRE(TypeBudgetModule::pressureCount == 1);
    REQUIRE(reply.rfind("HTTP/1.1 200", 0) == 0);
    REQUIRE_FALSE(wholeJsonResponse(reply));
    for (unsigned tick = 0; tick < 20; ++tick) {
        http.tick20ms();
        char buffer[4096];
        const int count = client.read(reinterpret_cast<uint8_t*>(buffer), sizeof(buffer));
        CHECK(count != 0);
        if (count > 0) reply.append(buffer, static_cast<size_t>(count));
        else mm::platform::delayMs(1);
    }
    mm::platform::setTestFreeInternalHeap(mm::platform::HEAP_RESERVE + 65536);
    reply += readUntilClosed(http, client);
    CHECK(readAfterClose(client) == 0);
    const auto body = reply.find("\r\n\r\n");
    REQUIRE(body != std::string::npos);
    mm::json::JsonDoc doc;
    REQUIRE(mm::json::parse(reply.c_str() + body + 4, doc));
    const auto* types = mm::json::member(doc, doc.rootNode(), "types");
    REQUIRE(types != nullptr);
    std::vector<std::string> actual;
    for (const auto* item = doc.node(types->firstChild); item; item = doc.node(item->next)) {
        const auto* name = mm::json::member(doc, item, "name");
        REQUIRE(name != nullptr);
        REQUIRE(name->type == mm::json::JsonType::String);
        actual.emplace_back(name->str);
    }
    CHECK(actual == expected); // no skipped/duplicate terminal row or missing JSON footer
    http.release();
}

TEST_CASE("HttpServer: a native preset GET transfers exact file bytes with a sub-2-KiB allocation and releases on every outcome") {
    ConnectionLimits limits;
    PresetIndexRoot files;
    const auto document = nativeReadDocument();
    REQUIRE(document.size() == 1277);
    REQUIRE(mm::platform::fsWriteAtomic("/.config/presets/QA.json", document.data(), document.size()));
    char expectedTag[65];
    {
        mm::PresetStore::File stored;
        REQUIRE(mm::PresetStore::read("QA", stored));
        std::strcpy(expectedTag, stored.etag);
    }
    PresetAllocationLimit allocations;
    mm::HttpServerModule http;
    http.port = kTestPort;
    http.setSendBudget(mm::platform::HEAP_RESERVE, 1);
    http.setup();
    mm::platform::TcpConnection client;
    REQUIRE(connectAndAccept(http, client, kTestPort));
    const auto baseline = mm::platform::allocatedBytes();
    bool disconnect = false, refuse = false;
    SUBCASE("complete document") {}
    SUBCASE("client disconnects while the transferred file is retained") { disconnect = true; }
    SUBCASE("fresh answer cannot retain a file at the send floor") { refuse = true; }
    mm::platform::setTestAllocRefusedFrom(2048);
    if (refuse) mm::platform::setTestFreeInternalHeap(mm::platform::HEAP_RESERVE);
    const char* request = "GET /api/presets/QA HTTP/1.1\r\nHost: test\r\n\r\n";
    REQUIRE(client.write(reinterpret_cast<const uint8_t*>(request), std::strlen(request)));
    std::string reply = pumpUntil(http, client, 1);
    if (refuse) {
        reply += readUntilClosed(http, client);
        REQUIRE(reply.rfind("HTTP/1.1 503", 0) == 0);
        CHECK(reply.find("HTTP/1.1 200") == std::string::npos);
        CHECK(responseBytes(reply) == reply.size());
        CHECK(mm::platform::allocatedBytes() == baseline);
    } else {
        REQUIRE(reply.rfind("HTTP/1.1 200", 0) == 0);
        CHECK(mm::platform::allocatedBytes() - baseline == document.size() + 1);
        CHECK(reply.find(std::string("ETag: \"") + expectedTag + "\"") != std::string::npos);
        if (disconnect) {
            client.close();
            for (unsigned tick = 0; tick < 100 && mm::platform::allocatedBytes() > baseline; ++tick) {
                http.tick20ms();
                mm::platform::delayMs(1);
            }
        } else {
            http.setSendBudget(mm::platform::HEAP_RESERVE, 128);
            reply += readUntilClosed(http, client);
            CHECK(responseBytes(reply) == reply.size());
            CHECK(reply.substr(reply.find("\r\n\r\n") + 4) == document);
            CHECK(readAfterClose(client) == 0);
        }
        CHECK(mm::platform::allocatedBytes() == baseline);
    }
    http.release();
}

TEST_CASE("HttpServer: a native preset GET frees its file when all HTTP answer slots are occupied") {
    ConnectionLimits limits;
    PresetIndexRoot files;
    const auto document = nativeReadDocument();
    REQUIRE(mm::platform::fsWriteAtomic("/.config/presets/QA.json", document.data(), document.size()));
    const UiFolder embeddedOnly(nullptr);
    mm::HttpServerModule http;
    http.port = kTestPort;
    http.setSendBudget(mm::platform::HEAP_RESERVE, 1);
    http.setup();
    mm::platform::setTestFreeInternalHeap(mm::platform::HEAP_RESERVE);
    mm::platform::TcpConnection holders[mm::HttpServerModule::kSendSlots];
    for (auto& holder : holders) {
        REQUIRE(connectAndAccept(http, holder, kTestPort));
        const char* request = "GET /app.js HTTP/1.1\r\nHost: test\r\n\r\n";
        REQUIRE(holder.write(reinterpret_cast<const uint8_t*>(request), std::strlen(request)));
        REQUIRE(pumpUntil(http, holder, 1).rfind("HTTP/1.1 200", 0) == 0);
    }
    mm::platform::TcpConnection client;
    REQUIRE(connectAndAccept(http, client, kTestPort));
    const auto baseline = mm::platform::allocatedBytes();
    const auto reply = getUntilClosed(http, client, "/api/presets/QA");
    REQUIRE(reply.rfind("HTTP/1.1 503", 0) == 0);
    CHECK(reply.find("HTTP/1.1 200") == std::string::npos);
    CHECK(responseBytes(reply) == reply.size());
    CHECK(readAfterClose(client) == 0);
    CHECK(mm::platform::allocatedBytes() == baseline);
    for (auto& holder : holders) holder.close();
    http.release();
}

TEST_CASE("HttpServer: a generic file below the allocator reserve returns a whole 503 without retaining its stream") {
    ConnectionLimits limits;
    PresetIndexRoot files;
    constexpr char payload[] = "a nonempty file";
    REQUIRE(mm::platform::fsWriteAtomic("/bounded.bin", payload, sizeof(payload) - 1));
    mm::HttpServerModule http;
    http.port = kTestPort;
    http.setSendBudget(mm::platform::HEAP_RESERVE, 1024);
    http.setup();
    mm::platform::TcpConnection client;
    REQUIRE(connectAndAccept(http, client, kTestPort));
    const size_t baseline = mm::platform::allocatedBytes();
    mm::platform::setTestFreeInternalHeap(26684);
    const auto reply = getUntilClosed(http, client, "/api/file?path=/bounded.bin");
    REQUIRE(reply.rfind("HTTP/1.1 503", 0) == 0);
    CHECK(responseBytes(reply) == reply.size());
    CHECK(wholeJsonResponse(reply));
    CHECK(reply.find("HTTP/1.1 200") == std::string::npos);
    CHECK(readAfterClose(client) == 0);
    CHECK(mm::platform::allocatedBytes() == baseline);
    http.release();
}

TEST_CASE("HttpServer: deleting replacing or renaming a streamed module closes its original partial response") {
    ConnectionLimits limits;
    mm::ModuleFactory::registerType<MetadataReplacement>("HttpReplacement");
    MetadataHttp fixture;
    fixture.http.setSendBudget(mm::platform::HEAP_RESERVE, 1);
    mm::platform::TcpConnection original;
    REQUIRE(connectAndAccept(fixture.http, original, kTestPort));
    const char* read = "GET /api/modules/Strip HTTP/1.1\r\nHost: test\r\n\r\n";
    REQUIRE(original.write(reinterpret_cast<const uint8_t*>(read), std::strlen(read)));
    std::string partial = pumpUntil(fixture.http, original, 150);
    REQUIRE(partial.rfind("HTTP/1.1 200", 0) == 0);
    REQUIRE(partial.find("\r\n\r\n") != std::string::npos);
    REQUIRE_FALSE(wholeJsonResponse(partial));
    std::string method, body;
    SUBCASE("delete") { method = "DELETE /api/modules/Strip"; }
    SUBCASE("replace with the same name") {
        method = "POST /api/modules/Strip/replace";
        body = R"({"type":"HttpReplacement","name":"Strip"})";
    }
    SUBCASE("replace and rename") {
        method = "POST /api/modules/Strip/replace";
        body = R"({"type":"HttpReplacement","name":"Renamed Strip"})";
    }
    REQUIRE_FALSE(method.empty());
    mm::platform::TcpConnection mutation;
    REQUIRE(connectAndAccept(fixture.http, mutation, kTestPort));
    const std::string request = method + " HTTP/1.1\r\nHost: test\r\nContent-Type: application/json\r\nContent-Length: " +
        std::to_string(body.size()) + "\r\n\r\n" + body;
    REQUIRE(mutation.write(reinterpret_cast<const uint8_t*>(request.data()), request.size()));
    const auto reply = readUntilClosed(fixture.http, mutation);
    REQUIRE(reply.rfind("HTTP/1.1 200", 0) == 0);
    if (method.rfind("DELETE", 0) == 0) {
        CHECK(fixture.scheduler.firstByName("Strip") == nullptr);
    } else {
        const bool renamed = body.find("Renamed Strip") != std::string::npos;
        const auto* replacement = fixture.scheduler.firstByName(renamed ? "Renamed Strip" : "Strip");
        REQUIRE(replacement != nullptr);
        CHECK(std::strcmp(replacement->typeName(), "HttpReplacement") == 0);
    }
    partial += readUntilClosed(fixture.http, original);
    CHECK(readAfterClose(original) == 0);
    CHECK_FALSE(wholeJsonResponse(partial));
    CHECK(partial.find("Renamed Strip") == std::string::npos);
}

TEST_CASE("HttpServer: content-only preset autosave preserves a fragmented module read while a slot change resyncs it") {
    ConnectionLimits limits;
    PresetIndexRoot files;
    mm::ControlModule control;
    MetadataHttp fixture;
    control.setName("Control");
    control.defineControls();
    fixture.scheduler.addModule(&control);
    control.setup();
    REQUIRE(control.presetCount() == 1);
    mm::PresetStore::File stored;
    REQUIRE(mm::PresetStore::read("QA", stored));
    bool slotChanged = false;
    SUBCASE("effect content changes without changing the visible preset projection") {}
    SUBCASE("the preset moves to a different visible pad") { slotChanged = true; }
    static mm::HttpServerModule* resyncHttp = nullptr;
    static unsigned* observedSchema = nullptr;
    unsigned schemaEvents = 0;
    struct Hook {
        Hook(mm::HttpServerModule& http, unsigned& events) {
            resyncHttp = &http;
            observedSchema = &events;
            mm::MoonModule::setSchemaChangedHook([] {
                ++*observedSchema;
                resyncHttp->requestFullResyncForTest();
            });
        }
        ~Hook() {
            mm::MoonModule::setSchemaChangedHook(nullptr);
            resyncHttp = nullptr;
            observedSchema = nullptr;
        }
    } hook(fixture.http, schemaEvents);
    fixture.http.setSendBudget(mm::platform::HEAP_RESERVE, 1);
    mm::platform::TcpConnection original;
    REQUIRE(connectAndAccept(fixture.http, original, kTestPort));
    const char* read = "GET /api/modules/Strip HTTP/1.1\r\nHost: test\r\n\r\n";
    REQUIRE(original.write(reinterpret_cast<const uint8_t*>(read), std::strlen(read)));
    std::string partial = pumpUntil(fixture.http, original, 150);
    REQUIRE(partial.rfind("HTTP/1.1 200", 0) == 0);
    REQUIRE_FALSE(wholeJsonResponse(partial));
    const auto revision = control.presetsRevision();
    const std::string body = std::string("{\"$slot\":") + (slotChanged ? "1" : "0") +
        R"(,"Effects":{"Layer":{"type":"Layer","Scene":{"type":"SolidEffect","red":2}}}})";
    mm::platform::TcpConnection mutation;
    REQUIRE(connectAndAccept(fixture.http, mutation, kTestPort));
    const std::string request = "PUT /api/presets/QA HTTP/1.1\r\nHost: test\r\nContent-Type: application/json\r\nIf-Match: \"" +
        std::string(stored.etag) + "\"\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    REQUIRE(mutation.write(reinterpret_cast<const uint8_t*>(request.data()), request.size()));
    const auto reply = readUntilClosed(fixture.http, mutation);
    REQUIRE(reply.rfind("HTTP/1.1 200", 0) == 0);
    CHECK(control.presetsRevision() > revision);
    fixture.http.setSendBudget(mm::platform::HEAP_RESERVE, 256);
    partial += readUntilClosed(fixture.http, original);
    CHECK(readAfterClose(original) == 0);
    if (slotChanged) {
        CHECK(schemaEvents > 0);
        CHECK_FALSE(wholeJsonResponse(partial));
    } else {
        CHECK(schemaEvents == 0);
        CHECK(wholeJsonResponse(partial));
        CHECK(partial.find(std::string(128, 'm')) != std::string::npos);
    }
    control.release();
}

TEST_CASE("HttpServer: short native preset documents and mutation replies progress beside a leased tree within the send floor and budget") {
    for (bool mutation : {false, true}) {
        for (bool pauseAtFloor : {false, true}) {
            INFO("mutation=" << mutation << " pauseAtFloor=" << pauseAtFloor);
            ConnectionLimits limits;
            PresetIndexRoot files;
            const std::string document = "{\"Effects\":{},\"$padding\":\"" + std::string(1250, 'p') + "\"}";
            REQUIRE(mm::platform::fsWriteAtomic("/.config/presets/QA.json", document.data(), document.size()));
            MetadataHttp fixture;
            constexpr size_t permit = 64;
            fixture.http.setSendBudget(mm::platform::HEAP_RESERVE, permit);
            mm::platform::setTestFreeInternalHeap(mm::platform::HEAP_RESERVE + 256);
            mm::platform::TcpConnection tree;
            REQUIRE(connectAndAccept(fixture.http, tree, kTestPort));
            const char* treeRequest = "GET /api/modules/Drivers HTTP/1.1\r\nHost: test\r\n\r\n";
            REQUIRE(tree.write(reinterpret_cast<const uint8_t*>(treeRequest), std::strlen(treeRequest)));
            std::string treeReply = pumpUntil(fixture.http, tree, 512);
            REQUIRE(treeReply.rfind("HTTP/1.1 200", 0) == 0);
            REQUIRE_FALSE(wholeJsonResponse(treeReply));
            if (pauseAtFloor && mutation) mm::platform::setTestFreeInternalHeap(mm::platform::HEAP_RESERVE);
            mm::platform::TcpConnection small;
            REQUIRE(connectAndAccept(fixture.http, small, kTestPort));
            const std::string body = R"({"Effects":{}})";
            const std::string request = mutation
                ? "PUT /api/presets/Saved HTTP/1.1\r\nHost: test\r\nIf-None-Match: *\r\nContent-Length: " +
                    std::to_string(body.size()) + "\r\n\r\n" + body
                : "GET /api/presets/QA HTTP/1.1\r\nHost: test\r\n\r\n";
            REQUIRE(small.write(reinterpret_cast<const uint8_t*>(request.data()), request.size()));
            std::string smallReply;
            const auto collect = [](mm::platform::TcpConnection& client, std::string& reply) {
                char buffer[4096];
                const int count = client.read(reinterpret_cast<uint8_t*>(buffer), sizeof(buffer));
                if (count > 0) reply.append(buffer, static_cast<size_t>(count));
                return count;
            };
            const auto bodyBytes = [](const std::string& reply) {
                const auto header = reply.find("\r\n\r\n");
                return header == std::string::npos ? size_t{0} : reply.size() - header - 4;
            };
            if (pauseAtFloor) {
                if (!mutation) {
                    // A fresh file answer now refuses at the floor. Create this
                    // answer first, then pause its already-retained body.
                    fixture.http.tick20ms();
                    mm::platform::delayMs(1);
                    collect(tree, treeReply);
                    collect(small, smallReply);
                    REQUIRE(smallReply.rfind("HTTP/1.1 200", 0) == 0);
                    mm::platform::setTestFreeInternalHeap(mm::platform::HEAP_RESERVE);
                }
                const auto before = treeReply.size();
                const auto beforeSmallBody = bodyBytes(smallReply);
                for (unsigned tick = 0; tick < 25; ++tick) {
                    fixture.http.tick20ms();
                    CHECK(collect(tree, treeReply) != 0);
                    CHECK(collect(small, smallReply) != 0);
                    mm::platform::delayMs(1);
                }
                REQUIRE(smallReply.find("\r\n\r\n") != std::string::npos);
                CHECK(treeReply.size() == before);
                CHECK(bodyBytes(smallReply) == beforeSmallBody);
                mm::platform::setTestFreeInternalHeap(mm::platform::HEAP_RESERVE + 256);
            }
            const auto treeBeforeSmall = treeReply.size();
            bool treeClosed = false, smallClosed = false;
            for (unsigned tick = 0; tick < 1000 && !smallClosed; ++tick) {
                const auto previousSmallBody = bodyBytes(smallReply);
                fixture.http.tick20ms();
                const int largeCount = collect(tree, treeReply);
                const int smallCount = collect(small, smallReply);
                if (largeCount == 0) treeClosed = true;
                if (smallCount == 0) smallClosed = true;
                CHECK(bodyBytes(smallReply) - previousSmallBody <= permit);
                if (largeCount < 0 && smallCount < 0) mm::platform::delayMs(1);
            }
            REQUIRE(smallClosed);
            CHECK_FALSE(treeClosed);
            CHECK(treeReply.size() > treeBeforeSmall);
            REQUIRE(smallReply.rfind(mutation ? "HTTP/1.1 201" : "HTTP/1.1 200", 0) == 0);
            CHECK(responseBytes(smallReply) == smallReply.size());
            CHECK(wholeJsonResponse(smallReply));
            if (mutation) CHECK(smallReply.find("\"stored\":true") != std::string::npos);
            else CHECK(smallReply.substr(smallReply.find("\r\n\r\n") + 4) == document);
            fixture.http.setSendBudget(mm::platform::HEAP_RESERVE, 256);
            treeReply += readUntilClosed(fixture.http, tree);
            CHECK(readAfterClose(tree) == 0);
            CHECK(wholeJsonResponse(treeReply));
        }
    }
}

TEST_CASE("HttpServer: a native preset document above 4 KiB waits for the current tree fragment then progresses before the tree ends") {
    ConnectionLimits limits;
    PresetIndexRoot files;
    const std::string document = "{\"Effects\":{},\"$padding\":\"" + std::string(6000, 'p') + "\"}";
    REQUIRE(mm::platform::fsWriteAtomic("/.config/presets/Large.json", document.data(), document.size()));
    MetadataHttp fixture;
    fixture.http.setSendBudget(mm::platform::HEAP_RESERVE, 1);
    mm::platform::setTestFreeInternalHeap(mm::platform::HEAP_RESERVE + 256);
    mm::platform::TcpConnection tree;
    REQUIRE(connectAndAccept(fixture.http, tree, kTestPort));
    const char* treeRequest = "GET /api/modules/Drivers HTTP/1.1\r\nHost: test\r\n\r\n";
    REQUIRE(tree.write(reinterpret_cast<const uint8_t*>(treeRequest), std::strlen(treeRequest)));
    std::string treeReply = pumpUntil(fixture.http, tree, 1);
    REQUIRE_FALSE(wholeJsonResponse(treeReply));
    mm::platform::TcpConnection large;
    REQUIRE(connectAndAccept(fixture.http, large, kTestPort));
    const char* request = "GET /api/presets/Large HTTP/1.1\r\nHost: test\r\n\r\n";
    REQUIRE(large.write(reinterpret_cast<const uint8_t*>(request), std::strlen(request)));
    std::string largeReply;
    const auto before = treeReply.size();
    for (unsigned tick = 0; tick < 3; ++tick) {
        fixture.http.tick20ms();
        char buffer[4096];
        int count = tree.read(reinterpret_cast<uint8_t*>(buffer), sizeof(buffer));
        REQUIRE(count != 0);
        if (count > 0) treeReply.append(buffer, static_cast<size_t>(count));
        count = large.read(reinterpret_cast<uint8_t*>(buffer), sizeof(buffer));
        REQUIRE(count != 0);
        if (count > 0) largeReply.append(buffer, static_cast<size_t>(count));
        mm::platform::delayMs(1);
    }
    REQUIRE(largeReply.rfind("HTTP/1.1 200", 0) == 0);
    const auto header = largeReply.find("\r\n\r\n");
    REQUIRE(header != std::string::npos);
    CHECK(largeReply.size() == header + 4);
    CHECK(treeReply.size() > before);
    CHECK_FALSE(wholeJsonResponse(treeReply));
    fixture.http.setSendBudget(mm::platform::HEAP_RESERVE, 64);
    bool treeClosed = false;
    for (unsigned tick = 0; tick < 1000 && largeReply.size() == header + 4; ++tick) {
        fixture.http.tick20ms();
        char buffer[4096];
        int count = tree.read(reinterpret_cast<uint8_t*>(buffer), sizeof(buffer));
        if (count == 0) treeClosed = true;
        if (count > 0) treeReply.append(buffer, static_cast<size_t>(count));
        count = large.read(reinterpret_cast<uint8_t*>(buffer), sizeof(buffer));
        if (count > 0) largeReply.append(buffer, static_cast<size_t>(count));
        if (count < 0) mm::platform::delayMs(1);
    }
    CHECK(largeReply.size() > header + 4);
    CHECK_FALSE(treeClosed);
    CHECK_FALSE(wholeJsonResponse(treeReply));
    fixture.http.setSendBudget(mm::platform::HEAP_RESERVE, 256);
    treeReply += readUntilClosed(fixture.http, tree);
    largeReply += readUntilClosed(fixture.http, large);
    CHECK(wholeJsonResponse(treeReply));
    CHECK(readAfterClose(tree) == 0);
    CHECK(readAfterClose(large) == 0);
    CHECK(responseBytes(largeReply) == largeReply.size());
    CHECK(largeReply.substr(header + 4) == document);
}

TEST_CASE("HttpServer: large metadata trees yield between retained fragments before either document finishes") {
    ConnectionLimits limits;
    MetadataHttp fixture;
    fixture.http.setSendBudget(mm::platform::HEAP_RESERVE, 1);
    mm::platform::setTestFreeInternalHeap(mm::platform::HEAP_RESERVE + 8192);
    mm::platform::TcpConnection first, second;
    REQUIRE(connectAndAccept(fixture.http, first, kTestPort));
    const char* request = "GET /api/modules/Drivers HTTP/1.1\r\nHost: test\r\n\r\n";
    REQUIRE(first.write(reinterpret_cast<const uint8_t*>(request), std::strlen(request)));
    std::string firstReply = pumpUntil(fixture.http, first, 1);
    REQUIRE_FALSE(wholeJsonResponse(firstReply));
    REQUIRE(connectAndAccept(fixture.http, second, kTestPort));
    request = "GET /api/modules/Strip HTTP/1.1\r\nHost: test\r\n\r\n";
    REQUIRE(second.write(reinterpret_cast<const uint8_t*>(request), std::strlen(request)));
    std::string secondReply;
    fixture.http.setSendBudget(mm::platform::HEAP_RESERVE, 16);
    bool firstClosed = false;
    for (unsigned tick = 0; tick < 1000; ++tick) {
        mm::platform::setTestNowMs(1000 + tick);
        fixture.http.tick20ms();
        char buffer[4096];
        int count = first.read(reinterpret_cast<uint8_t*>(buffer), sizeof(buffer));
        if (count == 0) firstClosed = true;
        if (count > 0) firstReply.append(buffer, static_cast<size_t>(count));
        count = second.read(reinterpret_cast<uint8_t*>(buffer), sizeof(buffer));
        if (count > 0) secondReply.append(buffer, static_cast<size_t>(count));
        const auto end = secondReply.find("\r\n\r\n");
        if (end != std::string::npos && secondReply.size() > end + 4) break;
        if (count < 0) mm::platform::delayMs(1);
    }
    REQUIRE(secondReply.rfind("HTTP/1.1 200", 0) == 0);
    const auto end = secondReply.find("\r\n\r\n");
    REQUIRE(end != std::string::npos);
    CHECK(secondReply.size() > end + 4);
    CHECK_FALSE(firstClosed);
    CHECK_FALSE(wholeJsonResponse(firstReply));
    CHECK(mm::platform::millis() < 11000); // progress did not require the ten-second stalled-lease guard
    fixture.http.setSendBudget(mm::platform::HEAP_RESERVE, 256);
    firstReply += readUntilClosed(fixture.http, first);
    secondReply += readUntilClosed(fixture.http, second);
    CHECK(wholeJsonResponse(firstReply));
    CHECK(wholeJsonResponse(secondReply));
    CHECK(readAfterClose(first) == 0);
    CHECK(readAfterClose(second) == 0);
}

TEST_CASE("HttpServer: continuous binary preview frames coexist with a short HTTP answer at a one-byte send budget") {
    ConnectionLimits limits;
    PresetIndexRoot files;
    const auto document = nativeReadDocument();
    REQUIRE(mm::platform::fsWriteAtomic("/.config/presets/QA.json", document.data(), document.size()));
    mm::HttpServerModule http;
    http.port = kTestPort;
    http.setSendBudget(mm::platform::HEAP_RESERVE, 1);
    http.setup();
    mm::platform::setTestFreeInternalHeap(mm::platform::HEAP_RESERVE + 256);
    mm::platform::TcpConnection preview;
    REQUIRE(connectAndAccept(http, preview, kTestPort));
    const char* upgrade = "GET /wsp HTTP/1.1\r\nHost: test\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";
    REQUIRE(preview.write(reinterpret_cast<const uint8_t*>(upgrade), std::strlen(upgrade)));
    std::string handshake = pumpUntil(http, preview, 1);
    REQUIRE(handshake.rfind("HTTP/1.1 101", 0) == 0);
    REQUIRE(handshake.find("\r\n\r\n") != std::string::npos);
    mm::platform::TcpConnection answer;
    REQUIRE(connectAndAccept(http, answer, kTestPort));
    const char* request = "GET /api/presets/QA HTTP/1.1\r\nHost: test\r\n\r\n";
    REQUIRE(answer.write(reinterpret_cast<const uint8_t*>(request), std::strlen(request)));
    const uint8_t appHeader[] = {1, 2, 3};
    const std::string pixels(48, 'p');
    std::string previewWire, reply;
    unsigned offered = 0;
    bool closed = false;
    for (unsigned tick = 0; tick < 4000 && !closed; ++tick) {
        if (http.bufferedSendIdle()) {
            REQUIRE(http.sendBufferedFrame(appHeader, sizeof(appHeader),
                reinterpret_cast<const uint8_t*>(pixels.data()), pixels.size()));
            ++offered;
        }
        http.tick20ms();
        char buffer[4096];
        int count = preview.read(reinterpret_cast<uint8_t*>(buffer), sizeof(buffer));
        REQUIRE(count != 0);
        if (count > 0) previewWire.append(buffer, static_cast<size_t>(count));
        count = answer.read(reinterpret_cast<uint8_t*>(buffer), sizeof(buffer));
        if (count > 0) reply.append(buffer, static_cast<size_t>(count));
        if (count == 0) closed = true;
        if (count < 0) mm::platform::delayMs(1);
    }
    REQUIRE(closed);
    REQUIRE(offered >= 2);
    REQUIRE(reply.rfind("HTTP/1.1 200", 0) == 0);
    CHECK(responseBytes(reply) == reply.size());
    CHECK(reply.substr(reply.find("\r\n\r\n") + 4) == document);
    const std::string frame = std::string("\x82\x33\x01\x02\x03", 5) + pixels;
    for (unsigned tick = 0; tick < 1000 && previewWire.size() < offered * frame.size(); ++tick) {
        http.tick20ms();
        char buffer[4096];
        const int count = preview.read(reinterpret_cast<uint8_t*>(buffer), sizeof(buffer));
        REQUIRE(count != 0);
        if (count > 0) previewWire.append(buffer, static_cast<size_t>(count));
        else mm::platform::delayMs(1);
    }
    CHECK(http.bufferedSendIdle());
    REQUIRE(previewWire.size() == offered * frame.size());
    for (unsigned i = 0; i < offered; ++i) CHECK(previewWire.substr(i * frame.size(), frame.size()) == frame);
    preview.close();
    http.release();
}

TEST_CASE("HttpServer: completed file types and tree sources release below the send floor without readmission") {
    ConnectionLimits limits;
    PresetIndexRoot files;
    MetadataHttp fixture;
    const char* path = nullptr;
    SUBCASE("file stream") {
        constexpr char payload[] = "a complete file body";
        REQUIRE(mm::platform::fsWriteAtomic("/bounded.bin", payload, sizeof(payload) - 1));
        path = "/api/file?path=/bounded.bin";
    }
    SUBCASE("types stream") { path = "/api/types"; }
    SUBCASE("single module tree stream") { path = "/api/modules/Drivers"; }
    REQUIRE(path != nullptr);
    mm::platform::TcpConnection warmup;
    REQUIRE(connectAndAccept(fixture.http, warmup, kTestPort));
    const auto full = getUntilClosed(fixture.http, warmup, path);
    REQUIRE(full.rfind("HTTP/1.1 200", 0) == 0);
    const auto headerEnd = full.find("\r\n\r\n");
    REQUIRE(headerEnd != std::string::npos);
    const size_t bodySize = full.size() - headerEnd - 4;
    REQUIRE(bodySize > 0);
    // End the final byte on a byte-budget boundary, before any next-tick refill.
    size_t chunk = 1;
    for (size_t candidate = 2; candidate <= 1024; ++candidate)
        if (bodySize % candidate == 0) chunk = candidate;
    fixture.http.setSendBudget(mm::platform::HEAP_RESERVE, chunk);
    mm::platform::TcpConnection client;
    REQUIRE(connectAndAccept(fixture.http, client, kTestPort));
    const size_t baseline = mm::platform::allocatedBytes();
    const std::string request = std::string("GET ") + path + " HTTP/1.1\r\nHost: test\r\n\r\n";
    REQUIRE(client.write(reinterpret_cast<const uint8_t*>(request.data()), request.size()));
    std::string reply;
    char buffer[4096];
    for (size_t i = 0; i < bodySize + 3000; ++i) {
        fixture.http.tick20ms();
        const int count = client.read(reinterpret_cast<uint8_t*>(buffer), sizeof(buffer));
        if (count > 0) reply.append(buffer, static_cast<size_t>(count));
        const auto end = reply.find("\r\n\r\n");
        if (end != std::string::npos && reply.size() - end - 4 == bodySize) break;
        if (count < 0) mm::platform::delayMs(1);
    }
    REQUIRE(reply == full);
    mm::platform::setTestFreeInternalHeap(mm::HttpServerModule::kSendHeadroom - 1);
    int other = 0;
    REQUIRE(fixture.http.maySendForTest(&other));
    mm::platform::setTestFreeInternalHeap(mm::platform::HEAP_RESERVE - 1);
    fixture.http.tick20ms();
    CHECK(readAfterClose(client) == 0);
    CHECK(mm::platform::allocatedBytes() == baseline);
    fixture.http.endSendForTest(&other);
}

TEST_CASE("HttpServer: a drained streamed preset index closes without fresh send admission") {
    ConnectionLimits limits;
    PresetIndexRoot files;
    mm::HttpServerModule http;
    http.port = kTestPort;
    http.setSendBudget(mm::platform::HEAP_RESERVE, 1);
    http.setup();
    mm::platform::TcpConnection client;
    REQUIRE(connectAndAccept(http, client, kTestPort));
    const size_t baseline = mm::platform::allocatedBytes();
    const char* request = "GET /api/presets HTTP/1.1\r\nHost: test\r\n\r\n";
    REQUIRE(client.write(reinterpret_cast<const uint8_t*>(request), std::strlen(request)));
    std::string reply;
    char buffer[4096];
    for (int i = 0; i < 3000; ++i) {
        http.tick20ms();
        const int count = client.read(reinterpret_cast<uint8_t*>(buffer), sizeof(buffer));
        if (count > 0) reply.append(buffer, static_cast<size_t>(count));
        if (reply.ends_with("}") && wholeJsonResponse(reply)) break;
        if (count < 0) mm::platform::delayMs(1);
    }
    REQUIRE(reply.rfind("HTTP/1.1 200", 0) == 0);
    REQUIRE(wholeJsonResponse(reply));
    CHECK(reply.find("Content-Length:") == std::string::npos);
    int other = 0;
    mm::platform::setTestFreeInternalHeap(mm::HttpServerModule::kSendHeadroom - 1);
    REQUIRE(http.maySendForTest(&other));
    mm::platform::setTestFreeInternalHeap(mm::platform::HEAP_RESERVE - 1);
    http.tick20ms();
    CHECK(readAfterClose(client) == 0);
    CHECK(mm::platform::allocatedBytes() == baseline);
    http.endSendForTest(&other);
    http.release();
}

TEST_CASE("HttpServer: resource waits expire an owned preset reply without renewing its progress clock") {
    ConnectionLimits limits;
    PresetIndexRoot files;
    mm::HttpServerModule http;
    http.port = kTestPort;
    http.setup();
    mm::platform::TcpConnection client;
    REQUIRE(connectAndAccept(http, client, kTestPort));
    const size_t baseline = mm::platform::allocatedBytes();
    mm::platform::setTestFreeInternalHeap(mm::HttpServerModule::kSendHeadroom - 1);
    int other = 0;
    REQUIRE(http.maySendForTest(&other));
    const char* request = "GET /api/presets HTTP/1.1\r\nHost: test\r\n\r\n";
    REQUIRE(client.write(reinterpret_cast<const uint8_t*>(request), std::strlen(request)));
    std::string header;
    char buffer[4096];
    for (int i = 0; i < 100 && header.find("\r\n\r\n") == std::string::npos; ++i) {
        http.tick20ms();
        const int count = client.read(reinterpret_cast<uint8_t*>(buffer), sizeof(buffer));
        if (count > 0) header.append(buffer, static_cast<size_t>(count));
        else mm::platform::delayMs(1);
    }
    REQUIRE(header.rfind("HTTP/1.1 200", 0) == 0);
    REQUIRE(header.find("\r\n\r\n") != std::string::npos);
    REQUIRE(header.size() == header.find("\r\n\r\n") + 4);
    REQUIRE(mm::platform::allocatedBytes() > baseline);
    SUBCASE("another sender retains its fair lease") {}
    SUBCASE("internal heap also falls below the send floor") {
        mm::platform::setTestFreeInternalHeap(mm::platform::HEAP_RESERVE - 1);
    }
    for (const uint32_t elapsed : {2000u, 4000u, 6000u, 8000u, 10000u}) {
        mm::platform::setTestNowMs(1000 + elapsed);
        http.tick20ms();
        CHECK(client.read(reinterpret_cast<uint8_t*>(buffer), sizeof(buffer)) == -1);
    }
    mm::platform::setTestNowMs(11001);
    http.tick20ms();
    CHECK(readAfterClose(client) == 0);
    CHECK(mm::platform::allocatedBytes() == baseline);
    http.endSendForTest(&other);
    http.release();
}

TEST_CASE("HttpServer: a native preset index below the allocator reserve returns a complete 503") {
    ConnectionLimits limits;
    PresetIndexRoot files;
    mm::HttpServerModule http;
    http.port = kTestPort;
    http.setSendBudget(mm::platform::HEAP_RESERVE, 1024);
    http.setup();
    mm::platform::TcpConnection client;
    REQUIRE(connectAndAccept(http, client, kTestPort));
    const size_t baseline = mm::platform::allocatedBytes();
    mm::platform::setTestFreeInternalHeap(26684);
    const std::string reply = getUntilClosed(http, client, "/api/presets");
    REQUIRE(reply.rfind("HTTP/1.1 503", 0) == 0);
    CHECK(responseBytes(reply) == reply.size());
    CHECK(reply.find("HTTP/1.1 200") == std::string::npos);
    CHECK(readAfterClose(client) == 0);
    CHECK(mm::platform::allocatedBytes() == baseline);
    http.release();
}

TEST_CASE("HttpServer: all 64 native presets stream through a small permit without an index-sized allocation") {
    ConnectionLimits limits;
    PresetIndexRoot files;
    files.replaceWith(64);
    PresetAllocationLimit allocations;
    mm::HttpServerModule http;
    http.port = kTestPort;
    http.setSendBudget(mm::platform::HEAP_RESERVE, 96);
    http.setup();
    mm::platform::TcpConnection client;
    REQUIRE(connectAndAccept(http, client, kTestPort));
    const size_t baseline = mm::platform::allocatedBytes();
    mm::platform::setTestFreeInternalHeap(mm::platform::HEAP_RESERVE + 8192);
    mm::platform::setTestAllocRefusedFrom(4096);
    const auto reply = getUntilClosed(http, client, "/api/presets");
    REQUIRE(reply.rfind("HTTP/1.1 200", 0) == 0);
    REQUIRE(readAfterClose(client) == 0);
    CHECK(reply.size() > 8192);
    CHECK(reply.find("Content-Length:") == std::string::npos);
    CHECK(mm::platform::allocatedBytes() == baseline);
    mm::platform::setTestAllocRefusedFrom(0);
    const auto start = reply.find("\r\n\r\n");
    REQUIRE(start != std::string::npos);
    mm::json::JsonDoc doc;
    REQUIRE(mm::json::parse(reply.c_str() + start + 4, doc));
    const auto* items = mm::json::member(doc, doc.rootNode(), "items");
    const auto* limit = mm::json::member(doc, doc.rootNode(), "limit");
    const auto* active = mm::json::member(doc, doc.rootNode(), "active");
    REQUIRE(items != nullptr);
    REQUIRE(items->type == mm::json::JsonType::Array);
    REQUIRE(limit != nullptr);
    CHECK(limit->intValue == 64);
    REQUIRE(active != nullptr);
    const auto* effects = mm::json::member(doc, active, "effects");
    REQUIRE(effects != nullptr);
    CHECK(effects->type == mm::json::JsonType::Null);
    bool seen[64] = {};
    unsigned count = 0;
    for (const auto* item = doc.node(items->firstChild); item; item = doc.node(item->next)) {
        const auto* name = mm::json::member(doc, item, "name");
        const auto* slot = mm::json::member(doc, item, "slot");
        const auto* etag = mm::json::member(doc, item, "etag");
        const auto* roles = mm::json::member(doc, item, "roles");
        const auto* kind = mm::json::member(doc, item, "kind");
        const auto* origin = mm::json::member(doc, item, "originScene");
        REQUIRE(name != nullptr); REQUIRE(slot != nullptr); REQUIRE(etag != nullptr);
        REQUIRE(roles != nullptr); REQUIRE(kind != nullptr); REQUIRE(origin != nullptr);
        unsigned number = 64;
        REQUIRE(std::sscanf(name->str, "Look %u", &number) == 1);
        REQUIRE(number < 64);
        CHECK_FALSE(seen[number]); seen[number] = true;
        CHECK(slot->intValue == number);
        REQUIRE(etag->type == mm::json::JsonType::String);
        CHECK(std::strlen(etag->str) == 64);
        REQUIRE(roles->type == mm::json::JsonType::Array);
        const auto* role = doc.node(roles->firstChild);
        REQUIRE(role != nullptr);
        CHECK(std::strcmp(role->str, "effects") == 0);
        CHECK(role->next == -1);
        CHECK(std::strcmp(kind->str, "look") == 0);
        CHECK(std::strcmp(origin->str, "sparkles") == 0);
        ++count;
    }
    CHECK(count == 64);
    for (bool present : seen) CHECK(present);
    http.release();
}

TEST_CASE("HttpServer: a native preset index resumes a bounded send-room pause with every original row and content tag") {
    ConnectionLimits limits;
    PresetIndexRoot files;
    files.replaceWith(64);
    mm::HttpServerModule http;
    http.port = kTestPort;
    http.setSendBudget(mm::platform::HEAP_RESERVE, 128);
    http.setup();
    mm::platform::TcpConnection reference;
    REQUIRE(connectAndAccept(http, reference, kTestPort));
    const auto expected = getUntilClosed(http, reference, "/api/presets");
    REQUIRE(wholeJsonResponse(expected));
    const auto expectedBody = expected.substr(expected.find("\r\n\r\n") + 4);
    mm::platform::setTestFreeInternalHeap(mm::platform::HEAP_RESERVE + 8192);
    mm::platform::TcpConnection client;
    REQUIRE(connectAndAccept(http, client, kTestPort));
    const char* request = "GET /api/presets HTTP/1.1\r\nHost: test\r\n\r\n";
    REQUIRE(client.write(reinterpret_cast<const uint8_t*>(request), std::strlen(request)));
    std::string reply = pumpUntil(http, client, 512);
    REQUIRE(reply.rfind("HTTP/1.1 200", 0) == 0);
    REQUIRE_FALSE(wholeJsonResponse(reply));
    mm::platform::setTestFreeInternalHeap(mm::platform::HEAP_RESERVE);
    for (unsigned tick = 0; tick < 20; ++tick) {
        http.tick20ms();
        char buffer[4096];
        const int count = client.read(reinterpret_cast<uint8_t*>(buffer), sizeof(buffer));
        CHECK(count != 0);
        if (count > 0) reply.append(buffer, static_cast<size_t>(count));
        else mm::platform::delayMs(1);
    }
    mm::platform::setTestFreeInternalHeap(mm::platform::HEAP_RESERVE + 8192);
    reply += readUntilClosed(http, client);
    CHECK(readAfterClose(client) == 0);
    REQUIRE(wholeJsonResponse(reply));
    CHECK(reply.substr(reply.find("\r\n\r\n") + 4) == expectedBody);
    http.release();
}

TEST_CASE("HttpServer: an empty native preset index is a complete streamed response") {
    ConnectionLimits limits;
    PresetIndexRoot files;
    files.replaceWith(0);
    mm::HttpServerModule http;
    http.port = kTestPort;
    http.setSendBudget(mm::platform::HEAP_RESERVE, 1);
    http.setup();
    mm::platform::TcpConnection client;
    REQUIRE(connectAndAccept(http, client, kTestPort));
    const size_t baseline = mm::platform::allocatedBytes();
    const auto reply = getUntilClosed(http, client, "/api/presets");
    REQUIRE(reply.rfind("HTTP/1.1 200", 0) == 0);
    CHECK(wholeJsonResponse(reply));
    CHECK(reply.find("\"items\":[]") != std::string::npos);
    CHECK(reply.find("\"limit\":64") != std::string::npos);
    CHECK(reply.find("\"active\":{\"effects\":null}") != std::string::npos);
    CHECK(readAfterClose(client) == 0);
    CHECK(mm::platform::allocatedBytes() == baseline);
    http.release();
}

TEST_CASE("HttpServer: a native preset first-file allocation failure returns a complete error before 200") {
    ConnectionLimits limits;
    PresetIndexRoot files;
    const std::string large = "{\"$slot\":0,\"Effects\":{},\"$padding\":\"" + std::string(9000, 'p') + "\"}";
    REQUIRE(mm::platform::fsWriteAtomic("/.config/presets/QA.json", large.data(), large.size()));
    PresetAllocationLimit allocations;
    mm::HttpServerModule http;
    http.port = kTestPort;
    http.setup();
    mm::platform::TcpConnection client;
    REQUIRE(connectAndAccept(http, client, kTestPort));
    const size_t baseline = mm::platform::allocatedBytes();
    mm::platform::setTestAllocRefusedFrom(4096);
    const auto reply = getUntilClosed(http, client, "/api/presets");
    REQUIRE(reply.rfind("HTTP/1.1 507", 0) == 0);
    CHECK(reply.find("HTTP/1.1 200") == std::string::npos);
    CHECK(responseBytes(reply) == reply.size());
    CHECK(wholeJsonResponse(reply));
    CHECK(readAfterClose(client) == 0);
    CHECK(mm::platform::allocatedBytes() == baseline);
    http.release();
}

TEST_CASE("HttpServer: a native preset late deletion or read failure closes an incomplete index instead of a valid prefix") {
    ConnectionLimits limits;
    PresetIndexRoot files;
    files.replaceWith(64);
    PresetAllocationLimit allocations;
    mm::HttpServerModule http;
    http.port = kTestPort;
    http.setSendBudget(mm::platform::HEAP_RESERVE, 1);
    http.setup();
    mm::platform::TcpConnection client;
    REQUIRE(connectAndAccept(http, client, kTestPort));
    const size_t baseline = mm::platform::allocatedBytes();
    const char* request = "GET /api/presets HTTP/1.1\r\nHost: test\r\n\r\n";
    REQUIRE(client.write(reinterpret_cast<const uint8_t*>(request), std::strlen(request)));
    std::string partial = pumpUntil(http, client, 1);
    REQUIRE(partial.rfind("HTTP/1.1 200", 0) == 0);
    REQUIRE_FALSE(wholeJsonResponse(partial));
    REQUIRE(mm::platform::allocatedBytes() > baseline);
    SUBCASE("a later snapshotted file is deleted") {
        std::filesystem::remove_all(files.root + mm::PresetStore::kDirectory);
    }
    SUBCASE("a later file cannot allocate its read buffer") {
        const std::string large = "{\"$slot\":0,\"Effects\":{},\"$padding\":\"" + std::string(9000, 'p') + "\"}";
        for (unsigned i = 0; i < 64; ++i) {
            char path[96];
            std::snprintf(path, sizeof(path), "%s/Look %02u.json", mm::PresetStore::kDirectory, i);
            REQUIRE(mm::platform::fsWriteAtomic(path, large.data(), large.size()));
        }
        mm::platform::setTestAllocRefusedFrom(4096);
    }
    partial += readUntilClosed(http, client);
    CHECK(readAfterClose(client) == 0);
    CHECK_FALSE(wholeJsonResponse(partial));
    CHECK(partial.find("\"limit\":64") == std::string::npos);
    CHECK(mm::platform::allocatedBytes() == baseline);
    http.release();
}

TEST_CASE("HttpServer: disconnecting a streamed native preset index releases its snapshot and fragment") {
    ConnectionLimits limits;
    PresetIndexRoot files;
    files.replaceWith(64);
    mm::HttpServerModule http;
    http.port = kTestPort;
    http.setSendBudget(mm::platform::HEAP_RESERVE, 1);
    http.setup();
    mm::platform::TcpConnection client;
    REQUIRE(connectAndAccept(http, client, kTestPort));
    const size_t baseline = mm::platform::allocatedBytes();
    const char* request = "GET /api/presets HTTP/1.1\r\nHost: test\r\n\r\n";
    REQUIRE(client.write(reinterpret_cast<const uint8_t*>(request), std::strlen(request)));
    const auto partial = pumpUntil(http, client, 1);
    REQUIRE(partial.rfind("HTTP/1.1 200", 0) == 0);
    REQUIRE(mm::platform::allocatedBytes() > baseline);
    client.close();
    for (unsigned tick = 0; tick < 100 && mm::platform::allocatedBytes() > baseline; ++tick) {
        http.tick20ms();
        mm::platform::delayMs(1);
    }
    CHECK(mm::platform::allocatedBytes() == baseline);
    http.release();
}

// A classic ESP32 under a page load and a preview queued sends until 2.6 KB of internal RAM was left, and WiFi, with no room to receive, dropped the board off the network.
TEST_CASE("HttpServer: a send queues nothing while internal RAM is at the floor, and finishes once room returns") {
    FloorGet f;
    std::string got;
    for (int i = 0; i < 50; i++) {
        f.http.tick20ms();
        const int n = f.client.read(reinterpret_cast<uint8_t*>(f.buf), sizeof(f.buf));
        if (n > 0) got.append(f.buf, static_cast<size_t>(n));
        mm::platform::delayMs(1);
    }
    const size_t headerEnd = got.find("\r\n\r\n");
    REQUIRE(headerEnd != std::string::npos);
    CHECK(got.size() == headerEnd + 4);   // the header went, no byte of the body
    mm::platform::setTestFreeInternalHeap(0);
    for (int i = 0; i < 3000; i++) {
        f.http.tick20ms();
        const int n = f.client.read(reinterpret_cast<uint8_t*>(f.buf), sizeof(f.buf));
        if (n > 0) got.append(f.buf, static_cast<size_t>(n));
        else if (n == 0) break;
        else mm::platform::delayMs(1);
    }
    CHECK(got.size() - (headerEnd + 4) == embeddedAppJs().len);
}

// A board held at the floor kept every waiting send, so its slots filled and every later request answered 503 until RAM freed.
TEST_CASE("HttpServer: a send held at the internal-RAM floor a whole stall window closes, so its client retries") {
    mm::platform::setTestNowMs(1000);
    FloorGet f;
    for (int i = 0; i < 20; i++) { f.http.tick20ms(); f.client.read(reinterpret_cast<uint8_t*>(f.buf), sizeof(f.buf)); mm::platform::delayMs(1); }
    mm::platform::setTestNowMs(61000);   // well past the stall window
    int n = -1;
    for (int i = 0; i < 200 && n != 0; i++) {
        f.http.tick20ms();
        n = f.client.read(reinterpret_cast<uint8_t*>(f.buf), sizeof(f.buf));
        if (n < 0) mm::platform::delayMs(1);
    }
    CHECK(n == 0);   // closed: the slot is free again
}

// A classic ESP32 keeps 40 to 55 KB free, below the headroom, and no page file went out at all: below it files go one at a time, never none.
TEST_CASE("HttpServer: an embedded file still arrives whole while internal RAM is short") {
    struct Heap { ~Heap() { mm::platform::setTestFreeInternalHeap(0); } } heap;   // restored however the test ends
    mm::HttpServerModule http;
    http.port = kTestPort;
    const UiFolder embeddedOnly(nullptr);   // no files on disk: the embedded copy is served, as on a device
    http.setup();
    mm::platform::TcpConnection client;
    REQUIRE(connectAndAccept(http, client, kTestPort));

    mm::platform::setTestFreeInternalHeap(mm::HttpServerModule::kSendHeadroom - 1);
    const std::string got = getUntilClosed(http, client, "/app.js");
    const auto headerEnd = got.find("\r\n\r\n");
    REQUIRE(headerEnd != std::string::npos);
    CHECK(got.size() - (headerEnd + 4) == embeddedAppJs().len);

    client.close();
    http.release();
}

// Product send windows retain the 32 KiB allocator reserve below the generic 40 KiB floor.
TEST_CASE("HttpServer: product assets progress below the generic radio-free reserve") {
    struct Heap { ~Heap() { mm::platform::setTestFreeInternalHeap(0); } } heap;
    mm::HttpServerModule http;
    http.port = kTestPort;
    http.setSendBudget(0, 1024);   // even an underspecified product must leave the shared allocator reserve
    const UiFolder embeddedOnly(nullptr);
    http.setup();
    mm::platform::TcpConnection client;
    REQUIRE(connectAndAccept(http, client, kTestPort));
    mm::platform::setTestFreeInternalHeap(mm::platform::HEAP_RESERVE - 1);
    const char* request = "GET /app.js HTTP/1.1\r\nHost: test\r\n\r\n";
    REQUIRE(client.write(reinterpret_cast<const uint8_t*>(request), std::strlen(request)));
    std::string bytes;
    uint8_t buffer[4096];
    for (int i = 0; i < 20; i++) {
        http.tick20ms();
        const int n = client.read(buffer, sizeof(buffer));
        if (n > 0) bytes.append(reinterpret_cast<char*>(buffer), static_cast<size_t>(n));
        else mm::platform::delayMs(1);
    }
    const size_t headerEnd = bytes.find("\r\n\r\n");
    REQUIRE(headerEnd != std::string::npos);
    CHECK(bytes.size() == headerEnd + 4);
    mm::platform::setTestFreeInternalHeap(35 * 1024);
    for (int i = 0; i < 3000; i++) {
        http.tick20ms();
        const int n = client.read(buffer, sizeof(buffer));
        if (n > 0) bytes.append(reinterpret_cast<char*>(buffer), static_cast<size_t>(n));
        else if (n == 0) break;
        else mm::platform::delayMs(1);
    }
    CHECK(bytes.size() - (headerEnd + 4) == embeddedAppJs().len);
    client.close();
    http.release();
}
