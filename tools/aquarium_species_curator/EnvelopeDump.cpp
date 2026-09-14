#include "gameplay/world3d/aquarium/AquariumPokemonMetrics.hpp"
#include "gameplay/world3d/aquarium/AquariumSpeciesCatalog.hpp"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>

namespace aquarium = pr::gameplay::world3d::aquarium;

namespace {

void includeBounds(
    aquarium::AquariumPokemonMetrics& target,
    const aquarium::AquariumPokemonMetrics& source) {
    if (!source.valid) return;
    if (!target.valid) {
        target = source;
        return;
    }
    target.min_x = std::min(target.min_x, source.min_x);
    target.max_x = std::max(target.max_x, source.max_x);
    target.min_y = std::min(target.min_y, source.min_y);
    target.max_y = std::max(target.max_y, source.max_y);
    target.min_z = std::min(target.min_z, source.min_z);
    target.max_z = std::max(target.max_z, source.max_z);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: aquarium_species_envelope_dump <project-root> <catalogue>\n";
        return 2;
    }
    const std::filesystem::path project_root = std::filesystem::absolute(argv[1]);
    const auto loaded = aquarium::loadAquariumSpeciesCatalog(argv[2]);
    if (!loaded.valid) {
        for (const auto& diagnostic : loaded.diagnostics) std::cerr << diagnostic << '\n';
        return 1;
    }
    std::cout << std::setprecision(9) << "{\"envelopes\":[";
    bool first = true;
    for (const auto& species : loaded.catalog.approved) {
        const std::filesystem::path configured(species.model_path);
        const std::filesystem::path model = configured.is_absolute()
            ? configured : project_root / configured;
        std::string error;
        int movement_poses = 0;
        auto movement = aquarium::measureAquariumPokemonAnimationEnvelope(
            model.string(), species.form, {species.animation}, 12, &movement_poses, &error);
        movement = aquarium::orientAquariumPokemonMetrics(
            movement, species.pitch_degrees, species.yaw_degrees);
        aquarium::AquariumPokemonMetrics envelope = movement;
        int idle_poses = 0;
        if (!species.idle_animation.empty() && species.idle_animation != species.animation) {
            auto idle = aquarium::measureAquariumPokemonAnimationEnvelope(
                model.string(), species.form, {species.idle_animation}, 12, &idle_poses, &error);
            idle = aquarium::orientAquariumPokemonMetrics(
                idle, species.idle_pitch_degrees, species.yaw_degrees);
            includeBounds(envelope, idle);
        }
        if (!envelope.valid) {
            std::cerr << "Could not bake " << species.id << ": " << error << '\n';
            return 1;
        }
        if (!first) std::cout << ',';
        first = false;
        std::cout << "{\"id\":\"" << species.id
                  << "\",\"version\":1,\"sourceSignature\":\""
                  << species.physicalEnvelopeSourceSignature()
                  << "\",\"sampledPoses\":" << movement_poses + idle_poses
                  << ",\"boundsModelUnits\":["
                  << envelope.min_x << ',' << envelope.max_x << ','
                  << envelope.min_y << ',' << envelope.max_y << ','
                  << envelope.min_z << ',' << envelope.max_z << "]}";
    }
    std::cout << "]}\n";
    return 0;
}
