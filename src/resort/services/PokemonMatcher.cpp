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

} // namespace

PokemonMatcher::PokemonMatcher(PokemonRepository& pokemon, MirrorSessionRepository& mirrors)
    : pokemon_(pokemon),
      mirrors_(mirrors) {}

PokemonMatchResult PokemonMatcher::findBestMatch(const ImportedPokemon& imported) const {
    const auto tid16 = importedTid16(imported);
    const std::string ot_name = importedOtName(imported);
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
            return matched(*found, MatchConfidence::Exact, "home_tracker");
        }
    }

    const auto pid = importedPid(imported);
    const auto ec = importedEncryptionConstant(imported);
    const auto sid16 = importedSid16(imported);

    if (pid && ec && (tid16 || sid16) && !ot_name.empty()) {
        if (auto found = pokemon_.findByPidEcTidSidOt(*pid, *ec, tid16, sid16, ot_name)) {
            return matched(*found, MatchConfidence::Exact, "pid_ec_tid_sid_ot");
        }
    }

    if (pid && (tid16 || sid16) && !ot_name.empty()) {
        if (auto found = pokemon_.findByPidTidSidOt(*pid, tid16, sid16, ot_name)) {
            return matched(*found, MatchConfidence::Strong, "pid_tid_sid_ot");
        }
    }

    if (looksLikeGen12(imported)) {
        return noMatch("gen12_best_effort_identity_deferred");
    }

    return noMatch("no_stable_identifier_match");
}

} // namespace pr::resort
