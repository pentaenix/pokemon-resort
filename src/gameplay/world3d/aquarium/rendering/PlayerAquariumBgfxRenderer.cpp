#include "gameplay/world3d/aquarium/rendering/PlayerAquariumBgfxRenderer.hpp"
#include "gameplay/world3d/aquarium/rendering/AquariumResourceGeneration.hpp"

#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace pr::gameplay::world3d::aquarium::rendering {
namespace geo = pr::aquarium::geometry;

namespace {

std::uint32_t packAbgr(float r, float g, float b, float a) {
    const auto byte = [](float value) {
        return static_cast<std::uint32_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
    };
    return byte(r) | (byte(g) << 8U) | (byte(b) << 16U) | (byte(a) << 24U);
}

std::array<float, 4> materialColor(geo::MeshMaterial material) {
    switch (material) {
    case geo::MeshMaterial::Structure: return {0.12f, 0.23f, 0.36f, 1.0f};
    case geo::MeshMaterial::Sand: return {0.88f, 0.76f, 0.50f, 1.0f};
    case geo::MeshMaterial::Water: return {0.18f, 0.62f, 0.78f, 0.38f};
    case geo::MeshMaterial::Glass: return {0.58f, 0.86f, 0.94f, 0.31f};
    }
    return {1.0f, 1.0f, 1.0f, 1.0f};
}

} // namespace

class PlayerAquariumBgfxRenderer::Impl {
public:
    struct Vertex {
        float x, y, z;
        std::uint32_t abgr;
        float u, v;
        float nx, ny, nz;
    };
    struct Mesh {
        bgfx::VertexBufferHandle vertices = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle indices = BGFX_INVALID_HANDLE;
        std::uint32_t index_count = 0;
        geo::MeshMaterial material = geo::MeshMaterial::Structure;
        std::string tank_id;
        float center_x = 0.0f;
        float center_y = 0.0f;
        float center_z = 0.0f;
        float transform[16]{};

        void destroy() {
            if (bgfx::isValid(vertices)) bgfx::destroy(vertices);
            if (bgfx::isValid(indices)) bgfx::destroy(indices);
            vertices = BGFX_INVALID_HANDLE;
            indices = BGFX_INVALID_HANDLE;
        }
    };

    void initialize(
        const bgfx::VertexLayout& layout, bgfx::ProgramHandle program,
        bgfx::TextureHandle white_texture, bgfx::UniformHandle texture_uniform,
        bgfx::UniformHandle tint_cutoff_uniform, bgfx::UniformHandle color_adjust_uniform,
        bgfx::UniformHandle texture_blur_uniform, bgfx::UniformHandle uv_offset_uniform,
        bgfx::UniformHandle light_dir_uniform, bgfx::UniformHandle light_params_uniform) {
        layout_ = layout;
        program_ = program;
        white_texture_ = white_texture;
        texture_uniform_ = texture_uniform;
        tint_cutoff_uniform_ = tint_cutoff_uniform;
        color_adjust_uniform_ = color_adjust_uniform;
        texture_blur_uniform_ = texture_blur_uniform;
        uv_offset_uniform_ = uv_offset_uniform;
        light_dir_uniform_ = light_dir_uniform;
        light_params_uniform_ = light_params_uniform;
        initialized_ = true;
        if (!pending_tanks_.empty()) {
            std::string ignored;
            replaceTanks(pending_tanks_, &ignored);
            pending_tanks_.clear();
        }
    }

    void shutdown() {
        resources_.clear([](Mesh& mesh) { mesh.destroy(); });
        initialized_ = false;
    }

    bool replaceTanks(
        const std::vector<construction::PlayerTankRuntime>& tanks,
        std::string* error) {
        if (!initialized_) {
            pending_tanks_ = tanks;
            if (error) error->clear();
            return true;
        }
        if (!stageTanks(tanks, error)) return false;
        return publishStagedTanks();
    }

