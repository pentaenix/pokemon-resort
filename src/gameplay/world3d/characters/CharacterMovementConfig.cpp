#include "gameplay/world3d/characters/CharacterMovementConfig.hpp"

#include "core/config/Json.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>

namespace pr::gameplay::world3d::characters {

namespace fs = std::filesystem;

namespace {

std::string strOr(const JsonValue* v, const std::string& fallback) {
    return (v && v->isString()) ? v->asString() : fallback;
}

float numOr(const JsonValue* v, float fallback) {
    return (v && v->isNumber()) ? static_cast<float>(v->asNumber()) : fallback;
}

} // namespace

CharacterMovementConfig defaultCharacterMovementConfig() {
    CharacterMovementConfig config;
    config.walk_units_per_second = 64.0f;
    config.run_units_per_second = 112.0f;
    config.blocked_step_sfx_repeat_seconds = 0.54f;
    config.turn_step_delay_seconds = 0.0f;
    config.profiles.emplace("slow", CharacterMovementProfile{48.0f});
    config.profiles.emplace("fast", CharacterMovementProfile{96.0f});
    return config;
}

float CharacterMovementConfig::speedForProfile(const std::string& profile_id) const {
    const std::string requested = profile_id.empty() ? "walk" : profile_id;
    if (requested == "walk" || requested == "normal") {
        return walkSpeed();
    }
    if (requested == "run") {
        return runSpeed();
    }
    if (const auto it = profiles.find(requested); it != profiles.end()) {
        return std::max(1.0f, it->second.units_per_second);
    }
    return walkSpeed();
}

float CharacterMovementConfig::walkSpeed() const {
    return std::max(1.0f, walk_units_per_second);
}

float CharacterMovementConfig::runSpeed() const {
    return std::max(walkSpeed(), run_units_per_second);
}

float CharacterMovementConfig::blockedStepSfxRepeatSeconds() const {
    return std::max(0.05f, blocked_step_sfx_repeat_seconds);
}

float CharacterMovementConfig::turnStepDelaySeconds() const {
    return std::clamp(turn_step_delay_seconds, 0.0f, 0.25f);
}

CharacterMovementConfig loadCharacterMovementConfig(const std::string& project_root) {
    CharacterMovementConfig config = defaultCharacterMovementConfig();
    try {
        const JsonValue root =
            parseJsonFile((fs::path(project_root) / "config" / "gameplay" / "movement.json").string());
        if (!root.isObject()) {
            return config;
        }

        if (const JsonValue* walk = root.get("walk"); walk && walk->isObject()) {
            config.walk_units_per_second =
                std::max(1.0f, numOr(walk->get("unitsPerSecond"), config.walk_units_per_second));
        }
        if (const JsonValue* run = root.get("run"); run && run->isObject()) {
            config.run_units_per_second =
                std::max(1.0f, numOr(run->get("unitsPerSecond"), config.run_units_per_second));
        }
        if (const JsonValue* blocked = root.get("blockedStep"); blocked && blocked->isObject()) {
            config.blocked_step_sfx_repeat_seconds =
                std::max(0.05f, numOr(blocked->get("sfxRepeatSeconds"), config.blocked_step_sfx_repeat_seconds));
        }
        if (const JsonValue* turn = root.get("turning"); turn && turn->isObject()) {
            config.turn_step_delay_seconds =
                std::clamp(numOr(turn->get("stepDelaySeconds"), config.turn_step_delay_seconds), 0.0f, 0.25f);
        }
        const JsonValue* profiles = root.get("profiles");
        if (profiles && profiles->isObject()) {
            for (const auto& [id, value] : profiles->asObject()) {
                if (!value.isObject()) {
                    continue;
                }
                CharacterMovementProfile profile;
                profile.units_per_second =
                    std::max(1.0f, numOr(value.get("unitsPerSecond"), profile.units_per_second));
                config.profiles[id] = profile;
            }
        }
    } catch (const std::exception& ex) {
        std::cerr << "[Overworld3D] Could not load character movement config: "
                  << ex.what() << '\n';
    }
    return config;
}

} // namespace pr::gameplay::world3d::characters
