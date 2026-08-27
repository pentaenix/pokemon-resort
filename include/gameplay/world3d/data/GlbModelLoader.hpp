#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace pr::gameplay::world3d::data {

// Decoded, triangulated glTF-binary (.glb) model in model space.
//
// The loader bakes node transforms into vertex positions so the renderer only has to
// apply the per-placement transform (translate / yaw / scale). Geometry is grouped per
// material; each material carries the embedded base-color image bytes (PNG/JPEG) so the
// renderer can upload textures lazily against its own SDL_Renderer.

struct GlbVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
    int node = -1;
    std::vector<std::array<float, 3>> morph_position_deltas;
};

struct GlbMorphAnimationChannel {
    enum class Interpolation { Linear, Step, CubicSpline };

    int target_node = -1;
    int target_count = 0;
    Interpolation interpolation = Interpolation::Linear;
    std::vector<float> times;
    std::vector<float> weights; // key-major: times.size() * target_count
    std::vector<float> in_tangents;
    std::vector<float> out_tangents;
};

struct GlbRotationAnimationChannel {
    enum class Interpolation { Linear, Step };

    int target_node = -1;
    Interpolation interpolation = Interpolation::Linear;
    std::vector<float> times;
    std::vector<std::array<float, 4>> rotations;
};

struct GlbNodeTransform {
    std::array<float, 3> pivot_world{};
    std::array<float, 4> parent_world_rotation{0.0f, 0.0f, 0.0f, 1.0f};
    std::array<float, 4> base_local_rotation{0.0f, 0.0f, 0.0f, 1.0f};
};

struct GlbAnimation {
    std::string name;
    float duration_seconds = 0.0f;
    std::vector<GlbMorphAnimationChannel> morph_channels;
    std::vector<GlbRotationAnimationChannel> rotation_channels;
};

struct GlbMaterial {
    enum class AlphaMode {
        Opaque,
        Mask,
        Blend
    };

    enum class RenderClass {
        Opaque,
        Mask,
        Blend,
        UniformDecal
    };

    std::string name;
    std::vector<std::uint8_t> image_bytes; // embedded PNG/JPEG; empty when untextured
    bool has_texture = false;
    bool alpha_blend = false;              // legacy SDL fallback flag: glTF alphaMode MASK or BLEND
    AlphaMode alpha_mode = AlphaMode::Opaque;
    RenderClass render_class = RenderClass::Opaque;
    float alpha_cutoff = 0.5f;
    float base_color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
};

// glTF defines the effective base-color alpha as COLOR_0.a multiplied by the
// material's baseColorFactor.a. Keep this composition in one place so a
// renderer cannot accidentally turn translucent glass or soft-shadow meshes
// opaque when it starts preserving authored vertex colors.
inline float compositeGlbAlpha(const GlbVertex& vertex, const GlbMaterial& material) {
    return vertex.a * material.base_color[3];
}

struct GlbTriangle {
    GlbVertex a{};
    GlbVertex b{};
    GlbVertex c{};
    int material = -1;
};

struct GlbMesh {
    std::vector<GlbTriangle> triangles;
    std::vector<GlbMaterial> materials;
    std::vector<GlbAnimation> animations;
    std::vector<int> node_morph_target_counts;
    std::vector<GlbNodeTransform> node_transforms;
    bool valid = false;
    float aabb_min[3] = {0.0f, 0.0f, 0.0f};
    float aabb_max[3] = {0.0f, 0.0f, 0.0f};
};

// Load and triangulate a .glb file. Returns a mesh with valid=false on failure and, when
// provided, a human-readable reason in *error. Only self-contained .glb files (geometry and
// images embedded in the binary chunk) are supported; external buffers/URIs are rejected.
GlbMesh loadGlbModel(const std::string& path, std::string* error = nullptr);

// Samples the first authored animation as a looping clip. Empty per-node entries mean that
// node has no animated morph weights. Static models return an empty vector.
std::vector<std::vector<float>> sampleGlbMorphWeights(const GlbMesh& mesh, double time_seconds);
std::array<float, 3> sampleGlbMorphPosition(
    const GlbVertex& vertex,
    const std::vector<std::vector<float>>& node_weights);
GlbVertex applyGlbMorphWeights(const GlbVertex& vertex, const std::vector<std::vector<float>>& node_weights);

std::vector<std::array<float, 4>> sampleGlbNodeRotations(
    const GlbMesh& mesh,
    double time_seconds);
std::array<float, 3> sampleGlbAnimatedPosition(
    const GlbMesh& mesh,
    const GlbVertex& vertex,
    const std::vector<std::vector<float>>& node_weights,
    const std::vector<std::array<float, 4>>& node_rotations);

} // namespace pr::gameplay::world3d::data
