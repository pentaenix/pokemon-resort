#include "gameplay/world3d/aquarium/construction/AquariumDesign.hpp"

#include "aquarium_geometry/Kernel.hpp"
#include "core/config/Json.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace pr::gameplay::world3d::aquarium::construction {

namespace geometry = pr::aquarium::geometry;

namespace {

const JsonValue& required(const JsonValue& object, const char* key) {
    const JsonValue* value = object.get(key);
    if (value == nullptr) {
        throw std::runtime_error(std::string("Missing required field: ") + key);
    }
    return *value;
}

std::string requiredString(const JsonValue& object, const char* key) {
    const JsonValue& value = required(object, key);
    if (!value.isString()) {
        throw std::runtime_error(std::string("Expected string: ") + key);
    }
    return value.asString();
}

std::int64_t requiredInteger(const JsonValue& object, const char* key) {
    const JsonValue& value = required(object, key);
    if (!value.isNumber() || !std::isfinite(value.asNumber()) ||
        std::floor(value.asNumber()) != value.asNumber()) {
        throw std::runtime_error(std::string("Expected integer: ") + key);
    }
    constexpr double kMaxExactJsonInteger = 9007199254740991.0;
    if (value.asNumber() < -kMaxExactJsonInteger || value.asNumber() > kMaxExactJsonInteger) {
        throw std::runtime_error(std::string("Integer exceeds exact JSON range: ") + key);
    }
    return static_cast<std::int64_t>(value.asNumber());
}

std::int32_t requiredInt32(const JsonValue& object, const char* key) {
    const std::int64_t value = requiredInteger(object, key);
    if (value < std::numeric_limits<std::int32_t>::min() ||
        value > std::numeric_limits<std::int32_t>::max()) {
        throw std::runtime_error(std::string("32-bit integer out of range: ") + key);
    }
    return static_cast<std::int32_t>(value);
}

std::int32_t optionalInt32(
    const JsonValue& object, const char* key, std::int32_t fallback) {
    return object.get(key) ? requiredInt32(object, key) : fallback;
}

geometry::FootprintShape parseShape(const std::string& value) {
    if (value == "rectangle") return geometry::FootprintShape::Rectangle;
    if (value == "l") return geometry::FootprintShape::L;
    if (value == "u") return geometry::FootprintShape::U;
    throw std::runtime_error("Unsupported footprint shape: " + value);
}

const char* shapeName(geometry::FootprintShape shape) {
    switch (shape) {
    case geometry::FootprintShape::Rectangle: return "rectangle";
    case geometry::FootprintShape::L: return "l";
    case geometry::FootprintShape::U: return "u";
    }
    return "unknown";
}

geometry::TunnelRoute parseRoute(const std::string& value) {
    if (value == "straight") return geometry::TunnelRoute::Straight;
    if (value == "one-elbow") return geometry::TunnelRoute::OneElbow;
    throw std::runtime_error("Unsupported tunnel route: " + value);
}

const char* routeName(geometry::TunnelRoute route) {
    return route == geometry::TunnelRoute::Straight ? "straight" : "one-elbow";
}

geometry::TankDesign parseTank(const JsonValue& value) {
    if (!value.isObject()) throw std::runtime_error("Tank entry must be an object");
    geometry::TankDesign tank;
    tank.id = requiredString(value, "id");
    const JsonValue& footprint = required(value, "footprint");
    if (!footprint.isObject()) throw std::runtime_error("Tank footprint must be an object");
    tank.footprint.shape = parseShape(requiredString(footprint, "shape"));
    const JsonValue& origin = required(footprint, "originCell");
    if (!origin.isObject()) throw std::runtime_error("originCell must be an object");
    tank.footprint.origin_cell.column = requiredInt32(origin, "column");
    tank.footprint.origin_cell.row = requiredInt32(origin, "row");
    tank.footprint.width_cells = requiredInt32(footprint, "widthCells");
    tank.footprint.depth_cells = requiredInt32(footprint, "depthCells");
    tank.footprint.rotation_quarter_turns = requiredInt32(footprint, "rotationQuarterTurns");
    if (const JsonValue* notch = footprint.get("notch")) {
        if (!notch->isObject()) throw std::runtime_error("notch must be an object");
        tank.footprint.notch_width_cells = requiredInt32(*notch, "widthCells");
        tank.footprint.notch_depth_cells = requiredInt32(*notch, "depthCells");
    }
    if (const JsonValue* subtracted = footprint.get("subtractedCells")) {
        if (!subtracted->isArray()) {
            throw std::runtime_error("subtractedCells must be an array");
        }
        for (const JsonValue& cell : subtracted->asArray()) {
            if (!cell.isObject()) {
                throw std::runtime_error("Subtracted footprint cell must be an object");
            }
            tank.footprint.subtracted_cells.push_back({
                requiredInt32(cell, "column"), requiredInt32(cell, "row")});
        }
    }
    tank.height_steps = requiredInt32(value, "heightSteps");
    tank.depth_steps = optionalInt32(value, "depthSteps", 0);
    tank.corner_radius_steps = requiredInt32(value, "cornerRadiusSteps");
    if (const JsonValue* radii = value.get("cornerRadii")) {
        if (!radii->isArray()) throw std::runtime_error("cornerRadii must be an array");
        for (const JsonValue& item : radii->asArray()) {
            if (!item.isObject()) throw std::runtime_error("Corner radius must be an object");
            const JsonValue& vertex = required(item, "vertex");
            if (!vertex.isObject()) throw std::runtime_error("Corner vertex must be an object");
            tank.corner_radii.push_back({
                {requiredInt32(vertex, "column"), requiredInt32(vertex, "row")},
                requiredInt32(item, "radiusSteps")});
        }
    }

    const JsonValue& substrate = required(value, "substrate");
    const JsonValue& glass = required(value, "glass");
    if (!substrate.isObject() || requiredString(substrate, "kind") != "sand-flat") {
        throw std::runtime_error("Version 1 requires sand-flat substrate");
    }
    if (!glass.isObject() || requiredString(glass, "style") != "clear-fixed-v1") {
        throw std::runtime_error("Version 1 requires clear-fixed-v1 glass");
    }

    const JsonValue& tunnels = required(value, "tunnels");
    if (!tunnels.isArray()) throw std::runtime_error("tunnels must be an array");
    for (const JsonValue& tunnel_value : tunnels.asArray()) {
        if (!tunnel_value.isObject()) throw std::runtime_error("Tunnel entry must be an object");
        geometry::TunnelDesign tunnel;
        tunnel.id = requiredString(tunnel_value, "id");
        tunnel.route = parseRoute(requiredString(tunnel_value, "route"));
        const JsonValue& points = required(tunnel_value, "centrelineCells");
        if (!points.isArray()) throw std::runtime_error("centrelineCells must be an array");
        for (const JsonValue& point : points.asArray()) {
            if (!point.isObject()) throw std::runtime_error("Tunnel point must be an object");
            tunnel.centreline_cells.push_back({requiredInt32(point, "column"), requiredInt32(point, "row")});
        }
        tank.tunnels.push_back(std::move(tunnel));
    }
    return tank;
}

void migrateLegacyTunnelGrid(geometry::TankDesign& tank) {
    if (!tank.tunnels.empty()) tank.height_steps = std::max(tank.height_steps, 6);
    for (auto& tunnel : tank.tunnels) {
        auto& points = tunnel.centreline_cells;
        if (points.size() < 2U) continue;
        const geometry::CellPoint first = points.front();
        const geometry::CellPoint second = points[1];
        const geometry::CellPoint entry_outward{
            first.column - second.column, first.row - second.row};
        if (entry_outward.column > 0 || entry_outward.row > 0) {
            points.insert(points.begin(), {
                first.column + entry_outward.column,
                first.row + entry_outward.row});
        }
        const geometry::CellPoint last = points.back();
        const geometry::CellPoint before_last = points[points.size() - 2U];
        const geometry::CellPoint exit_outward{
            last.column - before_last.column, last.row - before_last.row};
        if (exit_outward.column > 0 || exit_outward.row > 0) {
            points.push_back({
                last.column + exit_outward.column,
                last.row + exit_outward.row});
        }
    }
}

JsonValue serializePoint(const geometry::CellPoint& point) {
    return JsonValue(JsonValue::Object{
        {"column", JsonValue(static_cast<double>(point.column))},
        {"row", JsonValue(static_cast<double>(point.row))},
    });
}

JsonValue serializeTank(const geometry::TankDesign& tank) {
    JsonValue::Object footprint{
        {"depthCells", JsonValue(static_cast<double>(tank.footprint.depth_cells))},
        {"originCell", JsonValue(JsonValue::Object{
             {"column", JsonValue(static_cast<double>(tank.footprint.origin_cell.column))},
             {"row", JsonValue(static_cast<double>(tank.footprint.origin_cell.row))},
         })},
        {"rotationQuarterTurns", JsonValue(static_cast<double>(tank.footprint.rotation_quarter_turns))},
        {"shape", JsonValue(std::string(shapeName(tank.footprint.shape)))},
        {"widthCells", JsonValue(static_cast<double>(tank.footprint.width_cells))},
    };
    if (tank.footprint.shape != geometry::FootprintShape::Rectangle) {
        footprint.emplace("notch", JsonValue(JsonValue::Object{
            {"depthCells", JsonValue(static_cast<double>(tank.footprint.notch_depth_cells))},
            {"widthCells", JsonValue(static_cast<double>(tank.footprint.notch_width_cells))},
        }));
    }
    if (!tank.footprint.subtracted_cells.empty()) {
        JsonValue::Array subtracted;
        auto cells = tank.footprint.subtracted_cells;
        std::sort(cells.begin(), cells.end(), [](geometry::GridCell lhs, geometry::GridCell rhs) {
            return lhs.row < rhs.row || (lhs.row == rhs.row && lhs.column < rhs.column);
        });
        for (const geometry::GridCell cell : cells) {
            subtracted.emplace_back(JsonValue::Object{
                {"column", JsonValue(static_cast<double>(cell.column))},
                {"row", JsonValue(static_cast<double>(cell.row))},
            });
        }
        footprint.emplace("subtractedCells", JsonValue(std::move(subtracted)));
    }

    JsonValue::Array tunnels;
    for (const geometry::TunnelDesign& tunnel : tank.tunnels) {
        JsonValue::Array points;
        for (const geometry::CellPoint& point : tunnel.centreline_cells) {
            points.push_back(serializePoint(point));
        }
        tunnels.emplace_back(JsonValue::Object{
            {"centrelineCells", JsonValue(std::move(points))},
            {"id", JsonValue(tunnel.id)},
            {"route", JsonValue(std::string(routeName(tunnel.route)))},
        });
    }

    JsonValue::Array corner_radii;
    auto radii = tank.corner_radii;
    std::sort(radii.begin(), radii.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.vertex.row < rhs.vertex.row ||
            (lhs.vertex.row == rhs.vertex.row && lhs.vertex.column < rhs.vertex.column);
    });
    for (const auto& radius : radii) {
        corner_radii.emplace_back(JsonValue::Object{
            {"radiusSteps", JsonValue(static_cast<double>(radius.radius_steps))},
            {"vertex", JsonValue(JsonValue::Object{
                {"column", JsonValue(static_cast<double>(radius.vertex.column))},
                {"row", JsonValue(static_cast<double>(radius.vertex.row))},
            })},
        });
    }

