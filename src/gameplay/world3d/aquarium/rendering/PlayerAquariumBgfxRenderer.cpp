#include "gameplay/world3d/aquarium/rendering/PlayerAquariumBgfxRenderer.hpp"
#include "gameplay/world3d/aquarium/rendering/AquariumResourceGeneration.hpp"
#include "gameplay/world3d/aquarium/rendering/AquariumBoundedFog.hpp"
#include "gameplay/world3d/aquarium/AquariumExhibitPreset.hpp"
#include "gameplay/world3d/aquarium/AquariumSubstratePreset.hpp"

#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>

namespace pr::gameplay::world3d::aquarium::rendering {
namespace geo = pr::aquarium::geometry;

namespace {

constexpr std::uint32_t kResourceRetirementDelayFrames = 3;

std::uint32_t packAbgr(float r, float g, float b, float a) {
    const auto byte = [](float value) {
        return static_cast<std::uint32_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
    };
    return byte(r) | (byte(g) << 8U) | (byte(b) << 16U) | (byte(a) << 24U);
}

std::array<float, 4> litWaterColor(
    const std::array<float, 4>& base,
    const AquariumExhibitPreset& exhibit,
    float brightness) {
    const float highlight = std::max(0.0f, brightness - 1.0f) * 0.65f;
    return {
        base[0] * brightness + exhibit.spill_color[0] * highlight,
        base[1] * brightness + exhibit.spill_color[1] * highlight,
        base[2] * brightness + exhibit.spill_color[2] * highlight,
        base[3]};
}

std::array<float, 4> materialColor(
    geo::MeshMaterial material,
    const AquariumExhibitPreset& exhibit,
    float brightness) {
    switch (material) {
    case geo::MeshMaterial::Structure: return {0.12f, 0.23f, 0.36f, 1.0f};
    // The RTPKS exterior sand already carries its warm hue. Keep this tint
    // nearly neutral and slightly desaturated for underwater presentation.
    case geo::MeshMaterial::Sand: return {
        0.86f * exhibit.sand_tint[0] * exhibit.sand_brightness_multiplier * brightness,
        0.84f * exhibit.sand_tint[1] * exhibit.sand_brightness_multiplier * brightness,
        0.78f * exhibit.sand_tint[2] * exhibit.sand_brightness_multiplier * brightness,
        1.0f};
    case geo::MeshMaterial::WaterVolume:
        return litWaterColor(exhibit.water_volume, exhibit, brightness);
    case geo::MeshMaterial::WaterSurface:
        return litWaterColor(exhibit.water_surface, exhibit, brightness);
    case geo::MeshMaterial::Glass: return {0.65f, 0.90f, 0.96f, 0.15f};
    case geo::MeshMaterial::TunnelFrame: return {0.48f, 0.54f, 0.60f, 1.0f};
    }
    return {1.0f, 1.0f, 1.0f, 1.0f};
}

float wrappedUvLerp(float a, float b, float amount, float period) {
    float delta = b - a;
    if (period > 0.0f) delta -= std::round(delta / period) * period;
    return a + delta * amount;
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
        float water_bottom_y = 0.0f;
        float water_surface_y = 1.0f;
        float longest_span_world = static_cast<float>(geo::kWorldUnitsPerCell);
        float half_width_world = static_cast<float>(geo::kWorldUnitsPerCell);
        float half_depth_world = static_cast<float>(geo::kWorldUnitsPerCell);
        float water_attenuation_multiplier = 1.0f;
        float tank_brightness = 1.0f;
        std::array<float, 3> fog_color{};
        float fog_visibility_world = 160.0f;
        float fog_maximum_opacity = 0.34f;
        std::size_t substrate_index = 0;
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
        bgfx::ProgramHandle water_program,
        bgfx::ProgramHandle glass_program,
        bgfx::ProgramHandle fog_program,
        bgfx::TextureHandle white_texture,
        const std::array<bgfx::TextureHandle, kAquariumSubstratePresets.size()>&
            substrate_textures,
        AquariumWaterSurfaceMaterial water_surface_material,
        bgfx::UniformHandle texture_uniform,
        bgfx::UniformHandle tint_cutoff_uniform, bgfx::UniformHandle color_adjust_uniform,
        bgfx::UniformHandle texture_blur_uniform, bgfx::UniformHandle uv_offset_uniform,
        bgfx::UniformHandle light_dir_uniform, bgfx::UniformHandle light_params_uniform) {
        layout_ = layout;
        program_ = program;
        water_program_ = water_program;
        glass_program_ = glass_program;
        fog_program_ = fog_program;
        white_texture_ = white_texture;
        substrate_textures_ = substrate_textures;
        water_surface_material_ = std::move(water_surface_material);
        texture_uniform_ = texture_uniform;
        tint_cutoff_uniform_ = tint_cutoff_uniform;
        color_adjust_uniform_ = color_adjust_uniform;
        texture_blur_uniform_ = texture_blur_uniform;
        uv_offset_uniform_ = uv_offset_uniform;
        light_dir_uniform_ = light_dir_uniform;
        light_params_uniform_ = light_params_uniform;
        water_bounds_uniform_ = bgfx::createUniform(
            "u_aquariumWaterBounds", bgfx::UniformType::Vec4);
        water_camera_uniform_ = bgfx::createUniform(
            "u_aquariumWaterCamera", bgfx::UniformType::Vec4);
        water_extents_uniform_ = bgfx::createUniform(
            "u_aquariumWaterExtents", bgfx::UniformType::Vec4);
        fog_scene_depth_uniform_ = bgfx::createUniform(
            "s_aquariumSceneDepth", bgfx::UniformType::Sampler);
        fog_inverse_view_projection_uniform_ = bgfx::createUniform(
            "u_aquariumFogInvViewProj", bgfx::UniformType::Mat4);
        fog_camera_uniform_ = bgfx::createUniform(
            "u_aquariumFogCamera", bgfx::UniformType::Vec4);
        fog_box_min_uniform_ = bgfx::createUniform(
            "u_aquariumFogBoxMin", bgfx::UniformType::Vec4);
        fog_box_max_uniform_ = bgfx::createUniform(
            "u_aquariumFogBoxMax", bgfx::UniformType::Vec4);
        fog_params_uniform_ = bgfx::createUniform(
            "u_aquariumFogParams", bgfx::UniformType::Vec4);
        fog_color_uniform_ = bgfx::createUniform(
            "u_aquariumFogColor", bgfx::UniformType::Vec4);
        initialized_ = true;
        if (!pending_tanks_.empty()) {
            std::string ignored;
            replaceTanks(pending_tanks_, &ignored);
            pending_tanks_.clear();
        }
    }

