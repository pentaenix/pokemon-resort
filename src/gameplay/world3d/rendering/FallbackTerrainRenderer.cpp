#include "gameplay/world3d/rendering/FallbackTerrainRenderer.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace pr::gameplay::world3d::rendering {

void renderFallbackTerrain(
    SDL_Renderer* renderer,
    const camera::Gen4FollowCamera& camera,
    const SceneConfig& scene,
    float tile_size,
    int viewport_w,
    int viewport_h) {
#if SDL_VERSION_ATLEAST(2,0,18)
    const int grid_w = std::max(1, scene.grid.width);
    const int grid_h = std::max(1, scene.grid.height);
    const auto tile_h = [&scene](int tx, int ty) -> int {
        if (scene.terrain.heights.empty()) return 0;
        if (ty < 0 || ty >= static_cast<int>(scene.terrain.heights.size())) return 0;
        const auto& row = scene.terrain.heights[static_cast<std::size_t>(ty)];
        if (tx < 0 || tx >= static_cast<int>(row.size())) return 0;
        return row[static_cast<std::size_t>(tx)];
    };
    const auto tile_special = [&scene](int tx, int ty) -> int {
        if (scene.terrain.specials.empty()) return 0;
        if (ty < 0 || ty >= static_cast<int>(scene.terrain.specials.size())) return 0;
        const auto& row = scene.terrain.specials[static_cast<std::size_t>(ty)];
        if (tx < 0 || tx >= static_cast<int>(row.size())) return 0;
        return static_cast<int>(row[static_cast<std::size_t>(tx)]);
    };
    const auto in_bounds = [grid_w, grid_h](int tx, int ty) -> bool {
        return tx >= 0 && tx < grid_w && ty >= 0 && ty < grid_h;
    };
    const auto ramp_direction = [&](int tx, int ty) -> int {
        const int special = tile_special(tx, ty);
        if (special >= 2 && special <= 5) {
            return special;
        }
        if (special != 1 && special != 6 && special != 7) {
            return 0;
        }
        const int h = tile_h(tx, ty);
        const int n = tile_h(tx, ty - 1);
        const int e = tile_h(tx + 1, ty);
        const int s = tile_h(tx, ty + 1);
        const int w = tile_h(tx - 1, ty);
        int best_dir = 0;
        int best_delta = 0;
        if ((n - h) > best_delta) { best_delta = n - h; best_dir = 2; }
        if ((e - h) > best_delta) { best_delta = e - h; best_dir = 3; }
        if ((s - h) > best_delta) { best_delta = s - h; best_dir = 4; }
        if ((w - h) > best_delta) { best_delta = w - h; best_dir = 5; }
        return best_dir;
    };
    const auto corner_heights = [&](int tx, int ty, float (&out)[4]) {
        const int h = tile_h(tx, ty);
        const float flat = static_cast<float>(std::max(0, h)) * tile_size;
        out[0] = flat;
        out[1] = flat;
        out[2] = flat;
        out[3] = flat;
        const int dir = ramp_direction(tx, ty);
        if (dir != 0) {
            const float low = static_cast<float>(h) * tile_size;
            const float high = static_cast<float>(h + 1) * tile_size;
            if (dir == 2) {
                out[0] = high; out[1] = high; out[2] = low; out[3] = low;
            } else if (dir == 3) {
                out[0] = low; out[1] = high; out[2] = high; out[3] = low;
            } else if (dir == 4) {
                out[0] = low; out[1] = low; out[2] = high; out[3] = high;
            } else if (dir == 5) {
                out[0] = high; out[1] = low; out[2] = low; out[3] = high;
            }
        }
    };
    const auto curved_corner_heights = [&](int tx, int ty, float (&out)[4]) {
        const int h = tile_h(tx, ty);
        const float base = static_cast<float>(h) * tile_size;
        const auto sample = [&](int sx, int sy) -> float {
            return static_cast<float>(tile_h(sx, sy)) * tile_size;
        };
        const float n = sample(tx, ty - 1);
        const float e = sample(tx + 1, ty);
        const float s = sample(tx, ty + 1);
        const float w = sample(tx - 1, ty);
        out[0] = (base + n + w) / 3.0f;
        out[1] = (base + n + e) / 3.0f;
        out[2] = (base + s + e) / 3.0f;
        out[3] = (base + s + w) / 3.0f;
    };
    const auto corner_curve_mode_for_auto_ramp = [&](int tx, int ty) -> int {
        if (tile_special(tx, ty) != 1) return 0;
        const auto is_ramp_like = [&](int sx, int sy) -> bool {
            const int sp = tile_special(sx, sy);
            return sp >= 1 && sp <= 7;
        };
        const int h = tile_h(tx, ty);
        const int dn = tile_h(tx, ty - 1) - h;
        const int de = tile_h(tx + 1, ty) - h;
        const int ds = tile_h(tx, ty + 1) - h;
        const int dw = tile_h(tx - 1, ty) - h;
        const bool n_ramp = is_ramp_like(tx, ty - 1);
        const bool e_ramp = is_ramp_like(tx + 1, ty);
        const bool s_ramp = is_ramp_like(tx, ty + 1);
        const bool w_ramp = is_ramp_like(tx - 1, ty);
        const bool orthogonal_ramp_bend = (n_ramp && e_ramp) || (e_ramp && s_ramp) || (s_ramp && w_ramp) || (w_ramp && n_ramp);
        const bool n_up = dn > 0;
        const bool e_up = de > 0;
        const bool s_up = ds > 0;
        const bool w_up = dw > 0;
        const bool n_down = dn < 0;
        const bool e_down = de < 0;
        const bool s_down = ds < 0;
        const bool w_down = dw < 0;
        const bool adjacent_up = (n_up && e_up) || (e_up && s_up) || (s_up && w_up) || (w_up && n_up);
        const bool adjacent_down = (n_down && e_down) || (e_down && s_down) || (s_down && w_down) || (w_down && n_down);
        if (adjacent_up) return 6;
        if (adjacent_down) return 7;
        if (orthogonal_ramp_bend) return 6;
        return 0;
    };
    struct RenderFace {
        SDL_Vertex verts[4];
        float depth_key = std::numeric_limits<float>::lowest();
        bool wire = false;
    };
    static thread_local std::vector<RenderFace> render_faces;
    render_faces.clear();
    const std::size_t reserve_faces = static_cast<std::size_t>(grid_w * grid_h * 3);
    if (render_faces.capacity() < reserve_faces) {
        render_faces.reserve(reserve_faces);
    }

    const auto push_quad_face = [&camera, viewport_w, viewport_h](
                                    float x0w, float y0w, float z0w,
                                    float x1w, float y1w, float z1w,
                                    float x2w, float y2w, float z2w,
                                    float x3w, float y3w, float z3w,
                                    SDL_Color color,
                                    bool wire) {
        float x0s, y0s, d0, x1s, y1s, d1, x2s, y2s, d2, x3s, y3s, d3;
        if (!camera.worldToScreen({x0w, y0w, z0w}, viewport_w, viewport_h, x0s, y0s, d0) ||
            !camera.worldToScreen({x1w, y1w, z1w}, viewport_w, viewport_h, x1s, y1s, d1) ||
            !camera.worldToScreen({x2w, y2w, z2w}, viewport_w, viewport_h, x2s, y2s, d2) ||
            !camera.worldToScreen({x3w, y3w, z3w}, viewport_w, viewport_h, x3s, y3s, d3)) {
            return;
        }
        RenderFace face{};
        face.verts[0] = SDL_Vertex{{x0s, y0s}, color, {0.0f, 0.0f}};
        face.verts[1] = SDL_Vertex{{x1s, y1s}, color, {0.0f, 0.0f}};
        face.verts[2] = SDL_Vertex{{x2s, y2s}, color, {0.0f, 0.0f}};
        face.verts[3] = SDL_Vertex{{x3s, y3s}, color, {0.0f, 0.0f}};
        face.depth_key = (d0 + d1 + d2 + d3) * 0.25f;
        face.wire = wire;
        render_faces.push_back(face);
    };
    const auto emit_tile_top = [&](int x, int z, SDL_Color color, bool wire) {
        const float x0w = static_cast<float>(x) * tile_size;
        const float z0w = static_cast<float>(z) * tile_size;
        const float x1w = x0w + tile_size;
        const float z1w = z0w + tile_size;
        const int special = tile_special(x, z);
        const int auto_corner_mode = corner_curve_mode_for_auto_ramp(x, z);
        const int curve_mode = (special == 6 || special == 7) ? special : auto_corner_mode;
        float corners[4]{};
        if (curve_mode == 6 || curve_mode == 7) {
            curved_corner_heights(x, z, corners);
            const float center = (corners[0] + corners[1] + corners[2] + corners[3]) * 0.25f;
            const float neighbor_delta =
                std::max(std::max(std::fabs(corners[0] - center), std::fabs(corners[1] - center)),
                         std::max(std::fabs(corners[2] - center), std::fabs(corners[3] - center)));
            const float adaptive_amplitude = std::min(tile_size * 0.35f, neighbor_delta * 0.75f);
            const float minimum_corner_amplitude = tile_size * 0.18f;
            const float amplitude = std::max(minimum_corner_amplitude, adaptive_amplitude);
            const float sign = (curve_mode == 6) ? 1.0f : -1.0f;
            constexpr int kSubdiv = 4;
            for (int iz = 0; iz < kSubdiv; ++iz) {
                const float v0 = static_cast<float>(iz) / static_cast<float>(kSubdiv);
                const float v1 = static_cast<float>(iz + 1) / static_cast<float>(kSubdiv);
                for (int ix = 0; ix < kSubdiv; ++ix) {
                    const float u0 = static_cast<float>(ix) / static_cast<float>(kSubdiv);
                    const float u1 = static_cast<float>(ix + 1) / static_cast<float>(kSubdiv);
                    const auto bilerp = [&](float u, float v) -> float {
                        const float a = corners[0] + ((corners[1] - corners[0]) * u);
                        const float b = corners[3] + ((corners[2] - corners[3]) * u);
                        const float lin = a + ((b - a) * v);
                        const float bu = 1.0f - ((2.0f * u - 1.0f) * (2.0f * u - 1.0f));
                        const float bv = 1.0f - ((2.0f * v - 1.0f) * (2.0f * v - 1.0f));
                        return lin + (sign * amplitude * bu * bv);
                    };
                    const float xa0 = x0w + (tile_size * u0);
                    const float xa1 = x0w + (tile_size * u1);
                    const float za0 = z0w + (tile_size * v0);
                    const float za1 = z0w + (tile_size * v1);
                    push_quad_face(
                        xa0, bilerp(u0, v0), za0,
                        xa1, bilerp(u1, v0), za0,
                        xa1, bilerp(u1, v1), za1,
                        xa0, bilerp(u0, v1), za1,
                        color,
                        wire);
                }
            }
        } else {
            corner_heights(x, z, corners);
            push_quad_face(
                x0w, corners[0], z0w,
                x1w, corners[1], z0w,
                x1w, corners[2], z1w,
                x0w, corners[3], z1w,
                color,
                wire);
        }
    };

    for (int z = 0; z < grid_h; ++z) {
        for (int x = 0; x < grid_w; ++x) {
            const bool checker = ((x + z) & 1) == 0;
            const SDL_Color color = checker ? SDL_Color{116, 156, 190, 255} : SDL_Color{125, 166, 200, 255};
            emit_tile_top(x, z, color, false);
        }
    }
    const SDL_Color wall_color_ns{88, 117, 145, 255};
    const SDL_Color wall_color_ew{80, 108, 136, 255};
    for (int z = 0; z < grid_h; ++z) {
        for (int x = 0; x < grid_w; ++x) {
            float c[4]{};
            corner_heights(x, z, c);
            const float x0w = static_cast<float>(x) * tile_size;
            const float z0w = static_cast<float>(z) * tile_size;
            const float x1w = x0w + tile_size;
            const float z1w = z0w + tile_size;
            float n[4]{};

            if (in_bounds(x + 1, z)) {
                corner_heights(x + 1, z, n);
                if (c[1] > n[0] || c[2] > n[3]) {
                    push_quad_face(x1w, n[0], z0w, x1w, c[1], z0w, x1w, c[2], z1w, x1w, n[3], z1w, wall_color_ew, false);
                } else if (n[0] > c[1] || n[3] > c[2]) {
                    push_quad_face(x1w, c[1], z0w, x1w, n[0], z0w, x1w, n[3], z1w, x1w, c[2], z1w, wall_color_ew, false);
                }
            }

            if (in_bounds(x, z + 1)) {
                corner_heights(x, z + 1, n);
                if (c[3] > n[0] || c[2] > n[1]) {
                    push_quad_face(x0w, n[0], z1w, x1w, n[1], z1w, x1w, c[2], z1w, x0w, c[3], z1w, wall_color_ns, false);
                } else if (n[0] > c[3] || n[1] > c[2]) {
                    push_quad_face(x0w, c[3], z1w, x1w, c[2], z1w, x1w, n[1], z1w, x0w, n[0], z1w, wall_color_ns, false);
                }
            }
        }
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    const SDL_Color wire_color{102, 138, 170, 120};
    for (int z = 0; z < grid_h; ++z) {
        for (int x = 0; x < grid_w; ++x) {
            emit_tile_top(x, z, wire_color, true);
        }
    }

    std::sort(render_faces.begin(), render_faces.end(), [](const RenderFace& a, const RenderFace& b) {
        return a.depth_key > b.depth_key;
    });
    constexpr int tri_indices[6] = {0, 1, 2, 0, 2, 3};
    for (const RenderFace& face : render_faces) {
        if (!face.wire) {
            SDL_RenderGeometry(renderer, nullptr, face.verts, 4, tri_indices, 6);
            continue;
        }
        SDL_SetRenderDrawColor(
            renderer,
            face.verts[0].color.r,
            face.verts[0].color.g,
            face.verts[0].color.b,
            face.verts[0].color.a);
        SDL_RenderDrawLine(renderer, static_cast<int>(face.verts[0].position.x), static_cast<int>(face.verts[0].position.y),
                           static_cast<int>(face.verts[1].position.x), static_cast<int>(face.verts[1].position.y));
        SDL_RenderDrawLine(renderer, static_cast<int>(face.verts[1].position.x), static_cast<int>(face.verts[1].position.y),
                           static_cast<int>(face.verts[2].position.x), static_cast<int>(face.verts[2].position.y));
        SDL_RenderDrawLine(renderer, static_cast<int>(face.verts[2].position.x), static_cast<int>(face.verts[2].position.y),
                           static_cast<int>(face.verts[3].position.x), static_cast<int>(face.verts[3].position.y));
        SDL_RenderDrawLine(renderer, static_cast<int>(face.verts[3].position.x), static_cast<int>(face.verts[3].position.y),
                           static_cast<int>(face.verts[0].position.x), static_cast<int>(face.verts[0].position.y));
    }
#else
    (void)renderer;
    (void)camera;
    (void)scene;
    (void)tile_size;
    (void)viewport_w;
    (void)viewport_h;
#endif
}

} // namespace pr::gameplay::world3d::rendering
