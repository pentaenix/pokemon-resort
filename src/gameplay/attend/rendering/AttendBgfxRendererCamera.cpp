#include "AttendBgfxRendererInternal.hpp"

#include "gameplay/attend/rendering/AttendPokemonMaterialPolicy.hpp"

namespace pr::gameplay::attend::rendering {

void AttendBgfxRenderer::Impl::updateFloorAnimation(double scene_time_seconds) {
    if (!floor_animated_ || !floor_mesh_.dynamic || !bgfx::isValid(floor_mesh_.dvbh) ||
        floor_model_.primitives.empty() || !floor_animation_) {
        return;
    }
    const std::vector<std::array<float, 16>> globals =
        buildAttendPokemonGlobals(floor_model_, floor_animation_, scene_time_seconds);
    const std::vector<std::vector<std::array<float, 16>>> skin_matrices =
        buildAttendPokemonSkinMatrices(floor_model_, globals);
    for (std::size_t primitive_index = 0; primitive_index < floor_model_.primitives.size(); ++primitive_index) {
        skinAttendPokemonPrimitiveWithPose(
            floor_model_,
            floor_model_.primitives[primitive_index],
            globals,
            skin_matrices,
            floor_skinned_primitives_[primitive_index]);
    }

    floor_frame_vertices_.clear();
    floor_frame_vertices_.reserve(floor_mesh_.vertex_count);
    for (std::size_t mat = 0; mat < floor_mesh_.materials.size(); ++mat) {
        for (std::size_t primitive_index = 0; primitive_index < floor_model_.primitives.size(); ++primitive_index) {
            const AttendPokemonPrimitive& primitive = floor_model_.primitives[primitive_index];
            if (primitive.material != static_cast<int>(mat)) continue;
            for (const AttendPokemonVertex& v : floor_skinned_primitives_[primitive_index]) {
                floor_frame_vertices_.push_back(transformAttendVertex(
                    v,
                    config_.floor.model_x,
                    config_.floor.model_y,
                    config_.floor.model_z,
                    config_.floor.model_yaw_degrees,
                    config_.floor.model_scale));
            }
        }
    }
    if (floor_frame_vertices_.size() != floor_mesh_.vertex_count) return;
    const bgfx::Memory* vb_mem = bgfx::copy(
        floor_frame_vertices_.data(),
        static_cast<std::uint32_t>(floor_frame_vertices_.size() * sizeof(Vertex)));
    bgfx::update(floor_mesh_.dvbh, 0, vb_mem);
}

void AttendBgfxRenderer::Impl::scheduleNextRandomBlink(double scene_time_seconds) {
    std::uniform_real_distribution<float> dist(
        config_.interaction_adapter.spontaneous_blink_min_seconds,
        config_.interaction_adapter.spontaneous_blink_max_seconds);
    next_random_blink_seconds_ = scene_time_seconds + static_cast<double>(dist(blink_rng_));
    random_blink_started_seconds_ = -1.0;
    random_blink_amount_ = 0.0f;
}

void AttendBgfxRenderer::Impl::updateRandomBlink(double scene_time_seconds) {
    if (!pokemon_eye_close_animation_ && !has_eye_expression_frames_) {
        random_blink_amount_ = 0.0f;
        return;
    }
    if (petting_) {
        random_blink_amount_ = 0.0f;
        if (next_random_blink_seconds_ < scene_time_seconds + 1.5) {
            next_random_blink_seconds_ = scene_time_seconds + 1.5;
        }
        return;
    }
    if (next_random_blink_seconds_ < 0.0) {
        scheduleNextRandomBlink(scene_time_seconds);
        return;
    }
    if (random_blink_started_seconds_ < 0.0 && scene_time_seconds >= next_random_blink_seconds_) {
        random_blink_started_seconds_ = scene_time_seconds;
    }
    if (random_blink_started_seconds_ < 0.0) {
        random_blink_amount_ = 0.0f;
        return;
    }

    const float duration = std::max(0.05f, config_.interaction_adapter.spontaneous_blink_duration_seconds);
    const float t = std::clamp(
        static_cast<float>((scene_time_seconds - random_blink_started_seconds_) / static_cast<double>(duration)),
        0.0f,
        1.0f);
    random_blink_amount_ = std::sin(t * kPi);
    if (t >= 1.0f) {
        scheduleNextRandomBlink(scene_time_seconds);
    }
}

void AttendBgfxRenderer::Impl::updatePetControls(double scene_time_seconds) {
    if (last_pet_update_seconds_ < 0.0) {
        last_pet_update_seconds_ = scene_time_seconds;
        pet_started_seconds_ = petting_ ? scene_time_seconds : -1.0;
        pet_eye_close_amount_ = 0.0f;
        was_petting_ = petting_;
        return;
    }
    const float dt = std::clamp(static_cast<float>(scene_time_seconds - last_pet_update_seconds_), 0.0f, 0.1f);
    last_pet_update_seconds_ = scene_time_seconds;

    if (petting_ && !was_petting_) {
        pet_started_seconds_ = scene_time_seconds;
    } else if (!petting_ && was_petting_) {
        pet_started_seconds_ = -1.0;
        pet_eye_close_cooldown_until_seconds_ =
            scene_time_seconds + static_cast<double>(config_.interaction_adapter.pet_eye_close_cooldown_seconds);
    }
    was_petting_ = petting_;

    const bool delay_elapsed = petting_ && pet_started_seconds_ >= 0.0 &&
        scene_time_seconds >= pet_started_seconds_ +
            static_cast<double>(config_.interaction_adapter.pet_eye_close_delay_seconds);
    const bool cooldown_elapsed = scene_time_seconds >= pet_eye_close_cooldown_until_seconds_;
    const float target = (delay_elapsed && cooldown_elapsed) ? 1.0f : 0.0f;
    const float speed = target > pet_eye_close_amount_ ? 4.6f : 6.5f;
    if (pet_eye_close_amount_ < target) {
        pet_eye_close_amount_ = std::min(target, pet_eye_close_amount_ + dt * speed);
    } else {
        pet_eye_close_amount_ = std::max(target, pet_eye_close_amount_ - dt * speed);
    }
    const float contact_target = petting_ ? pet_contact_target_ : 0.0f;
    const float contact_follow = std::clamp(dt * 7.0f, 0.0f, 1.0f);
    pet_contact_bias_ += (contact_target - pet_contact_bias_) * contact_follow;
}

void AttendBgfxRenderer::Impl::updateFaceControls(double scene_time_seconds) {
    if (last_face_update_seconds_ < 0.0) {
        last_face_update_seconds_ = scene_time_seconds;
        face_look_x_ = face_target_x_;
        face_look_y_ = face_target_y_;
        return;
    }
    const float dt = std::clamp(static_cast<float>(scene_time_seconds - last_face_update_seconds_), 0.0f, 0.1f);
    last_face_update_seconds_ = scene_time_seconds;
    const float follow = std::clamp(dt * 5.0f, 0.0f, 1.0f);
    face_look_x_ += (face_target_x_ - face_look_x_) * follow;
    face_look_y_ += (face_target_y_ - face_look_y_) * follow;
}

void AttendBgfxRenderer::Impl::updateViewportLook(double scene_time_seconds) {
    if (last_viewport_look_update_seconds_ < 0.0) {
        last_viewport_look_update_seconds_ = scene_time_seconds;
        viewport_look_x_ = viewport_look_target_x_;
        viewport_look_y_ = viewport_look_target_y_;
        return;
    }
    const float dt = std::clamp(static_cast<float>(scene_time_seconds - last_viewport_look_update_seconds_), 0.0f, 0.1f);
    last_viewport_look_update_seconds_ = scene_time_seconds;
    const float seconds = std::max(0.01f, config_.viewport_look.smooth_seconds);
    const float follow = 1.0f - std::exp(-dt / seconds);
    viewport_look_x_ += (viewport_look_target_x_ - viewport_look_x_) * follow;
    viewport_look_y_ += (viewport_look_target_y_ - viewport_look_y_) * follow;
}

void AttendBgfxRenderer::Impl::updateFaceViewTransition(double scene_time_seconds) {
    if (last_face_view_transition_update_seconds_ < 0.0) {
        last_face_view_transition_update_seconds_ = scene_time_seconds;
        face_view_blend_ = face_view_ ? 1.0f : 0.0f;
        return;
    }
    const float dt = std::clamp(
        static_cast<float>(scene_time_seconds - last_face_view_transition_update_seconds_),
        0.0f,
        0.1f);
    last_face_view_transition_update_seconds_ = scene_time_seconds;
    const float target = face_view_ ? 1.0f : 0.0f;
    constexpr float kTransitionSeconds = 0.42f;
    const float follow = 1.0f - std::exp(-dt / kTransitionSeconds);
    face_view_blend_ += (target - face_view_blend_) * follow;
    if (std::abs(face_view_blend_ - target) < 0.002f) {
        face_view_blend_ = target;
    }
}

void AttendBgfxRenderer::Impl::updatePokemonBoundsFromVertices(const std::vector<Vertex>& vertices) {
    if (vertices.empty()) {
        pokemon_bounds_valid_ = false;
        pokemon_world_height_ = 0.0f;
        return;
    }
    bool any = false;
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        if (i < pokemon_frame_vertex_primitives_.size() &&
            !pokemonPrimitiveVisibleForHit(pokemon_frame_vertex_primitives_[i])) {
            continue;
        }
        const Vertex& vertex = vertices[i];
        if (!any) {
            pokemon_min_x_ = pokemon_max_x_ = vertex.x;
            pokemon_min_y_ = pokemon_max_y_ = vertex.y;
            pokemon_min_z_ = pokemon_max_z_ = vertex.z;
            any = true;
            continue;
        }
        pokemon_min_x_ = std::min(pokemon_min_x_, vertex.x);
        pokemon_min_y_ = std::min(pokemon_min_y_, vertex.y);
        pokemon_min_z_ = std::min(pokemon_min_z_, vertex.z);
        pokemon_max_x_ = std::max(pokemon_max_x_, vertex.x);
        pokemon_max_y_ = std::max(pokemon_max_y_, vertex.y);
        pokemon_max_z_ = std::max(pokemon_max_z_, vertex.z);
    }
    if (!any) {
        pokemon_bounds_valid_ = false;
        pokemon_world_height_ = 0.0f;
        return;
    }
    pokemon_bounds_valid_ = true;
    pokemon_world_height_ = std::max(0.0f, (pokemon_max_y_ - pokemon_min_y_) * config_.pokemon.scale);
}

