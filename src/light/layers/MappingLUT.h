#pragma once

#include "light/util/light_types.h"  // nrOfLightsType
#include "platform/platform.h"

#include <cstring>

namespace mm {

/// The table mapping each logical light to the physical lights it drives: one, none or many, with no table at all on the sequential identity fast path.
///
/// @technical
///
/// ## Four mapping kinds
///
/// Identity (a plain grid) needs no table; a shuffled map (serpentine), an unmapped light (a sparse layout) and a multimap (mirroring) each need one.
///
/// ## Compressed sparse row
///
/// One array indexes each logical light into a run, the other holds the flat destinations, and the container supplies the total, so every destination is in bounds.
///
/// ## Paged destinations
///
/// A large map on a board without PSRAM can outgrow the largest free block, so the destinations split into power-of-two pages; the output is identical either way.
class MappingLUT {
    // One build owns one payload budget. The current heap can shrink during its
    // walks; the snapshot also counts earlier allocations on host heap fixtures.
    // Admission cooperates with other users rather than reserving the heap atomically.
    class AllocationBudget {
    public:
        explicit AllocationBudget(size_t reserve)
            : reserve_(reserve < platform::HEAP_RESERVE ? platform::HEAP_RESERVE : reserve),
              remaining_(available()) {}

        bool fits(size_t bytes) const { return bytes <= remaining_ && bytes <= available(); }

        void reclaimed(size_t bytes) {
            const size_t unlimited = static_cast<size_t>(-1);
            remaining_ = bytes > unlimited - remaining_ ? unlimited : remaining_ + bytes;
        }

        void* allocate(size_t bytes) {
            if (!fits(bytes)) return nullptr;
            void* result = platform::alloc(bytes);
            if (!result) return nullptr;
            const size_t heap = platform::freeHeap();
            const size_t internal = platform::freeInternalHeap();
            if ((heap > 0 && heap < reserve_) || (internal > 0 && internal < reserve_)) {
                platform::free(result);
                return nullptr;
            }
            remaining_ -= bytes;
            return result;
        }

    private:
        size_t available() const {
            const size_t internal = platform::freeInternalHeap();
            if (internal > 0 && internal < reserve_) return 0;
            const size_t heap = platform::freeHeap();
            if (heap == 0) return static_cast<size_t>(-1); // unreported desktop heap
            return heap > reserve_ ? heap - reserve_ : 0;
        }

        size_t reserve_;
        size_t remaining_;
    };

public:
    /// An empty table, in identity mode until one is built.
    MappingLUT() = default;
    /// Release whatever the table allocated.
    ~MappingLUT() { free(); }

    /// Never copied: the table owns its allocations and two owners would double-free them.
    MappingLUT(const MappingLUT&) = delete;
    MappingLUT& operator=(const MappingLUT&) = delete;

    /// How many destinations one page holds, sized to fit a fragmented heap. A power of two, so the page split is a shift and a mask: Xtensa has no hardware divide.
    static constexpr nrOfLightsType kPageEntries = 4096;
    /// The shift that turns a destination index into a page number.
    static constexpr nrOfLightsType kPageShift = 12;
    /// The mask that turns a destination index into a slot within its page.
    static constexpr nrOfLightsType kPageMask = kPageEntries - 1;
    /// How many pages the table can hold, capping it at 256K destinations.
    static constexpr int kMaxPages = 64;
    static_assert((kPageEntries & kPageMask) == 0, "kPageEntries must be a power of two");

    /// Fast path: logical == physical, no table needed. `hasLUT` returns false.
    void setIdentity(nrOfLightsType count) {
        free();
        identity_ = true;
        logicalCount_ = count;
    }

    /// Map no source to any output, so composition clears physical lights without allocating a table.
    void setEmpty() {
        free();
        identity_ = false;
    }

    /// Allocate the table within the shared heap reserve: a single block, then pages, then refusal.
    bool build(nrOfLightsType logicalCount, nrOfLightsType maxDestinations) {
        if (static_cast<size_t>(logicalCount) >= kMaxElements ||
            static_cast<size_t>(maxDestinations) > kMaxElements) { free(); return false; }
        const bool reuseOffsets = offsets_ && logicalCount_ == logicalCount;
        if (!reuseOffsets) free();
        AllocationBudget budget(platform::HEAP_RESERVE);
        identity_ = false;
        overwrites_ = true;
        logicalCount_ = logicalCount;

        size_t offsetBytes = (static_cast<size_t>(logicalCount) + 1) * sizeof(nrOfLightsType);
        size_t destBytes = static_cast<size_t>(maxDestinations) * sizeof(nrOfLightsType);

        // Only the destinations array can hit the fragmentation cliff; offsets is small.
        if (!reuseOffsets) offsets_ = static_cast<nrOfLightsType*>(budget.allocate(offsetBytes));
        if (!offsets_) { free(); return false; }
        std::memset(offsets_, 0, offsetBytes);

        if (!allocateDestinations(destBytes, maxDestinations, budget)) {
            free();
            return false;
        }

        destinationCount_ = 0;
        destinationLimit_ = maxDestinations;
        return true;
    }

