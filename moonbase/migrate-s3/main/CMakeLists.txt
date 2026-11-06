idf_component_register(
    SRCS "migrate_s3_main.cpp" "../../../src/core/util/sha256.cpp" "../../../src/core/util/FirmwareSignature.cpp"
    INCLUDE_DIRS "../../../src"
    EMBED_FILES "${TABLE_BIN}"
    REQUIRES nvs_flash esp_netif esp_event esp_wifi esp_http_server esp_adc esp_driver_gpio
             app_update esp_app_format esp_partition spi_flash mbedtls espressif__cjson espressif__mdns joltwallet__littlefs)
idf_component_get_property(littlefs_dir joltwallet__littlefs COMPONENT_DIR)
target_include_directories(${COMPONENT_LIB} PRIVATE "${littlefs_dir}/src/littlefs")
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_20)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
# One image per product: the variant decides which release it installs and whether it holds the battery controller's power.
if(NOT MM_MIGRATE_VARIANT MATCHES "^(nebulite-s3|helio1)$")
    message(FATAL_ERROR "MM_MIGRATE_VARIANT must be nebulite-s3 or helio1, not '${MM_MIGRATE_VARIANT}'")
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE MM_MIGRATE_VARIANT="${MM_MIGRATE_VARIANT}")
