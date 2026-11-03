/// ESP-IDF places this descriptor immediately after esp_app_desc_t.
/// It identifies the build variant without changing the IDF project name.
#ifndef MM_FIRMWARE_NAME
#define MM_FIRMWARE_NAME "unknown"
#endif
static_assert(sizeof(MM_FIRMWARE_NAME) <= 32, "firmware variant name too long");
struct FirmwareVariantDescriptor { char magic[8]; char name[32]; };
static_assert(sizeof(FirmwareVariantDescriptor) == 40);
extern "C" __attribute__((used, section(".rodata_custom_desc")))
const FirmwareVariantDescriptor mm_firmware_variant_descriptor = {"MMVAR01", MM_FIRMWARE_NAME};
