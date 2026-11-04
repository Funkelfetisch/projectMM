#pragma once

#include "core/util/BleMessage.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace mm {

/// One epoch-scoped client session and exact request/result retry cache; callers allocate it when RPC is negotiated.
class BleRpcSession {
public:
    /// Dispatch a fresh request, replay its retained result, or reject an invalid correlation or expired ID.
    enum class Decision : uint8_t { Accept, Retry, SessionExpired, IdReused, IdExpired, Invalid };

    /// Validate a challenge before allocating or binding a peer's session slot.
    static bool validChallenge(const char* nonce) { return validToken(nonce); }

    /// Install the service's fresh 128-bit lowercase hex epoch and reset the namespace; invalid input preserves it.
    bool beginEpoch(const char* epoch) {
        if (!validToken(epoch)) return false;
        std::memmove(epoch_, epoch, sizeof(epoch_));
        nonce_[0] = '\0';
        session_ = 0;
        clearCache();
        return true;
    }

    /// The service epoch supplied by the platform, distinct from a client challenge or an ownership credential.
    const char* epoch() const { return epoch_; }
    /// The latest accepted client challenge, echoed by hello to reject a prior central's reply.
    const char* nonce() const { return nonce_; }
    /// Monotonic client namespace within this service epoch; zero means hello has not established one.
    uint32_t session() const { return session_; }
    /// Highest accepted request ID in the current session; clients establish a fresh session after 65535.
    uint16_t highWater() const { return highWater_; }

    /// Establish or explicitly resume a session; an identical current challenge keeps its namespace and cache.
    const char* hello(const char* nonce, const char* resumeEpoch = nullptr, uint32_t resumeSession = 0,
                      uint32_t* epochSessionCounter = nullptr) {
        if (!validToken(nonce)) return "nonce";
        if (!epoch_[0]) return "epoch-unavailable";
        if (resumeEpoch || resumeSession) {
            if (!validToken(resumeEpoch) || !session_ || resumeSession != session_ || std::strcmp(resumeEpoch, epoch_))
                return "session-expired";
        } else if (!session_ || std::strcmp(nonce, nonce_)) {
            uint32_t& counter = epochSessionCounter ? *epochSessionCounter : session_;
            if (counter == UINT32_MAX) return "session-exhausted";
            session_ = ++counter;
            clearCache();
        }
        std::memmove(nonce_, nonce, sizeof(nonce_));
        return nullptr;
    }

    /// Admit increasing IDs, retaining exact bytes so retries of reads and writes share the same correlation rule.
    Decision accept(const char* epoch, uint32_t session, uint16_t id, const char* request) {
        const size_t length = boundedLength(request);
        if (!validToken(epoch) || !id || !length || length > BleMessage::kCapacity) return Decision::Invalid;
        if (!session_ || session != session_ || std::strcmp(epoch, epoch_)) return Decision::SessionExpired;
        if (id < highWater_) return Decision::IdExpired;
        if (id == highWater_)
            return std::strcmp(request, request_) == 0 ? Decision::Retry : Decision::IdReused;
        highWater_ = id;
        std::memcpy(request_, request, length + 1);
        reply_[0] = '\0';
        return Decision::Accept;
    }

    /// Retain one bounded result for exact replay; rejected values leave the previous result intact.
    bool cacheReply(const char* reply) {
        const size_t length = boundedLength(reply);
        if (!length || length > BleMessage::kCapacity) return false;
        std::memmove(reply_, reply, length + 1);
        return true;
    }
    /// The exact bytes of the last accepted request, which a streaming list repeats at its next cursor.
    const char* request() const { return request_; }
    /// The retained result, or an empty string while its accepted request awaits dispatch.
    const char* cachedReply() const { return reply_; }

private:
    static bool validToken(const char* token) {
        if (!token) return false;
        for (size_t i = 0; i < 32; ++i)
            if (!((token[i] >= '0' && token[i] <= '9') || (token[i] >= 'a' && token[i] <= 'f'))) return false;
        return token[32] == '\0';
    }
    static size_t boundedLength(const char* text) {
        if (!text) return 0;
        size_t length = 0;
        while (length <= BleMessage::kCapacity && text[length]) ++length;
        return length;
    }
    void clearCache() {
        highWater_ = 0;
        request_[0] = reply_[0] = '\0';
    }

    char epoch_[33] = {};
    char nonce_[33] = {};
    uint32_t session_ = 0;
    uint16_t highWater_ = 0;
    char request_[BleMessage::kCapacity + 1] = {};
    char reply_[BleMessage::kCapacity + 1] = {};
};

} // namespace mm
