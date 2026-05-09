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
///   `gender`, moves (+ PP), `ability_id`, `ability_slot`
///   (`shiny` is merged as **monotonic**: canonical shiny OR cart shiny — never downgrade)
/// - Moves (+ PP) are always mirrored from the returning cart so target-generation move loss or player
///   replacement persists when the Pokémon comes back.
/// - Nickname and `is_nicknamed` are immutable on cross-generation mirror return. Same-origin/source-game
///   returns may update them only when the incoming format has an explicit nickname flag, or when an
///   older-format read is inferred to contain a custom nickname.
/// - Ribbons in `resort_catalog.ribbons` / `ribbon_flags` are **gain-only** on mirror return: boolean
///   achievements OR-merge; contest tier / count fields use the maximum. An absent or false value in the
///   incoming cart import must not remove a ribbon Resort already recorded.
/// - Pokerus in `resort_catalog.pokerus` is preserved when the incoming cart import is absent or zeroed;
///   non-zero infection/cure evidence may merge forward, with day counts monotonic.
///
/// **Generation notes**
/// - Modern gens: IV/EV/nature live primarily in cold/raw blobs; bottle caps / mints imply those bytes
///   can change while hot mirrors level/moves — reflected when we expand hot or rely on snapshots.
/// - Gen I–II: `dv16` stays static here; do not mirror cart DV edits into Resort identity.
/// - Ability: only updated from the cart when `evolved` is true (new species may imply a new ability).
///
/// Sets `next` from `canonical_before` (preserving all static fields), then overlays mutable gameplay
/// fields from `cart` per `evolved` / `allow_name_update`.
void applyMirrorReturnHotMutableOverlay(
    PokemonHot& next,
    const PokemonHot& canonical_before,
    const PokemonHot& cart,
    bool evolved,
    bool allow_name_update);

/// Root-level warm JSON keys on a bridge import that describe **this cart read** (format, checksum,
/// location) and must not overwrite canonical Resort warm metadata on mirror return.
constexpr std::string_view kMirrorWarmStripIncomingKeys[] = {
    "source_location",
    "format",
    "checksum_valid",
    "source_game_key",
    "source_game_id",
    "source_context",
    // Friendship / nature from the returning cart must not overwrite Resort warm catalog on mirror return.
    "original_trainer_friendship",
    "handling_trainer_friendship",
    "current_friendship",
    "nature",
};

/// `PokemonMatcher` reasons that identify an existing canonical row without necessarily attaching an
/// active `mirror_session_id` (e.g. home tracker match after session bookkeeping). Imports with these
/// reasons must use `MirrorReturnGameplaySync`, not `mergeFullReplace`, so static identity stays Resort-owned.
bool isStableIdentityMatchReasonForMirrorReturn(std::string_view match_reason);

/// Max national move id representable in a PKHeX storage format generation (aligns with
/// `BridgeProjectPastProjection.MaxMoveForFormat`).
int maxNationalMoveIdForConstraintGeneration(int constraint_generation);

/// Clears move slots whose move id exceeds the limit for `constraint_generation` (e.g. Grass Knot after a
/// Gen III leg). If all slots become empty, fills slot 0 with Tackle (33) like the bridge past projection.
void sanitizeHotMovesForConstraintGeneration(PokemonHot& hot, int constraint_generation);

} // namespace pr::resort
