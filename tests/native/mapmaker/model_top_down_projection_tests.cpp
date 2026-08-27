#include "mapmaker/preview/ModelTopDownProjection.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

bool near(float left, float right) { return std::abs(left - right) < 0.001f; }

void testConvexProjectionAndPlacementDimensions() {
    const std::vector<pr::mapmaker::ModelTopDownPoint> vertices{
        {-16.0f, -8.0f}, {16.0f, -8.0f}, {16.0f, 8.0f}, {-16.0f, 8.0f},
        {0.0f, 0.0f}, {-16.0f, -8.0f}};
    const auto projection = pr::mapmaker::buildModelTopDownProjection(vertices);
    expect(projection.valid && projection.outline.size() == 4U,
        "a rectangular GLB projection should reduce to its four-corner silhouette");
    expect(near(projection.width, 32.0f) && near(projection.depth, 16.0f),
        "model-space projection dimensions should preserve actual X/Z bounds");

    const auto placed = pr::mapmaker::placeModelTopDownProjection(projection, 90.0f, 1.0f, 16.0f);
    expect(near(placed.width_tiles, 1.0f) && near(placed.depth_tiles, 2.0f),
        "a 90 degree placement should rotate projected width and depth using runtime math");
}

} // namespace

int main() {
    try {
        testConvexProjectionAndPlacementDimensions();
        std::cout << "model_top_down_projection_tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "model_top_down_projection_tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
