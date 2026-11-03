#pragma once

#include "core/util/FirmwareUploadAssembly.h"
#include "core/util/FirmwareUploadCoordinator.h"

namespace mm {
/// Combines bounded reassembly and upload lifecycle without defining a wire format, authorizing a sender, or exposing a transport endpoint; the caller serializes all calls and supplies a unique connection generation.
template <typename Backend>
class FirmwareUploadFragmentBridge {
public:
    /// What the reassembly reports for one fragment.
    using AssemblyResult = FirmwareUploadAssembly::Result;
    /// The upload lifecycle the bridge feeds a complete message to.
    using Coordinator = FirmwareUploadCoordinator<Backend>;
    /// The coordinator's result with the status a transport sends back beside it.
    using Outcome = typename Coordinator::Outcome;
    struct FragmentResult { AssemblyResult assembly; bool dispatched; Outcome outcome; };

    /// Bind the backend and set the fragment, idle and resume timeouts, in milliseconds.
    FirmwareUploadFragmentBridge(Backend& backend, uint32_t fragmentTimeoutMs,
                                 uint32_t idleTimeoutMs, uint32_t resumeTimeoutMs)
        : assembly_(fragmentTimeoutMs), coordinator_(backend, idleTimeoutMs, resumeTimeoutMs) {}

    /// Reassemble one fragment of `owner`'s message and, once it is whole, hand it to the coordinator; `dispatched` says whether that happened.
    FragmentResult append(uint64_t owner, size_t total, size_t offset,
                          const uint8_t* bytes, size_t size, uint32_t now) {
        tick(now);
        const auto result = assembly_.append(owner, total, offset, bytes, size, now);
        if (result != AssemblyResult::Ready) return {result, false, {}};
        const auto outcome = coordinator_.handleAndReport(owner, assembly_.data(), assembly_.size(), now);
        assembly_.release(owner);
        return {result, true, outcome};
    }

    /// Drop `owner`'s partial message and tell the coordinator its connection ended.
    typename Coordinator::Result disconnected(uint64_t owner, uint32_t now) {
        tick(now);
        assembly_.release(owner);
        return coordinator_.disconnected(owner, now);
    }

    /// Expire stalled reassemblies and idle uploads against `now`.
    void tick(uint32_t now) { assembly_.expire(now); coordinator_.tick(now); }
    /// Whether an upload is in flight.
    bool active() const { return coordinator_.active(); }

private:
    FirmwareUploadAssembly assembly_;
    Coordinator coordinator_;
};
} // namespace mm
