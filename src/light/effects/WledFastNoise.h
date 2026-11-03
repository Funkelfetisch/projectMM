#pragma once

#include <cstdint>

namespace mm {
/// FastLED 3.6 fixed 8-bit two-dimensional noise, for archived WLED looks. Coordinates are 8.8 and wrap as uint16_t. This is distinct from MoonLight's improved-Perlin noise. Copyright (c) 2013 FastLED; MIT terms are retained in docs/licenses/FastLED-3.6.0.txt. @attribution FastLED contributors
class WledFastNoise {
public:
    /// The noise value, 0 to 255, at the 8.8 coordinates `x` and `y`.
    static uint8_t sample(uint16_t x, uint16_t y) {
        const uint8_t X = uint8_t(x >> 8), Y = uint8_t(y >> 8);
        const uint8_t A = uint8_t(p_[X] + Y), B = uint8_t(p_[unsigned(X) + 1] + Y);
        const uint8_t AA = p_[A], AB = p_[unsigned(A) + 1], BA = p_[B], BB = p_[unsigned(B) + 1];
        const uint8_t u = ease(uint8_t(x)), v = ease(uint8_t(y));
        const int8_t xx = int8_t(uint8_t(x) >> 1), yy = int8_t(uint8_t(y) >> 1);
        const int8_t a = mix(grad(p_[AA], xx, yy), grad(p_[BA], int8_t(int(xx)-128), yy), u);
        const int8_t b = mix(grad(p_[AB], xx, int8_t(int(yy)-128)), grad(p_[BB], int8_t(int(xx)-128), int8_t(int(yy)-128)), u);
        const uint8_t value = uint8_t(int(mix(a, b, v)) + 64);
        return uint8_t(unsigned(value) * 2u > 255u ? 255u : unsigned(value) * 2u);
    }
private:
    static uint8_t scale(uint8_t v, uint8_t f) { return uint8_t(uint16_t(v) * (uint16_t(f)+1u) >> 8); }
    static uint8_t ease(uint8_t v) {
        uint8_t j = v & 128 ? uint8_t(255-v) : v;
        uint8_t result = uint8_t(scale(j, j) << 1);
        return v & 128 ? uint8_t(255-result) : result;
    }
    static int8_t grad(uint8_t h, int8_t x, int8_t y) {
        int8_t u = h & 4 ? y : x, v = h & 4 ? x : y;
        if (h & 1) u = int8_t(-int(u));
        if (h & 2) v = int8_t(-int(v));
        return int8_t((u >> 1) + (v >> 1) + (u & 1));
    }
    static int8_t mix(int8_t a, int8_t b, uint8_t fraction) {
        return b > a ? int8_t(int(a) + scale(uint8_t(int(b)-int(a)), fraction))
                     : int8_t(int(a) - scale(uint8_t(int(a)-int(b)), fraction));
    }
    inline static constexpr uint8_t p_[257] = {
    151, 160, 137,  91,  90,  15, 131,  13, 201,  95,  96,  53, 194, 233,   7, 225,
    140,  36, 103,  30,  69, 142,   8,  99,  37, 240,  21,  10,  23, 190,   6, 148,
    247, 120, 234,  75,   0,  26, 197,  62,  94, 252, 219, 203, 117,  35,  11,  32,
     57, 177,  33,  88, 237, 149,  56,  87, 174,  20, 125, 136, 171, 168,  68, 175,
     74, 165,  71, 134, 139,  48,  27, 166,  77, 146, 158, 231,  83, 111, 229, 122,
     60, 211, 133, 230, 220, 105,  92,  41,  55,  46, 245,  40, 244, 102, 143,  54,
     65,  25,  63, 161,   1, 216,  80,  73, 209,  76, 132, 187, 208,  89,  18, 169,
    200, 196, 135, 130, 116, 188, 159,  86, 164, 100, 109, 198, 173, 186,   3,  64,
     52, 217, 226, 250, 124, 123,   5, 202,  38, 147, 118, 126, 255,  82,  85, 212,
    207, 206,  59, 227,  47,  16,  58,  17, 182, 189,  28,  42, 223, 183, 170, 213,
    119, 248, 152,   2,  44, 154, 163,  70, 221, 153, 101, 155, 167,  43, 172,   9,
    129,  22,  39, 253,  19,  98, 108, 110,  79, 113, 224, 232, 178, 185, 112, 104,
    218, 246,  97, 228, 251,  34, 242, 193, 238, 210, 144,  12, 191, 179, 162, 241,
     81,  51, 145, 235, 249,  14, 239, 107,  49, 192, 214,  31, 181, 199, 106, 157,
    184,  84, 204, 176, 115, 121,  50,  45, 127,   4, 150, 254, 138, 236, 205,  93,
    222, 114,  67,  29,  24,  72, 243, 141, 128, 195,  78,  66, 215,  61, 156, 180,
    151};
};
} // namespace mm
