#include "AttendBgfxRendererInternal.hpp"

#include "gameplay/attend/rendering/AttendPokemonMaterialPolicy.hpp"

namespace pr::gameplay::attend::rendering {

bool AttendBgfxRenderer::Impl::buildStaticGlbMesh(
    MeshResource& mesh,
    const std::string& path,
    float x,
    float y,
    float z,
    float yaw_degrees,
    float scale) {
    if (path.empty()) return false;
    std::string error;
    const pr::gameplay::world3d::data::GlbMesh src =
        pr::gameplay::world3d::data::loadGlbModel(path, &error);
    if (!src.valid || src.triangles.empty()) {
        std::cerr << "[AttendBgfx] Could not load static GLB " << path << ": " << error << std::endl;
        return false;
    }

    mesh.materials.resize(src.materials.size());
    for (std::size_t i = 0; i < src.materials.size(); ++i) {
        const auto& in = src.materials[i];
        MaterialResource& out = mesh.materials[i];
        out.name = in.name;
        out.visible = !containsAnySubstring(in.name, config_.floor.hidden_material_substrings);
        std::copy(std::begin(in.base_color), std::end(in.base_color), std::begin(out.base_color));
        out.alpha_cutoff = in.alpha_cutoff;
        out.sampler_flags = smoothSamplerFlags();
        if (in.has_texture) {
            out.texture = decodeTexture(in.image_bytes, in.name.empty() ? "attend_static_glb" : in.name.c_str());
        }
        const bool declared_blend =
            in.alpha_mode == pr::gameplay::world3d::data::GlbMaterial::AlphaMode::Blend;
        const bool legacy_binary_cutout = shouldTreatLegacyBinaryAlphaBlendAsMask(
            declared_blend,
            out.texture.has_zero_alpha,
            out.texture.has_partial_alpha,
            out.base_color[3]);
        out.mask_cutout =
            in.alpha_mode == pr::gameplay::world3d::data::GlbMaterial::AlphaMode::Mask ||
            legacy_binary_cutout ||
            (out.texture.has_zero_alpha && !declared_blend && out.base_color[3] >= 0.999f);
        out.blend = !out.mask_cutout && (declared_blend || out.base_color[3] < 0.999f);
        if (out.mask_cutout) {
            out.alpha_cutoff = std::max(out.alpha_cutoff, 0.5f);
        }
    }
    if (mesh.materials.empty()) {
        mesh.materials.push_back(MaterialResource{});
    }

    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    vertices.reserve(src.triangles.size() * 3);
    indices.reserve(src.triangles.size() * 3);
    const int material_count = static_cast<int>(mesh.materials.size());
    for (int material = 0; material < material_count; ++material) {
        const std::uint32_t start = static_cast<std::uint32_t>(indices.size());
        for (const auto& tri : src.triangles) {
            const int tri_material = tri.material >= 0 ? tri.material : 0;
            if (tri_material != material) continue;
            const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
            vertices.push_back(transformStaticVertex(tri.a, x, y, z, yaw_degrees, scale));
            vertices.push_back(transformStaticVertex(tri.b, x, y, z, yaw_degrees, scale));
            vertices.push_back(transformStaticVertex(tri.c, x, y, z, yaw_degrees, scale));
            indices.insert(indices.end(), {base, base + 1, base + 2});
        }
        const std::uint32_t count = static_cast<std::uint32_t>(indices.size()) - start;
        if (count > 0) {
            mesh.ranges.push_back(MeshResource::Range{start, count, material});
        }
    }
    return uploadMesh(mesh, vertices, indices);
}

bool AttendBgfxRenderer::Impl::buildAnimatedFloor() {
    if (config_.floor.model_path.empty()) return false;
    std::string error;
    floor_model_ = loadAttendPokemonModel(config_.floor.model_path, &error);
    if (!floor_model_.valid || floor_model_.primitives.empty()) {
        floor_model_ = AttendPokemonModel{};
        return false;
    }
    floor_environment_ = floor_model_.environment_scene.enabled;
    if (floor_environment_) {
        const AttendEnvironmentScene& environment = floor_model_.environment_scene;
        const auto supported_state = [](const std::vector<AttendEnvironmentState>& states,
                                        const std::string& requested,
                                        const std::string& fallback) {
            const auto available = [&](const std::string& id) {
                return std::any_of(states.begin(), states.end(), [&](const AttendEnvironmentState& state) {
                    return state.available && state.id == id;
                });
            };
            if (!requested.empty() && available(requested)) return requested;
            if (available(fallback)) return fallback;
            const auto first = std::find_if(states.begin(), states.end(), [](const AttendEnvironmentState& state) {
                return state.available;
            });
            return first == states.end() ? fallback : first->id;
        };
        environment_time_id_ = supported_state(
            environment.time_states,
            config_.floor.time_of_day,
            environment.default_time.empty() ? "day" : environment.default_time);
        environment_weather_id_ = supported_state(
            environment.weather_states,
            config_.floor.weather_id,
            environment.default_weather.empty() ? "clear" : environment.default_weather);
    }
    floor_animation_ = floor_environment_
        ? nullptr
        : findAttendPokemonAnimation(floor_model_, config_.floor.animation_name);
    if (!floor_environment_ && (!floor_animation_ || floor_animation_->channels.empty())) {
        floor_model_ = AttendPokemonModel{};
        floor_animation_ = nullptr;
        return false;
    }

    floor_mesh_.materials.resize(floor_model_.materials.size());
    for (std::size_t i = 0; i < floor_model_.materials.size(); ++i) {
        const AttendPokemonMaterial& src = floor_model_.materials[i];
        MaterialResource& dst = floor_mesh_.materials[i];
        dst.name = src.name;
        dst.visible = !containsAnySubstring(src.name, config_.floor.hidden_material_substrings);
        std::copy(std::begin(src.base_color), std::end(src.base_color), std::begin(dst.base_color));
        dst.alpha_cutoff = src.alpha_cutoff;
        dst.pokemon_eye = false;
        dst.texture_mapping = src.texture_mapping;
        dst.sampler_flags = smoothSamplerFlags(src.base_color_sampler.wrap_s, src.base_color_sampler.wrap_t);
        const std::string environment_material_name = lowercaseAscii(src.name);
        dst.pica_tev = src.pica_tev;
        // sea_iro stores its displayed ocean colour directly. RAE samples it
        // as untagged PICA bytes and lets the browser's output transform do
        // the presentation conversion; Attend must likewise avoid applying
        // a second one-way gamma transform to only this colour pass.
        const bool sea_color_buffer =
            environment_material_name.find("sea_iro") != std::string::npos &&
            !src.has_authoritative_pica;
        const bool authored_ground_color =
            environment_material_name.find("_jime") != std::string::npos ||
            environment_material_name == "btl_g_eg03" ||
            environment_material_name == "btl_g_egk3";
        dst.pica_tev.sea_color_buffer = sea_color_buffer;
        dst.pica_tev.display_encoded_output = sea_color_buffer || authored_ground_color;
        const bool named_wave_overlay =
            environment_material_name.find("nami") != std::string::npos;
        dst.environment_pass_priority = src.environment_role == "water_base"
            ? 0
            : ((src.environment_role == "water_overlay" || named_wave_overlay) ? 10 : 5);
        // Some freshwater wave materials predate the explicit environment
        // role metadata. They are the same soft additive PICA pass as the sea
        // wave materials; rendering them at full linear strength produces a
        // conspicuous circular band through the arena.
        if (named_wave_overlay && dst.pica_tev.effect_color_scale >= 0.999f) {
            // sea_nami01 is the broad ocean highlight sheet. At the same
            // strength as the small foam cards it covers the entire centre
            // arena with a white veil; the 3DS framebuffer attenuates this
            // pass more strongly than the local wavelets.
            dst.pica_tev.effect_color_scale =
                environment_material_name.find("sea_nami01") != std::string::npos
                    ? 0.58f
                    : 0.82f;
        }
        dst.uv_offsets = src.pica_tev.initial_offsets;
        if (src.pica_tev.enabled) {
            for (int unit = 0; unit < 3; ++unit) {
                const std::size_t index = static_cast<std::size_t>(unit);
                dst.texture_unit_sampler_flags[index] = smoothSamplerFlags(
                    src.texture_unit_samplers[index].wrap_s,
                    src.texture_unit_samplers[index].wrap_t);
                if (src.has_texture_unit[index]) {
                    dst.texture_units[index] = decodeTexture(
                        src.texture_unit_bytes[index],
                        src.name.empty() ? config_.floor.id.c_str() : src.name.c_str());
                }
            }
        } else if (src.has_base_color_texture) {
            dst.texture = decodeTexture(
                src.base_color_bytes,
                src.name.empty() ? config_.floor.id.c_str() : src.name.c_str());
        }
        dst.additive = src.render_class == AttendRenderClass::Additive;
        dst.multiplicative = src.pica_multiplicative_blend;
        dst.blend = src.render_class == AttendRenderClass::Blend ||
                    dst.additive ||
                    dst.multiplicative ||
                    src.render_class == AttendRenderClass::UniformDecal ||
                    dst.base_color[3] < 0.999f ||
                    src.pica_vertex_alpha_blend ||
                    dst.pica_tev.standalone_black_key;
        dst.mask_cutout = src.render_class == AttendRenderClass::Mask &&
            !src.pica_vertex_alpha_blend &&
            !src.pica_multiplicative_blend;
        if (!src.pica_tev.enabled && !src.has_rae_policy && !src.has_alpha_mode) {
            dst.blend = dst.blend || dst.texture.has_partial_alpha;
            dst.mask_cutout = dst.texture.has_zero_alpha && !dst.blend && dst.base_color[3] >= 0.999f;
        }
        if (dst.mask_cutout) {
            dst.alpha_cutoff = std::max(dst.alpha_cutoff, 0.5f);
        }
    }
    if (floor_mesh_.materials.empty()) {
        floor_mesh_.materials.push_back(MaterialResource{});
    }

    float floor_model_x = config_.floor.model_x;
    float floor_model_y = config_.floor.model_y;
    float floor_model_z = config_.floor.model_z;
    if (floor_environment_) {
        floor_model_x -= floor_model_.environment_scene.surface_anchor[0] * config_.floor.model_scale;
        floor_model_y -= floor_model_.environment_scene.surface_anchor[1] * config_.floor.model_scale;
        floor_model_z -= floor_model_.environment_scene.surface_anchor[2] * config_.floor.model_scale;
    }

    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    floor_skinned_primitives_.resize(floor_model_.primitives.size());
    const std::vector<std::array<float, 16>> floor_globals =
        buildAttendPokemonGlobals(floor_model_, floor_animation_, 0.0);
    const std::vector<std::vector<std::array<float, 16>>> floor_skin_matrices =
        buildAttendPokemonSkinMatrices(floor_model_, floor_globals);
    for (std::size_t primitive_index = 0; primitive_index < floor_model_.primitives.size(); ++primitive_index) {
        skinAttendPokemonPrimitiveWithPose(
            floor_model_,
            floor_model_.primitives[primitive_index],
            floor_globals,
            floor_skin_matrices,
            floor_skinned_primitives_[primitive_index]);
    }
    if (floor_environment_) {
        for (std::size_t primitive_index = 0; primitive_index < floor_model_.primitives.size(); ++primitive_index) {
            const AttendPokemonPrimitive& primitive = floor_model_.primitives[primitive_index];
            const std::uint32_t start = static_cast<std::uint32_t>(indices.size());
            const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
            for (const AttendPokemonVertex& v : floor_skinned_primitives_[primitive_index]) {
                vertices.push_back(transformAttendVertex(
                    v,
                    floor_model_x,
                    floor_model_y,
                    floor_model_z,
                    config_.floor.model_yaw_degrees,
                    config_.floor.model_scale));
            }
            for (std::uint32_t index : primitive.indices) indices.push_back(base + index);
            const std::uint32_t count = static_cast<std::uint32_t>(indices.size()) - start;
            if (count == 0) continue;
            MeshResource::Range range;
            range.start = start;
            range.count = count;
            range.material = primitive.material;
            range.node_index = primitive.mesh_node;
            range.runtime_visible = primitive.default_visible;
            if (primitive.mesh_node >= 0 && primitive.mesh_node < static_cast<int>(floor_model_.nodes.size())) {
                const AttendPokemonNode& node = floor_model_.nodes[static_cast<std::size_t>(primitive.mesh_node)];
                range.node_name = node.name;
                range.composition_priority = node.composition_priority;
            }
            floor_mesh_.ranges.push_back(std::move(range));
        }
        std::stable_sort(
            floor_mesh_.ranges.begin(),
            floor_mesh_.ranges.end(),
            [&](const MeshResource::Range& a, const MeshResource::Range& b) {
                const MaterialResource* ma = a.material >= 0 && a.material < static_cast<int>(floor_mesh_.materials.size())
                    ? &floor_mesh_.materials[static_cast<std::size_t>(a.material)] : nullptr;
                const MaterialResource* mb = b.material >= 0 && b.material < static_cast<int>(floor_mesh_.materials.size())
                    ? &floor_mesh_.materials[static_cast<std::size_t>(b.material)] : nullptr;
                const bool blend_a = ma && ma->blend;
                const bool blend_b = mb && mb->blend;
                if (blend_a != blend_b) return !blend_a;
                const int pass_a = ma ? ma->environment_pass_priority : 5;
                const int pass_b = mb ? mb->environment_pass_priority : 5;
                if (pass_a != pass_b) return pass_a < pass_b;
                if (blend_a && a.composition_priority != b.composition_priority) {
                    return a.composition_priority < b.composition_priority;
                }
                return a.start < b.start;
            });
        floor_frame_vertices_ = vertices;
        const bool uploaded = uploadMesh(floor_mesh_, vertices, indices);
        if (!uploaded) {
            floor_environment_ = false;
            floor_model_ = AttendPokemonModel{};
            floor_skinned_primitives_.clear();
            floor_frame_vertices_.clear();
        }
        return uploaded;
    }
    for (std::size_t mat = 0; mat < floor_mesh_.materials.size(); ++mat) {
        const std::uint32_t start = static_cast<std::uint32_t>(indices.size());
        for (std::size_t primitive_index = 0; primitive_index < floor_model_.primitives.size(); ++primitive_index) {
            const AttendPokemonPrimitive& primitive = floor_model_.primitives[primitive_index];
            if (primitive.material != static_cast<int>(mat)) continue;
            const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
            const std::vector<AttendPokemonVertex>& skinned = floor_skinned_primitives_[primitive_index];
            for (const AttendPokemonVertex& v : skinned) {
                vertices.push_back(transformAttendVertex(
                    v,
                    config_.floor.model_x,
                    config_.floor.model_y,
                    config_.floor.model_z,
                    config_.floor.model_yaw_degrees,
                    config_.floor.model_scale));
            }
            for (std::uint32_t index : primitive.indices) {
                indices.push_back(base + index);
            }
        }
        const std::uint32_t count = static_cast<std::uint32_t>(indices.size()) - start;
        if (count > 0) {
            floor_mesh_.ranges.push_back(MeshResource::Range{start, count, static_cast<int>(mat)});
        }
    }
    floor_frame_vertices_ = vertices;
    floor_animated_ = uploadDynamicMesh(floor_mesh_, vertices, indices);
    if (!floor_animated_) {
        floor_model_ = AttendPokemonModel{};
        floor_animation_ = nullptr;
        floor_skinned_primitives_.clear();
        floor_frame_vertices_.clear();
    }
    return floor_animated_;
}

bool AttendBgfxRenderer::Impl::buildFloor() {
    if (!config_.floor.enabled) {
        return true;
    }
    floor_environment_ = false;
    bool floor_built = buildAnimatedFloor();
    if (!floor_built) {
        floor_mesh_.destroy();
        floor_built = !config_.floor.model_path.empty() &&
            buildStaticGlbMesh(
                floor_mesh_,
                config_.floor.model_path,
                config_.floor.model_x,
                config_.floor.model_y,
                config_.floor.model_z,
                config_.floor.model_yaw_degrees,
                config_.floor.model_scale);
    }

    if (!floor_built) {
        last_error_ = "Attend environment floor model could not be loaded: " + config_.floor.model_path;
        return false;
    }

    for (MeshResource& mesh : floor_extension_meshes_) {
        mesh.destroy();
    }
    floor_extension_meshes_.clear();
    floor_extension_meshes_.reserve(config_.floor.extensions.size());
    for (const AttendFloorExtensionConfig& extension : config_.floor.extensions) {
        if (!extension.enabled || extension.model_path.empty()) continue;
        MeshResource mesh;
        if (!buildStaticGlbMesh(
                mesh,
                extension.model_path,
                extension.model_x,
                extension.model_y,
                extension.model_z,
                extension.model_yaw_degrees,
                extension.model_scale)) {
            last_error_ = "Attend environment floor extension model could not be loaded: " + extension.model_path;
            mesh.destroy();
            return false;
        }
        floor_extension_meshes_.push_back(std::move(mesh));
    }
    return true;
}

bool AttendBgfxRenderer::Impl::buildWall() {
    if (!config_.wall.enabled) return true;
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    const int segments = std::max(4, config_.wall.segments);
    const int vertical = std::max(1, config_.wall.vertical_segments);
    if (config_.wall.shape == "dome" || config_.wall.shape == "sphere") {
        const int dome_segments = std::max(24, segments);
        const int dome_vertical = std::max(12, vertical);
        const float radius = std::max(8.0f, config_.wall.radius);
        vertices.reserve(static_cast<std::size_t>((dome_segments + 1) * (dome_vertical + 1)));
        for (int y = 0; y <= dome_vertical; ++y) {
            const float vt = static_cast<float>(y) / static_cast<float>(dome_vertical);
            const float phi = -kPi * 0.5f + vt * kPi;
            const float ring_radius = std::cos(phi) * radius;
            const float py = config_.wall.bottom_y + std::sin(phi) * radius;
            Color3 color = sampleGradient(config_.wall.gradient_colors, vt);
            const float horizon = 1.0f - std::clamp(config_.wall.edge_darkening, 0.0f, 0.95f) *
                std::pow(std::max(0.0f, 0.35f - vt) / 0.35f, 2.0f) * 0.35f;
            for (int s = 0; s <= dome_segments; ++s) {
                const float st = static_cast<float>(s) / static_cast<float>(dome_segments);
                const float theta = st * kPi * 2.0f;
                float nx = -std::sin(theta) * std::cos(phi);
                float ny = -std::sin(phi);
                float nz = -std::cos(theta) * std::cos(phi);
                normalize3(nx, ny, nz);
                vertices.push_back(Vertex{
                    std::sin(theta) * ring_radius,
                    py,
                    std::cos(theta) * ring_radius,
                    nx,
                    ny,
                    nz,
                    packAbgr(color.r * horizon, color.g * horizon, color.b * horizon),
                    st,
                    vt});
            }
        }
        for (int y = 0; y < dome_vertical; ++y) {
            for (int s = 0; s < dome_segments; ++s) {
                const std::uint32_t a = static_cast<std::uint32_t>(y * (dome_segments + 1) + s);
                const std::uint32_t b = a + 1;
                const std::uint32_t c = static_cast<std::uint32_t>((y + 1) * (dome_segments + 1) + s);
                const std::uint32_t d = c + 1;
                indices.insert(indices.end(), {a, b, c, b, d, c});
            }
        }
        wall_mesh_.materials.push_back(MaterialResource{});
        wall_mesh_.ranges.push_back(MeshResource::Range{0, static_cast<std::uint32_t>(indices.size()), 0});
        return uploadMesh(wall_mesh_, vertices, indices);
    }
    const float half_arc = config_.wall.arc_degrees * 0.5f * (kPi / 180.0f);
    vertices.reserve(static_cast<std::size_t>((segments + 1) * (vertical + 1)));
    for (int y = 0; y <= vertical; ++y) {
        const float vt = static_cast<float>(y) / static_cast<float>(vertical);
        Color3 color = sampleGradient(config_.wall.gradient_colors, vt);
        for (int s = 0; s <= segments; ++s) {
            const float st = static_cast<float>(s) / static_cast<float>(segments);
            const float a = -half_arc + st * half_arc * 2.0f;
            const float edge = std::abs(st - 0.5f) * 2.0f;
            const float shade = 1.0f - config_.wall.edge_darkening * edge * edge;
            float nx = -std::sin(a);
            float ny = 0.0f;
            float nz = std::cos(a);
            normalize3(nx, ny, nz);
            vertices.push_back(Vertex{
                std::sin(a) * config_.wall.radius,
                config_.wall.bottom_y + vt * config_.wall.height,
                -config_.wall.distance - std::cos(a) * config_.wall.radius + config_.wall.radius,
                nx,
                ny,
                nz,
                packAbgr(color.r * shade, color.g * shade, color.b * shade),
                st,
                vt});
        }
    }
    for (int y = 0; y < vertical; ++y) {
        for (int s = 0; s < segments; ++s) {
            const std::uint32_t a = static_cast<std::uint32_t>(y * (segments + 1) + s);
            const std::uint32_t b = a + 1;
            const std::uint32_t c = static_cast<std::uint32_t>((y + 1) * (segments + 1) + s);
            const std::uint32_t d = c + 1;
            indices.insert(indices.end(), {a, c, b, b, c, d});
        }
    }
    wall_mesh_.materials.push_back(MaterialResource{});
    wall_mesh_.ranges.push_back(MeshResource::Range{0, static_cast<std::uint32_t>(indices.size()), 0});
    return uploadMesh(wall_mesh_, vertices, indices);
}

AttendBgfxRenderer::Impl::TextureResource AttendBgfxRenderer::Impl::decodeTexture(
    const std::vector<std::uint8_t>& bytes,
    const char* debug_name) {
    TextureResource out;
    if (bytes.empty()) return out;
    SDL_RWops* rw = SDL_RWFromConstMem(bytes.data(), static_cast<int>(bytes.size()));
    if (!rw) return out;
    SDL_Surface* loaded = IMG_Load_RW(rw, 1);
    if (!loaded) return out;
    SDL_Surface* rgba = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(loaded);
    if (!rgba) return out;
    out.width = rgba->w;
    out.height = rgba->h;
    const auto* pixels = static_cast<const std::uint8_t*>(rgba->pixels);
    int partial_alpha_pixels = 0;
    for (int i = 0; i < rgba->w * rgba->h; ++i) {
        const std::uint8_t alpha = pixels[i * 4 + 3];
        out.minimum_alpha = std::min(out.minimum_alpha, alpha);
        if (alpha == 0) out.has_zero_alpha = true;
        if (alpha > 0 && alpha < 255) {
            out.has_partial_alpha = true;
            ++partial_alpha_pixels;
        }
    }
    const int pixel_count = rgba->w * rgba->h;
    out.partial_alpha_fraction = pixel_count > 0
        ? static_cast<float>(partial_alpha_pixels) / static_cast<float>(pixel_count)
        : 0.0f;
    const bgfx::Memory* mem = bgfx::copy(rgba->pixels, static_cast<std::uint32_t>(rgba->w * rgba->h * 4));
    out.handle = bgfx::createTexture2D(
        static_cast<std::uint16_t>(rgba->w),
        static_cast<std::uint16_t>(rgba->h),
        false,
        1,
        bgfx::TextureFormat::RGBA8,
        samplerFlags(),
        mem);
    if (out.valid()) bgfx::setName(out.handle, debug_name);
    SDL_FreeSurface(rgba);
    return out;
}

AttendBgfxRenderer::Impl::TextureResource AttendBgfxRenderer::Impl::buildPokemonEyeTexture(
    const std::vector<std::uint8_t>& base_bytes,
    const std::vector<std::uint8_t>& lym_bytes,
    const char* debug_name) {
    TextureResource out;
    if (base_bytes.empty() || lym_bytes.empty()) return out;

    SDL_RWops* base_rw = SDL_RWFromConstMem(base_bytes.data(), static_cast<int>(base_bytes.size()));
    SDL_RWops* lym_rw = SDL_RWFromConstMem(lym_bytes.data(), static_cast<int>(lym_bytes.size()));
    if (!base_rw || !lym_rw) {
        if (base_rw) SDL_RWclose(base_rw);
        if (lym_rw) SDL_RWclose(lym_rw);
        return out;
    }
    SDL_Surface* loaded_base = IMG_Load_RW(base_rw, 1);
    SDL_Surface* loaded_lym = IMG_Load_RW(lym_rw, 1);
    if (!loaded_base || !loaded_lym) {
        if (loaded_base) SDL_FreeSurface(loaded_base);
        if (loaded_lym) SDL_FreeSurface(loaded_lym);
        return out;
    }
    SDL_Surface* base = SDL_ConvertSurfaceFormat(loaded_base, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_Surface* lym = SDL_ConvertSurfaceFormat(loaded_lym, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(loaded_base);
    SDL_FreeSurface(loaded_lym);
    if (!base || !lym) {
        if (base) SDL_FreeSurface(base);
        if (lym) SDL_FreeSurface(lym);
        return out;
    }

    const auto* base_pixels = static_cast<const std::uint8_t*>(base->pixels);
    const auto* lym_pixels = static_cast<const std::uint8_t*>(lym->pixels);
    int base_alpha_pixels = 0;
    int base_nonwhite_pixels = 0;
    int base_zero_alpha_pixels = 0;
    int base_partial_alpha_pixels = 0;
    for (int i = 0; i < base->w * base->h; ++i) {
        const std::size_t src = static_cast<std::size_t>(i * 4);
        const std::uint8_t r = base_pixels[src + 0];
        const std::uint8_t g = base_pixels[src + 1];
        const std::uint8_t b = base_pixels[src + 2];
        const std::uint8_t a = base_pixels[src + 3];
        if (a == 0) {
            ++base_zero_alpha_pixels;
            continue;
        }
        if (a < 255) ++base_partial_alpha_pixels;
        ++base_alpha_pixels;
        if (r < 245 || g < 245 || b < 245) ++base_nonwhite_pixels;
    }
    const float base_nonwhite_ratio = base_alpha_pixels > 0
        ? static_cast<float>(base_nonwhite_pixels) / static_cast<float>(base_alpha_pixels)
        : 0.0f;
    if (base_nonwhite_ratio > 0.05f) {
        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(base->w * base->h * 4));
        std::copy(base_pixels, base_pixels + rgba.size(), rgba.begin());
        const bgfx::Memory* mem = bgfx::copy(rgba.data(), static_cast<std::uint32_t>(rgba.size()));
        out.handle = bgfx::createTexture2D(
            static_cast<std::uint16_t>(base->w),
            static_cast<std::uint16_t>(base->h),
            false,
            1,
            bgfx::TextureFormat::RGBA8,
            samplerFlags(),
            mem);
        out.has_zero_alpha = base_zero_alpha_pixels > 0;
        out.has_partial_alpha = base_partial_alpha_pixels > 0;
        if (out.valid()) bgfx::setName(out.handle, debug_name);
        SDL_FreeSurface(base);
        SDL_FreeSurface(lym);
        return out;
    }

    int mask_min_x = lym->w;
    int mask_min_y = lym->h;
    int mask_max_x = -1;
    int mask_max_y = -1;
    for (int y = 0; y < lym->h; ++y) {
        for (int x = 0; x < lym->w; ++x) {
            const std::size_t src = static_cast<std::size_t>((y * lym->w + x) * 4);
            if (lym_pixels[src + 3] > 0) {
                mask_min_x = std::min(mask_min_x, x);
                mask_min_y = std::min(mask_min_y, y);
                mask_max_x = std::max(mask_max_x, x);
                mask_max_y = std::max(mask_max_y, y);
            }
        }
    }
    const bool has_mask = mask_max_x >= mask_min_x && mask_max_y >= mask_min_y;
    const float mask_center_x = has_mask ? (static_cast<float>(mask_min_x + mask_max_x) * 0.5f) : 0.0f;
    const float mask_center_y = has_mask ? (static_cast<float>(mask_min_y + mask_max_y) * 0.5f) : 0.0f;
    const float mask_radius_x = has_mask ? std::max(3.5f, static_cast<float>(mask_max_x - mask_min_x + 1) * 0.62f) : 1.0f;
    const float mask_radius_y = has_mask ? std::max(3.5f, static_cast<float>(mask_max_y - mask_min_y + 1) * 0.70f) : 1.0f;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(lym->w * lym->h * 4));
    for (int y = 0; y < lym->h; ++y) {
        for (int x = 0; x < lym->w; ++x) {
            const std::size_t src = static_cast<std::size_t>((y * lym->w + x) * 4);
            const int base_x = std::clamp(
                static_cast<int>((static_cast<float>(x) / static_cast<float>(std::max(1, lym->w - 1))) * static_cast<float>(base->w - 1) + 0.5f),
                0,
                std::max(0, base->w - 1));
            const int base_y = std::clamp(
                static_cast<int>((static_cast<float>(y) / static_cast<float>(std::max(1, lym->h - 1))) * static_cast<float>(base->h - 1) + 0.5f),
                0,
                std::max(0, base->h - 1));
            const std::size_t base_src = static_cast<std::size_t>((base_y * base->w + base_x) * 4);
            const float dx = (static_cast<float>(x) - mask_center_x) / mask_radius_x;
            const float dy = (static_cast<float>(y) - mask_center_y) / mask_radius_y;
            const bool pupil = has_mask && (dx * dx + dy * dy) <= 1.0f;
            rgba[src + 0] = pupil ? 22 : base_pixels[base_src + 0];
            rgba[src + 1] = pupil ? 14 : base_pixels[base_src + 1];
            rgba[src + 2] = pupil ? 9 : base_pixels[base_src + 2];
            rgba[src + 3] = 255;
        }
    }

    const bgfx::Memory* mem = bgfx::copy(rgba.data(), static_cast<std::uint32_t>(rgba.size()));
    out.handle = bgfx::createTexture2D(
        static_cast<std::uint16_t>(lym->w),
        static_cast<std::uint16_t>(lym->h),
        false,
        1,
        bgfx::TextureFormat::RGBA8,
        samplerFlags(),
        mem);
    if (out.valid()) bgfx::setName(out.handle, debug_name);
    SDL_FreeSurface(base);
    SDL_FreeSurface(lym);
    return out;
}

} // namespace pr::gameplay::attend::rendering
