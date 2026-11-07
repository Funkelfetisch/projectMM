// The S3 migration image: installed through WLED's own update page, it receives a signed MoonLight over HTTP, rewrites the partition table to MoonLight's dual-OTA layout and boots MoonLight.
//
// The decisions live in helio/S3Migration.h, where a host test drives them; this file is the board's side. One image per product, MM_MIGRATE_VARIANT: the garment's holds the battery controller's power and checks its battery, HELIO's leaves those pins alone.
// The network journal lives in NVS, which both tables place at 0x9000, so it outlives the payload overwriting WLED's filesystem.
#include "helio/S3Migration.h"
#include "helio/NebuliteS3PowerPins.h"
#include "helio/FirmwarePublisherKey.h"
#include "core/util/FirmwareReleasePolicy.h"
#include "core/util/FirmwareImage.h"
#include "core/util/fnv.h"
#include "core/util/hex.h"
#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_image_format.h"
#include "esp_event.h"
#include "esp_flash.h"
#include "esp_flash_encrypt.h"
#include "esp_flash_partitions.h"
#include "esp_http_server.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "mdns.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_rom_crc.h"
#include "esp_secure_boot.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lfs.h"
#include "nvs_flash.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

extern const uint8_t table_start[] asm("_binary_partition_table_bin_start");
extern const uint8_t table_end[] asm("_binary_partition_table_bin_end");

