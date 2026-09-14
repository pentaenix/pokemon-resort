#pragma once

#include "gameplay/world3d/aquarium/AquariumNavigation.hpp"

#include <map>
#include <memory>
#include <vector>

namespace pr::gameplay::world3d::aquarium {

struct AquariumBodyClearance {
    float radius = 0.0f;
    float lower_extent = 0.0f;
    float upper_extent = 0.0f;
    bool floor = false;
    float floor_query_y = 0.0f;
    float floor_origin_y = 0.0f;
};

bool containsAquariumBody(const AquariumNavigation&, const AquariumBodyClearance&, Point3);
bool aquariumBodySegmentNavigable(
    const AquariumNavigation&, const AquariumBodyClearance&, Point3 from, Point3 to);

enum class AquariumRouteStatus { Direct, Routed, Unreachable, SearchLimit };
struct AquariumBodyRoute {
    AquariumRouteStatus status = AquariumRouteStatus::Unreachable;
    std::vector<Point3> waypoints;
    unsigned expanded_nodes = 0;
};

struct AquariumRouteQuery {
    AquariumBodyRoute result;
    bool complete = false;
    struct Search;
    std::shared_ptr<Search> search;
};

// Geometry-owned, lazily evaluated lattice. Share between residents with the
// same clearance; discard on tank geometry replacement, never per frame.
class AquariumBodyNavigation {
public:
    AquariumBodyNavigation(AquariumNavigation navigation, AquariumBodyClearance body);
    AquariumBodyRoute route(Point3 from, Point3 to);
    std::shared_ptr<AquariumRouteQuery> beginRoute(Point3 from, Point3 to);
    void advanceRoute(AquariumRouteQuery&, unsigned expansion_budget);

private:
    using Cell = std::array<int, 3>;
    struct Lattice {
        float spacing = 0.5f;
        std::map<Cell, bool> occupied;
        std::map<std::pair<Cell, Cell>, bool> edges;
    };
    Point3 point(Cell cell, const Lattice&) const;
    bool available(Cell cell, Lattice&);
    AquariumNavigation navigation_;
    AquariumBodyClearance body_;
    Point3 minimum_{};
    Point3 maximum_{};
    std::vector<float> heights_;
    Lattice coarse_;
    Lattice fine_{0.25f, {}, {}};
};

} // namespace pr::gameplay::world3d::aquarium
