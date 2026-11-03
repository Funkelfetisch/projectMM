/// NimBLE control transport: the host task owns pairing, advertisements and notifications; the render task copies bounded messages (Espressif bleprph pattern).
#include "platform/platform.h"
#include "core/util/BleMessage.h"
#include "core/util/BlePeerTable.h"
#include "platform/esp32/BleAdvertisementRefresh.h"
#include "platform/esp32/BleConnectionPolicy.h"
#include "platform/esp32/BlePeerDisconnect.h"
#include "sdkconfig.h"

#if CONFIG_BT_NIMBLE_ENABLED
#include "host/ble_hs.h"
#include "host/ble_sm.h"
#include "host/ble_uuid.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "host/util/util.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "platform/esp32/BleRepeatPairing.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
extern "C" void ble_store_config_init(void);

static_assert(!MYNEWT_VAL(BLE_HANDLE_REPEAT_PAIRING_DELETION), "The application owns scoped repeat-pairing authorization");

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>

namespace mm::platform {
constexpr size_t kPeerCapacity = kBleControlPeerCapacity;
namespace {
constexpr const char* kBleTag = "mm_ble";
// The historical BLE2JSON UUID family, with a separate 0x0700 service for this versioned contract.
const ble_uuid128_t serviceUuid = BLE_UUID128_INIT(0x19,0x18,0x8c,0x2c,0x19,0x13,0x9b,0x88,0x7a,0x4f,0xeb,0xa2,0x00,0x07,0xe3,0xbe);
const ble_uuid128_t stateUuid = BLE_UUID128_INIT(0x19,0x18,0x8c,0x2c,0x19,0x13,0x9b,0x88,0x7a,0x4f,0xeb,0xa2,0x01,0x07,0xe3,0xbe);
const ble_uuid128_t commandUuid = BLE_UUID128_INIT(0x19,0x18,0x8c,0x2c,0x19,0x13,0x9b,0x88,0x7a,0x4f,0xeb,0xa2,0x02,0x07,0xe3,0xbe);
const ble_uuid128_t identityUuid = BLE_UUID128_INIT(0x19,0x18,0x8c,0x2c,0x19,0x13,0x9b,0x88,0x7a,0x4f,0xeb,0xa2,0x03,0x07,0xe3,0xbe);
const ble_uuid16_t batteryServiceUuid = BLE_UUID16_INIT(0x180F);
const ble_uuid16_t batteryLevelUuid = BLE_UUID16_INIT(0x2A19);
const ble_uuid16_t batteryFormatUuid = BLE_UUID16_INIT(0x2904);

struct Peripheral;
struct BatterySlot {
    Peripheral* owner = nullptr;
    uint8_t physicalSlot = 0;
    uint16_t valueHandle = 0;
    std::array<uint8_t, 7> format{};
    ble_gatt_chr_def characteristics[2] = {};
    ble_gatt_dsc_def descriptors[2] = {};
};
struct BatteryGatt {
    BleBatteryConfig config{};
    BleBatteryUpdates updates{};
    BatterySlot slots[2];
    ble_gatt_svc_def services[4] = {};
    ble_npl_event changed;
};

struct Peripheral {
    std::mutex mutex;
    std::array<uint8_t, 16> epoch{};
    BlePeerTable<kPeerCapacity> peers;
    char identity[256] = {};
    uint32_t revision = 0;
    uint8_t addressType = 0;
    uint16_t valueHandle = 0;
    std::atomic<uint16_t> connection{BLE_HS_CONN_HANDLE_NONE};
    std::atomic<uint8_t> connectionCount{0};
    std::atomic<uint32_t> linkAccepted{0}, linkFailed{0}, linkDisconnected{0};
    std::atomic<int32_t> lastDisconnectReason{-1}, lastUpdateStatus{-1};
    std::atomic<uint32_t> missingPeerReads{0}, unsupportedAccess{0};
    bool abandoned = false; // scheduler task only: service destroyed after a failed stop
    ble_gatt_chr_def characteristics[4] = {};
    BleRepeatPairing repeatPairing;
    std::atomic<uint32_t> repeatRequests{0}, repeatDeleted{0}, repeatRejected{0};
    std::atomic<uint32_t> fullAllowed{0}, fullRejected{0}, overflows{0}, fullUnknown{0};
    ble_gatt_svc_def services[2] = {};
    ble_npl_event changed;
    ble_npl_event advertisementChanged;
    std::array<uint8_t, 13> advertisement{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255};
    std::array<uint8_t, 13> advertised{};
    uint32_t advertisementAt = 0;
    bool advertisementSet = false;
    BatteryGatt* batteries = nullptr;
    ~Peripheral() { delete batteries; }
};
Peripheral* active = nullptr;  // one radio, claimed during prepare and released after the host stops

uint64_t pairingTimeMs() MM_NONBLOCKING { return static_cast<uint64_t>(esp_timer_get_time()) / 1000u; }

int storeStatus(ble_store_status_event* event, void* argument) {
    auto* peripheral = static_cast<Peripheral*>(argument);
    if (!event || !peripheral || peripheral != active) return BLE_HS_ESTORE_CAP;
    BleRepeatPairing::PeerStatus peer;
    const uint16_t repairHandle = peripheral->connectionCount.load() == 1 ? peripheral->connection.load() : BLE_HS_CONN_HANDLE_NONE;
    const int result = peripheral->repeatPairing.storeStatus(*event, repairHandle, pairingTimeMs(), &peer);
    int object = 0;
    if (event->event_code == BLE_STORE_EVENT_FULL) {
        object = event->full.obj_type;
        (result == 0 ? peripheral->fullAllowed : peripheral->fullRejected).fetch_add(1);
        if (peer == BleRepeatPairing::PeerStatus::Unknown) peripheral->fullUnknown.fetch_add(1);
    } else if (event->event_code == BLE_STORE_EVENT_OVERFLOW) {
        object = event->overflow.obj_type;
        peripheral->overflows.fetch_add(1);
    }
    ESP_LOGW(kBleTag, "store event=%d object=%d peer=%u result=%d", event->event_code, object,
             static_cast<unsigned>(peer), result);
    return result;
}

void recordBondStorage(const Peripheral& peripheral) {
    int peers = -1, own = -1;
    const int peerResult = ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &peers);
    const int ownResult = ble_store_util_count(BLE_STORE_OBJ_TYPE_OUR_SEC, &own);
    char epoch[33];
    for (size_t i = 0; i < peripheral.epoch.size(); ++i)
        std::snprintf(epoch + 2 * i, 3, "%02x", static_cast<unsigned>(peripheral.epoch[i]));
    char document[256];
    const int length = std::snprintf(document, sizeof(document),
        "{\"epoch\":\"%s\",\"atMs\":%llu,\"peerCount\":%d,\"peerRc\":%d,\"ourCount\":%d,\"ourRc\":%d,\"capacity\":%u,\"statusCallback\":%s}",
        epoch, static_cast<unsigned long long>(pairingTimeMs()), peers, peerResult, own, ownResult,
        static_cast<unsigned>(MYNEWT_VAL(BLE_STORE_MAX_BONDS)), ble_hs_cfg.store_status_cb ? "true" : "false");
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(document) || !fsMkdir("/.diagnostics") ||
        !fsWriteAtomic("/.diagnostics/ble-store-20261027.json", document, static_cast<size_t>(length)))
        ESP_LOGW(kBleTag, "bond-store diagnostic snapshot unavailable");
}