    return JsonValue(JsonValue::Object{
        {"cornerRadii", JsonValue(std::move(corner_radii))},
        {"cornerRadiusSteps", JsonValue(static_cast<double>(tank.corner_radius_steps))},
        {"depthSteps", JsonValue(static_cast<double>(tank.depth_steps))},
        {"footprint", JsonValue(std::move(footprint))},
        {"glass", JsonValue(JsonValue::Object{{"style", JsonValue(std::string("clear-fixed-v1"))}})},
        {"heightSteps", JsonValue(static_cast<double>(tank.height_steps))},
        {"id", JsonValue(tank.id)},
        {"substrate", JsonValue(JsonValue::Object{{"kind", JsonValue(std::string("sand-flat"))}})},
        {"tunnels", JsonValue(std::move(tunnels))},
    });
}

} // namespace

std::vector<std::string> validateAquariumDesign(const AquariumDesignDocument& document) {
    std::vector<std::string> diagnostics;
    if (document.design_id.empty()) diagnostics.push_back("missing_design_id");
    if (document.map_id.empty()) diagnostics.push_back("missing_map_id");
    if (document.revision > 9007199254740991ULL) diagnostics.push_back("revision_exceeds_json_integer_range");
    if (document.population_policy.id != "placeholder-wishiwashi" ||
        document.population_policy.version != 1) {
        diagnostics.push_back("unsupported_population_policy");
    }
    std::set<std::string> ids;
    for (const geometry::TankDesign& tank : document.tanks) {
        if (tank.id.empty()) {
            diagnostics.push_back("missing_tank_id");
        } else if (!ids.insert(tank.id).second) {
            diagnostics.push_back("duplicate_tank_id:" + tank.id);
        }
        geometry::AquariumBuildRequest request;
        request.tank = tank;
        for (const geometry::ValidationDiagnostic& diagnostic : geometry::validateAquarium(request).diagnostics) {
            diagnostics.push_back(tank.id + ":" + diagnostic.code);
        }
    }
    return diagnostics;
}

