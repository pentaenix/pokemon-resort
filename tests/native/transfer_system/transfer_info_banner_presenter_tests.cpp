#include "ui/transfer_system/TransferInfoBannerPresenter.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

pr::PcSlotSpecies makeSlot() {
    pr::PcSlotSpecies slot;
    slot.present = true;
    slot.slug = "piplup";
    slot.species_name = "Piplup";
    slot.nickname = "Buddy";
    slot.gender = 1;
    slot.is_shiny = true;
    slot.ball_id = 4;
    slot.ot_name = "Dawn";
    slot.origin_game = "Diamond";
    slot.met_location_name = "Route 201";
    slot.nature = "Calm";
    slot.ability_id = 67;
    slot.ability_name = "Torrent";
    slot.primary_type = "Water";
    slot.secondary_type = "Flying";
    slot.pokerus_status = "infected";
    slot.markings = 3;
    return slot;
}

void testPresenterMapsTextAndIconFields() {
    const pr::PcSlotSpecies slot = makeSlot();
    pr::transfer_system::TransferInfoBannerContext context;
    context.mode = "pokemon";
    context.slot = &slot;
    context.source_game_key = "pokemon_diamond";

    const auto nickname =
        pr::transfer_system::resolveTransferInfoBannerField("nickname", context);
    expect(nickname.visible && nickname.text == "Buddy", "nickname field should prefer nickname text");

    const auto ball =
        pr::transfer_system::resolveTransferInfoBannerField("ball_icon", context);
    expect(ball.visible && ball.use_pokesprite_item && ball.pokesprite_item_id == 4,
           "ball field should request the pokesprite item icon");

    const auto held_item =
        pr::transfer_system::resolveTransferInfoBannerField("held_item_icon", context);
    expect(!held_item.visible,
           "held item icon should stay hidden when the Pokemon is not holding an item");

    const auto gender =
        pr::transfer_system::resolveTransferInfoBannerField("gender_icon", context);
    expect(gender.visible && gender.icon_group == "gender-symbol" && gender.icon_key == "female",
           "gender field should request the generated female symbol renderer");

    const auto secondary_type =
        pr::transfer_system::resolveTransferInfoBannerField("type_secondary_icon", context);
    expect(secondary_type.visible && secondary_type.icon_group == "misc:types" && secondary_type.icon_key == "flying",
           "secondary type field should expose the second type icon through pokesprite misc assets");

    const auto ability =
        pr::transfer_system::resolveTransferInfoBannerField("ability", context);
    expect(ability.visible && ability.text == "Torrent", "ability field should expose ability text");

    const auto shiny =
        pr::transfer_system::resolveTransferInfoBannerField("shiny_icon", context);
    expect(shiny.visible && shiny.icon_group == "misc:special-attribute" && shiny.icon_key == "shiny",
           "shiny field should use pokesprite misc special-attribute icons");

    const auto pokerus =
        pr::transfer_system::resolveTransferInfoBannerField("pokerus_icon", context);
    expect(pokerus.visible && pokerus.icon_group == "misc:special-attribute" && pokerus.icon_key == "infected",
           "pokerus field should use pokesprite misc special-attribute icons");

    const auto origin =
        pr::transfer_system::resolveTransferInfoBannerField("origin_region", context);
    expect(origin.visible && origin.text == "Sinnoh (D)",
           "origin field should expose the readable source region plus game code");

    const auto source_game =
        pr::transfer_system::resolveTransferInfoBannerField("source_game_icon", context);
    expect(source_game.visible && source_game.icon_group == "game" && source_game.icon_key == "sinnoh",
           "source game icon should map Sinnoh saves to the Sinnoh region icon");
}

