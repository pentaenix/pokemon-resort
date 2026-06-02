#include "gameplay/world3d/followers/NatureIdleConfig.hpp"

#include "core/config/Json.hpp"

#include <algorithm>
#include <filesystem>

namespace pr::gameplay::world3d::followers {

namespace fs = std::filesystem;

namespace {

double numOr(const JsonValue* v, double fallback) {
    return (v && v->isNumber()) ? v->asNumber() : fallback;
}

int intOr(const JsonValue* v, int fallback) {
    return (v && v->isNumber()) ? static_cast<int>(v->asNumber()) : fallback;
}

std::string strOr(const JsonValue* v, const std::string& fallback) {
    return (v && v->isString()) ? v->asString() : fallback;
}

void parseRange(const JsonValue* root, NatureIdleRangeSeconds& out) {
    if (!root || !root->isObject()) return;
    out.min = numOr(root->get("min"), out.min);
    out.max = numOr(root->get("max"), out.max);
    if (out.max < out.min) std::swap(out.min, out.max);
}

void parseWeightTable(
    const JsonValue* root,
    std::unordered_map<std::string, std::unordered_map<std::string, int>>& out) {
    if (!root || !root->isObject()) return;
    for (const auto& [outer_key, outer_value] : root->asObject()) {
        if (!outer_value.isObject()) continue;
        auto& bucket = out[outer_key];
        for (const auto& [inner_key, inner_value] : outer_value.asObject()) {
            if (!inner_value.isNumber()) continue;
            bucket[inner_key] = static_cast<int>(inner_value.asNumber());
        }
    }
}

} // namespace

NatureIdleBehaviorConfig loadNatureIdleBehaviorConfig(const std::string& project_root) {
    NatureIdleBehaviorConfig out;
    try {
        const JsonValue root = parseJsonFile(
            (fs::path(project_root) / "config" / "gameplay" / "followers" / "idle_behaviours.json").string());
        if (!root.isObject()) return out;
        const JsonValue* idle = root.get("natureIdle");
        if (!idle || !idle->isObject()) return out;

        out.enabled = idle->get("enabled") ? idle->get("enabled")->asBool() : out.enabled;
        out.nature_source = strOr(idle->get("natureSource"), out.nature_source);
        out.disable_if_nature_missing_or_invalid =
            idle->get("disableIfNatureMissingOrInvalid")
                ? idle->get("disableIfNatureMissingOrInvalid")->asBool()
                : out.disable_if_nature_missing_or_invalid;
        out.start_after_idle_seconds =
            numOr(idle->get("startAfterIdleSeconds"), out.start_after_idle_seconds);
        parseRange(idle->get("behaviorDurationSeconds"), out.behavior_duration_seconds);
        parseRange(idle->get("cooldownBetweenBehaviorsSeconds"), out.cooldown_between_behaviors_seconds);
        out.movement_speed_multiplier =
            numOr(idle->get("movementSpeedMultiplier"), out.movement_speed_multiplier);
        out.return_to_origin_speed_multiplier =
            numOr(idle->get("returnToOriginSpeedMultiplier"), out.return_to_origin_speed_multiplier);
        out.cancel_return_speed_multiplier =
            numOr(idle->get("cancelReturnSpeedMultiplier"), out.cancel_return_speed_multiplier);
        out.max_player_radius = intOr(idle->get("maxPlayerRadius"), out.max_player_radius);
        out.max_npc_follow_radius = intOr(idle->get("maxNpcFollowRadius"), out.max_npc_follow_radius);
        out.restore_original_position_on_natural_end =
            idle->get("restoreOriginalPositionOnNaturalEnd")
                ? idle->get("restoreOriginalPositionOnNaturalEnd")->asBool()
                : out.restore_original_position_on_natural_end;
        out.restore_original_direction_on_natural_end =
            idle->get("restoreOriginalDirectionOnNaturalEnd")
                ? idle->get("restoreOriginalDirectionOnNaturalEnd")->asBool()
                : out.restore_original_direction_on_natural_end;
        out.allow_soft_snap_on_cancel_return =
            idle->get("allowSoftSnapOnCancelReturn")
                ? idle->get("allowSoftSnapOnCancelReturn")->asBool()
                : out.allow_soft_snap_on_cancel_return;
        out.soft_snap_delay_seconds =
            numOr(idle->get("softSnapDelaySeconds"), out.soft_snap_delay_seconds);
        if (const JsonValue* clamp = idle->get("behaviorWeightClamp"); clamp && clamp->isObject()) {
            out.behavior_weight_clamp.min = intOr(clamp->get("min"), out.behavior_weight_clamp.min);
            out.behavior_weight_clamp.max = intOr(clamp->get("max"), out.behavior_weight_clamp.max);
        }
        out.quirky_random_behavior_chance =
            numOr(idle->get("quirkyRandomBehaviorChance"), out.quirky_random_behavior_chance);
        if (const JsonValue* jump = idle->get("jump"); jump && jump->isObject()) {
            out.jump.height_pixels = intOr(jump->get("heightPixels"), out.jump.height_pixels);
            out.jump.duration_seconds = numOr(jump->get("durationSeconds"), out.jump.duration_seconds);
        }
        if (const JsonValue* poke = idle->get("poke"); poke && poke->isObject()) {
            out.poke.distance_tiles = numOr(poke->get("distanceTiles"), out.poke.distance_tiles);
            out.poke.forward_seconds = numOr(poke->get("forwardSeconds"), out.poke.forward_seconds);
            out.poke.return_seconds = numOr(poke->get("returnSeconds"), out.poke.return_seconds);
        }
        if (const JsonValue* dust = idle->get("landingDust"); dust && dust->isObject()) {
            out.landing_dust.enabled =
                dust->get("enabled") ? dust->get("enabled")->asBool() : out.landing_dust.enabled;
            out.landing_dust.texture_path = strOr(dust->get("texturePath"), out.landing_dust.texture_path);
            out.landing_dust.frame_width = intOr(dust->get("frameWidth"), out.landing_dust.frame_width);
            out.landing_dust.frame_height = intOr(dust->get("frameHeight"), out.landing_dust.frame_height);
            out.landing_dust.frame_count = intOr(dust->get("frameCount"), out.landing_dust.frame_count);
            out.landing_dust.sprite_scale =
                static_cast<float>(numOr(dust->get("spriteScale"), out.landing_dust.sprite_scale));
            out.landing_dust.screen_offset_y_px =
                intOr(dust->get("screenOffsetYPx"), out.landing_dust.screen_offset_y_px);
        }
        if (const JsonValue* fallback = idle->get("fallbackOrder"); fallback && fallback->isArray()) {
            out.fallback_order.clear();
            for (const JsonValue& item : fallback->asArray()) {
                if (item.isString()) out.fallback_order.push_back(item.asString());
            }
        }
        parseWeightTable(idle->get("groupWeights"), out.group_weights);
        parseWeightTable(idle->get("natureModifiers"), out.nature_modifiers);
    } catch (...) {
        return out;
    }

    out.start_after_idle_seconds = std::max(0.0, out.start_after_idle_seconds);
    out.behavior_duration_seconds.min = std::max(0.0, out.behavior_duration_seconds.min);
    out.behavior_duration_seconds.max = std::max(out.behavior_duration_seconds.min, out.behavior_duration_seconds.max);
    out.cooldown_between_behaviors_seconds.min = std::max(0.0, out.cooldown_between_behaviors_seconds.min);
    out.cooldown_between_behaviors_seconds.max =
        std::max(out.cooldown_between_behaviors_seconds.min, out.cooldown_between_behaviors_seconds.max);
    out.movement_speed_multiplier = std::max(0.1, out.movement_speed_multiplier);
    out.return_to_origin_speed_multiplier = std::max(0.1, out.return_to_origin_speed_multiplier);
    out.cancel_return_speed_multiplier = std::max(0.1, out.cancel_return_speed_multiplier);
    out.max_player_radius = std::max(1, out.max_player_radius);
    out.max_npc_follow_radius = std::max(1, out.max_npc_follow_radius);
    if (out.behavior_weight_clamp.max < out.behavior_weight_clamp.min) {
        std::swap(out.behavior_weight_clamp.min, out.behavior_weight_clamp.max);
    }
    out.quirky_random_behavior_chance = std::clamp(out.quirky_random_behavior_chance, 0.0, 1.0);
    out.jump.height_pixels = std::max(1, out.jump.height_pixels);
    out.jump.duration_seconds = std::max(0.01, out.jump.duration_seconds);
    out.poke.distance_tiles = std::max(0.0, out.poke.distance_tiles);
    out.poke.forward_seconds = std::max(0.01, out.poke.forward_seconds);
    out.poke.return_seconds = std::max(0.01, out.poke.return_seconds);
    out.landing_dust.frame_width = std::max(1, out.landing_dust.frame_width);
    out.landing_dust.frame_height = std::max(1, out.landing_dust.frame_height);
    out.landing_dust.frame_count = std::max(1, out.landing_dust.frame_count);
    out.landing_dust.sprite_scale = std::max(0.1f, out.landing_dust.sprite_scale);
    return out;
}

} // namespace pr::gameplay::world3d::followers
