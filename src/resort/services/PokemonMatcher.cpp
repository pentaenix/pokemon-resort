#include "resort/services/PokemonMatcher.hpp"

namespace pr::resort {

namespace {

bool looksLikeGen12(const ImportedPokemon& imported) {
    return imported.format_name == "pk1" || imported.format_name == "pk2";
}

std::optional<std::uint32_t> importedPid(const ImportedPokemon& imported) {
    return imported.identity.pid ? imported.identity.pid : imported.hot.pid;
}

std::optional<std::uint32_t> importedEncryptionConstant(const ImportedPokemon& imported) {
    return imported.identity.encryption_constant
        ? imported.identity.encryption_constant
        : imported.hot.encryption_constant;
}

std::optional<std::uint16_t> importedTid16(const ImportedPokemon& imported) {
    return imported.identity.tid16 ? imported.identity.tid16 : imported.hot.tid16;
}

std::optional<std::uint16_t> importedSid16(const ImportedPokemon& imported) {
    return imported.identity.sid16 ? imported.identity.sid16 : imported.hot.sid16;
}

std::string importedOtName(const ImportedPokemon& imported) {
    return imported.identity.ot_name.empty() ? imported.hot.ot_name : imported.identity.ot_name;
}

std::optional<std::uint16_t> importedDv16(const ImportedPokemon& imported) {
    return imported.identity.dv16 ? imported.identity.dv16 : imported.hot.dv16;
}

PokemonMatchResult matched(
    const ResortPokemon& pokemon,
    MatchConfidence confidence,
    const std::string& reason) {
    PokemonMatchResult result;
    result.matched = true;
    result.pkrid = pokemon.id.pkrid;
    result.confidence = confidence;
    result.reason = reason;
    return result;
}

PokemonMatchResult matchedMirror(const MirrorSession& session, const std::string& reason) {
    PokemonMatchResult result;
    result.matched = true;
    result.pkrid = session.pkrid;
    result.mirror_session_id = session.mirror_session_id;
    result.confidence = MatchConfidence::Exact;
    result.reason = reason;
    return result;
}

PokemonMatchResult noMatch(const std::string& reason) {
    PokemonMatchResult result;
    result.reason = reason;
    return result;
}

bool progressionIsCompatible(const MirrorSession& session, const ImportedPokemon& imported) {
    const unsigned short lineage = imported.hot.lineage_root_species == 0
        ? imported.hot.species_id
        : imported.hot.lineage_root_species;
    if (lineage != 0 && session.sent_lineage_root != 0 && lineage != session.sent_lineage_root) {
        return false;
    }
    if (imported.hot.exp < session.sent_exp) {
        return false;
    }
    return true;
}

bool gen12SyntheticBeaconMatches(
    const MirrorSession& session,
    std::uint16_t tid16,
    const std::string& ot_name) {
    return session.beacon_tid16 && *session.beacon_tid16 == tid16 &&
           session.beacon_ot_name && *session.beacon_ot_name == ot_name;
}

bool gen12NativeOriginalTrainerMatches(
    const MirrorSession& session,
    std::uint16_t tid16,
    const std::string& ot_name) {
    return session.original_game && *session.original_game == session.target_game &&
           session.original_tid16 && *session.original_tid16 == tid16 &&
           session.original_ot_name && *session.original_ot_name == ot_name;
}

bool gen12MirrorProgressionIsCompatible(
    const MirrorSession& session,
    const ImportedPokemon& imported,
    std::uint16_t tid16,
    const std::string& ot_name) {
    if (session.target_game != imported.source_game) {
        return false;
    }
    if (!gen12SyntheticBeaconMatches(session, tid16, ot_name) &&
        !gen12NativeOriginalTrainerMatches(session, tid16, ot_name)) {
        return false;
    }
    if (imported.hot.exp < session.sent_exp) {
        return false;
    }
    if (imported.hot.level < session.sent_level) {
        return false;
    }
    return true;
}

int moveOverlapScore(const ResortPokemon& canonical, const ImportedPokemon& imported) {
    int score = 0;
    for (const auto& before : canonical.hot.move_ids) {
        if (!before) {
            continue;
        }
        for (const auto& after : imported.hot.move_ids) {
            if (after && *before == *after) {
                score += 10;
                break;
            }
        }
    }
    return score;
}

int gen12NativeMirrorScore(
    const MirrorSession& session,
    const ResortPokemon* canonical,
    const ImportedPokemon& imported) {
    int score = 100;
    if (imported.hot.species_id == session.sent_species_id) {
        score += 60;
    }
    if (imported.hot.form_id == session.sent_form_id) {
        score += 10;
    }
    const unsigned short imported_lineage = imported.hot.lineage_root_species == 0
        ? imported.hot.species_id
        : imported.hot.lineage_root_species;
    if (imported_lineage == session.sent_lineage_root) {
        score += 40;
    }
    if (imported.hot.exp == session.sent_exp) {
        score += 15;
    }
    const auto dv_reported = importedDv16(imported);
    const std::optional<std::uint16_t> dv_for_identity =
        (dv_reported && *dv_reported != 0) ? dv_reported : std::nullopt;
    if (dv_for_identity && session.sent_dv16 && *dv_for_identity == *session.sent_dv16) {
        score += 140;
    }

    if (canonical) {
        if (dv_for_identity && canonical->hot.dv16 && *dv_for_identity == *canonical->hot.dv16) {
            score += 40;
        }
        if (canonical->hot.gender == imported.hot.gender) {
            score += 8;
        }
        score += moveOverlapScore(*canonical, imported);
        if (canonical->hot.held_item_id && imported.hot.held_item_id &&
            *canonical->hot.held_item_id == *imported.hot.held_item_id) {
            score += 5;
        }
        if (canonical->hot.origin_game == session.target_game) {
            score += 30;
        }
        if (canonical->hot.lineage_root_species == session.sent_lineage_root) {
            score += 20;
        }
    }

    return score;
}

PokemonMatchResult matchedMaybeActiveMirror(
    MirrorSessionRepository& mirrors,
    const ResortPokemon& pokemon,
    const ImportedPokemon& imported,
    MatchConfidence confidence,
    const std::string& reason,
    bool allow_lineage_change = false) {
    if (auto active = mirrors.findActiveForPokemon(pokemon.id.pkrid)) {
        const bool compatible = allow_lineage_change
            ? imported.hot.exp >= active->sent_exp
            : progressionIsCompatible(*active, imported);
        if (active->target_game == imported.source_game && compatible) {
            return matchedMirror(*active, "active_mirror_" + reason);
        }
    }
    return matched(pokemon, confidence, reason);
}

} // namespace

PokemonMatcher::PokemonMatcher(PokemonRepository& pokemon, MirrorSessionRepository& mirrors)
    : pokemon_(pokemon),
      mirrors_(mirrors) {}

PokemonMatchResult PokemonMatcher::findBestMatch(const ImportedPokemon& imported) const {
    const auto tid16 = importedTid16(imported);
    const std::string ot_name = importedOtName(imported);
    if (looksLikeGen12(imported)) {
        if (!tid16 || ot_name.empty()) {
            return noMatch("gen12_missing_tid_or_ot");
        }

        const auto candidates = mirrors_.findActiveCandidatesByBeacon(imported.source_game, *tid16, ot_name);
        const MirrorSession* best = nullptr;
        int best_score = -1;
        bool ambiguous = false;
        std::string best_reason;

        for (const auto& candidate : candidates) {
            if (!gen12MirrorProgressionIsCompatible(candidate, imported, *tid16, ot_name)) {
                continue;
            }
            const auto canonical = pokemon_.findById(candidate.pkrid);
            const int score = gen12NativeMirrorScore(
                candidate,
                canonical ? &*canonical : nullptr,
                imported);
            const std::string reason = gen12SyntheticBeaconMatches(candidate, *tid16, ot_name)
                ? "active_mirror_beacon"
                : "active_mirror_gen12_native_beacon";
            if (score > best_score) {
                best = &candidate;
                best_score = score;
                ambiguous = false;
                best_reason = reason;
            } else if (score == best_score) {
                ambiguous = true;
            }
        }

        if (best && !ambiguous) {
            return matchedMirror(*best, best_reason);
        }
        if (best && ambiguous) {
            return noMatch("gen12_active_mirror_beacon_ambiguous");
        }
        return noMatch(candidates.empty()
            ? "gen12_no_active_mirror_beacon"
            : "gen12_active_mirror_beacon_progression_rejected");
    }

    if (tid16 && !ot_name.empty()) {
        if (auto mirror = mirrors_.findActiveByBeacon(imported.source_game, *tid16, ot_name)) {
            if (progressionIsCompatible(*mirror, imported)) {
                return matchedMirror(*mirror, "active_mirror_beacon");
            }
            return noMatch("active_mirror_beacon_progression_rejected");
        }
    }

    const auto home_tracker = imported.identity.home_tracker
        ? imported.identity.home_tracker
        : imported.hot.home_tracker;
    if (home_tracker && !home_tracker->empty()) {
        if (auto found = pokemon_.findByHomeTracker(*home_tracker)) {
            return matchedMaybeActiveMirror(mirrors_, *found, imported, MatchConfidence::Exact, "home_tracker", true);
        }
    }

    const auto pid = importedPid(imported);
    const auto ec = importedEncryptionConstant(imported);
    const auto sid16 = importedSid16(imported);

    if (pid && ec && (tid16 || sid16) && !ot_name.empty()) {
        if (auto found = pokemon_.findByPidEcTidSidOt(*pid, *ec, tid16, sid16, ot_name)) {
            return matchedMaybeActiveMirror(mirrors_, *found, imported, MatchConfidence::Exact, "pid_ec_tid_sid_ot", true);
        }
    }

    if (pid && (tid16 || sid16) && !ot_name.empty()) {
        if (auto found = pokemon_.findByPidTidSidOt(*pid, tid16, sid16, ot_name)) {
            return matchedMaybeActiveMirror(mirrors_, *found, imported, MatchConfidence::Strong, "pid_tid_sid_ot");
        }
    }

    return noMatch("no_stable_identifier_match");
}

} // namespace pr::resort
