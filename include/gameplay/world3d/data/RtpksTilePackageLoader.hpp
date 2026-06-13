#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pr::gameplay::world3d::data {

struct RtpksMaterial {
    int material_id = -1;
    std::string name;
    std::string texture_name;
    int alpha = 31;
    std::vector<std::uint8_t> image_bytes;
};

struct RtpksMaterialRange {
    int material_id = -1;
    int tri_start = 0;
    int tri_count = 0;
    int quad_start = 0;
    int quad_count = 0;
};

struct RtpksTileMesh {
    int resort_tile_id = -1;
    int width = 1;
    int height = 1;
    float x_offset = 0.0f;
    float y_offset = 0.0f;
    std::vector<float> triangles;
    std::vector<float> quads;
    std::vector<float> tex_coords_tri;
    std::vector<float> tex_coords_quad;
    std::vector<float> colors_tri;
    std::vector<float> colors_quad;
    std::vector<RtpksMaterialRange> material_ranges;
};

struct RtpksTilePackage {
    std::string path;
    std::string pack_id;
    std::vector<RtpksMaterial> materials;
    std::vector<RtpksTileMesh> tiles;

    const RtpksMaterial* materialById(int material_id) const;
    const RtpksTileMesh* tileById(int resort_tile_id) const;
};

RtpksTilePackage loadRtpksTilePackage(const std::string& path, std::string* error = nullptr);

} // namespace pr::gameplay::world3d::data
