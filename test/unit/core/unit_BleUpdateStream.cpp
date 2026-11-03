/// @module BleUpdateStream

/// The stream between the BLE host task and the render loop during an update over BLE: what it keeps, drops and answers, which a phone streaming an image relies on.

#include "doctest.h"
#include "core/util/BleUpdateStream.h"

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

namespace {
// One packet as the phone writes it: the offset, bit 31 asking for an acknowledgement, then `count` bytes counting up from `offset`.
std::vector<uint8_t> packet(uint32_t offset, size_t count, bool ask = false) {
    std::vector<uint8_t> frame(4 + count);
    const uint32_t word = offset | (ask ? mm::BleUpdateStream::kAckRequest : 0u);
    for (size_t i = 0; i < 4; ++i) frame[i] = static_cast<uint8_t>(word >> (8 * i));
    for (size_t i = 0; i < count; ++i) frame[4 + i] = static_cast<uint8_t>(offset + i);
    return frame;
}
bool accept(mm::BleUpdateStream& stream, uint64_t owner, const std::vector<uint8_t>& frame) {
    return stream.accept(owner, frame.data(), frame.size());
}
// The end the stream's answer names when it answers at `stored`; 0 when it does not answer.
uint32_t answer(mm::BleUpdateStream& stream, uint32_t stored) {
    uint32_t asked = 0;
    return stream.acknowledge(stored, asked) ? asked : 0;
}
// Everything held, as the render loop takes it; `from` is the offset of the first byte.
std::vector<uint8_t> takeAll(mm::BleUpdateStream& stream, uint32_t& from) {
    std::vector<uint8_t> all;
    uint8_t buffer[300];
    uint32_t offset = 0;
    bool first = true;
    while (const size_t count = stream.take(buffer, sizeof(buffer), offset)) {
        if (first) from = offset;
        first = false;
        all.insert(all.end(), buffer, buffer + count);
    }
    return all;
}
}  // namespace

TEST_CASE("BleUpdateStream: packets continuing what is held are kept in order, and the window's last packet is answered once stored") {
    mm::BleUpdateStream stream;
    stream.open(7, 0);
    CHECK(accept(stream, 7, packet(0, 508)));
    CHECK(accept(stream, 7, packet(508, 92, true)));
    CHECK(answer(stream, 0) == 0);   // asked for 600, nothing stored yet
    uint32_t from = 99;
    const auto bytes = takeAll(stream, from);
    CHECK(from == 0);
    REQUIRE(bytes.size() == 600);
    for (size_t i = 0; i < bytes.size(); ++i) CHECK(bytes[i] == static_cast<uint8_t>(i));
    CHECK(answer(stream, 599) == 0);
    CHECK(answer(stream, 600) == 600);   // the answer names the request it answers
    CHECK(answer(stream, 600) == 0);     // each request is answered once
}

TEST_CASE("BleUpdateStream: a packet from another link, or while closed, is dropped") {
    mm::BleUpdateStream stream;
    CHECK_FALSE(accept(stream, 7, packet(0, 10, true)));   // never opened
    stream.open(7, 0);
    CHECK_FALSE(accept(stream, 8, packet(0, 10, true)));
    stream.close();
    CHECK_FALSE(stream.isOpen());
    CHECK_FALSE(accept(stream, 7, packet(0, 10, true)));
    uint32_t from = 0;
    CHECK(takeAll(stream, from).empty());
    CHECK(answer(stream, 0) == 0);   // no request reached it
}

TEST_CASE("BleUpdateStream: a packet too short for an offset and a byte is ignored") {
    mm::BleUpdateStream stream;
    stream.open(7, 0);
    const auto header = packet(0, 0, true);
    CHECK_FALSE(stream.accept(7, header.data(), header.size()));
    CHECK_FALSE(stream.accept(7, nullptr, 0));
    CHECK(answer(stream, 0) == 0);
}

TEST_CASE("BleUpdateStream: a gap drops the packet, and once the ring is drained the answer names what is stored") {
    mm::BleUpdateStream stream;
    stream.open(7, 0);
    CHECK(accept(stream, 7, packet(0, 100)));
    CHECK_FALSE(accept(stream, 7, packet(200, 100, true)));   // 100..199 never arrived
    CHECK(answer(stream, 0) == 0);   // what is held is not stored yet
    uint32_t from = 0;
    CHECK(takeAll(stream, from).size() == 100);
    CHECK(answer(stream, 100) == 300);   // short of the 300 asked: the phone resends from 100
    CHECK(accept(stream, 7, packet(100, 200, true)));
    CHECK(takeAll(stream, from).size() == 200);
    CHECK(from == 100);
    CHECK(answer(stream, 300) == 300);
}

