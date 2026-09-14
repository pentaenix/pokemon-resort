#include "gameplay/world3d/aquarium/AquariumSpeciesCatalog.hpp"

#include "core/config/Json.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace pr::gameplay::world3d::aquarium {
namespace {

const JsonValue& required(const JsonValue& object, const char* key) {
    const JsonValue* value = object.get(key);
    if (!value) throw std::runtime_error(std::string("Missing required field: ") + key);
    return *value;
}

std::string stringValue(const JsonValue& object, const char* key) {
    const JsonValue& value = required(object, key);
    if (!value.isString()) throw std::runtime_error(std::string("Expected string: ") + key);
    return value.asString();
}

double numberValue(const JsonValue& object, const char* key, double fallback) {
    const JsonValue* value = object.get(key);
    if (!value) return fallback;
    if (!value->isNumber() || !std::isfinite(value->asNumber())) {
        throw std::runtime_error(std::string("Expected finite number: ") + key);
    }
    return value->asNumber();
}

int integerValue(const JsonValue& object, const char* key, int fallback) {
    const double value = numberValue(object, key, static_cast<double>(fallback));
    if (std::floor(value) != value || value < std::numeric_limits<int>::min() ||
        value > std::numeric_limits<int>::max()) {
        throw std::runtime_error(std::string("Expected integer: ") + key);
    }
    return static_cast<int>(value);
}

bool boolValue(const JsonValue& object, const char* key, bool fallback) {
    const JsonValue* value = object.get(key);
    if (!value) return fallback;
    if (!value->isBool()) throw std::runtime_error(std::string("Expected boolean: ") + key);
    return value->asBool();
}

std::vector<std::string> stringArray(const JsonValue& object, const char* key) {
    const JsonValue& value = required(object, key);
    if (!value.isArray()) throw std::runtime_error(std::string("Expected array: ") + key);
    std::vector<std::string> result;
    for (const JsonValue& item : value.asArray()) {
        if (!item.isString()) throw std::runtime_error(std::string("Expected string items: ") + key);
        result.push_back(item.asString());
    }
    return result;
}

struct CapacityClearancePolicy {
    int extra_columns = 0;
    int extra_rows = 0;
    int large_mask_minimum_cells = std::numeric_limits<int>::max();
    int large_extra_columns = 0;
    int large_extra_rows = 0;
    std::set<std::string> unexpanded_species;
};

AquariumSpeciesActivity parseActivity(
    const JsonValue& value,
    AquariumSpeciesActivity activity = {}) {
    if (!value.isObject()) throw std::runtime_error("Aquarium activity must be an object");
    const auto range = [&](const char* key, float& minimum, float& maximum) {
        const JsonValue* authored = value.get(key);
        if (!authored) return;
        if (!authored->isObject()) {
            throw std::runtime_error(std::string("Aquarium activity range must be an object: ") + key);
        }
        minimum = static_cast<float>(numberValue(*authored, "minimum", minimum));
        maximum = static_cast<float>(numberValue(*authored, "maximum", maximum));
    };
    range("moveSeconds", activity.move_seconds_minimum, activity.move_seconds_maximum);
    range("restSeconds", activity.rest_seconds_minimum, activity.rest_seconds_maximum);
    activity.roaming_height_meters = static_cast<float>(numberValue(
        value, "roamingHeightMeters", activity.roaming_height_meters));
    activity.crowd_body_scale = static_cast<float>(numberValue(
        value, "crowdBodyScale", activity.crowd_body_scale));
    activity.rest_at_bottom = boolValue(
        value, "restAtBottom", activity.rest_at_bottom);
    if (activity.move_seconds_minimum < 0.0f ||
        activity.move_seconds_maximum < activity.move_seconds_minimum ||
        activity.rest_seconds_minimum < 0.0f ||
        activity.rest_seconds_maximum < activity.rest_seconds_minimum ||
        activity.roaming_height_meters < 0.0f ||
        activity.crowd_body_scale < 0.35f || activity.crowd_body_scale > 1.0f) {
        throw std::runtime_error("Invalid aquarium activity policy");
    }
    return activity;
}

std::map<std::string, AquariumSpeciesActivity> parseMovementProfiles(
    const JsonValue& root) {
    std::map<std::string, AquariumSpeciesActivity> profiles;
    const JsonValue* authored = root.get("movementProfiles");
    if (!authored) return profiles;
    if (!authored->isObject()) {
        throw std::runtime_error("movementProfiles must be an object");
    }
    for (const auto& [name, value] : authored->asObject()) {
        profiles.emplace(name, parseActivity(value));
    }
    return profiles;
}

CapacityClearancePolicy parseCapacityClearance(const JsonValue& root) {
    CapacityClearancePolicy policy;
    const JsonValue* value = root.get("capacityClearance");
    if (!value) return policy;
    if (!value->isObject()) {
        throw std::runtime_error("capacityClearance must be an object");
    }
    policy.extra_columns = integerValue(*value, "extraColumns", 0);
    policy.extra_rows = integerValue(*value, "extraRows", 0);
    policy.large_mask_minimum_cells = integerValue(
        *value, "largeMaskMinimumCells", policy.large_mask_minimum_cells);
    policy.large_extra_columns = integerValue(
        *value, "largeExtraColumns", policy.extra_columns);
    policy.large_extra_rows = integerValue(
        *value, "largeExtraRows", policy.extra_rows);
    if (policy.extra_columns < 0 || policy.extra_columns > 4 ||
        policy.extra_rows < 0 || policy.extra_rows > 4 ||
        policy.large_mask_minimum_cells < 1 ||
        policy.large_extra_columns < policy.extra_columns ||
        policy.large_extra_columns > 4 ||
        policy.large_extra_rows < policy.extra_rows ||
        policy.large_extra_rows > 4) {
        throw std::runtime_error("capacityClearance expansion must be between zero and four cells");
    }
    if (value->get("unexpandedSpecies")) {
        const auto species = stringArray(*value, "unexpandedSpecies");
        policy.unexpanded_species.insert(species.begin(), species.end());
    }
    return policy;
}

std::vector<std::string> expandCapacityMask(
    const std::vector<std::string>& source,
    int extra_columns,
    int extra_rows) {
    if (source.empty() || (extra_columns == 0 && extra_rows == 0)) return source;
    int source_width = 0;
    for (const std::string& row : source) {
        source_width = std::max(source_width, static_cast<int>(row.size()));
    }
    std::vector<std::string> expanded(
        source.size() + static_cast<std::size_t>(extra_rows),
        std::string(static_cast<std::size_t>(source_width + extra_columns), '0'));
    for (int row = 0; row < static_cast<int>(source.size()); ++row) {
        for (int column = 0; column < static_cast<int>(source[row].size()); ++column) {
            if (source[row][column] != '1') continue;
            for (int dy = 0; dy <= extra_rows; ++dy) {
                for (int dx = 0; dx <= extra_columns; ++dx) {
                    expanded[row + dy][column + dx] = '1';
                }
            }
        }
    }
    return expanded;
}

AquariumSpeciesEntry parseApprovedEntry(
    const JsonValue& value,
    const CapacityClearancePolicy& clearance,
    const std::map<std::string, AquariumSpeciesActivity>& movement_profiles) {
    AquariumSpeciesEntry entry;
    entry.id = stringValue(value, "id");
    entry.dex = integerValue(value, "dex", 0);
    entry.species = stringValue(value, "species");
    entry.display_name = stringValue(value, "displayName");
    entry.form = stringValue(value, "form");
    const JsonValue& model = required(value, "model");
    const JsonValue& presentation = required(value, "presentation");
    const JsonValue& habitat = required(value, "habitat");
    const JsonValue& behavior = required(value, "behavior");
    const JsonValue& capacity = required(value, "capacity");
    entry.model_path = stringValue(model, "path");
    entry.animation = stringValue(presentation, "animation");
    entry.pitch_degrees = static_cast<float>(numberValue(presentation, "pitchDegrees", 0.0));
    entry.yaw_degrees = static_cast<float>(numberValue(presentation, "yawDegrees", 0.0));
    entry.scale_multiplier = static_cast<float>(numberValue(presentation, "scaleMultiplier", 1.0));
    entry.vertical_zone = stringValue(habitat, "verticalZone");
    entry.surface_behavior = stringValue(habitat, "surfaceBehavior");
    entry.water_kinds = stringArray(habitat, "waterKinds");
    entry.movement_profile = stringValue(behavior, "movementProfile");
    if (const auto profile = movement_profiles.find(entry.movement_profile);
        profile != movement_profiles.end()) {
        entry.activity = profile->second;
    }
    if (const JsonValue* activity = behavior.get("activity")) {
        entry.activity = parseActivity(*activity, entry.activity);
    }
    entry.travel_direction = behavior.get("travelDirection")
        ? stringValue(behavior, "travelDirection") : "forward";
    entry.idle_animation = behavior.get("idleAnimation")
        ? stringValue(behavior, "idleAnimation") : std::string{};
    entry.idle_pitch_degrees = static_cast<float>(numberValue(behavior, "idlePitchDegrees", 0.0));
    entry.minimum_group = integerValue(behavior, "minimumGroup", 1);
    entry.preferred_group = integerValue(behavior, "preferredGroup", entry.minimum_group);
    entry.random_start = boolValue(behavior, "randomStart", false);
    entry.idle_seconds_minimum = static_cast<float>(
        numberValue(behavior, "idleSecondsMinimum", 0.0));
    entry.idle_seconds_maximum = static_cast<float>(
        numberValue(behavior, "idleSecondsMaximum", entry.idle_seconds_minimum));
    entry.local_move_distance_meters = static_cast<float>(
        numberValue(behavior, "localMoveDistanceMeters", 0.0));
    entry.flee_radius_meters = static_cast<float>(
        numberValue(behavior, "fleeRadiusMeters", 0.0));
    entry.flee_distance_meters = static_cast<float>(
        numberValue(behavior, "fleeDistanceMeters", 0.0));
    entry.flee_speed_multiplier = static_cast<float>(
        numberValue(behavior, "fleeSpeedMultiplier", 1.0));
    if (behavior.get("threatSpecies")) {
        entry.threat_species = stringArray(behavior, "threatSpecies");
    }
    entry.capacity_mask = stringArray(capacity, "mask");
    if (clearance.unexpanded_species.find(entry.species) ==
        clearance.unexpanded_species.end()) {
        const bool large = entry.capacityCellCount() >=
            clearance.large_mask_minimum_cells;
        entry.capacity_mask = expandCapacityMask(
            entry.capacity_mask,
            large ? clearance.large_extra_columns : clearance.extra_columns,
            large ? clearance.large_extra_rows : clearance.extra_rows);
    }
    if (const JsonValue* envelope = value.get("physicalEnvelope")) {
        if (!envelope->isObject()) throw std::runtime_error("physicalEnvelope must be an object");
        entry.physical_envelope.version = integerValue(*envelope, "version", 0);
        entry.physical_envelope.source_signature = stringValue(*envelope, "sourceSignature");
        entry.physical_envelope.sampled_poses = integerValue(*envelope, "sampledPoses", 0);
        const JsonValue& bounds = required(*envelope, "boundsModelUnits");
        if (!bounds.isArray() || bounds.asArray().size() != 6U) {
            throw std::runtime_error("physicalEnvelope.boundsModelUnits must contain six numbers");
        }
        float* values[] = {
            &entry.physical_envelope.min_x, &entry.physical_envelope.max_x,
            &entry.physical_envelope.min_y, &entry.physical_envelope.max_y,
            &entry.physical_envelope.min_z, &entry.physical_envelope.max_z};
        for (std::size_t index = 0; index < 6U; ++index) {
            if (!bounds.asArray()[index].isNumber() ||
                !std::isfinite(bounds.asArray()[index].asNumber())) {
                throw std::runtime_error("physicalEnvelope bounds must be finite numbers");
            }
            *values[index] = static_cast<float>(bounds.asArray()[index].asNumber());
        }
        entry.physical_envelope.valid = entry.physical_envelope.version == 1 &&
            entry.physical_envelope.sampled_poses > 0 &&
            entry.physical_envelope.min_x < entry.physical_envelope.max_x &&
            entry.physical_envelope.min_y < entry.physical_envelope.max_y &&
            entry.physical_envelope.min_z < entry.physical_envelope.max_z &&
            entry.physical_envelope.source_signature ==
                entry.physicalEnvelopeSourceSignature();
    }
    return entry;
}

} // namespace

