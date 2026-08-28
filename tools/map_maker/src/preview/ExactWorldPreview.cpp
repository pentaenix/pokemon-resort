#include "mapmaker/preview/ExactWorldPreview.hpp"
#include "ExactWorldPreviewSupport.hpp"

#include "gameplay/world3d/camera/Gen4CameraPreset.hpp"
#include "gameplay/world3d/aquarium/AquariumConfig.hpp"
#include "gameplay/world3d/aquarium/AquariumSimulation.hpp"
#include "gameplay/world3d/characters/CharacterController.hpp"
#include "gameplay/world3d/data/JsonOverworldLoader.hpp"
#include "gameplay/world3d/rendering/PixelScale.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <optional>
#include <stdexcept>
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
using CharacterController = gameplay::world3d::characters::CharacterController;
using preview_detail::StablePlayer;
using preview_detail::mapFocus;
using preview_detail::normalizedAbsolute;
using preview_detail::resolveMapPath;
using preview_detail::stablePlayerFor;
using preview_detail::tileFocus;
using preview_detail::unscaledScenePreset;
using preview_detail::updatePlayerFrame;

constexpr float kPi = 3.14159265358979323846f;

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
    gameplay::world3d::CharacterSpriteDefinition character{};
    std::unique_ptr<CharacterController> player_controller;
    gameplay::world3d::aquarium::AquariumCatalog aquarium_catalog{};
    std::unique_ptr<gameplay::world3d::aquarium::AquariumSimulation> aquarium_simulation;
    fs::file_time_type aquarium_config_write_time{};
    bool aquarium_config_write_time_known = false;
    double aquarium_config_poll_seconds = 0.0;
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
    double player_animation_seconds = 0.0;
    int movement_x = 0;
    int movement_y = 0;
    bool follow_player = true;
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

    void rememberAquariumConfigWriteTime() {
        std::error_code ec;
        const fs::path path = project_root / "config/gameplay/world3d/aquariums.json";
        const fs::file_time_type value = fs::last_write_time(path, ec);
        if (!ec) {
            aquarium_config_write_time = value;
            aquarium_config_write_time_known = true;
        }
    }

    void pollAquariumConfig(double delta_seconds) {
        aquarium_config_poll_seconds += std::max(0.0, delta_seconds);
        if (aquarium_config_poll_seconds < 0.25 || !loaded_scene || !renderer) return;
        aquarium_config_poll_seconds = 0.0;
        std::error_code ec;
        const fs::path path = project_root / "config/gameplay/world3d/aquariums.json";
        const fs::file_time_type value = fs::last_write_time(path, ec);
        if (ec || (aquarium_config_write_time_known && value == aquarium_config_write_time)) return;
        aquarium_config_write_time = value;
        aquarium_config_write_time_known = true;
        std::string config_error;
        auto catalog = gameplay::world3d::aquarium::loadAquariumCatalog(
            project_root.string(), &config_error);
        if (!config_error.empty()) {
            error = "Aquarium config reload rejected: " + config_error;
            std::cerr << "[Aquarium] " << error << '\n';
            return;
        }
        auto simulation =
            std::make_unique<gameplay::world3d::aquarium::AquariumSimulation>(
                project_root, *loaded_scene,
                gameplay::world3d::aquarium::aquariumMapConfig(catalog, loaded_scene->id));
        for (const std::string& warning : simulation->warnings()) {
            std::cerr << "[Aquarium] " << warning << '\n';
        }
        renderer->setAquariumPokemonActors(simulation->actors());
        aquarium_catalog = std::move(catalog);
        aquarium_simulation = std::move(simulation);
        error.clear();
        std::cerr << "[Aquarium] Map Studio applied config for " << loaded_scene->id
                  << " with " << aquarium_simulation->actors().size() << " actors\n";
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
        auto player_controller = std::make_unique<CharacterController>(scene);
        CameraPreset unscaled = unscaledScenePreset(scene);
        CameraPreset exact = unscaled;
        gameplay::world3d::rendering::applySceneCameraScaleToCameraPreset(exact, scene);
        Vec3 focus = mapFocus(scene);
        auto camera = std::make_unique<Camera>(exact);
        camera->setTarget(focus);

        auto candidate = std::make_unique<Renderer>(
            impl_->project_root.string(), scene, character);
        std::string aquarium_error;
        auto aquarium_catalog = gameplay::world3d::aquarium::loadAquariumCatalog(
            impl_->project_root.string(), &aquarium_error);
        if (!aquarium_error.empty()) throw std::runtime_error(aquarium_error);
        auto aquarium_simulation =
            std::make_unique<gameplay::world3d::aquarium::AquariumSimulation>(
                impl_->project_root,
                scene,
                gameplay::world3d::aquarium::aquariumMapConfig(aquarium_catalog, scene.id));
        candidate->setAquariumPokemonActors(aquarium_simulation->actors());
        if (!impl_->initializeRenderer(*candidate)) return false;

        std::unique_ptr<Renderer> retired = std::move(impl_->renderer);
        impl_->renderer = std::move(candidate);
        impl_->loaded_scene = std::move(scene);
        impl_->camera = std::move(camera);
        impl_->unscaled_preset = unscaled;
        impl_->player = std::move(player);
        impl_->character = std::move(character);
        impl_->player_controller = std::move(player_controller);
        impl_->aquarium_catalog = std::move(aquarium_catalog);
        impl_->aquarium_simulation = std::move(aquarium_simulation);
        impl_->rememberAquariumConfigWriteTime();
        impl_->focus = focus;
        impl_->map_path = resolved;
        impl_->zoom_min = std::max(0.01f, impl_->loaded_scene->scene_camera.distance_scale_min);
        impl_->zoom_max = std::max(
            impl_->zoom_min, impl_->loaded_scene->scene_camera.distance_scale_max);
        impl_->zoom_scale = std::clamp(
            impl_->loaded_scene->scene_camera.distance_scale, impl_->zoom_min, impl_->zoom_max);
        impl_->animation_time_seconds = 0.0;
        impl_->player_animation_seconds = 0.0;
        impl_->movement_x = 0;
        impl_->movement_y = 0;
        impl_->follow_player = true;
        impl_->focus = impl_->player.position;
        impl_->camera->setTarget(impl_->focus);
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
    impl_->pollAquariumConfig(delta_seconds);
    if (impl_->animations_enabled && std::isfinite(delta_seconds) && delta_seconds > 0.0) {
        impl_->animation_time_seconds += delta_seconds;
        if (impl_->aquarium_simulation) impl_->aquarium_simulation->update(delta_seconds);
    }
    if (impl_->player_controller && std::isfinite(delta_seconds) && delta_seconds > 0.0) {
        impl_->player_controller->moveInput(
            impl_->movement_x, impl_->movement_y, std::min(delta_seconds, 0.1));
        impl_->player.position = impl_->player_controller->position();
        impl_->player.binding = impl_->player_controller->terrainBinding();
        const bool moving = impl_->player_controller->moving();
        if (moving) impl_->player_animation_seconds += delta_seconds;
        else impl_->player_animation_seconds = 0.0;
        updatePlayerFrame(impl_->player, impl_->character,
            impl_->player_controller->facing(), moving,
            impl_->player_controller->onActualWater(), impl_->player_animation_seconds);
        if (impl_->follow_player) {
            impl_->focus = impl_->player.position;
            impl_->camera->setTarget(impl_->focus);
        }
    }
    const int logical_width = gameplay::world3d::rendering::worldViewportBaseWidth(*impl_->loaded_scene);
    const int logical_height = gameplay::world3d::rendering::worldViewportBaseHeight(*impl_->loaded_scene);
    const Renderer::EmbeddedViewportOptions options{
        impl_->animations_enabled, impl_->animation_time_seconds};
    if (impl_->aquarium_simulation) {
        impl_->renderer->setAquariumPokemonActors(impl_->aquarium_simulation->actors());
    }
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

void ExactWorldPreview::setMovementInput(int dx, int dy) {
    if (!impl_) return;
    impl_->movement_x = std::clamp(dx, -1, 1);
    impl_->movement_y = std::clamp(dy, -1, 1);
}

void ExactWorldPreview::resetPlayer() {
    if (!impl_ || !impl_->loaded_scene) return;
    impl_->player_controller = std::make_unique<CharacterController>(*impl_->loaded_scene);
    impl_->player = stablePlayerFor(*impl_->loaded_scene, impl_->character);
    impl_->player_animation_seconds = 0.0;
    impl_->movement_x = 0;
    impl_->movement_y = 0;
    impl_->follow_player = true;
    focusPlayer();
}

bool ExactWorldPreview::startPlayerAtTile(int tile_x, int tile_y) {
    if (!impl_ || !impl_->player_controller) return false;
    if (!impl_->player_controller->teleportToTile(
        tile_x, tile_y, gameplay::world3d::FacingDirection::South)) return false;
    impl_->player.position = impl_->player_controller->position();
    impl_->player.binding = impl_->player_controller->terrainBinding();
    updatePlayerFrame(impl_->player, impl_->character,
        impl_->player_controller->facing(), false,
        impl_->player_controller->onActualWater(), 0.0);
    impl_->follow_player = true;
    focusPlayer();
    return true;
}

void ExactWorldPreview::focusMap() {
    if (!impl_ || !impl_->loaded_scene) return;
    impl_->follow_player = false;
    focusWorld(mapFocus(*impl_->loaded_scene));
}

void ExactWorldPreview::focusPlayer() {
    if (impl_ && impl_->player.valid) {
        impl_->follow_player = true;
        focusWorld(impl_->player.position);
    }
}

void ExactWorldPreview::focusTile(int tile_x, int tile_y) {
    if (!impl_ || !impl_->loaded_scene) return;
    impl_->follow_player = false;
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
    impl_->follow_player = false;
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
