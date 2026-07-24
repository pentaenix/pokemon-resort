#include "core/crypto/Sha256.hpp"
#include "gameplay/attend/rendering/AttendPokemonAsset.hpp"
#include "gameplay/attend/rendering/AttendPokemonModel.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

fs::path repositoryRoot() {
    fs::path current = fs::current_path();
    while (!current.empty()) {
        if (fs::exists(current / "CMakeLists.txt") &&
            fs::exists(current / "assets" / "pokemon_attend" / "pokemon_models" /
                       "pm0087_00_Dewgong.glbz")) {
            return current;
        }
        const fs::path parent = current.parent_path();
        if (parent == current) break;
        current = parent;
    }
    return {};
}

} // namespace

int main() {
    const fs::path root = repositoryRoot();
    expect(!root.empty(), "repository root with compiled Dewgong should be discoverable");
    if (root.empty()) return EXIT_FAILURE;

    const fs::path glbz_path =
        root / "assets" / "pokemon_attend" / "pokemon_models" / "pm0087_00_Dewgong.glbz";
    std::vector<std::uint8_t> restored;
    std::string error;
    expect(
        pr::gameplay::attend::rendering::loadAttendPokemonAssetBytes(
            glbz_path.string(), restored, &error),
        "compiled Dewgong should decompress and pass its GLBZ hash: " + error);
    expect(restored.size() >= 4 && restored[0] == 'g' && restored[1] == 'l' &&
               restored[2] == 'T' && restored[3] == 'F',
           "compiled Dewgong should restore raw GLB bytes");
    expect(pr::sha256HexLowercase(restored) ==
               "512bc6a39ebbfd849c2b575206d7af8d5ee619c272983a9b822c59bf9a88368a",
           "compiled Dewgong should restore the exact RAE source GLB hash");

    const fs::path raw_path = fs::temp_directory_path() / "pr_dewgong_glbz_roundtrip.glb";
    {
        std::ofstream output(raw_path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(restored.data()),
                     static_cast<std::streamsize>(restored.size()));
    }

    const auto compressed_model =
        pr::gameplay::attend::rendering::loadAttendPokemonModel(glbz_path.string(), &error);
    const auto raw_model =
        pr::gameplay::attend::rendering::loadAttendPokemonModel(raw_path.string(), &error);
    std::error_code remove_error;
    fs::remove(raw_path, remove_error);

    expect(compressed_model.valid, "compiled Dewgong should load as a renderable Attend model");
    expect(raw_model.valid, "the restored raw Dewgong GLB should still load normally");
    expect(compressed_model.nodes.size() == raw_model.nodes.size(),
           "GLBZ and raw GLB should expose identical node counts");
    expect(compressed_model.primitives.size() == raw_model.primitives.size(),
           "GLBZ and raw GLB should expose identical primitive counts");
    expect(compressed_model.materials.size() == raw_model.materials.size(),
           "GLBZ and raw GLB should expose identical material counts");
    expect(compressed_model.animations.size() == raw_model.animations.size(),
           "GLBZ and raw GLB should expose identical animation counts");
    expect(compressed_model.texture_variants.size() == raw_model.texture_variants.size(),
           "GLBZ and raw GLB should preserve normal/shiny variants");
    expect(compressed_model.form_variants.size() == raw_model.form_variants.size(),
           "GLBZ and raw GLB should preserve form variants");

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