void AttendBgfxRenderer::Impl::captureCameraBoundsFromCurrentPose() {
    if (!pokemon_bounds_valid_) {
        camera_bounds_valid_ = false;
        camera_world_height_ = 0.0f;
        return;
    }
    camera_bounds_valid_ = true;
    camera_world_height_ = pokemon_world_height_;
    camera_min_x_ = pokemon_min_x_;
    camera_min_y_ = pokemon_min_y_;
    camera_min_z_ = pokemon_min_z_;
    camera_max_x_ = pokemon_max_x_;
    camera_max_y_ = pokemon_max_y_;
    camera_max_z_ = pokemon_max_z_;
}

void AttendBgfxRenderer::Impl::cameraForFrame(float look_x, float look_y, bx::Vec3& eye, bx::Vec3& at) const {
    if (freecam_enabled_) {
        const float yaw = freecam_pose_.yaw_degrees * (kPi / 180.0f);
        const float pitch = freecam_pose_.pitch_degrees * (kPi / 180.0f);
        const bx::Vec3 forward{
            std::sin(yaw) * std::cos(pitch),
            std::sin(pitch),
            std::cos(yaw) * std::cos(pitch)};
        eye = bx::Vec3{freecam_pose_.x, freecam_pose_.y, freecam_pose_.z};
        at = bx::Vec3{
            freecam_pose_.x + forward.x,
            freecam_pose_.y + forward.y,
            freecam_pose_.z + forward.z};
        return;
    }
    float target_x = config_.camera.target_x;
    float target_y = config_.camera.target_height;
    float target_z = config_.camera.target_z;
    float distance = config_.camera.distance;
    float height = config_.camera.height;

    if (config_.camera.auto_focus && camera_bounds_valid_) {
        const float scaled_min_y = config_.pokemon.y + camera_min_y_ * config_.pokemon.scale;
        const float scaled_max_y = config_.pokemon.y + camera_max_y_ * config_.pokemon.scale;
        const float model_height = std::max(0.2f, scaled_max_y - scaled_min_y);
        const float model_width = std::max(0.2f, (camera_max_x_ - camera_min_x_) * config_.pokemon.scale);
        const float model_depth = std::max(0.2f, (camera_max_z_ - camera_min_z_) * config_.pokemon.scale);
        const float fov_y = std::max(1.0f, config_.camera.fov_y_degrees) * (kPi / 180.0f);
        const float aspect = 16.0f / 9.0f;
        const float fov_x = 2.0f * std::atan(std::tan(fov_y * 0.5f) * aspect);
        const float face_blend = std::clamp(face_view_blend_, 0.0f, 1.0f);
        const float eased_face_blend = face_blend * face_blend * (3.0f - 2.0f * face_blend);
        const auto mix = [eased_face_blend](float full, float face) {
            return full + (face - full) * eased_face_blend;
        };
        float full_target_y_ratio = config_.camera.target_y_ratio;
        float full_height_offset = config_.camera.height_offset;
        float face_target_y_ratio = config_.camera.face_target_y_ratio;
        float face_height_offset = config_.camera.face_height_offset;
        if (form_variant_index_ >= 0 &&
            form_variant_index_ < static_cast<int>(pokemon_model_.form_variants.size())) {
            const std::string& form_id = pokemon_model_.form_variants[static_cast<std::size_t>(form_variant_index_)].id;
            if (const auto it = config_.camera.form_framing_adjustments.find(form_id);
                it != config_.camera.form_framing_adjustments.end()) {
                full_target_y_ratio = std::clamp(full_target_y_ratio + it->second.target_y_ratio_offset, 0.0f, 1.0f);
                face_target_y_ratio = std::clamp(face_target_y_ratio + it->second.target_y_ratio_offset, 0.0f, 1.0f);
                full_height_offset += it->second.height_offset;
                face_height_offset += it->second.height_offset;
            }
        }
        const float desired_ratio = std::clamp(
            mix(config_.camera.screen_height_ratio, config_.camera.face_screen_height_ratio),
            0.15f,
            0.98f);
        const float distance_scale = mix(config_.camera.distance_scale, config_.camera.face_distance_scale);
        const float target_y_ratio = mix(full_target_y_ratio, face_target_y_ratio);
        const float height_offset = mix(full_height_offset, face_height_offset);
        const float height_offset_limit = model_height * mix(0.52f, 0.08f);
        const float desired_distance =
            (model_height / (2.0f * std::tan(fov_y * 0.5f) * desired_ratio)) *
            distance_scale;
        const float width_distance =
            (model_width / (2.0f * std::tan(fov_x * 0.5f) * std::max(0.35f, desired_ratio * 1.12f))) *
            distance_scale;
        const float depth_padding = model_depth *
            mix(config_.camera.depth_padding_scale, config_.camera.face_depth_padding_scale);
        target_x = config_.pokemon.x;
        target_y = scaled_min_y + model_height * target_y_ratio;
        target_z = config_.pokemon.z;
        distance = std::clamp(
            std::max(desired_distance, width_distance) + depth_padding,
            config_.camera.min_distance,
            config_.camera.max_distance);
        height = target_y + std::min(height_offset, height_offset_limit);
    }

    eye = bx::Vec3{
        target_x,
        height,
        target_z + distance};
    at = bx::Vec3{
        target_x + look_x,
        target_y + look_y,
        target_z};
}