    void shutdown() {
        resources_.clear([](Mesh& mesh) { mesh.destroy(); });
        if (bgfx::isValid(water_bounds_uniform_)) bgfx::destroy(water_bounds_uniform_);
        if (bgfx::isValid(water_camera_uniform_)) bgfx::destroy(water_camera_uniform_);
        if (bgfx::isValid(water_extents_uniform_)) bgfx::destroy(water_extents_uniform_);
        if (bgfx::isValid(fog_scene_depth_uniform_)) bgfx::destroy(fog_scene_depth_uniform_);
        if (bgfx::isValid(fog_inverse_view_projection_uniform_)) bgfx::destroy(fog_inverse_view_projection_uniform_);
        if (bgfx::isValid(fog_camera_uniform_)) bgfx::destroy(fog_camera_uniform_);
        if (bgfx::isValid(fog_box_min_uniform_)) bgfx::destroy(fog_box_min_uniform_);
        if (bgfx::isValid(fog_box_max_uniform_)) bgfx::destroy(fog_box_max_uniform_);
        if (bgfx::isValid(fog_params_uniform_)) bgfx::destroy(fog_params_uniform_);
        if (bgfx::isValid(fog_color_uniform_)) bgfx::destroy(fog_color_uniform_);
        water_bounds_uniform_ = BGFX_INVALID_HANDLE;
        water_camera_uniform_ = BGFX_INVALID_HANDLE;
        water_extents_uniform_ = BGFX_INVALID_HANDLE;
        fog_scene_depth_uniform_ = BGFX_INVALID_HANDLE;
        fog_inverse_view_projection_uniform_ = BGFX_INVALID_HANDLE;
        fog_camera_uniform_ = BGFX_INVALID_HANDLE;
        fog_box_min_uniform_ = BGFX_INVALID_HANDLE;
        fog_box_max_uniform_ = BGFX_INVALID_HANDLE;
        fog_params_uniform_ = BGFX_INVALID_HANDLE;
        fog_color_uniform_ = BGFX_INVALID_HANDLE;
        initialized_ = false;
    }

