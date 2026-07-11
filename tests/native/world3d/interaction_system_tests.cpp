#include "gameplay/world3d/interactions/InteractionSequence.hpp"
#include "gameplay/world3d/interactions/InteractionText.hpp"
#include "gameplay/world3d/characters/SpriteSheetAnimator.hpp"
#include "gameplay/world3d/npc/ResortPokemonSpawnConfig.hpp"
#include "gameplay/world3d/scripts/OverworldScript.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

namespace {

struct TestFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure(message);
    }
}

void testTextSelectorPrefersMostSpecificAndCooldownIsGlobal() {
    using namespace pr::gameplay::world3d::interactions;
    InteractionTextCatalog catalog{};
    catalog.entries.push_back({"water_pool_grass", "{SPECIES} splashes happily.", {"POKEMON", "TYPE_WATER", "POOL", "GRASS_TILE"}, 1, 30.0});
    catalog.entries.push_back({"water_generic", "{SPECIES} looks refreshed.", {"POKEMON", "TYPE_WATER"}, 1, 0.0});
    catalog.entries.push_back({"generic", "Hello.", {}, 1, 0.0});

    InteractionTextContext context{};
    context.tags = {
        normalizeInteractionTag("POKEMON"),
        normalizeInteractionTag("TYPE_WATER"),
        normalizeInteractionTag("POOL"),
        normalizeInteractionTag("GRASS_TILE"),
    };
    context.variables["SPECIES"] = "Psyduck";
    InteractionTextCooldowns cooldowns;
    std::mt19937 rng{7};

    const InteractionTextSelection first = selectInteractionText(catalog, context, cooldowns, 10.0, rng);
    expect(first.found, "specific interaction text should be selected");
    expect(first.id == "water_pool_grass", "selector should prefer the most specific matching text");
    expect(first.body == "Psyduck splashes happily.", "selector should render template variables");

    const InteractionTextSelection second = selectInteractionText(catalog, context, cooldowns, 11.0, rng);
    expect(second.found, "selector should fall back after global cooldown");
    expect(second.id == "water_generic", "global cooldown should suppress the already used specific text");
}

void testTextTemplatesSupportAuthoringVariables() {
    using namespace pr::gameplay::world3d::interactions;
    InteractionTextContext context{};
    context.variables = {
        {"NAME", "Pikachu"},
        {"SPECIES", "Pikachu"},
        {"ROLE", "Pokemon"},
        {"TILE", "ground"},
        {"PLAYER_NAME", "Player"},
    };
    const std::string text = renderInteractionTextTemplate(
        "{NAME} the {SPECIES} waits with {PLAYER_NAME} on {TILE} as a {ROLE}. {FORM}",
        context.variables);
    expect(text == "Pikachu the Pikachu waits with Player on ground as a Pokemon. ",
        "template variables should render known values and clear unavailable future values");
}

void testInteractionDefaultsAndSizeMapping() {
    using namespace pr::gameplay::world3d::interactions;
    InteractionSequenceController controller;
    controller.begin(InteractionTargetKind::Pokemon, defaultInteractionBehaviorCatalog());
    const InteractionAction* action = controller.currentAction();
    expect(action && action->kind == InteractionActionKind::FacePlayer,
        "pokemon default interaction should first face the player");
    controller.advance();
    action = controller.currentAction();
    expect(action && action->kind == InteractionActionKind::PokemonInteractionSession,
        "pokemon default interaction should start a pokemon session before text");
    controller.advance();
    action = controller.currentAction();
    expect(action && action->kind == InteractionActionKind::TextFree,
        "pokemon default interaction should request free text");

    expect(interactionActivityIdForPokemonSize("") == "small_interact",
        "missing pokemonSize should map to small interaction");
    expect(interactionActivityIdForPokemonSize("medium") == "medium_interact",
        "medium pokemonSize should map to medium interaction");
    expect(interactionActivityIdForPokemonSize("large") == "large_interact",
        "large pokemonSize should map to large interaction");
    expect(interactionActivityIdForPokemonSize("huge") == "small_interact",
        "unknown pokemonSize should map to small interaction");
    expect(!interactionActivityIdForPokemonSize("human"),
        "human size profile should use behavior-only interactions without a player activity");
}

void testActivitySessionAnimatorLifecycle() {
    using namespace pr::gameplay::world3d;
    CharacterSpriteDefinition def{};
    def.columns = 4;
    def.rows = 4;
    def.frame_width = 16;
    def.frame_height = 16;
    def.idle = CharacterAnimationDef{{0}, 100};
    def.activity_sessions["small_interact"] = CharacterActivitySessionDef{
        CharacterAnimationDef{{1}, 10},
        CharacterAnimationDef{{2, 3}, 10},
        CharacterAnimationDef{{0}, 10},
        true};
    characters::SpriteSheetAnimator animator(def);
    expect(animator.startActivitySession("small_interact"),
        "animator should start a valid activity session");
    expect(!animator.activityStayActive(), "activity should begin in enter phase");
    animator.update(0.011);
    expect(animator.activityStayActive(), "single-frame enter should advance to stay after its frame time");
    const SDL_Rect stay_rect = animator.sourceRect();
    expect(stay_rect.x == 2 * def.frame_width, "stay phase should render the stay animation frame");
    animator.update(0.011);
    const SDL_Rect loop_rect = animator.sourceRect();
    expect(loop_rect.x == 3 * def.frame_width, "stay phase should loop through stay frames");
    animator.requestActivityExit();
    expect(animator.activitySessionActive(), "exit request should keep the session active until exit finishes");
    animator.update(0.011);
    expect(animator.activityFinished(), "single-frame exit should finish after its frame time");
}

