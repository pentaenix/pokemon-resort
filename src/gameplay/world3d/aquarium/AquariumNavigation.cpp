#include "gameplay/world3d/aquarium/AquariumNavigation.hpp"

#include "core/config/Json.hpp"

#include <algorithm>
#include <cmath>
#include <exception>

namespace pr::gameplay::world3d::aquarium {
namespace {

float numberOr(const JsonValue* value, float fallback) {
    return value && value->isNumber() ? static_cast<float>(value->asNumber()) : fallback;
}

Point2 parsePoint2(const JsonValue& value) {
    Point2 out{};
    if (!value.isArray() || value.asArray().size() < 2U) return out;
    out[0] = numberOr(&value.asArray()[0], 0.0f);
    out[1] = numberOr(&value.asArray()[1], 0.0f);
    return out;
}

Point3 parsePoint3(const JsonValue& value) {
    Point3 out{};
    if (!value.isArray() || value.asArray().size() < 3U) return out;
    for (std::size_t i = 0; i < 3U; ++i) out[i] = numberOr(&value.asArray()[i], 0.0f);
    return out;
}

bool pointInRing(const PolygonRing& ring, Point2 point) {
    if (ring.size() < 3U) return false;
    bool inside = false;
    for (std::size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++) {
        const Point2& a = ring[i];
        const Point2& b = ring[j];
        const bool crosses = ((a[1] > point[1]) != (b[1] > point[1])) &&
            (point[0] < (b[0] - a[0]) * (point[1] - a[1]) /
                    ((b[1] - a[1]) + 1.0e-12f) + a[0]);
        if (crosses) inside = !inside;
    }
    return inside;
}

float distanceToSegment(Point2 point, Point2 a, Point2 b) {
    const float dx = b[0] - a[0];
    const float dz = b[1] - a[1];
    const float denominator = dx * dx + dz * dz;
    const float t = denominator > 1.0e-12f
        ? std::clamp(((point[0] - a[0]) * dx + (point[1] - a[1]) * dz) / denominator, 0.0f, 1.0f)
        : 0.0f;
    const float ex = point[0] - (a[0] + dx * t);
    const float ez = point[1] - (a[1] + dz * t);
    return std::sqrt(ex * ex + ez * ez);
}

bool ringClearance(const PolygonRing& ring, Point2 point, float clearance) {
    if (clearance <= 0.0f) return true;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        if (distanceToSegment(point, ring[i], ring[(i + 1U) % ring.size()]) < clearance) return false;
    }
    return true;
}

bool polygonContains(const PolygonWithHoles& polygon, Point2 point, float clearance) {
    if (polygon.empty() || !pointInRing(polygon.front(), point) ||
        !ringClearance(polygon.front(), point, clearance)) return false;
    for (std::size_t i = 1; i < polygon.size(); ++i) {
        if (pointInRing(polygon[i], point) || !ringClearance(polygon[i], point, clearance)) return false;
    }
    return true;
}

} // namespace

AquariumNavigation loadAquariumNavigation(const std::string& path, std::string* error) {
    AquariumNavigation out;
    try {
        const JsonValue root = parseJsonFile(path);
        out.export_units_per_meter = std::max(0.0001f, numberOr(root.get("exportUnitsPerMeter"), 1.0f));
        if (const JsonValue* spawns = root.get("suggestedSpawnPoints"); spawns && spawns->isArray()) {
            for (const JsonValue& spawn : spawns->asArray()) {
                if (spawn.isObject()) {
                    if (const JsonValue* position = spawn.get("position")) {
                        out.suggested_spawns.push_back(parsePoint3(*position));
                    }
                }
            }
        }
        const JsonValue* layers = root.get("swimVolumeLayers");
        if (layers && layers->isArray()) {
            for (const JsonValue& layer_value : layers->asArray()) {
                if (!layer_value.isObject()) continue;
                SwimVolumeLayer layer;
                if (const JsonValue* id = layer_value.get("id"); id && id->isString()) layer.id = id->asString();
                layer.y_bottom = numberOr(layer_value.get("yBottom"), 0.0f);
                layer.y_top = numberOr(layer_value.get("yTop"), layer.y_bottom);
                const JsonValue* polygons = layer_value.get("polygons");
                if (polygons && polygons->isArray()) {
                    for (const JsonValue& polygon_value : polygons->asArray()) {
                        if (!polygon_value.isArray()) continue;
                        PolygonWithHoles polygon;
                        for (const JsonValue& ring_value : polygon_value.asArray()) {
                            if (!ring_value.isArray()) continue;
                            PolygonRing ring;
                            for (const JsonValue& point : ring_value.asArray()) ring.push_back(parsePoint2(point));
                            if (ring.size() >= 3U) polygon.push_back(std::move(ring));
                        }
                        if (!polygon.empty()) layer.polygons.push_back(std::move(polygon));
                    }
                }
                if (layer.y_top > layer.y_bottom && !layer.polygons.empty()) out.layers.push_back(std::move(layer));
            }
        }
        out.valid = !out.layers.empty();
        if (!out.valid && error) *error = "navigation contains no usable swimVolumeLayers";
    } catch (const std::exception& exception) {
        if (error) *error = exception.what();
    }
    return out;
}

bool containsPoint(const AquariumNavigation& navigation, Point3 point, float clearance_meters) {
    for (const SwimVolumeLayer& layer : navigation.layers) {
        // Adjacent exported layers are slices of one continuous water volume,
        // not solid shelves. Applying body clearance at every shared Y seam
        // creates artificial gaps and prevents fish reaching tunnels below the
        // floor; horizontal clearance still protects glass and obstacle holes.
        if (point[1] < layer.y_bottom || point[1] > layer.y_top) continue;
        for (const PolygonWithHoles& polygon : layer.polygons) {
            if (polygonContains(polygon, {point[0], point[2]}, clearance_meters)) return true;
        }
    }
    return false;
}

bool segmentIsNavigable(
    const AquariumNavigation& navigation,
    Point3 from,
    Point3 to,
    float clearance_meters) {
    const float dx = to[0] - from[0];
    const float dy = to[1] - from[1];
    const float dz = to[2] - from[2];
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    const int samples = std::max(2, static_cast<int>(std::ceil(distance / 0.08f)));
    for (int i = 0; i <= samples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(samples);
        if (!containsPoint(navigation,
            {from[0] + dx * t, from[1] + dy * t, from[2] + dz * t}, clearance_meters)) return false;
    }
    return true;
}

} // namespace pr::gameplay::world3d::aquarium
