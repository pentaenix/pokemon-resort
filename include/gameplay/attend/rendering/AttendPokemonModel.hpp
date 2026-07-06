#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace pr::gameplay::attend::rendering {

struct AttendPokemonVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float nx = 0.0f;
    float ny = 1.0f;
    float nz = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
    std::array<std::uint16_t, 4> joints{0, 0, 0, 0};
    std::array<float, 4> weights{0.0f, 0.0f, 0.0f, 0.0f};
};

enum class AttendRenderClass {
    Opaque,
    Mask,
    Blend,
    UniformDecal
};

enum class AttendMaterialRole {
    None,
    EyeSclera,
    EyeIris,
    Mouth
};

struct AttendTextureSampler {
    int wrap_s = 10497;
    int wrap_t = 10497;
    int mag_filter = 9729;
    int min_filter = 9729;
};

struct AttendEyeSheet {
    bool enabled = false;
    int cols = 1;
    int rows = 1;
    float scale_x = 1.0f;
    float scale_y = 1.0f;
    float translation_x = 0.0f;
    float translation_y = 0.0f;
    int wrap_s = 10497;
    int wrap_t = 10497;
    int default_frame = 0;
    std::vector<std::array<float, 2>> frame_offsets;
};

struct AttendPokemonMaterial {
    std::string name;
    std::vector<std::uint8_t> base_color_bytes;
    std::vector<std::uint8_t> emissive_bytes;
    int base_color_image = -1;
    int emissive_image = -1;
    bool has_base_color_texture = false;
    bool has_emissive_texture = false;
    bool pokemon_eye = false;
    float base_color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float alpha_cutoff = 0.5f;
    std::string alpha_mode = "OPAQUE";
    bool has_alpha_mode = false;
    bool has_rae_policy = false;
    AttendRenderClass render_class = AttendRenderClass::Opaque;
    AttendMaterialRole material_role = AttendMaterialRole::None;
    int shiny_material_index = -1;
    std::vector<std::pair<std::string, int>> form_material_indices;
    AttendTextureSampler base_color_sampler;
    AttendEyeSheet eye_sheet;
};

struct AttendTextureVariantOption {
    std::string id;
    std::string label;
};

struct AttendPokemonPrimitive {
    std::vector<AttendPokemonVertex> vertices;
    std::vector<std::uint32_t> indices;
    int material = -1;
    int mesh_node = -1;
    int skin = -1;
    int render_order = 0;
    int scene_order = 0;
    bool default_visible = true;
    std::vector<std::string> visible_for_forms;
};

struct AttendPokemonAnimationChannel {
    enum class Path {
        Translation,
        Rotation,
        Scale
    };

    int target_node = -1;
    Path path = Path::Translation;
    std::vector<float> times;
    std::vector<std::array<float, 4>> values;
};

struct AttendPokemonAnimation {
    std::string name;
    float duration_seconds = 0.0f;
    std::vector<AttendPokemonAnimationChannel> channels;
};

struct AttendPokemonNode {
    std::string name;
    int parent = -1;
    std::vector<int> children;
    std::array<float, 16> local_matrix{};
    std::array<float, 3> base_translation{0.0f, 0.0f, 0.0f};
    std::array<float, 4> base_rotation{0.0f, 0.0f, 0.0f, 1.0f};
    std::array<float, 3> base_scale{1.0f, 1.0f, 1.0f};
    bool has_matrix = false;
    int render_order = 0;
    int scene_order = 0;
    bool default_visible = true;
    std::vector<std::string> visible_for_forms;
};

struct AttendPokemonPoseOverlay {
    const AttendPokemonAnimation* animation = nullptr;
    double time_seconds = 0.0;
    float weight = 0.0f;
    bool eyelids_only = false;
    std::vector<std::string> eyelid_node_substrings{"eyelid"};
    float head_yaw_degrees = 0.0f;
    float head_pitch_degrees = 0.0f;
    float head_weight = 0.0f;
    std::vector<std::string> head_node_names{"head"};
    float head_yaw_axis[3] = {1.0f, 0.0f, 0.0f};
    float head_pitch_axis[3] = {0.0f, 1.0f, 0.0f};
};

struct AttendPokemonSkin {
    std::vector<int> joints;
    std::vector<std::array<float, 16>> inverse_bind_matrices;
};

struct AttendPokemonModel {
    std::vector<AttendPokemonNode> nodes;
    std::vector<AttendPokemonMaterial> materials;
    std::vector<AttendPokemonPrimitive> primitives;
    std::vector<AttendPokemonSkin> skins;
    std::vector<AttendPokemonAnimation> animations;
    std::string default_texture_variant = "normal";
    std::vector<AttendTextureVariantOption> texture_variants;
    std::string default_form_variant;
    std::vector<AttendTextureVariantOption> form_variants;
    bool valid = false;
};

AttendPokemonModel loadAttendPokemonModel(const std::string& path, std::string* error = nullptr);
const AttendPokemonAnimation* findAttendPokemonAnimation(
    const AttendPokemonModel& model,
    const std::string& animation_name);
std::vector<std::array<float, 16>> buildAttendPokemonGlobals(
    const AttendPokemonModel& model,
    const AttendPokemonAnimation* animation,
    double scene_time_seconds,
    AttendPokemonPoseOverlay overlay = {});
std::vector<std::vector<std::array<float, 16>>> buildAttendPokemonSkinMatrices(
    const AttendPokemonModel& model,
    const std::vector<std::array<float, 16>>& globals);
void skinAttendPokemonPrimitive(
    const AttendPokemonModel& model,
    const AttendPokemonPrimitive& primitive,
    const AttendPokemonAnimation* animation,
    double scene_time_seconds,
    std::vector<AttendPokemonVertex>& out_vertices,
    AttendPokemonPoseOverlay overlay = {});
void skinAttendPokemonPrimitiveWithPose(
    const AttendPokemonModel& model,
    const AttendPokemonPrimitive& primitive,
    const std::vector<std::array<float, 16>>& globals,
    const std::vector<std::vector<std::array<float, 16>>>& skin_matrices,
    std::vector<AttendPokemonVertex>& out_vertices);

} // namespace pr::gameplay::attend::rendering
