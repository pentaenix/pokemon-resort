#include "gameplay/world3d/rendering/GlbModelRenderer.hpp"

#include <SDL_image.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace pr::gameplay::world3d::rendering {

namespace {

Uint8 toByte(float v) {
    return static_cast<Uint8>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
}

float sampleAlpha(const GlbModelRenderer::MaterialAlphaMask& mask, float u, float v) {
    if (mask.width <= 0 || mask.height <= 0 || mask.alpha.empty()) {
        return 255.0f;
    }
    const int x = std::clamp(static_cast<int>(u * static_cast<float>(mask.width)), 0, mask.width - 1);
    const int y = std::clamp(static_cast<int>(v * static_cast<float>(mask.height)), 0, mask.height - 1);
    return static_cast<float>(mask.alpha[static_cast<std::size_t>(y * mask.width + x)]);
}

bool pointInsideUvTriangle(
    float px,
    float py,
    float u0,
    float v0,
    float u1,
    float v1,
    float u2,
    float v2) {
    constexpr float kEpsilon = 1e-5f;
    const float e0 = (px - u0) * (v1 - v0) - (py - v0) * (u1 - u0);
    const float e1 = (px - u1) * (v2 - v1) - (py - v1) * (u2 - u1);
    const float e2 = (px - u2) * (v0 - v2) - (py - v2) * (u0 - u2);
    const bool has_neg = (e0 < -kEpsilon) || (e1 < -kEpsilon) || (e2 < -kEpsilon);
    const bool has_pos = (e0 > kEpsilon) || (e1 > kEpsilon) || (e2 > kEpsilon);
    return !(has_neg && has_pos);
}

bool uvTriangleTouchesTransparentAlpha(
    const GlbModelRenderer::MaterialAlphaMask& mask,
    float u0,
    float v0,
    float u1,
    float v1,
    float u2,
    float v2) {
    if (mask.width <= 0 || mask.height <= 0 || mask.alpha.empty()) {
        return true;
    }

    const float min_u = std::min({u0, u1, u2});
    const float max_u = std::max({u0, u1, u2});
    const float min_v = std::min({v0, v1, v2});
    const float max_v = std::max({v0, v1, v2});

    const int min_x = std::clamp(static_cast<int>(std::floor(min_u * static_cast<float>(mask.width))), 0, mask.width - 1);
    const int max_x = std::clamp(static_cast<int>(std::floor(max_u * static_cast<float>(mask.width))), 0, mask.width - 1);
    const int min_y = std::clamp(static_cast<int>(std::floor(min_v * static_cast<float>(mask.height))), 0, mask.height - 1);
    const int max_y = std::clamp(static_cast<int>(std::floor(max_v * static_cast<float>(mask.height))), 0, mask.height - 1);

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            const float su = (static_cast<float>(x) + 0.5f) / static_cast<float>(mask.width);
            const float sv = (static_cast<float>(y) + 0.5f) / static_cast<float>(mask.height);
            if (!pointInsideUvTriangle(su, sv, u0, v0, u1, v1, u2, v2)) {
                continue;
            }
            if (sampleAlpha(mask, su, sv) < 128.0f) {
                return true;
            }
        }
    }

    if (sampleAlpha(mask, u0, v0) < 128.0f) return true;
    if (sampleAlpha(mask, u1, v1) < 128.0f) return true;
    if (sampleAlpha(mask, u2, v2) < 128.0f) return true;

    const float cu = (u0 + u1 + u2) / 3.0f;
    const float cv = (v0 + v1 + v2) / 3.0f;
    return sampleAlpha(mask, cu, cv) < 128.0f;
}