namespace mm::helio::migration {
namespace {
constexpr char kTag[] = "migrate-s3";
constexpr const char* kProduct = MM_MIGRATE_VARIANT;
// Only the NEBULITE S3 battery controller has the GPIO10 probe and the GPIO21/14 power switch; HELIO never touches those pins.
constexpr bool kGarment = std::string_view(MM_MIGRATE_VARIANT) == "nebulite-s3";
constexpr uint32_t kSector = 4096, kJournalMagic = 0x53334d31;
// This versioned blob stays in the unchanged NVS region; NVS commits are atomic.
struct Journal {
    uint32_t magic = kJournalMagic;
    char ssid[33] = {}, password[65] = {}, apSsid[33] = {}, apPassword[65] = {}, mdnsName[33] = {};
    FirmwareReleaseClaims claims{};
    bool committed = false;   // the app sent commit, so a run after a cut finishes on its own
};
Journal journal;
nvs_handle_t journalHandle = 0;
bool batteryController = false, targetTable = false;
uint32_t measuredBatteryMv = 0;   // the last battery reading, 0 before one or on HELIO
bool bootPowerOk = true;          // the boot-time power verdict, which the status reply and the upload answer from

void pause(const char* boundary) {
    ESP_LOGI(kTag, "%s", boundary);
#if CONFIG_MM_MIGRATE_PAUSE_S > 0
    vTaskDelay(pdMS_TO_TICKS(CONFIG_MM_MIGRATE_PAUSE_S * 1000));
#endif
}

bool flashEqual(uint32_t address, std::span<const uint8_t> expected) {
    std::array<uint8_t, 1024> actual;
    for (size_t at = 0; at < expected.size(); at += actual.size()) {
        const size_t count = std::min(actual.size(), expected.size() - at);
        if (esp_flash_read(nullptr, actual.data(), address + at, count) != ESP_OK ||
            std::memcmp(actual.data(), expected.data() + at, count)) return false;
    }
    return true;
}

bool writeVerified(uint32_t address, std::span<const uint8_t> bytes) {
    const size_t eraseSize = (bytes.size() + kSector - 1) / kSector * kSector;
    return !bytes.empty() && esp_flash_erase_region(nullptr, address, eraseSize) == ESP_OK &&
        esp_flash_write(nullptr, bytes.data(), address, bytes.size()) == ESP_OK && flashEqual(address, bytes);
}

bool journalValid(const Journal& saved) {
    return saved.magic == kJournalMagic && saved.ssid[32] == 0 && saved.password[64] == 0 &&
           saved.apSsid[32] == 0 && saved.apPassword[64] == 0;
}

bool readJournal(Journal& saved) {
    size_t size = sizeof(saved);
    return nvs_get_blob(journalHandle, "journal", &saved, &size) == ESP_OK &&
        size == sizeof(saved) && journalValid(saved);
}

bool saveJournal() {
    if (nvs_set_blob(journalHandle, "journal", &journal, sizeof(journal)) != ESP_OK ||
        nvs_commit(journalHandle) != ESP_OK) return false;
    Journal saved;
    return readJournal(saved) && std::memcmp(&saved, &journal, sizeof(saved)) == 0;
}

// Parse the binary table, rather than esp_partition_find, so extra or hidden entries are refused.
bool parseTable(std::span<const uint8_t> bytes, std::span<Partition> entries, size_t& count) {
    count = 0;
    for (size_t at = 0; at + sizeof(esp_partition_info_t) <= bytes.size(); at += sizeof(esp_partition_info_t)) {
        esp_partition_info_t entry;
        std::memcpy(&entry, bytes.data() + at, sizeof(entry));
        if (entry.magic != ESP_PARTITION_MAGIC) return count > 0;
        if (count == entries.size()) return false;
        entries[count++] = {entry.type, entry.subtype, entry.pos.offset, entry.pos.size, entry.flags};
    }
    return false;
}

bool checkTables() {
    const ptrdiff_t tableBytes = table_end - table_start;
    if (tableBytes <= 0 || tableBytes > static_cast<ptrdiff_t>(kSector)) return false;
    std::array<uint8_t, kSector> bytes;
    std::array<Partition, 16> entries;
    size_t count = 0;
    if (esp_flash_read(nullptr, bytes.data(), 0x8000, bytes.size()) != ESP_OK ||
        !parseTable(bytes, entries, count)) return false;
    const auto actual = std::span<const Partition>(entries.data(), count);
    targetTable = matches(actual, kMoonLight);
    if (!targetTable && !matches(actual, kWled)) return false;
    if (!parseTable({table_start, static_cast<size_t>(table_end - table_start)}, entries, count) ||
        !matches({entries.data(), count}, kMoonLight)) return false;
    return true;
}

bool holdPower() {
    if (!kGarment) return true;
    gpio_config_t probe{};
    probe.pin_bit_mask = 1ULL << NebuliteS3PowerPins::kProbeGpio;
    probe.mode = GPIO_MODE_INPUT;
    if (gpio_config(&probe) != ESP_OK) return false;
    batteryController = gpio_get_level(static_cast<gpio_num_t>(NebuliteS3PowerPins::kProbeGpio));
    if (!batteryController) return true;
    for (const auto pin : {NebuliteS3PowerPins::kEnableGpio, NebuliteS3PowerPins::kMainEnableGpio}) {
        const auto gpio = static_cast<gpio_num_t>(pin);
        // Set the output latch before enabling the driver, then preserve it across reset.
        if (gpio_set_level(gpio, 1) != ESP_OK || gpio_set_direction(gpio, GPIO_MODE_OUTPUT) != ESP_OK ||
            gpio_hold_en(gpio) != ESP_OK) return false;
    }
    gpio_deep_sleep_hold_en();
    return true;
}

bool powerReady() {
    if (!batteryController) return true; // GPIO10 identifies the externally powered controller.
    adc_oneshot_unit_handle_t adc = nullptr;
    adc_oneshot_unit_init_cfg_t init{};
    init.unit_id = ADC_UNIT_2; // S3 GPIO15 = ADC2 channel 4, as the WLED board configuration specifies.
    if (adc_oneshot_new_unit(&init, &adc) != ESP_OK) return false;
    adc_oneshot_chan_cfg_t channel{};
    channel.atten = ADC_ATTEN_DB_12;
    channel.bitwidth = ADC_BITWIDTH_DEFAULT;
    bool ok = adc_oneshot_config_channel(adc, ADC_CHANNEL_4, &channel) == ESP_OK;
    adc_cali_handle_t calibration = nullptr;
    adc_cali_curve_fitting_config_t config{};
    config.unit_id = init.unit_id;
    config.chan = ADC_CHANNEL_4;
    config.atten = channel.atten;
    config.bitwidth = channel.bitwidth;
    ok = ok && adc_cali_create_scheme_curve_fitting(&config, &calibration) == ESP_OK;
    uint32_t totalMv = 0;
    for (int sample = 0; ok && sample < 16; ++sample) {
        int raw = 0, mv = 0;
        ok = adc_oneshot_read(adc, ADC_CHANNEL_4, &raw) == ESP_OK &&
             adc_cali_raw_to_voltage(calibration, raw, &mv) == ESP_OK && mv > 0;
        if (ok) totalMv += static_cast<uint32_t>(mv);
    }
    if (calibration) adc_cali_delete_scheme_curve_fitting(calibration);
    adc_oneshot_del_unit(adc);
    if (!ok) return false;
    const uint32_t batteryMv = batteryMillivolts(totalMv / 16);
    measuredBatteryMv = batteryMv;
    const BatteryVerdict verdict = batteryVerdict(batteryMv);
    if (verdict == BatteryVerdict::Unknown) ESP_LOGW(kTag, "battery reads %u mV, outside a LiPo's range; going on", static_cast<unsigned>(batteryMv));
    return verdict != BatteryVerdict::Low;
}

cJSON* loadJson(const char* path) {
    FILE* file = std::fopen(path, "rb");
    if (!file) return nullptr;
    constexpr size_t capacity = 16385;
    char* bytes = static_cast<char*>(std::malloc(capacity));
    if (!bytes) { std::fclose(file); return nullptr; }
    const size_t count = std::fread(bytes, 1, capacity, file);
    const bool ok = !std::ferror(file) && count < capacity;
    std::fclose(file);
    if (ok) bytes[count] = 0;
    cJSON* result = ok ? cJSON_ParseWithLength(bytes, count + 1) : nullptr;
    std::free(bytes);
    return result;
}

bool copyString(cJSON* object, const char* key, std::span<char> out) {
    const auto* value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsString(value) || std::strlen(value->valuestring) >= out.size()) return false;
    std::strcpy(out.data(), value->valuestring);
    return true;
}

cJSON* firstNetwork(cJSON* root) {
    return cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(
        cJSON_GetObjectItemCaseSensitive(root, "nw"), "ins"), 0);
}

