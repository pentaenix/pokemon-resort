#include "mapmaker/preview/ExactWorldPreview.hpp"

#include "gameplay/world3d/camera/Gen4CameraPreset.hpp"
#include "gameplay/world3d/data/JsonOverworldLoader.hpp"
#include "gameplay/world3d/rendering/PixelScale.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace pr::mapmaker {
namespace {

namespace fs = std::filesystem;
using Camera = gameplay::world3d::camera::Gen4FollowCamera;
using CameraPreset = gameplay::world3d::camera::Gen4CameraPreset;
using Vec3 = gameplay::world3d::camera::Vec3;
using Renderer = gameplay::world3d::rendering::bgfx_backend::OverworldBgfxRenderer;
using Scene = gameplay::world3d::SceneConfig;
using Binding = gameplay::world3d::terrain::ActorTerrainBinding;

constexpr float kPi = 3.14159265358979323846f;

struct StablePlayer {
    Vec3 position{};
    Binding binding{};
    SDL_Rect source_rect{};
    std::string activity_id;
    bool draw_shadow = true;
    bool valid = false;
};

fs::path normalizedAbsolute(fs::path path) {
    std::error_code error;
    fs::path absolute = path.is_absolute() ? path : fs::absolute(path, error);
    if (error) absolute = std::move(path);
    const fs::path canonical = fs::weakly_canonical(absolute, error);
    return error ? absolute.lexically_normal() : canonical;
}

fs::path resolveMapPath(const fs::path& project_root, const fs::path& requested) {
    if (requested.is_absolute()) return normalizedAbsolute(requested);
    const std::array<fs::path, 3> candidates{
        project_root / requested,
        project_root / "assets" / "overworld" / "maps" / requested,
        requested};
    std::error_code error;
    for (const fs::path& candidate : candidates) {
        if (fs::is_regular_file(candidate, error) && !error) return normalizedAbsolute(candidate);
        error.clear();
    }
    return (project_root / requested).lexically_normal();
}

CameraPreset unscaledScenePreset(const Scene& scene) {
    CameraPreset preset = gameplay::world3d::camera::loadGen4PresetById(scene.camera_preset.c_str());
    if (scene.camera_distance > 0.0f) preset.distance = scene.camera_distance;
    preset.pitch_deg = scene.camera_pitch_deg;
    preset.yaw_deg = scene.camera_yaw_deg;
    preset.roll_deg = scene.camera_roll_deg;
    preset.near_clip = scene.camera_near_clip;
    preset.far_clip = scene.camera_far_clip;
    preset.aspect_width = scene.camera_aspect_width;
    preset.aspect_height = scene.camera_aspect_height;
    preset.fov_y_deg = scene.camera_fov_y_deg;
    return preset;
}

int facingRow(
    const gameplay::world3d::CharacterSpriteDefinition& character,
    gameplay::world3d::FacingDirection facing) {
    switch (facing) {
        case gameplay::world3d::FacingDirection::South: return character.row_south;
        case gameplay::world3d::FacingDirection::West: return character.row_west;
        case gameplay::world3d::FacingDirection::East: return character.row_east;
        case gameplay::world3d::FacingDirection::North: return character.row_north;
    }
    return character.row_south;
}

bool actualWaterAt(const Scene& scene, int tile_x, int tile_y) {
    if (!scene.water_terrain.enabled || tile_y < 0 ||
        tile_y >= static_cast<int>(scene.water_terrain.actual_water_cells.size())) return false;
    const auto& row = scene.water_terrain.actual_water_cells[static_cast<std::size_t>(tile_y)];
    return tile_x >= 0 && tile_x < static_cast<int>(row.size()) &&
        row[static_cast<std::size_t>(tile_x)] != 0;
}

StablePlayer stablePlayerFor(
    const Scene& scene,
    const gameplay::world3d::CharacterSpriteDefinition& character) {
    StablePlayer out;
    const int width = std::max(1, scene.grid.width);
    const int height = std::max(1, scene.grid.height);
    const float tile_size = std::max(1.0f, scene.grid.tile_size);
    const int tile_x = std::clamp(scene.player.spawn_tile_x, 0, width - 1);
    const int tile_y = std::clamp(scene.player.spawn_tile_y, 0, height - 1);
    out.position.x = (static_cast<float>(tile_x) + 0.5f) * tile_size;
    out.position.z = (static_cast<float>(tile_y) + 0.5f) * tile_size;
    out.binding = gameplay::world3d::terrain::bindActorStanding(
        scene, tile_x, tile_y, out.position.x, out.position.z);
    out.position.y = out.binding.simulation_y;

    const bool swimming = actualWaterAt(scene, tile_x, tile_y) && character.has_swim;
    const auto& animation = swimming ? character.swim : character.idle;
    const int frame = animation.frames.empty() ? 0 : animation.frames.front();
    const int column = std::clamp(frame, 0, std::max(0, character.columns - 1));
    const int row = std::clamp(
        facingRow(character, scene.player.facing), 0, std::max(0, character.rows - 1));
    out.source_rect = SDL_Rect{
        column * character.frame_width,
        row * character.frame_height,
        character.frame_width,
        character.frame_height};
    out.activity_id = swimming ? "__locomotion_swim" : std::string{};
    out.draw_shadow = !swimming;
    out.valid = true;
    return out;
}

Vec3 tileFocus(const Scene& scene, int tile_x, int tile_y) {
    tile_x = std::clamp(tile_x, 0, std::max(1, scene.grid.width) - 1);
    tile_y = std::clamp(tile_y, 0, std::max(1, scene.grid.height) - 1);
    const float tile_size = std::max(1.0f, scene.grid.tile_size);
    Vec3 focus{
        (static_cast<float>(tile_x) + 0.5f) * tile_size,
        0.0f,
        (static_cast<float>(tile_y) + 0.5f) * tile_size};
    focus.y = gameplay::world3d::terrain::bindActorStanding(
        scene, tile_x, tile_y, focus.x, focus.z).simulation_y;
    return focus;
}

Vec3 mapFocus(const Scene& scene) {
    const float tile_size = std::max(1.0f, scene.grid.tile_size);
    const float x = static_cast<float>(std::max(1, scene.grid.width)) * tile_size * 0.5f;
    const float z = static_cast<float>(std::max(1, scene.grid.height)) * tile_size * 0.5f;
    const int tile_x = std::clamp(
        static_cast<int>(std::floor(x / tile_size)), 0, std::max(1, scene.grid.width) - 1);
    const int tile_y = std::clamp(
        static_cast<int>(std::floor(z / tile_size)), 0, std::max(1, scene.grid.height) - 1);
    Vec3 focus{x, 0.0f, z};
    focus.y = gameplay::world3d::terrain::bindActorStanding(
        scene, tile_x, tile_y, x, z).simulation_y;
    return focus;
}

} // namespace

