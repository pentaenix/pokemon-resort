#include "gameplay/world3d/data/RtpksTilePackageLoader.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using pr::gameplay::world3d::data::RtpksTileMesh;

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (condition) return;
    ++failures;
    std::cerr << "[FAIL] " << message << '\n';
}

} // namespace

int main() {
    const fs::path pack_path = fs::path(PR_SOURCE_DIR) /
        "assets/overworld/tilepacks/maptiles.rtpks";
    std::string error;
    const auto package = pr::gameplay::world3d::data::loadRtpksTilePackage(pack_path.string(), &error);
    expect(error.empty(), "committed RTPKS loads: " + error);

    const int door_count = static_cast<int>(std::count_if(
        package.tiles.begin(), package.tiles.end(), [](const RtpksTileMesh& tile) {
            return tile.triggerable_door;
        }));
    expect(door_count == 30, "Doors tab exposes all 30 deduplicated variants");

    const RtpksTileMesh* hinged = package.tileById(3338);
    expect(hinged != nullptr, "door_bar has its stable imported id");
    if (hinged) {
        expect(hinged->door_open_animation == "door_op", "door_bar addresses its named open clip");
        expect(hinged->door_close_clip == "door_cl", "door_bar addresses its named close clip");
        expect(hinged->vertex_animations.size() == 2, "door_bar retains both baked skeletal clips");
        for (const auto& clip : hinged->vertex_animations) {
            expect(clip.frames.size() == 8, clip.name + " retains all eight source frames");
            expect(!clip.frames.empty() && clip.frames.front().size() == hinged->triangles.size(),
                clip.name + " frame positions match the compiled triangle stream");
        }
    }

    const RtpksTileMesh* elevator = package.tileById(3366);
    expect(elevator && elevator->vertex_animations.size() == 2,
        "elevator retains its up/down clips");
    if (elevator && !elevator->vertex_animations.empty()) {
        expect(elevator->vertex_animations.front().frames.size() == 41,
            "elevator retains its 41-frame source timeline");
    }

    const RtpksTileMesh* sliding = package.tileById(3337);
    expect(sliding && sliding->vertex_animations.empty(),
        "texture-motion door stays on the material animation path");
    if (sliding && !sliding->material_ranges.empty()) {
        const auto* material = package.materialById(sliding->material_ranges.front().material_id);
        expect(material && material->animation_uv_offsets.size() == 8,
            "texture-motion door retains its eight opening samples");
        if (material && !material->animation_uv_offsets.empty()) {
            expect(material->animation_uv_offsets.front()[0] == 0.0f,
                "texture-motion door rests closed and opens forward");
        }
    }

    if (failures == 0) std::cout << "[PASS] RTPKS door tile tests\n";
    return failures == 0 ? 0 : 1;
}
