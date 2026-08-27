#include "gameplay/world3d/interactions/InteractionSequence.hpp"
#include "gameplay/world3d/interactions/InteractionText.hpp"
#include "gameplay/world3d/characters/SpriteSheetAnimator.hpp"
#include "gameplay/world3d/npc/ResortPokemonSpawnConfig.hpp"
#include "gameplay/world3d/scripts/OverworldScript.hpp"
#include "ui/transitions/ScreenTransition.hpp"

#include <algorithm>
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

pr::gameplay::world3d::scripts::OverworldScript interactionScript(
    std::string id, std::string gate, pr::gameplay::world3d::scripts::ScriptAction action,
    int priority = 0) {
    using namespace pr::gameplay::world3d::scripts;
    OverworldScript script{};
    script.id = std::move(id);
    script.kind = ScriptKind::Interaction;
    script.target_gates = {std::move(gate)};
    script.trigger = "ACCEPT";
    script.priority = priority;
    script.actions = {std::move(action)};
    return script;
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

void testPokemonTextStartsANewRandomCycleAfterExhaustion() {
    using namespace pr::gameplay::world3d::interactions;
    InteractionTextCatalog catalog{};
    for (const char* id : {"A", "B", "C", "D"}) {
        catalog.entries.push_back({id, id, {"POKEMON"}, 1, 999.0});
    }
    InteractionTextContext context{};
    context.tags.insert("POKEMON");
    InteractionTextCooldowns cooldowns;
    std::mt19937 rng{19};
    for (int cycle = 0; cycle < 2; ++cycle) {
        std::unordered_set<std::string> ids;
        for (int i = 0; i < 4; ++i) {
            const auto selected = selectInteractionText(catalog, context, cooldowns, 0.0, rng);
            expect(selected.found && !selected.body.empty(),
                "exhausted Pokemon text should begin another cycle instead of returning empty text");
            expect(ids.insert(selected.id).second,
                "a Pokemon text cycle should use every eligible line before repeating");
        }
        expect(ids.size() == 4, "each randomized Pokemon text cycle should exhaust the eligible pool");
    }
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
        script << R"({"id":"test_interaction","kind":"interaction","targetGates":["POKEMON"],"priority":2,"weight":3,"cooldownSeconds":4,"when":{"allTags":["POKEMON"],"closeTo":{"tag":"PLAYER","maxTiles":2}},"actions":[{"action":"DISABLE_ATTEND"},{"action":"TEXT_FREE"},{"action":"EXIT_INTERACTION"}]})";
    }
    std::vector<ScriptValidationIssue> issues;
    const ScriptCatalog catalog = loadScriptCatalog(root.string(), &issues);
    expect(issues.empty(), "valid script catalog should load without validation issues");
    expect(catalog.scripts.size() == 1 && catalog.scripts.front().id == "test_interaction",
        "catalog should load its referenced script file");
    expect(catalog.scripts.front().actions.size() == 3 &&
            catalog.scripts.front().actions.front().kind == ScriptActionKind::DisableAttend &&
            catalog.scripts.front().actions.back().kind == ScriptActionKind::ExitInteraction,
        "catalog should parse authored Attend and interaction-exit steps");
    fs::remove_all(root);
}

void testDoorScriptActionsLoadFromCatalog() {
    namespace fs = std::filesystem;
    using namespace pr::gameplay::world3d::scripts;
    const fs::path root = fs::temp_directory_path() / "pokemon_resort_door_script_catalog_test";
    fs::remove_all(root);
    const fs::path scripts = root / "config" / "gameplay" / "world3d" / "scripts" / "doors";
    fs::create_directories(scripts);
    {
        std::ofstream index(scripts.parent_path() / "script_catalog.json");
        index << R"({"scripts":[{"path":"doors/enter.json"}]})";
        std::ofstream script(scripts / "enter.json");
        script << R"({"id":"door_enter","kind":"door","targetGates":["DOOR"],"trigger":"MOVE_TOWARD","actions":[{"action":"PLAY_TILE_ANIMATION","value":"open"},{"action":"TRANSITION_CLOSE"},{"action":"TELEPORT_TO_LINK"},{"action":"TRANSITION_OPEN"},{"action":"MOVE_PLAYER","direction":"forward","tiles":1,"durationSeconds":0.4}]})";
    }
    std::vector<ScriptValidationIssue> issues;
    const ScriptCatalog catalog = loadScriptCatalog(root.string(), &issues);
    expect(issues.empty(), "valid door script should load without validation issues");
    expect(catalog.scripts.size() == 1 && catalog.scripts.front().kind == ScriptKind::Door,
        "catalog should preserve the door script kind");
    const auto& actions = catalog.scripts.front().actions;
    expect(actions.size() == 5 &&
            actions[0].kind == ScriptActionKind::PlayTileAnimation &&
            actions[1].kind == ScriptActionKind::TransitionClose &&
            actions[2].kind == ScriptActionKind::TeleportToLink &&
            actions[3].kind == ScriptActionKind::TransitionOpen &&
            actions[4].kind == ScriptActionKind::MovePlayer && actions[4].tiles == 1 &&
            actions[4].use_current_facing && actions[4].duration_seconds == 0.4,
        "door script should preserve its ordered travel action vocabulary");
    fs::remove_all(root);
}

