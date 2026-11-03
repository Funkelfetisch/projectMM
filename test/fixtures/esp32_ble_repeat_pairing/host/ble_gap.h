#pragma once

#include "host/ble_hs.h"

#define BLE_GAP_REPEAT_PAIRING_RETRY 1
#define BLE_GAP_REPEAT_PAIRING_IGNORE 2

struct ble_gap_sec_state {
    unsigned encrypted : 1;
    unsigned authenticated : 1;
    unsigned bonded : 1;
    unsigned key_size : 5;
    unsigned authorize : 1;
};

struct ble_gap_conn_desc {
    ble_gap_sec_state sec_state;
    ble_addr_t our_id_addr;
    ble_addr_t peer_id_addr;
    ble_addr_t our_ota_addr;
    ble_addr_t peer_ota_addr;
    uint16_t conn_handle;
    uint16_t conn_itvl;
    uint16_t conn_latency;
    uint16_t supervision_timeout;
    uint8_t role;
    uint8_t master_clock_accuracy;
};

struct ble_gap_repeat_pairing {
    uint16_t conn_handle;
    uint8_t cur_key_size;
    uint8_t cur_authenticated : 1;
    uint8_t cur_sc : 1;
    uint8_t new_key_size;
    uint8_t new_authenticated : 1;
    uint8_t new_sc : 1;
    uint8_t new_bonding : 1;
};

extern "C" int ble_gap_conn_find(uint16_t handle, ble_gap_conn_desc* out_desc);
extern "C" int ble_gap_terminate(uint16_t conn_handle, uint8_t hci_reason);
