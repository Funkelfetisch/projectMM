#pragma once

#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"

#include <atomic>
#include <cstdint>
#include <cstring>

namespace mm::platform {

/// One explicitly authorized, time-limited stale-bond replacement for the current peer.
class BleRepeatPairing {
public:
    /// Diagnostic lookup outcome, independent of whether policy admits the request.
    enum class PeerStatus : uint8_t { Unchecked, Unknown, Known };

    /// The observed primary must still match under the caller's lock; never arm with multiple peers.
    static bool mayArm(uint16_t observed, uint16_t current, uint8_t connectionCount) {
        return observed == current && connectionCount <= 1;
    }

    /// Open a three-minute, single-use repair window independently of connection resets.
    void arm(uint64_t now) {
        deadline_.store(now <= UINT64_MAX - 180000u ? now + 180000u : 0);
    }
    /// Changing the peer while another remains connected cannot transfer an authorization window.
    void cancel() { deadline_.store(0); reset(); }

    /// Milliseconds left against the boot's monotonic 64-bit clock.
    uint32_t remaining(uint64_t now) const {
        const uint64_t deadline = deadline_.load();
        return deadline && now < deadline && deadline - now <= 180000u
            ? static_cast<uint32_t>(deadline - now) : 0;
    }

    /// A new connection starts a new bounded repair attempt.
    void reset() { attempted_ = false; blocked_ = false; }

    /// A partial store failure denies protected access until the connection ends.
    bool blocked() const { return blocked_; }

    /// Admit only a known peer's full-store preflight; the repeat callback owns security checks and deletion.
    int storeStatus(const ble_store_status_event& event, uint16_t activeHandle, uint64_t now,
                    PeerStatus* peerStatus = nullptr) const {
        if (peerStatus) *peerStatus = PeerStatus::Unchecked;
        if (event.event_code != BLE_STORE_EVENT_FULL ||
            (event.full.obj_type != BLE_STORE_OBJ_TYPE_PEER_SEC && event.full.obj_type != BLE_STORE_OBJ_TYPE_OUR_SEC))
            return BLE_HS_ESTORE_CAP;

        ble_gap_conn_desc connection{};
        uint64_t deadline = 0;
        if (!repairConnection(event.full.conn_handle, activeHandle, now, connection, deadline)) return BLE_HS_ESTORE_CAP;
        ble_store_key_sec key{};
        key.peer_addr = connection.peer_id_addr;
        ble_store_value_sec bond{};
        const int result = ble_store_read_peer_sec(&key, &bond);
        if (result == BLE_HS_ENOENT) {
            if (peerStatus) *peerStatus = PeerStatus::Unknown;
            return BLE_HS_ESTORE_CAP;
        }
        if (result != 0 || bond.peer_addr.type != key.peer_addr.type ||
            std::memcmp(bond.peer_addr.val, key.peer_addr.val, sizeof(key.peer_addr.val)) != 0)
            return BLE_HS_ESTORE_CAP;
        if (peerStatus) *peerStatus = PeerStatus::Known;
        return bond.key_size >= 7 && bond.key_size <= 16 && deadline_.load() == deadline ? 0 : BLE_HS_ESTORE_CAP;
    }

    /// Delete only the connection's identity bond; retry follows a successful deletion.
    int handle(const ble_gap_repeat_pairing& request, uint16_t activeHandle, uint64_t now) {
        if (request.cur_key_size < 7 || request.cur_key_size > 16 || request.new_key_size != 16 ||
            !request.new_bonding || !request.new_sc ||
            (request.cur_authenticated && !request.new_authenticated))
            return BLE_GAP_REPEAT_PAIRING_IGNORE;

        ble_gap_conn_desc connection{};
        uint64_t deadline = 0;
        if (!repairConnection(request.conn_handle, activeHandle, now, connection, deadline))
            return BLE_GAP_REPEAT_PAIRING_IGNORE;

        // NimBLE reports the event again synchronously if a retry leaves its peer bond present.
        if (!deadline_.compare_exchange_strong(deadline, 0)) return BLE_GAP_REPEAT_PAIRING_IGNORE;
        attempted_ = true;
        if (ble_store_util_delete_peer(&connection.peer_id_addr) != 0) {
            // Store deletion spans several records; an error can leave a partly removed bond.
            blocked_ = true;
            ble_gap_terminate(activeHandle, BLE_ERR_REM_USER_CONN_TERM);
            return BLE_GAP_REPEAT_PAIRING_IGNORE;
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

private:
    bool repairConnection(uint16_t requestedHandle, uint16_t activeHandle, uint64_t now,
                          ble_gap_conn_desc& connection, uint64_t& deadline) const {
        deadline = deadline_.load();
        return deadline && now < deadline && deadline - now <= 180000u && !attempted_ && !blocked_ &&
            activeHandle != BLE_HS_CONN_HANDLE_NONE && requestedHandle == activeHandle &&
            ble_gap_conn_find(requestedHandle, &connection) == 0 && connection.conn_handle == activeHandle &&
            !connection.sec_state.encrypted &&
            (connection.peer_id_addr.type == BLE_ADDR_PUBLIC || connection.peer_id_addr.type == BLE_ADDR_RANDOM);
    }

    std::atomic<uint64_t> deadline_{0};
    bool attempted_ = false;
    bool blocked_ = false;
};

} // namespace mm::platform