void testRuntimeInteriorExitWalksBeforeTransfer() {
    namespace fs = std::filesystem;
    using namespace pr::gameplay::world3d::scripts;
    fs::path root = fs::current_path();
    if (!fs::exists(root / "config/gameplay/world3d/scripts/script_catalog.json")) {
        root = root.parent_path();
    }
    std::vector<ScriptValidationIssue> issues;
    const ScriptCatalog catalog = loadScriptCatalog(root.string(), &issues);
    const auto own_issue = std::find_if(issues.begin(), issues.end(),
        [](const ScriptValidationIssue& issue) {
            return issue.script_id == "interior_exit_step_then_transfer";
        });
    expect(own_issue == issues.end(), own_issue == issues.end()
        ? std::string{}
        : "interior threshold exit script is invalid: " + own_issue->message);
    const auto found = std::find_if(catalog.scripts.begin(), catalog.scripts.end(),
        [](const OverworldScript& script) {
            return script.id == "interior_exit_step_then_transfer";
        });
    expect(found != catalog.scripts.end() && found->valid,
        "runtime catalog should expose the interior threshold exit script");
    const auto& actions = found->actions;
    expect(actions.size() == 6U &&
            actions[0].kind == ScriptActionKind::MovePlayer &&
            actions[0].tiles == 1 &&
            actions[1].kind == ScriptActionKind::TransitionClose &&
            actions[2].kind == ScriptActionKind::TeleportToLink &&
            actions[3].kind == ScriptActionKind::TransitionOpen &&
            actions[4].kind == ScriptActionKind::MovePlayer,
        "interior exit should finish its south threshold step before closing the iris");
}

void testInteractionSourceSplitAndFallbacks() {
    using namespace pr::gameplay::world3d::interactions;
    using namespace pr::gameplay::world3d::scripts;
    ScriptCatalog catalog{};
    catalog.scripts.push_back(interactionScript("pokemon_authored", "POKEMON", {ScriptActionKind::TextLiteral, {}, "Pokemon script"}));
    catalog.scripts.push_back(interactionScript("npc_authored", "CHARACTER", {ScriptActionKind::TextLiteral, {}, "NPC script"}));
    ScriptCooldowns cooldowns;
    std::mt19937 rng{11};

    InteractionSourceRequest pokemon{};
    pokemon.target_kind = InteractionTargetKind::Pokemon;
    pokemon.npc_mode = NpcInteractionMode::DirectDialogue; // Must be ignored for Pokemon.
    pokemon.script_context.tags.insert("POKEMON");
    auto resolved = resolveInteractionSource(catalog, pokemon, cooldowns, 0.0, rng);
    expect(resolved.entered_script_path && resolved.script_id == "pokemon_authored",
        "Pokemon interactions should always enter the script interaction path");

    OverworldScript after_attend{};
    after_attend.id = "pokemon_after_attend";
    after_attend.kind = ScriptKind::Interaction;
    after_attend.target_gates = {"POKEMON"};
    after_attend.trigger = "ACCEPT";
    after_attend.priority = 100;
    after_attend.when.all_tags = {"POKEMON", "AFTER_POKEMON_ATTEND"};
    ScriptAction jump{};
    jump.kind = ScriptActionKind::Jump;
    jump.height_pixels = 12;
    ScriptAction wait{};
    wait.kind = ScriptActionKind::Wait;
    wait.duration_seconds = 0.5;
    ScriptAction disable_attend{};
    disable_attend.kind = ScriptActionKind::DisableAttend;
    ScriptAction exit_interaction{};
    exit_interaction.kind = ScriptActionKind::ExitInteraction;
    after_attend.actions = {disable_attend, jump, wait, exit_interaction};
    catalog.scripts.push_back(after_attend);
    pokemon.script_context.tags.insert("AFTER_POKEMON_ATTEND");
    resolved = resolveInteractionSource(catalog, pokemon, cooldowns, 0.5, rng);
    expect(resolved.script_id == "pokemon_after_attend" && resolved.behavior.actions.size() == 4 &&
            resolved.behavior.actions[0].kind == InteractionActionKind::DisableAttend &&
            resolved.behavior.actions[1].kind == InteractionActionKind::Jump &&
            resolved.behavior.actions[1].height_pixels == 12 &&
            resolved.behavior.actions[2].kind == InteractionActionKind::Wait &&
            resolved.behavior.actions[2].duration_seconds == 0.5 &&
            resolved.behavior.actions[3].kind == InteractionActionKind::ExitInteraction,
        "post-attend scripts should preserve Attend, JUMP, WAIT, and exit steps");

    ScriptCatalog empty{};
    resolved = resolveInteractionSource(empty, pokemon, cooldowns, 1.0, rng);
    expect(resolved.entered_script_path && resolved.used_fallback,
        "Pokemon without an eligible script should stay in the interaction runtime and use fallback");
    expect(resolved.behavior.actions.size() == 3 &&
        resolved.behavior.actions[1].kind == InteractionActionKind::PokemonInteractionSession,
        "Pokemon fallback should preserve face, session animation, and free-text behavior");

    InteractionSourceRequest npc{};
    npc.target_kind = InteractionTargetKind::Character;
    npc.script_context.tags.insert("CHARACTER");
    resolved = resolveInteractionSource(catalog, npc, cooldowns, 2.0, rng);
    expect(!resolved.entered_script_path && resolved.behavior.actions.back().kind == InteractionActionKind::TextFree,
        "NPC direct_dialogue should use character dialogue through TEXT_FREE");

    npc.npc_mode = NpcInteractionMode::Scripted;
    resolved = resolveInteractionSource(catalog, npc, cooldowns, 3.0, rng);
    expect(resolved.entered_script_path && resolved.script_id == "npc_authored" &&
        resolved.behavior.actions.front().kind == InteractionActionKind::TextLiteral,
        "NPC scripted mode should run an eligible interaction script");

    resolved = resolveInteractionSource(empty, npc, cooldowns, 4.0, rng);
    expect(resolved.used_fallback && resolved.behavior.actions.back().kind == InteractionActionKind::TextFree,
        "NPC scripted mode without a script should fall back to direct character dialogue");
    expect(npcInteractionModeFromMetadata("") == NpcInteractionMode::DirectDialogue &&
        npcInteractionModeFromMetadata("unknown") == NpcInteractionMode::DirectDialogue,
        "legacy and invalid NPC interaction modes should default to direct_dialogue");
}