bool captureNetwork() {
    esp_vfs_littlefs_conf_t config{};
    config.base_path = "/wled";
    config.partition_label = "spiffs";
    config.read_only = true;
    if (esp_vfs_littlefs_register(&config) != ESP_OK) return false;
    cJSON* settings = loadJson("/wled/cfg.json");
    cJSON* secrets = loadJson("/wled/wsec.json");
    // A station network needs its password from wsec.json, since taking a missing one for an open network loses the board; a WLED that only ever ran its access point has WLED's placeholder SSID and no password.
    bool ok = settings && copyString(firstNetwork(settings), "ssid", journal.ssid);
    if (ok && !wledStationConfigured(journal.ssid)) journal.ssid[0] = 0;
    if (ok && journal.ssid[0]) ok = secrets && copyString(firstNetwork(secrets), "psk", journal.password);
    if (ok) {
        const auto ap = cJSON_GetObjectItemCaseSensitive(settings, "ap");
        if (!copyString(ap, "ssid", journal.apSsid) || !journal.apSsid[0]) std::strcpy(journal.apSsid, "MoonLight Migration");
        copyString(cJSON_GetObjectItemCaseSensitive(settings, "id"), "mdns", journal.mdnsName);
        if (secrets) copyString(cJSON_GetObjectItemCaseSensitive(secrets, "ap"), "psk", journal.apPassword);
    }
    cJSON_Delete(settings);
    cJSON_Delete(secrets);
    esp_vfs_littlefs_unregister("spiffs");
    return ok && saveJournal();
}

bool moveToApp0() {
    const auto* running = esp_ota_get_running_partition();
    if (!running) return false;
    if (running->address == 0x10000) return true;
    if (targetTable || running->address != 0x210000) return false;
    esp_partition_pos_t position{running->address, running->size};
    esp_image_metadata_t metadata{};
    if (esp_image_verify(ESP_IMAGE_VERIFY, &position, &metadata) != ESP_OK ||
        metadata.image_len > kWledUploadLimit) return false;
    const auto* destination = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
    if (!destination || destination->address != 0x10000) return false;
    esp_ota_handle_t update = 0;
    if (esp_ota_begin(destination, metadata.image_len, &update) != ESP_OK) return false;
    std::array<uint8_t, kSector> buffer;
    for (uint32_t at = 0; at < metadata.image_len; at += buffer.size()) {
        const size_t count = std::min<size_t>(buffer.size(), metadata.image_len - at);
        if (esp_partition_read(running, at, buffer.data(), count) != ESP_OK ||
            esp_ota_write(update, buffer.data(), count) != ESP_OK) {
            esp_ota_abort(update);
            return false;
        }
    }
    if (esp_ota_end(update) != ESP_OK || esp_ota_set_boot_partition(destination) != ESP_OK) return false;
    pause("self copy verified; restarting app0");
    esp_restart();
    return false;
}

