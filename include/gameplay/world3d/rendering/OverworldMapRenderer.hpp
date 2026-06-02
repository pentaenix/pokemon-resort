#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"
#include "gameplay/world3d/camera/Gen4FollowCamera.hpp"

#include <SDL.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace pr::gameplay::world3d::rendering {

class OverworldMapRenderer {
public:
    explicit OverworldMapRenderer(const SceneConfig& scene);

    bool load();
    void render(SDL_Renderer* renderer, const camera::Gen4FollowCamera& camera, int viewport_w, int viewport_h) const;

private:
    struct Tri {
        camera::Vec3 a{};
        camera::Vec3 b{};
        camera::Vec3 c{};
        SDL_FPoint ta{};
        SDL_FPoint tb{};
        SDL_FPoint tc{};
        std::string material;
    };

    SceneConfig scene_;
    std::vector<Tri> triangles_;
    std::unordered_map<std::string, std::string> material_texture_paths_;
    mutable std::unordered_map<std::string, std::shared_ptr<SDL_Texture>> material_textures_;

    bool ensureTexturesLoaded(SDL_Renderer* renderer) const;
};

} // namespace pr::gameplay::world3d::rendering
