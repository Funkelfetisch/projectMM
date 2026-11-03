// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/util/FirmwareAuthorizedBackend.h"
#include "core/util/FirmwareReleasePolicy.h"
#include "core/util/FirmwareSlot.h"
#include "core/util/FirmwareUploadSession.h"
#include "core/util/hex.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace mm {
/// One firmware transfer over BLE: a release the publisher signed, the inactive slot, and an ordered upload bound to the connection that began it; every refusal is the `ota-*` name the reply carries.
class BleFirmwareUpdate {
public:
    /// A transfer that stores nothing for this long is abandoned.
    static constexpr uint32_t kIdleTimeoutMs = 30000;

    /// Why an operation refused, each the wire's `ota-*` name.
    enum class Error : uint8_t { None, Busy, Unauthorized, Slot, Io, Owner, Offset, Invalid, Incomplete, Integrity };
    /// The wire name of `error`; null for `None`.
    static const char* errorName(Error error) {
        static constexpr const char* kNames[] = {nullptr, "ota-busy", "ota-unauthorized", "ota-slot", "ota-io",
                                                 "ota-owner", "ota-offset", "ota-invalid", "ota-incomplete", "ota-integrity"};
        return kNames[static_cast<uint8_t>(error)];
    }

    /// What the running firmware trusts: the publisher's key, its own target, and the lowest security version it accepts.
    struct Release {
        /// The publisher's P-256 public key, which outlives the update.
        const std::array<uint8_t, 65>* publisherKey;
        /// The hash of this firmware's variant name, which a release must name.
        uint32_t target;
        /// The lowest security version a release may carry.
        uint32_t minimumVersion;
    };

    /// An update over `slot`, which outlives it, trusting `release`.
    BleFirmwareUpdate(FirmwareSlot& slot, const Release& release)
        : slot_(slot), authorization_{release, {}}, backend_(slot, authorization_), session_(backend_) {}
    /// The members refer to one another, so a transfer is neither copied nor assigned.
    BleFirmwareUpdate(const BleFirmwareUpdate&) = delete;
    BleFirmwareUpdate& operator=(const BleFirmwareUpdate&) = delete;

    /// Authorize the release, then open the slot for `owner`: it opens only once target, size, security version and signature check out; `total` is the image size on success.
    Error begin(uint64_t owner, std::string_view claimsHex, std::string_view signatureHex, uint32_t now, uint32_t& total) {
        if (session_.active() || slot_.busy()) return Error::Busy;
        // A finalization that failed left the adapter holding the writer; the platform has since released it, and this cleanup confirms so.
        if (backend_.active() && !backend_.abort()) return Error::Io;
        FirmwareReleaseClaims claims;
        if (!decode(claimsHex, signatureHex, claims)) return Error::Unauthorized;
        const size_t capacity = slot_.capacity();
        if (!capacity || claims.imageSize > capacity) return Error::Slot;
        authorization_.claims = claims;
        const Error error = open(owner, claims);
        if (error != Error::None) return error;
        session_.noteProgress(owner, now);
        owner_ = owner;
        total = claims.imageSize;
        return Error::None;
    }

    /// Store `length` streamed image bytes at `offset` for `owner`; `received` is the byte count held afterwards, whatever the result.
    Error append(uint64_t owner, uint32_t offset, const uint8_t* bytes, size_t length, uint32_t now, uint32_t& received) {
        const Result result = session_.append(owner, offset, bytes, length);
        if (result == Result::Ok) session_.noteProgress(owner, now);
        received = static_cast<uint32_t>(session_.received());
        return map(result);
    }

    /// Verify the transfer digest the signed claims carry, then commit the image.
    Error end(uint64_t owner) { return map(session_.finish(owner)); }
    /// Stop `owner`'s transfer and release the slot.
    Error abort(uint64_t owner) { return map(session_.abort(owner)); }
    /// Abandon a transfer that has made no progress; true when it did.
    bool expireIdle(uint32_t now) { return session_.expireIdle(now, kIdleTimeoutMs); }

