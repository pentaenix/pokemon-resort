#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace pr::gameplay::world3d::aquarium {

struct AquariumSubstratePreset {
    std::string_view kind;
    std::string_view display_name;
    int resort_tile_id = -1;
    std::array<unsigned char, 3> swatch_base{};
    std::array<unsigned char, 3> swatch_detail{};
};

// These are flat, repeatable 1x1 ground materials from the Black 2 tile set
// already shipped in maptiles.rtpks. The stable tile id is the source of truth;
// previews are drawn procedurally so UI code does not duplicate game textures.
inline constexpr std::array<AquariumSubstratePreset, 4> kAquariumSubstratePresets{{
    {"sand-flat", "SAND", 103, {218, 199, 142}, {245, 229, 178}},
    {"gravel-flat", "GRAVEL", 102, {126, 111, 83}, {181, 160, 113}},
    {"moss-flat", "MOSS", 104, {91, 112, 64}, {119, 151, 75}},
    {"dirt-flat", "DIRT", 104, {128, 94, 58}, {181, 139, 79}},
}};

inline constexpr int kAquariumBrightnessLevelCount = 9;
inline constexpr int kAquariumDefaultBrightnessLevel = 4;
inline constexpr int kAquariumMurkinessLevelCount = 9;
// Nine player-facing ticks span the useful part of the original curve. The
// final tick is intentionally equivalent to the former level 5; the denser
// former 6..8 values made most of the slider unusably opaque.
inline constexpr int kAquariumMurkinessControlLevelCount =
    kAquariumMurkinessLevelCount;
inline constexpr int kAquariumDefaultMurkinessLevel = 2;

inline const AquariumSubstratePreset& aquariumSubstratePreset(std::string_view kind) {
    for (const auto& preset : kAquariumSubstratePresets) {
        if (preset.kind == kind) return preset;
    }
    return kAquariumSubstratePresets.front();
}

inline bool isAquariumSubstratePreset(std::string_view kind) {
    for (const auto& preset : kAquariumSubstratePresets) {
        if (preset.kind == kind) return true;
    }
    return false;
}

inline std::size_t aquariumSubstratePresetIndex(std::string_view kind) {
    for (std::size_t index = 0; index < kAquariumSubstratePresets.size(); ++index) {
        if (kAquariumSubstratePresets[index].kind == kind) return index;
    }
    return 0;
}

inline float aquariumBrightnessMultiplier(int level) {
    constexpr std::array<float, kAquariumBrightnessLevelCount> values{
        0.16f, 0.30f, 0.48f, 0.70f, 1.0f, 1.12f, 1.24f, 1.36f, 1.50f};
    if (level < 0) level = 0;
    if (level >= kAquariumBrightnessLevelCount) level = kAquariumBrightnessLevelCount - 1;
    return values[static_cast<std::size_t>(level)];
}

inline float aquariumMurkinessMultiplier(int level) {
    // This is the original 0..5 response resampled over nine UI ticks. Keep it
    // coordinated with the visibility and opacity tables in AquariumBoundedFog.
    constexpr std::array<float, kAquariumMurkinessLevelCount> values{
        0.15f, 0.3375f, 0.5875f, 0.93125f, 1.5f,
        2.3125f, 3.875f, 6.1875f, 9.0f};
    if (level < 0) level = 0;
    if (level >= kAquariumMurkinessLevelCount) level = kAquariumMurkinessLevelCount - 1;
    return values[static_cast<std::size_t>(level)];
}

} // namespace pr::gameplay::world3d::aquarium