void testResortPokemonSpawnConfigCapsTheWholeRoster() {
    namespace fs = std::filesystem;
    using namespace pr::gameplay::world3d::npc;
    const fs::path root = fs::temp_directory_path() / "pokemon_resort_spawn_config_test";
    fs::remove_all(root);
    fs::create_directories(root / "config" / "gameplay" / "world3d");
    {
        std::ofstream file(root / "config" / "gameplay" / "world3d" / "pokemon_spawns.json");
        file << R"({"resortBoxRoster":{"enabled":true,"profileId":"test","boxId":2,"maxPokemon":3}})";
    }
    const ResortPokemonSpawnConfig config = loadResortPokemonSpawnConfig(root.string());
    expect(config.enabled, "spawn roster config should preserve enabled state");
    expect(config.profile_id == "test", "spawn roster config should select the configured profile");
    expect(config.box_id == 2, "spawn roster config should select the configured box");
    expect(config.max_pokemon == 3, "spawn roster cap should include follower and roamers");
    fs::remove_all(root);
}

void testScriptSelectionUsesTagsPriorityWeightAndCooldown() {
    using namespace pr::gameplay::world3d::scripts;
    ScriptCatalog catalog{};
    catalog.scripts = {
        {"pokemon_idle", ScriptKind::Idle, {"POKEMON"}, "PLAYER_IDLE", 1, 1, 10.0, {{"POKEMON"}, {}, "PLAYER", 2}, {{ScriptActionKind::Jump}}},
        {"pokemon_idle_fallback", ScriptKind::Idle, {"POKEMON"}, "PLAYER_IDLE", 0, 1, 0.0, {{"POKEMON"}, {}, {}, -1}, {{ScriptActionKind::Wander}}},
    };
    ScriptContext context{};
    context.tags.insert("POKEMON");
    context.nearest_tag_distance_tiles["PLAYER"] = 1;
    ScriptCooldowns cooldowns;
    std::mt19937 rng{4};
    const OverworldScript* first = selectScript(catalog, ScriptKind::Idle, context, cooldowns, 0.0, rng);
    expect(first && first->id == "pokemon_idle", "script selection should prefer highest matching priority");
    const OverworldScript* second = selectScript(catalog, ScriptKind::Idle, context, cooldowns, 1.0, rng);
    expect(second && second->id == "pokemon_idle_fallback", "script cooldown should suppress the selected script");
    context.nearest_tag_distance_tiles["PLAYER"] = 3;
    ScriptCooldowns fresh_cooldowns;
    const OverworldScript* distant = selectScript(catalog, ScriptKind::Idle, context, fresh_cooldowns, 0.0, rng);
    expect(distant && distant->id == "pokemon_idle_fallback", "CLOSE_TO should reject distant actors");

    ScriptCatalog invalid{};
    invalid.scripts.push_back({"bad", ScriptKind::Interaction, {}, "ACCEPT", 0, 1, 0.0, {}, {{ScriptActionKind::Cry}}});
    expect(!validateScriptCatalog(invalid).empty(), "scripts with unavailable actions should fail validation");
}

void testScriptCatalogLoadsIndividualJsonFiles() {
    namespace fs = std::filesystem;
    using namespace pr::gameplay::world3d::scripts;
    const fs::path root = fs::temp_directory_path() / "pokemon_resort_script_catalog_test";
    fs::remove_all(root);
    const fs::path scripts = root / "config" / "gameplay" / "world3d" / "scripts" / "interactions";
    fs::create_directories(scripts);
    {
        std::ofstream index(scripts.parent_path() / "script_catalog.json");
        index << R"({"scripts":[{"path":"interactions/test.json"}]})";
        std::ofstream script(scripts / "test.json");
        script << R"({"id":"test_interaction","kind":"interaction","targetGates":["POKEMON"],"priority":2,"weight":3,"cooldownSeconds":4,"when":{"allTags":["POKEMON"],"closeTo":{"tag":"PLAYER","maxTiles":2}},"actions":[{"action":"TEXT_FREE"}]})";
    }
    std::vector<ScriptValidationIssue> issues;
    const ScriptCatalog catalog = loadScriptCatalog(root.string(), &issues);
    expect(issues.empty(), "valid script catalog should load without validation issues");
    expect(catalog.scripts.size() == 1 && catalog.scripts.front().id == "test_interaction",
        "catalog should load its referenced script file");
    fs::remove_all(root);
}

} // namespace

int main() {
    try {
        testTextSelectorPrefersMostSpecificAndCooldownIsGlobal();
        testTextTemplatesSupportAuthoringVariables();
        testInteractionDefaultsAndSizeMapping();
        testActivitySessionAnimatorLifecycle();
        testResortPokemonSpawnConfigCapsTheWholeRoster();
        testScriptSelectionUsesTagsPriorityWeightAndCooldown();
        testScriptCatalogLoadsIndividualJsonFiles();
    } catch (const TestFailure& failure) {
        std::cerr << "[FAIL] " << failure.what() << '\n';
        return EXIT_FAILURE;
    } catch (const std::exception& ex) {
        std::cerr << "[ERROR] " << ex.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "[PASS] interaction system tests\n";
    return EXIT_SUCCESS;
}