bool validIdentity(const char* name, const char* identity) {
    return name && std::strlen(name) <= 29 && identity && std::strlen(identity) < 256;
}

int gapEvent(ble_gap_event* event, void* arg);
void advertise();
int accessBattery(uint16_t, uint16_t, ble_gatt_access_ctxt* context, void* arg);
void notifyBatteries(ble_npl_event* event);

int updateAdvertisement(Peripheral& peripheral) {
    std::array<uint8_t, 13> bytes;
    { std::lock_guard<std::mutex> lock(peripheral.mutex); bytes = peripheral.advertisement; }
    const uint32_t now = millis();
    // Service Data - 128-bit UUID (Bluetooth Core Supplement, AD type 0x21).
    uint8_t scan[31] = {30, 0x21};
    std::memcpy(scan + 2, serviceUuid.value, 16);
    std::memcpy(scan + 18, bytes.data(), bytes.size());
    const int status = ble_gap_adv_rsp_set_data(scan, sizeof(scan));
    if (status == 0) {
        peripheral.advertised = bytes;
        peripheral.advertisementAt = now;
        peripheral.advertisementSet = true;
    }
    return status;
}

void advertisementChanged(ble_npl_event* event) {
    auto* peripheral = static_cast<Peripheral*>(ble_npl_event_get_arg(event));
    // With another connection slot free, the device is still discoverable and
    // its scan response must reflect changes made by the connected client.
    if (peripheral != active) return;
    std::array<uint8_t, 13> desired;
    { std::lock_guard<std::mutex> lock(peripheral->mutex); desired = peripheral->advertisement; }
    const auto action = bleAdvertisementAction(peripheral->connectionCount.load() < kPeerCapacity,
        ble_gap_adv_active(), desired == peripheral->advertised, peripheral->advertisementSet,
        millis(), peripheral->advertisementAt);
    if (action == BleAdvertisementAction::Wait) return;
    if (action == BleAdvertisementAction::Restart) {
        // Legacy NimBLE does not guarantee scan-response changes while advertising.
        const int status = ble_gap_adv_stop();
        if (status != 0 && status != BLE_HS_EALREADY) {
            ESP_LOGW(kBleTag, "scan hint advertising stop: %d", status);
            return;
        }
    }
    advertise();
    // A rate-limited or failed update remains pending; the one-second producer retries the latest value.
}