bool pinRecovery() {
    const auto* running = esp_ota_get_running_partition();
    if (!running || running->address != 0x10000) return false;
    // Both sectors select app0 under BOTH tables. A cut while replacing either leaves the other usable.
    esp_ota_select_entry_t selection;
    std::memset(&selection, 0xff, sizeof(selection));
    selection.ota_seq = 1;
    selection.ota_state = ESP_OTA_IMG_VALID;
    selection.crc = esp_rom_crc32_le(UINT32_MAX, reinterpret_cast<const uint8_t*>(&selection.ota_seq), sizeof(selection.ota_seq));
    const auto bytes = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&selection), sizeof(selection));
    // Normalize via IDF first: whichever sector it chooses already points to the running app0.
    if (esp_ota_set_boot_partition(running) != ESP_OK) return false;
    // Preserve the newest valid record while replacing its peer first.
    esp_ota_select_entry_t records[2];
    if (esp_flash_read(nullptr, &records[0], 0xe000, sizeof(records[0])) != ESP_OK ||
        esp_flash_read(nullptr, &records[1], 0xf000, sizeof(records[1])) != ESP_OK) return false;
    const auto valid = [](const esp_ota_select_entry_t& record) {
        return record.ota_seq != UINT32_MAX && record.ota_state != ESP_OTA_IMG_INVALID &&
            record.ota_state != ESP_OTA_IMG_ABORTED && record.crc == esp_rom_crc32_le(
                UINT32_MAX, reinterpret_cast<const uint8_t*>(&record.ota_seq), sizeof(record.ota_seq));
    };
    const int newest = valid(records[0]) && (!valid(records[1]) || records[0].ota_seq >= records[1].ota_seq) ? 0 : 1;
    if (!valid(records[newest]) || (records[newest].ota_seq - 1) % 2 != 0) return false;
    for (const int sector : {1 - newest, newest}) {
        if (!writeVerified(0xe000 + sector * kSector, bytes)) return false;
        pause("recovery boot record verified");
    }
    return true;
}

uint32_t productTarget() { return fnv1a(kProduct, std::strlen(kProduct)); }

// The claims alone, before any byte is received: signed for this product, recent enough, within the slot.
bool claimsValid() {
    const auto& claims = journal.claims;
    return claims.imageSize >= firmware::kVariantOffset + firmware::kVariantBytes &&
        payloadAcceptable(claims, productTarget(), kFirmwareSecurityVersion, kFirmwarePublisherKey, claims.imageSize, claims.imageDigest);
}

bool payloadValid() {
    if (!claimsValid()) return false;
    std::array<uint8_t, kSector> buffer;
    Sha256 hash;
    for (uint32_t at = 0; at < journal.claims.imageSize; at += buffer.size()) {
        const size_t count = std::min<size_t>(buffer.size(), journal.claims.imageSize - at);
        if (esp_flash_read(nullptr, buffer.data(), kPayloadOffset + at, count) != ESP_OK) return false;
        if (at == 0) {
            const auto image = firmware::identify(buffer.data(), count);
            const auto variant = firmware::identifyVariant(buffer.data(), count);
            if (!image.described || image.chip != firmware::ChipId::Esp32S3 || !firmware::isAppImage(image.project) ||
                variant.status != firmware::VariantStatus::Valid || std::strcmp(variant.name, kProduct)) return false;
        }
        if (!hash.update(buffer.data(), count)) return false;
    }
    std::array<uint8_t, 32> digest;
    hash.digest(digest.data());
    esp_partition_pos_t position{kPayloadOffset, kPayloadSize};
    esp_image_metadata_t metadata{};
    return payloadAcceptable(journal.claims, productTarget(), kFirmwareSecurityVersion, kFirmwarePublisherKey, journal.claims.imageSize, digest) &&
        esp_image_verify(ESP_IMAGE_VERIFY, &position, &metadata) == ESP_OK && metadata.image_len <= journal.claims.imageSize;
}

int bdRead(const lfs_config* config, lfs_block_t block, lfs_off_t offset, void* data, lfs_size_t size) {
    return esp_flash_read(nullptr, data, kFilesystemOffset + block * config->block_size + offset, size) == ESP_OK ? 0 : LFS_ERR_IO;
}
int bdWrite(const lfs_config* config, lfs_block_t block, lfs_off_t offset, const void* data, lfs_size_t size) {
    return esp_flash_write(nullptr, data, kFilesystemOffset + block * config->block_size + offset, size) == ESP_OK ? 0 : LFS_ERR_IO;
}
int bdErase(const lfs_config* config, lfs_block_t block) {
    return esp_flash_erase_region(nullptr, kFilesystemOffset + block * config->block_size, config->block_size) == ESP_OK ? 0 : LFS_ERR_IO;
}
int bdSync(const lfs_config*) { return 0; }
lfs_config filesystemConfig() {
    lfs_config config{};
    config.read = bdRead; config.prog = bdWrite; config.erase = bdErase; config.sync = bdSync;
    config.read_size = CONFIG_LITTLEFS_READ_SIZE; config.prog_size = CONFIG_LITTLEFS_WRITE_SIZE;
    config.block_size = kSector; config.block_count = kFilesystemSize / kSector;
    config.cache_size = CONFIG_LITTLEFS_CACHE_SIZE; config.lookahead_size = CONFIG_LITTLEFS_LOOKAHEAD_SIZE;
    config.block_cycles = CONFIG_LITTLEFS_BLOCK_CYCLES;
    return config;
}

