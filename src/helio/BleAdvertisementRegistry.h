#pragma once

#include <cstdint>
#include <cstring>

namespace mm::ble_advertisement {
struct NamedCode { uint8_t code; const char* name; };
/// Exact persisted setup profile names; unknown and custom models remain unclassified.
inline uint8_t productCodeForModel(const char* model) {
    if (!model) return 0;
    if (std::strcmp(model, "NEBULITE Croptop S3") == 0 || std::strcmp(model, "NEBULITE Croptop ESP32") == 0) return 20;
    if (std::strcmp(model, "NEBULITE Jacket S3") == 0 || std::strcmp(model, "NEBULITE Jacket ESP32") == 0) return 22;
    // The 2021 ESP32 bags: the classic fanny pack and the standard six-LED backpack.
    if (std::strcmp(model, "NEBULITE Fanny pack ESP32") == 0) return 16;
    if (std::strcmp(model, "NEBULITE Backpack ESP32") == 0) return 18;
    return 0;
}

/// Append-only wire identities, independent of catalog and picker ordering.
inline constexpr NamedCode scenes[] = {
    {1, "solid"},
    {2, "candle-multi"},
    {3, "colortwinkles"},
    {4, "freqmap-amber"},
    {5, "lake"},
    {6, "loading"},
    {7, "pink-palette"},
    {8, "phased"},
    {9, "ripple"},
    {10, "running"},
    {11, "starburst"},
    {12, "gravcenter"},
    {13, "juggles"},
    {14, "multi-comet"},
    {15, "noisemeter"},
    {16, "plasmoid"},
    {17, "freqmap-wide"},
    {18, "geq"},
    {19, "gravfreq"},
    {20, "freqwave"},
    {21, "orbiting-lobes"},
    {22, "harmonic-halo"},
    {23, "liquid-mirror"},
    {24, "twin-suns"},
};
inline constexpr NamedCode palettes[] = {
    {0, "Rainbow"},
    {1, "Party"},
    {2, "Lava"},
    {3, "Ocean"},
    {4, "Forest"},
    {5, "Fierce Ice"},
    {6, "Sunset"},
    {7, "Sunset 2"},
    {8, "Orange & Teal"},
    {9, "Aurora"},
    {10, "Aurora 2"},
    {11, "Atlantica"},
    {12, "Analogous"},
    {13, "April Night"},
    {14, "Aqua Flash"},
    {15, "Autumn"},
    {16, "Beech"},
    {17, "Blink Red"},
    {18, "C9"},
    {19, "C9 2"},
    {20, "C9 New"},
    {21, "Candy"},
    {22, "Candy2"},
    {23, "Colorfull"},
    {24, "Departure"},
    {25, "Drywet"},
    {26, "Fairy Reaf"},
    {27, "Grintage"},
    {28, "Hult"},
    {29, "Hult 64"},
    {30, "Jul"},
    {31, "Landscape"},
    {32, "Light Pink"},
    {33, "Lite Light"},
    {34, "Magenta"},
    {35, "Magred"},
    {36, "Orangery"},
    {37, "Pastel"},
    {38, "Pink Candy"},
    {39, "Red & Blue"},
    {40, "Red Flash"},
    {41, "Red Reaf"},
    {42, "Red Shift"},
    {43, "Red Tide"},
    {44, "Retro Clown"},
    {45, "Rewhi"},
    {46, "Rivendell"},
    {47, "Sakura"},
    {48, "Semi Blue"},
    {49, "Sherbet"},
    {50, "Splash"},
    {51, "Temperature"},
    {52, "Tertiary"},
    {53, "Tiamat"},
    {54, "Toxy Reaf"},
    {55, "Vintage"},
    {56, "Yelblu Hot"},
    {57, "Yelblu"},
    {58, "Yelmag"},
    {59, "Yellowout"},
};
inline uint8_t sceneCode(const char* id) {
    for (const auto& entry : scenes) if (id && std::strcmp(id, entry.name) == 0) return entry.code;
    return 0;
}
inline uint8_t paletteCode(const char* name) {
    for (const auto& entry : palettes) if (name && std::strcmp(name, entry.name) == 0) return entry.code;
    return 255;
}
}  // namespace mm::ble_advertisement