void testPresenterHidesOptionalStatusIconsWhenInactive() {
    pr::PcSlotSpecies slot = makeSlot();
    slot.is_shiny = false;
    slot.pokerus_status.clear();
    slot.markings = 0;
    pr::transfer_system::TransferInfoBannerContext context;
    context.mode = "pokemon";
    context.slot = &slot;
    context.source_game_key = "pokemon_diamond";

    const auto shiny =
        pr::transfer_system::resolveTransferInfoBannerField("shiny_icon", context);
    expect(!shiny.visible, "shiny icon should hide when the Pokemon is not shiny");

    const auto pokerus =
        pr::transfer_system::resolveTransferInfoBannerField("pokerus_icon", context);
    expect(!pokerus.visible, "pokerus icon should hide when there is no pokerus status");

    const auto mark_circle =
        pr::transfer_system::resolveTransferInfoBannerField("mark_circle_icon", context);
    expect(mark_circle.visible && mark_circle.icon_group == "marking" && mark_circle.icon_key == "circle_off",
           "mark circle icon should stay visible and draw the off mark when the bit is not set");
}

void testPresenterHidesSecondaryTypeWhenItDuplicatesPrimaryType() {
    pr::PcSlotSpecies slot = makeSlot();
    slot.primary_type = "Water";
    slot.secondary_type = "Water";

    pr::transfer_system::TransferInfoBannerContext context;
    context.mode = "pokemon";
    context.slot = &slot;

    const auto primary =
        pr::transfer_system::resolveTransferInfoBannerField("type_icon", context);
    expect(primary.visible && primary.icon_key == "water",
           "primary type should stay visible for single-type Pokemon");

    const auto secondary =
        pr::transfer_system::resolveTransferInfoBannerField("type_secondary_icon", context);
    expect(!secondary.visible,
           "secondary type should hide when bridge data repeats the primary type");
}

void testPresenterHidesGenderIconForGenderlessPokemon() {
    pr::PcSlotSpecies slot = makeSlot();
    slot.gender = 2;

    pr::transfer_system::TransferInfoBannerContext context;
    context.mode = "pokemon";
    context.slot = &slot;

    const auto gender =
        pr::transfer_system::resolveTransferInfoBannerField("gender_icon", context);
    expect(!gender.visible, "gender icon should hide when the Pokemon is genderless");
}

void testPresenterMapsExtraStatusIcons() {
    pr::PcSlotSpecies slot = makeSlot();
    slot.tera_type = "Fire";
    slot.mark_icon = "Rare Mark";
    slot.is_alpha = true;
    slot.is_gigantamax = true;

    pr::transfer_system::TransferInfoBannerContext context;
    context.mode = "pokemon";
    context.slot = &slot;

    const auto tera =
        pr::transfer_system::resolveTransferInfoBannerField("tera_type_icon", context);
    expect(tera.visible && tera.icon_group == "misc:tera-types" && tera.icon_key == "fire",
           "tera type icon should use pokesprite tera type misc icons when present");

    const auto mark =
        pr::transfer_system::resolveTransferInfoBannerField("mark_icon", context);
    expect(mark.visible && mark.icon_group == "misc:mark" && mark.icon_key == "rare-mark",
           "selected mark icon should use pokesprite mark misc icons when present");

    const auto alpha =
        pr::transfer_system::resolveTransferInfoBannerField("alpha_icon", context);
    expect(alpha.visible && alpha.icon_group == "misc:special-attribute" && alpha.icon_key == "alpha-icon",
           "alpha icon should use pokesprite special-attribute icons when present");

    const auto gigantamax =
        pr::transfer_system::resolveTransferInfoBannerField("gigantamax_icon", context);
    expect(gigantamax.visible && gigantamax.icon_group == "misc:special-attribute" &&
               gigantamax.icon_key == "gigantamax-icon",
           "gigantamax icon should use pokesprite special-attribute icons when present");
}