    void setLighting(
        float brightness,
        const std::array<float, 3>& tint,
        float water_attenuation_intensity,
        float water_surface_speed,
        float sand_darkening) {
        lighting_brightness_ = std::max(0.0f, brightness);
        lighting_tint_ = tint;
        water_attenuation_intensity_ = std::isfinite(water_attenuation_intensity)
            ? std::max(0.0f, water_attenuation_intensity)
            : 1.0f;
        water_surface_speed_ = std::isfinite(water_surface_speed)
            ? std::clamp(water_surface_speed, 0.0f, 4.0f)
            : 1.0f;
        sand_darkening_ = std::isfinite(sand_darkening)
            ? std::clamp(sand_darkening, 0.0f, 1.0f)
            : 0.0f;
    }

    void setAnimationClock(bool enabled, double time_seconds) {
        animations_enabled_ = enabled;
        animation_time_seconds_ = std::isfinite(time_seconds)
            ? std::max(0.0, time_seconds)
            : 0.0;
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
            const AquariumExhibitPreset& exhibit =
                aquariumExhibitPreset(tank.design.exhibit_preset);
            const float tank_brightness = aquariumBrightnessMultiplier(
                tank.design.brightness_level);
            const float substrate_world_units = tank.design.substrate_kind == "moss-flat"
                ? static_cast<float>(geo::kWorldUnitsPerCell * 2)
                : static_cast<float>(geo::kWorldUnitsPerCell);
            float water_bottom_y = 0.0f;
            float water_surface_y = static_cast<float>(
                tank.design.height_steps * geo::kVerticalStepWorldUnits);
            if (!tank.build.navigation.layers.empty()) {
                water_bottom_y = tank.build.navigation.layers.front().floor_y;
                water_surface_y = tank.build.navigation.layers.front().ceiling_y;
                for (const auto& layer : tank.build.navigation.layers) {
                    water_bottom_y = std::min(water_bottom_y, layer.floor_y);
                    water_surface_y = std::max(water_surface_y, layer.ceiling_y);
                }
            }
            const float longest_span_world = static_cast<float>(
                std::max(geo::occupiedWidthCells(tank.design.footprint),
                    geo::occupiedDepthCells(tank.design.footprint)) *
                geo::kWorldUnitsPerCell);
            for (const geo::SemanticMesh& source : tank.build.meshes.meshes) {
                if (source.vertices.empty() || source.indices.empty()) continue;
                // Keep tank brightness in floating point until draw time.
                // Baking it into normalized 8-bit vertex colors made upper
                // slider ticks saturate during upload and appear ineffective.
                auto color = materialColor(source.material, exhibit, 1.0f);
                if (source.material == geo::MeshMaterial::WaterSurface &&
                    bgfx::isValid(water_surface_material_.texture)) {
                    color[3] *= std::clamp(water_surface_material_.opacity, 0.0f, 1.0f);
                }
                std::vector<Vertex> vertices;
                vertices.reserve(source.vertices.size());
                for (const geo::Vertex& vertex : source.vertices) {
                    float texture_u = vertex.uv.x;
                    float texture_v = vertex.uv.y;
                    if (source.material == geo::MeshMaterial::Sand) {
                        texture_u = vertex.position.x / substrate_world_units;
                        texture_v = vertex.position.z / substrate_world_units;
                    } else if (source.material == geo::MeshMaterial::WaterSurface &&
                        bgfx::isValid(water_surface_material_.texture)) {
                        const float world_cell_x =
                            (vertex.position.x + tank.world_center_x) /
                            static_cast<float>(geo::kWorldUnitsPerCell);
                        const float world_cell_z =
                            (vertex.position.z + tank.world_center_z) /
                            static_cast<float>(geo::kWorldUnitsPerCell);
                        texture_u = world_cell_x * water_surface_material_.u_per_tile[0] +
                            world_cell_z * water_surface_material_.u_per_tile[1];
                        texture_v = world_cell_x * water_surface_material_.v_per_tile[0] +
                            world_cell_z * water_surface_material_.v_per_tile[1];
                    }
                    vertices.push_back({
                        vertex.position.x, vertex.position.y, vertex.position.z,
                        packAbgr(color[0], color[1], color[2], color[3]),
                        texture_u, texture_v,
                        vertex.normal.x, vertex.normal.y, vertex.normal.z,
                    });
                }
                Mesh mesh;
                mesh.material = source.material;
                mesh.tank_id = tank.design.id;
                mesh.center_x = tank.world_center_x;
                mesh.center_y = tank.world_floor_y;
                mesh.center_z = tank.world_center_z;
                mesh.water_bottom_y = water_bottom_y;
                mesh.water_surface_y = water_surface_y;
                mesh.longest_span_world = longest_span_world;
                mesh.half_width_world = static_cast<float>(
                    geo::occupiedWidthCells(tank.design.footprint) *
                    geo::kWorldUnitsPerCell) * 0.5f;
                mesh.half_depth_world = static_cast<float>(
                    geo::occupiedDepthCells(tank.design.footprint) *
                    geo::kWorldUnitsPerCell) * 0.5f;
                mesh.water_attenuation_multiplier = exhibit.attenuation_multiplier;
                mesh.tank_brightness = tank_brightness;
                mesh.water_attenuation_multiplier *= aquariumMurkinessMultiplier(
                    tank.design.murkiness_level);
                const auto fog_color = litWaterColor(
                    exhibit.water_volume, exhibit, tank_brightness);
                mesh.fog_color = {fog_color[0], fog_color[1], fog_color[2]};
                mesh.fog_visibility_world = aquariumFogVisibilityWorld(
                    tank.design.murkiness_level);
                mesh.fog_maximum_opacity = aquariumFogMaximumOpacity(
                    tank.design.murkiness_level);
                mesh.substrate_index = aquariumSubstratePresetIndex(
                    tank.design.substrate_kind);
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
        return resources_.publishStaged(
            [](Mesh& previous) { previous.destroy(); },
            kResourceRetirementDelayFrames);
    }

    void discardStagedTanks() {
        resources_.discardStaged([](Mesh& candidate) { candidate.destroy(); });
    }

    void advanceFrame() {
        resources_.advanceRetirements([](Mesh& retired) { retired.destroy(); });
    }

    void submit(std::uint16_t view_id, bool transparent,
        float camera_x = 0.0f, float camera_y = 0.0f, float camera_z = 0.0f) {
        if (!initialized_ || !bgfx::isValid(program_)) return;
        if(transparent && !decoration_focus_.empty())return;
        const float tint[4]{
            lighting_tint_[0] * lighting_brightness_,
            lighting_tint_[1] * lighting_brightness_,
            lighting_tint_[2] * lighting_brightness_,
            0.0f};
        const float adjust[4]{1.0f, 1.0f, 1.0f, 0.0f};
        const float zeros[4]{};
        const float light_dir[4]{-0.35f, 0.82f, 0.45f, 0.0f};
        const float light_params[4]{0.78f, 0.28f, 0.12f, 0.0f};
        float water_surface_uv_offset[4]{};
        if (!water_surface_material_.animation_uv_offsets.empty()) {
            const auto& offsets = water_surface_material_.animation_uv_offsets;
            const double raw_sample = (animations_enabled_ ? animation_time_seconds_ : 0.0) *
                static_cast<double>(std::max(0.0f, water_surface_material_.animation_timebase_hz)) *
                static_cast<double>(water_surface_speed_);
            const double bounded_sample = water_surface_material_.animation_loop
                ? std::fmod(raw_sample, static_cast<double>(offsets.size()))
                : std::min(raw_sample, static_cast<double>(offsets.size() - 1U));
            const std::size_t frame = static_cast<std::size_t>(std::floor(bounded_sample));
            const std::size_t next = water_surface_material_.animation_loop
                ? (frame + 1U) % offsets.size()
                : std::min(frame + 1U, offsets.size() - 1U);
            const float fraction = static_cast<float>(bounded_sample - std::floor(bounded_sample));
            // Ambient Nitro material matrices translate the texture rather
            // than the geometry, hence the inverse sampling direction.
            water_surface_uv_offset[0] = -wrappedUvLerp(
                offsets[frame][0], offsets[next][0], fraction,
                water_surface_material_.uv_wrap_period[0]);
            water_surface_uv_offset[1] = -wrappedUvLerp(
                offsets[frame][1], offsets[next][1], fraction,
                water_surface_material_.uv_wrap_period[1]);
        }
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
            if(!decoration_focus_.empty() && mesh.tank_id!=decoration_focus_)continue;
            if(std::find(inspection_hidden_.begin(),inspection_hidden_.end(),mesh.tank_id)!=inspection_hidden_.end())continue;
            const bool is_transparent = mesh.material == geo::MeshMaterial::WaterVolume ||
                                        mesh.material == geo::MeshMaterial::WaterSurface ||
                                        mesh.material == geo::MeshMaterial::Glass;
            if (is_transparent != transparent) continue;
            // The closed water-volume proxy is consumed by the bounded fog
            // composite. Drawing it as ordinary alpha geometry cannot account
            // for the opaque surface depth and made the murkiness control inert.
            if (mesh.material == geo::MeshMaterial::WaterVolume) continue;
            bgfx::setTransform(mesh.transform);
            bgfx::setVertexBuffer(0, mesh.vertices);
            bgfx::setIndexBuffer(mesh.indices, 0, mesh.index_count);
            const bool textured_sand = mesh.material == geo::MeshMaterial::Sand &&
                mesh.substrate_index < substrate_textures_.size() &&
                bgfx::isValid(substrate_textures_[mesh.substrate_index]);
            const bool textured_water_surface =
                mesh.material == geo::MeshMaterial::WaterSurface &&
                bgfx::isValid(water_surface_material_.texture);
            const bool glass = mesh.material == geo::MeshMaterial::Glass;
            constexpr std::uint32_t kPointRepeatSampler =
                BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT |
                BGFX_SAMPLER_MIP_POINT;
            bgfx::setTexture(0, texture_uniform_,
                textured_sand ? substrate_textures_[mesh.substrate_index] :
                    textured_water_surface ? water_surface_material_.texture : white_texture_,
                textured_sand || textured_water_surface ? kPointRepeatSampler : UINT32_MAX);
            const bool tank_lighted_material =
                mesh.material == geo::MeshMaterial::Sand ||
                mesh.material == geo::MeshMaterial::WaterSurface;
            const float material_brightness = tank_lighted_material
                ? mesh.tank_brightness : 1.0f;
            const float sand_brightness = mesh.material == geo::MeshMaterial::Sand
                ? 1.0f - sand_darkening_ : 1.0f;
            const float draw_tint[4]{
                tint[0] * sand_brightness * material_brightness,
                tint[1] * sand_brightness * material_brightness,
                tint[2] * sand_brightness * material_brightness,
                tint[3],
            };
            bgfx::setUniform(tint_cutoff_uniform_, draw_tint);
            bgfx::setUniform(color_adjust_uniform_, adjust);
            bgfx::setUniform(texture_blur_uniform_, zeros);
            bgfx::setUniform(uv_offset_uniform_,
                textured_water_surface ? water_surface_uv_offset : zeros);
            bgfx::setUniform(light_dir_uniform_, light_dir);
            bgfx::setUniform(light_params_uniform_, light_params);
            const bool water = mesh.material == geo::MeshMaterial::WaterVolume ||
                mesh.material == geo::MeshMaterial::WaterSurface;
            if (water || glass) {
                const float bounds[4]{
                    mesh.water_bottom_y,
                    mesh.water_surface_y,
                    mesh.longest_span_world,
                    water_attenuation_intensity_ * mesh.water_attenuation_multiplier,
                };
                const float camera[4]{
                    camera_x - mesh.center_x,
                    camera_y - mesh.center_y,
                    camera_z - mesh.center_z,
                    mesh.material == geo::MeshMaterial::WaterSurface ? 1.0f : 0.0f,
                };
                bgfx::setUniform(water_bounds_uniform_, bounds);
                bgfx::setUniform(water_camera_uniform_, camera);
                const float extents[4]{
                    mesh.half_width_world,
                    std::max(1.0f, (mesh.water_surface_y - mesh.water_bottom_y) * 0.5f),
                    mesh.half_depth_world,
                    (mesh.water_surface_y + mesh.water_bottom_y) * 0.5f,
                };
                bgfx::setUniform(water_extents_uniform_, extents);
            }
            const bool double_sided_surface = mesh.material == geo::MeshMaterial::Sand ||
                mesh.material == geo::MeshMaterial::WaterSurface;
            const std::uint64_t culling = double_sided_surface ? 0 : BGFX_STATE_CULL_CCW;
            const std::uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                BGFX_STATE_DEPTH_TEST_LESS | culling |
                (transparent ? BGFX_STATE_BLEND_ALPHA : BGFX_STATE_WRITE_Z);
            bgfx::setState(state);
            const bgfx::ProgramHandle draw_program =
                water && bgfx::isValid(water_program_) ? water_program_ :
                glass && bgfx::isValid(glass_program_) ? glass_program_ : program_;
            bgfx::submit(view_id, draw_program);
        }
    }