    /// Whether a transfer is open.
    bool active() const { return session_.active(); }
    /// The image bytes the open transfer holds.
    uint32_t received() const { return static_cast<uint32_t>(session_.received()); }
    /// The connection that began the running transfer; meaningful while `active()`.
    uint64_t owner() const { return owner_; }

    /// `owner`'s phase as the wire names it, with the byte counts it has in hand: `idle` when it has no transfer.
    const char* phase(uint64_t owner, uint32_t& received, uint32_t& total) const {
        Session::Status status{};
        received = total = 0;
        if (!session_.status(owner, status)) return "idle";
        received = static_cast<uint32_t>(status.received);
        total = static_cast<uint32_t>(status.total);
        switch (status.phase) {
            case Session::Phase::Uploading: return "uploading";
            case Session::Phase::Committed: return "committed";
            case Session::Phase::Aborted: return "aborted";
            default: return "failed";
        }
    }

private:
    /// The domain tag, three 32-bit fields and the image digest the publisher signed.
    static constexpr size_t kClaimsBytes = 16 + 12 + 32;
    static constexpr size_t kSignatureBytes = 64;

    using Digest = std::array<uint8_t, 32>;
    /// Presents the decoded claims of the release under consideration to FirmwareAuthorizedBackend.
    struct Authorization {
        Release release;
        FirmwareReleaseClaims claims;
        bool verify(size_t size, const Digest& digest) const {
            return FirmwareReleasePolicy(claims, release.target, release.minimumVersion, *release.publisherKey).verify(size, digest);
        }
    };
    using Backend = FirmwareAuthorizedBackend<FirmwareSlot, Authorization>;
    using Session = FirmwareUploadSession<Backend>;
    using Result = Session::Result;

    /// The claims and signature as the publisher's tool prints them, hex for hex; a release of zero bytes is no release.
    static bool decode(std::string_view claimsHex, std::string_view signatureHex, FirmwareReleaseClaims& claims) {
        uint8_t claimBytes[kClaimsBytes], signature[kSignatureBytes];
        return fromHex(claimsHex, claimBytes) && fromHex(signatureHex, signature) &&
               decodeFirmwareReleaseClaims(claimBytes, sizeof(claimBytes), signature, sizeof(signature), claims) && claims.imageSize != 0;
    }
    /// Start the upload session, which verifies the release before it opens the slot. Owner, size and capacity are valid by now, so only the release can have refused.
    Error open(uint64_t owner, const FirmwareReleaseClaims& claims) {
        switch (session_.begin(owner, claims.imageSize, claims.imageDigest)) {
            case Result::Ok: return Error::None;
            case Result::Invalid: return Error::Unauthorized;
            case Result::Busy: return Error::Busy;
            default: return slot_.busy() ? Error::Busy : Error::Io;
        }
    }
    template <size_t Bytes>
    static bool fromHex(std::string_view text, uint8_t (&out)[Bytes]) {
        if (text.size() != 2 * Bytes) return false;
        for (size_t i = 0; i < Bytes; ++i) {
            const int high = hexDigit(text[2 * i]), low = hexDigit(text[2 * i + 1]);
            if (high < 0 || low < 0) return false;
            out[i] = static_cast<uint8_t>(high << 4 | low);
        }
        return true;
    }
    static Error map(Result result) {
        switch (result) {
            case Result::Ok: return Error::None;
            case Result::Owner: return Error::Owner;
            case Result::Offset: return Error::Offset;
            case Result::Invalid: return Error::Invalid;
            case Result::Incomplete: return Error::Incomplete;
            case Result::Integrity: return Error::Integrity;
            case Result::Busy: return Error::Busy;
            default: return Error::Io;
        }
    }

    /// Each member refers to the ones before it, so this order is also the reverse of their teardown.
    FirmwareSlot& slot_;
    Authorization authorization_;
    Backend backend_;
    Session session_;
    uint64_t owner_ = 0;
};
} // namespace mm
