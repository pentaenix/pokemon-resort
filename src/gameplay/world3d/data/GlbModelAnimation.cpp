#include "gameplay/world3d/data/GlbModelLoader.hpp"

#include <algorithm>
#include <cmath>

namespace pr::gameplay::world3d::data {

std::vector<std::vector<float>> sampleGlbMorphWeights(const GlbMesh& mesh, double time_seconds) {
    if (mesh.animations.empty() || mesh.animations.front().morph_channels.empty()) return {};

    const GlbAnimation& animation = mesh.animations.front();
    std::vector<std::vector<float>> result(mesh.node_morph_target_counts.size());
    const float duration = animation.duration_seconds;
    const float time = duration > 0.0f
        ? static_cast<float>(std::fmod(std::max(0.0, time_seconds), static_cast<double>(duration)))
        : 0.0f;

    for (const GlbMorphAnimationChannel& channel : animation.morph_channels) {
        if (channel.target_node < 0 || channel.target_node >= static_cast<int>(result.size()) ||
            channel.target_count <= 0 || channel.times.empty()) {
            continue;
        }
        std::vector<float>& sampled = result[static_cast<std::size_t>(channel.target_node)];
        sampled.assign(static_cast<std::size_t>(channel.target_count), 0.0f);

        auto upper = std::upper_bound(channel.times.begin(), channel.times.end(), time);
        const std::size_t next = upper == channel.times.end()
            ? channel.times.size() - 1
            : static_cast<std::size_t>(upper - channel.times.begin());
        const std::size_t previous = next == 0 ? 0 : next - 1;
        float blend = 0.0f;
        if (channel.interpolation != GlbMorphAnimationChannel::Interpolation::Step && next != previous) {
            const float span = channel.times[next] - channel.times[previous];
            if (span > 0.0f) blend = std::clamp((time - channel.times[previous]) / span, 0.0f, 1.0f);
        }
        for (int target = 0; target < channel.target_count; ++target) {
            const std::size_t a = previous * static_cast<std::size_t>(channel.target_count) + static_cast<std::size_t>(target);
            const std::size_t b = next * static_cast<std::size_t>(channel.target_count) + static_cast<std::size_t>(target);
            if (b >= channel.weights.size()) break;
            if (channel.interpolation == GlbMorphAnimationChannel::Interpolation::CubicSpline &&
                a < channel.out_tangents.size() && b < channel.in_tangents.size() && next != previous) {
                const float span = channel.times[next] - channel.times[previous];
                const float t2 = blend * blend;
                const float t3 = t2 * blend;
                sampled[static_cast<std::size_t>(target)] =
                    ((2.0f * t3 - 3.0f * t2 + 1.0f) * channel.weights[a]) +
                    ((t3 - 2.0f * t2 + blend) * span * channel.out_tangents[a]) +
                    ((-2.0f * t3 + 3.0f * t2) * channel.weights[b]) +
                    ((t3 - t2) * span * channel.in_tangents[b]);
            } else {
                sampled[static_cast<std::size_t>(target)] = channel.weights[a] +
                    ((channel.weights[b] - channel.weights[a]) * blend);
            }
        }
    }
    return result;
}

std::array<float, 3> sampleGlbMorphPosition(
    const GlbVertex& vertex,
    const std::vector<std::vector<float>>& node_weights) {
    std::array<float, 3> result{vertex.x, vertex.y, vertex.z};
    if (vertex.node < 0 || vertex.node >= static_cast<int>(node_weights.size())) return result;
    const std::vector<float>& weights = node_weights[static_cast<std::size_t>(vertex.node)];
    const std::size_t count = std::min(weights.size(), vertex.morph_position_deltas.size());
    for (std::size_t i = 0; i < count; ++i) {
        result[0] += vertex.morph_position_deltas[i][0] * weights[i];
        result[1] += vertex.morph_position_deltas[i][1] * weights[i];
        result[2] += vertex.morph_position_deltas[i][2] * weights[i];
    }
    return result;
}

GlbVertex applyGlbMorphWeights(
    const GlbVertex& vertex,
    const std::vector<std::vector<float>>& node_weights) {
    GlbVertex result = vertex;
    const std::array<float, 3> position = sampleGlbMorphPosition(vertex, node_weights);
    result.x = position[0];
    result.y = position[1];
    result.z = position[2];
    return result;
}

} // namespace pr::gameplay::world3d::data