    bool stageTanks(
        const std::vector<construction::PlayerTankRuntime>& tanks,
        std::string* error) {
        if (!initialized_) {
            if (error) *error = "Aquarium renderer is not initialized";
            return false;
        }
        const auto destroy = [](Mesh& mesh) { mesh.destroy(); };
        resources_.discardStaged(destroy);
        std::vector<Mesh> candidate;
        for (const auto& tank : tanks) {
            for (const geo::SemanticMesh& source : tank.build.meshes.meshes) {
                if (source.vertices.empty() || source.indices.empty()) continue;
                const auto color = materialColor(source.material);
                std::vector<Vertex> vertices;
                vertices.reserve(source.vertices.size());
                for (const geo::Vertex& vertex : source.vertices) {
                    vertices.push_back({
                        vertex.position.x, vertex.position.y, vertex.position.z,
                        packAbgr(color[0], color[1], color[2], color[3]),
                        vertex.uv.x, vertex.uv.y,
                        vertex.normal.x, vertex.normal.y, vertex.normal.z,
                    });
                }
                Mesh mesh;
                mesh.material = source.material;
                mesh.tank_id = tank.design.id;
                mesh.center_x = tank.world_center_x;
                mesh.center_y = tank.world_floor_y;
                mesh.center_z = tank.world_center_z;
                mesh.index_count = static_cast<std::uint32_t>(source.indices.size());
                bx::mtxTranslate(mesh.transform,
                    tank.world_center_x, tank.world_floor_y, tank.world_center_z);
                mesh.vertices = bgfx::createVertexBuffer(bgfx::copy(
                    vertices.data(), static_cast<std::uint32_t>(vertices.size() * sizeof(Vertex))), layout_);
                mesh.indices = bgfx::createIndexBuffer(bgfx::copy(
                    source.indices.data(),
                    static_cast<std::uint32_t>(source.indices.size() * sizeof(std::uint32_t))),
                    BGFX_BUFFER_INDEX32);
                if (!bgfx::isValid(mesh.vertices) || !bgfx::isValid(mesh.indices)) {
                    mesh.destroy();
                    resources_.stage(std::move(candidate), false, destroy);
                    if (error) *error = "Could not upload candidate aquarium mesh";
                    return false;
                }
                candidate.push_back(std::move(mesh));
            }
        }
        resources_.stage(std::move(candidate), true, destroy);
        if (error) error->clear();
        return true;
    }

    bool publishStagedTanks() {
        return resources_.publishStaged([](Mesh& previous) { previous.destroy(); });
    }

    void discardStagedTanks() {
        resources_.discardStaged([](Mesh& candidate) { candidate.destroy(); });
    }

    void submit(std::uint16_t view_id, bool transparent,
        float camera_x = 0.0f, float camera_y = 0.0f, float camera_z = 0.0f) {
        if (!initialized_ || !bgfx::isValid(program_)) return;
        const float tint[4]{1.0f, 1.0f, 1.0f, 0.0f};
        const float adjust[4]{1.0f, 1.0f, 1.0f, 0.0f};
        const float zeros[4]{};
        const float light_dir[4]{-0.35f, 0.82f, 0.45f, 0.0f};
        const float light_params[4]{0.78f, 0.28f, 0.12f, 0.0f};
        std::vector<const Mesh*> ordered;
        ordered.reserve(resources_.active().size());
        for (const Mesh& mesh : resources_.active()) ordered.push_back(&mesh);
        if (transparent) {
            std::stable_sort(ordered.begin(), ordered.end(), [&](const Mesh* lhs, const Mesh* rhs) {
                const auto distance_squared = [&](const Mesh* mesh) {
                    const float dx = mesh->center_x - camera_x;
                    const float dy = mesh->center_y - camera_y;
                    const float dz = mesh->center_z - camera_z;
                    return dx * dx + dy * dy + dz * dz;
                };
                const float lhs_distance = distance_squared(lhs);
                const float rhs_distance = distance_squared(rhs);
                if (std::abs(lhs_distance - rhs_distance) > 0.001f) {
                    return lhs_distance > rhs_distance;
                }
                if (lhs->tank_id != rhs->tank_id) return lhs->tank_id < rhs->tank_id;
                return static_cast<int>(lhs->material) < static_cast<int>(rhs->material);
            });
        }
        for (const Mesh* mesh_ptr : ordered) {
            const Mesh& mesh = *mesh_ptr;
            const bool is_transparent = mesh.material == geo::MeshMaterial::Water ||
                                        mesh.material == geo::MeshMaterial::Glass;
            if (is_transparent != transparent) continue;
            bgfx::setTransform(mesh.transform);
            bgfx::setVertexBuffer(0, mesh.vertices);
            bgfx::setIndexBuffer(mesh.indices, 0, mesh.index_count);
            bgfx::setTexture(0, texture_uniform_, white_texture_);
            bgfx::setUniform(tint_cutoff_uniform_, tint);
            bgfx::setUniform(color_adjust_uniform_, adjust);
            bgfx::setUniform(texture_blur_uniform_, zeros);
            bgfx::setUniform(uv_offset_uniform_, zeros);
            bgfx::setUniform(light_dir_uniform_, light_dir);
            bgfx::setUniform(light_params_uniform_, light_params);
            const bool double_sided_surface = mesh.material == geo::MeshMaterial::Sand ||
                mesh.material == geo::MeshMaterial::Water;
            const std::uint64_t culling = double_sided_surface ? 0 : BGFX_STATE_CULL_CCW;
            const std::uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                BGFX_STATE_DEPTH_TEST_LESS | culling |
                (transparent ? BGFX_STATE_BLEND_ALPHA : BGFX_STATE_WRITE_Z);
            bgfx::setState(state);
            bgfx::submit(view_id, program_);
        }
    }

