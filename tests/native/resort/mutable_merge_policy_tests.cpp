#include "resort/domain/ImportedPokemon.hpp"
#include "resort/domain/PokemonMergeFieldPolicy.hpp"
#include "resort/domain/ResortTypes.hpp"
#include "resort/services/MirrorReturnAnalysis.hpp"
#include "resort/services/PokemonMergeService.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

struct TestFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void expect(bool ok, const std::string& msg) {
    if (!ok) {
        throw TestFailure(msg);
    }
}

void testCleanReturnHasNoIdentityFlags() {
    pr::resort::ResortPokemon canon{};
    canon.id.pkrid = "pkr_test";
    canon.hot.species_id = 1;
    canon.hot.level = 10;
    canon.hot.exp = 1000;
    canon.hot.ot_name = "Ash";
    canon.hot.tid16 = 111;
    canon.hot.sid16 = 222;
    canon.hot.pid = 0x12345678u;

    pr::resort::ImportedPokemon imp{};
    imp.hot = canon.hot;
    imp.hot.level = 12;
    imp.hot.exp = 2000;

    const auto r = pr::resort::MirrorReturnAnalysis::analyzePreMerge(canon, imp);
    expect(!r.quarantine_recommended, "unexpected quarantine");
    for (const auto& f : r.flags) {
        expect(
            f != "pid_changed" && f != "tid16_changed" && f != "sid16_changed",
            "unexpected identity flag: " + f);
    }
}

void testMirrorReturnPreservesEncounterProvenance() {
    pr::resort::ResortPokemon canon{};
    canon.id.pkrid = "pkr_test";
    canon.hot.species_id = 289;
    canon.hot.form_id = 0;
    canon.hot.level = 36;
    canon.hot.exp = 58673u;
    canon.hot.met_location_id = 47;
    canon.hot.met_level = 20;
    canon.hot.ball_id = 4;
    canon.hot.origin_game = 3;
    canon.hot.ot_name = "Red";
    canon.hot.move_ids[0] = 10;
    canon.hot.move_ids[1] = 116;
    canon.hot.move_pp[0] = 20;
    canon.hot.move_pp[1] = 10;

    pr::resort::ImportedPokemon imp{};
    imp.source_game = 64;
    imp.format_name = "pk4";
    imp.hot = canon.hot;
    imp.hot.level = 39;
    imp.hot.exp = 74150u;
    imp.hot.met_location_id = 9999;
    imp.hot.met_level = 99;
    imp.hot.ball_id = 99;

    pr::resort::PokemonMergeService merge;
    const auto r = merge.mergeImported(
        canon,
        imp,
        12345,
        pr::resort::ImportMergeKind::MirrorReturnGameplaySync);
    expect(r.changed, "expected gameplay merge");
    expect(canon.hot.level == 39, "level should advance");
    expect(canon.hot.exp == 74150u, "exp should advance");
    expect(canon.hot.met_location_id == 47, "met_location_id must stay Resort-owned");
    expect(canon.hot.met_level == 20, "met_level must stay Resort-owned");
    expect(*canon.hot.ball_id == 4, "ball_id must stay Resort-owned");
}

void testMirrorReturnMergesWarmCatalog() {
    pr::resort::ResortPokemon canon{};
    canon.id.pkrid = "pkr_test";
    canon.hot.species_id = 25;
    canon.hot.level = 20;
    canon.hot.exp = 8000u;
    canon.warm.json = R"json({"schema_version":1,"ribbons":["old"]})json";

    pr::resort::ImportedPokemon imp{};
    imp.source_game = 64;
    imp.format_name = "pk4";
    imp.hot = canon.hot;
    imp.warm_json =
        R"json({"schema_version":1,"resort_catalog":{"schema":1,"ribbon_flags":{"RibbonChampion":true}}})json";

    pr::resort::PokemonMergeService merge;
    const auto r = merge.mergeImported(
        canon,
        imp,
        12345,
        pr::resort::ImportMergeKind::MirrorReturnGameplaySync);
    expect(r.changed, "warm catalog merge should bump revision");
    expect(canon.warm.json.find("RibbonChampion") != std::string::npos, "merged warm should contain new catalog keys");
    expect(canon.warm.json.find("old") != std::string::npos, "merged warm should preserve prior arrays");
}

