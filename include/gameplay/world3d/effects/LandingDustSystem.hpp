#pragma once

#include "gameplay/world3d/camera/Gen4FollowCamera.hpp"
#include "gameplay/world3d/followers/NatureIdleConfig.hpp"

#include <SDL.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pr::gameplay::world3d::effects {

struct LandingDustSpawnRequest {
    std::uintptr_t owner_id = 0;
    camera::Vec3 world_pos{};
    double duration_seconds = 0.0;
};

class LandingDustSystem {
public:
    LandingDustSystem(
        const std::string& project_root,
        const followers::NatureIdleLandingDustConfig& config);

    void initialize(SDL_Renderer* renderer);
    bool valid() const { return texture_ != nullptr && config_.enabled; }
    void update(double dt);
    void spawn(const LandingDustSpawnRequest& request);
    void render(
        SDL_Renderer* renderer,
        const camera::Gen4FollowCamera& camera,
        int viewport_w,
        int viewport_h,
        float tint_r,
        float tint_g,
        float tint_b,
        float brightness) const;

private:
    struct DustInstance {
        bool active = false;
        std::uintptr_t owner_id = 0;
        camera::Vec3 world_pos{};
        double elapsed_seconds = 0.0;
        double duration_seconds = 0.0;
    };

    std::string project_root_;
    followers::NatureIdleLandingDustConfig config_{};
    std::shared_ptr<SDL_Texture> texture_;
    std::vector<DustInstance> instances_;

    std::optional<std::size_t> findActiveOwner(std::uintptr_t owner_id) const;
    std::optional<std::size_t> findFreeSlot() const;
};

} // namespace pr::gameplay::world3d::effects
