#include "gameplay/world3d/data/GlbModelLoader.hpp"

#include <algorithm>
#include <cmath>

namespace pr::gameplay::world3d::data {
namespace {

using Quaternion = std::array<float, 4>;

Quaternion normalizeQuaternion(Quaternion value) {
    const float magnitude = std::sqrt(std::max(0.000001f,
        value[0] * value[0] + value[1] * value[1] +
        value[2] * value[2] + value[3] * value[3]));
    for (float& component : value) component /= magnitude;
    return value;
}

Quaternion conjugate(Quaternion value) {
    return {-value[0], -value[1], -value[2], value[3]};
}

Quaternion multiply(Quaternion a, Quaternion b) {
    return {
        a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
        a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
        a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
        a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]};
}

Quaternion interpolateRotation(Quaternion a, Quaternion b, float amount) {
    const float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if (dot < 0.0f) for (float& component : b) component = -component;
    Quaternion result{};
    for (std::size_t i = 0; i < result.size(); ++i) {
        result[i] = a[i] + (b[i] - a[i]) * amount;
    }
    return normalizeQuaternion(result);
}

std::array<float, 3> rotatePoint(Quaternion rotation, std::array<float, 3> point) {
    const Quaternion vector{point[0], point[1], point[2], 0.0f};
    const Quaternion rotated = multiply(multiply(rotation, vector), conjugate(rotation));
    return {rotated[0], rotated[1], rotated[2]};
}

} // namespace

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

std::vector<std::array<float, 4>> sampleGlbNodeRotations(
    const GlbMesh& mesh,
    double time_seconds) {
    std::vector<Quaternion> result(
        mesh.node_transforms.size(), Quaternion{0.0f, 0.0f, 0.0f, 1.0f});
    for (const GlbAnimation& animation : mesh.animations) {
        if (animation.rotation_channels.empty()) continue;
        const float time = animation.duration_seconds > 0.0f
            ? static_cast<float>(std::fmod(
                std::max(0.0, time_seconds), static_cast<double>(animation.duration_seconds)))
            : 0.0f;
        for (const GlbRotationAnimationChannel& channel : animation.rotation_channels) {
            if (channel.target_node < 0 ||
                channel.target_node >= static_cast<int>(result.size()) ||
                channel.times.empty() || channel.rotations.size() != channel.times.size()) continue;
            auto upper = std::upper_bound(channel.times.begin(), channel.times.end(), time);
            const std::size_t next = upper == channel.times.end()
                ? channel.times.size() - 1U
                : static_cast<std::size_t>(upper - channel.times.begin());
            const std::size_t previous = next == 0U ? 0U : next - 1U;
            float blend = 0.0f;
            if (channel.interpolation != GlbRotationAnimationChannel::Interpolation::Step &&
                next != previous) {
                const float span = channel.times[next] - channel.times[previous];
                if (span > 0.0f) blend = std::clamp(
                    (time - channel.times[previous]) / span, 0.0f, 1.0f);
            }
            const Quaternion sampled = interpolateRotation(
                channel.rotations[previous], channel.rotations[next], blend);
            const GlbNodeTransform& transform =
                mesh.node_transforms[static_cast<std::size_t>(channel.target_node)];
            const Quaternion local_delta = normalizeQuaternion(multiply(
                sampled, conjugate(normalizeQuaternion(transform.base_local_rotation))));
            const Quaternion parent = normalizeQuaternion(transform.parent_world_rotation);
            result[static_cast<std::size_t>(channel.target_node)] = normalizeQuaternion(multiply(
                multiply(parent, local_delta), conjugate(parent)));
        }
    }
    return result;
}

std::array<float, 3> sampleGlbAnimatedPosition(
    const GlbMesh& mesh,
    const GlbVertex& vertex,
    const std::vector<std::vector<float>>& node_weights,
    const std::vector<std::array<float, 4>>& node_rotations) {
    std::array<float, 3> position = sampleGlbMorphPosition(vertex, node_weights);
    if (vertex.node < 0 || vertex.node >= static_cast<int>(mesh.node_transforms.size()) ||
        vertex.node >= static_cast<int>(node_rotations.size())) return position;
    const GlbNodeTransform& transform = mesh.node_transforms[static_cast<std::size_t>(vertex.node)];
    std::array<float, 3> relative{
        position[0] - transform.pivot_world[0],
        position[1] - transform.pivot_world[1],
        position[2] - transform.pivot_world[2]};
    relative = rotatePoint(node_rotations[static_cast<std::size_t>(vertex.node)], relative);
    return {
        transform.pivot_world[0] + relative[0],
        transform.pivot_world[1] + relative[1],
        transform.pivot_world[2] + relative[2]};
}

} // namespace pr::gameplay::world3d::data