// A projected vertex carrying both screen position and (un-wrapped) texture coordinates.
// UV→screen is affine within a triangle, so every field interpolates linearly when we clip.
struct UvVert {
    float sx = 0.0f;
    float sy = 0.0f;
    float depth = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

UvVert lerpVert(const UvVert& a, const UvVert& b, float t) {
    return UvVert{
        a.sx + (b.sx - a.sx) * t,
        a.sy + (b.sy - a.sy) * t,
        a.depth + (b.depth - a.depth) * t,
        a.u + (b.u - a.u) * t,
        a.v + (b.v - a.v) * t,
        a.r + (b.r - a.r) * t,
        a.g + (b.g - a.g) * t,
        a.b + (b.b - a.b) * t,
        a.a + (b.a - a.a) * t,
    };
}

// Sutherland-Hodgman clip of a convex polygon against one axis-aligned half-plane in UV
// space: axis 0 = u, axis 1 = v; keep the side >= bound (keepGreater) or <= bound.
void clipAxis(std::vector<UvVert>& poly, int axis, float bound, bool keepGreater) {
    if (poly.size() < 2) { poly.clear(); return; }
    std::vector<UvVert> out;
    out.reserve(poly.size() + 2);
    const auto coord = [axis](const UvVert& p) { return axis == 0 ? p.u : p.v; };
    for (std::size_t i = 0; i < poly.size(); ++i) {
        const UvVert& A = poly[i];
        const UvVert& B = poly[(i + 1) % poly.size()];
        const float ca = coord(A) - bound;
        const float cb = coord(B) - bound;
        const bool inA = keepGreater ? (ca >= 0.0f) : (ca <= 0.0f);
        const bool inB = keepGreater ? (cb >= 0.0f) : (cb <= 0.0f);
        if (inA) out.push_back(A);
        if (inA != inB) {
            const float denom = ca - cb;
            if (std::fabs(denom) > 1e-9f) out.push_back(lerpVert(A, B, ca / denom));
        }
    }
    poly.swap(out);
}

bool uvPolygonTouchesTransparentAlphaRebased(
    const GlbModelRenderer::MaterialAlphaMask& mask,
    const std::vector<UvVert>& poly,
    float ou,
    float ov) {
    for (std::size_t k = 1; k + 1 < poly.size(); ++k) {
        const UvVert& p0 = poly[0];
        const UvVert& p1 = poly[k];
        const UvVert& p2 = poly[k + 1];
        if (uvTriangleTouchesTransparentAlpha(
                mask,
                p0.u - ou,
                p0.v - ov,
                p1.u - ou,
                p1.v - ov,
                p2.u - ou,
                p2.v - ov)) {
            return true;
        }
    }
    return false;
}

bool uvTriangleTouchesTransparentAlphaWrapped(
    const GlbModelRenderer::MaterialAlphaMask& mask,
    float u0,
    float v0,
    float u1,
    float v1,
    float u2,
    float v2) {
    const UvVert va{0.0f, 0.0f, 0.0f, u0, v0};
    const UvVert vb{0.0f, 0.0f, 0.0f, u1, v1};
    const UvVert vc{0.0f, 0.0f, 0.0f, u2, v2};
    const int cuMin = static_cast<int>(std::floor(std::min({va.u, vb.u, vc.u})));
    const int cuMax = static_cast<int>(std::floor(std::max({va.u, vb.u, vc.u}) - 1e-4f));
    const int cvMin = static_cast<int>(std::floor(std::min({va.v, vb.v, vc.v})));
    const int cvMax = static_cast<int>(std::floor(std::max({va.v, vb.v, vc.v}) - 1e-4f));

    if (cuMin == cuMax && cvMin == cvMax) {
        return uvTriangleTouchesTransparentAlpha(
            mask,
            va.u - static_cast<float>(cuMin),
            va.v - static_cast<float>(cvMin),
            vb.u - static_cast<float>(cuMin),
            vb.v - static_cast<float>(cvMin),
            vc.u - static_cast<float>(cuMin),
            vc.v - static_cast<float>(cvMin));
    }

    for (int cv = cvMin; cv <= cvMax; ++cv) {
        for (int cu = cuMin; cu <= cuMax; ++cu) {
            std::vector<UvVert> poly{va, vb, vc};
            clipAxis(poly, 0, static_cast<float>(cu), true);
            clipAxis(poly, 0, static_cast<float>(cu + 1), false);
            clipAxis(poly, 1, static_cast<float>(cv), true);
            clipAxis(poly, 1, static_cast<float>(cv + 1), false);
            if (poly.size() >= 3 &&
                uvPolygonTouchesTransparentAlphaRebased(mask, poly, static_cast<float>(cu), static_cast<float>(cv))) {
                return true;
            }
        }
    }

    return false;
}

} // namespace

GlbModelRenderer::GlbModelRenderer(data::GlbMesh mesh, float x, float y, float z, float yaw_deg, float scale)
    : mesh_(std::move(mesh)), x_(x), y_(y), z_(z), scale_(scale) {
    const float yaw = yaw_deg * (3.1415926535f / 180.0f);
    cos_yaw_ = std::cos(yaw);
    sin_yaw_ = std::sin(yaw);
    material_textures_.assign(mesh_.materials.size(), nullptr);
    material_alpha_masks_.assign(mesh_.materials.size(), {});
    triangle_cutout_.assign(mesh_.triangles.size(), 0);
}

