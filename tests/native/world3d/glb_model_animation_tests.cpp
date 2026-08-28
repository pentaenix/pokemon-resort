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
            fs::exists(current / "assets/overworld/models/palm_young/palm_young.glb") &&
            fs::exists(current / "assets/overworld/models/aquarium/aquarium.glb")) return current;
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
        (root / "assets/overworld/models/palm_young/palm_young.glb").string(), &error);
    expect(mesh.valid, "animated palm GLB should load: " + error);
    expect(mesh.animations.size() == 1, "animated palm should expose its Wind_Leaves_Loop clip");
    expect(!mesh.animations.empty() && mesh.animations.front().name == "Wind_Leaves_Loop",
           "world GLB loader should preserve the authored animation name");
    expect(!mesh.animations.empty() && !mesh.animations.front().morph_channels.empty(),
           "world GLB loader should decode morph-weight animation channels");
    bool palm_uses_nitro_modulation = false;
    for (const auto& material : mesh.materials) {
        palm_uses_nitro_modulation = palm_uses_nitro_modulation || material.nitro_vertex_color;
    }
    expect(palm_uses_nitro_modulation,
        "RAE Nintendo DS palm materials must retain Nitro vertex-color modulation");
    expect(pr::gameplay::world3d::data::compositeGlbVertexColor(0.25f, mesh.materials.front()) == 1.0f,
        "RAE DS vertex color must not darken the established textured overworld presentation");

    const auto weights_a = pr::gameplay::world3d::data::sampleGlbMorphWeights(mesh, 0.0);
    const auto weights_b = pr::gameplay::world3d::data::sampleGlbMorphWeights(mesh, 1.5);
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
        "palm_left", "palm_old", "palm_right", "palm_right_2", "palm_young"};
    for (const std::string& variant : palm_variants) {
        error.clear();
        const auto palm = pr::gameplay::world3d::data::loadGlbModel(
            (root / "assets/overworld/models" / variant / (variant + ".glb")).string(), &error);
        expect(palm.valid, variant + " should load through the animated world GLB path: " + error);
        expect(!palm.animations.empty() && !palm.animations.front().morph_channels.empty(),
               variant + " should retain its morph animation clip");
    }

    error.clear();
    const auto aquarium = pr::gameplay::world3d::data::loadGlbModel(
        (root / "assets/overworld/models/aquarium/aquarium.glb").string(), &error);
    expect(aquarium.valid, "Aquarium Maker GLB should load: " + error);
    bool aquarium_uses_nitro_modulation = false;
    for (const auto& material : aquarium.materials) {
        aquarium_uses_nitro_modulation =
            aquarium_uses_nitro_modulation || material.nitro_vertex_color;
    }
    expect(!aquarium_uses_nitro_modulation,
        "Aquarium Maker colors must remain ordinary glTF colors, not receive DS brightening");
    expect(aquarium.animations.size() == 2U,
        "both authored kelp WaterSway clips should load");
    expect(!aquarium.animations.empty() &&
        !aquarium.animations.front().rotation_channels.empty(),
        "Aquarium Maker node-rotation tracks should survive GLB loading");
    bool has_authored_vertex_color = false;
    bool has_translucent_material = false;
    bool has_composited_translucent_vertex = false;
    bool has_swaying_vertex = false;
    const auto rotations_a = pr::gameplay::world3d::data::sampleGlbNodeRotations(aquarium, 0.0);
    const auto rotations_b = pr::gameplay::world3d::data::sampleGlbNodeRotations(aquarium, 1.5);
    const std::vector<std::vector<float>> no_weights;
    for (const auto& triangle : aquarium.triangles) {
        if (triangle.material >= 0 &&
            triangle.material < static_cast<int>(aquarium.materials.size())) {
            const auto& material = aquarium.materials[static_cast<std::size_t>(triangle.material)];
            has_translucent_material = has_translucent_material || material.base_color[3] < 0.999f;
            has_composited_translucent_vertex = has_composited_translucent_vertex ||
                pr::gameplay::world3d::data::compositeGlbAlpha(triangle.a, material) < 0.999f;
        }
        for (const auto* vertex : {&triangle.a, &triangle.b, &triangle.c}) {
            has_authored_vertex_color = has_authored_vertex_color ||
                std::abs(vertex->r - 1.0f) > 0.001f ||
                std::abs(vertex->g - 1.0f) > 0.001f ||
                std::abs(vertex->b - 1.0f) > 0.001f;
            const auto a = pr::gameplay::world3d::data::sampleGlbAnimatedPosition(
                aquarium, *vertex, no_weights, rotations_a);
            const auto b = pr::gameplay::world3d::data::sampleGlbAnimatedPosition(
                aquarium, *vertex, no_weights, rotations_b);
            const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
            has_swaying_vertex = has_swaying_vertex ||
                std::sqrt(dx * dx + dy * dy + dz * dz) > 0.0001f;
        }
    }
    expect(has_authored_vertex_color,
        "rock and kelp COLOR_0 data must not be flattened to white");
    expect(has_translucent_material && has_composited_translucent_vertex,
        "aquarium glass opacity must be composed with COLOR_0 instead of becoming opaque");
    expect(has_swaying_vertex,
        "sampling WaterSway at two times should rotate kelp geometry");

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
