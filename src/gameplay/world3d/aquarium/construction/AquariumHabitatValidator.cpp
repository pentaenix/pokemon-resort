#include "gameplay/world3d/aquarium/construction/AquariumHabitatValidator.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace pr::gameplay::world3d::aquarium::construction {
namespace {

constexpr float kWorldUnitsPerMeter = 16.0f;
constexpr float kGlassComfortMeters = 0.12f;

bool isFloorProfile(const std::string& profile) {
    return profile == "bottom-crawler" || profile == "bottom-stationary" ||
        profile == "bottom-burrower" || profile == "anchored" ||
        profile == "bottom-swimmer" || profile == "timid-reef";
}

bool isSurfaceProfile(const AquariumSpeciesEntry& species) {
    return species.movement_profile == "surface-walker" ||
        species.vertical_zone == "surface";
}

std::array<float, 4> horizontalBounds(const AquariumNavigation& navigation) {
    std::array<float, 4> bounds{
        std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest()};
    for (const auto& layer : navigation.layers) {
        for (const auto& polygon : layer.polygons) {
            if (polygon.empty()) continue;
            for (const Point2 point : polygon.front()) {
                bounds[0] = std::min(bounds[0], point[0]);
                bounds[1] = std::max(bounds[1], point[0]);
                bounds[2] = std::min(bounds[2], point[1]);
                bounds[3] = std::max(bounds[3], point[1]);
            }
        }
    }
    return bounds;
}

std::array<float, 2> verticalBounds(const AquariumNavigation& navigation) {
    std::array<float, 2> bounds{
        std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest()};
    for (const auto& layer : navigation.layers) {
        bounds[0] = std::min(bounds[0], layer.y_bottom);
        bounds[1] = std::max(bounds[1], layer.y_top);
    }
    return bounds;
}

bool bodyFitsAt(
    const AquariumNavigation& navigation,
    Point3 position,
    float radius,
    float lower_extent,
    float upper_extent,
    bool surface) {
    if (!containsPoint(navigation, position, radius)) return false;
    Point3 bottom = position;
    bottom[1] += lower_extent;
    if (!containsPoint(navigation, bottom, radius)) return false;
    if (surface) return true;
    Point3 top = position;
    top[1] += upper_extent;
    return containsPoint(navigation, top, radius);
}

} // namespace

AquariumHabitatFit validateAquariumHabitat(
    const AquariumSpeciesEntry& species,
    const AquariumNavigation& navigation,
    float pokemon_model_scale) {
    AquariumHabitatFit result;
    const auto& envelope = species.physical_envelope;
    if (!envelope.valid || !navigation.valid || pokemon_model_scale <= 0.0f) return result;

    const float scale = pokemon_model_scale * species.scale_multiplier / kWorldUnitsPerMeter;
    const float half_width = std::max(std::abs(envelope.min_x), std::abs(envelope.max_x)) * scale;
    const bool large_cruiser = species.movement_profile == "large-cruiser";
    constexpr float kPi = 3.14159265358979323846f;
    const float swim_pitch = (large_cruiser ? 10.0f : 6.0f) * kPi / 180.0f;
    float oriented_min_y = envelope.min_y;
    float oriented_max_y = envelope.max_y;
    float oriented_min_z = envelope.min_z;
    float oriented_max_z = envelope.max_z;
    for (const float angle : {-swim_pitch, swim_pitch}) {
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        for (const float y : {envelope.min_y, envelope.max_y}) {
            for (const float z : {envelope.min_z, envelope.max_z}) {
                oriented_min_y = std::min(oriented_min_y, cosine * y - sine * z);
                oriented_max_y = std::max(oriented_max_y, cosine * y - sine * z);
                oriented_min_z = std::min(oriented_min_z, sine * y + cosine * z);
                oriented_max_z = std::max(oriented_max_z, sine * y + cosine * z);
            }
        }
    }
    const float half_length = std::max(
        std::abs(oriented_min_z), std::abs(oriented_max_z)) * scale;
    const float swept_radius = large_cruiser
        ? std::hypot(half_width, half_length)
        : std::max(half_width, half_length);
    result.body_radius_meters = swept_radius + kGlassComfortMeters;
    const float lower_extent = oriented_min_y * scale;
    const float upper_extent = oriented_max_y * scale;
    result.body_height_meters = upper_extent - lower_extent;

    const auto horizontal = horizontalBounds(navigation);
    const auto vertical = verticalBounds(navigation);
    if (horizontal[1] - horizontal[0] < result.body_radius_meters * 2.0f ||
        horizontal[3] - horizontal[2] < result.body_radius_meters * 2.0f) {
        result.reason = AquariumHabitatFitReason::HorizontalClearance;
        return result;
    }
    const bool surface = isSurfaceProfile(species);
    if (!surface && vertical[1] - vertical[0] < result.body_height_meters + 0.04f) {
        result.reason = AquariumHabitatFitReason::VerticalClearance;
        return result;
    }

    const bool floor = isFloorProfile(species.movement_profile);
    const float step = std::clamp(result.body_radius_meters * 0.45f, 0.10f, 0.28f);
    std::vector<Point3> candidates;
    for (float z = horizontal[2]; z <= horizontal[3] + 0.001f; z += step) {
        for (float x = horizontal[0]; x <= horizontal[1] + 0.001f; x += step) {
            float y = (vertical[0] + vertical[1]) * 0.5f;
            if (floor) y = vertical[0] - lower_extent + 0.02f;
            else if (surface) y = vertical[1] - std::min(0.0f, upper_extent) - 0.02f;
            else y = std::clamp(y, vertical[0] - lower_extent + 0.02f,
                vertical[1] - upper_extent - 0.02f);
            const Point3 position{x, y, z};
            if (bodyFitsAt(navigation, position, result.body_radius_meters,
                    lower_extent, upper_extent, surface)) {
                candidates.push_back(position);
            }
        }
    }
    if (candidates.empty()) {
        result.reason = AquariumHabitatFitReason::HorizontalClearance;
        return result;
    }
    result.preview_position = candidates.front();
    if (large_cruiser) {
        const float required_route = std::max(0.8f, result.body_radius_meters * 1.5f);
        bool route_found = false;
        for (std::size_t first = 0; first < candidates.size() && !route_found; ++first) {
            for (std::size_t second = first + 1; second < candidates.size(); ++second) {
                const float dx = candidates[first][0] - candidates[second][0];
                const float dz = candidates[first][2] - candidates[second][2];
                if (std::hypot(dx, dz) < required_route) continue;
                if (segmentIsNavigable(navigation, candidates[first], candidates[second],
                        result.body_radius_meters)) {
                    route_found = true;
                    break;
                }
            }
        }
        if (!route_found) {
            result.reason = AquariumHabitatFitReason::TurningSpace;
            return result;
        }
    }
    result.reason = AquariumHabitatFitReason::Fits;
    return result;
}

} // namespace pr::gameplay::world3d::aquarium::construction
