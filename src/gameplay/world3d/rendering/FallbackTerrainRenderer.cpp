#include "gameplay/world3d/rendering/FallbackTerrainRenderer.hpp"

#include "gameplay/world3d/interiors/DefaultRoomGeometry.hpp"
#include "gameplay/world3d/interiors/InteriorFloorCutout.hpp"
#include "gameplay/world3d/rendering/InteriorDefaultRoom.hpp"
#include "gameplay/world3d/terrain/TerrainSurface.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>
#include <vector>

namespace pr::gameplay::world3d::rendering {

namespace {

SDL_Color toSdlColor(const TerrainColor& color) {
    return SDL_Color{color.r, color.g, color.b, color.a};
}

} // namespace

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
        terrain::fillTileCornerHeights(scene, tx, ty, out);
    };
    const auto curved_corner_heights = [&](int tx, int ty, float (&out)[4]) {
        const int h = tile_h(tx, ty);
        const float floor_height = terrain::heightPerFloor(scene);
        const float base = static_cast<float>(h) * floor_height;
        const auto sample = [&](int sx, int sy) -> float {
            return static_cast<float>(tile_h(sx, sy)) * floor_height;
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
        const auto clip = interiors::clipDefaultRoomFloorCell(scene, x, z, tile_size);
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
                const float v0 = std::max(clip.v0,
                    static_cast<float>(iz) / static_cast<float>(kSubdiv));
                const float v1 = std::min(clip.v1,
                    static_cast<float>(iz + 1) / static_cast<float>(kSubdiv));
                if (v1 <= v0) continue;
                for (int ix = 0; ix < kSubdiv; ++ix) {
                    const float u0 = std::max(clip.u0,
                        static_cast<float>(ix) / static_cast<float>(kSubdiv));
                    const float u1 = std::min(clip.u1,
                        static_cast<float>(ix + 1) / static_cast<float>(kSubdiv));
                    if (u1 <= u0) continue;
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
            const float y00 = terrain::heightAtWorldPositionOnTile(
                scene, clip.x0, clip.z0, x, z, true);
            const float y10 = terrain::heightAtWorldPositionOnTile(
                scene, clip.x1, clip.z0, x, z, true);
            const float y11 = terrain::heightAtWorldPositionOnTile(
                scene, clip.x1, clip.z1, x, z, true);
            const float y01 = terrain::heightAtWorldPositionOnTile(
                scene, clip.x0, clip.z1, x, z, true);
            push_quad_face(
                clip.x0, y00, clip.z0,
                clip.x1, y10, clip.z0,
                clip.x1, y11, clip.z1,
                clip.x0, y01, clip.z1,
                color,
                wire);
        }
    };

    const bool default_interior_room = shouldRenderDefaultInteriorRoom(scene);
    const auto& room = scene.interior.default_room;
    for (int z = 0; z < grid_h; ++z) {
        for (int x = 0; x < grid_w; ++x) {
            const bool checker = ((x + z) & 1) == 0;
            const bool slope = terrain::isSlopeSpecial(tile_special(x, z));
            SDL_Color color = checker
                ? toSdlColor(default_interior_room ? room.floor_color_a : scene.terrain.floor_color_a)
                : toSdlColor(default_interior_room ? room.floor_color_b : scene.terrain.floor_color_b);
            if (scene.terrain.floor_height_recolor_enabled && tile_h(x, z) == 1) {
                color = checker
                    ? toSdlColor(scene.terrain.first_non_base_floor_color_a)
                    : toSdlColor(scene.terrain.first_non_base_floor_color_b);
            }
            if (slope && scene.terrain.ramp_recolor_enabled) {
                color = checker ? toSdlColor(scene.terrain.ramp_color_a) : toSdlColor(scene.terrain.ramp_color_b);
            }
            if (default_interior_room && !scene.interior.floor_cutouts.empty() &&
                !terrain::isSlopeSpecial(tile_special(x, z))) {
                for (const auto& triangle :
                     interiors::clipFloorCellAgainstCutouts(scene, x, z, tile_size)) {
                    const float y0 = terrain::heightAtWorldPositionOnTile(
                        scene, triangle[0].x, triangle[0].z, x, z, true);
                    const float y1 = terrain::heightAtWorldPositionOnTile(
                        scene, triangle[1].x, triangle[1].z, x, z, true);
                    const float y2 = terrain::heightAtWorldPositionOnTile(
                        scene, triangle[2].x, triangle[2].z, x, z, true);
                    push_quad_face(
                        triangle[0].x, y0, triangle[0].z,
                        triangle[1].x, y1, triangle[1].z,
                        triangle[2].x, y2, triangle[2].z,
                        triangle[2].x, y2, triangle[2].z,
                        color, false);
                }
            } else {
                emit_tile_top(x, z, color, false);
            }
        }
    }
    for (const auto& quad : interiors::buildDefaultRoomFloorApron(scene, tile_size)) {
        const auto& p = quad.points;
        const SDL_Color color = toSdlColor(
            ((quad.source_tile_x + quad.source_tile_y) & 1) == 0
                ? room.floor_color_a : room.floor_color_b);
        push_quad_face(
            p[0].x, p[0].y, p[0].z,
            p[1].x, p[1].y, p[1].z,
            p[2].x, p[2].y, p[2].z,
            p[3].x, p[3].y, p[3].z,
            color, false);
    }
    const SDL_Color wall_color_ns = toSdlColor(
        default_interior_room ? room.wall_color_ns : scene.terrain.wall_color_ns);
    const SDL_Color wall_color_ew = toSdlColor(
        default_interior_room ? room.wall_color_ew : scene.terrain.wall_color_ew);
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

    if (default_interior_room) {
        const SDL_Color trim_color = toSdlColor(room.trim_color);
        const SDL_Color baseboard_color = toSdlColor(room.baseboard_color);
        const SDL_Color top_cap_color = toSdlColor(room.top_cap_color);
        const auto push_wall_segment = [&](std::string_view edge,
                                           float ax, float az, float ay,
                                           float bx, float bz, float by,
                                           float height_tiles,
                                           SDL_Color body_color) {
            const float height = std::max(0.0f, height_tiles) * tile_size;
            if (height <= 0.001f) return;
            const auto [normal_x, normal_z] =
                interiors::wallOutwardNormal(edge);
            const auto line = interiors::placeDefaultRoomWallLine(
                scene, edge, tile_size, ax, az, bx, bz);
            ax = line.ax;
            az = line.az;
            bx = line.bx;
            bz = line.bz;
            const float band = std::min(
                std::max(0.0f, room.trim_height_tiles) * tile_size,
                height * 0.35f);
            if (band > 0.001f) {
                push_quad_face(
                    ax, ay, az, bx, by, bz, bx, by + band, bz, ax, ay + band, az,
                    baseboard_color, false);
            }
            if (height > band * 2.0f + 0.001f) {
                push_quad_face(
                    ax, ay + band, az, bx, by + band, bz,
                    bx, by + height - band, bz, ax, ay + height - band, az,
                    body_color, false);
            }
            if (band > 0.001f) {
                push_quad_face(
                    ax, ay + height - band, az, bx, by + height - band, bz,
                    bx, by + height, bz, ax, ay + height, az,
                    trim_color, false);
            }
            const float cap_depth =
                std::max(0.0f, room.top_cap_depth_tiles) * tile_size;
            if (room.black_top_cap && cap_depth > 0.001f) {
                push_quad_face(
                    ax, ay + height, az,
                    bx, by + height, bz,
                    bx + normal_x * cap_depth, by + height, bz + normal_z * cap_depth,
                    ax + normal_x * cap_depth, ay + height, az + normal_z * cap_depth,
                    top_cap_color, false);
            }
        };
        const auto push_boundary_segment = [&](std::string_view edge,
                                                bool opening,
                                                float ax, float az, float ay,
                                                float bx, float bz, float by,
                                                float height_tiles,
                                                SDL_Color body_color) {
            if (!opening) {
                push_wall_segment(
                    edge, ax, az, ay, bx, bz, by, height_tiles, body_color);
                return;
            }
            const float clearance_tiles =
                defaultInteriorOpeningHeightTiles(scene, edge);
            const float lintel_height_tiles =
                std::max(0.0f, height_tiles - clearance_tiles);
            if (lintel_height_tiles <= 0.001f) return;
            const float clearance = clearance_tiles * tile_size;
            push_wall_segment(
                edge, ax, az, ay + clearance, bx, bz, by + clearance,
                lintel_height_tiles, body_color);
        };
        const float lower_facade_depth =
            std::max(0.0f, room.lower_facade_depth_tiles) * tile_size;
        if (lower_facade_depth > 0.001f) {
            const SDL_Color lower_facade_color = toSdlColor(room.lower_facade_color);
            for (int x = 0; x < grid_w; ++x) {
                float south[4]{};
                corner_heights(x, grid_h - 1, south);
                const auto line = interiors::placeDefaultRoomWallLine(
                    scene, "south", tile_size,
                    (x + 1) * tile_size, grid_h * tile_size,
                    x * tile_size, grid_h * tile_size);
                push_quad_face(
                    line.ax, south[2] - lower_facade_depth, line.az,
                    line.bx, south[3] - lower_facade_depth, line.bz,
                    line.bx, south[3], line.bz,
                    line.ax, south[2], line.az,
                    lower_facade_color, false);
            }
        }
        for (int x = 0; x < grid_w; ++x) {
            float north[4]{};
            corner_heights(x, 0, north);
            push_boundary_segment("north",
                defaultInteriorOpeningCovers(scene, "north", x),
                x * tile_size, 0.0f, north[0],
                (x + 1) * tile_size, 0.0f, north[1],
                defaultInteriorWallHeightTiles(scene, "north"), wall_color_ns);
            float south[4]{};
            corner_heights(x, grid_h - 1, south);
            push_boundary_segment("south",
                defaultInteriorOpeningCovers(scene, "south", x),
                (x + 1) * tile_size, grid_h * tile_size, south[2],
                x * tile_size, grid_h * tile_size, south[3],
                defaultInteriorWallHeightTiles(scene, "south"), wall_color_ns);
        }
        for (int z = 0; z < grid_h; ++z) {
            float west[4]{};
            corner_heights(0, z, west);
            push_boundary_segment("west",
                defaultInteriorOpeningCovers(scene, "west", z),
                0.0f, (z + 1) * tile_size, west[3],
                0.0f, z * tile_size, west[0],
                defaultInteriorWallHeightTiles(scene, "west"), wall_color_ew);
            float east[4]{};
            corner_heights(grid_w - 1, z, east);
            push_boundary_segment("east",
                defaultInteriorOpeningCovers(scene, "east", z),
                grid_w * tile_size, z * tile_size, east[1],
                grid_w * tile_size, (z + 1) * tile_size, east[2],
                defaultInteriorWallHeightTiles(scene, "east"), wall_color_ew);
        }
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    const SDL_Color wire_color = toSdlColor(scene.terrain.wire_color);
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