void testMirrorReturnPreservesAbilityWhenLeveledNotEvolved() {
    pr::resort::ResortPokemon canon{};
    canon.id.pkrid = "pkr_test";
    canon.hot.species_id = 25;
    canon.hot.form_id = 0;
    canon.hot.level = 20;
    canon.hot.exp = 8000u;
    canon.hot.ability_id = 9;
    canon.hot.ability_slot = 1;

    pr::resort::ImportedPokemon imp{};
    imp.source_game = 64;
    imp.format_name = "pk4";
    imp.hot = canon.hot;
    imp.hot.level = 22;
    imp.hot.exp = 9500u;
    imp.hot.ability_id = 99;
    imp.hot.ability_slot = 2;

    pr::resort::PokemonMergeService merge;
    merge.mergeImported(canon, imp, 1, pr::resort::ImportMergeKind::MirrorReturnGameplaySync);
    expect(*canon.hot.ability_id == 9, "ability_id must stay canonical when not evolved");
    expect(*canon.hot.ability_slot == 1, "ability_slot must stay canonical when not evolved");
}

void testMirrorReturnPreservesGen12DvEvenWhenEvolved() {
    constexpr std::uint16_t kCanonDv = static_cast<std::uint16_t>(0xC8B4);
    pr::resort::ResortPokemon canon{};
    canon.id.pkrid = "pkr_test";
    canon.hot.species_id = 12;
    canon.hot.form_id = 0;
    canon.hot.level = 16;
    canon.hot.dv16 = kCanonDv;

    pr::resort::ImportedPokemon imp{};
    imp.source_game = 39;
    imp.format_name = "pk2";
    imp.hot = canon.hot;
    imp.hot.species_id = 15;
    imp.hot.level = 20;
    imp.hot.dv16 = static_cast<std::uint16_t>(0x1111);

    pr::resort::PokemonMergeService merge;
    merge.mergeImported(canon, imp, 1, pr::resort::ImportMergeKind::MirrorReturnGameplaySync);
    expect(canon.hot.species_id == 15, "species should reflect evolution");
    expect(canon.hot.dv16 && *canon.hot.dv16 == kCanonDv, "packed Gen I–II DVs stay Resort-static");
}

void testMirrorReturnWarmMergeStripsCartFormatKey() {
    pr::resort::ResortPokemon canon{};
    canon.id.pkrid = "pkr_test";
    canon.hot.species_id = 1;
    canon.hot.level = 5;
    canon.warm.json = R"json({"schema_version":1,"format":"pk3","keeper":true})json";

    pr::resort::ImportedPokemon imp{};
    imp.hot = canon.hot;
    imp.warm_json = R"json({"schema_version":1,"format":"pk4","resort_catalog":{"schema":1,"x":1}})json";

    pr::resort::PokemonMergeService merge;
    merge.mergeImported(canon, imp, 1, pr::resort::ImportMergeKind::MirrorReturnGameplaySync);
    expect(canon.warm.json.find("\"format\":\"pk3\"") != std::string::npos,
           "canonical storage format label must not be overwritten by returning cart read");
    expect(canon.warm.json.find("resort_catalog") != std::string::npos,
           "resort_catalog from mirror should still merge");
}

