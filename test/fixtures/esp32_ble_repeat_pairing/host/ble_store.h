#pragma once

#include "host/ble_hs.h"

/// Recording seam for the NimBLE bond store: the types and calls the repair guard reads and deletes through.
#define BLE_STORE_OBJ_TYPE_OUR_SEC 1
#define BLE_STORE_OBJ_TYPE_PEER_SEC 2
#define BLE_STORE_OBJ_TYPE_CCCD 3
#define BLE_STORE_EVENT_OVERFLOW 1
#define BLE_STORE_EVENT_FULL 2

struct ble_store_key_sec {
    /// The peer whose bond the key names.
    ble_addr_t peer_addr;
    /// The bond's index among that peer's bonds.
    uint8_t idx;
};

/// Only the fields read by the repair guard are needed at this recording seam.
struct ble_store_value_sec {
    /// The peer the stored bond belongs to.
    ble_addr_t peer_addr;
    /// The stored key's size in bytes.
    uint8_t key_size;
};

union ble_store_value;
struct ble_store_status_event {
    /// Which store event fired, an overflow or a full store.
    int event_code;
    union {
        struct {
            int obj_type;
            const ble_store_value* value;
        } overflow;
        struct {
            int obj_type;
            uint16_t conn_handle;
        } full;
    };
};

extern "C" int ble_store_read_peer_sec(const ble_store_key_sec* key_sec, ble_store_value_sec* value_sec);
extern "C" int ble_store_util_delete_peer(const ble_addr_t* peer_id_addr);
