#pragma once

#include <string>
#include <vector>

namespace pr::transfer_system::detail {

std::string spriteFilenameForSlug(const std::string& slug);

/// For Nidoran, disambiguate by species/gender so cache keys remain stable.
std::string gameSlotSpriteKey(const std::string& slug, int gender, int species_id);

/// Generates alternate sprite keys for slugs that may include spaces/punctuation/gender.
/// Returns keys without forcing an extension.
std::vector<std::string> spriteSlugCandidates(const std::string& raw_slug, int gender, int species_id);

std::vector<std::string> spriteSlugCandidates(const std::string& raw_slug);

} // namespace pr::transfer_system::detail

