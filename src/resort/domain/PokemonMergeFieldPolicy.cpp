#include "resort/domain/PokemonMergeFieldPolicy.hpp"

namespace pr::resort {

namespace {

template <typename T>
void replaceIfPresent(std::optional<T>& target, const std::optional<T>& incoming) {
    if (incoming) {
        target = incoming;
    }
}

} // namespace

void applyMirrorReturnHotMutableOverlay(
    PokemonHot& next,
    const PokemonHot& canonical_before,
    const PokemonHot& cart,
    bool evolved,
    bool allow_name_update) {
    next = canonical_before;

    next.level = cart.level;
    next.exp = cart.exp;
    next.hp_current = cart.hp_current;
    next.hp_max = cart.hp_max;
    next.status_flags = cart.status_flags;
    replaceIfPresent(next.held_item_id, cart.held_item_id);
    next.move_ids = cart.move_ids;
    next.move_pp = cart.move_pp;
    next.move_pp_ups = cart.move_pp_ups;

    if (allow_name_update) {
        next.nickname = cart.nickname;
        next.is_nicknamed = cart.is_nicknamed;
    }

    if (evolved) {
        next.species_id = cart.species_id;
        next.form_id = cart.form_id;
        next.gender = cart.gender;
        replaceIfPresent(next.ability_id, cart.ability_id);
        replaceIfPresent(next.ability_slot, cart.ability_slot);
        next.lineage_root_species =
            canonical_before.lineage_root_species != 0 ? canonical_before.lineage_root_species
                                                        : canonical_before.species_id;
    }

    // Sticky shiny: transport-leg PID edits must not clear shininess when the cart snapshot disagrees.
    next.shiny = canonical_before.shiny || cart.shiny;
}

bool isStableIdentityMatchReasonForMirrorReturn(std::string_view match_reason) {
    return match_reason == "home_tracker" || match_reason == "pid_ec_tid_sid_ot" ||
           match_reason == "pid_tid_sid_ot";
}

int maxNationalMoveIdForConstraintGeneration(int constraint_generation) {
    if (constraint_generation <= 0) {
        return 99999;
    }
    if (constraint_generation <= 1) {
        return 165;
    }
    switch (constraint_generation) {
        case 2:
            return 251;
        case 3:
            return 354;
        case 4:
            return 467;
        case 5:
            return 559;
        case 6:
            return 621;
        case 7:
            return 719;
        case 8:
            return 826;
        default:
            return 919;
    }
}

void sanitizeHotMovesForConstraintGeneration(PokemonHot& hot, int constraint_generation) {
    const int cap = maxNationalMoveIdForConstraintGeneration(constraint_generation);
    if (constraint_generation <= 0 || cap <= 0) {
        return;
    }
    for (std::size_t i = 0; i < hot.move_ids.size(); ++i) {
        if (hot.move_ids[i] && static_cast<int>(*hot.move_ids[i]) > cap) {
            hot.move_ids[i] = std::nullopt;
            hot.move_pp[i] = std::nullopt;
            hot.move_pp_ups[i] = std::nullopt;
        }
    }
    bool any_move = false;
    for (const auto& mid : hot.move_ids) {
        if (mid && *mid != 0) {
            any_move = true;
            break;
        }
    }
    if (!any_move) {
        hot.move_ids[0] = static_cast<std::uint16_t>(33);
        hot.move_pp[0] = static_cast<std::uint8_t>(35);
        hot.move_pp_ups[0] = static_cast<std::uint8_t>(0);
    }
}

} // namespace pr::resort
