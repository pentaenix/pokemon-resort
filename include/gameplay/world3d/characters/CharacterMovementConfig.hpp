#pragma once

#include <string>
#include <unordered_map>

namespace pr::gameplay::world3d::characters {

struct CharacterMovementProfile {
    float units_per_second = 64.0f;
};

struct CharacterMovementConfig {
    float walk_units_per_second = 64.0f;
    float run_units_per_second = 112.0f;
    float blocked_step_sfx_repeat_seconds = 0.54f;
    float turn_step_delay_seconds = 0.0f;
    std::unordered_map<std::string, CharacterMovementProfile> profiles;

    float speedForProfile(const std::string& profile_id) const;
    float walkSpeed() const;
    float runSpeed() const;
    float blockedStepSfxRepeatSeconds() const;
    float turnStepDelaySeconds() const;
};

CharacterMovementConfig defaultCharacterMovementConfig();
CharacterMovementConfig loadCharacterMovementConfig(const std::string& project_root);

} // namespace pr::gameplay::world3d::characters
