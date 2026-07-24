#include "gameplay/world3d/rendering/OverworldMapRenderer.hpp"

#include "gameplay/world3d/terrain/TerrainSurface.hpp"

#include <SDL_image.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace pr::gameplay::world3d::rendering {

OverworldMapRenderer::OverworldMapRenderer(const SceneConfig& scene) : scene_(scene) {}

namespace {

enum : int {
    kSpecialFlat = 0,
    kSpecialEditorAutoRamp = 1,
    kSpecialRampNorth = 2,
    kSpecialRampEast = 3,
    kSpecialRampSouth = 4,
    kSpecialRampWest = 5,
    kSpecialConvexNE = 6,
    kSpecialConvexSE = 7,
    kSpecialConvexSW = 8,
    kSpecialConvexNW = 9,
    kSpecialConcaveNE = 10,
    kSpecialConcaveSE = 11,
    kSpecialConcaveSW = 12,
    kSpecialConcaveNW = 13,
};

int parseObjVertexIndex(const std::string& token, int vertex_count) {
    const std::size_t slash = token.find('/');
    const std::string raw = (slash == std::string::npos) ? token : token.substr(0, slash);
    if (raw.empty()) return -1;
    const int idx = std::stoi(raw);
    if (idx > 0) return idx - 1;
    if (idx < 0) return vertex_count + idx;
    return -1;
}

int parseObjUvIndex(const std::string& token, int uv_count) {
    const std::size_t first = token.find('/');
    if (first == std::string::npos) return -1;
    const std::size_t second = token.find('/', first + 1);
    const std::string raw = token.substr(first + 1, second == std::string::npos ? std::string::npos : second - first - 1);
    if (raw.empty()) return -1;
    const int idx = std::stoi(raw);
    if (idx > 0) return idx - 1;
    if (idx < 0) return uv_count + idx;
    return -1;
}

std::string trim(const std::string& s) {
    const std::size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const std::size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

float wrapUv(float v) {
    const float f = v - std::floor(v);
    return f < 0.0f ? f + 1.0f : f;
}

int terrainWidth(const SceneConfig& scene) {
    if (!scene.terrain.heights.empty() && !scene.terrain.heights.front().empty()) {
        return static_cast<int>(scene.terrain.heights.front().size());
    }
    return std::max(1, scene.grid.width);
}

int terrainHeight(const SceneConfig& scene) {
    if (!scene.terrain.heights.empty()) {
        return static_cast<int>(scene.terrain.heights.size());
    }
    return std::max(1, scene.grid.height);
}

SDL_Color toSdlColor(const TerrainColor& color) {
    return SDL_Color{color.r, color.g, color.b, color.a};
}

} // namespace

bool OverworldMapRenderer::load() {
    const bool has_terrain_grid = !scene_.terrain.heights.empty();
    if (scene_.visual.mesh_path.empty()) {
        return has_terrain_grid;
    }

    std::ifstream in(scene_.visual.mesh_path);
    if (!in.is_open()) {
        return has_terrain_grid;
    }

    std::vector<camera::Vec3> verts;
    std::vector<SDL_FPoint> uvs;
    std::string active_material;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("v ", 0) == 0) {
            std::istringstream ss(line.substr(2));
            camera::Vec3 v;
            ss >> v.x >> v.y >> v.z;
            v.x = scene_.visual.origin_x + (v.x * scene_.visual.scale);
            v.y = scene_.visual.origin_y + (v.y * scene_.visual.scale);
            v.z = scene_.visual.origin_z + (v.z * scene_.visual.scale);
            verts.push_back(v);
        } else if (line.rfind("vt ", 0) == 0) {
            std::istringstream ss(line.substr(3));
            SDL_FPoint uv{};
            ss >> uv.x >> uv.y;
            uv.y = 1.0f - uv.y;
            uvs.push_back(uv);
        } else if (line.rfind("usemtl ", 0) == 0) {
            active_material = trim(line.substr(7));
        } else if (line.rfind("f ", 0) == 0) {
            std::istringstream ss(line.substr(2));
            std::vector<int> face_v;
            std::vector<int> face_t;
            std::string token;
            while (ss >> token) {
                face_v.push_back(parseObjVertexIndex(token, static_cast<int>(verts.size())));
                face_t.push_back(parseObjUvIndex(token, static_cast<int>(uvs.size())));
            }
            if (face_v.size() < 3) continue;
            for (std::size_t i = 1; i + 1 < face_v.size(); ++i) {
                const int ia = face_v[0];
                const int ib = face_v[i];
                const int ic = face_v[i + 1];
                if (ia < 0 || ib < 0 || ic < 0 ||
                    ia >= static_cast<int>(verts.size()) ||
                    ib >= static_cast<int>(verts.size()) ||
                    ic >= static_cast<int>(verts.size())) {
                    continue;
                }
                const int ta = face_t[0];
                const int tb = face_t[i];
                const int tc = face_t[i + 1];
                const SDL_FPoint uva = (ta >= 0 && ta < static_cast<int>(uvs.size())) ? uvs[ta] : SDL_FPoint{0.0f, 0.0f};
                const SDL_FPoint uvb = (tb >= 0 && tb < static_cast<int>(uvs.size())) ? uvs[tb] : SDL_FPoint{0.0f, 0.0f};
                const SDL_FPoint uvc = (tc >= 0 && tc < static_cast<int>(uvs.size())) ? uvs[tc] : SDL_FPoint{0.0f, 0.0f};
                triangles_.push_back(Tri{verts[ia], verts[ib], verts[ic], uva, uvb, uvc, active_material});
            }
        }
    }

    const std::filesystem::path mtl_path(scene_.visual.material_path);
    const std::filesystem::path mtl_dir = mtl_path.parent_path();
    std::ifstream mtl(scene_.visual.material_path);
    if (mtl.is_open()) {
        std::string mat_name;
        while (std::getline(mtl, line)) {
            if (line.rfind("newmtl ", 0) == 0) {
                mat_name = trim(line.substr(7));
            } else if (line.rfind("map_Kd ", 0) == 0 && !mat_name.empty()) {
                std::string tex = trim(line.substr(7));
                if (!tex.empty()) {
                    const std::filesystem::path tex_path(tex);
                    if (tex_path.is_absolute()) {
                        material_texture_paths_[mat_name] = tex_path.string();
                    } else {
                        material_texture_paths_[mat_name] = (mtl_dir / tex_path).string();
                    }
                }
            }
        }
    }

    return has_terrain_grid || !triangles_.empty();
}