void testMirrorReturnWarmMergePreservesStaticFieldCatalog() {
    pr::resort::ResortPokemon canon{};
    canon.id.pkrid = "pkr_test";
    canon.hot.species_id = 1;
    canon.hot.level = 5;
    canon.warm.json =
        R"json({"schema_version":1,"resort_catalog":{"static_fields":{"met_location_id":1},"friendship":{"current":70}}})json";

    pr::resort::ImportedPokemon imp{};
    imp.hot = canon.hot;
    imp.warm_json =
        R"json({"schema_version":1,"resort_catalog":{"static_fields":{"met_location_id":999},"ribbon_flags":{"RibbonChampion":true}}})json";

    pr::resort::PokemonMergeService merge;
    merge.mergeImported(canon, imp, 1, pr::resort::ImportMergeKind::MirrorReturnGameplaySync);
    expect(canon.warm.json.find("\"met_location_id\":1") != std::string::npos,
           "canonical static_fields must not be replaced by returning cart static_fields");
    expect(canon.warm.json.find("999") == std::string::npos,
           "returning cart static_fields should be stripped before warm merge");
    expect(canon.warm.json.find("RibbonChampion") != std::string::npos,
           "non-static catalog keys should still merge");
}

void testStableIdentityMatchReasonRecognized() {
    expect(pr::resort::isStableIdentityMatchReasonForMirrorReturn("home_tracker"), "home_tracker");
    expect(pr::resort::isStableIdentityMatchReasonForMirrorReturn("pid_ec_tid_sid_ot"), "pid_ec");
    expect(pr::resort::isStableIdentityMatchReasonForMirrorReturn("pid_tid_sid_ot"), "pid_tid");
    expect(!pr::resort::isStableIdentityMatchReasonForMirrorReturn("no_stable_identifier_match"), "no match");
}

void testMirrorReturnWarmMergeIgnoresReturningFriendshipCatalog() {
    pr::resort::ResortPokemon canon{};
    canon.id.pkrid = "pkr_test";
    canon.hot.species_id = 1;
    canon.hot.level = 5;
    canon.warm.json =
        R"json({"schema_version":1,"resort_catalog":{"friendship":{"original_trainer":90,"current":70}}})json";

    pr::resort::ImportedPokemon imp{};
    imp.hot = canon.hot;
    imp.warm_json =
        R"json({"schema_version":1,"resort_catalog":{"friendship":{"original_trainer":5,"current":5}}})json";

    pr::resort::PokemonMergeService merge;
    merge.mergeImported(canon, imp, 1, pr::resort::ImportMergeKind::MirrorReturnGameplaySync);
    expect(canon.warm.json.find("\"original_trainer\":90") != std::string::npos,
           "canonical friendship must not be overwritten by returning cart resort_catalog.friendship");
    expect(canon.warm.json.find("\"current\":70") != std::string::npos, "canonical current friendship preserved");
}

void testMirrorReturnSanitizesIllegalMovesForOriginGeneration() {
    pr::resort::ResortPokemon canon{};
    canon.id.pkrid = "pkr_test";
    canon.hot.species_id = 25;
    canon.hot.level = 20;
    canon.hot.exp = 8000u;
    canon.hot.origin_game = 3; // Emerald → pk3 constraint gen 3
    canon.hot.move_ids[0] = 447; // Grass Knot — not representable in Gen III move table
    canon.hot.move_pp[0] = 15;
    canon.hot.move_pp_ups[0] = 0;

    pr::resort::ImportedPokemon imp{};
    imp.hot = canon.hot;
    imp.hot.level = 20;
    imp.hot.exp = 8000u;
    imp.hot.move_ids[0] = 10;
    imp.hot.move_pp[0] = 20;

    pr::resort::PokemonMergeService merge;
    merge.mergeImported(canon, imp, 1, pr::resort::ImportMergeKind::MirrorReturnGameplaySync);
    expect(canon.hot.move_ids[0] && *canon.hot.move_ids[0] == 10,
           "returning cart move should persist after a mirror leg");
    expect(canon.warm.json.find("auto_removed_moves") != std::string::npos,
           "automatically removed canonical moves should be remembered for later reteach mechanics");
    expect(canon.warm.json.find("\"move_id\":447") != std::string::npos,
           "removed move memory should include the original move id");
}