void advertise() {
    if (!active) return;
    if (ble_gap_adv_active() || active->connectionCount.load() >= kPeerCapacity) return;
    ble_hs_adv_fields fields = {};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = const_cast<ble_uuid128_t*>(&serviceUuid);
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    if (active->batteries) {
        fields.uuids16 = const_cast<ble_uuid16_t*>(&batteryServiceUuid);
        fields.num_uuids16 = 1;
        fields.uuids16_is_complete = 1;
    }
    // The GAP name stays complete; only the primary advertising name is shortened.
    const char* name = ble_svc_gap_device_name();
    const size_t budget = active->batteries ? 4 : 8;
    size_t length = 0;
    while (length < budget && name[length] && static_cast<unsigned char>(name[length]) >= 0x20 && static_cast<unsigned char>(name[length]) <= 0x7e) ++length;
    fields.name = reinterpret_cast<const uint8_t*>(length ? name : "NEBU");
    fields.name_len = static_cast<uint8_t>(length ? length : 4);
    fields.name_is_complete = length && name[length] == '\0';
    int status = ble_gap_adv_set_fields(&fields);
    if (status != 0) { ESP_LOGW(kBleTag, "advertising fields: %d", status); return; }
    status = updateAdvertisement(*active);
    if (status != 0) { ESP_LOGW(kBleTag, "scan response: %d", status); return; }
    ble_gap_adv_params params = {};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    status = ble_gap_adv_start(active->addressType, nullptr, BLE_HS_FOREVER, &params, gapEvent, active);
    if (status != 0) ESP_LOGW(kBleTag, "advertising start: %d", status);
    else ESP_LOGI(kBleTag, "advertising, address type %u", static_cast<unsigned>(active->addressType));
}

/// Request a longer supervision timeout only when the central's current
/// parameters are valid and short. Rejection leaves the link untouched.
void requestConnectionHint(uint16_t handle) {
    ble_gap_conn_desc desc{};
    if (ble_gap_conn_find(handle, &desc) != 0) return;
    BleConnectionHint hint{};
    if (!bleConnectionHint(desc.conn_itvl, desc.conn_latency, desc.supervision_timeout, hint)) return;
    ble_gap_upd_params params{};
    params.itvl_min = params.itvl_max = hint.interval;
    params.latency = hint.latency;
    params.supervision_timeout = hint.timeout;
    const int status = ble_gap_update_params(handle, &params);
    ESP_LOGI(kBleTag, "connection timeout request=%u status=%d", static_cast<unsigned>(hint.timeout), status);
}

