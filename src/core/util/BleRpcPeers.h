#pragma once
#include "core/util/BleRpcSession.h"
#include <array>
#include <cstring>
#include <new>

namespace mm {
/// Heap allocator for the sessions beyond the inline ones, replaceable by a test double.
struct BleRpcSessionAllocator {
    /// A fresh session, or null when memory is exhausted.
    static BleRpcSession* create() { return new (std::nothrow) BleRpcSession(); }
    /// Frees a session `create` returned.
    static void destroy(BleRpcSession* session) { delete session; }
};

/// Independent RPC namespaces, allocated on demand beyond two inline peers; the caller serializes access and supplies live connection generations.
template <size_t Capacity, typename Allocator = BleRpcSessionAllocator>
class BleRpcPeers {
    static_assert(Capacity > 0);
    static constexpr size_t kInlinePeers = Capacity < 2 ? Capacity : 2;
    struct Entry { BleRpcSession* session = nullptr; uint64_t owner = 0; bool occupied = false; };
public:
    /// Starts with no epoch, so `create` refuses until `beginEpoch` runs.
    BleRpcPeers() = default;
    /// The pool owns its sessions, so it is never copied.
    BleRpcPeers(const BleRpcPeers&) = delete;
    BleRpcPeers& operator=(const BleRpcPeers&) = delete;
    /// Frees the allocated sessions.
    ~BleRpcPeers() { clear(); }

    /// Starts a new epoch and drops every session; an epoch a session rejects changes nothing.
    bool beginEpoch(const char* epoch) {
        BleRpcSession probe;
        if (!probe.beginEpoch(epoch)) return false;
        clear();
        std::memcpy(epoch_, epoch, sizeof(epoch_));
        counter_ = 0;
        return true;
    }
    /// The session bound to the connection `owner`, or null.
    BleRpcSession* find(uint64_t owner) {
        if (!owner) return nullptr;
        for (auto& entry : entries_) if (entry.owner == owner) return entry.session;
        return nullptr;
    }
    /// Retry or explicit resume only binds a retained disconnected namespace.
    BleRpcSession* retryChallenge(uint64_t owner, const char* nonce) {
        if (!owner || !nonce || find(owner)) return nullptr;
        for (auto& entry : entries_) if (entry.occupied && !entry.owner && entry.session->session() &&
                std::strcmp(entry.session->nonce(), nonce) == 0) {
            entry.owner = owner; return entry.session;
        }
        return nullptr;
    }
    /// Active sessions are never evicted. Allocation failure leaves them intact.
    BleRpcSession* create(uint64_t owner) {
        if (!owner || !epoch_[0]) return nullptr;
        if (auto* existing = find(owner)) return existing;
        for (size_t i = 0; i < Capacity; ++i)
            if (!entries_[i].occupied) if (auto* result = initialize(i, owner)) return result;
        for (size_t i = 0; i < Capacity; ++i)
            if (entries_[i].occupied && !entries_[i].owner) return initialize(i, owner);
        return nullptr;
    }
    /// Rebinds the retained disconnected session numbered `session` to `owner`; null when the epoch or nonce does not match.
    BleRpcSession* resume(uint64_t owner, const char* epoch, uint32_t session, const char* nonce) {
        if (!owner || !epoch || std::strcmp(epoch_, epoch) || !session || find(owner)) return nullptr;
        for (auto& entry : entries_) if (entry.occupied && !entry.owner && entry.session->session() == session) {
            if (entry.session->hello(nonce, epoch, session, &counter_)) return nullptr;
            entry.owner = owner; return entry.session;
        }
        return nullptr;
    }
    /// Unbinds `owner` from its session, which stays retained for a resume.
    bool release(uint64_t owner) {
        if (!owner) return false;
        for (auto& entry : entries_) if (entry.owner == owner) { entry.owner = 0; return true; }
        return false;
    }
    /// Unbinds every owner that is not in the live list.
    void releaseInactive(const uint64_t* owners, size_t count) {
        for (auto& entry : entries_) {
            if (!entry.owner) continue;
            bool active = false;
            for (size_t i = 0; owners && i < count; ++i) if (entry.owner == owners[i]) { active = true; break; }
            if (!active) entry.owner = 0;
        }
    }
    /// The retained session with this number, bound to a connection or not.
    BleRpcSession* findSession(uint32_t session) {
        if (!session) return nullptr;
        for (auto& entry : entries_) if (entry.occupied && entry.session->session() == session) return entry.session;
        return nullptr;
    }
    /// The epoch's session counter, which each session's hello advances.
    uint32_t* counter() { return &counter_; }
private:
    void clear() {
        for (size_t i = kInlinePeers; i < Capacity; ++i)
            if (entries_[i].session) Allocator::destroy(entries_[i].session);
        entries_ = {};
    }
    BleRpcSession* initialize(size_t i, uint64_t owner) {
        // Keep the bounds explicit for both callers and embedded optimizers.
        if (i >= Capacity) return nullptr;
        auto& entry = entries_[i];
        if (!entry.session) {
            entry.session = i < kInlinePeers ? &inline_[i] : Allocator::create();
            if (!entry.session) return nullptr;
        }
        entry.session->~BleRpcSession();
        new (entry.session) BleRpcSession();
        if (!entry.session->beginEpoch(epoch_)) return nullptr;
        entry.owner = owner; entry.occupied = true;
        return entry.session;
    }
    std::array<BleRpcSession, kInlinePeers> inline_{};
    std::array<Entry, Capacity> entries_{};
    char epoch_[33] = {};
    uint32_t counter_ = 0;
};
} // namespace mm