void testPresenterShowsHeldItemIconWhenPokemonHasHeldItem() {
    pr::PcSlotSpecies slot = makeSlot();
    slot.held_item_id = 25;
    slot.held_item_name = "Potion";

    pr::transfer_system::TransferInfoBannerContext context;
    context.mode = "pokemon";
    context.slot = &slot;
    context.source_game_key = "pokemon_diamond";

    const auto held_item =
        pr::transfer_system::resolveTransferInfoBannerField("held_item_icon", context);
    expect(held_item.visible && held_item.use_pokesprite_item && held_item.pokesprite_item_id == 25,
           "held item icon should request the held item texture when present");
}

void testPresenterMapsEachMarkingStateToColoredIcons() {
    pr::PcSlotSpecies slot = makeSlot();
    slot.markings = (1 << 0) | (2 << 2);

    pr::transfer_system::TransferInfoBannerContext context;
    context.mode = "pokemon";
    context.slot = &slot;
    context.source_game_key = "pokemon_diamond";

    const auto circle =
        pr::transfer_system::resolveTransferInfoBannerField("mark_circle_icon", context);
    const auto triangle =
        pr::transfer_system::resolveTransferInfoBannerField("mark_triangle_icon", context);
    const auto square =
        pr::transfer_system::resolveTransferInfoBannerField("mark_square_icon", context);
    const auto heart =
        pr::transfer_system::resolveTransferInfoBannerField("mark_heart_icon", context);

    expect(circle.visible && circle.icon_group == "marking" && circle.icon_key == "circle_blue",
           "mark circle icon should use the _blue state when the first marking is blue");
    expect(triangle.visible && triangle.icon_group == "marking" && triangle.icon_key == "triangle_red",
           "mark triangle icon should use the _red state when the second marking is red");
    expect(square.visible && square.icon_group == "marking" && square.icon_key == "square_off",
           "mark square icon should use the _off asset when its marking bit is clear");
    expect(heart.visible && heart.icon_group == "marking" && heart.icon_key == "heart_off",
           "mark heart icon should use the _off asset when its marking bit is clear");
}

void testPresenterBuildsGameIconAndToolTooltipContent() {
    pr::transfer_system::TransferInfoBannerContext game_icon_context;
    game_icon_context.mode = "game_icon";
    game_icon_context.source_game_key = "pokemon_diamond";
    game_icon_context.game_title = "Pokemon Diamond";
    game_icon_context.trainer_name = "Dawn";
    game_icon_context.play_time = "1:01";
    game_icon_context.pokedex_seen = "12";
    game_icon_context.pokedex_caught = "8";
    game_icon_context.badges = "3";

    const auto title =
        pr::transfer_system::resolveTransferInfoBannerField("game_title", game_icon_context);
    expect(title.visible && title.text == "Pokemon Diamond", "game icon context should expose game title");

    const auto seen =
        pr::transfer_system::resolveTransferInfoBannerField("pokedex_seen", game_icon_context);
    expect(seen.visible && seen.text == "12", "game icon context should expose pokedex seen count");

    pr::transfer_system::TransferInfoBannerContext tool_context;
    tool_context.mode = "tool";
    tool_context.selected_tool_index = 1;
    tool_context.tooltip_copy.tool_basic_title = "Basic Mode";
    tool_context.tooltip_copy.tool_basic_body = "Single Pokemon transfer.";

    const auto tooltip_title =
        pr::transfer_system::resolveTransferInfoBannerField("tooltip_title", tool_context);
    const auto tooltip_body =
        pr::transfer_system::resolveTransferInfoBannerField("tooltip_body", tool_context);
    expect(tooltip_title.visible && tooltip_title.text == "Basic Mode", "tool context should expose tooltip title");
    expect(tooltip_body.visible && tooltip_body.text == "Single Pokemon transfer.",
           "tool context should expose tooltip body");

    pr::transfer_system::TransferInfoBannerContext pill_context;
    pill_context.mode = "pill";
    pill_context.items_mode = true;
    pill_context.tooltip_copy.pill_items_title = "Bag Storage";
    pill_context.tooltip_copy.pill_items_body = "Move items and money between bags.";

    const auto pill_title =
        pr::transfer_system::resolveTransferInfoBannerField("tooltip_title", pill_context);
    const auto pill_body =
        pr::transfer_system::resolveTransferInfoBannerField("tooltip_body", pill_context);
    expect(pill_title.visible && pill_title.text == "Bag Storage", "item pill context should expose bag title");
    expect(pill_body.visible && pill_body.text == "Move items and money between bags.",
           "item pill context should expose item and money transfer copy");

    pr::transfer_system::TransferInfoBannerContext box_space_context;
    box_space_context.mode = "box_space";
    box_space_context.tooltip_copy.box_space_title = "Box Overview";
    box_space_context.tooltip_copy.box_space_body = "Open any PC box quickly.";

    const auto box_space_title =
        pr::transfer_system::resolveTransferInfoBannerField("tooltip_title", box_space_context);
    const auto box_space_body =
        pr::transfer_system::resolveTransferInfoBannerField("tooltip_body", box_space_context);
    expect(box_space_title.visible && box_space_title.text == "Box Overview",
           "box space context should expose tooltip title");
    expect(box_space_body.visible && box_space_body.text == "Open any PC box quickly.",
           "box space context should expose tooltip body");
}