void testCrossGenMirrorReturnPreservesNicknameFlag() {
    pr::resort::ResortPokemon canon{};
    canon.id.pkrid = "pkr_test";
    canon.hot.species_id = 25;
    canon.hot.origin_game = 21;
    canon.hot.nickname = "Pikachu";
    canon.hot.is_nicknamed = false;
    canon.hot.level = 20;
    canon.hot.exp = 8000u;

    pr::resort::ImportedPokemon imp{};
    imp.source_game = 3;
    imp.format_name = "pk3";
    imp.hot = canon.hot;
    imp.hot.nickname = "PIKACHU";
    imp.hot.is_nicknamed = true;
    imp.hot.level = 25;
    imp.hot.exp = 12000u;

    pr::resort::PokemonMergeService merge;
    merge.mergeImported(canon, imp, 1, pr::resort::ImportMergeKind::MirrorReturnGameplaySync);
    expect(canon.hot.nickname == "Pikachu", "cross-gen mirror return must not promote uppercase species name to nickname");
    expect(!canon.hot.is_nicknamed, "cross-gen mirror return must preserve canonical nickname flag");
}

void testSameOriginMirrorReturnMayUpdateNicknameFlag() {
    pr::resort::ResortPokemon canon{};
    canon.id.pkrid = "pkr_test";
    canon.hot.species_id = 25;
    canon.hot.origin_game = 21;
    canon.hot.nickname = "Pikachu";
    canon.hot.is_nicknamed = false;
    canon.hot.level = 20;
    canon.hot.exp = 8000u;

    pr::resort::ImportedPokemon imp{};
    imp.source_game = 21;
    imp.format_name = "pk5";
    imp.hot = canon.hot;
    imp.hot.nickname = "Sparky";
    imp.hot.is_nicknamed = true;

    pr::resort::PokemonMergeService merge;
    merge.mergeImported(canon, imp, 1, pr::resort::ImportMergeKind::MirrorReturnGameplaySync);
    expect(canon.hot.nickname == "Sparky", "same-origin return may update nickname");
    expect(canon.hot.is_nicknamed, "same-origin return may update nickname flag");
}

void testSameOriginGen3DefaultDoesNotEraseCanonicalNickname() {
    pr::resort::ResortPokemon canon{};
    canon.id.pkrid = "pkr_test";
    canon.hot.species_id = 25;
    canon.hot.origin_game = 3;
    canon.hot.nickname = "Sparky";
    canon.hot.is_nicknamed = true;
    canon.hot.level = 20;
    canon.hot.exp = 8000u;

    pr::resort::ImportedPokemon imp{};
    imp.source_game = 3;
    imp.format_name = "pk3";
    imp.hot = canon.hot;
    imp.hot.nickname = "PIKACHU";
    imp.hot.is_nicknamed = false;
    imp.hot.level = 22;
    imp.hot.exp = 9500u;

    pr::resort::PokemonMergeService merge;
    merge.mergeImported(canon, imp, 1, pr::resort::ImportMergeKind::MirrorReturnGameplaySync);
    expect(canon.hot.nickname == "Sparky", "Gen 3 default species bytes must not erase canonical nickname");
    expect(canon.hot.is_nicknamed, "Gen 3 inferred default must not clear canonical nickname flag");
}

void testSameOriginGen3CustomNicknameMayUpdate() {
    pr::resort::ResortPokemon canon{};
    canon.id.pkrid = "pkr_test";
    canon.hot.species_id = 25;
    canon.hot.origin_game = 3;
    canon.hot.nickname = "Sparky";
    canon.hot.is_nicknamed = true;
    canon.hot.level = 20;
    canon.hot.exp = 8000u;

    pr::resort::ImportedPokemon imp{};
    imp.source_game = 3;
    imp.format_name = "pk3";
    imp.hot = canon.hot;
    imp.hot.nickname = "Zappy";
    imp.hot.is_nicknamed = true;

    pr::resort::PokemonMergeService merge;
    merge.mergeImported(canon, imp, 1, pr::resort::ImportMergeKind::MirrorReturnGameplaySync);
    expect(canon.hot.nickname == "Zappy", "Gen 3 distinct custom nickname should update on same-origin return");
    expect(canon.hot.is_nicknamed, "Gen 3 custom nickname should keep nickname flag true");
}