char* networkDocument() {
    cJSON* root = cJSON_CreateObject();
    cJSON* network = cJSON_AddObjectToObject(root, "Network");
    cJSON* wifi = cJSON_AddObjectToObject(network, "WiFi");
    bool ok = cJSON_AddStringToObject(wifi, "type", "WiFiModule");
    cJSON* known = cJSON_AddArrayToObject(wifi, "known");
    ok = ok && known;
    if (journal.ssid[0]) {
        cJSON* station = cJSON_CreateObject();
        if (!cJSON_AddItemToArray(known, station)) { cJSON_Delete(station); ok = false; }
        else ok = cJSON_AddNumberToObject(station, "id", 1) &&
            cJSON_AddStringToObject(station, "ssid", journal.ssid) &&
            cJSON_AddStringToObject(station, "password", journal.password) &&
            cJSON_AddNumberToObject(station, "ipSettings", 0) && ok;
    }
    char* encoded = ok ? cJSON_PrintUnformatted(root) : nullptr;
    cJSON_Delete(root);
    return encoded;
}

bool networkFile(bool write) {
    char* document = networkDocument();
    if (!document) return false;
    auto config = filesystemConfig();
    lfs_t fs{};
    bool ok = !write || lfs_format(&fs, &config) == 0;
    const bool mounted = ok && lfs_mount(&fs, &config) == 0;
    ok = mounted;
    if (ok && write) ok = lfs_mkdir(&fs, "/.config") == 0;
    lfs_file_t file{};
    const int mode = write ? LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC : LFS_O_RDONLY;
    if (ok && lfs_file_open(&fs, &file, "/.config/NetworkModule.json", mode) == 0) {
        const size_t size = std::strlen(document);
        if (write) ok = lfs_file_write(&fs, &file, document, size) == static_cast<lfs_ssize_t>(size);
        else {
            std::array<char, 1024> bytes{};
            ok = size < bytes.size() && lfs_file_size(&fs, &file) == static_cast<lfs_soff_t>(size) &&
                lfs_file_read(&fs, &file, bytes.data(), bytes.size()) == static_cast<lfs_ssize_t>(size) &&
                std::memcmp(bytes.data(), document, size) == 0;
        }
        ok = lfs_file_close(&fs, &file) == 0 && ok;
    } else ok = false;
    if (mounted) ok = lfs_unmount(&fs) == 0 && ok;
    cJSON_free(document);
    return ok;
}

class FlashStorage final : public Storage {
    bool journalDurable() override { Journal saved; return readJournal(saved) && std::memcmp(&saved, &journal, sizeof(saved)) == 0; }
    bool payloadValid() override { return migration::payloadValid(); }
    bool powerReady() override { return migration::powerReady(); }
    bool pinRecovery() override { return migration::pinRecovery(); }
    bool writeNetwork() override { const bool ok = networkFile(true); if (ok) pause("network written"); return ok; }
    bool networkValid() override { return networkFile(false); }
    bool writeTable() override {
        // A power cut in this single-sector erase/program requires USB recovery.
        if (!migration::powerReady()) return false;
        return writeVerified(0x8000, {table_start, static_cast<size_t>(table_end - table_start)});
    }
    bool activatePayload() override {
        const auto* payload = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, nullptr);
        if (!payload || payload->address != kPayloadOffset || esp_ota_set_boot_partition(payload) != ESP_OK) return false;
        // MoonLight now holds the network in its own filesystem; the journal's copy of the password goes.
        if (nvs_erase_key(journalHandle, "journal") == ESP_OK) nvs_commit(journalHandle);
        return true;
    }
};

void complete() {
    FlashStorage storage;
    const Result result = finish(storage, targetTable);
    if (result == Result::RebootRecovery || result == Result::BootPayload) {
        pause(result == Result::RebootRecovery ? "table verified; restarting recovery" : "payload selected");
        esp_restart();
    }
    ESP_LOGE(kTag, "migration stopped (%u); recovery remains selected", static_cast<unsigned>(result));
}

// The upload's progress, in RAM: a restart begins the upload again, unless the whole image already verifies in place.
struct Upload {
    const char* stage = "ready";   // ready, receiving, received, committing, failed
    uint32_t size = 0, received = 0;
    std::array<uint8_t, 32> sha256{};
    const char* error = nullptr;
} upload;
std::atomic<bool> commitRequested{false};
constexpr uint32_t kMaxChunk = 16384;

// Hex into bytes, exactly `bytes.size()` of them; false on any other length or a non-hex digit.
bool fromHex(const char* text, std::span<uint8_t> bytes) {
    if (!text || std::strlen(text) != bytes.size() * 2) return false;
    for (size_t i = 0; i < bytes.size(); ++i) {
        const int high = hexDigit(text[i * 2]), low = hexDigit(text[i * 2 + 1]);
        if (high < 0 || low < 0) return false;
        bytes[i] = static_cast<uint8_t>(high * 16 + low);
    }
    return true;
}

