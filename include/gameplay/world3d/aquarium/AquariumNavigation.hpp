#pragma once

#include <array>
#include <string>
#include <vector>

namespace pr::gameplay::world3d::aquarium {

using Point2 = std::array<float, 2>;
using Point3 = std::array<float, 3>;
using PolygonRing = std::vector<Point2>;
using PolygonWithHoles = std::vector<PolygonRing>;

struct SwimVolumeLayer {
    std::string id;
    float y_bottom = 0.0f;
    float y_top = 0.0f;
    std::vector<PolygonWithHoles> polygons;
};

struct AquariumNavigation {
    float export_units_per_meter = 1.0f;
    std::vector<SwimVolumeLayer> layers;
    std::vector<Point3> suggested_spawns;
    bool valid = false;
};

AquariumNavigation loadAquariumNavigation(const std::string& path, std::string* error = nullptr);
bool containsPoint(const AquariumNavigation& navigation, Point3 point, float clearance_meters = 0.0f);
bool segmentIsNavigable(
    const AquariumNavigation& navigation,
    Point3 from,
    Point3 to,
    float clearance_meters = 0.0f);

} // namespace pr::gameplay::world3d::aquarium
