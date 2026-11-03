/// @module Base64

#include "doctest.h"
#include "core/system/Base64.h"

#include <array>
#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

using namespace mm;

namespace {
std::string encoded(const std::string& bytes) {
    std::vector<char> text((bytes.size() + 2) / 3 * 4 + 1);
    base64Encode({reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()}, text);
    return text.data();
}

// Decodes into a buffer with guard bytes after it, so a write past the span shows.
struct Decoded {
    bool ok = false;
    size_t length = 0;
    std::string bytes;
    bool guardIntact = true;
};
Decoded decode(const std::string& text, size_t room = 512) {
    std::vector<uint8_t> buffer(room + 4, 0xA5);
    Decoded result;
    result.ok = base64Decode(text, {buffer.data(), room}, result.length);
    result.bytes.assign(reinterpret_cast<const char*>(buffer.data()), result.ok ? result.length : 0);
    for (size_t i = room; i < buffer.size(); ++i) result.guardIntact = result.guardIntact && buffer[i] == 0xA5;
    return result;
}
}  // namespace

TEST_CASE("base64Decode reads the RFC 4648 test vectors") {
    // RFC 4648 section 10.
    const std::pair<const char*, const char*> vectors[] = {
        {"", ""}, {"f", "Zg=="}, {"fo", "Zm8="}, {"foo", "Zm9v"}, {"foob", "Zm9vYg=="}, {"fooba", "Zm9vYmE="}, {"foobar", "Zm9vYmFy"}};
    for (const auto& [plain, text] : vectors) {
        const auto result = decode(text);
        CHECK(result.ok);
        CHECK(result.bytes == plain);
    }
}

TEST_CASE("base64Decode inverts base64Encode for every length and every byte value") {
    std::string bytes;
    for (size_t length = 0; length <= 300; ++length) {
        const auto result = decode(encoded(bytes));
        REQUIRE(result.ok);
        CHECK(result.bytes == bytes);
        bytes.push_back(static_cast<char>(length * 37 + 11));
    }
    std::string all;
    for (int value = 0; value < 256; ++value) all.push_back(static_cast<char>(value));
    CHECK(decode(encoded(all)).bytes == all);
}

TEST_CASE("base64Decode refuses text that is not canonical standard base64") {
    // Wrong length, a missing pad, a pad where data belongs, too many pads, and a pad before the end.
    for (const char* text : {"Zg", "Zg=", "Zm9", "Z", "Zm9vY", "Zg===", "====", "=Zg=", "Z=g=", "Zm=v", "Zg==Zm9v", "Zm8=Zm9v"}) {
        CAPTURE(text);
        CHECK_FALSE(decode(text).ok);
    }
    // The URL-safe alphabet, whitespace and other characters are not the standard alphabet.
    for (const std::string& text : {std::string("Zm9-"), std::string("Zm9_"), std::string("Zm 9"), std::string("Zm9\n"), std::string("Zm9v\n"),
                                    std::string("Zm9\xC3\xA9"), std::string("Zm9\0", 4), std::string("Zm9v\0", 5)}) {
        CAPTURE(text);
        CHECK_FALSE(decode(text).ok);
    }
    // Unused bits of a short final group must be zero, so one byte string has one text: "Zh==" and "Zm9=" are not "Zg==" and "Zm8=".
    CHECK_FALSE(decode("Zh==").ok);
    CHECK_FALSE(decode("Zm9=").ok);
    CHECK(decode("Zg==").ok);
    CHECK(decode("Zm8=").ok);
}

TEST_CASE("base64Decode never writes past the room it is given") {
    const std::string text = encoded(std::string(10, 'x'));
    for (size_t room = 0; room < 10; ++room) {
        const auto result = decode(text, room);
        CHECK_FALSE(result.ok);
        CHECK(result.length == 0);
        CHECK(result.guardIntact);
    }
    const auto exact = decode(text, 10);
    CHECK(exact.ok);
    CHECK(exact.length == 10);
    CHECK(exact.guardIntact);
}

TEST_CASE("base64Decode reports zero bytes for every refusal") {
    size_t decoded = 99;
    std::array<uint8_t, 4> room{};
    CHECK_FALSE(base64Decode("Zm9", room, decoded));
    CHECK(decoded == 0);
}