    /// Build a CSR in place from a deterministic emitter, called twice with a (logical, physical) visitor.
    template<typename Enumerate>
    bool build(nrOfLightsType logicalCount, nrOfLightsType maxDestinations, Enumerate enumerate,
               size_t reserve = platform::HEAP_RESERVE) {
        if (static_cast<size_t>(logicalCount) >= kMaxElements) { free(); return false; }
        const bool reuseOffsets = offsets_ && logicalCount_ == logicalCount;
        if (!reuseOffsets) free();
        AllocationBudget budget(reserve);
        identity_ = false;
        overwrites_ = true;
        logicalCount_ = logicalCount;
        const size_t offsetBytes = (static_cast<size_t>(logicalCount) + 1) * sizeof(nrOfLightsType);
        if (!reuseOffsets) offsets_ = static_cast<nrOfLightsType*>(budget.allocate(offsetBytes));
        if (!offsets_) { free(); return false; }
        std::memset(offsets_, 0, offsetBytes);

        size_t total = 0;
        bool valid = true;
        enumerate([&](nrOfLightsType logical, nrOfLightsType) {
            if (logical >= logicalCount || total >= maxDestinations) { valid = false; return; }
            ++offsets_[logical];
            ++total;
        });
        if (!valid || total > kMaxElements) { free(); return false; }

        nrOfLightsType running = 0;
        for (size_t i = 0; i < logicalCount; ++i) {
            const nrOfLightsType count = offsets_[i];
            offsets_[i] = running;
            running = static_cast<nrOfLightsType>(running + count);
        }
        offsets_[logicalCount] = running;
        if (!allocateDestinations(total * sizeof(nrOfLightsType), running, budget)) { free(); return false; }

        size_t written = 0;
        enumerate([&](nrOfLightsType logical, nrOfLightsType physical) {
            if (logical >= logicalCount || offsets_[logical] >= total) { valid = false; return; }
            writeDestination(offsets_[logical]++, physical);
            ++written;
        });
        if (!valid || written != total) { free(); return false; }
        for (size_t i = logicalCount; i > 0; --i) offsets_[i] = offsets_[i - 1];
        offsets_[0] = 0;
        destinationCount_ = running;
        destinationLimit_ = running;
        return true;
    }

    /// Fill one logical entry's destinations (call sequentially, idx 0..logicalCount-1)
    void setMapping(nrOfLightsType logicalIdx, const nrOfLightsType* physicals, nrOfLightsType count) {
        if (!offsets_ || logicalIdx >= logicalCount_) return;
        offsets_[logicalIdx] = destinationCount_;
        for (nrOfLightsType i = 0; i < count && destinationCount_ < destinationLimit_; i++) {
            writeDestination(destinationCount_, physicals[i]);
            destinationCount_++;
        }
    }

    /// Call after all setMapping calls to close the last offset
    void finalize() {
        if (offsets_) {
            offsets_[logicalCount_] = destinationCount_;
        }
    }

    /// Release the table and return to the identity fast path.
    void free() {
        if (offsets_) { platform::free(offsets_); offsets_ = nullptr; }
        freeDestinations();
        logicalCount_ = 0;
        destinationCount_ = 0;
        destinationCapacity_ = 0;
        destinationLimit_ = 0;
        identity_ = true;
        overwrites_ = true;
    }

    /// Whether composition uses a mapping, including an empty map that clears every output. @xref{four-mapping-kinds}
    bool hasLUT() const { return !identity_; }
    /// Whether the destinations are split into pages.
    bool isPaged() const { return paged_; }
    /// How many logical lights the table covers.
    nrOfLightsType logicalCount() const { return logicalCount_; }
    /// How many physical destinations the table holds in total.
    nrOfLightsType destinationCount() const { return destinationCount_; }

    /// Whether each physical destination is written by at most one logical light, which lets blendMap plain-copy, around four times faster than the additive path.
    bool overwrites() const { return overwrites_; }
    /// Declare whether destinations are distinct, which chooses the copy or additive blend.
    void setOverwrites(bool v) { overwrites_ = v; }

    /// The bytes the table uses, which is zero in identity mode.
    size_t memoryUsed() const {
        if (!offsets_) return 0;
        return (static_cast<size_t>(logicalCount_) + 1) * sizeof(nrOfLightsType)
             + static_cast<size_t>(destinationCapacity_) * sizeof(nrOfLightsType);
    }

