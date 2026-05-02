#pragma once

#include "resort/domain/ResortTypes.hpp"

#include <string_view>

namespace pr::resort {

/// Documents and applies **mirror-return** (`MirrorReturnGameplaySync`) merge rules for `PokemonHot`.
///
/// **Static** (Resort source of truth — never copied from the returning cart snapshot):
/// - **Encounter provenance**: `met_location_id`, `met_level`, `met_date_unix`, `ball_id`
/// - **Identity / OT linkage**: `ot_name`, `tid16`, `sid16`, `tid32`, `pid`, `encryption_constant`,
///   `home_tracker`, `origin_game`, `language`
/// - **Gen I–II packed DVs** (`dv16`): treated as identity-bearing for matching; never overwritten on
///   mirror return (including after evolution in another generation).
/// - **Lineage bookkeeping**: `lineage_root_species` follows Resort rules below, not the cart’s
///   post-trade lineage hints.
///
/// **Mutable** (gameplay / cart state that may legitimately differ when the Pokémon returns):
/// - **Always**: `level`, `exp`, `hp_current`, `hp_max`, `status_flags`, `held_item_id`
/// - **When the Pokémon evolved or changed form in the mirror** (`evolved`): `species_id`, `form_id`,
///   `nickname`, `is_nicknamed`, `shiny`, `gender`, moves (+ PP), `ability_id`, `ability_slot`
/// - **When level increased without evolution** (`leveled_up`): moves (+ PP) only (existing TM/learnset
///   progression policy).
///
/// **Generation notes**
/// - Modern gens: IV/EV/nature live primarily in cold/raw blobs; bottle caps / mints imply those bytes
///   can change while hot mirrors level/moves — reflected when we expand hot or rely on snapshots.
/// - Gen I–II: `dv16` stays static here; do not mirror cart DV edits into Resort identity.
/// - Ability: only updated from the cart when `evolved` is true (new species may imply a new ability).
///
/// Sets `next` from `canonical_before` (preserving all static fields), then overlays mutable gameplay
/// fields from `cart` per `evolved` / `leveled_up`.
void applyMirrorReturnHotMutableOverlay(
    PokemonHot& next,
    const PokemonHot& canonical_before,
    const PokemonHot& cart,
    bool evolved,
    bool leveled_up);

/// Root-level warm JSON keys on a bridge import that describe **this cart read** (format, checksum,
/// location) and must not overwrite canonical Resort warm metadata on mirror return.
constexpr std::string_view kMirrorWarmStripIncomingKeys[] = {
    "source_location",
    "format",
    "checksum_valid",
};

} // namespace pr::resort
