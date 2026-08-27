#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace pr::gameplay::world3d::data {

struct RtpksMaterialImageKeyframe {
    int frame = 0;
    std::vector<std::uint8_t> image_bytes;
};

struct RtpksMaterial {
    int material_id = -1;
    std::string name;
    std::string texture_name;
    int alpha = 31;
    std::string wrap_s = "repeat";
    std::string wrap_t = "repeat";
    std::string mag_filter = "nearest";
    std::string min_filter = "nearest";
    bool world_uv = false;
    std::array<float, 2> u_per_tile{0.0f, 0.0f};
    std::array<float, 2> v_per_tile{0.0f, 0.0f};
    std::vector<std::uint8_t> image_bytes;
    int animation_frame_time_ms = 0;
    float animation_timebase_hz = 0.0f;
    bool animation_step = false;
    std::vector<std::vector<std::uint8_t>> animation_frame_bytes;
    int animation_frame_count = 0;
    int animation_image_frame_count = 0;
    bool animation_loop = true;
    std::vector<std::array<float, 2>> animation_uv_offsets;
    std::vector<RtpksMaterialImageKeyframe> animation_image_keyframes;
    int render_order = 0;
    std::string layer_role = "surface";
};

struct RtpksMaterialRange {
    int material_id = -1;
    int tri_start = 0;
    int tri_count = 0;
    int quad_start = 0;
    int quad_count = 0;
};

struct RtpksVertexAnimationClip {
    std::string name;
    int frame_time_ms = 100;
    std::vector<std::vector<float>> frames;
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
    std::vector<RtpksVertexAnimationClip> vertex_animations;
    std::string name;
    std::vector<std::string> tags;
    std::string collision_mode = "none";
    bool collision_auto_apply = false;
    bool collision_clear_on_erase = false;
    std::vector<std::vector<bool>> collision_mask;
    bool triggerable_door = false;
    std::string door_front = "south";
    std::string door_open_animation = "open";
    std::string door_close_animation = "reverse_open";
    std::string door_close_clip;
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
// Loads only the authored meshes used by the requested stable resort tile ids,
// plus the transitive material/texture resources required by those meshes.
RtpksTilePackage loadRtpksTilePackageForTiles(
    const std::string& path,
    const std::vector<int>& resort_tile_ids,
    std::string* error = nullptr);
// Same selection as loadRtpksTilePackageForTiles, but skips image payloads.
// This is intended for systems which only need authored animation metadata.
RtpksTilePackage loadRtpksTileMetadataForTiles(
    const std::string& path,
    const std::vector<int>& resort_tile_ids,
    std::string* error = nullptr);
// Reads only runtime/manifest.json. Intended for gameplay tag/footprint queries;
// it deliberately avoids decoding mesh JSON and texture/image payloads.
RtpksTilePackage loadRtpksTileSemantics(const std::string& path, std::string* error = nullptr);

} // namespace pr::gameplay::world3d::data
