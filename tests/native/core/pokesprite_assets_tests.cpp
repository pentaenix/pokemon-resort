#include "core/assets/PokeSpriteAssets.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace {

struct TestFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure(message);
    }
}

fs::path repositoryRoot() {
    fs::path current = fs::current_path();
    while (!current.empty()) {
        if (fs::exists(current / "assets" / "pokesprite" / "data" / "misc.json")) {
            return current;
        }
        current = current.parent_path();
    }
    throw TestFailure("Could not locate repository root from " + fs::current_path().string());
}

void testPokerusAliasesResolveToMiscAssets() {
    const fs::path root = repositoryRoot();
    const auto assets = pr::PokeSpriteAssets::create(root.string());

    const pr::ResolvedMiscIcon infected = assets->resolveMiscIcon("special-attribute", "infected");
    expect(!infected.used_fallback, "infected Pokérus alias should not use fallback");
    expect(infected.relative_path == "misc/special-attribute/pokerus.png",
           "infected Pokérus alias should resolve to pokerus.png");

    const pr::ResolvedMiscIcon cured = assets->resolveMiscIcon("special-attribute", "cured");
    expect(!cured.used_fallback, "cured Pokérus alias should not use fallback");
    expect(cured.relative_path == "misc/special-attribute/pokerus-cured.png",
           "cured Pokérus alias should resolve to pokerus-cured.png");
}

void testTypeIconsDoNotFallThroughToTeraTypes() {
    const fs::path root = repositoryRoot();
    const auto assets = pr::PokeSpriteAssets::create(root.string());

    const pr::ResolvedMiscIcon water = assets->resolveMiscIcon("types", "water");
    expect(!water.used_fallback, "water type should resolve to a normal type icon");
    expect(water.relative_path == "misc/types/gen8/water.png", "water type should resolve to the gen8 type asset");

    const pr::ResolvedMiscIcon missing_type = assets->resolveMiscIcon("types", "missing");
    expect(missing_type.used_fallback, "unknown normal type keys should use the generic unknown fallback");
    expect(missing_type.relative_path != "misc/types/tera/tera-unknown.png",
           "normal type lookup should not fall through to tera type assets");
}

} // namespace

int main() {
    try {
        testPokerusAliasesResolveToMiscAssets();
        testTypeIconsDoNotFallThroughToTeraTypes();
    } catch (const std::exception& e) {
        std::cerr << "pokesprite_assets_tests failed: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
