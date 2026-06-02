#include "gameplay/world3d/effects/LandingDustSystem.hpp"

#include <SDL_image.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace pr::gameplay::world3d::effects {

namespace fs = std::filesystem;

LandingDustSystem::LandingDustSystem(
    const std::string& project_root,
    const followers::NatureIdleLandingDustConfig& config)
    : project_root_(project_root), config_(config) {
    instances_.reserve(32);
}

void LandingDustSystem::initialize(SDL_Renderer* renderer) {
    if (!renderer || !config_.enabled || texture_) return;

    const fs::path texture_path = fs::path(project_root_) / config_.texture_path;
    SDL_Surface* surface = IMG_Load(texture_path.string().c_str());
    if (!surface) return;

    SDL_Texture* raw = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    if (!raw) return;

    texture_.reset(raw, SDL_DestroyTexture);
    SDL_SetTextureBlendMode(texture_.get(), SDL_BLENDMODE_BLEND);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
}

void LandingDustSystem::update(double dt) {
    for (DustInstance& instance : instances_) {
        if (!instance.active) continue;
        instance.elapsed_seconds += dt;
        if (instance.elapsed_seconds >= instance.duration_seconds) {
            instance = DustInstance{};
        }
    }
}

void LandingDustSystem::spawn(const LandingDustSpawnRequest& request) {
    if (!config_.enabled || request.owner_id == 0 || request.duration_seconds <= 0.0) return;

    std::optional<std::size_t> slot = findActiveOwner(request.owner_id);
    if (!slot) slot = findFreeSlot();
    if (!slot) {
        instances_.push_back(DustInstance{});
        slot = instances_.size() - 1U;
    }

    DustInstance& instance = instances_[*slot];
    instance.active = true;
    instance.owner_id = request.owner_id;
    instance.world_pos = request.world_pos;
    instance.elapsed_seconds = 0.0;
    instance.duration_seconds = std::max(0.01, request.duration_seconds);
}

void LandingDustSystem::render(
    SDL_Renderer* renderer,
    const camera::Gen4FollowCamera& camera,
    int viewport_w,
    int viewport_h,
    float tint_r,
    float tint_g,
    float tint_b,
    float brightness) const {
    if (!renderer || !texture_ || !config_.enabled) return;

    const int frame_width = std::max(1, config_.frame_width);
    const int frame_height = std::max(1, config_.frame_height);
    const int frame_count = std::max(1, config_.frame_count);

    for (const DustInstance& instance : instances_) {
        if (!instance.active) continue;

        float sx = 0.0f;
        float sy = 0.0f;
        float depth = 0.0f;
        if (!camera.worldToScreen(instance.world_pos, viewport_w, viewport_h, sx, sy, depth)) continue;

        const float progress = static_cast<float>(
            std::clamp(instance.elapsed_seconds / std::max(0.01, instance.duration_seconds), 0.0, 0.999999));
        const int frame = std::clamp(static_cast<int>(std::floor(progress * static_cast<float>(frame_count))), 0, frame_count - 1);
        const SDL_Rect src{frame * frame_width, 0, frame_width, frame_height};

        const float scale = camera.perspectiveScale(depth) * config_.sprite_scale;
        const int w = std::max(2, static_cast<int>(std::round(static_cast<float>(frame_width) * scale * 0.60f)));
        const int h = std::max(2, static_cast<int>(std::round(static_cast<float>(frame_height) * scale * 0.60f)));
        const SDL_Rect dst{
            static_cast<int>(std::round(sx)) - (w / 2),
            static_cast<int>(std::round(sy)) - h + config_.screen_offset_y_px,
            w,
            h};

        const float br = std::max(0.0f, brightness);
        SDL_SetTextureColorMod(
            texture_.get(),
            static_cast<Uint8>(std::clamp(tint_r * br, 0.0f, 1.0f) * 255.0f),
            static_cast<Uint8>(std::clamp(tint_g * br, 0.0f, 1.0f) * 255.0f),
            static_cast<Uint8>(std::clamp(tint_b * br, 0.0f, 1.0f) * 255.0f));
        SDL_SetTextureAlphaMod(texture_.get(), 255);
        SDL_RenderCopy(renderer, texture_.get(), &src, &dst);
    }
}

std::optional<std::size_t> LandingDustSystem::findActiveOwner(std::uintptr_t owner_id) const {
    for (std::size_t i = 0; i < instances_.size(); ++i) {
        if (instances_[i].active && instances_[i].owner_id == owner_id) {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> LandingDustSystem::findFreeSlot() const {
    for (std::size_t i = 0; i < instances_.size(); ++i) {
        if (!instances_[i].active) return i;
    }
    return std::nullopt;
}

} // namespace pr::gameplay::world3d::effects
