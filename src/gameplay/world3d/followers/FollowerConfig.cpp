#include "gameplay/world3d/followers/FollowerConfig.hpp"

#include "core/config/Json.hpp"

#include <algorithm>
#include <filesystem>

namespace pr::gameplay::world3d::followers {

namespace fs = std::filesystem;

namespace {

int intOr(const JsonValue* v, int fallback) {
    return (v && v->isNumber()) ? static_cast<int>(v->asNumber()) : fallback;
}

double numOr(const JsonValue* v, double fallback) {
    return (v && v->isNumber()) ? v->asNumber() : fallback;
}

std::string strOr(const JsonValue* v, const std::string& fallback) {
    return (v && v->isString()) ? v->asString() : fallback;
}

void parseWorldOffset(
    const JsonValue* value,
    float& out_x,
    float& out_y,
    float& out_z) {
    if (!value || !value->isArray()) return;
    const auto& arr = value->asArray();
    if (arr.size() > 0 && arr[0].isNumber()) out_x = static_cast<float>(arr[0].asNumber());
    if (arr.size() > 1 && arr[1].isNumber()) out_y = static_cast<float>(arr[1].asNumber());
    if (arr.size() > 2 && arr[2].isNumber()) out_z = static_cast<float>(arr[2].asNumber());
}

} // namespace

FollowerSummonConfig loadFollowerSummonConfig(const std::string& project_root) {
    FollowerSummonConfig out;
    try {
        const JsonValue root = parseJsonFile(
            (fs::path(project_root) / "config" / "gameplay" / "followers" / "summon.json").string());
        if (!root.isObject()) return out;
        const JsonValue* follower = root.get("follower");
        if (!follower || !follower->isObject()) return out;

        out.follow_step_duration_ms =
            intOr(follower->get("followStepDurationMs"), out.follow_step_duration_ms);

        if (const JsonValue* ball = follower->get("ballAnimation"); ball && ball->isObject()) {
            out.ball_animation.duration_ms = intOr(ball->get("durationMs"), out.ball_animation.duration_ms);
            out.ball_animation.hold_last_frame_ms =
                intOr(ball->get("pokeball_hold_animation"), out.ball_animation.hold_last_frame_ms);
            out.ball_animation.hold_last_frame_ms =
                intOr(ball->get("holdLastFrameMs"), out.ball_animation.hold_last_frame_ms);
            out.ball_animation.full_animation =
                ball->get("fullAnimation") ? ball->get("fullAnimation")->asBool() : out.ball_animation.full_animation;
            out.ball_animation.fall_enabled =
                ball->get("fallEnabled") ? ball->get("fallEnabled")->asBool() : out.ball_animation.fall_enabled;
            out.ball_animation.fall_height_world =
                static_cast<float>(numOr(ball->get("fallHeightWorld"), out.ball_animation.fall_height_world));
            out.ball_animation.sprite_scale =
                static_cast<float>(numOr(ball->get("spriteScale"), out.ball_animation.sprite_scale));
            out.ball_animation.screen_offset_y_px =
                intOr(ball->get("screenOffsetYPx"), out.ball_animation.screen_offset_y_px);
            parseWorldOffset(
                ball->get("worldOffset"),
                out.ball_animation.world_offset_x,
                out.ball_animation.world_offset_y,
                out.ball_animation.world_offset_z);
        }

        const JsonValue* dust = follower->get("landingDust");
        if (!dust) {
            dust = follower->get("dust");
        }
        if (dust && dust->isObject()) {
            out.landing_dust.override_idle_config = true;
            out.landing_dust.sprite_scale =
                static_cast<float>(numOr(dust->get("spriteScale"), out.landing_dust.sprite_scale));
            out.landing_dust.screen_offset_y_px =
                intOr(dust->get("screenOffsetYPx"), out.landing_dust.screen_offset_y_px);
            parseWorldOffset(
                dust->get("worldOffset"),
                out.landing_dust.world_offset_x,
                out.landing_dust.world_offset_y,
                out.landing_dust.world_offset_z);
        }

        if (const JsonValue* entry = follower->get("entryAnimation"); entry && entry->isObject()) {
            out.entry_animation.duration_ms = intOr(entry->get("durationMs"), out.entry_animation.duration_ms);
            out.entry_animation.start_scale =
                static_cast<float>(numOr(entry->get("startScale"), out.entry_animation.start_scale));
            if (const JsonValue* color = entry->get("colorRgba"); color && color->isArray()) {
                const auto& arr = color->asArray();
                if (arr.size() > 0 && arr[0].isNumber()) out.entry_animation.tint_r = static_cast<float>(arr[0].asNumber()) / 255.0f;
                if (arr.size() > 1 && arr[1].isNumber()) out.entry_animation.tint_g = static_cast<float>(arr[1].asNumber()) / 255.0f;
                if (arr.size() > 2 && arr[2].isNumber()) out.entry_animation.tint_b = static_cast<float>(arr[2].asNumber()) / 255.0f;
                if (arr.size() > 3 && arr[3].isNumber()) out.entry_animation.alpha = static_cast<float>(arr[3].asNumber()) / 255.0f;
            }
        }
    } catch (...) {
        return out;
    }

    out.ball_animation.duration_ms = std::max(1, out.ball_animation.duration_ms);
    out.ball_animation.hold_last_frame_ms = std::max(0, out.ball_animation.hold_last_frame_ms);
    out.ball_animation.sprite_scale = std::max(0.1f, out.ball_animation.sprite_scale);
    out.landing_dust.sprite_scale = std::max(0.1f, out.landing_dust.sprite_scale);
    out.entry_animation.duration_ms = std::max(1, out.entry_animation.duration_ms);
    out.follow_step_duration_ms = std::max(1, out.follow_step_duration_ms);
    out.entry_animation.start_scale = std::clamp(out.entry_animation.start_scale, 0.0f, 1.0f);
    out.entry_animation.alpha = std::clamp(out.entry_animation.alpha, 0.0f, 1.0f);
    return out;
}

FollowerSessionConfig loadFollowerSessionConfig(const std::string& project_root) {
    FollowerSessionConfig out;
    try {
        const JsonValue root = parseJsonFile(
            (fs::path(project_root) / "config" / "gameplay" / "followers" / "session.json").string());
        if (!root.isObject()) return out;
        const JsonValue* follower = root.get("follower");
        if (!follower || !follower->isObject()) return out;
        out.enabled = follower->get("enabled") ? follower->get("enabled")->asBool() : out.enabled;
        out.pokemon_species = strOr(follower->get("pokemonSpecies"), out.pokemon_species);
        out.pokemon_charbin_path = strOr(follower->get("pokemonCharbinPath"), out.pokemon_charbin_path);
        out.pokemon_form_id = strOr(follower->get("pokemonFormId"), out.pokemon_form_id);
        out.pokemon_shiny = follower->get("pokemonShiny") ? follower->get("pokemonShiny")->asBool() : out.pokemon_shiny;
        out.pokeball_id = strOr(follower->get("pokeballId"), out.pokeball_id);
        out.nature = strOr(follower->get("nature"), out.nature);
        out.forced_behavior = strOr(follower->get("forcedBehavior"), out.forced_behavior);
        out.idle_script_id = strOr(follower->get("idleScriptId"), out.idle_script_id);
        out.movement_mode = strOr(follower->get("movementMode"), out.movement_mode);
    } catch (...) {
        return out;
    }
    if (out.movement_mode != "trail" && out.movement_mode != "replay") {
        out.movement_mode = "trail";
    }
    return out;
}

std::string resolveFollowerPokemonCharbinPath(const std::string& project_root, const std::string& pokemon_species) {
    if (pokemon_species.empty()) return {};
    return (fs::path(project_root) / "assets" / "characters" / "pokemon" / (pokemon_species + ".charbin")).string();
}

std::string resolveFollowerPokeballCharbinPath(const std::string& project_root, const std::string& pokeball_id) {
    const fs::path requested =
        fs::path(project_root) / "assets" / "characters" / "objects" / (pokeball_id + ".charbin");
    if (fs::exists(requested)) return requested.string();
    return (fs::path(project_root) / "assets" / "characters" / "objects" / "poke_ball.charbin").string();
}

} // namespace pr::gameplay::world3d::followers
