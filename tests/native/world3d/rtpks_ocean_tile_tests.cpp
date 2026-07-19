#include "gameplay/world3d/data/RtpksTilePackageLoader.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using pr::gameplay::world3d::data::RtpksMaterial;
using pr::gameplay::world3d::data::RtpksMaterialRange;
using pr::gameplay::world3d::data::RtpksTileMesh;

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (condition) return;
    ++failures;
    std::cerr << "[FAIL] " << message << '\n';
}

std::vector<float> rangeHeights(const RtpksTileMesh& tile, const RtpksMaterialRange& range) {
    std::vector<float> out;
    for (int tri = range.tri_start; tri < range.tri_start + range.tri_count; ++tri) {
        for (int corner = 0; corner < 3; ++corner) {
            const std::size_t offset = static_cast<std::size_t>((tri * 3 + corner) * 3 + 2);
            if (offset < tile.triangles.size()) out.push_back(tile.triangles[offset]);
        }
    }
    for (int quad = range.quad_start; quad < range.quad_start + range.quad_count; ++quad) {
        for (int corner = 0; corner < 4; ++corner) {
            const std::size_t offset = static_cast<std::size_t>((quad * 4 + corner) * 3 + 2);
            if (offset < tile.quads.size()) out.push_back(tile.quads[offset]);
        }
    }
    return out;
}

std::vector<float> rangeTextureVs(const RtpksTileMesh& tile, const RtpksMaterialRange& range) {
    std::vector<float> out;
    for (int tri = range.tri_start; tri < range.tri_start + range.tri_count; ++tri) {
        for (int corner = 0; corner < 3; ++corner) {
            const std::size_t offset = static_cast<std::size_t>((tri * 3 + corner) * 2 + 1);
            if (offset < tile.tex_coords_tri.size()) out.push_back(tile.tex_coords_tri[offset]);
        }
    }
    for (int quad = range.quad_start; quad < range.quad_start + range.quad_count; ++quad) {
        for (int corner = 0; corner < 4; ++corner) {
            const std::size_t offset = static_cast<std::size_t>((quad * 4 + corner) * 2 + 1);
            if (offset < tile.tex_coords_quad.size()) out.push_back(tile.tex_coords_quad[offset]);
        }
    }
    return out;
}

std::vector<float> allHeights(const RtpksTileMesh& tile) {
    std::vector<float> out;
    for (std::size_t i = 2; i < tile.triangles.size(); i += 3) out.push_back(tile.triangles[i]);
    for (std::size_t i = 2; i < tile.quads.size(); i += 3) out.push_back(tile.quads[i]);
    return out;
}

} // namespace

