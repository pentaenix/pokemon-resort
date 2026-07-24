#pragma once

#include "gameplay/attend/AttendLaunchContext.hpp"

#include <cstdint>
#include <optional>
#include <random>
#include <string>

namespace pr::gameplay::attend {

struct AttendSceneRouteResult {
    std::string scene_id = "alola_grass_arena";
    std::string reason = "fallback";
};

class AttendSceneRouter {
public:
    explicit AttendSceneRouter(const std::string& project_root);

    AttendSceneRouteResult resolve(
        const AttendLaunchContext& context,
        std::optional<std::uint32_t> deterministic_seed = std::nullopt);
    const std::string& lastError() const { return last_error_; }

private:
    std::string project_root_;
    std::string routing_path_;
    std::string environment_path_;
    std::mt19937 rng_;
    std::string last_error_;
};

} // namespace pr::gameplay::attend
