#pragma once

#include "core/util/FirmwareUploadFrame.h"
#include "core/util/FirmwareUploadSession.h"

namespace mm {
/// Dispatches one experimental frame using the connection generation supplied by the transport.
template <typename Backend>
typename FirmwareUploadSession<Backend>::Result dispatchFirmwareUpload(
        FirmwareUploadSession<Backend>& session, uint64_t owner, const uint8_t* bytes, size_t size, uint32_t now,
        typename FirmwareUploadSession<Backend>::Status* statusOut = nullptr) {
    using Result = typename FirmwareUploadSession<Backend>::Result;
    if (!owner) return Result::Owner;
    FirmwareUploadFrame frame;
    if (!FirmwareUploadFrame::parse(bytes, size, frame)) return Result::Invalid;
    switch (frame.kind) {
        case FirmwareUploadFrame::Kind::Begin: {
            const auto result = session.begin(owner, frame.value, frame.digest, frame.token);
            if (result == Result::Ok) session.noteProgress(owner, now);
            return result;
        }
        case FirmwareUploadFrame::Kind::Chunk: {
            const auto result = session.append(owner, frame.value, frame.data, frame.length);
            if (result == Result::Ok) session.noteProgress(owner, now);
            return result;
        }
        case FirmwareUploadFrame::Kind::Finish: return session.finish(owner);
        case FirmwareUploadFrame::Kind::Abort: return session.abort(owner);
        case FirmwareUploadFrame::Kind::Status: {
            typename FirmwareUploadSession<Backend>::Status status{};
            const bool found = size == 18 ? session.terminalStatus(frame.token, status) : session.status(owner, status);
            if (!found) return Result::Owner;
            if (statusOut) *statusOut = status;
            return Result::Ok;
        }
        case FirmwareUploadFrame::Kind::Resume: {
            const auto result = session.resume(owner, frame.token, now);
            if (result == Result::Ok) session.noteProgress(owner, now);
            return result;
        }
    }
    return Result::Invalid;
}
} // namespace mm
