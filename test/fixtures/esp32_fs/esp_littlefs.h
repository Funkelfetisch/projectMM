#pragma once

#include <cstddef>

using esp_err_t = int;
inline constexpr esp_err_t ESP_OK = 0;
inline constexpr esp_err_t ESP_FAIL = -1;
struct esp_vfs_littlefs_conf_t {
    const char* base_path;
    const char* partition_label;
    bool format_if_mount_failed;
    bool dont_mount;
};
inline esp_err_t esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t*) { return ESP_OK; }
inline esp_err_t esp_vfs_littlefs_unregister(const char*) { return ESP_OK; }
inline esp_err_t esp_littlefs_info(const char*, size_t*, size_t*) { return ESP_FAIL; }