int AquariumSpeciesEntry::capacityCellCount() const {
    int count = 0;
    for (const std::string& row : capacity_mask) {
        count += static_cast<int>(std::count(row.begin(), row.end(), '1'));
    }
    return count;
}

std::string AquariumSpeciesEntry::physicalEnvelopeSourceSignature() const {
    std::ostringstream source;
    source.precision(9);
    source << model_path << '|' << form << '|' << animation << '|' << idle_animation
           << '|' << pitch_degrees << '|' << idle_pitch_degrees << '|'
           << yaw_degrees << '|' << scale_multiplier;
    const std::string value = source.str();
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char byte : value) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    std::ostringstream result;
    result << std::hex << hash;
    return result.str();
}

const AquariumSpeciesEntry* AquariumSpeciesCatalog::findApproved(
    const std::string& id) const {
    const auto found = std::find_if(approved.begin(), approved.end(),
        [&](const auto& entry) { return entry.id == id; });
    return found == approved.end() ? nullptr : &*found;
}

AquariumSpeciesCatalogLoadResult loadAquariumSpeciesCatalog(
    const std::filesystem::path& path) {
    AquariumSpeciesCatalogLoadResult result;
    try {
        const JsonValue root = parseJsonFile(path.string());
        if (!root.isObject() || stringValue(root, "schema") != kAquariumSpeciesCatalogSchema) {
            throw std::runtime_error("Unrecognized aquarium species catalogue schema");
        }
        if (integerValue(root, "schemaVersion", 0) != 1) {
            throw std::runtime_error("Unsupported aquarium species catalogue version");
        }
        const double revision = numberValue(root, "catalogRevision", 0.0);
        if (revision < 0.0 || std::floor(revision) != revision) {
            throw std::runtime_error("Invalid aquarium species catalogue revision");
        }
        result.catalog.revision = static_cast<std::uint64_t>(revision);
        const CapacityClearancePolicy capacity_clearance = parseCapacityClearance(root);
        const auto movement_profiles = parseMovementProfiles(root);
        const JsonValue& entries = required(root, "entries");
        if (!entries.isArray()) throw std::runtime_error("Catalogue entries must be an array");
        std::set<std::string> ids;
        for (const JsonValue& value : entries.asArray()) {
            if (!value.isObject()) throw std::runtime_error("Catalogue entry must be an object");
            const JsonValue& review = required(value, "review");
            if (stringValue(review, "status") != "approved") continue;
            AquariumSpeciesEntry entry = parseApprovedEntry(
                value, capacity_clearance, movement_profiles);
            if (entry.id.empty() || entry.dex <= 0 || entry.species.empty() ||
                entry.model_path.empty() || entry.animation.empty() ||
                entry.scale_multiplier <= 0.0f || entry.minimum_group < 1 ||
                entry.preferred_group < entry.minimum_group ||
                entry.idle_seconds_minimum < 0.0f ||
                entry.idle_seconds_maximum < entry.idle_seconds_minimum ||
                entry.local_move_distance_meters < 0.0f ||
                entry.flee_radius_meters < 0.0f ||
                entry.flee_distance_meters < 0.0f ||
                entry.flee_speed_multiplier < 1.0f ||
                entry.capacityCellCount() < 1 || !ids.insert(entry.id).second) {
                throw std::runtime_error("Invalid approved aquarium species entry: " + entry.id);
            }
            result.catalog.approved.push_back(std::move(entry));
        }
        result.valid = true;
    } catch (const std::exception& error) {
        result.diagnostics.push_back(error.what());
    }
    return result;
}

} // namespace pr::gameplay::world3d::aquarium