class ExactWorldPreview::Impl {
public:
    explicit Impl(fs::path root) : project_root(normalizedAbsolute(std::move(root))) {}

    fs::path project_root;
    fs::path map_path;
    std::optional<Scene> loaded_scene;
    std::unique_ptr<Camera> camera;
    std::unique_ptr<Renderer> renderer;
    CameraPreset unscaled_preset{};
    StablePlayer player{};
    Vec3 focus{};
    SDL_Window* window = nullptr;
    void* metal_view = nullptr;
    std::string bgfx_preference;
    std::string error;
    int framebuffer_width = 1;
    int framebuffer_height = 1;
    int rebuild_count = 0;
    bool has_external_context = false;
    bool animations_enabled = false;
    double animation_time_seconds = 0.0;
    float zoom_scale = 1.0f;
    float zoom_min = 0.5f;
    float zoom_max = 3.0f;

    void rebuildCamera() {
        if (!loaded_scene) return;
        CameraPreset preset = unscaled_preset;
        gameplay::world3d::rendering::applySceneCameraScaleToCameraPreset(preset, *loaded_scene);
        zoom_scale = std::clamp(zoom_scale, zoom_min, zoom_max);
        const float authored_scale = std::clamp(
            loaded_scene->scene_camera.distance_scale, zoom_min, zoom_max);
        if (preset.distance > 0.0f && zoom_scale > 0.0f && authored_scale > 0.0f) {
            preset.distance *= authored_scale / zoom_scale;
        }
        camera = std::make_unique<Camera>(preset);
        camera->setTarget(focus);
    }

    bool initializeRenderer(Renderer& candidate) {
        if (!has_external_context) return true;
        if (candidate.initialize(
                window,
                framebuffer_width,
                framebuffer_height,
                bgfx_preference,
                metal_view)) return true;
        error = candidate.lastError();
        if (error.empty()) error = "Exact overworld renderer initialization failed";
        return false;
    }
};

ExactWorldPreview::ExactWorldPreview(fs::path project_root)
    : impl_(std::make_unique<Impl>(std::move(project_root))) {}

ExactWorldPreview::~ExactWorldPreview() = default;

