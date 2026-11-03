#pragma once

#include "core/util/FirmwareUploadProtocol.h"

namespace mm {
/// Transport-independent upload lifecycle: the caller serializes calls, supplies unique connection generations and calls tick even without frames, and gets no endpoint, peer authentication or signature check.
template <typename Backend>
class FirmwareUploadCoordinator {
public:
    /// The session over this backend.
    using Session = FirmwareUploadSession<Backend>;
    /// The outcome of one frame or disconnect.
    using Result = typename Session::Result;
    /// The session's progress report.
    using Status = typename Session::Status;
    /// A frame's result with the status a transport can send back beside it.
    struct Outcome { Result result; bool hasStatus; Status status; };

    FirmwareUploadCoordinator(Backend& backend, uint32_t idleTimeoutMs, uint32_t resumeTimeoutMs)
        : session_(backend), idleTimeoutMs_(idleTimeoutMs), resumeTimeoutMs_(resumeTimeoutMs) {}

    /// Expire what is overdue, then pass one frame from `owner` to the session, filling `status` when given.
    Result handle(uint64_t owner, const uint8_t* bytes, size_t size, uint32_t now, Status* status = nullptr) {
        tick(now);
        return dispatchFirmwareUpload(session_, owner, bytes, size, now, status);
    }
    /// Acknowledge an operation with its current or terminal status, so a transport never infers a commit from a successful write; a token status request may name a prior connection generation.
    Outcome handleAndReport(uint64_t owner, const uint8_t* bytes, size_t size, uint32_t now) {
        Status status{};
        const Result result = handle(owner, bytes, size, now, &status);
        FirmwareUploadFrame frame;
        const bool tokenStatus = result == Result::Ok && FirmwareUploadFrame::parse(bytes, size, frame) &&
                                 frame.kind == FirmwareUploadFrame::Kind::Status && size == 18;
        const bool hasStatus = tokenStatus || session_.status(owner, status);
        return {result, hasStatus, status};
    }
    /// Note that `owner` disconnected, opening the resume window for its upload.
    Result disconnected(uint64_t owner, uint32_t now) {
        tick(now);
        return session_.disconnected(owner, now, resumeTimeoutMs_);
    }
    /// Expire a session past its resume or idle timeout; true when one expired.
    bool tick(uint32_t now) {
        return session_.expire(now) || session_.expireIdle(now, idleTimeoutMs_);
    }
    /// Whether an upload is in flight.
    bool active() const { return session_.active(); }

private:
    Session session_;
    uint32_t idleTimeoutMs_;
    uint32_t resumeTimeoutMs_;
};
} // namespace mm
