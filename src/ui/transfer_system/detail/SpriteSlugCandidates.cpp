#include "ui/transfer_system/detail/SpriteSlugCandidates.hpp"

#include "ui/transfer_system/detail/StringUtil.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace fs = std::filesystem;

namespace pr::transfer_system::detail {

std::string spriteFilenameForSlug(const std::string& slug) {
    fs::path p(slug);
    if (p.has_extension()) {
        return slug;
    }
    return slug + ".png";
}

std::string gameSlotSpriteKey(const std::string& slug, int gender, int species_id) {
    const std::string low = asciiLowerCopy(slug);
    if (low == "nidoran") {
        return slug + "|sp=" + std::to_string(species_id) + "|g=" + std::to_string(gender);
    }
    return slug;
}

std::vector<std::string> spriteSlugCandidates(const std::string& raw_slug, int gender, int species_id) {
    auto replace_all = [](std::string s, const std::string& from, const std::string& to) {
        std::size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
        return s;
    };
    auto trim_ascii = [](std::string s) {
        auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
        std::size_t b = 0;
        while (b < s.size() && is_space(static_cast<unsigned char>(s[b]))) {
            ++b;
        }
        std::size_t e = s.size();
        while (e > b && is_space(static_cast<unsigned char>(s[e - 1]))) {
            --e;
        }
        return s.substr(b, e - b);
    };
    auto ascii_slug_sanitize = [](std::string s) {
        // Keep only [a-z0-9_-] so any stray unicode bytes don't break filename matching.
        std::string out;
        out.reserve(s.size());
        for (unsigned char c : s) {
            const char ch = static_cast<char>(c);
            if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-') {
                out.push_back(ch);
            }
        }
        // Collapse duplicate underscores/hyphens.
        std::string collapsed;
        collapsed.reserve(out.size());
        char last = '\0';
        for (char ch : out) {
            if ((ch == '_' || ch == '-') && ch == last) {
                continue;
            }
            collapsed.push_back(ch);
            last = ch;
        }
        return collapsed;
    };

    std::string base = trim_ascii(raw_slug);
    // Strip extension early for normalization.
    if (base.size() > 4 && base.substr(base.size() - 4) == ".png") {
        base.resize(base.size() - 4);
    }
    // Normalize common unicode symbols.
    // Some sources include variation selectors (e.g. "♂️" = U+2642 U+FE0F).
    base = replace_all(std::move(base), u8"\uFE0F", "");
    base = replace_all(std::move(base), u8"\uFE0E", "");
    base = replace_all(std::move(base), u8"♂️", "_m");
    base = replace_all(std::move(base), u8"♀️", "_f");
    base = replace_all(std::move(base), u8"♂", "_m");
    base = replace_all(std::move(base), u8"♀", "_f");
    // Collapse whitespace to underscores, keep dashes as underscores too (our sprite set uses `_` heavily).
    base = asciiLowerCopy(base);
    base = replaceCharsCopy(base, " -", '_');
    // Remove common punctuation found in names.
    base = removeCharsCopy(base, ".'’");
    base = ascii_slug_sanitize(std::move(base));

    std::vector<std::string> out;
    out.reserve(10);
    auto push_unique = [&](const std::string& s) {
        if (s.empty()) return;
        if (std::find(out.begin(), out.end(), s) == out.end()) {
            out.push_back(s);
        }
    };

    push_unique(base);
    push_unique(replaceCharsCopy(base, "-", '_'));
    push_unique(replaceCharsCopy(base, "_", '-'));
    push_unique(removeCharsCopy(base, "_"));

    // Known special-cases (common “space/punct/gender” problems).
    static const std::unordered_map<std::string, std::vector<std::string>> kSpecial{
        {"farfetch", {"farfetchd", "farfetch"}},
        {"farfetchd", {"farfetchd", "farfetch"}},
        {"mr_mime", {"mr_mime", "mrmime", "mr-mime"}},
        {"mrmime", {"mr_mime", "mrmime"}},
        {"mime_jr", {"mime_jr", "mimejr", "mime-jr"}},
        {"mimejr", {"mime_jr", "mimejr"}},
        {"ho_oh", {"ho_oh", "ho-oh", "hooh"}},
        {"hooh", {"ho_oh", "hooh"}},
        {"porygon_z", {"porygon_z", "porygon-z", "porygonz"}},
        {"porygonz", {"porygon_z", "porygonz"}},
        {"nidoran_m", {"nidoran_m", "nidoran-m", "nidoranmale", "nidoran_male"}},
        {"nidoran_f", {"nidoran_f", "nidoran-f", "nidoranfemale", "nidoran_female"}},
        {"nidoran_male", {"nidoran_m", "nidoran_male"}},
        {"nidoran_female", {"nidoran_f", "nidoran_female"}},
    };
    if (auto it = kSpecial.find(base); it != kSpecial.end()) {
        for (const auto& s : it->second) {
            push_unique(s);
        }
    }

    if (base == "nidoran") {
        auto push_nidoran_ordered = [&](bool male_first) {
            if (male_first) {
                push_unique("nidoran-m");
                push_unique("nidoran_m");
                push_unique("nidoran-f");
                push_unique("nidoran_f");
            } else {
                push_unique("nidoran-f");
                push_unique("nidoran_f");
                push_unique("nidoran-m");
                push_unique("nidoran_m");
            }
        };
        if (species_id == 32) {
            push_nidoran_ordered(true);
        } else if (species_id == 29) {
            push_nidoran_ordered(false);
        } else if (gender == 0) {
            push_nidoran_ordered(true);
        } else if (gender == 1) {
            push_nidoran_ordered(false);
        } else {
            push_nidoran_ordered(true);
        }
    }

    // Extremely defensive: if the raw slug mentions nidoran + gender, ensure we try the known filenames.
    const std::string raw_l = asciiLowerCopy(raw_slug);
    if (raw_l.find("nidoran") != std::string::npos) {
        if (raw_l.find("male") != std::string::npos || raw_l.find("_m") != std::string::npos || raw_l.find("-m") != std::string::npos ||
            raw_slug.find(u8"♂") != std::string::npos || raw_slug.find(u8"♂️") != std::string::npos ||
            raw_slug.find(u8"\u2642") != std::string::npos) {
            push_unique("nidoran-m");
            push_unique("nidoran_m");
        }
        if (raw_l.find("female") != std::string::npos || raw_l.find("_f") != std::string::npos || raw_l.find("-f") != std::string::npos ||
            raw_slug.find(u8"♀") != std::string::npos || raw_slug.find(u8"♀️") != std::string::npos ||
            raw_slug.find(u8"\u2640") != std::string::npos) {
            push_unique("nidoran-f");
            push_unique("nidoran_f");
        }
    }

    // If the raw slug had spaces (e.g. "Mr Mime"), try the space-removed and underscore forms too.
    const std::string raw_lower = asciiLowerCopy(raw_slug);
    push_unique(removeCharsCopy(replaceCharsCopy(raw_lower, " -", '_'), ".'’"));
    push_unique(removeCharsCopy(replaceCharsCopy(raw_lower, " -", '-'), ".'’"));
    push_unique(removeCharsCopy(raw_lower, " .'’-_"));

    return out;
}

std::vector<std::string> spriteSlugCandidates(const std::string& raw_slug) {
    return spriteSlugCandidates(raw_slug, -1, -1);
}

} // namespace pr::transfer_system::detail