void testRuntimeScriptPrecedenceAndCharbinCompatibility() {
    using namespace pr::gameplay::world3d::interactions;
    using namespace pr::gameplay::world3d::scripts;
    ScriptCatalog catalog{};
    catalog.scripts.push_back(interactionScript("npc_selected", "CHARACTER", {ScriptActionKind::TextLiteral, {}, "selected"}, 10));
    catalog.scripts.push_back(interactionScript("runtime_assigned", "CHARACTER", {ScriptActionKind::TextLiteral, {}, "runtime"}, 0));
    InteractionSourceRequest npc{};
    npc.target_kind = InteractionTargetKind::Character;
    npc.npc_mode = NpcInteractionMode::Scripted;
    npc.runtime_script_id = "runtime_assigned";
    npc.script_context.tags.insert("CHARACTER");
    ScriptCooldowns cooldowns;
    std::mt19937 rng{3};
    const auto resolved = resolveInteractionSource(catalog, npc, cooldowns, 0.0, rng);
    expect(resolved.script_id == "runtime_assigned", "runtime-assigned interaction script should have future precedence");

}

void testReusableBlackIrisTransitionLifecycle() {
    pr::transitions::TransitionStyle style{};
    style.duration_seconds = 0.2;
    pr::transitions::ScreenTransition transition;
    transition.startClosing(style);
    transition.update(0.1);
    expect(transition.closedAmount() > 0.4 && transition.closedAmount() < 0.6,
        "black iris should close according to configured duration");
    transition.update(0.1);
    expect(!transition.consumeClosed() && transition.closedAmount() == 1.0,
        "black iris should hold one fully closed presentation frame before scene switching");
    transition.update(0.0);
    expect(transition.consumeClosed(), "transition should emit one closed event after its fully closed frame");
    transition.update(0.1);
    expect(!transition.consumeClosed(), "a held closed transition should not repeat its scene-switch event");
    transition.startOpening(style);
    transition.update(0.2);
    expect(!transition.active() && transition.closedAmount() == 0.0,
        "return transition should reopen at the same configured speed");
}

} // namespace

int main() {
    try {
        testTextSelectorPrefersMostSpecificAndCooldownIsGlobal();
        testTextTemplatesSupportAuthoringVariables();
        testPokemonTextStartsANewRandomCycleAfterExhaustion();
        testInteractionDefaultsAndSizeMapping();
        testActivitySessionAnimatorLifecycle();
        testResortPokemonSpawnConfigCapsTheWholeRoster();
        testScriptSelectionUsesTagsPriorityWeightAndCooldown();
        testScriptCatalogLoadsIndividualJsonFiles();
        testDoorScriptActionsLoadFromCatalog();
        testRuntimeInteriorExitWalksBeforeTransfer();
        testInteractionSourceSplitAndFallbacks();
        testRuntimeScriptPrecedenceAndCharbinCompatibility();
        testReusableBlackIrisTransitionLifecycle();
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
