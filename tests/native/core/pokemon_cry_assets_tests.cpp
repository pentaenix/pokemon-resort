#include "core/app/audio/PokemonCryPlayer.hpp"
#include "core/app/frame/AppFrameRequests.hpp"
#include "core/assets/PokemonCryAssets.hpp"

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
        if (fs::exists(current / "assets" / "pokemon" / "cries")) {
            return current;
        }
        const fs::path parent = current.parent_path();
        if (parent == current) break;
        current = parent;
    }
    throw TestFailure("Could not locate repository root from " + fs::current_path().string());
}

void testResolvesExistingCryBySpeciesId() {
    const pr::PokemonCryAssets cries(repositoryRoot().string());
    const pr::ResolvedPokemonCry dewgong = cries.resolveBySpeciesId(87);
    expect(dewgong.found, "Dewgong cry should resolve from the shared Pokemon cry folder");
    expect(dewgong.relative_path == "assets/pokemon/cries/87.ogg",
           "Dewgong cry should resolve to the species-numbered OGG asset");
}

void testMissingCryFailsSilently() {
    const pr::PokemonCryAssets cries(repositoryRoot().string());
    const pr::ResolvedPokemonCry missing = cries.resolveBySpeciesId(-1);
    expect(!missing.found && missing.relative_path.empty(), "invalid species id should not resolve a cry");
}

void testPmModelStemSpeciesIdParsing() {
    expect(pr::PokemonCryAssets::speciesIdFromPmModelStem("pm0087_00_Dewgong") == 87,
           "RAE-style pm#### model stems should expose the National Dex species id");
    expect(pr::PokemonCryAssets::speciesIdFromPmModelStem("dewgong") == 0,
           "non-pm model stems should not guess a species id");
}

void testCryPlayerQueuesOneShotSfx() {
    pr::AppFrameRequests frame_requests;
    const pr::PokemonCryPlayer cries(repositoryRoot().string());
    expect(cries.requestCryForSpeciesId(frame_requests, 87), "Dewgong cry playback should queue a one-shot SFX");
    const pr::AppSfxRequests sfx = frame_requests.consumeSfxRequests();
    expect(sfx.one_shot_sfx_paths.size() == 1, "cry playback should queue exactly one SFX path");
    expect(sfx.one_shot_sfx_paths.front() == "assets/pokemon/cries/87.ogg",
           "cry playback should queue the resolved species cry path");
}

void testCryPlayerMissingAssetIsSilent() {
    pr::AppFrameRequests frame_requests;
    const pr::PokemonCryPlayer cries(repositoryRoot().string());
    expect(!cries.requestCryForSpeciesId(frame_requests, -1), "missing cry playback should report no request");
    expect(frame_requests.consumeSfxRequests().one_shot_sfx_paths.empty(),
           "missing cry playback should not queue an SFX path");
}

} // namespace

int main() {
    try {
        testResolvesExistingCryBySpeciesId();
        testMissingCryFailsSilently();
        testPmModelStemSpeciesIdParsing();
        testCryPlayerQueuesOneShotSfx();
        testCryPlayerMissingAssetIsSilent();
    } catch (const std::exception& e) {
        std::cerr << "pokemon_cry_assets_tests failed: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
