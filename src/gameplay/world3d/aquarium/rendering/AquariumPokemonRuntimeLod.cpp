#include "gameplay/world3d/aquarium/rendering/AquariumPokemonRuntimeLod.hpp"

#include <meshoptimizer/src/meshoptimizer.h>

#include <algorithm>
#include <cstdint>
#include <limits>

namespace pr::gameplay::world3d::aquarium::rendering {
namespace attend = gameplay::attend::rendering;
namespace {

constexpr std::size_t kMinimumTrianglesToReduce = 24;

std::optional<attend::AttendPokemonPrimitive> reducePrimitive(
    const attend::AttendPokemonPrimitive& source,
    float target_triangle_ratio) {
    if (source.vertices.empty() || source.indices.size() < kMinimumTrianglesToReduce * 3U ||
        source.indices.size() % 3U != 0U) {
        return std::nullopt;
    }
    for (const std::uint32_t index : source.indices) {
        if (index >= source.vertices.size()) return std::nullopt;
    }

    const float ratio = std::clamp(target_triangle_ratio, 0.25f, 1.0f);
    const std::size_t target_index_count = std::max<std::size_t>(
        3U, (static_cast<std::size_t>(source.indices.size() * ratio) / 3U) * 3U);
    if (target_index_count >= source.indices.size()) return std::nullopt;

    std::vector<std::uint32_t> simplified(source.indices.size());
    float result_error = 0.0f;
    const std::size_t simplified_count = meshopt_simplify(
        simplified.data(),
        source.indices.data(),
        source.indices.size(),
        &source.vertices.front().x,
        source.vertices.size(),
        sizeof(attend::AttendPokemonVertex),
        target_index_count,
        0.01f,
        meshopt_SimplifyLockBorder,
        &result_error);
    if (simplified_count < 3U || simplified_count >= source.indices.size() ||
        simplified_count % 3U != 0U) {
        return std::nullopt;
    }
    simplified.resize(simplified_count);

    attend::AttendPokemonPrimitive reduced;
    reduced.material = source.material;
    reduced.mesh_node = source.mesh_node;
    reduced.skin = source.skin;
    reduced.render_order = source.render_order;
    reduced.scene_order = source.scene_order;
    reduced.default_visible = source.default_visible;
    reduced.visible_for_forms = source.visible_for_forms;
    reduced.indices.reserve(simplified.size());

    constexpr std::uint32_t kUnused = std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> remap(source.vertices.size(), kUnused);
    for (const std::uint32_t source_index : simplified) {
        std::uint32_t& render_index = remap[source_index];
        if (render_index == kUnused) {
            render_index = static_cast<std::uint32_t>(reduced.vertices.size());
            reduced.vertices.push_back(source.vertices[source_index]);
        }
        reduced.indices.push_back(render_index);
    }
    if (reduced.vertices.empty() || reduced.vertices.size() > UINT16_MAX) return std::nullopt;
    return reduced;
}

} // namespace

AquariumPokemonRuntimeLod buildAquariumPokemonRuntimeLod(
    const attend::AttendPokemonModel& source,
    float target_triangle_ratio) {
    AquariumPokemonRuntimeLod result;
    result.replacements.resize(source.primitives.size());
    for (std::size_t i = 0; i < source.primitives.size(); ++i) {
        const attend::AttendPokemonPrimitive& primitive = source.primitives[i];
        result.statistics.source_vertices += primitive.vertices.size();
        result.statistics.source_triangles += primitive.indices.size() / 3U;
        result.replacements[i] = reducePrimitive(primitive, target_triangle_ratio);
        const auto& render = result.replacements[i]
            ? *result.replacements[i]
            : primitive;
        result.statistics.render_vertices += render.vertices.size();
        result.statistics.render_triangles += render.indices.size() / 3U;
    }
    return result;
}

} // namespace pr::gameplay::world3d::aquarium::rendering
