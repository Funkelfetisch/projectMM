#pragma once

using esp_partition_subtype_t = int;
inline constexpr int ESP_PARTITION_TYPE_DATA = 1;
inline constexpr int ESP_PARTITION_SUBTYPE_DATA_LITTLEFS = 2;
inline constexpr int ESP_PARTITION_SUBTYPE_DATA_SPIFFS = 3;
struct esp_partition_t {};
inline const esp_partition_t* esp_partition_find_first(int, int, const char*) {
    static esp_partition_t partition;
    return &partition;
}
