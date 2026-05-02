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
    bool leveled_up) {
    next = canonical_before;

    next.level = cart.level;
    next.exp = cart.exp;
    next.hp_current = cart.hp_current;
    next.hp_max = cart.hp_max;
    next.status_flags = cart.status_flags;
    replaceIfPresent(next.held_item_id, cart.held_item_id);

    if (evolved) {
        next.species_id = cart.species_id;
        next.form_id = cart.form_id;
        next.move_ids = cart.move_ids;
        next.move_pp = cart.move_pp;
        next.move_pp_ups = cart.move_pp_ups;
        next.nickname = cart.nickname;
        next.is_nicknamed = cart.is_nicknamed;
        next.shiny = cart.shiny;
        next.gender = cart.gender;
        replaceIfPresent(next.ability_id, cart.ability_id);
        replaceIfPresent(next.ability_slot, cart.ability_slot);
        next.lineage_root_species =
            canonical_before.lineage_root_species != 0 ? canonical_before.lineage_root_species
                                                        : canonical_before.species_id;
    } else if (leveled_up) {
        next.move_ids = cart.move_ids;
        next.move_pp = cart.move_pp;
        next.move_pp_ups = cart.move_pp_ups;
    }
}

} // namespace pr::resort