    void submitFog(
        std::uint16_t view_id,
        bgfx::TextureHandle scene_depth,
        const float* inverse_view_projection,
        float camera_x,
        float camera_y,
        float camera_z,
        bool homogeneous_depth) {
        if (!initialized_ || !bgfx::isValid(fog_program_) ||
            !bgfx::isValid(scene_depth) ||
            inverse_view_projection == nullptr) {
            return;
        }
        constexpr std::uint32_t kPointClampSampler =
            BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT |
            BGFX_SAMPLER_MIP_POINT | BGFX_SAMPLER_U_CLAMP |
            BGFX_SAMPLER_V_CLAMP;
        const float camera[4]{camera_x, camera_y, camera_z, 0.0f};
        std::vector<const Mesh*> ordered;
        ordered.reserve(resources_.active().size());
        for (const Mesh& mesh : resources_.active()) {
            if(std::find(inspection_hidden_.begin(),inspection_hidden_.end(),mesh.tank_id)!=inspection_hidden_.end())continue;
            if (mesh.material == geo::MeshMaterial::WaterVolume ||
                mesh.material == geo::MeshMaterial::WaterSurface) {
                ordered.push_back(&mesh);
            }
        }
        // Premultiplied fog operators compose back-to-front. Keep the two
        // complementary proxy surfaces for one tank adjacent and deterministic.
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
        std::string stencil_tank_id;
        std::uint32_t stencil_reference = 0;
        for (const Mesh* mesh_ptr : ordered) {
            const Mesh& mesh = *mesh_ptr;
            // The generated water volume contains perimeter sides while the
            // surface closes its top. Submit both as one conservative volume
            // silhouette so high-angle views fog the complete tank interior.
            const float box_min[4]{
                mesh.center_x - mesh.half_width_world,
                mesh.center_y + mesh.water_bottom_y,
                mesh.center_z - mesh.half_depth_world,
                0.0f};
            const float box_max[4]{
                mesh.center_x + mesh.half_width_world,
                mesh.center_y + mesh.water_surface_y,
                mesh.center_z + mesh.half_depth_world,
                0.0f};
            const float attenuation_scale = std::max(
                0.0f,
                water_attenuation_intensity_ *
                    mesh.water_attenuation_multiplier);
            const float params[4]{
                2.0f,
                std::max(
                    mesh.fog_visibility_world /
                        std::max(attenuation_scale, 0.001f),
                    4.001f),
                1.0f,
                mesh.fog_maximum_opacity *
                    std::clamp(attenuation_scale, 0.0f, 1.0f)};
            const float color[4]{
                mesh.fog_color[0], mesh.fog_color[1], mesh.fog_color[2],
                homogeneous_depth ? 1.0f : 0.0f};
            bgfx::setTransform(mesh.transform);
            bgfx::setVertexBuffer(0, mesh.vertices);
            bgfx::setIndexBuffer(mesh.indices, 0, mesh.index_count);
            bgfx::setTexture(
                0, fog_scene_depth_uniform_, scene_depth, kPointClampSampler);
            bgfx::setUniform(
                fog_inverse_view_projection_uniform_, inverse_view_projection);
            bgfx::setUniform(fog_camera_uniform_, camera);
            bgfx::setUniform(fog_box_min_uniform_, box_min);
            bgfx::setUniform(fog_box_max_uniform_, box_max);
            bgfx::setUniform(fog_params_uniform_, params);
            bgfx::setUniform(fog_color_uniform_, color);
            if (mesh.tank_id != stencil_tank_id) {
                stencil_tank_id = mesh.tank_id;
                stencil_reference = stencil_reference % 254U + 1U;
            }
            bgfx::setStencil(
                BGFX_STENCIL_TEST_NOTEQUAL |
                BGFX_STENCIL_FUNC_REF(stencil_reference) |
                BGFX_STENCIL_FUNC_RMASK(0xff) |
                BGFX_STENCIL_OP_FAIL_S_KEEP |
                BGFX_STENCIL_OP_FAIL_Z_KEEP |
                BGFX_STENCIL_OP_PASS_Z_REPLACE);
            // Both sides are intentional: front faces cover an outside camera,
            // while back faces preserve continuous fog when the camera enters a
            // tank or tunnel. Per-tank stencil prevents those faces from
            // applying the same attenuation more than once at a pixel.
            bgfx::setState(
                BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                BGFX_STATE_BLEND_FUNC(
                    BGFX_STATE_BLEND_ONE,
                    BGFX_STATE_BLEND_INV_SRC_ALPHA));
            bgfx::submit(view_id, fog_program_);
        }
    }

