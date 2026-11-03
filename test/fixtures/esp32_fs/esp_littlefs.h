#pragma once

/// Test double for the ESP-IDF LittleFS registration API: registering always succeeds and the usage query always fails.

#include <cstddef>

using esp_err_t = int;
inline constexpr esp_err_t ESP_OK = 0;
inline constexpr esp_err_t ESP_FAIL = -1;
struct esp_vfs_littlefs_conf_t {
    /// Where the filesystem mounts in the VFS.
    const char* base_path;
    /// The flash partition that holds it.
    const char* partition_label;
    /// Whether a failed mount formats the partition.
    bool format_if_mount_failed;
    /// Whether registration leaves the partition unmounted.
    bool dont_mount;
};
inline esp_err_t esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t*) { return ESP_OK; }
inline esp_err_t esp_vfs_littlefs_unregister(const char*) { return ESP_OK; }
inline esp_err_t esp_littlefs_info(const char*, size_t*, size_t*) { return ESP_FAIL; }
