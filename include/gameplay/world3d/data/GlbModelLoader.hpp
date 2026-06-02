#pragma once

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
};

struct GlbMaterial {
    std::vector<std::uint8_t> image_bytes; // embedded PNG/JPEG; empty when untextured
    bool has_texture = false;
    bool alpha_blend = false;              // glTF alphaMode MASK or BLEND
    float base_color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
};

struct GlbTriangle {
    GlbVertex a{};
    GlbVertex b{};
    GlbVertex c{};
    int material = -1;
};

struct GlbMesh {
    std::vector<GlbTriangle> triangles;
    std::vector<GlbMaterial> materials;
    bool valid = false;
    float aabb_min[3] = {0.0f, 0.0f, 0.0f};
    float aabb_max[3] = {0.0f, 0.0f, 0.0f};
};

// Load and triangulate a .glb file. Returns a mesh with valid=false on failure and, when
// provided, a human-readable reason in *error. Only self-contained .glb files (geometry and
// images embedded in the binary chunk) are supported; external buffers/URIs are rejected.
GlbMesh loadGlbModel(const std::string& path, std::string* error = nullptr);

} // namespace pr::gameplay::world3d::data