AquariumDesignLoadResult parseAquariumDesign(const std::string& text) {
    AquariumDesignLoadResult result;
    try {
        const JsonValue root = parseJsonText(text);
        if (!root.isObject()) throw std::runtime_error("Aquarium design root must be an object");
        if (requiredString(root, "schema") != kAquariumDesignSchema) {
            throw std::runtime_error("Unrecognized aquarium design schema");
        }
        const std::int64_t version = requiredInteger(root, "schemaVersion");
        if (version > geometry::kDesignSchemaVersion) {
            result.status = AquariumDesignLoadStatus::NewerVersion;
            result.diagnostics.push_back("newer_schema_version");
            return result;
        }
        if (version < 1 || version > geometry::kDesignSchemaVersion) {
            throw std::runtime_error("No migration exists for aquarium design schema version " + std::to_string(version));
        }

        const JsonValue& grid = required(root, "grid");
        if (!grid.isObject() || requiredInt32(grid, "tileWorldUnits") != geometry::kWorldUnitsPerCell ||
            requiredInt32(grid, "verticalStepWorldUnits") != geometry::kVerticalStepWorldUnits ||
            requiredInt32(grid, "radiusStepWorldUnits") != geometry::kRadiusStepWorldUnits ||
            requiredString(grid, "origin") != "map-north-west" ||
            requiredString(grid, "cellConvention") != "integer-boundaries-half-cell-centres") {
            throw std::runtime_error("Aquarium grid contract does not match a supported version");
        }
        if (version >= 2) {
            const JsonValue& offset = required(grid, "placementOffsetCells");
            if (!offset.isArray() || offset.asArray().size() != 2U ||
                !offset.asArray()[0].isNumber() || !offset.asArray()[1].isNumber() ||
                offset.asArray()[0].asNumber() != 0.5 || offset.asArray()[1].asNumber() != 0.5) {
                throw std::runtime_error("Aquarium placement offset must be half a cell on both axes");
            }
        }
        const JsonValue& metres = required(grid, "metresPerCell");
        if (!metres.isNumber() || metres.asNumber() != 1.0) {
            throw std::runtime_error("Aquarium designs require one metre per cell");
        }

        AquariumDesignDocument document;
        document.design_id = requiredString(root, "designId");
        document.map_id = requiredString(root, "mapId");
        const std::int64_t revision = requiredInteger(root, "revision");
        if (revision < 0) throw std::runtime_error("Revision cannot be negative");
        document.revision = static_cast<std::uint64_t>(revision);
        const JsonValue& policy = required(root, "populationPolicy");
        if (!policy.isObject()) throw std::runtime_error("populationPolicy must be an object");
        document.population_policy.id = requiredString(policy, "id");
        const std::int64_t policy_version = requiredInteger(policy, "version");
        if (policy_version < 0 || policy_version > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("Population policy version out of range");
        }
        document.population_policy.version = static_cast<std::uint32_t>(policy_version);
        const JsonValue& tanks = required(root, "tanks");
        if (!tanks.isArray()) throw std::runtime_error("tanks must be an array");
        for (const JsonValue& tank : tanks.asArray()) {
            document.tanks.push_back(parseTank(tank));
            if (version < 5) migrateLegacyTunnelGrid(document.tanks.back());
        }

        result.diagnostics = validateAquariumDesign(document);
        if (!result.diagnostics.empty()) return result;
        result.status = AquariumDesignLoadStatus::Loaded;
        result.document = std::move(document);
    } catch (const std::exception& error) {
        result.diagnostics.push_back(error.what());
    }
    return result;
}