// Every reply is JSON (docs/reference/migrating-over-the-air.md#the-s3-migration-image-over-http).
esp_err_t reply(httpd_req_t* request, const char* status, const char* body) {
    httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, body);
}
esp_err_t refuse(httpd_req_t* request, const char* status, const char* error) {
    upload.error = error;
    char body[96];
    std::snprintf(body, sizeof(body), "{\"error\":\"%s\",\"received\":%u}", error, static_cast<unsigned>(upload.received));
    return reply(request, status, body);
}

// The build id is PROJECT_VER's build metadata, after its '+'.
const char* buildId() {
    const char* version = esp_app_get_description()->version;
    const char* plus = std::strchr(version, '+');
    return plus ? plus + 1 : version;
}

esp_err_t status(httpd_req_t* request) {
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    uint32_t flash = 0;
    esp_flash_get_size(nullptr, &flash);
    // ADC2 serves Wi-Fi while it runs, so the battery reads as it did at boot; commit measures it again.
    char battery[16] = "null";
    if (batteryController && measuredBatteryMv) {
        const int pct = std::clamp((static_cast<int>(measuredBatteryMv) - 3000) * 100 / 1200, 0, 100);
        std::snprintf(battery, sizeof(battery), "%d", pct);
    }
    char error[40] = "null";
    if (upload.error) std::snprintf(error, sizeof(error), "\"%s\"", upload.error);
    char body[512];
    std::snprintf(body, sizeof(body),
        "{\"schema\":\"nebulite-migrate/1\",\"build\":\"%s\",\"chip\":\"esp32s3\",\"flashBytes\":%u,"
        "\"mac\":\"%02x%02x%02x%02x%02x%02x\",\"variant\":\"%s\",\"stage\":\"%s\",\"slotBytes\":%u,\"maxChunk\":%u,"
        "\"size\":%u,\"received\":%u,\"power\":{\"usb\":null,\"batteryPct\":%s,\"ok\":%s},\"error\":%s}",
        buildId(), static_cast<unsigned>(flash), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], kProduct, upload.stage,
        static_cast<unsigned>(kPayloadSize), static_cast<unsigned>(kMaxChunk), static_cast<unsigned>(upload.size),
        static_cast<unsigned>(upload.received), battery, bootPowerOk ? "true" : "false", error);
    return reply(request, "200 OK", body);
}

esp_err_t begin(httpd_req_t* request) {
    if (!std::strcmp(upload.stage, "committing")) return refuse(request, "409 Conflict", "busy");
    if (!bootPowerOk) return refuse(request, "503 Service Unavailable", "power");
    char body[512] = {};
    if (request->content_len <= 0 || request->content_len >= sizeof(body) ||
        httpd_req_recv(request, body, request->content_len) != static_cast<int>(request->content_len))
        return refuse(request, "400 Bad Request", "body");
    cJSON* root = cJSON_ParseWithLength(body, request->content_len);
    const cJSON* size = cJSON_GetObjectItemCaseSensitive(root, "size");
    std::array<uint8_t, 32> sha256{};
    std::array<uint8_t, kFirmwareReleaseClaimsSize> claims{};
    std::array<uint8_t, 64> signature{};
    FirmwareReleaseClaims decoded;
    const bool parsed = cJSON_IsNumber(size) && size->valuedouble >= 1 &&
        fromHex(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "sha256")), sha256) &&
        fromHex(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "claims")), claims) &&
        fromHex(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "signature")), signature) &&
        decodeFirmwareReleaseClaims(claims.data(), claims.size(), signature.data(), signature.size(), decoded);
    const uint32_t imageSize = parsed ? static_cast<uint32_t>(size->valuedouble) : 0;
    cJSON_Delete(root);
    if (!parsed) return refuse(request, "400 Bad Request", "body");
    if (imageSize > kPayloadSize || imageSize != decoded.imageSize || sha256 != decoded.imageDigest) return refuse(request, "400 Bad Request", "size");
    if (decoded.target != productTarget()) return refuse(request, "400 Bad Request", "target");
    const auto previous = journal.claims;
    journal.claims = decoded;
    if (!claimsValid()) { journal.claims = previous; return refuse(request, "400 Bad Request", "signature"); }
    // The same release as the upload under way resumes it; any other starts from 0.
    const bool resume = !std::strcmp(upload.stage, "receiving") && upload.size == imageSize && upload.sha256 == sha256;
    if (!resume) {
        // Journal commit and readback precede every upload, including a retry after a cut.
        if (!saveJournal() || !pinRecovery()) return refuse(request, "500 Internal Server Error", "flash");
        pause("upload journal durable; app0 pinned");
        upload = {"receiving", imageSize, 0, sha256, nullptr};
    }
    return status(request);
}