TEST_CASE("BleUpdateStream: a resend overlapping what is held keeps only the new bytes") {
    mm::BleUpdateStream stream;
    stream.open(7, 0);
    CHECK(accept(stream, 7, packet(0, 300)));
    CHECK(accept(stream, 7, packet(0, 300, true)));   // wholly held: kept as is, and the request still counts
    CHECK(accept(stream, 7, packet(200, 200)));       // overlapping: only 300..399 is new
    uint32_t from = 0;
    const auto bytes = takeAll(stream, from);
    REQUIRE(bytes.size() == 400);
    for (size_t i = 0; i < bytes.size(); ++i) CHECK(bytes[i] == static_cast<uint8_t>(i));
    CHECK(answer(stream, 400) == 300);   // the request came with the resend of 0..299
}

TEST_CASE("BleUpdateStream: a phone that overruns its window loses the excess and is told to resend") {
    mm::BleUpdateStream stream;
    stream.open(7, 0);
    uint32_t offset = 0;
    while (offset + 508 <= 8191) { REQUIRE(accept(stream, 7, packet(offset, 508))); offset += 508; }
    CHECK_FALSE(accept(stream, 7, packet(offset, 508, true)));   // the ring holds 8191 bytes
    uint32_t from = 0;
    const size_t held = takeAll(stream, from).size();
    CHECK(held == 8191);
    CHECK(answer(stream, static_cast<uint32_t>(held)) == offset + 508);
}

TEST_CASE("BleUpdateStream: opening again forgets what an earlier transfer left") {
    mm::BleUpdateStream stream;
    stream.open(7, 0);
    CHECK(accept(stream, 7, packet(0, 100, true)));
    stream.open(9, 0);
    CHECK(answer(stream, 0) == 0);   // the old request is gone
    uint32_t from = 0;
    CHECK(takeAll(stream, from).empty());
    CHECK_FALSE(accept(stream, 7, packet(0, 10)));
    CHECK(accept(stream, 9, packet(0, 10, true)));
    CHECK(takeAll(stream, from).size() == 10);
    CHECK(answer(stream, 10) == 10);
}

TEST_CASE("BleUpdateStream: a request raised while the store catches up is answered once, at the furthest end asked") {
    mm::BleUpdateStream stream;
    stream.open(7, 0);
    CHECK(accept(stream, 7, packet(0, 100, true)));
    CHECK(accept(stream, 7, packet(100, 100, true)));
    uint32_t from = 0;
    takeAll(stream, from);
    CHECK(answer(stream, 100) == 0);   // the phone now waits for 200
    CHECK(answer(stream, 200) == 200);
    CHECK(answer(stream, 200) == 0);
}

TEST_CASE("BleUpdateStream: a phone thread and a loop thread move a whole image through the stream, every window answered") {
    mm::BleUpdateStream stream;
    stream.open(7, 0);
    constexpr uint32_t kImage = 64 * 1024;
    std::atomic<uint32_t> answered{0};
    std::atomic<bool> done{false}, ordered{true};
    std::vector<uint8_t> stored;
    std::thread loop([&] {
        uint8_t buffer[512];
        uint32_t offset = 0;
        while (!done.load()) {
            while (const size_t count = stream.take(buffer, sizeof(buffer), offset)) {
                if (offset != stored.size()) ordered.store(false);   // asserted after the join: doctest is not called from this thread
                stored.insert(stored.end(), buffer, buffer + count);
            }
            uint32_t asked = 0;
            if (stream.acknowledge(static_cast<uint32_t>(stored.size()), asked) && asked == stored.size()) answered.store(asked);
            std::this_thread::yield();
        }
    });
    for (uint32_t window = 0; window < kImage; window += mm::BleUpdateStream::kWindowBytes) {
        for (uint32_t at = window; at < window + mm::BleUpdateStream::kWindowBytes; at += 512) {
            const uint32_t count = std::min<uint32_t>(512, window + mm::BleUpdateStream::kWindowBytes - at);
            REQUIRE(accept(stream, 7, packet(at, count, at + count == window + mm::BleUpdateStream::kWindowBytes)));
        }
        while (answered.load() < window + mm::BleUpdateStream::kWindowBytes) std::this_thread::yield();
    }
    done.store(true);
    loop.join();
    CHECK(ordered.load());
    REQUIRE(stored.size() == kImage);
    for (size_t i = 0; i < stored.size(); ++i) REQUIRE(stored[i] == static_cast<uint8_t>(i));
}
