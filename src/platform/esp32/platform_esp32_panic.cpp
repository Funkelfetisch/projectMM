/// Product bench diagnostics surviving a panic reset. Only numeric exception
/// metadata and code addresses are retained; no log text or stack contents.
#include "platform/platform.h"
#include "sdkconfig.h"

#if defined(MM_PANIC_RECORD) && CONFIG_IDF_TARGET_ARCH_XTENSA
#include "esp_attr.h"
#include "esp_app_desc.h"
#include "esp_private/panic_internal.h"
#include "soc/soc.h"
#include "xtensa_context.h"
#include <cstdio>
#include <cstdint>

namespace {
constexpr uint32_t kMagic = 0x48454c33;
struct Record {
    uint32_t magic, sequence, image, core, kind, cause, fault, count, pc[8], check;
};
RTC_NOINIT_ATTR volatile Record record;
DRAM_ATTR uint32_t currentImage = 0;

// An interrupted write or old/random RTC contents must not appear as a crash.
uint32_t IRAM_ATTR checksum() {
    uint32_t value = record.sequence ^ record.image ^ record.core ^ record.kind ^ record.cause ^ record.fault ^ record.count;
    for (unsigned i = 0; i < 8; ++i) value ^= record.pc[i];
    return value ^ kMagic;
}

bool IRAM_ATTR inDram(uint32_t address, uint32_t bytes) {
    return address >= SOC_DRAM_LOW && address < SOC_DRAM_HIGH && bytes <= SOC_DRAM_HIGH - address;
}
}

extern "C" void __real_esp_panic_handler(panic_info_t* info) __attribute__((noreturn));
extern "C" void __wrap_esp_panic_handler(panic_info_t* info) __attribute__((noreturn));
extern "C" void IRAM_ATTR __wrap_esp_panic_handler(panic_info_t* info) {
    const uint32_t sequence = record.magic == kMagic && record.check == checksum() ? record.sequence + 1 : 1;
    record.magic = 0;
    record.sequence = sequence;
    record.image = currentImage;
    record.core = static_cast<uint32_t>(info->core);
    record.kind = static_cast<uint32_t>(info->exception);
    record.cause = 0;
    record.fault = 0;
    record.count = 0;
    for (unsigned i = 0; i < 8; ++i) record.pc[i] = 0;
    record.pc[0] = reinterpret_cast<uintptr_t>(info->addr);
    record.count = 1;
    const uint32_t frameAddress = reinterpret_cast<uintptr_t>(info->frame);
    if (!(frameAddress & 3u) && inDram(frameAddress, sizeof(XtExcFrame))) {
        const auto* frame = static_cast<const XtExcFrame*>(info->frame);
        record.cause = frame->exccause;
        record.fault = frame->excvaddr;
        uint32_t sp = frame->a1;
        uint32_t next = frame->a0;
        // Xtensa window ABI, as ESP-IDF's esp_backtrace_get_next_frame: the
        // previous return PC/SP live 16/12 bytes below SP. Bound every access.
        while (record.count < 8 && next && !(sp & 15u) && sp >= 16 && inDram(sp - 16, 16)) {
            const uint32_t pc = (next & 0x3fffffffu) | 0x40000000u;
            if (pc >= 0x44000000u) break;
            record.pc[record.count] = pc;
            record.count = record.count + 1;
            const auto* words = reinterpret_cast<const volatile uint32_t*>(sp - 16);
            next = words[0];
            const uint32_t previousSp = words[1];
            if (previousSp <= sp || previousSp - sp > 65536) break;
            sp = previousSp;
        }
    }
    record.check = checksum();
    record.magic = kMagic;   // publish last, then retain the SDK's normal panic/reboot path
    __real_esp_panic_handler(info);
}

namespace mm::platform {
const char* lastPanicSummary() {
    const auto* app = esp_app_get_description();
    currentImage = static_cast<uint32_t>(app->app_elf_sha256[0]) |
        static_cast<uint32_t>(app->app_elf_sha256[1]) << 8 |
        static_cast<uint32_t>(app->app_elf_sha256[2]) << 16 |
        static_cast<uint32_t>(app->app_elf_sha256[3]) << 24;
    static char text[224];
    text[0] = 0;
    if (record.magic != kMagic || record.count > 8 || record.check != checksum()) return text;
    int used = std::snprintf(text, sizeof(text), "n=%u image=%08x core=%u kind=%u cause=%u fault=%08x pc=",
        static_cast<unsigned>(record.sequence), static_cast<unsigned>(record.image), static_cast<unsigned>(record.core), static_cast<unsigned>(record.kind),
        static_cast<unsigned>(record.cause), static_cast<unsigned>(record.fault));
    for (unsigned i = 0; i < record.count && used > 0 && static_cast<size_t>(used) < sizeof(text); ++i)
        used += std::snprintf(text + used, sizeof(text) - used, "%s%08x", i ? "," : "", static_cast<unsigned>(record.pc[i]));
    return text;
}
}
#else
namespace mm::platform {
const char* lastPanicSummary() { return ""; }
}
#endif