    /// The bytes a prospective build would take, which paging does not change.
    static size_t estimateBytes(nrOfLightsType logicalCount, nrOfLightsType maxDest) {
        if (static_cast<size_t>(logicalCount) >= kMaxElements ||
            static_cast<size_t>(maxDest) > kMaxElements - static_cast<size_t>(logicalCount) - 1)
            return static_cast<size_t>(-1);
        return (static_cast<size_t>(logicalCount) + 1) * sizeof(nrOfLightsType)
             + static_cast<size_t>(maxDest) * sizeof(nrOfLightsType);
    }

    /// Walk the physical destinations of one logical light, on the hot path. @xref{compressed-sparse-row}
    template<typename F>
    void forEachDestination(nrOfLightsType logicalIdx, F&& callback) const {
        if (identity_) {
            callback(logicalIdx);
            return;
        }
        if (!offsets_ || logicalIdx >= logicalCount_) return;
        nrOfLightsType start = offsets_[logicalIdx];
        nrOfLightsType end = offsets_[logicalIdx + 1];
        if (!paged_) {
            // The common case, where the branch predicts not-taken and this stays flat.
            for (nrOfLightsType i = start; i < end; i++) {
                callback(destinations_[i]);
            }
            return;
        }
        // One run can straddle a page boundary, so recompute the page when the slot wraps.
        for (nrOfLightsType i = start; i < end; i++) {
            const nrOfLightsType* page = pages_[i >> kPageShift];
            callback(page[i & kPageMask]);
        }
    }

    /// One light's channels, the common three unrolled since it runs per light on a gather.
    static void copyLight(uint8_t* d, const uint8_t* s, size_t cpl) MM_NONBLOCKING {
        if (cpl == 3) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; }
        else std::memcpy(d, s, cpl);
    }

private:
    static constexpr size_t kMaxElements = static_cast<size_t>(-1) / sizeof(nrOfLightsType);

    void freeDestinations() {
        if (destinations_) { platform::free(destinations_); destinations_ = nullptr; }
        for (int i = pageCount_ - 1; i >= 0; i--) {
            platform::free(pages_[i]);
            pages_[i] = nullptr;
        }
        pageCount_ = 0;
        paged_ = false;
        destinationCapacity_ = 0;
    }

    /// Tier the destinations allocation, false only on tier 3. @xref{paged-destinations}
    bool allocateDestinations(size_t destBytes, nrOfLightsType maxDestinations, AllocationBudget& budget) {
        if (maxDestinations <= destinationCapacity_) return true; // keep admitted storage, even under pressure
        const size_t releasedBytes = static_cast<size_t>(destinationCapacity_) * sizeof(nrOfLightsType);
        freeDestinations();
        budget.reclaimed(releasedBytes);
        if (!budget.fits(destBytes)) return false;

        // A zero maxAllocBlock means unlimited, so the desktop exercises the flat path too.
        size_t maxBlock = platform::maxAllocBlock();
        if (maxBlock == 0 || maxBlock >= destBytes) {
            destinations_ = static_cast<nrOfLightsType*>(budget.allocate(destBytes));
            if (destinations_) { destinationCapacity_ = maxDestinations; paged_ = false; return true; }
            // Fall through to paging if the single alloc lost a race.
        }

        // Tier 2: page it.
        int needed = static_cast<int>((static_cast<size_t>(maxDestinations) + kPageEntries - 1) / kPageEntries);
        if (needed > kMaxPages) return false;
        for (int p = 0; p < needed; p++) {
            const size_t remaining = static_cast<size_t>(maxDestinations) - static_cast<size_t>(p) * kPageEntries;
            const size_t entries = remaining < kPageEntries ? remaining : kPageEntries;
            pages_[p] = static_cast<nrOfLightsType*>(budget.allocate(entries * sizeof(nrOfLightsType)));
            if (!pages_[p]) {
                // Partial failure: unwind the pages we got (reverse order).
                for (int q = p - 1; q >= 0; q--) { platform::free(pages_[q]); pages_[q] = nullptr; }
                pageCount_ = 0;
                return false;
            }
        }
        pageCount_ = needed;
        destinationCapacity_ = maxDestinations;
        paged_ = true;
        return true;
    }

    void writeDestination(nrOfLightsType slot, nrOfLightsType value) {
        if (paged_) pages_[slot >> kPageShift][slot & kPageMask] = value;
        else        destinations_[slot] = value;
    }

    bool identity_ = true;
    bool paged_ = false;
    nrOfLightsType logicalCount_ = 0;
    nrOfLightsType* offsets_ = nullptr;
    nrOfLightsType* destinations_ = nullptr;   // tier-1 single block (null when paged)
    nrOfLightsType* pages_[kMaxPages] = {};     // tier-2 pages (empty when single)
    int pageCount_ = 0;
    nrOfLightsType destinationCount_ = 0;
    nrOfLightsType destinationCapacity_ = 0;
    nrOfLightsType destinationLimit_ = 0;  // current build's fill limit, distinct from retained storage
    bool overwrites_ = true;   // single-write destinations → blendMap may copy
};

} // namespace mm
