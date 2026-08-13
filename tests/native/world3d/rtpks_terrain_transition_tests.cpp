#include "gameplay/world3d/data/RtpksTilePackageLoader.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

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

    const auto* isolated = package.tileById(3367);
    expect(isolated != nullptr, "isolated sand/grass transition keeps its stable tile id");
    if (isolated) {
        const bool has_sand = std::any_of(isolated->material_ranges.begin(), isolated->material_ranges.end(),
            [](const auto& range) { return range.material_id == 8; });
        const bool has_grass = std::any_of(isolated->material_ranges.begin(), isolated->material_ranges.end(),
            [](const auto& range) { return range.material_id == 507; });
        expect(has_sand, "transition reuses the Black 2 sand material");
        expect(has_grass, "transition reuses the extracted Black 2 grass material");
        struct SurfaceRect { float min_x; float min_y; float max_x; float max_y; int material_id; };
        std::vector<SurfaceRect> surfaces;
        for (const auto& range : isolated->material_ranges) {
            for (int quad = range.quad_start; quad < range.quad_start + range.quad_count; ++quad) {
                const std::size_t offset = static_cast<std::size_t>(quad) * 12U;
                if (offset + 11U >= isolated->quads.size()) continue;
                SurfaceRect rect{isolated->quads[offset], isolated->quads[offset + 1U],
                    isolated->quads[offset], isolated->quads[offset + 1U], range.material_id};
                for (std::size_t vertex = 1; vertex < 4U; ++vertex) {
                    rect.min_x = std::min(rect.min_x, isolated->quads[offset + vertex * 3U]);
                    rect.max_x = std::max(rect.max_x, isolated->quads[offset + vertex * 3U]);
                    rect.min_y = std::min(rect.min_y, isolated->quads[offset + vertex * 3U + 1U]);
                    rect.max_y = std::max(rect.max_y, isolated->quads[offset + vertex * 3U + 1U]);
                }
                surfaces.push_back(rect);
            }
        }
        bool overlaps = false;
        for (std::size_t left = 0; left < surfaces.size(); ++left) {
            for (std::size_t right = left + 1U; right < surfaces.size(); ++right) {
                if (surfaces[left].material_id == surfaces[right].material_id) continue;
                const float width = std::min(surfaces[left].max_x, surfaces[right].max_x)
                    - std::max(surfaces[left].min_x, surfaces[right].min_x);
                const float height = std::min(surfaces[left].max_y, surfaces[right].max_y)
                    - std::max(surfaces[left].min_y, surfaces[right].min_y);
                overlaps = overlaps || (width > 0.00001f && height > 0.00001f);
            }
        }
        expect(!overlaps, "sand and grass partition the transition instead of depth-overlapping");
        expect(std::find(isolated->tags.begin(), isolated->tags.end(), "terrain.transition") != isolated->tags.end(),
            "transition tile exposes terrain.transition semantics");
        expect(!isolated->quads.empty(), "transition contains renderable hard-edged geometry");
    }

    const auto* grass_body = package.tileById(3322);
    expect(grass_body != nullptr, "existing grass body remains available as the fully connected shape");
    if (grass_body) {
        expect(grass_body->material_ranges.size() == 1 && grass_body->material_ranges[0].material_id == 507,
            "fully connected grass still uses only its original material");
    }

    if (failures == 0) std::cout << "[PASS] RTPKS terrain transition tests\n";
    return failures == 0 ? 0 : 1;
}
