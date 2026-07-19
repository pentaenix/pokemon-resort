#include "gameplay/world3d/data/GlbModelLoader.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

fs::path repositoryRoot() {
    fs::path current = fs::current_path();
    while (!current.empty()) {
        if (fs::exists(current / "CMakeLists.txt") &&
            fs::exists(current / "assets/overworld/models/palm_straight/palm_straight.glb")) return current;
        const fs::path parent = current.parent_path();
        if (parent == current) break;
        current = parent;
    }
    return {};
}

float positionDistance(
    const pr::gameplay::world3d::data::GlbVertex& a,
    const pr::gameplay::world3d::data::GlbVertex& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace

int main() {
    const fs::path root = repositoryRoot();
    expect(!root.empty(), "repository root and animated palm fixture should be discoverable");
    if (root.empty()) return EXIT_FAILURE;

    std::string error;
    const auto mesh = pr::gameplay::world3d::data::loadGlbModel(
        (root / "assets/overworld/models/palm_straight/palm_straight.glb").string(), &error);
    expect(mesh.valid, "animated palm GLB should load: " + error);
    expect(mesh.animations.size() == 1, "animated palm should expose its Wind_Leaves_Loop clip");
    expect(!mesh.animations.empty() && mesh.animations.front().name == "Wind_Leaves_Loop",
           "world GLB loader should preserve the authored animation name");
    expect(!mesh.animations.empty() && !mesh.animations.front().morph_channels.empty(),
           "world GLB loader should decode morph-weight animation channels");

    const auto weights_a = pr::gameplay::world3d::data::sampleGlbMorphWeights(mesh, 0.0);
    const auto weights_b = pr::gameplay::world3d::data::sampleGlbMorphWeights(mesh, 0.2);
    bool found_moving_vertex = false;
    for (const auto& triangle : mesh.triangles) {
        for (const auto* vertex : {&triangle.a, &triangle.b, &triangle.c}) {
            if (vertex->morph_position_deltas.empty()) continue;
            const auto a = pr::gameplay::world3d::data::applyGlbMorphWeights(*vertex, weights_a);
            const auto b = pr::gameplay::world3d::data::applyGlbMorphWeights(*vertex, weights_b);
            if (positionDistance(a, b) > 0.0001f) {
                found_moving_vertex = true;
                break;
            }
        }
        if (found_moving_vertex) break;
    }
    expect(found_moving_vertex, "sampling the palm clip at two times should deform its leaves");

    const std::vector<std::string> palm_variants{
        "palm_left", "palm_old", "palm_right", "palm_right_long", "palm_straight", "palm_young"};
    for (const std::string& variant : palm_variants) {
        error.clear();
        const auto palm = pr::gameplay::world3d::data::loadGlbModel(
            (root / "assets/overworld/models" / variant / (variant + ".glb")).string(), &error);
        expect(palm.valid, variant + " should load through the animated world GLB path: " + error);
        expect(!palm.animations.empty() && !palm.animations.front().morph_channels.empty(),
               variant + " should retain its morph animation clip");
    }

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