bool ExactWorldPreview::initialize(
    SDL_Window* window,
    int framebuffer_width,
    int framebuffer_height,
    const std::string& bgfx_preference,
    void* sdl_metal_view) {
    if (!impl_) return false;
    if (!window) {
        impl_->error = "Exact preview requires an external SDL window";
        return false;
    }
    impl_->window = window;
    impl_->metal_view = sdl_metal_view;
    impl_->framebuffer_width = std::max(1, framebuffer_width);
    impl_->framebuffer_height = std::max(1, framebuffer_height);
    impl_->bgfx_preference = bgfx_preference;
    impl_->has_external_context = true;
    if (!impl_->renderer) {
        impl_->error.clear();
        return true;
    }
    const bool initialized = impl_->initializeRenderer(*impl_->renderer);
    if (initialized) impl_->error.clear();
    return initialized;
}

void ExactWorldPreview::shutdown() {
    if (!impl_) return;
    if (impl_->renderer) impl_->renderer->shutdown();
    impl_->has_external_context = false;
    impl_->window = nullptr;
    impl_->metal_view = nullptr;
}

bool ExactWorldPreview::loadMap(const fs::path& owmap_path) {
    if (!impl_) return false;
    if (owmap_path.empty()) {
        impl_->error = "No OWMAP path was provided";
        return false;
    }
    try {
        const fs::path resolved = resolveMapPath(impl_->project_root, owmap_path);
        Scene scene = gameplay::world3d::data::loadSceneConfig(
            impl_->project_root.string(), resolved.string());
        if (scene.player.character_path.empty()) {
            throw std::runtime_error("OWMAP has no player character package: " + resolved.string());
        }
        auto character = gameplay::world3d::data::loadCharacterDefinition(
            impl_->project_root.string(), scene.player.character_path);
        StablePlayer player = stablePlayerFor(scene, character);
        CameraPreset unscaled = unscaledScenePreset(scene);
        CameraPreset exact = unscaled;
        gameplay::world3d::rendering::applySceneCameraScaleToCameraPreset(exact, scene);
        Vec3 focus = mapFocus(scene);
        auto camera = std::make_unique<Camera>(exact);
        camera->setTarget(focus);

        auto candidate = std::make_unique<Renderer>(
            impl_->project_root.string(), scene, character);
        if (!impl_->initializeRenderer(*candidate)) return false;

        std::unique_ptr<Renderer> retired = std::move(impl_->renderer);
        impl_->renderer = std::move(candidate);
        impl_->loaded_scene = std::move(scene);
        impl_->camera = std::move(camera);
        impl_->unscaled_preset = unscaled;
        impl_->player = std::move(player);
        impl_->focus = focus;
        impl_->map_path = resolved;
        impl_->zoom_min = std::max(0.01f, impl_->loaded_scene->scene_camera.distance_scale_min);
        impl_->zoom_max = std::max(
            impl_->zoom_min, impl_->loaded_scene->scene_camera.distance_scale_max);
        impl_->zoom_scale = std::clamp(
            impl_->loaded_scene->scene_camera.distance_scale, impl_->zoom_min, impl_->zoom_max);
        impl_->animation_time_seconds = 0.0;
        ++impl_->rebuild_count;
        impl_->error.clear();
        retired.reset();
        return true;
    } catch (const std::exception& exception) {
        impl_->error = exception.what();
        return false;
    }
}

bool ExactWorldPreview::reloadMap() {
    if (!impl_ || impl_->map_path.empty()) {
        if (impl_) impl_->error = "No OWMAP is loaded";
        return false;
    }
    return loadMap(impl_->map_path);
}

ExactWorldPreview::ViewportTexture ExactWorldPreview::render(
    int framebuffer_width,
    int framebuffer_height,
    double delta_seconds) {
    if (!ready()) return {};
    impl_->framebuffer_width = std::max(1, framebuffer_width);
    impl_->framebuffer_height = std::max(1, framebuffer_height);
    if (impl_->animations_enabled && std::isfinite(delta_seconds) && delta_seconds > 0.0) {
        impl_->animation_time_seconds += delta_seconds;
    }
    const int logical_width = gameplay::world3d::rendering::worldViewportBaseWidth(*impl_->loaded_scene);
    const int logical_height = gameplay::world3d::rendering::worldViewportBaseHeight(*impl_->loaded_scene);
    const Renderer::EmbeddedViewportOptions options{
        impl_->animations_enabled, impl_->animation_time_seconds};
    ViewportTexture texture = impl_->renderer->renderEmbeddedViewport(
        *impl_->camera,
        impl_->player.position,
        impl_->player.source_rect,
        impl_->player.activity_id,
        false,
        impl_->player.draw_shadow,
        impl_->player.binding,
        logical_width,
        logical_height,
        impl_->framebuffer_width,
        impl_->framebuffer_height,
        {},
        {},
        options);
    if (!texture.valid() && !impl_->renderer->lastError().empty()) {
        impl_->error = impl_->renderer->lastError();
    }
    return texture;
}

