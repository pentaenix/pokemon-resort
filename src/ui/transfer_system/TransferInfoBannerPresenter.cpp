#include "ui/transfer_system/TransferInfoBannerPresenter.hpp"

#include "core/domain/PokemonBall.hpp"
#include "core/domain/PokemonOrigin.hpp"

#include <algorithm>
#include <cctype>

namespace pr::transfer_system {

std::string sourceRegionKeyForGameId(const std::string& game_id);

namespace {

constexpr int kDefaultPokeBallItemId = 4;

std::string asciiLower(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::string normalizeIconToken(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    bool last_dash = false;
    for (const unsigned char raw : value) {
        const char ch = static_cast<char>(std::tolower(raw));
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
            out.push_back(ch);
            last_dash = false;
        } else if (!out.empty() && !last_dash) {
            out.push_back('-');
            last_dash = true;
        }
    }
    while (!out.empty() && out.back() == '-') {
        out.pop_back();
    }
    return out;
}

std::string titleCaseAscii(std::string value) {
    bool uppercase_next = true;
    for (char& ch : value) {
        if (ch == ' ' || ch == '-' || ch == '_') {
            uppercase_next = true;
            if (ch == '_') {
                ch = ' ';
            }
            continue;
        }
        if (uppercase_next) {
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            uppercase_next = false;
        } else {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
    }
    return value;
}

std::string fallbackAbilityName(const PcSlotSpecies& slot) {
    if (!slot.ability_name.empty()) {
        return slot.ability_name;
    }
    if (slot.ability_id > 0) {
        return "Ability " + std::to_string(slot.ability_id);
    }
    return {};
}

const PcSlotSpecies* activeSlot(const TransferInfoBannerContext& context) {
    return (context.slot && context.slot->occupied()) ? context.slot : nullptr;
}

std::pair<std::string, std::string> tooltipCopyForContext(const TransferInfoBannerContext& context) {
    if (context.mode == "exit") {
        return {context.tooltip_copy.exit_tooltip_title, context.tooltip_copy.exit_tooltip_body};
    }
    if (context.mode == "tool") {
        switch (context.selected_tool_index) {
            case 0:
                return {context.tooltip_copy.tool_multiple_title, context.tooltip_copy.tool_multiple_body};
            case 1:
                return {context.tooltip_copy.tool_basic_title, context.tooltip_copy.tool_basic_body};
            case 2:
                return {context.tooltip_copy.tool_swap_title, context.tooltip_copy.tool_swap_body};
            default:
                return {context.tooltip_copy.tool_items_title, context.tooltip_copy.tool_items_body};
        }
    }
    if (context.mode == "pill") {
        if (context.items_mode) {
            return {context.tooltip_copy.pill_items_title, context.tooltip_copy.pill_items_body};
        }
        return {context.tooltip_copy.pill_pokemon_title, context.tooltip_copy.pill_pokemon_body};
    }
    if (context.mode == "box_space") {
        return {context.tooltip_copy.box_space_title, context.tooltip_copy.box_space_body};
    }
    if (context.mode == "resort_icon") {
        std::string body = std::to_string(context.resort_storage_occupied_slots) + " / " +
                           std::to_string(std::max(0, context.resort_storage_total_slots)) + " spots taken";
        return {"Resort storage", std::move(body)};
    }
    return {};
}

std::string sourceRegionDisplayNameForGameId(const std::string& game_id) {
    const std::string key = sourceRegionKeyForGameId(game_id);
    if (key == "unknown") {
        return {};
    }
    return titleCaseAscii(key);
}

bool gameLikelyLacksCaughtBallData(const std::string& game_id) {
    const std::string id = asciiLower(game_id);
    return id == "pokemon_red" || id == "pokemon_blue" || id == "pokemon_yellow";
}

std::string markingStateKey(int markings, int marking_index) {
    if (marking_index < 0 || marking_index >= 6) {
        return "off";
    }

    const int two_bit_state = (markings >> (marking_index * 2)) & 0x3;
    if (two_bit_state == 1) {
        return "blue";
    }
    if (two_bit_state >= 2) {
        return "red";
    }
    return "off";
}

void setMarkingIcon(TransferInfoBannerFieldValue& value, const char* shape, std::string state) {
    value.icon_group = "marking";
    value.icon_key = std::string(shape) + "_" + std::move(state);
}

std::string sourceRegionKeyForSlot(const TransferInfoBannerContext& context) {
    const PcSlotSpecies* slot = activeSlot(context);
    if (!slot) {
        return "unknown";
    }
    return resolvePokemonOrigin(*slot, context.source_game_key).region_key;
}

std::string sourceRegionDisplayNameForSlot(const TransferInfoBannerContext& context) {
    const std::string key = sourceRegionKeyForSlot(context);
    if (key == "unknown") {
        return {};
    }
    return titleCaseAscii(key);
}

std::string sourceRegionWithGameCodeForSlot(const TransferInfoBannerContext& context) {
    const PcSlotSpecies* slot = activeSlot(context);
    if (!slot) {
        return {};
    }

    const std::string region = sourceRegionDisplayNameForSlot(context);
    if (region.empty()) {
        return {};
    }

    std::string code = resolvePokemonOrigin(*slot, context.source_game_key).game_code;
    if (code.empty()) {
        code = pokemonGameCode(context.source_game_key);
    }
    if (code.empty()) {
        return region;
    }
    return region + " (" + code + ")";
}

int resolvedBallItemIdForSlot(const TransferInfoBannerContext& context) {
    const PcSlotSpecies* slot = activeSlot(context);
    if (!slot) {
        return -1;
    }
    if (slot->ball_id > 0) {
        return pokespriteItemIdForBallId(slot->ball_id);
    }

    const std::string origin_game_id = normalizePokemonGameId(slot->origin_game);
    if (!origin_game_id.empty() && gameLikelyLacksCaughtBallData(origin_game_id)) {
        return kDefaultPokeBallItemId;
    }
    if (gameLikelyLacksCaughtBallData(context.source_game_key)) {
        return kDefaultPokeBallItemId;
    }
    return -1;
}

} // namespace

std::string sourceRegionKeyForGameId(const std::string& game_id) {
    return pokemonGameRegionKey(game_id);
}

TransferInfoBannerFieldValue resolveTransferInfoBannerField(
    const std::string& field_name,
    const TransferInfoBannerContext& context) {
    TransferInfoBannerFieldValue value;
    const PcSlotSpecies* slot = activeSlot(context);

    if (field_name == "source_game_icon") {
        value.visible = context.mode == "pokemon" || context.mode == "game_icon";
        value.icon_group = "game";
        value.icon_key = context.mode == "pokemon"
            ? sourceRegionKeyForSlot(context)
            : sourceRegionKeyForGameId(context.source_game_key);
        return value;
    }
    if (field_name == "ball_icon") {
        value.visible = context.mode == "pokemon" && slot != nullptr;
        if (slot) {
            const int ball_item_id = resolvedBallItemIdForSlot(context);
            value.use_pokesprite_item = ball_item_id > 0;
            value.pokesprite_item_id = ball_item_id;
            value.icon_key = ball_item_id > 0 ? std::string{} : "unknown";
        }
        return value;
    }
    if (field_name == "held_item_icon") {
        value.visible = context.mode == "pokemon" && slot != nullptr && slot->held_item_id > 0;
        if (slot && slot->held_item_id > 0) {
            value.use_pokesprite_item = true;
            value.pokesprite_item_id = slot->held_item_id;
        }
        return value;
    }
    if (field_name == "mark_circle_icon") {
        value.visible = context.mode == "pokemon" && slot != nullptr;
        if (slot) setMarkingIcon(value, "circle", markingStateKey(slot->markings, 0));
        return value;
    }
    if (field_name == "mark_triangle_icon") {
        value.visible = context.mode == "pokemon" && slot != nullptr;
        if (slot) setMarkingIcon(value, "triangle", markingStateKey(slot->markings, 1));
        return value;
    }
    if (field_name == "mark_square_icon") {
        value.visible = context.mode == "pokemon" && slot != nullptr;
        if (slot) setMarkingIcon(value, "square", markingStateKey(slot->markings, 2));
        return value;
    }
    if (field_name == "mark_heart_icon") {
        value.visible = context.mode == "pokemon" && slot != nullptr;
        if (slot) setMarkingIcon(value, "heart", markingStateKey(slot->markings, 3));
        return value;
    }
    if (field_name == "mark_star_icon") {
        value.visible = context.mode == "pokemon" && slot != nullptr;
        if (slot) setMarkingIcon(value, "star", markingStateKey(slot->markings, 4));
        return value;
    }
    if (field_name == "mark_diamond_icon") {
        value.visible = context.mode == "pokemon" && slot != nullptr;
        if (slot) setMarkingIcon(value, "rhombus", markingStateKey(slot->markings, 5));
        return value;
    }
    if (field_name == "nickname") {
        if (slot) {
            value.text = !slot->nickname.empty() ? slot->nickname : slot->species_name;
        }
        value.visible = context.mode == "pokemon" || context.mode == "empty";
        return value;
    }
    if (field_name == "species") {
        if (slot) {
            value.text = slot->species_name;
        }
        value.visible = context.mode == "pokemon" || context.mode == "empty";
        return value;
    }
    if (field_name == "level") {
        if (slot && slot->level >= 0) {
            value.text = "Lv. " + std::to_string(slot->level);
        }
        value.visible = context.mode == "pokemon" || context.mode == "empty";
        return value;
    }
    if (field_name == "gender_icon") {
        value.visible = context.mode == "pokemon" && slot != nullptr;
        value.icon_group = "gender-symbol";
        if (slot) {
            value.visible = slot->gender == 0 || slot->gender == 1;
            value.icon_key = slot->gender == 1 ? "female" : "male";
        }
        return value;
    }
    if (field_name == "type_icon") {
        value.visible = context.mode == "pokemon" && slot != nullptr;
        value.icon_group = "misc:types";
        if (slot) {
            value.icon_key = normalizeIconToken(slot->primary_type);
            if (value.icon_key.empty()) {
                value.icon_key = "unknown";
            }
        }
        return value;
    }
    if (field_name == "type_secondary_icon") {
        value.visible = context.mode == "pokemon" && slot != nullptr;
        value.icon_group = "misc:types";
        if (slot) {
            value.icon_key = normalizeIconToken(slot->secondary_type);
            const std::string primary_key = normalizeIconToken(slot->primary_type);
            value.visible = !value.icon_key.empty() && value.icon_key != primary_key;
        }
        return value;
    }
    if (field_name == "tera_type_icon") {
        value.visible = context.mode == "pokemon" && slot != nullptr;
        value.icon_group = "misc:tera-types";
        if (slot) {
            value.icon_key = normalizeIconToken(slot->tera_type);
            value.visible = !value.icon_key.empty();
        }
        return value;
    }
    if (field_name == "shiny_icon") {
        value.visible = context.mode == "pokemon" && slot != nullptr && slot->is_shiny;
        value.icon_group = "misc:special-attribute";
        value.icon_key = "shiny";
        return value;
    }
    if (field_name == "mark_icon") {
        value.visible = context.mode == "pokemon" && slot != nullptr && !slot->mark_icon.empty();
        value.icon_group = "misc:mark";
        if (slot) {
            value.icon_key = normalizeIconToken(slot->mark_icon);
        }
        return value;
    }
    if (field_name == "alpha_icon") {
        value.visible = context.mode == "pokemon" && slot != nullptr && slot->is_alpha;
        value.icon_group = "misc:special-attribute";
        value.icon_key = "alpha-icon";
        return value;
    }
    if (field_name == "gigantamax_icon") {
        value.visible = context.mode == "pokemon" && slot != nullptr && slot->is_gigantamax;
        value.icon_group = "misc:special-attribute";
        value.icon_key = "gigantamax-icon";
        return value;
    }
    if (field_name == "pokerus_icon") {
        if (context.mode == "pokemon" && slot) {
            value.icon_group = "misc:special-attribute";
            value.icon_key = normalizeIconToken(slot->pokerus_status);
            value.visible = !value.icon_key.empty();
        }
        return value;
    }
    if (field_name == "ot_name") {
        if (context.mode == "game_icon") {
            value.text = context.trainer_name;
        } else if (slot) {
            value.text = !slot->ot_name.empty() ? slot->ot_name : context.trainer_name;
        }
        value.visible = context.mode == "pokemon" || context.mode == "empty" || context.mode == "game_icon";
        return value;
    }
    if (field_name == "origin_region") {
        if (context.mode == "pokemon" || context.mode == "empty") {
            value.text = context.mode == "pokemon" ? sourceRegionWithGameCodeForSlot(context) : std::string{};
        }
        value.visible = context.mode == "pokemon" || context.mode == "empty";
        return value;
    }
    if (field_name == "nature") {
        if (slot) {
            value.text = slot->nature;
        }
        value.visible = context.mode == "pokemon" || context.mode == "empty";
        return value;
    }
    if (field_name == "ability") {
        if (slot) {
            value.text = fallbackAbilityName(*slot);
        }
        value.visible = context.mode == "pokemon" || context.mode == "empty";
        return value;
    }
    if (field_name == "game_title") {
        value.text = context.game_title;
        value.visible = context.mode == "game_icon";
        return value;
    }
    if (field_name == "play_time") {
        value.text = context.play_time;
        value.visible = context.mode == "game_icon";
        return value;
    }
    if (field_name == "pokedex_seen") {
        value.text = context.pokedex_seen;
        value.visible = context.mode == "game_icon";
        return value;
    }
    if (field_name == "pokedex_caught") {
        value.text = context.pokedex_caught;
        value.visible = context.mode == "game_icon";
        return value;
    }
    if (field_name == "badges") {
        value.text = context.badges;
        value.visible = context.mode == "game_icon";
        return value;
    }
    if (field_name == "tooltip_title") {
        const auto copy = tooltipCopyForContext(context);
        value.text = copy.first;
        value.visible = !value.text.empty();
        return value;
    }
    if (field_name == "tooltip_body") {
        const auto copy = tooltipCopyForContext(context);
        value.text = copy.second;
        value.visible = !value.text.empty();
        return value;
    }

    return value;
}

} // namespace pr::transfer_system