std::optional<float> GlbModelRenderer::anchorDepth(
    const camera::Gen4FollowCamera& camera,
    int viewport_w,
    int viewport_h) const {
    float sx = 0.0f, sy = 0.0f, depth = 0.0f;
    // Placement origin (ground anchor). Using the same metric the character billboards use
    // (camera depth of the world anchor) keeps inter-object occlusion consistent.
    if (!camera.worldToScreen(camera::Vec3{x_, y_, z_}, viewport_w, viewport_h, sx, sy, depth)) {
        return std::nullopt;
    }
    return depth;
}

void GlbModelRenderer::ensureTextures(SDL_Renderer* renderer) const {
    if (!textures_ready_) {
        textures_ready_ = true;
        for (std::size_t i = 0; i < mesh_.materials.size(); ++i) {
            const data::GlbMaterial& mat = mesh_.materials[i];
            if (!mat.has_texture || mat.image_bytes.empty()) continue;
            SDL_RWops* rw = SDL_RWFromConstMem(mat.image_bytes.data(), static_cast<int>(mat.image_bytes.size()));
            if (!rw) continue;
            SDL_Surface* loaded = IMG_Load_RW(rw, 1 /* free rw */);
            if (!loaded) continue;
            SDL_Surface* converted = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
            SDL_Surface* upload_surface = converted ? converted : loaded;

            if (mat.alpha_blend && converted) {
                MaterialAlphaMask mask;
                mask.width = converted->w;
                mask.height = converted->h;
                mask.alpha.resize(static_cast<std::size_t>(mask.width * mask.height));

                const Uint8* pixels = static_cast<const Uint8*>(converted->pixels);
                for (int y = 0; y < mask.height; ++y) {
                    const Uint32* row = reinterpret_cast<const Uint32*>(pixels + y * converted->pitch);
                    for (int x = 0; x < mask.width; ++x) {
                        Uint8 r = 0;
                        Uint8 g = 0;
                        Uint8 b = 0;
                        Uint8 a = 255;
                        SDL_GetRGBA(row[x], converted->format, &r, &g, &b, &a);
                        mask.alpha[static_cast<std::size_t>(y * mask.width + x)] = a;
                    }
                }

                material_alpha_masks_[i] = std::move(mask);
            }

            SDL_Texture* raw = SDL_CreateTextureFromSurface(renderer, upload_surface);
            if (converted) {
                SDL_FreeSurface(converted);
            }
            SDL_FreeSurface(loaded);
            if (!raw) continue;
            SDL_SetTextureScaleMode(raw, SDL_ScaleModeNearest);
            SDL_SetTextureBlendMode(raw, mat.alpha_blend ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
            material_textures_[i].reset(raw, SDL_DestroyTexture);
        }
    }

    if (!triangle_cutout_ready_) {
        classifyTriangleCutouts();
        triangle_cutout_ready_ = true;
    }
}

void GlbModelRenderer::classifyTriangleCutouts() const {
    triangle_cutout_.assign(mesh_.triangles.size(), 0);

    for (std::size_t i = 0; i < mesh_.triangles.size(); ++i) {
        const data::GlbTriangle& tri = mesh_.triangles[i];
        if (tri.material < 0 || tri.material >= static_cast<int>(mesh_.materials.size())) {
            triangle_cutout_[i] = 0;
            continue;
        }

        const data::GlbMaterial& mat = mesh_.materials[static_cast<std::size_t>(tri.material)];
        if (mat.render_class == data::GlbMaterial::RenderClass::UniformDecal) {
            triangle_cutout_[i] = 1;
            continue;
        }
        if (!mat.alpha_blend) {
            triangle_cutout_[i] = 0;
            continue;
        }
        if (mat.base_color[3] < 0.999f) {
            triangle_cutout_[i] = 1;
            continue;
        }

        const MaterialAlphaMask& mask = material_alpha_masks_[static_cast<std::size_t>(tri.material)];
        if (mask.width <= 0 || mask.height <= 0 || mask.alpha.empty()) {
            triangle_cutout_[i] = 1;
            continue;
        }

        triangle_cutout_[i] = uvTriangleTouchesTransparentAlphaWrapped(
            mask,
            tri.a.u,
            tri.a.v,
            tri.b.u,
            tri.b.v,
            tri.c.u,
            tri.c.v)
            ? 1
            : 0;
    }
}

void GlbModelRenderer::render(
    SDL_Renderer* renderer,
    const camera::Gen4FollowCamera& camera,
    int viewport_w,
    int viewport_h,
    float tint_r,
    float tint_g,
    float tint_b,
    float brightness) const {
#if SDL_VERSION_ATLEAST(2,0,18)
    if (!valid()) return;
    ensureTextures(renderer);

    const float br = std::max(0.0f, brightness);
    struct DrawTri {
        SDL_Vertex v[3];
        float depth = 0.0f;
        int material = -1;
        bool cutout = false;
        bool uniform_decal = false;
    };

    std::vector<DrawTri> draw;
    draw.reserve(mesh_.triangles.size());
    const double animation_time = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    const std::vector<std::vector<float>> morph_weights =
        data::sampleGlbMorphWeights(mesh_, animation_time);
    const std::vector<std::array<float, 4>> node_rotations =
        data::sampleGlbNodeRotations(mesh_, animation_time);

    const auto project = [&](const data::GlbVertex& vtx, float& sx, float& sy, float& depth) -> bool {
        // model space -> scale -> yaw(+Y) -> translate(placement)
        const std::array<float, 3> position = data::sampleGlbAnimatedPosition(
            mesh_, vtx, morph_weights, node_rotations);
        const float lx = position[0] * scale_;
        const float ly = position[1] * scale_;
        const float lz = position[2] * scale_;
        const float rx = lx * cos_yaw_ + lz * sin_yaw_;
        const float rz = -lx * sin_yaw_ + lz * cos_yaw_;
        const camera::Vec3 world{x_ + rx, y_ + ly, z_ + rz};
        return camera.worldToScreen(world, viewport_w, viewport_h, sx, sy, depth);
    };

    for (std::size_t tri_index = 0; tri_index < mesh_.triangles.size(); ++tri_index) {
        const data::GlbTriangle& tri = mesh_.triangles[tri_index];
        float x0, y0, d0, x1, y1, d1, x2, y2, d2;
        if (!project(tri.a, x0, y0, d0) || !project(tri.b, x1, y1, d1) || !project(tri.c, x2, y2, d2)) {
            continue;
        }
        const bool cutout = tri_index < triangle_cutout_.size() && triangle_cutout_[tri_index] != 0;
        bool uniform_decal = false;
        if (tri.material >= 0 && tri.material < static_cast<int>(mesh_.materials.size())) {
            const data::GlbMaterial& mat = mesh_.materials[static_cast<std::size_t>(tri.material)];
            uniform_decal = mat.render_class == data::GlbMaterial::RenderClass::UniformDecal;
        }
        float material_r = 1.0f;
        float material_g = 1.0f;
        float material_b = 1.0f;
        float material_a = 1.0f;
        if (tri.material >= 0 && tri.material < static_cast<int>(mesh_.materials.size())) {
            const data::GlbMaterial& mat = mesh_.materials[static_cast<std::size_t>(tri.material)];
            material_r = mat.base_color[0];
            material_g = mat.base_color[1];
            material_b = mat.base_color[2];
            material_a = mat.base_color[3];
        }

        // SDL_RenderGeometry CLAMPS texcoords to [0,1]; it does NOT honour the glTF sampler's
        // REPEAT wrap. DS-ripped UVs routinely run outside [0,1] (a roof at v=1.15 that should
        // wrap to 0.15) and some faces even span more than one tile in UV — clamping then
        // samples the wrong edge texel (black stains, smeared seams). We emulate REPEAT exactly
        // by clipping each triangle against the integer UV grid and re-basing every resulting
        // piece into [0,1): each piece lives in a single texture tile, so the clamp is a no-op.
        const UvVert va{x0, y0, d0, tri.a.u, tri.a.v, tri.a.r, tri.a.g, tri.a.b, tri.a.a};
        const UvVert vb{x1, y1, d1, tri.b.u, tri.b.v, tri.b.r, tri.b.g, tri.b.b, tri.b.a};
        const UvVert vc{x2, y2, d2, tri.c.u, tri.c.v, tri.c.r, tri.c.g, tri.c.b, tri.c.a};
        const int cuMin = static_cast<int>(std::floor(std::min({va.u, vb.u, vc.u})));
        const int cuMax = static_cast<int>(std::floor(std::max({va.u, vb.u, vc.u}) - 1e-4f));
        const int cvMin = static_cast<int>(std::floor(std::min({va.v, vb.v, vc.v})));
        const int cvMax = static_cast<int>(std::floor(std::max({va.v, vb.v, vc.v}) - 1e-4f));

        const auto emitPiece = [&](const std::vector<UvVert>& poly, float ou, float ov) {
            for (std::size_t k = 1; k + 1 < poly.size(); ++k) {
                const UvVert& p0 = poly[0];
                const UvVert& p1 = poly[k];
                const UvVert& p2 = poly[k + 1];
                const float u0 = p0.u - ou;
                const float v0 = p0.v - ov;
                const float u1 = p1.u - ou;
                const float v1 = p1.v - ov;
                const float u2 = p2.u - ou;
                const float v2 = p2.v - ov;
                DrawTri dt;
                const auto color = [&](const UvVert& point) {
                    return SDL_Color{
                        toByte(tint_r * br * material_r * point.r),
                        toByte(tint_g * br * material_g * point.g),
                        toByte(tint_b * br * material_b * point.b),
                        toByte(material_a * point.a)};
                };
                dt.v[0] = SDL_Vertex{{p0.sx, p0.sy}, color(p0), {u0, v0}};
                dt.v[1] = SDL_Vertex{{p1.sx, p1.sy}, color(p1), {u1, v1}};
                dt.v[2] = SDL_Vertex{{p2.sx, p2.sy}, color(p2), {u2, v2}};
                dt.depth = (p0.depth + p1.depth + p2.depth) / 3.0f;
                dt.material = tri.material;
                dt.cutout = cutout;
                dt.uniform_decal = uniform_decal;
                draw.push_back(dt);
            }
        };

        if (cuMin == cuMax && cvMin == cvMax) {
            // Common case: the whole triangle lives in one UV tile — just re-base it.
            emitPiece({va, vb, vc}, static_cast<float>(cuMin), static_cast<float>(cvMin));
        } else {
            for (int cv = cvMin; cv <= cvMax; ++cv) {
                for (int cu = cuMin; cu <= cuMax; ++cu) {
                    std::vector<UvVert> poly{va, vb, vc};
                    clipAxis(poly, 0, static_cast<float>(cu), true);
                    clipAxis(poly, 0, static_cast<float>(cu + 1), false);
                    clipAxis(poly, 1, static_cast<float>(cv), true);
                    clipAxis(poly, 1, static_cast<float>(cv + 1), false);
                    if (poly.size() >= 3) emitPiece(poly, static_cast<float>(cu), static_cast<float>(cv));
                }
            }
        }
    }

    // Depth is the primary sort key: draw far-to-near so general solid geometry keeps the
    // expected painter order. For nearly coplanar pieces only, use opaque-before-cutout as a
    // tie-break so banner/decal style alpha geometry still composites over its backing wall
    // instead of getting cut off by tiny centroid-depth noise.
    std::stable_sort(draw.begin(), draw.end(), [](const DrawTri& a, const DrawTri& b) {
        constexpr float kCoplanarDepthEpsilon = 0.25f;

        const float delta = a.depth - b.depth;
        if (std::fabs(delta) > kCoplanarDepthEpsilon) {
            return a.depth > b.depth; // far first
        }

        if (a.uniform_decal != b.uniform_decal) {
            return a.uniform_decal; // ground decals before walls when coplanar
        }

        if (a.cutout != b.cutout) {
            return !a.cutout; // opaque first, cutout second for near-coplanar pieces
        }

        return false;
    });

    static const int indices[3] = {0, 1, 2};
    for (const DrawTri& dt : draw) {
        SDL_Texture* tex = nullptr;
        if (dt.material >= 0 && dt.material < static_cast<int>(material_textures_.size())) {
            tex = material_textures_[static_cast<std::size_t>(dt.material)].get();
        }
        SDL_RenderGeometry(renderer, tex, dt.v, 3, indices, 3);
    }
#else
    (void)renderer;
    (void)camera;
    (void)viewport_w;
    (void)viewport_h;
    (void)tint_r;
    (void)tint_g;
    (void)tint_b;
    (void)brightness;
#endif
}

} // namespace pr::gameplay::world3d::rendering
