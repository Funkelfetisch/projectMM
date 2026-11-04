#pragma once
#include "core/util/BleConnectionRegistry.h"
#include "core/util/BlePeerIo.h"
#include "platform/BleLinkSecurity.h"
#include <array>
#include <cstring>
#include <new>

namespace mm {
/// The heap behind the peers past the two held inline; a test substitutes one that counts or refuses.
struct BlePeerAllocator {
    /// Allocate one peer's I/O buffers; null when the heap refuses.
    static BlePeerIo* create() { return new (std::nothrow) BlePeerIo(); }
    /// Free a peer made by `create`.
    static void destroy(BlePeerIo* io) { delete io; }
};
/// Independently framed requests and notified replies per connection; caller owns synchronization.
template <size_t Capacity, typename Allocator = BlePeerAllocator>
class BlePeerTable {
    static_assert(Capacity > 0);
    using Registry = BleConnectionRegistry<Capacity>;
    using Token = typename Registry::Token;
    static constexpr size_t kInlinePeers = Capacity < 2 ? Capacity : 2;
    struct Entry { Token token{}; BlePeerIo* io = nullptr; uint8_t subscriptions = 0; platform::BleLinkSecurity security{}; };
public:
    /// An empty table with no connection held.
    BlePeerTable() = default;
    /// Not copyable: the table owns its peers' buffers.
    BlePeerTable(const BlePeerTable&) = delete;
    BlePeerTable& operator=(const BlePeerTable&) = delete;
    /// Free the heap-held peers past the inline pair.
    ~BlePeerTable() { for (size_t i = kInlinePeers; i < Capacity; ++i) Allocator::destroy(entries_[i].io); }
    enum class Publish : uint8_t { Stored, Stale, Busy, Invalid };
    /// Register a connection and give it a peer; false for an invalid or duplicate handle, or when the table or heap is full.
    bool connect(uint16_t handle) {
        const Token token = identities_.connect(handle);
        if (!token) return false;
        for (size_t i = 0; i < Capacity; ++i) if (!entries_[i].token) {
            auto& entry = entries_[i];
            entry.io = i < kInlinePeers ? &inlinePeers_[i] : Allocator::create();
            if (!entry.io) { identities_.disconnect(token); return false; }
            entry.io->reset();
            entry.subscriptions = 0;
            entry.security = platform::BleLinkSecurity::Open;
            entry.token = token;
            return true;
        }
        identities_.disconnect(token);
        return false;
    }
    /// Drop a connection and free its peer when heap-held; false for an unknown handle.
    bool disconnect(uint16_t handle) {
        auto* entry = findHandle(handle);
        if (!entry) return false;
        identities_.disconnect(entry->token);
        const size_t index = static_cast<size_t>(entry - entries_.data());
        entry->io->disconnect();
        if (index >= kInlinePeers) Allocator::destroy(entry->io);
        entry->token = {}; entry->io = nullptr; entry->subscriptions = 0;
        return true;
    }
    /// Record the security manager's verdict on a connection; a failure never demotes an encrypted link.
    bool secured(uint16_t handle, bool encrypted) {
        auto* entry = findHandle(handle);
        if (!entry) return false;
        if (encrypted || entry->security != platform::BleLinkSecurity::Encrypted)
            entry->security = encrypted ? platform::BleLinkSecurity::Encrypted : platform::BleLinkSecurity::Failed;
        return true;
    }
    /// The generation of the connection behind `handle`; 0 when none is held.
    uint64_t generation(uint16_t handle) const {
        for (const auto& entry : entries_) if (entry.token && entry.token.handle == handle) return entry.token.generation;
        return 0;
    }
    /// The link security behind `generation`, and the connection a pairing request goes to; null once it is gone.
    platform::BleLinkSecurity* security(uint64_t generation, uint16_t& handle) {
        for (auto& entry : entries_) if (entry.token && entry.token.generation == generation) {
            handle = entry.token.handle;
            return &entry.security;
        }
        return nullptr;
    }
    /// Feed a request fragment to the connection's framer and return its parse result; Invalid for an unknown handle.
    BleMessage::Result feed(uint16_t handle, const uint8_t* bytes, size_t length) {
        auto* entry = findHandle(handle);
        return entry ? entry->io->feed(bytes, length) : BleMessage::Result::Invalid;
    }
    /// Copy the next at most `capacity` reply bytes waiting for `handle` into `out` and return how many; 0 when none wait or the handle is unknown.
    size_t pending(uint16_t handle, uint8_t* out, size_t capacity) {
        auto* entry = findHandle(handle);
        return entry ? entry->io->pending(out, capacity) : 0;
    }
    /// Mark `count` bytes of what `pending` returned for `handle` as sent.
    void sent(uint16_t handle, size_t count) {
        if (auto* entry = findHandle(handle)) entry->io->sent(count);
    }
    /// The connection's latest reply, without its LF; null for an unknown handle.
    const char* reply(uint16_t handle) {
        auto* entry = findHandle(handle);
        return entry ? entry->io->current() : nullptr;
    }
    /// Move the next complete request, taking peers in turn, into `out` and name its connection by generation; 0 when none waits.
    size_t take(char* out, size_t capacity, uint64_t& generation) {
        for (size_t n = 0; n < Capacity; ++n) {
            const size_t i = (next_ + n) % Capacity;
            auto& entry = entries_[i];
            if (!entry.token) continue;
            const size_t length = entry.io->take(out, capacity);
            if (!length) continue;
            generation = entry.token.generation;
            next_ = (i + 1) % Capacity;
            return length;
        }
        return 0;
    }
    /// Queue a reply for the connection behind `generation`, sent only once it enabled notifications; Stale without one, Busy while its previous reply is still going out.
    Publish publish(const char* value, uint64_t generation) {
        for (auto& entry : entries_) if (generation && entry.token && entry.token.generation == generation) {
            if (entry.io->publish(value, entry.subscriptions & 1u)) return Publish::Stored;
            return entry.io->sending() ? Publish::Busy : Publish::Invalid;
        }
        return Publish::Stale;
    }
    /// How many connections are held.
    size_t size() const { return identities_.size(); }
    /// The handle of the first held connection, or UINT16_MAX when none.
    uint16_t firstHandle() const {
        for (const auto& entry : entries_) if (entry.token) return entry.token.handle;
        return UINT16_MAX;
    }
    /// Copy up to `capacity` live connection generations into `out` and return how many.
    size_t generations(uint64_t* out, size_t capacity) const {
        if (!out) return 0;
        size_t count = 0;
        for (const auto& entry : entries_)
            if (entry.token && count < capacity) out[count++] = entry.token.generation;
        return count;
    }
    /// Turn a connection's subscription to `topic` (0 to 7) on or off; false for an unknown handle or topic.
    bool subscribe(uint16_t handle, uint8_t topic, bool enabled) {
        auto* entry = findHandle(handle);
        if (!entry || topic >= 8) return false;
        const uint8_t bit = static_cast<uint8_t>(1u << topic);
        if (enabled) entry->subscriptions |= bit;
        else entry->subscriptions &= static_cast<uint8_t>(~bit);
        // Replies travel on topic 0, so one switched off mid-reply must not hold the next.
        if (!enabled && topic == 0) entry->io->discard();
        return true;
    }
    /// Copy up to `capacity` handles subscribed to `topic` into `out` and return how many.
    size_t subscribers(uint8_t topic, uint16_t* out, size_t capacity) const {
        if (topic >= 8 || !out) return 0;
        const uint8_t bit = static_cast<uint8_t>(1u << topic);
        size_t count = 0;
        for (const auto& entry : entries_)
            if (entry.token && (entry.subscriptions & bit) && count < capacity) out[count++] = entry.token.handle;
        return count;
    }
private:
    Entry* findHandle(uint16_t handle) {
        for (auto& entry : entries_) if (entry.token && entry.token.handle == handle) return &entry;
        return nullptr;
    }
    Registry identities_;
    std::array<BlePeerIo, kInlinePeers> inlinePeers_{};
    std::array<Entry, Capacity> entries_{};
    size_t next_ = 0;
};
} // namespace mm