int main() {
    const fs::path pack_path = fs::path(PR_SOURCE_DIR) /
        "assets/overworld/tilepacks/maptiles.rtpks";
    std::string error;
    const auto package = pr::gameplay::world3d::data::loadRtpksTilePackage(pack_path.string(), &error);
    expect(error.empty(), "committed RTPKS loads without an error: " + error);

    const std::vector<int> ocean_ids = {
        3275, 3276, 3277, 3278, 3279, 3280, 3281, 3282, 3283,
        3284, 3285, 3286, 3287, 3288, 3289, 3290, 3291, 3292,
        3293, 3294, 3295, 3296, 3297, 3298, 3299, 3300, 3321,
    };
    for (const int tile_id : ocean_ids) {
        const RtpksTileMesh* tile = package.tileById(tile_id);
        expect(tile != nullptr, "ocean tile " + std::to_string(tile_id) + " is runtime-loadable");
        if (!tile) continue;
        for (const RtpksMaterialRange& range : tile->material_ranges) {
            expect(package.materialById(range.material_id) != nullptr,
                "ocean tile " + std::to_string(tile_id) + " resolves material " +
                std::to_string(range.material_id));
        }
    }

    const RtpksTileMesh* body = package.tileById(3287);
    if (body) {
        const std::vector<float> heights = allHeights(*body);
        expect(!heights.empty(), "open-ocean body contains geometry");
        if (!heights.empty()) {
            const auto [min_it, max_it] = std::minmax_element(heights.begin(), heights.end());
            expect(std::abs(*min_it + 1.5f) < 0.0001f, "lower ocean plane stays at -1.5 tiles");
            expect(std::abs(*max_it + 0.6875f) < 0.0001f, "upper ocean plane stays at -0.6875 tiles");
        }
    }

    const RtpksTileMesh* beach = package.tileById(3275);
    if (beach) {
        bool found_sand_datum = false;
        bool found_embedded_ocean = false;
        bool shoreline_uses_mesh_uvs = false;
        for (const RtpksMaterialRange& range : beach->material_ranges) {
            const RtpksMaterial* material = package.materialById(range.material_id);
            if (!material) continue;
            found_embedded_ocean = found_embedded_ocean ||
                material->name == "sea_mizu1" || material->name == "sea_mizu1_1";
            if (material->name == "sea_zanami" || material->name == "sea_zanami2") {
                shoreline_uses_mesh_uvs = shoreline_uses_mesh_uvs || !material->world_uv;
            }
            if (material->name != "sea_simi_1") continue;
            const std::vector<float> heights = rangeHeights(*beach, range);
            if (!heights.empty()) {
                found_sand_datum = std::abs(*std::max_element(heights.begin(), heights.end())) < 0.0001f;
            }
        }
        expect(found_sand_datum, "beach sand/seam surface is authored at height zero");
        expect(!found_embedded_ocean,
            "beach overlay does not duplicate the separately painted ocean body planes");
        expect(shoreline_uses_mesh_uvs,
            "beach motion keeps its authored non-affine mesh UV islands");
    }

    const bool has_ocean_motion = std::any_of(
        package.materials.begin(), package.materials.end(), [](const RtpksMaterial& material) {
            return material.name == "sea_mizu1" && std::abs(material.animation_timebase_hz - 30.0f) < 0.001f &&
                material.animation_step &&
                material.animation_uv_offsets.size() == 240;
        });
    expect(has_ocean_motion, "open ocean retains its 240-sample signed UV timeline");

    const bool has_translucent_upper_ocean = std::any_of(
        package.materials.begin(), package.materials.end(), [](const RtpksMaterial& material) {
            return material.name == "sea_mizu1_1" && material.alpha > 0 && material.alpha < 31 &&
                material.animation_uv_offsets.size() == 240;
        });
    expect(has_translucent_upper_ocean,
        "open ocean retains a separately scrolling translucent upper layer");

    const bool has_world_continuous_ocean_uvs = std::any_of(
        package.materials.begin(), package.materials.end(), [](const RtpksMaterial& material) {
            return material.name == "sea_mizu1" && material.world_uv &&
                std::abs(material.u_per_tile[0] + 0.25f) < 0.0001f &&
                std::abs(material.v_per_tile[1] + 0.25f) < 0.0001f;
        }) && std::any_of(
        package.materials.begin(), package.materials.end(), [](const RtpksMaterial& material) {
            return material.name == "sea_mizu1_1" && material.world_uv &&
                std::abs(material.u_per_tile[0] - 0.25f) < 0.0001f &&
                std::abs(material.v_per_tile[1] - 0.25f) < 0.0001f;
        });
    expect(has_world_continuous_ocean_uvs,
        "both ocean layers retain their source-derived world-continuous UV bases");

    const bool beach_seam_uses_raes_source_sampler = std::any_of(
        package.materials.begin(), package.materials.end(), [](const RtpksMaterial& material) {
            return material.name == "sea_simi_1" && material.wrap_s == "repeat" && material.wrap_t == "clamp";
        });
    expect(beach_seam_uses_raes_source_sampler,
        "beach wave/sand seam keeps the sampler paired with RAE's exact extracted geometry");

    const bool beach_lower_wave_uses_raes_source_sampler = std::any_of(
        package.materials.begin(), package.materials.end(), [](const RtpksMaterial& material) {
            return material.name == "sea_zanami2" && material.wrap_s == "repeat" && material.wrap_t == "clamp";
        });
    expect(beach_lower_wave_uses_raes_source_sampler,
        "beach lower wave keeps the sampler paired with RAE's exact extracted geometry");

    const RtpksMaterial* shoreline_wave = nullptr;
    const RtpksMaterial* shoreline_underlay = nullptr;
    for (const RtpksMaterial& material : package.materials) {
        if (material.name == "sea_zanami" && !material.animation_uv_offsets.empty()) shoreline_wave = &material;
        if (material.name == "sea_zanami2" && !material.animation_uv_offsets.empty()) shoreline_underlay = &material;
    }
    expect(shoreline_wave != nullptr && shoreline_underlay != nullptr,
        "beach shoreline retains both independent source layers");
    if (shoreline_wave && shoreline_underlay) {
        expect(std::abs(shoreline_wave->animation_timebase_hz - 30.0f) < 0.001f &&
                std::abs(shoreline_underlay->animation_timebase_hz - 30.0f) < 0.001f &&
                shoreline_wave->animation_step && shoreline_underlay->animation_step,
            "beach shoreline samples use exact stepped Generation V 30 Hz map ticks");
        const auto& wave_offsets = shoreline_wave->animation_uv_offsets;
        const auto& underlay_offsets = shoreline_underlay->animation_uv_offsets;
        const auto lateralRange = [](const auto& offsets) {
            const auto [min_it, max_it] = std::minmax_element(
                offsets.begin(), offsets.end(), [](const auto& lhs, const auto& rhs) {
                    return lhs[0] < rhs[0];
                });
            return (*max_it)[0] - (*min_it)[0];
        };
        expect(!wave_offsets.empty() && !underlay_offsets.empty() &&
                lateralRange(wave_offsets) > 31.9f && lateralRange(underlay_offsets) > 31.9f,
            "beach crest and underlay preserve their ROM-authored lateral channels");
        const bool underlay_stays_vertical = std::all_of(
            underlay_offsets.begin(), underlay_offsets.end(), [](const auto& offset) {
                return std::abs(offset[1]) < 0.0001f;
            });
        expect(underlay_stays_vertical,
            "beach underlay keeps its independent zero-V source track");
        if (!wave_offsets.empty()) {
            const auto [min_it, max_it] = std::minmax_element(
                wave_offsets.begin(), wave_offsets.end(), [](const auto& lhs, const auto& rhs) {
                    return lhs[1] < rhs[1];
                });
            expect(std::abs((*max_it)[1] - 0.0600586f) < 0.0001f &&
                    std::abs((*min_it)[1] + 0.0700684f) < 0.0001f,
                "beach crest reaches its full advance and only half of its former retreat");
        }
    }

    const RtpksTileMesh* north_beach = package.tileById(3283);
    const RtpksTileMesh* north_beach_oracle = package.tileById(125);
    expect(north_beach && north_beach_oracle &&
            north_beach->triangles == north_beach_oracle->triangles &&
            north_beach->quads == north_beach_oracle->quads &&
            north_beach->tex_coords_tri == north_beach_oracle->tex_coords_tri &&
            north_beach->tex_coords_quad == north_beach_oracle->tex_coords_quad,
        "Water-tab north beach is the proven Default-tab runtime geometry and UV oracle");

    const bool has_rock_motion = std::any_of(
        package.materials.begin(), package.materials.end(), [](const RtpksMaterial& material) {
            return material.name == "sea_gake02" && material.animation_frame_time_ms == 50 &&
                material.animation_uv_offsets.size() == 240;
        });
    expect(has_rock_motion, "rock/water edge retains its source-correct 50 ms timeline");

    if (failures == 0) std::cout << "[PASS] canonical ocean RTPKS tile tests\n";
    return failures == 0 ? 0 : 1;
}