esp_err_t chunk(httpd_req_t* request) {
    if (std::strcmp(upload.stage, "receiving")) return refuse(request, "409 Conflict", "state");
    char query[32] = {}, value[16] = {};
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "offset", value, sizeof(value)) != ESP_OK) return refuse(request, "400 Bad Request", "offset");
    const uint32_t offset = static_cast<uint32_t>(std::strtoul(value, nullptr, 10));
    const uint32_t length = request->content_len;
    switch (chunkVerdict(upload.received, offset, length, upload.size, kMaxChunk)) {
    case ChunkVerdict::Duplicate: {
        char body[40];
        std::snprintf(body, sizeof(body), "{\"received\":%u}", static_cast<unsigned>(upload.received));
        return reply(request, "200 OK", body);
    }
    case ChunkVerdict::Offset: return refuse(request, "409 Conflict", "offset");
    case ChunkVerdict::Length: return refuse(request, "400 Bad Request", "length");
    case ChunkVerdict::Write: break;
    }
    static std::array<uint8_t, kMaxChunk> buffer;
    for (uint32_t got = 0; got < length;) {
        const int size = httpd_req_recv(request, reinterpret_cast<char*>(buffer.data() + got), length - got);
        if (size <= 0) return refuse(request, "408 Request Timeout", "timeout");
        got += static_cast<uint32_t>(size);
    }
    if (!writeVerified(kPayloadOffset + offset, {buffer.data(), length})) return refuse(request, "500 Internal Server Error", "flash");
    upload.received = offset + length;
    if (upload.received == upload.size) {
        if (!payloadValid()) {
            upload.stage = "failed"; upload.received = 0;
            return refuse(request, "422 Unprocessable Entity", "sha256");
        }
        upload.stage = "received";
        pause("signed payload readback verified");
    }
    char body[40];
    std::snprintf(body, sizeof(body), "{\"received\":%u}", static_cast<unsigned>(upload.received));
    return reply(request, "200 OK", body);
}

esp_err_t commit(httpd_req_t* request) {
    if (std::strcmp(upload.stage, "received")) return refuse(request, "409 Conflict", "state");
    if (!bootPowerOk) return refuse(request, "503 Service Unavailable", "power");
    journal.committed = true;
    if (!saveJournal()) { journal.committed = false; return refuse(request, "500 Internal Server Error", "flash"); }
    upload.stage = "committing";
    // Sent before the table write and the restarts, so the phone hears it first.
    const esp_err_t sent = reply(request, "202 Accepted", "{\"stage\":\"committing\"}");
    commitRequested = true;
    return sent;
}

void wifiEvent(void*, esp_event_base_t base, int32_t id, void*) {
    if (base == WIFI_EVENT && (id == WIFI_EVENT_STA_START || id == WIFI_EVENT_STA_DISCONNECTED) && journal.ssid[0]) esp_wifi_connect();
}