void testPresenterFallsBackToTrainerNameWhenPokemonOtIsMissing() {
    pr::PcSlotSpecies slot = makeSlot();
    slot.ot_name.clear();

    pr::transfer_system::TransferInfoBannerContext context;
    context.mode = "pokemon";
    context.slot = &slot;
    context.trainer_name = "Lucas";

    const auto ot =
        pr::transfer_system::resolveTransferInfoBannerField("ot_name", context);
    expect(ot.visible && ot.text == "Lucas",
           "pokemon OT field should fall back to the save trainer name when slot OT is missing");
}

void testPresenterUsesPokemonOriginForHgssRegion() {
    pr::PcSlotSpecies slot = makeSlot();
    slot.origin_game = "HeartGold";
    slot.met_location_name = "Route 3";

    pr::transfer_system::TransferInfoBannerContext context;
    context.mode = "pokemon";
    context.slot = &slot;
    context.source_game_key = "pokemon_heartgold";

    const auto origin =
        pr::transfer_system::resolveTransferInfoBannerField("origin_region", context);
    expect(origin.visible && origin.text == "Kanto (HG)",
           "HGSS Pokemon origin should prefer met-location region over save title when it points to Kanto");
}

void testPresenterLeavesOriginEmptyOutsidePokemonFocus() {
    pr::transfer_system::TransferInfoBannerContext context;
    context.mode = "empty";
    context.source_game_key = "pokemon_heartgold";

    const auto origin =
        pr::transfer_system::resolveTransferInfoBannerField("origin_region", context);
    expect(origin.visible && origin.text.empty(),
           "origin field should stay empty outside Pokemon focus so the renderer shows placeholder text");
}

void testPresenterDoesNotMisclassifyHeartGoldAsKalos() {
    pr::PcSlotSpecies slot = makeSlot();
    slot.origin_game = "HeartGold";
    slot.met_location_name = "New Bark Town";

    pr::transfer_system::TransferInfoBannerContext context;
    context.mode = "pokemon";
    context.slot = &slot;
    context.source_game_key = "pokemon_heartgold";

    const auto origin =
        pr::transfer_system::resolveTransferInfoBannerField("origin_region", context);
    expect(origin.visible && origin.text == "Johto (HG)",
           "HeartGold Pokemon should never be classified as Kalos just because the name contains a letter x/y match");
}

void testPresenterUsesCurrentSaveCodeWhenOriginGameIsUnknown() {
    pr::PcSlotSpecies slot = makeSlot();
    slot.origin_game.clear();

    pr::transfer_system::TransferInfoBannerContext context;
    context.mode = "pokemon";
    context.slot = &slot;
    context.source_game_key = "pokemon_swsh";

    const auto origin =
        pr::transfer_system::resolveTransferInfoBannerField("origin_region", context);
    expect(origin.visible && origin.text == "Galar (Sw/Sh)",
           "origin field should fall back to the active save family code when exact origin game is unknown");
}