int gapEvent(ble_gap_event* event, void* arg) {
    auto* peripheral = static_cast<Peripheral*>(arg);
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            ESP_LOGI(kBleTag, "connect status=%d handle=%u", event->connect.status,
                     static_cast<unsigned>(event->connect.conn_handle));
            if (event->connect.status == 0) {
                {
                    std::lock_guard<std::mutex> lock(peripheral->mutex);
                    const bool first = peripheral->peers.size() == 0;
                    if (!peripheral->peers.connect(event->connect.conn_handle)) {
                        peripheral->linkFailed.fetch_add(1, std::memory_order_relaxed);
                        ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                        break;
                    }
                    if (first) { peripheral->repeatPairing.reset(); peripheral->connection.store(event->connect.conn_handle); }
                    peripheral->connectionCount.store(static_cast<uint8_t>(peripheral->peers.size()));
                    peripheral->linkAccepted.fetch_add(1, std::memory_order_relaxed);
                }
                requestConnectionHint(event->connect.conn_handle);
                advertise();
            }
            else { peripheral->linkFailed.fetch_add(1, std::memory_order_relaxed); advertise(); }
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(kBleTag, "disconnect reason=%d", event->disconnect.reason);
            blePeerDisconnected(*peripheral, event->disconnect.conn.conn_handle, event->disconnect.reason, advertise);
            break;
        case BLE_GAP_EVENT_ADV_COMPLETE: advertise(); break;
        case BLE_GAP_EVENT_CONN_UPDATE:
            peripheral->lastUpdateStatus.store(event->conn_update.status, std::memory_order_relaxed);
            ESP_LOGI(kBleTag, "connection parameter update status=%d", event->conn_update.status);
            break;
        case BLE_GAP_EVENT_SUBSCRIBE: {
            std::lock_guard<std::mutex> lock(peripheral->mutex);
            if (event->subscribe.attr_handle == peripheral->valueHandle)
                peripheral->peers.subscribe(event->subscribe.conn_handle, 0, event->subscribe.cur_notify != 0);
            if (peripheral->batteries) {
                for (uint8_t i = 0; i < peripheral->batteries->config.count; ++i) {
                    auto& slot = peripheral->batteries->slots[i];
                    if (event->subscribe.attr_handle != slot.valueHandle) continue;
                    peripheral->peers.subscribe(event->subscribe.conn_handle, slot.physicalSlot, event->subscribe.cur_notify != 0);
                    // This also covers restored bonded CCCDs, after the stack has restored their subscription.
                    if (event->subscribe.cur_notify)
                        ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &peripheral->batteries->changed);
                }
            }
            break;
        }
        case BLE_GAP_EVENT_ENC_CHANGE:
            ESP_LOGI(kBleTag, "encryption status=%d", event->enc_change.status);
            break;
        case BLE_GAP_EVENT_REPEAT_PAIRING: {
            peripheral->repeatRequests.fetch_add(1);
            const uint16_t repairHandle = peripheral->connectionCount.load() == 1 ? peripheral->connection.load() : BLE_HS_CONN_HANDLE_NONE;
            const int result = peripheral->repeatPairing.handle(event->repeat_pairing, repairHandle, pairingTimeMs());
            if (result == BLE_GAP_REPEAT_PAIRING_RETRY) peripheral->repeatDeleted.fetch_add(1);
            else peripheral->repeatRejected.fetch_add(1);
            ESP_LOGW(kBleTag, "repeat pairing handle=%u result=%d blocked=%u",
                     static_cast<unsigned>(event->repeat_pairing.conn_handle), result,
                     static_cast<unsigned>(peripheral->repeatPairing.blocked()));
            return result;
        }
        default: break;
    }
    return 0;
}