bool serve() {
    if (esp_netif_init() != ESP_OK || esp_event_loop_create_default() != ESP_OK) return false;
    esp_netif_t* accessPoint = esp_netif_create_default_wifi_ap();
    esp_netif_t* station = esp_netif_create_default_wifi_sta();
    if (!accessPoint || !station) return false;
    // WLED's access-point address, so a phone that installed the migrator through WLED's access point finds it where WLED was.
    esp_netif_ip_info_t address{};
    address.ip.addr = ESP_IP4TOADDR(4, 3, 2, 1);
    address.gw.addr = ESP_IP4TOADDR(4, 3, 2, 1);
    address.netmask.addr = ESP_IP4TOADDR(255, 255, 255, 0);
    if (esp_netif_dhcps_stop(accessPoint) != ESP_OK || esp_netif_set_ip_info(accessPoint, &address) != ESP_OK ||
        esp_netif_dhcps_start(accessPoint) != ESP_OK) return false;
    if (journal.mdnsName[0]) esp_netif_set_hostname(station, journal.mdnsName);
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&init) != ESP_OK || esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK ||
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifiEvent, nullptr) != ESP_OK) return false;
    wifi_config_t ap{}, stationConfig{};
    std::memcpy(ap.ap.ssid, journal.apSsid, std::strlen(journal.apSsid));
    ap.ap.ssid_len = std::strlen(journal.apSsid);
    ap.ap.max_connection = 2;
    ap.ap.channel = 1;
    const size_t apPasswordSize = std::strlen(journal.apPassword);
    if (apPasswordSize >= 8 && apPasswordSize < sizeof(ap.ap.password)) {
        std::memcpy(ap.ap.password, journal.apPassword, apPasswordSize);
        ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else ap.ap.authmode = WIFI_AUTH_OPEN;
    std::memcpy(stationConfig.sta.ssid, journal.ssid, std::strlen(journal.ssid));
    std::memcpy(stationConfig.sta.password, journal.password, std::strlen(journal.password));
    if (esp_wifi_set_mode(journal.ssid[0] ? WIFI_MODE_APSTA : WIFI_MODE_AP) != ESP_OK ||
        esp_wifi_set_config(WIFI_IF_AP, &ap) != ESP_OK ||
        (journal.ssid[0] && esp_wifi_set_config(WIFI_IF_STA, &stationConfig) != ESP_OK) || esp_wifi_start() != ESP_OK) return false;
    // WLED's own mDNS name, and a service the app can browse for.
    if (mdns_init() == ESP_OK) {
        mdns_hostname_set(journal.mdnsName[0] ? journal.mdnsName : "nebulite-migrate");
        mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);
        mdns_service_add(nullptr, "_nebulite-migrate", "_tcp", 80, nullptr, 0);
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 16384;
    config.recv_wait_timeout = 20;
    httpd_handle_t server = nullptr;
    if (httpd_start(&server, &config) != ESP_OK) return false;
    const httpd_uri_t endpoints[] = {
        {"/migrate", HTTP_GET, status, nullptr},
        {"/migrate/begin", HTTP_POST, begin, nullptr},
        {"/migrate/chunk", HTTP_POST, chunk, nullptr},
        {"/migrate/commit", HTTP_POST, commit, nullptr},
    };
    for (const auto& endpoint : endpoints) if (httpd_register_uri_handler(server, &endpoint) != ESP_OK) return false;
    ESP_LOGI(kTag, "migration HTTP ready");
    while (!commitRequested) vTaskDelay(pdMS_TO_TICKS(100));
    vTaskDelay(pdMS_TO_TICKS(500));   // the 202 drains before the radio goes down
    // ADC2 serves Wi-Fi while it runs: the radio goes down before the final power check and the table write.
    httpd_stop(server);
    mdns_free();
    esp_wifi_stop();
    complete();
    return true;
}

} // namespace
} // namespace mm::helio::migration

namespace mm::helio::migration {
namespace {
// Nothing written yet: start WLED again, in the other slot, so a board that cannot move keeps working as it was.
[[noreturn]] void backToWled(const char* why) {
    ESP_LOGE(kTag, "cannot move this board: %s; restarting WLED", why);
    if (const esp_partition_t* other = esp_ota_get_next_update_partition(nullptr)) esp_ota_set_boot_partition(other);
    esp_restart();
}

// Part of the way already: WLED is gone, so wait for what was missing, such as a charging battery, and try again.
[[noreturn]] void retryLater(const char* why) {
    ESP_LOGE(kTag, "migration waits: %s; retrying in 30 s", why);
    vTaskDelay(pdMS_TO_TICKS(30000));
    esp_restart();
}
} // namespace
} // namespace mm::helio::migration

extern "C" void app_main() {
    using namespace mm::helio::migration;
    if (!holdPower()) backToWled("the power switch did not hold");
    uint32_t flashSize = 0;
    if (esp_flash_get_size(nullptr, &flashSize) != ESP_OK || flashSize < 0x800000) backToWled("less than 8 MB of flash");
    if (esp_flash_encryption_enabled() || esp_secure_boot_enabled()) backToWled("flash encryption or secure boot is on");
    if (!checkTables()) backToWled("the partition table is neither WLED's nor MoonLight's");
    // Erasing NVS on a version or full error would destroy the journal and WLED's settings, so a failed init stops here.
    if (nvs_flash_init() != ESP_OK || nvs_open("s3-migrate", NVS_READWRITE, &journalHandle) != ESP_OK) {
        if (targetTable) retryLater("NVS did not open");
        backToWled("NVS did not open");
    }
    Journal saved;
    const bool resuming = readJournal(saved) || targetTable;
    bootPowerOk = powerReady();
    if (!bootPowerOk) {
        if (resuming) retryLater("the battery is low");
        backToWled("the battery is low");
    }
    if (readJournal(saved)) journal = saved;
    else {
        size_t existingSize = 0;
        if (nvs_get_blob(journalHandle, "journal", nullptr, &existingSize) != ESP_ERR_NVS_NOT_FOUND || targetTable) retryLater("the network journal is unreadable");
        if (!captureNetwork()) backToWled("WLED's network did not read");
    }
    pause("network journal durable");
    esp_ota_mark_app_valid_cancel_rollback();
    if (!moveToApp0() || !pinRecovery()) return;
    if (payloadValid()) {
        if (journal.committed) { complete(); return; }
        // Received before a cut but not yet committed: the app commits it again.
        upload = {"received", journal.claims.imageSize, journal.claims.imageSize, journal.claims.imageDigest, nullptr};
    }
    serve();
}