void testPresenterPrefersStoredSourceGameOverActiveSave() {
    pr::PcSlotSpecies slot = makeSlot();
    slot.origin_game.clear();
    slot.source_game_id = 21;
    slot.source_game_key = "pokemon_black";

    pr::transfer_system::TransferInfoBannerContext context;
    context.mode = "pokemon";
    context.slot = &slot;
    context.source_game_key = "pokemon_emerald";

    const auto origin =
        pr::transfer_system::resolveTransferInfoBannerField("origin_region", context);
    expect(origin.visible && origin.text == "Unova (B)",
           "Pokemon imported from Black should keep Unova (B) source text even while Emerald is active");
}

void testPresenterUsesConcreteSourceGameKeyForHgssAggregates() {
    pr::PcSlotSpecies slot = makeSlot();
    slot.origin_game.clear();
    slot.source_game_id = 64;
    slot.source_game_key = "pokemon_heartgold";

    pr::transfer_system::TransferInfoBannerContext context;
    context.mode = "pokemon";
    context.slot = &slot;
    context.source_game_key = "pokemon_emerald";

    const auto origin =
        pr::transfer_system::resolveTransferInfoBannerField("origin_region", context);
    expect(origin.visible && origin.text == "Johto (HG)",
           "HGSS aggregate bridge source ids should display the concrete HeartGold source key when available");
}

void testPresenterFallsBackToRegularPokeBallForGen1SavesWithoutBallData() {
    pr::PcSlotSpecies slot = makeSlot();
    slot.ball_id = -1;
    slot.origin_game = "Blue";

    pr::transfer_system::TransferInfoBannerContext context;
    context.mode = "pokemon";
    context.slot = &slot;
    context.source_game_key = "pokemon_blue";

    const auto ball =
        pr::transfer_system::resolveTransferInfoBannerField("ball_icon", context);
    expect(ball.visible && ball.use_pokesprite_item && ball.pokesprite_item_id == 4,
           "Gen 1 Pokemon without stored caught-ball data should fall back to the regular Poke Ball icon");
}

void testPresenterMapsCaughtBallEnumToPokespriteItemId() {
    pr::PcSlotSpecies slot = makeSlot();
    pr::transfer_system::TransferInfoBannerContext context;
    context.mode = "pokemon";
    context.slot = &slot;
    context.source_game_key = "pokemon_ultra_moon";

    slot.ball_id = 21;
    auto ball = pr::transfer_system::resolveTransferInfoBannerField("ball_icon", context);
    expect(ball.visible && ball.use_pokesprite_item && ball.pokesprite_item_id == 496,
           "Love Ball should map from PKHeX ball enum id 21 to PokeSprite item id 496");

    slot.ball_id = 25;
    ball = pr::transfer_system::resolveTransferInfoBannerField("ball_icon", context);
    expect(ball.visible && ball.use_pokesprite_item && ball.pokesprite_item_id == 576,
           "Dream Ball should map from PKHeX ball enum id 25 to PokeSprite item id 576");

    slot.ball_id = 26;
    ball = pr::transfer_system::resolveTransferInfoBannerField("ball_icon", context);
    expect(ball.visible && ball.use_pokesprite_item && ball.pokesprite_item_id == 851,
           "Beast Ball should map from PKHeX ball enum id 26 to PokeSprite item id 851");
}