    bgfx::VertexLayout layout_{};
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle white_texture_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle texture_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle tint_cutoff_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle color_adjust_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle texture_blur_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uv_offset_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle light_dir_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle light_params_uniform_ = BGFX_INVALID_HANDLE;
    AquariumResourceGeneration<Mesh> resources_;
    std::vector<construction::PlayerTankRuntime> pending_tanks_;
    bool initialized_ = false;
};

PlayerAquariumBgfxRenderer::PlayerAquariumBgfxRenderer() : impl_(std::make_unique<Impl>()) {}
PlayerAquariumBgfxRenderer::~PlayerAquariumBgfxRenderer() = default;
void PlayerAquariumBgfxRenderer::initialize(
    const bgfx::VertexLayout& layout, bgfx::ProgramHandle program,
    bgfx::TextureHandle white_texture, bgfx::UniformHandle texture_uniform,
    bgfx::UniformHandle tint_cutoff_uniform, bgfx::UniformHandle color_adjust_uniform,
    bgfx::UniformHandle texture_blur_uniform, bgfx::UniformHandle uv_offset_uniform,
    bgfx::UniformHandle light_dir_uniform, bgfx::UniformHandle light_params_uniform) {
    impl_->initialize(layout, program, white_texture, texture_uniform, tint_cutoff_uniform,
        color_adjust_uniform, texture_blur_uniform, uv_offset_uniform,
        light_dir_uniform, light_params_uniform);
}
void PlayerAquariumBgfxRenderer::shutdown() { impl_->shutdown(); }
bool PlayerAquariumBgfxRenderer::replaceTanks(
    const std::vector<construction::PlayerTankRuntime>& tanks, std::string* error) {
    return impl_->replaceTanks(tanks, error);
}
bool PlayerAquariumBgfxRenderer::stageTanks(
    const std::vector<construction::PlayerTankRuntime>& tanks, std::string* error) {
    return impl_->stageTanks(tanks, error);
}
bool PlayerAquariumBgfxRenderer::publishStagedTanks() { return impl_->publishStagedTanks(); }
void PlayerAquariumBgfxRenderer::discardStagedTanks() { impl_->discardStagedTanks(); }
void PlayerAquariumBgfxRenderer::submitOpaque(std::uint16_t view_id) { impl_->submit(view_id, false); }
void PlayerAquariumBgfxRenderer::submitTransparent(
    std::uint16_t view_id, float camera_x, float camera_y, float camera_z) {
    impl_->submit(view_id, true, camera_x, camera_y, camera_z);
}
std::size_t PlayerAquariumBgfxRenderer::resourceCount() const {
    return impl_->resources_.active().size() * 2U;
}

} // namespace pr::gameplay::world3d::aquarium::rendering