int accessValue(uint16_t connection, uint16_t, ble_gatt_access_ctxt* context, void* arg) {
    auto* peripheral = static_cast<Peripheral*>(arg);
    if (peripheral->repeatPairing.blocked() && connection == peripheral->connection.load() && ble_uuid_cmp(context->chr->uuid, &identityUuid.u) != 0)
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    if (context->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint8_t bytes[513];
        uint16_t count = 0;
        if (ble_hs_mbuf_to_flat(context->om, bytes, sizeof(bytes), &count) != 0)
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        std::lock_guard<std::mutex> lock(peripheral->mutex);
        const auto result = peripheral->peers.feed(connection, bytes, count);
        if (result == BleMessage::Result::Busy) return BLE_ATT_ERR_INSUFFICIENT_RES;
        return result == BleMessage::Result::Invalid ? BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN : 0;
    }
    if (context->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        peripheral->unsupportedAccess.fetch_add(1, std::memory_order_relaxed);
        return BLE_ATT_ERR_UNLIKELY;
    }
    char bytes[513];
    {
        std::lock_guard<std::mutex> lock(peripheral->mutex);
        const bool identity = ble_uuid_cmp(context->chr->uuid, &identityUuid.u) == 0;
        const char* value = identity ? peripheral->identity : connection == BLE_HS_CONN_HANDLE_NONE ? peripheral->peers.initial() :
            peripheral->peers.read(connection, context->offset);
        if (!value) {
            peripheral->missingPeerReads.fetch_add(1, std::memory_order_relaxed);
            return BLE_ATT_ERR_UNLIKELY;
        }
        std::snprintf(bytes, sizeof(bytes), "%s", value);
    }
    return os_mbuf_append(context->om, bytes, static_cast<uint16_t>(std::strlen(bytes))) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

int accessBattery(uint16_t, uint16_t, ble_gatt_access_ctxt* context, void* arg) {
    auto* slot = static_cast<BatterySlot*>(arg);
    if (context->op == BLE_GATT_ACCESS_OP_READ_DSC)
        return os_mbuf_append(context->om, slot->format.data(), static_cast<uint16_t>(slot->format.size())) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    if (context->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;
    uint8_t level = 0;
    {
        std::lock_guard<std::mutex> lock(slot->owner->mutex);
        const auto& updates = slot->owner->batteries->updates;
        if (!updates.fresh(millis()) || !updates.values.slots[slot->physicalSlot - 1].readLevel(level))
            return BLE_ATT_ERR_UNLIKELY;   // BAS has no unknown-level sentinel
    }
    return os_mbuf_append(context->om, &level, 1) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

void notifyBatteries(ble_npl_event* event) {
    auto* peripheral = static_cast<Peripheral*>(ble_npl_event_get_arg(event));
    if (!peripheral->batteries) return;
    BleBatterySnapshot values;
    std::array<std::array<uint16_t, kPeerCapacity>, 2> targets{};
    std::array<size_t, 2> counts{};
    { std::lock_guard<std::mutex> lock(peripheral->mutex);
      if (!peripheral->batteries->updates.fresh(millis())) return;
      values = peripheral->batteries->updates.values;
      for (uint8_t i = 0; i < peripheral->batteries->config.count; ++i)
          counts[i] = peripheral->peers.subscribers(peripheral->batteries->slots[i].physicalSlot, targets[i].data(), targets[i].size()); }
    bool failed = false;
    for (uint8_t i = 0; i < peripheral->batteries->config.count; ++i) {
        auto& slot = peripheral->batteries->slots[i];
        uint8_t level = 0;
        if (!values.slots[slot.physicalSlot - 1].readLevel(level)) continue;
        for (size_t n = 0; n < counts[i]; ++n) {
            os_mbuf* buffer = ble_hs_mbuf_from_flat(&level, 1);
            if (!buffer || ble_gatts_notify_custom(targets[i][n], slot.valueHandle, buffer) != 0) failed = true;
        }
    }
    if (failed) {
        std::lock_guard<std::mutex> lock(peripheral->mutex);
        peripheral->batteries->updates.deliveryFailed();
    }
}

bool configureBatteries(Peripheral& peripheral, const BleBatteryConfig& config) {
    if (!config.count) return true;
    peripheral.batteries = new (std::nothrow) BatteryGatt();
    if (!peripheral.batteries) return false;
    auto& batteries = *peripheral.batteries;
    batteries.config = config;
    batteries.services[0] = peripheral.services[0];   // preserve all existing custom handles
    for (uint8_t i = 0; i < config.count; ++i) {
        auto& slot = batteries.slots[i];
        slot.owner = &peripheral; slot.physicalSlot = config.slots[i];
        slot.format = BleBatteryConfig::presentation(slot.physicalSlot);
        auto& format = slot.descriptors[0];
        format.uuid = &batteryFormatUuid.u; format.att_flags = BLE_ATT_F_READ;
        format.access_cb = accessBattery; format.arg = &slot;
        auto& level = slot.characteristics[0];
        level.uuid = &batteryLevelUuid.u; level.access_cb = accessBattery; level.arg = &slot;
        level.flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY;
        level.val_handle = &slot.valueHandle; level.descriptors = slot.descriptors;
        auto& service = batteries.services[i + 1];
        service.type = BLE_GATT_SVC_TYPE_PRIMARY; service.uuid = &batteryServiceUuid.u;
        service.characteristics = slot.characteristics;
    }
    ble_npl_event_init(&batteries.changed, notifyBatteries, &peripheral);
    return true;
}

void notifyChanged(ble_npl_event* event) {
    auto* peripheral = static_cast<Peripheral*>(ble_npl_event_get_arg(event));
    uint32_t revision = 0;
    std::array<uint16_t, kPeerCapacity> targets{}; size_t count = 0;
    { std::lock_guard<std::mutex> lock(peripheral->mutex);
      revision = peripheral->revision; count = peripheral->peers.subscribers(0, targets.data(), targets.size()); }
    const uint32_t now = millis();
    uint8_t bytes[8];
    for (uint8_t i = 0; i < 4; ++i) { bytes[i] = static_cast<uint8_t>(revision >> (i * 8)); bytes[i + 4] = static_cast<uint8_t>(now >> (i * 8)); }
    for (size_t n = 0; n < count; ++n)
        if (os_mbuf* buffer = ble_hs_mbuf_from_flat(bytes, sizeof(bytes)))
            ble_gatts_notify_custom(targets[n], peripheral->valueHandle, buffer);
}

void synchronize() {
    if (!active) return;
    int status = ble_hs_util_ensure_addr(0);
    if (status == 0) status = ble_hs_id_infer_auto(0, &active->addressType);
    if (status != 0) { ESP_LOGE(kBleTag, "address synchronization: %d", status); return; }
    // Reinitialize the RAM-only changed range on every boot, including removal of BAS from an existing bond's database.
    ble_svc_gatt_changed(1, 0xffff);
    recordBondStorage(*active);
    advertise();
}

void hostTask(void*) { nimble_port_run(); nimble_port_freertos_deinit(); }
}  // namespace

bool bleControlAvailable() MM_NONBLOCKING { return true; }

static bool reapAbandonedHost() {
    if (!active || !active->abandoned) return true;
    BleControlHandle abandoned{active};
    return bleControlStop(abandoned);
}

bool bleControlStart(BleControlHandle& handle, const char* name, const char* identity) {
    return bleControlStart(handle, name, identity, BleBatteryConfig{});
}

bool bleControlStart(BleControlHandle& handle, const char* name, const char* identity, const BleBatteryConfig& batteries) {
    if (!batteries.valid()) return false;
    if (!bleControlStop(handle)) return false;
    // An earlier service may have died after a failed stop. It handed this
    // context to the platform; only a confirmed stop makes replacement safe.
    if (!reapAbandonedHost()) return false;
    if (active || !validIdentity(name, identity)) return false;
    auto* peripheral = new (std::nothrow) Peripheral();
    if (!peripheral) return false;
    std::snprintf(peripheral->identity, sizeof(peripheral->identity), "%s", identity);
    const esp_err_t initialized = nimble_port_init();
    if (initialized != ESP_OK) { ESP_LOGE(kBleTag, "host initialization: %s", esp_err_to_name(initialized)); delete peripheral; return false; }
    esp_fill_random(peripheral->epoch.data(), peripheral->epoch.size());
    active = peripheral;
    ble_hs_cfg.sync_cb = synchronize;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    // The ordinary lighting service is open to nearby controllers. It does
    // not request persistent keys or require pairing for its GATT operations.
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    auto& state = peripheral->characteristics[0];
    state.uuid = &stateUuid.u; state.access_cb = accessValue; state.arg = peripheral;
    state.flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY;
    state.val_handle = &peripheral->valueHandle;
    auto& command = peripheral->characteristics[1];
    command.uuid = &commandUuid.u; command.access_cb = accessValue; command.arg = peripheral;
    command.flags = BLE_GATT_CHR_F_WRITE;
    auto& info = peripheral->characteristics[2];
    info.uuid = &identityUuid.u; info.access_cb = accessValue; info.arg = peripheral;
    info.flags = BLE_GATT_CHR_F_READ;
    peripheral->services[0].type = BLE_GATT_SVC_TYPE_PRIMARY;
    peripheral->services[0].uuid = &serviceUuid.u;
    peripheral->services[0].characteristics = peripheral->characteristics;
    if (!configureBatteries(*peripheral, batteries)) {
        active = nullptr; nimble_port_deinit(); delete peripheral; return false;
    }
    auto* services = peripheral->batteries ? peripheral->batteries->services : peripheral->services;
    if (ble_gatts_count_cfg(services) != 0 || ble_gatts_add_svcs(services) != 0 ||
        ble_svc_gap_device_name_set(name) != 0) {
        active = nullptr; nimble_port_deinit(); delete peripheral; return false;
    }
    ble_store_config_init();
    ble_hs_cfg.store_status_cb = storeStatus;
    ble_hs_cfg.store_status_arg = peripheral;
    ble_npl_event_init(&peripheral->changed, notifyChanged, peripheral);
    ble_npl_event_init(&peripheral->advertisementChanged, advertisementChanged, peripheral);
    handle.impl = peripheral;
    nimble_port_freertos_init(hostTask);
    return true;
}

bool bleControlEpoch(const BleControlHandle& handle, std::array<uint8_t, 16>& epoch) {
    const auto* peripheral = static_cast<const Peripheral*>(handle.impl);
    if (!peripheral) return false;
    epoch = peripheral->epoch;
    return true;
}

bool bleControlStop(BleControlHandle& handle) {
    auto* peripheral = static_cast<Peripheral*>(handle.impl);
    if (!peripheral) return true;
    // nimble_port_stop synchronizes with the host loop before its callback storage is freed.
    const int status = nimble_port_stop();
    if (status != 0) { ESP_LOGE(kBleTag, "host stop failed: %d", status); return false; }
    if (ble_hs_cfg.store_status_cb == storeStatus && ble_hs_cfg.store_status_arg == peripheral) {
        ble_hs_cfg.store_status_cb = nullptr;
        ble_hs_cfg.store_status_arg = nullptr;
    }
    nimble_port_deinit();
    active = nullptr;
    delete peripheral;
    handle.impl = nullptr;
    return true;
}

void bleControlAbandon(BleControlHandle& handle) {
    auto* peripheral = static_cast<Peripheral*>(handle.impl);
    if (!peripheral) return;
    if (peripheral != active) { ESP_LOGE(kBleTag, "cannot retain unknown BLE host"); return; }
    peripheral->abandoned = true;
    handle.impl = nullptr;
    ESP_LOGE(kBleTag, "BLE host retained after service destruction");
}

size_t bleControlReceive(BleControlHandle& handle, char* out, size_t capacity, uint64_t& generation) MM_NONBLOCKING {
    auto* peripheral = static_cast<Peripheral*>(handle.impl);
    if (!peripheral) return 0;
    std::unique_lock<std::mutex> lock(peripheral->mutex, std::try_to_lock);
    return lock.owns_lock() ? peripheral->peers.take(out, capacity, generation) : 0;
}

bool bleControlOwners(BleControlHandle& handle, uint64_t* out, size_t capacity, size_t& count) MM_NONBLOCKING {
    auto* peripheral = static_cast<Peripheral*>(handle.impl);
    if (!peripheral || !out || capacity < kPeerCapacity) return false;
    std::unique_lock<std::mutex> lock(peripheral->mutex, std::try_to_lock);
    if (!lock.owns_lock()) return false;
    count = peripheral->peers.generations(out, capacity);
    return true;
}

bool bleControlPublish(BleControlHandle& handle, const char* response, uint32_t revision, uint64_t generation) MM_NONBLOCKING {
    auto* peripheral = static_cast<Peripheral*>(handle.impl);
    if (!peripheral || !response || std::strlen(response) > 512) return false;
    std::unique_lock<std::mutex> lock(peripheral->mutex, std::try_to_lock);
    if (!lock.owns_lock()) return false;
    // A stale result is consumed, not retried against a later connection.
    const auto result = peripheral->peers.publish(response, generation);
    if (result == BlePeerTable<kPeerCapacity>::Publish::Stale) return true;
    if (result != BlePeerTable<kPeerCapacity>::Publish::Stored) return false;
    peripheral->revision = revision;
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &peripheral->changed);
    return true;
}

bool bleControlPublishAdvertisement(BleControlHandle& handle, const std::array<uint8_t, 13>& bytes) MM_NONBLOCKING {
    auto* peripheral = static_cast<Peripheral*>(handle.impl);
    if (!peripheral || bytes[0] != 1 || (bytes[1] & 0x80)) return false;
    std::unique_lock<std::mutex> lock(peripheral->mutex, std::try_to_lock);
    if (!lock.owns_lock()) return false;
    peripheral->advertisement = bytes;
    // Requeue even equal pending bytes: a previous host update may have been rate-limited.
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &peripheral->advertisementChanged);
    return true;
}

