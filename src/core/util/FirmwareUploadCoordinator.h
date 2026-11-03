#pragma once

#include "core/util/FirmwareUploadProtocol.h"

namespace mm {
/// Transport-independent upload lifecycle. The caller serializes calls, supplies
/// unique connection generations and invokes tick even without incoming frames.
/// This does not provide an endpoint, authenticate the peer or verify signatures.
template <typename Backend>
class FirmwareUploadCoordinator {
public:
    using Session = FirmwareUploadSession<Backend>;
    using Result = typename Session::Result;
    using Status = typename Session::Status;
    struct Outcome { Result result; bool hasStatus; Status status; };

    FirmwareUploadCoordinator(Backend& backend, uint32_t idleTimeoutMs, uint32_t resumeTimeoutMs)
        : session_(backend), idleTimeoutMs_(idleTimeoutMs), resumeTimeoutMs_(resumeTimeoutMs) {}

    Result handle(uint64_t owner, const uint8_t* bytes, size_t size, uint32_t now, Status* status = nullptr) {
        tick(now);
        return dispatchFirmwareUpload(session_, owner, bytes, size, now, status);
    }
    /// A transport can acknowledge an operation and include its current or
    /// terminal status without inferring a commit from a successful write.
    /// Token status requests may refer to a prior connection generation.
    Outcome handleAndReport(uint64_t owner, const uint8_t* bytes, size_t size, uint32_t now) {
        Status status{};
        const Result result = handle(owner, bytes, size, now, &status);
        FirmwareUploadFrame frame;
        const bool tokenStatus = result == Result::Ok && FirmwareUploadFrame::parse(bytes, size, frame) &&
                                 frame.kind == FirmwareUploadFrame::Kind::Status && size == 18;
        const bool hasStatus = tokenStatus || session_.status(owner, status);
        return {result, hasStatus, status};
    }
    Result disconnected(uint64_t owner, uint32_t now) {
        tick(now);
        return session_.disconnected(owner, now, resumeTimeoutMs_);
    }
    bool tick(uint32_t now) {
        return session_.expire(now) || session_.expireIdle(now, idleTimeoutMs_);
    }
    bool active() const { return session_.active(); }

private:
    Session session_;
    uint32_t idleTimeoutMs_;
    uint32_t resumeTimeoutMs_;
};
} // namespace mm