void testPresenterResolvesLegacyOriginGameTokens() {
    pr::transfer_system::TransferInfoBannerContext context;
    context.mode = "pokemon";
    context.source_game_key = "pokemon_heartgold";

    pr::PcSlotSpecies red = makeSlot();
    red.origin_game = "pokemon_rd";
    red.met_location_name = "Kanto region";
    context.slot = &red;
    const auto red_origin =
        pr::transfer_system::resolveTransferInfoBannerField("origin_region", context);
    expect(red_origin.visible && red_origin.text == "Kanto (Rd)",
           "legacy cached Gen 1 origin ids should resolve to the right Kanto game");

    pr::PcSlotSpecies blue = makeSlot();
    blue.origin_game = "Blue";
    blue.met_location_name = "Kanto region";
    context.slot = &blue;
    const auto blue_origin =
        pr::transfer_system::resolveTransferInfoBannerField("origin_region", context);
    expect(blue_origin.visible && blue_origin.text == "Kanto (Bu)",
           "raw PKHeX Gen 1 origin names should resolve to the right Kanto game");

    pr::PcSlotSpecies crystal = makeSlot();
    crystal.origin_game = "pokemon_c";
    crystal.met_location_name = "Johto region";
    context.slot = &crystal;
    const auto crystal_origin =
        pr::transfer_system::resolveTransferInfoBannerField("origin_region", context);
    expect(crystal_origin.visible && crystal_origin.text == "Johto (C)",
           "legacy cached Gen 2 origin ids should resolve to the right Johto game");
}

void testPresenterResortIconTooltipShowsOccupiedTotals() {
    pr::transfer_system::TransferInfoBannerContext context;
    context.mode = "resort_icon";
    context.resort_storage_occupied_slots = 7;
    context.resort_storage_total_slots = 180;

    const auto title =
        pr::transfer_system::resolveTransferInfoBannerField("tooltip_title", context);
    const auto body =
        pr::transfer_system::resolveTransferInfoBannerField("tooltip_body", context);
    expect(title.visible && title.text == "Resort storage", "resort icon tooltip should expose a stable title");
    expect(body.visible && body.text == "7 / 180 spots taken",
           "resort icon tooltip body should summarize occupied slots over resort capacity");
}

void testPresenterResolvesWhite2OriginToken() {
    pr::PcSlotSpecies slot = makeSlot();
    slot.origin_game = "white_2";
    slot.met_location_name.clear();

    pr::transfer_system::TransferInfoBannerContext context;
    context.mode = "pokemon";
    context.slot = &slot;
    context.source_game_key = "pokemon_white_2";

    const auto origin =
        pr::transfer_system::resolveTransferInfoBannerField("origin_region", context);
    expect(origin.visible && origin.text == "Unova (W2)",
           "White 2 origin variants should resolve to Unova with the W2 game code");
}

} // namespace

int main() {
    testPresenterMapsTextAndIconFields();
    testPresenterHidesOptionalStatusIconsWhenInactive();
    testPresenterHidesSecondaryTypeWhenItDuplicatesPrimaryType();
    testPresenterHidesGenderIconForGenderlessPokemon();
    testPresenterMapsExtraStatusIcons();
    testPresenterShowsHeldItemIconWhenPokemonHasHeldItem();
    testPresenterMapsEachMarkingStateToColoredIcons();
    testPresenterBuildsGameIconAndToolTooltipContent();
    testPresenterResortIconTooltipShowsOccupiedTotals();
    testPresenterFallsBackToTrainerNameWhenPokemonOtIsMissing();
    testPresenterUsesPokemonOriginForHgssRegion();
    testPresenterLeavesOriginEmptyOutsidePokemonFocus();
    testPresenterDoesNotMisclassifyHeartGoldAsKalos();
    testPresenterUsesCurrentSaveCodeWhenOriginGameIsUnknown();
    testPresenterPrefersStoredSourceGameOverActiveSave();
    testPresenterUsesConcreteSourceGameKeyForHgssAggregates();
    testPresenterFallsBackToRegularPokeBallForGen1SavesWithoutBallData();
    testPresenterMapsCaughtBallEnumToPokespriteItemId();
    testPresenterResolvesLegacyOriginGameTokens();
    testPresenterResolvesWhite2OriginToken();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
