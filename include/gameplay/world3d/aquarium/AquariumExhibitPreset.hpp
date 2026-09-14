#pragma once

#include <array>
#include <string_view>

namespace pr::gameplay::world3d::aquarium {

struct AquariumExhibitPreset {
    std::string_view id;
    std::string_view display_name;
    std::string_view description;
    std::array<float, 4> water_volume;
    std::array<float, 4> water_surface;
    std::array<float, 3> spill_color;
    float attenuation_multiplier = 1.0f;
    float spill_opacity_multiplier = 1.0f;
    // Interior grading is deliberately separate from the water shader. It is
    // applied only to player-tank substrate and residents so the surrounding
    // room and authored aquariums retain their map presentation.
    std::array<float, 3> sand_tint{1.0f, 1.0f, 1.0f};
    float sand_brightness_multiplier = 1.0f;
    std::array<float, 3> pokemon_tint{1.0f, 1.0f, 1.0f};
    float pokemon_brightness_multiplier = 1.0f;
};

inline constexpr std::array<AquariumExhibitPreset, 4> kAquariumExhibitPresets{{
    {"river", "RIVER", "Clear moving water", {0.10f, 0.48f, 0.68f, 0.11f},
        {0.24f, 0.73f, 0.87f, 0.32f}, {0.18f, 0.58f, 0.86f}, 1.0f, 1.0f,
        {1.0f, 1.0f, 1.0f}, 1.0f, {1.0f, 1.0f, 1.0f}, 1.0f},
    {"swamp", "SWAMP", "Green and murky", {0.055f, 0.25f, 0.075f, 0.25f},
        {0.24f, 0.47f, 0.10f, 0.45f}, {0.24f, 0.54f, 0.16f}, 1.0f, 0.78f,
        {0.92f, 1.0f, 0.86f}, 1.0f, {0.88f, 1.0f, 0.82f}, 1.0f},
    {"open-ocean", "OPEN OCEAN", "Bright blue water", {0.015f, 0.18f, 0.72f, 0.16f},
        {0.04f, 0.42f, 1.0f, 0.38f}, {0.035f, 0.32f, 1.0f}, 1.0f, 1.15f,
        {0.82f, 0.90f, 1.0f}, 1.0f, {0.82f, 0.93f, 1.08f}, 1.0f},
    {"depths", "DEPTHS", "Dark abyssal water", {0.001f, 0.004f, 0.018f, 0.52f},
        {0.008f, 0.028f, 0.075f, 0.43f}, {0.006f, 0.028f, 0.11f}, 1.0f, 0.28f,
        {0.50f, 0.62f, 0.85f}, 1.0f, {0.55f, 0.68f, 0.90f}, 1.0f},
}};

inline const AquariumExhibitPreset& aquariumExhibitPreset(std::string_view id) {
    for (const auto& preset : kAquariumExhibitPresets) {
        if (preset.id == id) return preset;
    }
    return kAquariumExhibitPresets.front();
}

inline bool isAquariumExhibitPreset(std::string_view id) {
    for (const auto& preset : kAquariumExhibitPresets) {
        if (preset.id == id) return true;
    }
    return false;
}

inline int aquariumExhibitDefaultBrightnessLevel(std::string_view id) {
    if (id == "depths") return 0;
    if (id == "swamp") return 3;
    if (id == "open-ocean") return 5;
    return 4;
}

} // namespace pr::gameplay::world3d::aquarium
