#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace mm {

/// Bounded connection identities; all operations belong to one owning task or an external lock, and a recycled radio handle receives a new generation, so late replies cannot target its new owner.
template <std::size_t Capacity>
class BleConnectionRegistry {
    static_assert(Capacity > 0);
public:
    struct Token {
        /// The radio's connection handle, which the radio recycles.
        uint16_t handle = 0;
        /// Unique per connection and never zero, so a recycled handle is told apart.
        uint64_t generation = 0;
        bool operator==(const Token&) const = default;
        /// Whether the token names a connection, which is any non-zero generation.
        explicit operator bool() const { return generation != 0; }
    };

    /// Invalid, duplicate, full and exhausted requests never displace a connected peer.
    Token connect(uint16_t handle) {
        if (handle == UINT16_MAX || lastGeneration_ == std::numeric_limits<uint64_t>::max()) return {};
        for (const auto& slot : slots_) if (slot && slot.handle == handle) return {};
        for (auto& slot : slots_) {
            if (slot) continue;
            slot = {handle, ++lastGeneration_};
            return slot;
        }
        return {};
    }
    /// Whether `token` names a connected peer.
    bool contains(Token token) const {
        if (!token) return false;
        for (const auto& slot : slots_) if (slot == token) return true;
        return false;
    }
    /// A stale token cannot disconnect a new occupant with the same handle.
    bool disconnect(Token token) {
        if (!token) return false;
        for (auto& slot : slots_) if (slot == token) { slot = {}; return true; }
        return false;
    }
    /// How many peers are connected.
    std::size_t size() const {
        std::size_t count = 0;
        for (const auto& slot : slots_) if (slot) ++count;
        return count;
    }
private:
    std::array<Token, Capacity> slots_{};
    uint64_t lastGeneration_ = 0;
};
} // namespace mm
