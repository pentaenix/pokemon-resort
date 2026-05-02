#include "resort/services/MirrorReturnAnalysis.hpp"

namespace pr::resort {

namespace {

void pushUnique(std::vector<std::string>& out, const char* flag) {
    for (const auto& e : out) {
        if (e == flag) {
            return;
        }
    }
    out.push_back(flag);
}

} // namespace

ImportConflictReport MirrorReturnAnalysis::analyzePreMerge(
    const ResortPokemon& canonical,
    const ImportedPokemon& incoming) {
    ImportConflictReport report;
    const PokemonHot& ch = canonical.hot;
    const PokemonHot& ih = incoming.hot;

    if (!ch.ot_name.empty() && !ih.ot_name.empty() && ch.ot_name != ih.ot_name) {
        pushUnique(report.flags, "ot_name_changed");
    }
    if (ch.tid16 && ih.tid16 && *ch.tid16 != *ih.tid16) {
        pushUnique(report.flags, "tid16_changed");
    }
    if (ch.sid16 && ih.sid16 && *ch.sid16 != *ih.sid16) {
        pushUnique(report.flags, "sid16_changed");
    }
    if (ch.pid && ih.pid && *ch.pid != *ih.pid) {
        pushUnique(report.flags, "pid_changed");
    }

    if (ih.level + 50 < ch.level) {
        pushUnique(report.flags, "large_level_regression");
    }
    if (ih.exp + 1'000'000u < ch.exp) {
        pushUnique(report.flags, "large_exp_regression");
    }

    if (ch.species_id != 0 && ih.species_id != 0 && ch.species_id != ih.species_id) {
        pushUnique(report.flags, "species_changed");
    }

    for (const std::string& f : report.flags) {
        if (f == "pid_changed" || f == "tid16_changed" || f == "sid16_changed") {
            report.quarantine_recommended = true;
            break;
        }
    }

    return report;
}

} // namespace pr::resort