    bgfx::VertexLayout layout_{};
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle water_program_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle glass_program_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle fog_program_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle white_texture_ = BGFX_INVALID_HANDLE;
    std::array<bgfx::TextureHandle, kAquariumSubstratePresets.size()>
        substrate_textures_{};
    AquariumWaterSurfaceMaterial water_surface_material_{};
    bgfx::UniformHandle texture_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle tint_cutoff_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle color_adjust_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle texture_blur_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uv_offset_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle light_dir_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle light_params_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle water_bounds_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle water_camera_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle water_extents_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle fog_scene_depth_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle fog_inverse_view_projection_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle fog_camera_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle fog_box_min_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle fog_box_max_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle fog_params_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle fog_color_uniform_ = BGFX_INVALID_HANDLE;
    AquariumResourceGeneration<Mesh> resources_;
    std::vector<construction::PlayerTankRuntime> pending_tanks_;
    std::string decoration_focus_;
    std::vector<std::string> inspection_hidden_;
    std::array<float, 3> lighting_tint_{1.0f, 1.0f, 1.0f};
    float lighting_brightness_ = 1.0f;
    float water_attenuation_intensity_ = 1.0f;
    float water_surface_speed_ = 1.0f;
    float sand_darkening_ = 0.0f;
    bool animations_enabled_ = true;
    double animation_time_seconds_ = 0.0;
    bool initialized_ = false;
};

PlayerAquariumBgfxRenderer::PlayerAquariumBgfxRenderer() : impl_(std::make_unique<Impl>()) {}
PlayerAquariumBgfxRenderer::~PlayerAquariumBgfxRenderer() = default;
void PlayerAquariumBgfxRenderer::initialize(
    const bgfx::VertexLayout& layout, bgfx::ProgramHandle program,
    bgfx::ProgramHandle water_program,
    bgfx::ProgramHandle glass_program,
    bgfx::ProgramHandle fog_program,
    bgfx::TextureHandle white_texture,
    const std::array<bgfx::TextureHandle, kAquariumSubstratePresets.size()>&
        substrate_textures,
    AquariumWaterSurfaceMaterial water_surface_material,
    bgfx::UniformHandle texture_uniform,
    bgfx::UniformHandle tint_cutoff_uniform, bgfx::UniformHandle color_adjust_uniform,
    bgfx::UniformHandle texture_blur_uniform, bgfx::UniformHandle uv_offset_uniform,
    bgfx::UniformHandle light_dir_uniform, bgfx::UniformHandle light_params_uniform) {
    impl_->initialize(layout, program, water_program, glass_program, fog_program,
        white_texture, substrate_textures,
        std::move(water_surface_material),
        texture_uniform, tint_cutoff_uniform,
        color_adjust_uniform, texture_blur_uniform, uv_offset_uniform,
        light_dir_uniform, light_params_uniform);
}
void PlayerAquariumBgfxRenderer::shutdown() { impl_->shutdown(); }
void PlayerAquariumBgfxRenderer::setLighting(
    float brightness,
    const std::array<float, 3>& tint,
    float water_attenuation_intensity,
    float water_surface_speed,
    float sand_darkening) {
    impl_->setLighting(
        brightness, tint, water_attenuation_intensity, water_surface_speed, sand_darkening);
}
void PlayerAquariumBgfxRenderer::setAnimationClock(bool enabled, double time_seconds) {
    impl_->setAnimationClock(enabled, time_seconds);
}
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
void PlayerAquariumBgfxRenderer::advanceFrame() { impl_->advanceFrame(); }
void PlayerAquariumBgfxRenderer::submitOpaque(std::uint16_t view_id) { impl_->submit(view_id, false); }
void PlayerAquariumBgfxRenderer::setDecorationFocus(std::string tank_id) {impl_->decoration_focus_=std::move(tank_id);}
void PlayerAquariumBgfxRenderer::setInspectionHiddenTanks(std::vector<std::string> tank_ids) {impl_->inspection_hidden_=std::move(tank_ids);}
void PlayerAquariumBgfxRenderer::submitFog(
    std::uint16_t view_id,
    bgfx::TextureHandle scene_depth,
    const float* inverse_view_projection,
    float camera_x,
    float camera_y,
    float camera_z,
    bool homogeneous_depth) {
    impl_->submitFog(
        view_id, scene_depth, inverse_view_projection,
        camera_x, camera_y, camera_z, homogeneous_depth);
}
void PlayerAquariumBgfxRenderer::submitTransparent(
    std::uint16_t view_id, float camera_x, float camera_y, float camera_z) {
    impl_->submit(view_id, true, camera_x, camera_y, camera_z);
}
std::size_t PlayerAquariumBgfxRenderer::resourceCount() const {
    return impl_->resources_.active().size() * 2U;
}
std::size_t PlayerAquariumBgfxRenderer::retiredResourceCount() const {
    return impl_->resources_.retiredResourceCount() * 2U;
}

} // namespace pr::gameplay::world3d::aquarium::rendering