std::string serializeAquariumDesignCanonical(const AquariumDesignDocument& document) {
    const std::vector<std::string> diagnostics = validateAquariumDesign(document);
    if (!diagnostics.empty()) {
        throw std::runtime_error("Cannot serialize invalid aquarium design: " + diagnostics.front());
    }
    JsonValue::Array tanks;
    for (const geometry::TankDesign& tank : document.tanks) tanks.push_back(serializeTank(tank));
    JsonValue root(JsonValue::Object{
        {"designId", JsonValue(document.design_id)},
        {"grid", JsonValue(JsonValue::Object{
             {"cellConvention", JsonValue(std::string("integer-boundaries-half-cell-centres"))},
             {"metresPerCell", JsonValue(1.0)},
             {"origin", JsonValue(std::string("map-north-west"))},
             {"placementOffsetCells", JsonValue(JsonValue::Array{JsonValue(0.5), JsonValue(0.5)})},
             {"radiusStepWorldUnits", JsonValue(static_cast<double>(geometry::kRadiusStepWorldUnits))},
             {"tileWorldUnits", JsonValue(static_cast<double>(geometry::kWorldUnitsPerCell))},
             {"verticalStepWorldUnits", JsonValue(static_cast<double>(geometry::kVerticalStepWorldUnits))},
         })},
        {"mapId", JsonValue(document.map_id)},
        {"populationPolicy", JsonValue(JsonValue::Object{
             {"id", JsonValue(document.population_policy.id)},
             {"version", JsonValue(static_cast<double>(document.population_policy.version))},
         })},
        {"revision", JsonValue(static_cast<double>(document.revision))},
        {"schema", JsonValue(std::string(kAquariumDesignSchema))},
        {"schemaVersion", JsonValue(static_cast<double>(geometry::kDesignSchemaVersion))},
        {"tanks", JsonValue(std::move(tanks))},
    });
    return serializeJsonValue(root, JsonStyle::Pretty, 2) + "\n";
}

} // namespace pr::gameplay::world3d::aquarium::construction