bool bleControlConnected(const BleControlHandle& handle) MM_NONBLOCKING {
    const auto* peripheral = static_cast<const Peripheral*>(handle.impl);
    return peripheral && peripheral->connection.load() != BLE_HS_CONN_HANDLE_NONE;
}

bool bleControlPairingState(const BleControlHandle& handle, BlePairingState& state) MM_NONBLOCKING {
    const auto* peripheral = static_cast<const Peripheral*>(handle.impl);
    if (!peripheral) { state = {}; return false; }
    state = {peripheral->repeatRequests.load(), peripheral->repeatDeleted.load(), peripheral->repeatRejected.load(),
             peripheral->repeatPairing.remaining(pairingTimeMs()), peripheral->fullAllowed.load(),
             peripheral->fullRejected.load(), peripheral->overflows.load(), peripheral->fullUnknown.load()};
    return true;
}

bool bleControlLinkState(const BleControlHandle& handle, BleLinkState& state) MM_NONBLOCKING {
    const auto* peripheral = static_cast<const Peripheral*>(handle.impl);
    if (!peripheral) { state = {}; return false; }
    state = {peripheral->linkAccepted.load(std::memory_order_relaxed),
             peripheral->linkFailed.load(std::memory_order_relaxed),
             peripheral->linkDisconnected.load(std::memory_order_relaxed),
             peripheral->lastDisconnectReason.load(std::memory_order_relaxed),
             peripheral->lastUpdateStatus.load(std::memory_order_relaxed),
             peripheral->missingPeerReads.load(std::memory_order_relaxed),
             peripheral->unsupportedAccess.load(std::memory_order_relaxed),
             peripheral->connectionCount.load(std::memory_order_relaxed)};
    return true;
}

bool bleControlAllowRepair(BleControlHandle& handle) MM_NONBLOCKING {
    auto* peripheral = static_cast<Peripheral*>(handle.impl);
    if (!peripheral) return false;
    const uint16_t observed = peripheral->connection.load();
    std::unique_lock<std::mutex> lock(peripheral->mutex, std::try_to_lock);
    if (!lock.owns_lock() || !BleRepeatPairing::mayArm(observed, peripheral->connection.load(), peripheral->connectionCount.load())) return false;
    peripheral->repeatPairing.arm(pairingTimeMs());
    return true;
}

bool bleControlPublishBattery(BleControlHandle& handle, const BleBatterySnapshot& batteries) MM_NONBLOCKING {
    auto* peripheral = static_cast<Peripheral*>(handle.impl);
    if (!peripheral) return false;
    if (!peripheral->batteries) return true;
    std::unique_lock<std::mutex> lock(peripheral->mutex, std::try_to_lock);
    if (!lock.owns_lock()) return false;
    if (!peripheral->batteries->updates.publish(batteries, millis())) return true;
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &peripheral->batteries->changed);
    return true;
}
}  // namespace mm::platform
#else
#include "platform/ble_control_stub.inc"
#endif
