#include "AttendBgfxRendererInternal.hpp"

namespace pr::gameplay::attend::rendering {

void AttendBgfxRenderer::Impl::updatePokemonAnimation(double scene_time_seconds) {
    if (!pokemon_mesh_.dynamic || !bgfx::isValid(pokemon_mesh_.dvbh) || pokemon_model_.primitives.empty()) return;
    startPendingSemanticAnimation(scene_time_seconds);
    startPendingWakeFromSleep(scene_time_seconds);
    startPendingReaction(scene_time_seconds);
    if (reaction_active_ && scene_time_seconds >= reaction_start_seconds_ + reaction_duration_seconds_) {
        reaction_active_ = false;
        reaction_animation_ = nullptr;
        reaction_from_semantic_ = false;
        reaction_reverse_ = false;
        semantic_sleeping_ = false;
        if (pending_sleep_loop_after_start_) {
            startPendingSleepLoop(scene_time_seconds);
        }
    }
    const float look_distance = std::sqrt(face_look_x_ * face_look_x_ + face_look_y_ * face_look_y_);
    const float look_weight = std::clamp((look_distance - 0.10f) / 0.90f, 0.0f, 1.0f);
    const float head_look_strength = std::max(0.0f, config_.interaction_adapter.head_look_strength);
    const float pet_contact_pitch_bias = pet_contact_bias_ >= 0.0f
        ? pet_contact_bias_ * 8.5f
        : pet_contact_bias_ * 5.0f;
    const float head_pitch_degrees = (-face_look_y_ * 5.5f * head_look_strength) + pet_contact_pitch_bias;
    const bool asleep = semantic_sleeping_ && reaction_active_ && reaction_from_semantic_;
    const float head_weight = asleep
        ? 0.0f
        : std::clamp(std::max(look_weight, std::abs(pet_contact_bias_) * 0.9f), 0.0f, 1.0f);
    const float eye_close_amount = std::max(pet_eye_close_amount_, random_blink_amount_);
    const bool reaction_eye_visible = reaction_active_ || scene_time_seconds < reaction_eye_linger_until_seconds_;
    current_eye_expression_frame_ = reaction_eye_visible
        ? reaction_eye_expression_frame_
        : (eye_close_amount > 0.38f ? closed_eye_expression_frame_ : normal_eye_expression_frame_);
    current_mouth_expression_frame_ = reaction_active_
        ? reaction_mouth_expression_frame_
        : normal_mouth_expression_frame_;
    const AttendPokemonAnimation* eyelid_animation =
        reaction_eye_visible ? nullptr : (pokemon_eye_close_animation_ ? pokemon_eye_close_animation_ : nullptr);
    const float reaction_weight = reactionWeight(scene_time_seconds);
    const AttendInteractionAdapterConfig::ReactionCombo* pet_happy_combo = reactionCombo("pet_happy");
    const float ready_weight = (!reaction_active_ && petting_ && pet_happy_combo)
        ? petReadyCueWeight(scene_time_seconds, *pet_happy_combo)
        : 0.0f;
    const AttendPokemonAnimation* ready_animation = ready_weight > 0.0f && pet_happy_combo
        ? resolveSemanticAnimation(pet_happy_combo->ready_animation_semantic)
        : nullptr;
    const bool using_reaction_overlay = reaction_active_ && reaction_animation_;
    const bool using_ready_overlay = !using_reaction_overlay && ready_animation && ready_weight > 0.0f;
    if (!reaction_active_ && ready_weight > 0.0f && pet_happy_combo) {
        current_mouth_expression_frame_ =
            mouthExpressionFrame(pet_happy_combo->ready_mouth_expression, normal_mouth_expression_frame_);
    }
    const AttendPokemonPoseOverlay eye_close_overlay{
        using_reaction_overlay ? reaction_animation_ : (using_ready_overlay ? ready_animation : eyelid_animation),
        using_reaction_overlay
            ? scene_time_seconds - reaction_start_seconds_
            : (using_ready_overlay && pet_started_seconds_ >= 0.0
                ? scene_time_seconds - pet_started_seconds_
                : config_.interaction_adapter.eye_close_time_seconds),
        using_reaction_overlay ? reaction_weight : (using_ready_overlay ? ready_weight : (reaction_eye_visible ? 0.0f : eye_close_amount)),
        using_reaction_overlay && reaction_reverse_,
        !(using_reaction_overlay || using_ready_overlay),
        using_reaction_overlay || using_ready_overlay ? std::vector<std::string>{} : config_.interaction_adapter.eyelid_node_substrings,
        -face_look_x_ * 8.0f * head_look_strength,
        head_pitch_degrees,
        head_weight,
        config_.interaction_adapter.head_node_names,
        {
            config_.interaction_adapter.head_yaw_axis[0],
            config_.interaction_adapter.head_yaw_axis[1],
            config_.interaction_adapter.head_yaw_axis[2]},
        {
            config_.interaction_adapter.head_pitch_axis[0],
            config_.interaction_adapter.head_pitch_axis[1],
            config_.interaction_adapter.head_pitch_axis[2]}};
    const std::vector<std::array<float, 16>> globals =
        buildAttendPokemonGlobals(
            pokemon_model_,
            pokemon_animation_,
            scene_time_seconds,
            attendPokemonAnimationLoopDurationForForm(pokemon_model_, pokemon_animation_, form_variant_index_),
            eye_close_overlay);
    const std::vector<std::vector<std::array<float, 16>>> skin_matrices =
        buildAttendPokemonSkinMatrices(pokemon_model_, globals);
    if (pokemon_frame_vertices_.size() != pokemon_mesh_.vertex_count ||
        pokemon_primitive_vertex_offsets_.size() != pokemon_model_.primitives.size()) {
        return;
    }
    bool updated_any = false;
    for (std::size_t primitive_index : pokemon_draw_order_) {
        if (primitive_index >= pokemon_model_.primitives.size()) continue;
        if (!pokemonPrimitiveVisibleForHit(primitive_index)) continue;
        const AttendPokemonPrimitive& primitive = pokemon_model_.primitives[primitive_index];
        if (primitive_index >= pokemon_primitive_vertex_offsets_.size()) continue;
        const std::uint32_t base = pokemon_primitive_vertex_offsets_[primitive_index];
        if (base == static_cast<std::uint32_t>(-1) ||
            static_cast<std::size_t>(base) + primitive.vertices.size() > pokemon_frame_vertices_.size()) {
            continue;
        }
        skinAttendPokemonPrimitiveWithPose(
            pokemon_model_,
            primitive,
            globals,
            skin_matrices,
            skinned_primitives_[primitive_index]);
        const AttendPokemonMaterial* material =
            primitive.material >= 0 && primitive.material < static_cast<int>(pokemon_model_.materials.size())
                ? &pokemon_model_.materials[static_cast<std::size_t>(primitive.material)]
                : nullptr;
        const std::vector<AttendPokemonVertex>& skinned = skinned_primitives_[primitive_index];
        for (std::size_t i = 0; i < skinned.size(); ++i) {
            pokemon_frame_vertices_[static_cast<std::size_t>(base) + i] = pokemonVertexForMaterial(
                skinned[i],
                material,
                current_eye_expression_frame_,
                current_mouth_expression_frame_);
        }
        const bgfx::Memory* primitive_mem = bgfx::copy(
            pokemon_frame_vertices_.data() + base,
            static_cast<std::uint32_t>(skinned.size() * sizeof(Vertex)));
        bgfx::update(pokemon_mesh_.dvbh, base, primitive_mem);
        updated_any = true;
    }
    if (!updated_any) return;
    updatePokemonBoundsFromVertices(pokemon_frame_vertices_);
}

const AttendPokemonAnimation* AttendBgfxRenderer::Impl::resolveSemanticAnimation(const std::string& semantic) const {
    const auto it = config_.interaction_adapter.semantic_animation_slots.find(semantic);
    if (it == config_.interaction_adapter.semantic_animation_slots.end()) return nullptr;
    for (const std::string& candidate : it->second) {
        for (const AttendPokemonAnimation& animation : pokemon_model_.animations) {
            if (animation.name == candidate) return &animation;
        }
    }
    for (const std::string& candidate : it->second) {
        for (const AttendPokemonAnimation& animation : pokemon_model_.animations) {
            if (!candidate.empty() && animation.name.find(candidate) != std::string::npos) return &animation;
        }
    }
    return nullptr;
}

float AttendBgfxRenderer::Impl::reactionWeight(double scene_time_seconds) const {
    if (!reaction_active_ || reaction_duration_seconds_ <= 0.0) return 0.0f;
    const double elapsed = std::clamp(scene_time_seconds - reaction_start_seconds_, 0.0, reaction_duration_seconds_);
    float in_weight = 1.0f;
    if (reaction_fade_in_seconds_ > 0.0) {
        in_weight = static_cast<float>(std::clamp(elapsed / reaction_fade_in_seconds_, 0.0, 1.0));
    }
    float out_weight = 1.0f;
    if (reaction_fade_out_seconds_ > 0.0) {
        const double remaining = reaction_duration_seconds_ - elapsed;
        out_weight = static_cast<float>(std::clamp(remaining / reaction_fade_out_seconds_, 0.0, 1.0));
    }
    const float weight = std::min(in_weight, out_weight);
    return weight * weight * (3.0f - 2.0f * weight);
}

const AttendInteractionAdapterConfig::ReactionCombo* AttendBgfxRenderer::Impl::reactionCombo(const std::string& id) const {
    const auto it = config_.interaction_adapter.reaction_combos.find(id);
    return it == config_.interaction_adapter.reaction_combos.end() ? nullptr : &it->second;
}

float AttendBgfxRenderer::Impl::petReadyCueWeight(
    double scene_time_seconds,
    const AttendInteractionAdapterConfig::ReactionCombo& combo) const {
    if (combo.ready_animation_semantic.empty() || combo.ready_weight <= 0.0f || pet_started_seconds_ < 0.0) return 0.0f;
    const double elapsed = scene_time_seconds - pet_started_seconds_;
    if (elapsed < static_cast<double>(combo.min_pet_seconds)) return 0.0f;
    const double cue_elapsed = elapsed - static_cast<double>(combo.min_pet_seconds);
    const float fade = static_cast<float>(std::clamp(cue_elapsed / static_cast<double>(std::max(0.01f, combo.ready_fade_seconds)), 0.0, 1.0));
    const float eased = fade * fade * (3.0f - 2.0f * fade);
    return std::clamp(combo.ready_weight * eased, 0.0f, 1.0f);
}

int AttendBgfxRenderer::Impl::eyeExpressionFrame(const std::string& semantic, int fallback) const {
    const auto it = config_.interaction_adapter.eye_expression_frames.find(semantic);
    return it == config_.interaction_adapter.eye_expression_frames.end() ? fallback : std::max(0, it->second);
}

int AttendBgfxRenderer::Impl::mouthExpressionFrame(const std::string& semantic, int fallback) const {
    const auto it = config_.interaction_adapter.mouth_expression_frames.find(semantic);
    return it == config_.interaction_adapter.mouth_expression_frames.end() ? fallback : std::max(0, it->second);
}

void AttendBgfxRenderer::Impl::startPendingReaction(double scene_time_seconds) {
    if (pending_reaction_id_.empty()) return;
    const std::string id = std::move(pending_reaction_id_);
    pending_reaction_id_.clear();
    const double interaction_seconds = pending_reaction_interaction_seconds_;
    pending_reaction_interaction_seconds_ = 0.0;
    const auto it = config_.interaction_adapter.reaction_combos.find(id);
    if (it == config_.interaction_adapter.reaction_combos.end()) return;

    const AttendInteractionAdapterConfig::ReactionCombo& combo = it->second;
    if (interaction_seconds < static_cast<double>(combo.min_pet_seconds)) return;
    reaction_animation_ = resolveSemanticAnimation(combo.animation_semantic);
    reaction_eye_expression_frame_ = eyeExpressionFrame(combo.eye_expression, normal_eye_expression_frame_);
    reaction_mouth_expression_frame_ = mouthExpressionFrame(combo.mouth_expression, normal_mouth_expression_frame_);
    reaction_duration_seconds_ = std::max(0.05, static_cast<double>(combo.duration_seconds));
    if (reaction_animation_ && reaction_animation_->duration_seconds > 0.0f) {
        reaction_duration_seconds_ = std::min(
            reaction_duration_seconds_,
            static_cast<double>(std::max(0.05f, reaction_animation_->duration_seconds)));
    }
    const double fade_scale = pokemon_world_height_ >= combo.large_pokemon_height
        ? static_cast<double>(combo.large_pokemon_fade_scale)
        : 1.0;
    reaction_fade_in_seconds_ = std::min(static_cast<double>(combo.fade_in_seconds) * fade_scale, reaction_duration_seconds_ * 0.5);
    reaction_fade_out_seconds_ = std::min(static_cast<double>(combo.fade_out_seconds) * fade_scale, reaction_duration_seconds_ * 0.5);
    reaction_eye_linger_until_seconds_ =
        scene_time_seconds + reaction_duration_seconds_ + static_cast<double>(combo.eye_linger_seconds);
    reaction_start_seconds_ = scene_time_seconds;
    reaction_active_ = true;
    reaction_from_semantic_ = false;
    reaction_reverse_ = false;
    semantic_sleeping_ = false;
    pending_sleep_loop_after_start_ = false;
    random_blink_amount_ = 0.0f;
}

void AttendBgfxRenderer::Impl::startPendingSemanticAnimation(double scene_time_seconds) {
    if (pending_semantic_animation_.empty()) return;
    const std::string semantic = std::move(pending_semantic_animation_);
    const std::string eye = std::move(pending_semantic_eye_expression_);
    const std::string mouth = std::move(pending_semantic_mouth_expression_);
    const double requested_duration = pending_semantic_duration_seconds_;
    const double requested_fade_in = pending_semantic_fade_in_seconds_;
    const double requested_fade_out = pending_semantic_fade_out_seconds_;
    const bool requested_reverse = pending_semantic_reverse_;
    pending_semantic_animation_.clear();
    pending_semantic_eye_expression_.clear();
    pending_semantic_mouth_expression_.clear();
    pending_semantic_duration_seconds_ = 0.0;
    pending_semantic_reverse_ = false;

    if (semantic == config_.idle_behavior.sleep_animation_semantic && !requested_reverse) {
        const AttendPokemonAnimation* sleep_loop = resolveSemanticAnimation(semantic);
        if (!sleep_loop) return;
        if (const AttendPokemonAnimation* sleep_start = resolveSemanticAnimation("sleep_start")) {
            pending_sleep_loop_after_start_ = true;
            pending_sleep_loop_duration_seconds_ = requested_duration;
            pending_sleep_loop_fade_in_seconds_ = requested_fade_in;
            pending_sleep_loop_fade_out_seconds_ = requested_fade_out;
            pending_sleep_loop_eye_expression_ = eye;
            pending_sleep_loop_mouth_expression_ = mouth;
            reaction_animation_ = sleep_start;
            reaction_eye_expression_frame_ = normal_eye_expression_frame_;
            reaction_mouth_expression_frame_ = normal_mouth_expression_frame_;
            reaction_duration_seconds_ = std::max(0.05, static_cast<double>(sleep_start->duration_seconds));
            reaction_fade_in_seconds_ = std::min(std::max(0.0, requested_fade_in), reaction_duration_seconds_ * 0.5);
            reaction_fade_out_seconds_ = 0.0;
            reaction_eye_linger_until_seconds_ = scene_time_seconds + reaction_duration_seconds_;
            reaction_start_seconds_ = scene_time_seconds;
            reaction_active_ = true;
            reaction_from_semantic_ = true;
            reaction_reverse_ = false;
            semantic_sleeping_ = true;
            random_blink_amount_ = 0.0f;
            return;
        }
    }

    reaction_animation_ = resolveSemanticAnimation(semantic);
    if (!reaction_animation_) return;
    reaction_eye_expression_frame_ = eye.empty()
        ? normal_eye_expression_frame_
        : eyeExpressionFrame(eye, normal_eye_expression_frame_);
    reaction_mouth_expression_frame_ = mouth.empty()
        ? normal_mouth_expression_frame_
        : mouthExpressionFrame(mouth, normal_mouth_expression_frame_);
    reaction_duration_seconds_ = requested_duration > 0.0
        ? requested_duration
        : static_cast<double>(std::max(0.05f, reaction_animation_->duration_seconds));
    reaction_duration_seconds_ = std::clamp(reaction_duration_seconds_, 0.05, 7200.0);
    reaction_fade_in_seconds_ = std::min(std::max(0.0, requested_fade_in), reaction_duration_seconds_ * 0.5);
    reaction_fade_out_seconds_ = std::min(std::max(0.0, requested_fade_out), reaction_duration_seconds_ * 0.5);
    reaction_eye_linger_until_seconds_ = scene_time_seconds + reaction_duration_seconds_;
    reaction_start_seconds_ = scene_time_seconds;
    reaction_active_ = true;
    reaction_from_semantic_ = true;
    reaction_reverse_ = requested_reverse;
    semantic_sleeping_ = semantic == config_.idle_behavior.sleep_animation_semantic && !requested_reverse;
    random_blink_amount_ = 0.0f;
}

void AttendBgfxRenderer::Impl::startPendingSleepLoop(double scene_time_seconds) {
    pending_sleep_loop_after_start_ = false;
    reaction_animation_ = resolveSemanticAnimation(config_.idle_behavior.sleep_animation_semantic);
    if (!reaction_animation_) return;
    reaction_eye_expression_frame_ = eyeExpressionFrame(pending_sleep_loop_eye_expression_, closed_eye_expression_frame_);
    reaction_mouth_expression_frame_ = mouthExpressionFrame(pending_sleep_loop_mouth_expression_, normal_mouth_expression_frame_);
    reaction_duration_seconds_ = pending_sleep_loop_duration_seconds_ > 0.0
        ? pending_sleep_loop_duration_seconds_
        : static_cast<double>(std::max(0.05f, reaction_animation_->duration_seconds));
    reaction_duration_seconds_ = std::clamp(reaction_duration_seconds_, 0.05, 7200.0);
    reaction_fade_in_seconds_ = 0.0;
    reaction_fade_out_seconds_ = std::min(std::max(0.0, pending_sleep_loop_fade_out_seconds_), reaction_duration_seconds_ * 0.5);
    reaction_eye_linger_until_seconds_ = scene_time_seconds + reaction_duration_seconds_;
    reaction_start_seconds_ = scene_time_seconds;
    reaction_active_ = true;
    reaction_from_semantic_ = true;
    reaction_reverse_ = false;
    semantic_sleeping_ = true;
    random_blink_amount_ = 0.0f;
}

void AttendBgfxRenderer::Impl::startPendingWakeFromSleep(double scene_time_seconds) {
    if (!pending_wake_from_sleep_) return;
    pending_wake_from_sleep_ = false;
    const AttendPokemonAnimation* wake_animation = resolveSemanticAnimation("sleep_start");
    if (!wake_animation) return;
    pending_semantic_animation_ = "sleep_start";
    pending_semantic_duration_seconds_ = std::max(0.05f, wake_animation->duration_seconds);
    pending_semantic_fade_in_seconds_ = 0.0;
    pending_semantic_fade_out_seconds_ = 0.40;
    pending_semantic_eye_expression_ = "closed";
    pending_semantic_mouth_expression_ = "closed_normal";
    pending_semantic_reverse_ = true;
    startPendingSemanticAnimation(scene_time_seconds);
}

} // namespace pr::gameplay::attend::rendering
