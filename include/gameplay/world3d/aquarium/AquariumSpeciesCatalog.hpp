#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pr::gameplay::world3d::aquarium {

inline constexpr const char* kAquariumSpeciesCatalogSchema =
    "pokemon-resort-aquarium-species";

struct AquariumSpeciesPhysicalEnvelope {
    int version = 0;
    std::string source_signature;
    float min_x = 0.0f;
    float max_x = 0.0f;
    float min_y = 0.0f;
    float max_y = 0.0f;
    float min_z = 0.0f;
    float max_z = 0.0f;
    int sampled_poses = 0;
    bool valid = false;
};

struct AquariumSpeciesActivity {
    float move_seconds_minimum = 0.0f;
    float move_seconds_maximum = 0.0f;
    float rest_seconds_minimum = 0.0f;
    float rest_seconds_maximum = 0.0f;
    float roaming_height_meters = 0.0f;
    float crowd_body_scale = 0.68f;
    bool rest_at_bottom = false;

    bool intermittent() const {
        return move_seconds_maximum > 0.0f && rest_seconds_maximum > 0.0f;
    }
};

struct AquariumSpeciesEntry {
    std::string id;
    int dex = 0;
    std::string species;
    std::string display_name;
    std::string form = "00";
    std::string model_path;
    std::string animation;
    float pitch_degrees = 0.0f;
    float yaw_degrees = 0.0f;
    float scale_multiplier = 1.0f;
    std::string vertical_zone = "open-water";
    std::string surface_behavior = "submerged";
    std::string movement_profile = "free-swimmer";
    std::string travel_direction = "forward";
    std::string idle_animation;
    float idle_pitch_degrees = 0.0f;
    int minimum_group = 1;
    int preferred_group = 1;
    bool random_start = false;
    float idle_seconds_minimum = 0.0f;
    float idle_seconds_maximum = 0.0f;
    float local_move_distance_meters = 0.0f;
    float flee_radius_meters = 0.0f;
    float flee_distance_meters = 0.0f;
    float flee_speed_multiplier = 1.0f;
    std::vector<std::string> threat_species;
    std::vector<std::string> water_kinds;
    std::vector<std::string> capacity_mask;
    AquariumSpeciesPhysicalEnvelope physical_envelope;
    AquariumSpeciesActivity activity;

    int capacityCellCount() const;
    std::string physicalEnvelopeSourceSignature() const;
};

struct AquariumSpeciesCatalog {
    std::uint64_t revision = 0;
    std::vector<AquariumSpeciesEntry> approved;

    const AquariumSpeciesEntry* findApproved(const std::string& id) const;
};

struct AquariumSpeciesCatalogLoadResult {
    AquariumSpeciesCatalog catalog;
    bool valid = false;
    std::vector<std::string> diagnostics;
};

AquariumSpeciesCatalogLoadResult loadAquariumSpeciesCatalog(
    const std::filesystem::path& path);

} // namespace pr::gameplay::world3d::aquarium
