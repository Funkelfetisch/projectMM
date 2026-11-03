#pragma once

namespace mm::platform {
/// A checked detach is retryable across partially completed SDK operations, since Ops retains its disabled, channel and encoder state when any operation fails.
template <typename Ops>
bool rmtCheckedDetachPhases(Ops& ops) {
    if (!ops.disabled()) {
        if (!ops.wait() || !ops.disable()) return false;
        ops.markDisabled();
    }
    if (!ops.removeChannel()) return false;
    return ops.removeEncoder();
}
} // namespace mm::platform
