#pragma once

#include "core/util/sha256.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace mm {
/// One bounded app-image transfer, the backend owning image validation and the OTA slot. A failed begin leaves no transfer and failed cleanup may retain backend resources. The caller serializes access and keeps the backend alive through destruction.
template <typename Backend>
class FirmwareUploadSession {
public:
    /// The SHA-256 of the whole image, checked when the last byte lands.
    using Digest = std::array<uint8_t, 32>;
    /// Correlates a reconnect with its transfer; all zero is no token.
    using ResumeToken = std::array<uint8_t, 16>;
    enum class Result : uint8_t { Ok, Busy, Owner, Invalid, Offset, Incomplete, Integrity, Io };
    /// Aborted records that this session stopped, not that the platform discarded flash contents or aborted its handle.
    enum class Phase : uint8_t { Uploading, Committed, Aborted, Failed };
    struct Status {
        /// Where the transfer stands, how the last call ended, and the bytes received of the total.
        Phase phase; Result result; size_t received; size_t total;
        /// Set only after digest verification and a successful backend commit: volatile transfer evidence, not signature or boot verification.
        bool hasVerifiedDigest = false;
        Digest verifiedDigest{};
    };

    /// Binds the backend the transfer writes into.
    explicit FirmwareUploadSession(Backend& backend) : backend_(backend) {}
    /// A session has one owner, so copying is deleted.
    FirmwareUploadSession(const FirmwareUploadSession&) = delete;
    FirmwareUploadSession& operator=(const FirmwareUploadSession&) = delete;
    /// Aborts a transfer still active, which releases the inactive slot.
    ~FirmwareUploadSession() { abortActive(); }

