#pragma once

#include "host/ble_hs.h"

/// Minimal recording seam for the NimBLE GAP API: the connection descriptor and repeat-pairing records the code under test reads, and the two calls it makes.
#define BLE_GAP_REPEAT_PAIRING_RETRY 1
#define BLE_GAP_REPEAT_PAIRING_IGNORE 2

struct ble_gap_sec_state {
    unsigned encrypted : 1;       ///< The link is encrypted.
    unsigned authenticated : 1;   ///< The link key was authenticated, so it resists a man in the middle.
    unsigned bonded : 1;          ///< The peers share stored keys.
    unsigned key_size : 5;        ///< The encryption key size, in bytes.
    unsigned authorize : 1;       ///< The peer is authorized for services that demand it.
};

struct ble_gap_conn_desc {
    ble_gap_sec_state sec_state;   ///< The link's security state.
    ble_addr_t our_id_addr;        ///< Our identity address.
    ble_addr_t peer_id_addr;       ///< The peer's identity address.
    ble_addr_t our_ota_addr;       ///< The address we use over the air, which may be a private one.
    ble_addr_t peer_ota_addr;      ///< The address the peer uses over the air.
    uint16_t conn_handle;          ///< The connection's handle.
    uint16_t conn_itvl;            ///< The connection interval, in 1.25 ms units.
    uint16_t conn_latency;         ///< The peripheral latency, in connection events.
    uint16_t supervision_timeout;  ///< The supervision timeout, in 10 ms units.
    uint8_t role;                  ///< Our role in the connection, central or peripheral.
    uint8_t master_clock_accuracy; ///< The central's sleep clock accuracy.
};

struct ble_gap_repeat_pairing {
    uint16_t conn_handle;             ///< The connection the peer pairs again on.
    uint8_t cur_key_size;             ///< The stored bond's key size, in bytes.
    uint8_t cur_authenticated : 1;    ///< The stored bond was authenticated.
    uint8_t cur_sc : 1;               ///< The stored bond used LE Secure Connections.
    uint8_t new_key_size;             ///< The new pairing's key size, in bytes.
    uint8_t new_authenticated : 1;    ///< The new pairing is authenticated.
    uint8_t new_sc : 1;               ///< The new pairing uses LE Secure Connections.
    uint8_t new_bonding : 1;          ///< The new pairing asks to bond.
};

extern "C" int ble_gap_conn_find(uint16_t handle, ble_gap_conn_desc* out_desc);
extern "C" int ble_gap_terminate(uint16_t conn_handle, uint8_t hci_reason);