void ExactWorldPreview::setAnimationsEnabled(bool enabled) {
    if (impl_) impl_->animations_enabled = enabled;
}

bool ExactWorldPreview::animationsEnabled() const {
    return impl_ && impl_->animations_enabled;
}

void ExactWorldPreview::setAnimationTimeSeconds(double seconds) {
    if (impl_ && std::isfinite(seconds)) impl_->animation_time_seconds = std::max(0.0, seconds);
}

double ExactWorldPreview::animationTimeSeconds() const {
    return impl_ ? impl_->animation_time_seconds : 0.0;
}

void ExactWorldPreview::focusMap() {
    if (!impl_ || !impl_->loaded_scene) return;
    focusWorld(mapFocus(*impl_->loaded_scene));
}

void ExactWorldPreview::focusPlayer() {
    if (impl_ && impl_->player.valid) focusWorld(impl_->player.position);
}

void ExactWorldPreview::focusTile(int tile_x, int tile_y) {
    if (!impl_ || !impl_->loaded_scene) return;
    focusWorld(tileFocus(*impl_->loaded_scene, tile_x, tile_y));
}

void ExactWorldPreview::focusWorld(Vec3 world) {
    if (!impl_) return;
    impl_->focus = world;
    if (impl_->camera) impl_->camera->setTarget(world);
}

void ExactWorldPreview::panScreenPixels(float delta_x, float delta_y, int viewport_height) {
    if (!impl_ || !impl_->camera) return;
    const auto pose = impl_->camera->pose();
    const float ground_length = std::hypot(pose.forward.x, pose.forward.z);
    if (ground_length <= 0.0001f) return;
    const float world_per_pixel =
        2.0f * pose.preset.distance *
        std::tan(std::max(0.001f, pose.preset.fov_y_deg * kPi / 360.0f)) /
        static_cast<float>(std::max(1, viewport_height));
    const float forward_x = pose.forward.x / ground_length;
    const float forward_z = pose.forward.z / ground_length;
    panWorld(
        ((-pose.right.x * delta_x) + (forward_x * delta_y)) * world_per_pixel,
        ((-pose.right.z * delta_x) + (forward_z * delta_y)) * world_per_pixel);
}

void ExactWorldPreview::panWorld(float delta_world_x, float delta_world_z) {
    if (!impl_) return;
    focusWorld(Vec3{
        impl_->focus.x + delta_world_x,
        impl_->focus.y,
        impl_->focus.z + delta_world_z});
}

void ExactWorldPreview::setZoomScale(float scale) {
    if (!impl_ || !std::isfinite(scale)) return;
    impl_->zoom_scale = std::clamp(scale, impl_->zoom_min, impl_->zoom_max);
    impl_->rebuildCamera();
}

void ExactWorldPreview::zoomByWheel(float wheel_delta) {
    if (!impl_ || !std::isfinite(wheel_delta) || wheel_delta == 0.0f) return;
    setZoomScale(impl_->zoom_scale * std::exp(wheel_delta * 0.18f));
}

bool ExactWorldPreview::ready() const {
    return impl_ && impl_->loaded_scene && impl_->camera && impl_->player.valid &&
        impl_->renderer && impl_->renderer->valid();
}

const Scene* ExactWorldPreview::scene() const {
    return impl_ && impl_->loaded_scene ? &*impl_->loaded_scene : nullptr;
}

const Camera* ExactWorldPreview::camera() const {
    return impl_ ? impl_->camera.get() : nullptr;
}

const fs::path& ExactWorldPreview::projectRoot() const { return impl_->project_root; }
const fs::path& ExactWorldPreview::mapPath() const { return impl_->map_path; }
const std::string& ExactWorldPreview::lastError() const { return impl_->error; }
int ExactWorldPreview::sceneRebuildCount() const { return impl_ ? impl_->rebuild_count : 0; }
float ExactWorldPreview::zoomScale() const { return impl_ ? impl_->zoom_scale : 1.0f; }
float ExactWorldPreview::zoomMinimum() const { return impl_ ? impl_->zoom_min : 0.5f; }
float ExactWorldPreview::zoomMaximum() const { return impl_ ? impl_->zoom_max : 3.0f; }
Vec3 ExactWorldPreview::cameraFocus() const { return impl_ ? impl_->focus : Vec3{}; }
Vec3 ExactWorldPreview::playerPosition() const { return impl_ ? impl_->player.position : Vec3{}; }

const Binding* ExactWorldPreview::playerBinding() const {
    return impl_ && impl_->player.valid ? &impl_->player.binding : nullptr;
}

} // namespace pr::mapmaker
