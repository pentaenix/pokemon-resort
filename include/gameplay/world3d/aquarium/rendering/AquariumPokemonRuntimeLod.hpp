#pragma once

#include "gameplay/attend/rendering/AttendPokemonModel.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace pr::gameplay::world3d::aquarium::rendering {

struct AquariumPokemonRuntimeLodStats {
    std::size_t source_vertices = 0;
    std::size_t source_triangles = 0;
    std::size_t render_vertices = 0;
    std::size_t render_triangles = 0;
};

// Owns replacements only for primitives that were safely reduced. An empty
// entry means the renderer must use the corresponding source primitive.
struct AquariumPokemonRuntimeLod {
    std::vector<std::optional<gameplay::attend::rendering::AttendPokemonPrimitive>> replacements;
    AquariumPokemonRuntimeLodStats statistics;
};

AquariumPokemonRuntimeLod buildAquariumPokemonRuntimeLod(
    const gameplay::attend::rendering::AttendPokemonModel& source,
    float target_triangle_ratio = 0.60f);

} // namespace pr::gameplay::world3d::aquarium::rendering
