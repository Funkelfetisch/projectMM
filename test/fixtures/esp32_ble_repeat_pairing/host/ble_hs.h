#pragma once

#include <cstdint>

/// Minimal recording seam for the ESP-IDF v6.1 NimBLE host API.
#define BLE_HS_CONN_HANDLE_NONE 0xffff
#define BLE_ERR_REM_USER_CONN_TERM 0x13
#define BLE_HS_ENOENT 5
#define BLE_HS_ESTORE_CAP 27
#define BLE_ADDR_PUBLIC 0
#define BLE_ADDR_RANDOM 1

struct ble_addr_t {
    uint8_t type;     ///< Public or random, as BLE_ADDR_PUBLIC and BLE_ADDR_RANDOM name them.
    uint8_t val[6];   ///< The six address bytes.
};