    /// Opens a transfer of `size` bytes that must hash to `digest`: Busy while one is active, Invalid for an empty or oversized request.
    Result begin(uint64_t owner, size_t size, const Digest& digest, const ResumeToken& token = {}) {
        if (active_) return Result::Busy;
        const size_t capacity = backend_.capacity();
        if (!owner || !size || !capacity || size > capacity) return Result::Invalid;
        // An opt-in backend may require a release authorization before the inactive slot is opened; a backend without authorize() skips it.
        if constexpr (requires { backend_.authorize(size, digest); }) {
            if (!backend_.authorize(size, digest)) return Result::Invalid;
        }
        if (!backend_.begin(size)) return Result::Io;
        terminalOwner_ = 0;
        owner_ = owner; size_ = size; received_ = 0; digest_ = digest; resumeToken_ = token;
        suspended_ = false; hash_ = Sha256{}; active_ = true;
        return Result::Ok;
    }
    /// Writes the next chunk at `offset` for the owner: Offset on a gap or replay, and a failed write aborts the transfer with Io.
    Result append(uint64_t owner, size_t offset, const uint8_t* data, size_t length) {
        if (!active_ || suspended_ || !owner || owner != owner_) return Result::Owner;
        if (offset != received_) return Result::Offset;
        if (!data || !length || length > size_ - received_) return Result::Invalid;
        if (!backend_.write(data, length)) { abortActive(Phase::Failed, Result::Io); return Result::Io; }
        if (!hash_.update(data, length)) { abortActive(Phase::Failed, Result::Io); return Result::Io; }
        received_ += length;
        return Result::Ok;
    }
    /// Verifies the digest and commits the image: Incomplete before every byte arrived, Integrity on a mismatch.
    Result finish(uint64_t owner) {
        if (!active_ || suspended_ || !owner || owner != owner_) return Result::Owner;
        if (received_ != size_) return Result::Incomplete;
        Digest actual{}; hash_.digest(actual.data());
        if (actual != digest_) return abortActive(Phase::Failed, Result::Integrity) ? Result::Integrity : Result::Io;
        const bool ok = backend_.end(true);
        complete(ok ? Phase::Committed : Phase::Failed, ok ? Result::Ok : Result::Io, ok ? &actual : nullptr);
        return ok ? Result::Ok : Result::Io;
    }
    /// Aborts the owner's transfer and releases the inactive slot.
    Result abort(uint64_t owner) {
        if (!active_ || suspended_ || !owner || owner != owner_) return Result::Owner;
        return abortActive(Phase::Aborted, Result::Ok) ? Result::Ok : Result::Io;
    }
    /// Transport disconnect hook: keeps a resumable transfer only for a valid bounded interval and otherwise releases the inactive slot at once; the transport must call expire() while it is suspended.
    Result disconnected(uint64_t owner, uint32_t now, uint32_t timeoutMs) {
        if (!active_ || suspended_ || !owner || owner != owner_) return Result::Owner;
        if (!validToken(resumeToken_)) return abortActive(Phase::Aborted, Result::Ok) ? Result::Ok : Result::Io;
        if (!timeoutMs || timeoutMs > INT32_MAX) {
            return abortActive(Phase::Aborted, Result::Invalid) ? Result::Invalid : Result::Io;
        }
        return suspend(owner, now, timeoutMs);
    }
    /// Suspend only a token-bearing transfer, for a bounded interval shorter than half the timer range.
    Result suspend(uint64_t owner, uint32_t now, uint32_t timeoutMs) {
        if (!active_ || suspended_ || !owner || owner != owner_) return Result::Owner;
        if (!validToken(resumeToken_) || !timeoutMs || timeoutMs > INT32_MAX) return Result::Invalid;
        suspendedOwner_ = owner_; owner_ = 0; suspended_ = true; suspendedAt_ = now; suspendTimeout_ = timeoutMs;
        return Result::Ok;
    }
    /// The token correlates a reconnect; it does not authenticate a sender on an open radio.
    Result resume(uint64_t owner, const ResumeToken& token, uint32_t now) {
        if (!active_ || !suspended_ || !owner) return Result::Owner;
        if (expire(now)) return Result::Owner;
        if (!validToken(token) || token != resumeToken_) return Result::Owner;
        owner_ = owner; suspendedOwner_ = 0; suspended_ = false; return Result::Ok;
    }
    /// Aborts a suspended transfer whose timeout has elapsed at `now`, true when it did.
    bool expire(uint32_t now) {
        if (!active_ || !suspended_ || static_cast<uint32_t>(now - suspendedAt_) < suspendTimeout_) return false;
        abortActive(Phase::Aborted, Result::Ok); return true;
    }
    /// A transport may bound a connected transfer that stops making progress: only a successful begin, new chunk or resume calls this, so status polls and rejected or replayed frames never keep the update slot busy.
    void noteProgress(uint64_t owner, uint32_t now) {
        if (active_ && !suspended_ && owner && owner == owner_) {
            lastProgressAt_ = now;
            progressTracked_ = true;
        }
    }
    /// The owning transport must poll this even when no new frame arrives; a zero or ambiguous (half timer range or greater) timeout is disabled.
    bool expireIdle(uint32_t now, uint32_t timeoutMs) {
        if (!active_ || suspended_ || !progressTracked_ || !timeoutMs || timeoutMs > INT32_MAX ||
            static_cast<uint32_t>(now - lastProgressAt_) < timeoutMs) return false;
        abortActive(Phase::Aborted, Result::Ok); return true;
    }
    /// Whether a transfer is open, uploading or suspended.
    bool active() const { return active_; }
    /// Fills `out` with the owner's live transfer or its terminal result, false for an unknown owner.
    bool status(uint64_t owner, Status& out) const {
        if (!owner) return false;
        if (active_ && owner == owner_) { out = {Phase::Uploading, Result::Ok, received_, size_}; return true; }
        if (owner == terminalOwner_) { out = terminal_; return true; }
        return false;
    }
    /// Terminal status may be queried by correlation token until another transfer begins.
    bool terminalStatus(const ResumeToken& token, Status& out) const {
        if (!terminalOwner_ || !validToken(token) || token != terminalToken_) return false;
        out = terminal_; return true;
    }
    /// Bytes accepted so far.
    size_t received() const { return received_; }

private:
    void complete(Phase phase, Result result, const Digest* verifiedDigest = nullptr) {
        terminalOwner_ = owner_ ? owner_ : suspendedOwner_;
        terminalToken_ = resumeToken_; terminal_ = {phase, result, received_, size_};
        if (verifiedDigest) { terminal_.hasVerifiedDigest = true; terminal_.verifiedDigest = *verifiedDigest; }
        active_ = false; suspended_ = false; progressTracked_ = false; owner_ = suspendedOwner_ = 0; size_ = received_ = 0; digest_ = {}; resumeToken_ = {};
    }
    bool abortActive(Phase phase = Phase::Aborted, Result result = Result::Ok) {
        if (!active_) return true;
        // Legacy end(false) reports commit success, not abort success; a backend with an explicit abort() can report cleanup failure instead.
        bool ok = true;
        if constexpr (requires { backend_.abort(); }) ok = backend_.abort();
        else backend_.end(false);
        complete(ok ? phase : Phase::Failed, ok ? result : Result::Io);
        return ok;
    }
    static bool validToken(const ResumeToken& token) {
        for (uint8_t byte : token) if (byte) return true;
        return false;
    }
    Backend& backend_;
    Digest digest_{};
    ResumeToken resumeToken_{}, terminalToken_{};
    Sha256 hash_{};
    uint64_t owner_ = 0, suspendedOwner_ = 0;
    uint64_t terminalOwner_ = 0;
    size_t size_ = 0, received_ = 0;
    bool active_ = false;
    bool suspended_ = false;
    bool progressTracked_ = false;
    uint32_t suspendedAt_ = 0, suspendTimeout_ = 0;
    uint32_t lastProgressAt_ = 0;
    Status terminal_{Phase::Aborted, Result::Ok, 0, 0};
};
} // namespace mm
