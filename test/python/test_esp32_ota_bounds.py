"""Exercise the real ESP32 otaWrite/otaEnd control flow against a recording flash seam.

The function bodies are compiled unchanged; the seam records flush, abort and
boot-slot selection. It is not ESP-IDF, real flash or a power-loss test.
"""
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]
CXX = shutil.which('c++') or shutil.which('g++') or shutil.which('clang++')
pytestmark = pytest.mark.skipif(CXX is None, reason='no C++ compiler')


def test_actual_ota_begin_reserves_slot_and_respects_task_lease(tmp_path):
    source = (ROOT / 'src/platform/esp32/platform_esp32_ota.cpp').read_text()
    image = source[source.index('struct Image {'):source.index('\n// A macro, not a lambda:', source.index('struct Image {'))]
    free = source[source.index('void retainAppLease('):source.index('\n// Write what the buffer holds.', source.index('void retainAppLease('))]
    abort = source[source.index('bool otaAbortApp('):source.index('\n#undef imageStatus', source.index('bool otaAbortApp('))]
    write = source[source.index('bool otaWrite(OtaWrite&'):source.index('\nbool otaEnd(', source.index('bool otaWrite(OtaWrite&'))]
    begin = source[source.index('static bool otaBeginOwned('):source.index('\nsize_t otaNextAppCapacity()', source.index('static bool otaBeginOwned('))]
    seam = r'''
#include "core/util/FirmwareUpdateGate.h"
#include "core/util/FirmwareOtaPort.h"
#include "core/util/FirmwareUploadSession.h"
#include "core/util/FirmwareImage.h"
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <utility>
using namespace mm;
FirmwareUpdateGate updateGate;
FirmwareUpdateLease committedAppLease{updateGate};
using mm::platform::OtaTarget;
using mm::platform::OtaWrite;
struct esp_partition_t { size_t size; };
using esp_ota_handle_t = int;
using esp_err_t = int;
constexpr int ESP_OK = 0, OTA_WITH_SEQUENTIAL_WRITES = 0, MALLOC_CAP_8BIT = 0;
constexpr size_t kOtaChunkBytes = 4;
esp_partition_t running{1024}, target{1024};
const esp_partition_t* esp_ota_get_running_partition() { return &running; }
const esp_partition_t* esp_ota_get_next_update_partition(const void*) { return &target; }
const esp_partition_t* moonBasePartition() { return &target; }
int begins = 0, aborts = 0, beginStatus = 0, abortStatus = 0;
int esp_ota_begin(const esp_partition_t*, int, int* handle) { ++begins; *handle = 1; return beginStatus; }
int esp_ota_abort(int) { ++aborts; return abortStatus; }
const char* esp_err_to_name(int) { return "mock"; }
void* heap_caps_malloc(size_t n, int) { return std::malloc(n); }
void heap_caps_free(void* p) { std::free(p); }
void statusf(char* out, size_t n, const char* format, ...) {
    va_list args; va_start(args, format); std::vsnprintf(out, n, format, args); va_end(args);
}
'''
    adapter = r'''
struct RealBeginSeam {
    static size_t capacity() { return target.size; }
    static bool begin(OtaWrite& w, size_t n, char* status, size_t cap, uint32_t* count) {
        return otaBeginOwned(w, OtaTarget::App, n, status, cap, count, nullptr);
    }
    static bool write(OtaWrite& w, const uint8_t* data, size_t n) { return ::otaWrite(w, data, n); }
    static bool end(OtaWrite& w, bool) {
        if (!w.impl) return false;
        freeImage(w, static_cast<Image*>(w.impl)); return true;
    }
    static bool abort(OtaWrite& w) { return ::otaAbortApp(w); }
};
'''
    main = r'''
int main() {
    char s1[96] = {}, s2[96] = {}; uint32_t count = 0; OtaWrite a, b;
    assert(otaBeginOwned(a, OtaTarget::App, 16, s1, sizeof(s1), &count, nullptr));
    assert(!otaBeginOwned(b, OtaTarget::App, 16, s2, sizeof(s2), &count, nullptr));
    assert(!b.impl && begins == 1);
    auto* held = static_cast<Image*>(a.impl); freeImage(a, held);
    assert(aborts == 1);
    FirmwareUpdateLease worker(updateGate); assert(worker.acquire());
    assert(!otaBeginOwned(b, OtaTarget::App, 16, s2, sizeof(s2), &count, nullptr));
    assert(otaBeginOwned(b, OtaTarget::MoonBase, 16, s2, sizeof(s2), &count, &worker));
    freeImage(b, static_cast<Image*>(b.impl));
    assert(worker.held() && !updateGate.acquire()); worker.release();
    beginStatus = 1;
    assert(!otaBeginOwned(a, OtaTarget::App, 16, s1, sizeof(s1), &count, nullptr));
    assert(!a.impl && updateGate.acquire()); updateGate.release();
    beginStatus = 0;
    assert(otaBeginOwned(a, OtaTarget::App, 16, s1, sizeof(s1), &count, nullptr));
    held = static_cast<Image*>(a.impl); retainAppLease(*held); freeImage(a, held);
    assert(committedAppLease.held() && !updateGate.acquire());
    committedAppLease.release(); assert(updateGate.acquire()); updateGate.release();
    // The upload session uses the already-gated real begin body directly;
    // an HTTP contender must fail, then the slot must reopen after abort.
    FirmwareOtaPort<RealBeginSeam> port;
    FirmwareUploadSession upload(port);
    FirmwareUploadSession<decltype(port)>::Digest digest{};
    assert(upload.begin(11, 16, digest) == decltype(upload)::Result::Ok);
    assert(!otaBeginOwned(b, OtaTarget::App, 16, s2, sizeof(s2), &count, nullptr));
    assert(upload.abort(11) == decltype(upload)::Result::Ok);
    assert(otaBeginOwned(b, OtaTarget::App, 16, s2, sizeof(s2), &count, nullptr));
    freeImage(b, static_cast<Image*>(b.impl));
    assert(upload.begin(12, 16, digest) == decltype(upload)::Result::Ok);
    abortStatus = 1;
    assert(upload.abort(12) == decltype(upload)::Result::Io);
    decltype(upload)::Status status{};
    assert(upload.status(12, status) && status.phase == decltype(upload)::Phase::Failed);
    assert(committedAppLease.held() && !updateGate.acquire());
    assert(!otaBeginOwned(b, OtaTarget::App, 16, s2, sizeof(s2), &count, nullptr));
    committedAppLease.release(); abortStatus = 0;
    // A failed flash write invokes implicit cleanup before the session sees the
    // failure. If that cleanup fails, no new writer may acquire the slot.
    assert(upload.begin(13, 16, digest) == decltype(upload)::Result::Ok);
    abortStatus = 1; uint8_t bytes[4] = {};
    assert(upload.append(13, 0, bytes, sizeof(bytes)) == decltype(upload)::Result::Io);
    assert(upload.status(13, status) && status.phase == decltype(upload)::Phase::Failed);
    assert(committedAppLease.held() && !updateGate.acquire());
    assert(!otaBeginOwned(b, OtaTarget::App, 16, s2, sizeof(s2), &count, nullptr));
    committedAppLease.release(); abortStatus = 0;
    assert(otaBeginOwned(b, OtaTarget::App, 16, s2, sizeof(s2), &count, nullptr));
    freeImage(b, static_cast<Image*>(b.impl));
    // The legacy cleanup path also retains the gate if its implicit abort fails.
    assert(otaBeginOwned(a, OtaTarget::App, 16, s1, sizeof(s1), &count, nullptr));
    abortStatus = 1; const int priorAborts = aborts;
    freeImage(a, static_cast<Image*>(a.impl));
    assert(aborts == priorAborts + 1 && committedAppLease.held() && !updateGate.acquire());
    committedAppLease.release(); abortStatus = 0;
}
'''
    cpp, binary = tmp_path / 'guard.cpp', tmp_path / 'guard'
    cpp.write_text(seam + image + '\n#define imageStatus(img, ...) statusf((img)->status, (img)->statusLen, __VA_ARGS__)\n' + free + begin + '\nbool flushImage(Image*) { return false; }\n' + write + abort + adapter + main)
    result = subprocess.run([CXX, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src'), '-I', str(ROOT / 'src/platform/desktop'), str(cpp), str(ROOT / 'src/core/util/sha256.cpp'), '-o', str(binary)], capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr
    subprocess.run([str(binary)], check=True, timeout=5)


def test_actual_url_starts_reserve_and_release_shared_gate(tmp_path):
    source = (ROOT / 'src/platform/esp32/platform_esp32_ota.cpp').read_text()
    params = source[source.index('struct OtaTaskParams {'):source.index('\n// One bounded status writer', source.index('struct OtaTaskParams {'))]
    app = source[source.index('bool http_fetch_to_ota('):source.index('\n\n// Updating the recovery image itself', source.index('bool http_fetch_to_ota('))]
    start = source.index('bool otaFetchMoonBaseUrl(')
    moon = source[start:source.index('\n\n', source.index('\n    return true;\n}', start))]
    seam = r'''
#include "core/util/FirmwareUpdateGate.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
using namespace mm;
FirmwareUpdateGate updateGate;
using BaseType_t = int;
constexpr int pdPASS = 1;
void otaTask(void*) {}
void moonBaseUrlTask(void*) {}
'''
    stubs = r'''
void* captured = nullptr; bool failCreate = false;
int xTaskCreate(void (*)(void*), const char*, unsigned, void* arg, unsigned, void*) {
    if (failCreate) return 0;
    captured = arg; return pdPASS;
}
void otaSetStatus(OtaTaskParams* p, const char* text) { std::snprintf(p->statusBuf, p->statusBufLen, "%s", text); }
'''
    main = r'''
int main() {
    char first[128] = {}, other[128] = {}; uint32_t read = 0, total = 0;
    assert(http_fetch_to_ota("https://a", first, sizeof(first), &read, &total, nullptr));
    auto* pending = static_cast<OtaTaskParams*>(captured);
    assert(pending->lease.held());
    assert(!otaFetchMoonBaseUrl("https://b", other, sizeof(other), &read, &total));
    delete pending; captured = nullptr;
    assert(otaFetchMoonBaseUrl("https://b", other, sizeof(other), &read, &total));
    pending = static_cast<OtaTaskParams*>(captured); assert(pending->lease.held());
    assert(!http_fetch_to_ota("https://a", first, sizeof(first), &read, &total, nullptr));
    delete pending; captured = nullptr;
    failCreate = true;
    assert(!http_fetch_to_ota("https://a", first, sizeof(first), &read, &total, nullptr));
    assert(updateGate.acquire()); updateGate.release();
    assert(!otaFetchMoonBaseUrl("https://b", other, sizeof(other), &read, &total));
    assert(updateGate.acquire()); updateGate.release();
}
'''
    cpp, binary = tmp_path / 'url_gate.cpp', tmp_path / 'url_gate'
    cpp.write_text(seam + params + stubs + app + moon + main)
    result = subprocess.run([CXX, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src'), str(cpp), '-o', str(binary)], capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr
    subprocess.run([str(binary)], check=True, timeout=5)


def test_next_app_capacity_refuses_unknown_or_running_slot(tmp_path):
    source = (ROOT / 'src/platform/esp32/platform_esp32_ota.cpp').read_text()
    fn = source[source.index('size_t otaNextAppCapacity()'):source.index('\nbool otaBegin(', source.index('size_t otaNextAppCapacity()'))]
    seam = r'''
#include <cassert>
#include <cstddef>
struct esp_partition_t { size_t size; };
const esp_partition_t* running = nullptr;
const esp_partition_t* next = nullptr;
const esp_partition_t* esp_ota_get_running_partition() { return running; }
const esp_partition_t* esp_ota_get_next_update_partition(const void*) { return next; }
'''
    main = r'''
int main() {
    esp_partition_t active{4096}, target{8192};
    assert(otaNextAppCapacity() == 0);
    next = &target; assert(otaNextAppCapacity() == 0);
    running = &active; next = nullptr; assert(otaNextAppCapacity() == 0);
    next = &active; assert(otaNextAppCapacity() == 0);
    next = &target; assert(otaNextAppCapacity() == 8192);
}
'''
    cpp, binary = tmp_path / 'slot.cpp', tmp_path / 'slot'
    cpp.write_text(seam + fn + main)
    result = subprocess.run([CXX, '-std=c++20', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(binary)], capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr
    subprocess.run([str(binary)], check=True, timeout=5)


@pytest.fixture(scope='module')
def writer_binary(tmp_path_factory):
    source = (ROOT / 'src/platform/esp32/platform_esp32_ota.cpp').read_text()
    writer = source[source.index('bool otaWrite(OtaWrite&'):source.index('\nbool otaAbortApp(')]
    seam = r'''
#include "core/util/FirmwareImage.h"
#include "core/util/FirmwareUploadCoordinator.h"
#include "core/util/build_info.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
using namespace mm;
namespace mm { const char* const kFirmwareName = "helio1"; }
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_IMAGE_VERIFY = 0;
constexpr size_t kOtaChunkBytes = 4;
enum class OtaTarget { App, MoonBase };
struct Part { size_t size = 16; size_t address = 0; };
struct Image { OtaTarget target = OtaTarget::App; Part* part; int handle = 1; uint8_t* buf;
    size_t held = 0; uint32_t written = 0, erased = 0; size_t contentLen = 0;
    char* status; size_t statusLen; uint32_t* bytesOut; };
struct OtaWrite { void* impl = nullptr; };
struct esp_partition_pos_t { size_t offset; size_t size; };
struct esp_image_metadata_t {};
int flushes = 0, aborts = 0, commits = 0, ends = 0, retained = 0;
bool failEnd = false, failBoot = false;
#define imageStatus(img, ...) std::snprintf((img)->status, (img)->statusLen, __VA_ARGS__)
const char* esp_err_to_name(int) { return "mock"; }
void freeImage(OtaWrite& w, Image* img) { if (img->handle) ++aborts; w.impl = nullptr; }
void retainAppLease(Image&) { ++retained; }
bool flushImage(Image* img) { if (img->held) { ++flushes; img->written += img->held; img->held = 0; } return true; }
int esp_ota_end(int) { ++ends; return failEnd ? 1 : ESP_OK; }
int esp_ota_set_boot_partition(Part*) { if (failBoot) return 1; ++commits; return ESP_OK; }
int esp_image_verify(int, esp_partition_pos_t*, esp_image_metadata_t*) { return ESP_OK; }
'''
    main = r'''
int main() {
    Part part; uint8_t buffer[kOtaChunkBytes] = {}, input[16] = {}; char status[160] = {}; uint32_t bytes = 0;
    Image img{OtaTarget::App, &part, 1, buffer, 0, 0, 0, 8, status, sizeof(status), &bytes};
    OtaWrite w{&img};
    assert(otaWrite(w, input, 4) && flushes == 1);
    assert(!otaWrite(w, input, 5) && !w.impl && aborts == 1 && !commits && !ends);
    assert(!otaEnd(w, true) && !commits);
    img = {OtaTarget::App, &part, 1, buffer, 0, 0, 0, 8, status, sizeof(status), &bytes}; w.impl = &img;
    assert(otaWrite(w, input, 3));
    assert(!otaEnd(w, true) && !w.impl && aborts == 2 && flushes == 1 && !commits && !ends);
    img = {OtaTarget::App, &part, 1, buffer, 0, 0, 0, 8, status, sizeof(status), &bytes}; w.impl = &img;
    assert(otaWrite(w, input, 3) && otaWrite(w, input, 5));
    assert(otaEnd(w, true) && !w.impl && ends == 1 && commits == 1 && aborts == 2 && retained == 1);
    img = {OtaTarget::App, &part, 1, buffer, 0, 0, 0, 0, status, sizeof(status), &bytes}; w.impl = &img;
    assert(!otaWrite(w, input, 17) && !w.impl && aborts == 3 && commits == 1);
    // Explicitly interrupted transfers never validate or select a new image.
    img = {OtaTarget::App, &part, 1, buffer, 0, 0, 0, 8, status, sizeof(status), &bytes}; w.impl = &img;
    assert(otaWrite(w, input, 3));
    assert(!otaEnd(w, false) && !w.impl && aborts == 4 && ends == 1 && commits == 1 && retained == 1);
    // Validation failure consumes the OTA handle but must not select a boot partition.
    img = {OtaTarget::App, &part, 1, buffer, 0, 0, 0, 8, status, sizeof(status), &bytes}; w.impl = &img;
    assert(otaWrite(w, input, 8)); failEnd = true;
    assert(!otaEnd(w, true) && !w.impl && ends == 2 && commits == 1 && retained == 1 && aborts == 4);
    // Boot selection failure must be reported, without retaining the success lease.
    img = {OtaTarget::App, &part, 1, buffer, 0, 0, 0, 8, status, sizeof(status), &bytes}; w.impl = &img;
    assert(otaWrite(w, input, 8)); failEnd = false; failBoot = true;
    assert(!otaEnd(w, true) && !w.impl && ends == 3 && commits == 1 && retained == 1 && aborts == 4);
    // Exercise the actual writer through framed transfer dispatch, including its
    // boot-selection failure. The flash and ESP-IDF calls remain simulated.
    struct WriterBackend {
        OtaWrite& writer; Image& image;
        size_t capacity() const { return 16; }
        bool begin(size_t n) { image.contentLen = n; writer.impl = &image; return true; }
        bool write(const uint8_t* p, size_t n) { return otaWrite(writer, p, n); }
        bool end(bool commit) { return otaEnd(writer, commit); }
    };
    img = {OtaTarget::App, &part, 1, buffer, 0, 0, 0, 0, status, sizeof(status), &bytes}; w.impl = nullptr;
    WriterBackend backend{w, img}; mm::FirmwareUploadCoordinator coordinator(backend, 100, 100);
    using R = decltype(coordinator)::Result;
    uint8_t begin[38] = {1, 1, 8, 0, 0, 0}; mm::sha256(input, 8, begin + 6);
    const uint8_t chunk[] = {1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    const uint8_t finish[] = {1, 3}, query[] = {1, 5};
    auto report = coordinator.handleAndReport(1, begin, sizeof(begin), 1);
    assert(report.result == R::Ok && report.hasStatus && report.status.received == 0);
    report = coordinator.handleAndReport(2, chunk, sizeof(chunk), 2);
    assert(report.result == R::Owner && !report.hasStatus);
    report = coordinator.handleAndReport(1, chunk, sizeof(chunk), 2);
    assert(report.result == R::Ok && report.hasStatus && report.status.received == 8);
    report = coordinator.handleAndReport(1, finish, sizeof(finish), 3);
    assert(report.result == R::Io && report.hasStatus && report.status.phase == decltype(coordinator)::Session::Phase::Failed);
    decltype(coordinator)::Status receipt{};
    assert(coordinator.handle(1, query, sizeof(query), 4, &receipt) == R::Ok);
    assert(receipt.phase == decltype(coordinator)::Session::Phase::Failed && receipt.result == R::Io);
    assert(!w.impl && commits == 1 && retained == 1);
    img = {OtaTarget::App, &part, 1, buffer, 0, 0, 0, 0, status, sizeof(status), &bytes};
    failBoot = false;
    assert(coordinator.handle(2, begin, sizeof(begin), 5) == R::Ok);
    assert(coordinator.handle(2, chunk, sizeof(chunk), 6) == R::Ok);
    report = coordinator.handleAndReport(2, finish, sizeof(finish), 7);
    assert(report.result == R::Ok && report.hasStatus && report.status.phase == decltype(coordinator)::Session::Phase::Committed);
    assert(coordinator.handle(2, query, sizeof(query), 8, &receipt) == R::Ok);
    assert(receipt.phase == decltype(coordinator)::Session::Phase::Committed && receipt.result == R::Ok);
    assert(!w.impl && commits == 2 && retained == 2);
    return 0;
}
'''
    folder = tmp_path_factory.mktemp('esp32-ota-bounds')
    cpp = folder / 'writer.cpp'
    binary = folder / 'writer'
    cpp.write_text(seam + writer + main)
    result = subprocess.run([CXX, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src'),
                             str(cpp), str(ROOT / 'src/core/util/sha256.cpp'), '-o', str(binary)], capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr
    return binary


def test_esp32_writer_rejects_bad_lengths_before_commit(writer_binary):
    result = subprocess.run([str(writer_binary)], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr


def test_esp32_first_flush_checks_chip_before_writing(tmp_path):
    source = (ROOT / 'src/platform/esp32/platform_esp32_ota.cpp').read_text()
    flush = source[source.index('bool flushImage(Image* img)'):source.index('\n}  // namespace', source.index('bool flushImage(Image* img)'))]
    seam = r'''
#include "core/util/FirmwareImage.h"
#include "core/util/build_info.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
using namespace mm;
namespace mm { const char* const kFirmwareName = "helio1"; }
using esp_err_t = int;
constexpr int ESP_OK = 0;
#define CONFIG_IDF_FIRMWARE_CHIP_ID 9
#define imageStatus(img, ...) std::snprintf((img)->status, (img)->statusLen, __VA_ARGS__)
enum class OtaTarget { App, MoonBase };
struct Part { uint32_t size = 4096; };
struct Image { OtaTarget target = OtaTarget::App; Part* part; int handle = 1; uint8_t* buf;
    size_t held = 0; uint32_t written = 0, erased = 0; size_t contentLen = 0;
    char* status; size_t statusLen; uint32_t* bytesOut; };
int writes = 0;
bool moonBaseImageRejected(const uint8_t*, size_t, char*, size_t) { return false; }
const char* esp_err_to_name(int) { return "mock"; }
int eraseAhead(Part*, uint32_t&, uint32_t) { return ESP_OK; }
int esp_partition_write(Part*, uint32_t, const void*, size_t) { ++writes; return ESP_OK; }
int esp_ota_write(int, const void*, size_t) { ++writes; return ESP_OK; }
'''
    main = r'''
void makeImage(uint8_t* buf, uint16_t chip, const char* name) {
    std::memset(buf, 0, firmware::kIdentifyBytes + 4);
    buf[0] = firmware::kImageMagic;
    std::memcpy(buf + firmware::kChipIdOffset, &chip, sizeof(chip));
    uint32_t magic = firmware::kDescMagic;
    std::memcpy(buf + firmware::kDescOffset, &magic, sizeof(magic));
    std::memcpy(buf + firmware::kDescOffset + 48, name, std::strlen(name));
}
int main(int argc, char** argv) {
    Part part; uint8_t buf[firmware::kVariantOffset + firmware::kVariantBytes] = {}; char status[160] = {}; uint32_t bytes = 0;
    Image img{OtaTarget::App, &part, 1, buf, firmware::kIdentifyBytes, 0, 0, 0, status, sizeof(status), &bytes};
    makeImage(buf, static_cast<uint16_t>(firmware::ChipId::Esp32), "MoonLight");
    assert(!flushImage(&img) && writes == 0 && std::strstr(status, "another chip"));
    makeImage(buf, static_cast<uint16_t>(firmware::ChipId::Esp32S3), "MoonLight-moonbase");
    assert(!flushImage(&img) && writes == 0 && std::strstr(status, "MoonBase"));
    makeImage(buf, static_cast<uint16_t>(firmware::ChipId::Esp32S3), "MoonLight");
    assert(flushImage(&img) && writes == 1 && img.written == firmware::kIdentifyBytes);
    std::memset(buf, 0, sizeof(buf));
    makeImage(buf, static_cast<uint16_t>(firmware::ChipId::Esp32S3), "MoonLight");
    std::memcpy(buf + firmware::kVariantOffset, firmware::kVariantMagic, 8);
    std::memcpy(buf + firmware::kVariantOffset + 8, "nebulite-s3", 12);
    img.held = sizeof(buf); img.written = 0;
    assert(!flushImage(&img) && writes == 1 && std::strstr(status, "variant mismatch"));
    std::memset(buf + firmware::kVariantOffset + 8, 0, 32);
    std::memcpy(buf + firmware::kVariantOffset + 8, "helio1", 7);
    assert(flushImage(&img) && writes == 2);
    if (argc == 3) {
        uint8_t image[4096] = {};
        std::ifstream wrong(argv[1], std::ios::binary);
        wrong.read(reinterpret_cast<char*>(image), sizeof(image));
        assert(wrong.gcount() == sizeof(image));
        img.buf = image; img.held = sizeof(image); img.written = 0;
        assert(!flushImage(&img) && writes == 2 && std::strstr(status, "variant mismatch"));
        std::ifstream right(argv[2], std::ios::binary);
        right.read(reinterpret_cast<char*>(image), sizeof(image));
        assert(right.gcount() == sizeof(image));
        assert(flushImage(&img) && writes == 3);
    }
    return 0;
}
'''
    cpp, binary = tmp_path / 'flush.cpp', tmp_path / 'flush'
    cpp.write_text(seam + flush + main)
    result = subprocess.run([CXX, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src'),
                             str(cpp), '-o', str(binary)], capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr
    wrong = ROOT / 'build/esp32-nebulite-s3/MoonLight.bin'
    right = ROOT / 'build/esp32-helio1/MoonLight.bin'
    # Real-image checks are only enabled when both tagged product builds exist.
    tagged = lambda p: p.is_file() and p.read_bytes()[288:296] == b'MMVAR01\0'
    real_images = [str(wrong), str(right)] if tagged(wrong) and tagged(right) else []
    result = subprocess.run([str(binary), *real_images], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr


def test_https_variant_is_checked_before_selecting_boot_partition(tmp_path):
    source = (ROOT / 'src/platform/esp32/platform_esp32_ota.cpp').read_text()
    start = source.index('    // ESP-IDF\'s HTTPS helper exposes only')
    end = source.index('    err = esp_https_ota_finish(handle);', start)
    guard = source[start:end]
    seam = r'''
#include "core/util/FirmwareImage.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace mm;
struct Part { int value; }; Part active{1}, inactive{2};
struct Params {}; int handle=1, written=328, errors=0, aborts=0, finishes=0;
bool readFail=false, same=false, product=true;
uint8_t flash[400]={};
const char* kFirmwareName="helio1";
bool isProductFirmware(const char*) { return product; }
const Part* esp_ota_get_running_partition() { return &active; }
const Part* esp_ota_get_next_update_partition(void*) { return same ? &active : &inactive; }
int esp_https_ota_get_image_len_read(int) { return written; }
constexpr int ESP_OK=0;
int esp_partition_read(const Part*, size_t offset, void* out, size_t len) {
  if (readFail) return 1;
  std::memcpy(out, flash+offset, len); return ESP_OK;
}
void otaSetStatus(Params*, const char*, const char*) { ++errors; }
void abortHttpsOta(Params*, int) { ++aborts; }
void vTaskDelete(void*) {}
void check() { auto* p=new Params;
'''
    main = r'''
  ++finishes; delete p;
}
int main() {
  std::memcpy(flash+firmware::kVariantOffset, firmware::kVariantMagic, 8);
  std::memcpy(flash+firmware::kVariantOffset+8, "nebulite-s3", 12);
  check(); assert(aborts==1 && errors==1 && finishes==0);
  std::memset(flash+firmware::kVariantOffset+8, 0, 32);
  std::memcpy(flash+firmware::kVariantOffset+8, "helio1", 7);
  check(); assert(finishes==1 && aborts==1);
  written=300; check(); assert(aborts==2 && finishes==1); // tagged but truncated
  written=328; std::memset(flash+firmware::kVariantOffset, 0, 40);
  check(); assert(finishes==2 && aborts==2); // historical untagged
  readFail=true; check(); assert(aborts==3 && finishes==2);
  readFail=false; same=true; check(); assert(aborts==4 && finishes==2);
  same=false; product=false; check(); assert(finishes==3); // generic behavior unchanged
}
'''
    cpp, binary = tmp_path / 'https_guard.cpp', tmp_path / 'https_guard'
    cpp.write_text(seam + guard + main)
    compiled = subprocess.run([CXX, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src'),
                               str(cpp), '-o', str(binary)], capture_output=True, text=True, timeout=30)
    assert compiled.returncode == 0, compiled.stdout + compiled.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr


def test_https_abort_failure_keeps_shared_update_gate(tmp_path):
    source = (ROOT / 'src/platform/esp32/platform_esp32_ota.cpp').read_text()
    start = source.index('void abortHttpsOta(')
    helper = source[start:source.index('\n\nvoid otaTask(', start)]
    seam = r'''
#include "core/util/FirmwareUpdateGate.h"
#include <cassert>
#include <utility>
using namespace mm;
FirmwareUpdateGate updateGate;
FirmwareUpdateLease committedAppLease{updateGate};
struct OtaTaskParams { FirmwareUpdateLease lease{updateGate}; };
using esp_https_ota_handle_t = int;
using esp_err_t = int;
constexpr int ESP_OK = 0;
int abortResult = 0, errors = 0;
int esp_https_ota_abort(int) { return abortResult; }
const char* esp_err_to_name(int) { return "mock"; }
void otaSetStatus(OtaTaskParams*, const char*, const char*) { ++errors; }
'''
    main = r'''
int main() {
  { OtaTaskParams p; assert(p.lease.acquire()); abortHttpsOta(&p, 1);
    assert(p.lease.held() && !committedAppLease.held() && errors == 0); }
  assert(updateGate.acquire()); updateGate.release();
  { OtaTaskParams p; assert(p.lease.acquire()); abortResult = 1; abortHttpsOta(&p, 1);
    assert(!p.lease.held() && committedAppLease.held() && errors == 1); }
  assert(!updateGate.acquire());
  committedAppLease.release(); assert(updateGate.acquire()); updateGate.release();
}
'''
    cpp, binary = tmp_path / 'https_abort.cpp', tmp_path / 'https_abort'
    cpp.write_text(seam + helper + main)
    result = subprocess.run([CXX, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src'),
                             str(cpp), '-o', str(binary)], capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr
    subprocess.run([str(binary)], check=True, timeout=5)