void AttendBgfxRenderer::Impl::updatePokemonPointerRect(
    const float* pokemon_matrix,
    const float* view,
    const float* proj,
    int width,
    int height) {
    last_pokemon_pointer_rect_ = SDL_Rect{0, 0, 0, 0};
    if (pokemon_frame_vertices_.empty() || width <= 0 || height <= 0) return;

    const auto transform = [](const float* m, float x, float y, float z, float& ox, float& oy, float& oz, float& ow) {
        ox = x * m[0] + y * m[4] + z * m[8] + m[12];
        oy = x * m[1] + y * m[5] + z * m[9] + m[13];
        oz = x * m[2] + y * m[6] + z * m[10] + m[14];
        ow = x * m[3] + y * m[7] + z * m[11] + m[15];
    };

    float min_x = 1.0f;
    float min_y = 1.0f;
    float max_x = 0.0f;
    float max_y = 0.0f;
    bool any = false;
    for (std::size_t i = 0; i < pokemon_frame_vertices_.size(); ++i) {
        if (i < pokemon_frame_vertex_primitives_.size() &&
            !pokemonPrimitiveVisibleForHit(pokemon_frame_vertex_primitives_[i])) {
            continue;
        }
        const Vertex& vertex = pokemon_frame_vertices_[i];
        float wx = 0.0f;
        float wy = 0.0f;
        float wz = 0.0f;
        float ww = 1.0f;
        transform(pokemon_matrix, vertex.x, vertex.y, vertex.z, wx, wy, wz, ww);
        float vx = 0.0f;
        float vy = 0.0f;
        float vz = 0.0f;
        float vw = 1.0f;
        transform(view, wx, wy, wz, vx, vy, vz, vw);
        float cx = 0.0f;
        float cy = 0.0f;
        float cz = 0.0f;
        float cw = 1.0f;
        transform(proj, vx, vy, vz, cx, cy, cz, cw);
        if (std::abs(cw) < 0.0001f) continue;
        const float ndc_x = cx / cw;
        const float ndc_y = cy / cw;
        if (!std::isfinite(ndc_x) || !std::isfinite(ndc_y)) continue;
        const float sx = ndc_x * 0.5f + 0.5f;
        const float sy = 0.5f - ndc_y * 0.5f;
        min_x = std::min(min_x, sx);
        min_y = std::min(min_y, sy);
        max_x = std::max(max_x, sx);
        max_y = std::max(max_y, sy);
        any = true;
    }
    if (!any) return;
    min_x = std::clamp(min_x, 0.0f, 1.0f);
    max_x = std::clamp(max_x, 0.0f, 1.0f);
    min_y = std::clamp(min_y, 0.0f, 1.0f);
    max_y = std::clamp(max_y, 0.0f, 1.0f);
    if (max_x <= min_x || max_y <= min_y) return;

    const float sx = static_cast<float>(std::max(1, overlay_logical_w_)) / static_cast<float>(std::max(1, width));
    const float sy = static_cast<float>(std::max(1, overlay_logical_h_)) / static_cast<float>(std::max(1, height));
    const int x = static_cast<int>(std::floor(min_x * static_cast<float>(width) * sx));
    const int y = static_cast<int>(std::floor(min_y * static_cast<float>(height) * sy));
    const int w = static_cast<int>(std::ceil((max_x - min_x) * static_cast<float>(width) * sx));
    const int h = static_cast<int>(std::ceil((max_y - min_y) * static_cast<float>(height) * sy));
    const int pad_x = std::clamp(w / 24, 4, 14);
    const int pad_y = std::clamp(h / 24, 4, 14);
    last_pokemon_pointer_rect_ = SDL_Rect{
        std::max(0, x - pad_x),
        std::max(0, y - pad_y),
        std::min(std::max(1, overlay_logical_w_) - std::max(0, x - pad_x), w + pad_x * 2),
        std::min(std::max(1, overlay_logical_h_) - std::max(0, y - pad_y), h + pad_y * 2)};
}