void testMirrorReturnRestoresHotPidFromOriginalPid() {
    pr::resort::ResortPokemon canon{};
    canon.id.pkrid = "pkr_test";
    canon.hot.species_id = 1;
    canon.hot.level = 10;
    canon.hot.pid = 0xDEADBEEFu;
    canon.original_pid = 0xCAFEBABEu;

    pr::resort::ImportedPokemon imp{};
    imp.hot = canon.hot;
    imp.hot.level = 12;

    pr::resort::PokemonMergeService merge;
    merge.mergeImported(canon, imp, 1, pr::resort::ImportMergeKind::MirrorReturnGameplaySync);
    expect(canon.hot.pid && *canon.hot.pid == 0xCAFEBABEu, "hot.pid must restore to original_pid after mirror return");
}

void testMirrorReturnPreservesCanonicalShinyWhenEvolvedCartNonShiny() {
    pr::resort::ResortPokemon canon{};
    canon.id.pkrid = "pkr_test";
    canon.hot.species_id = 25;
    canon.hot.form_id = 0;
    canon.hot.level = 30;
    canon.hot.exp = 5000u;
    canon.hot.shiny = true;

    pr::resort::ImportedPokemon imp{};
    imp.source_game = 3;
    imp.format_name = "pk3";
    imp.hot = canon.hot;
    imp.hot.species_id = 26;
    imp.hot.level = 50;
    imp.hot.exp = 120000u;
    imp.hot.shiny = false;

    pr::resort::PokemonMergeService merge;
    merge.mergeImported(canon, imp, 1, pr::resort::ImportMergeKind::MirrorReturnGameplaySync);
    expect(canon.hot.shiny == true, "canonical shiny must remain after evolved mirror return");
}

void testPidChangeQuarantines() {
    pr::resort::ResortPokemon canon{};
    canon.id.pkrid = "pkr_test";
    canon.hot.species_id = 1;
    canon.hot.level = 10;
    canon.hot.pid = 0x11111111u;

    pr::resort::ImportedPokemon imp{};
    imp.hot = canon.hot;
    imp.hot.pid = 0x22222222u;

    const auto r = pr::resort::MirrorReturnAnalysis::analyzePreMerge(canon, imp);
    expect(r.quarantine_recommended, "expected quarantine for pid change");
    bool found = false;
    for (const auto& f : r.flags) {
        if (f == "pid_changed") {
            found = true;
        }
    }
    expect(found, "expected pid_changed flag");
}

} // namespace

int main() {
    try {
        testCleanReturnHasNoIdentityFlags();
        testMirrorReturnPreservesEncounterProvenance();
        testMirrorReturnMergesWarmCatalog();
        testMirrorReturnPreservesAbilityWhenLeveledNotEvolved();
        testMirrorReturnPreservesGen12DvEvenWhenEvolved();
        testMirrorReturnWarmMergeStripsCartFormatKey();
        testMirrorReturnWarmMergePreservesStaticFieldCatalog();
        testStableIdentityMatchReasonRecognized();
        testMirrorReturnWarmMergeIgnoresReturningFriendshipCatalog();
        testMirrorReturnSanitizesIllegalMovesForOriginGeneration();
        testCrossGenMirrorReturnPreservesNicknameFlag();
        testSameOriginMirrorReturnMayUpdateNicknameFlag();
        testSameOriginGen3DefaultDoesNotEraseCanonicalNickname();
        testSameOriginGen3CustomNicknameMayUpdate();
        testMirrorReturnRestoresHotPidFromOriginalPid();
        testMirrorReturnPreservesCanonicalShinyWhenEvolvedCartNonShiny();
        testPidChangeQuarantines();
        std::cout << "mutable_merge_policy_tests: OK\n";
        return 0;
    } catch (const TestFailure& ex) {
        std::cerr << "mutable_merge_policy_tests: FAILED: " << ex.what() << '\n';
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << "mutable_merge_policy_tests: ERROR: " << ex.what() << '\n';
        return 2;
    }
}
