// @module PixelblazeCompatEffect

#include "doctest.h"
#include "light/effects/PixelblazeCompatEffect.h"

#include <cstring>

namespace {

struct AllocCapGuard {
    ~AllocCapGuard() { mm::platform::setTestMaxAllocBlock(0); }
};

} // namespace

TEST_CASE("Pixelblaze program bounds nested loops with one frame budget") {
    mm::pixelblaze::Program program;
    const char* source =
        "export function render(index) {"
        "for (i = 0; i < 4096; i++) {"
        "for (j = 0; j < 4096; j++) { value += 1; }"
        "} rgb(1, 0, 0); }";

    REQUIRE(program.compile(source, 222));
    program.beginFrame(16.0f, 16);
    (void)program.renderPixel(0, 0.0f, 0.0f);

    CHECK_FALSE(program.ok());
    CHECK(std::strcmp(program.error(), "instruction budget hit") == 0);
}

TEST_CASE("Pixelblaze program renders a finite script within its frame budget") {
    mm::pixelblaze::Program program;
    REQUIRE(program.compile("export function render(index) { rgb(1, 0, 0); }", 222));

    program.beginFrame(16.0f, 16);
    const mm::RGB color = program.renderPixel(0, 0.0f, 0.0f);

    CHECK(program.ok());
    CHECK(color.r == 255);
    CHECK(color.g == 0);
    CHECK(color.b == 0);
}

TEST_CASE("Pixelblaze program reports its platform-managed array storage") {
    mm::pixelblaze::Program program;
    REQUIRE(program.compile(
        "var values = array(pixelCount); export function render(index) { rgb(0, 0, 0); }",
        222));
    CHECK(program.dynamicBytes() >= static_cast<size_t>(222) * sizeof(float));
}

TEST_CASE("Pixelblaze rejects frame-time array allocation without growing memory") {
    mm::pixelblaze::Program program;
    REQUIRE(program.compile(
        "export function beforeRender(delta) { scratch = array(pixelCount); }"
        "export function render(index) { rgb(1, 0, 0); }",
        222));
    REQUIRE(program.dynamicBytes() == 0);

    program.beginFrame(16.0f, 16);

    CHECK_FALSE(program.ok());
    CHECK(std::strcmp(program.error(), "arrays must be initialized at top level") == 0);
    CHECK(program.dynamicBytes() == 0);
    const mm::RGB color = program.renderPixel(0, 0.0f, 0.0f);
    CHECK(color.r == 0);
    CHECK(color.g == 0);
    CHECK(color.b == 0);
}

TEST_CASE("Pixelblaze surfaces top-level array allocation failure") {
    AllocCapGuard guard;
    mm::platform::setTestMaxAllocBlock(64);
    mm::pixelblaze::Program program;

    CHECK_FALSE(program.compile(
        "var values = array(pixelCount); export function render(index) { rgb(0, 0, 0); }",
        222));
    CHECK(std::strcmp(program.error(), "array allocation failed") == 0);
    CHECK(program.dynamicBytes() == 0);
}