bool AttendBgfxRenderer::Impl::floorMaterialVisible(const MaterialResource* material) const {
    if (!material || !material->visible) return material == nullptr || (material && material->visible);
    if (config_.floor.weather_material_substrings.empty() ||
        !containsAnySubstring(material->name, config_.floor.weather_material_substrings)) {
        return true;
    }
    if (floor_weather_index_ < 0 ||
        floor_weather_index_ >= static_cast<int>(config_.floor.weather_modes.size())) {
        return false;
    }
    const WeatherModeConfig& mode = config_.floor.weather_modes[static_cast<std::size_t>(floor_weather_index_)];
    return containsAnySubstring(material->name, mode.visible_material_substrings);
}

bool AttendBgfxRenderer::Impl::pokemonPrimitiveVisibleForHit(std::size_t primitive_index) const {
    if (primitive_index >= pokemon_model_.primitives.size()) return false;
    const AttendPokemonPrimitive& primitive = pokemon_model_.primitives[primitive_index];
    if (!primitive.visible_for_forms.empty()) {
        const std::string form_id =
            form_variant_index_ >= 0 && form_variant_index_ < static_cast<int>(pokemon_model_.form_variants.size())
                ? pokemon_model_.form_variants[static_cast<std::size_t>(form_variant_index_)].id
                : std::string{};
        if (form_id.empty() || !stringListContains(primitive.visible_for_forms, form_id)) {
            return false;
        }
    }
    if (primitive.material >= 0 && primitive.material < static_cast<int>(pokemon_mesh_.materials.size())) {
        const MaterialResource& material = pokemon_mesh_.materials[static_cast<std::size_t>(primitive.material)];
        if (!material.visible) return false;
        if (material.separate_eye_iris &&
            !shouldRenderAttendSeparateEyeIris(
                current_eye_expression_frame_,
                normal_eye_expression_frame_)) {
            return false;
        }
    }
    return true;
}

} // namespace pr::gameplay::attend::rendering