bool OverworldMapRenderer::ensureTexturesLoaded(SDL_Renderer* renderer) const {
    for (const auto& [mat, path] : material_texture_paths_) {
        if (material_textures_.find(mat) != material_textures_.end()) {
            continue;
        }
        SDL_Surface* surface = IMG_Load(path.c_str());
        if (!surface) {
            continue;
        }
        SDL_Texture* raw = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_FreeSurface(surface);
        if (!raw) {
            continue;
        }
        material_textures_[mat].reset(raw, SDL_DestroyTexture);
    }
    return true;
}

void OverworldMapRenderer::render(
    SDL_Renderer* renderer,
    const camera::Gen4FollowCamera& camera,
    int viewport_w,
    int viewport_h) const {
#if SDL_VERSION_ATLEAST(2,0,18)
    ensureTexturesLoaded(renderer);

    struct DrawTri {
        SDL_Vertex v0;
        SDL_Vertex v1;
        SDL_Vertex v2;
        float avg_depth = std::numeric_limits<float>::max();
        std::string material;
    };

    std::vector<DrawTri> draw_tris;
    draw_tris.reserve(triangles_.size());

    for (const Tri& tri : triangles_) {
        float x0, y0, z0, x1, y1, z1, x2, y2, z2;
        if (!camera.worldToScreen(tri.a, viewport_w, viewport_h, x0, y0, z0) ||
            !camera.worldToScreen(tri.b, viewport_w, viewport_h, x1, y1, z1) ||
            !camera.worldToScreen(tri.c, viewport_w, viewport_h, x2, y2, z2)) {
            continue;
        }

        const float avg_depth = (z0 + z1 + z2) / 3.0f;
        const Uint8 shade = static_cast<Uint8>(std::clamp(235.0f - (avg_depth / 22.0f), 100.0f, 235.0f));
        SDL_Color color{shade, shade, shade, 255};
        const float br = std::max(0.0f, scene_.lighting_brightness);
        color.r = static_cast<Uint8>(std::clamp((scene_.lighting_tint_r * br) * static_cast<float>(color.r), 0.0f, 255.0f));
        color.g = static_cast<Uint8>(std::clamp((scene_.lighting_tint_g * br) * static_cast<float>(color.g), 0.0f, 255.0f));
        color.b = static_cast<Uint8>(std::clamp((scene_.lighting_tint_b * br) * static_cast<float>(color.b), 0.0f, 255.0f));
        draw_tris.push_back(DrawTri{
            SDL_Vertex{{x0, y0}, color, {wrapUv(tri.ta.x), wrapUv(tri.ta.y)}},
            SDL_Vertex{{x1, y1}, color, {wrapUv(tri.tb.x), wrapUv(tri.tb.y)}},
            SDL_Vertex{{x2, y2}, color, {wrapUv(tri.tc.x), wrapUv(tri.tc.y)}},
            avg_depth,
            tri.material});
    }

    std::sort(draw_tris.begin(), draw_tris.end(), [](const DrawTri& a, const DrawTri& b) {
        return a.avg_depth > b.avg_depth;
    });

    for (const DrawTri& tri : draw_tris) {
        SDL_Vertex verts[3] = {tri.v0, tri.v1, tri.v2};
        const int indices[3] = {0, 1, 2};
        SDL_Texture* tex = nullptr;
        auto it = material_textures_.find(tri.material);
        if (it != material_textures_.end() && it->second) {
            tex = it->second.get();
            const float br = std::max(0.0f, scene_.lighting_brightness);
            SDL_SetTextureColorMod(
                tex,
                static_cast<Uint8>(std::clamp(scene_.lighting_tint_r * br, 0.0f, 1.0f) * 255.0f),
                static_cast<Uint8>(std::clamp(scene_.lighting_tint_g * br, 0.0f, 1.0f) * 255.0f),
                static_cast<Uint8>(std::clamp(scene_.lighting_tint_b * br, 0.0f, 1.0f) * 255.0f));
        }
        SDL_RenderGeometry(renderer, tex, verts, 3, indices, 3);
    }

    const int grid_w = terrainWidth(scene_);
    const int grid_h = terrainHeight(scene_);
    if (scene_.terrain.heights.empty() || grid_w <= 0 || grid_h <= 0) {
        return;
    }

    const float tile_size = std::max(1.0f, scene_.grid.tile_size);
    const auto tile_h = [this](int tx, int ty) -> int {
        if (ty < 0 || ty >= static_cast<int>(scene_.terrain.heights.size())) return 0;
        const auto& row = scene_.terrain.heights[static_cast<std::size_t>(ty)];
        if (tx < 0 || tx >= static_cast<int>(row.size())) return 0;
        return static_cast<int>(row[static_cast<std::size_t>(tx)]);
    };
    const auto tile_special = [this](int tx, int ty) -> int {
        if (ty < 0 || ty >= static_cast<int>(scene_.terrain.specials.size())) return kSpecialFlat;
        const auto& row = scene_.terrain.specials[static_cast<std::size_t>(ty)];
        if (tx < 0 || tx >= static_cast<int>(row.size())) return kSpecialFlat;
        return static_cast<int>(row[static_cast<std::size_t>(tx)]);
    };

    struct RenderFace {
        SDL_Vertex verts[4];
        float depth_key = std::numeric_limits<float>::lowest();
    };
    static thread_local std::vector<RenderFace> render_faces;
    render_faces.clear();
    const std::size_t reserve_faces = static_cast<std::size_t>(grid_w * grid_h * 4);
    if (render_faces.capacity() < reserve_faces) {
        render_faces.reserve(reserve_faces);
    }

    const auto push_quad_face = [&](float x0w, float y0w, float z0w,
                                    float x1w, float y1w, float z1w,
                                    float x2w, float y2w, float z2w,
                                    float x3w, float y3w, float z3w,
                                    SDL_Color color) {
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
        render_faces.push_back(face);
    };

    const auto fill_tile_corner_heights = [&](int x, int y, float (&c)[4]) {
        terrain::fillTileCornerHeights(scene_, x, y, c);
    };

    const auto push_tile_top = [&](int x, int y, SDL_Color color) {
        const float x0 = static_cast<float>(x) * tile_size;
        const float z0 = static_cast<float>(y) * tile_size;
        const float x1 = x0 + tile_size;
        const float z1 = z0 + tile_size;
        float c[4]{};
        fill_tile_corner_heights(x, y, c);
        const int special = tile_special(x, y);
        if (special == kSpecialEditorAutoRamp) {
            color = SDL_Color{255, 64, 64, 255};
        } else if (special < kSpecialFlat || special > kSpecialConcaveNW) {
            color = SDL_Color{255, 0, 255, 255};
        }

        push_quad_face(x0, c[0], z0, x1, c[1], z0, x1, c[2], z1, x0, c[3], z1, color);
    };

    const SDL_Color floor_a = toSdlColor(scene_.terrain.floor_color_a);
    const SDL_Color floor_b = toSdlColor(scene_.terrain.floor_color_b);
    const SDL_Color first_non_base_a = toSdlColor(scene_.terrain.first_non_base_floor_color_a);
    const SDL_Color first_non_base_b = toSdlColor(scene_.terrain.first_non_base_floor_color_b);
    const SDL_Color ramp_a = toSdlColor(scene_.terrain.ramp_color_a);
    const SDL_Color ramp_b = toSdlColor(scene_.terrain.ramp_color_b);
    for (int y = 0; y < grid_h; ++y) {
        for (int x = 0; x < grid_w; ++x) {
            const bool slope = terrain::isSlopeSpecial(tile_special(x, y));
            const bool checker = ((x + y) & 1) != 0;
            SDL_Color color = checker ? floor_b : floor_a;
            if (scene_.terrain.floor_height_recolor_enabled && tile_h(x, y) == 1) {
                color = checker ? first_non_base_b : first_non_base_a;
            }
            if (slope && scene_.terrain.ramp_recolor_enabled) {
                color = checker ? ramp_b : ramp_a;
            }
            push_tile_top(x, y, color);
        }
    }

    const auto add_wall_if_drop = [&](float xa, float za, float ya0, float ya1,
                                      float xb, float zb, float yb0, float yb1,
                                      SDL_Color color) {
        const float edge_min_a = std::min(ya0, ya1);
        const float edge_min_b = std::min(yb0, yb1);
        if (edge_min_a <= edge_min_b) return;
        push_quad_face(
            xa, edge_min_b, za,
            xb, edge_min_b, zb,
            xb, edge_min_a, zb,
            xa, edge_min_a, za,
            color);
    };

    const SDL_Color wall_ns = toSdlColor(scene_.terrain.wall_color_ns);
    const SDL_Color wall_ew = toSdlColor(scene_.terrain.wall_color_ew);
    for (int y = 0; y < grid_h; ++y) {
        for (int x = 0; x < grid_w; ++x) {
            const float x0 = static_cast<float>(x) * tile_size;
            const float z0 = static_cast<float>(y) * tile_size;
            const float x1 = x0 + tile_size;
            const float z1 = z0 + tile_size;

            float c[4]{};
            fill_tile_corner_heights(x, y, c);

            // East shared edge.
            if (x + 1 < grid_w) {
                float n[4]{};
                fill_tile_corner_heights(x + 1, y, n);
                add_wall_if_drop(x1, z0, c[1], c[2], x1, z1, n[0], n[3], wall_ew);
                add_wall_if_drop(x1, z0, n[0], n[3], x1, z1, c[1], c[2], wall_ew);
            }
            // South shared edge.
            if (y + 1 < grid_h) {
                float n[4]{};
                fill_tile_corner_heights(x, y + 1, n);
                add_wall_if_drop(x0, z1, c[3], c[2], x1, z1, n[0], n[1], wall_ns);
                add_wall_if_drop(x0, z1, n[0], n[1], x1, z1, c[3], c[2], wall_ns);
            }
        }
    }

    std::sort(render_faces.begin(), render_faces.end(), [](const RenderFace& a, const RenderFace& b) {
        return a.depth_key > b.depth_key;
    });

    constexpr int tri_indices[6] = {0, 1, 2, 0, 2, 3};
    for (const RenderFace& face : render_faces) {
        SDL_RenderGeometry(renderer, nullptr, face.verts, 4, tri_indices, 6);
    }
#else
    (void)renderer;
    (void)camera;
    (void)viewport_w;
    (void)viewport_h;
#endif
}

} // namespace pr::gameplay::world3d::rendering
