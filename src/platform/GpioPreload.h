#pragma once

namespace mm::platform {
/// Program the output latch before enabling the output driver; a failed preload must never proceed to enable. The caller owns a pin detached from peripherals.
template <typename Preload, typename Enable>
bool preloadThenEnable(Preload preload, Enable enable) {
    if (!preload()) return false;
    return enable();
}
} // namespace mm::platform
