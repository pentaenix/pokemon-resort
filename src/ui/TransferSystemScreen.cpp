#include "ui/TransferSystemScreen.hpp"

#include "core/PokeSpriteAssets.hpp"
#include "resort/domain/ResortTypes.hpp"
#include "resort/services/PokemonResortService.hpp"

#include "core/Assets.hpp"
#include "core/Json.hpp"
#include "ui/transfer_system/GameTransferConfig.hpp"
#include "ui/transfer_system/TransferInfoBannerPresenter.hpp"
#include "ui/transfer_system/TransferSystemFocusGraph.hpp"

#include <SDL_image.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>

namespace fs = std::filesystem;

namespace pr {

namespace {

constexpr int kLeftBoxColumnX = 40;
constexpr int kBoxViewportY = 100;

fs::path resolvePath(const std::string& root, const std::string& configured) {
    fs::path path(configured);
    return path.is_absolute() ? path : (fs::path(root) / path);
}

std::string spriteFilenameForSlug(const std::string& slug) {
    fs::path p(slug);
    if (p.has_extension()) {
        return slug;
    }
    return slug + ".png";
}

bool utf8_pop_back_last(std::string& s) {
    if (s.empty()) {
        return false;
    }
    std::size_t i = s.size() - 1;
    while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) {
        --i;
    }
    s.erase(i);
    return true;
}

std::string trimAsciiWhitespaceCopy(std::string s) {
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!s.empty() && is_space(static_cast<unsigned char>(s.front()))) {
        s.erase(s.begin());
    }
    while (!s.empty() && is_space(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
    return s;
}

std::string asciiLowerCopy(std::string s) {
    for (char& ch : s) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

std::string replaceCharsCopy(std::string s, const std::string& from_any, char to) {
    for (char& ch : s) {
        if (from_any.find(ch) != std::string::npos) {
            ch = to;
        }
    }
    return s;
}

std::string removeCharsCopy(std::string s, const std::string& remove_any) {
    std::string out;
    out.reserve(s.size());
    for (char ch : s) {
        if (remove_any.find(ch) == std::string::npos) {
            out.push_back(ch);
        }
    }
    return out;
}

std::string gameSlotSpriteKey(const std::string& slug, int gender, int species_id) {
    const std::string low = asciiLowerCopy(slug);
    if (low == "nidoran") {
        return slug + "|sp=" + std::to_string(species_id) + "|g=" + std::to_string(gender);
    }
    return slug;
}

/// Generates alternate sprite keys for slugs that may include spaces/punctuation/gender.
/// We try these in order until an existing PNG is found.
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

TextureHandle loadTexture(SDL_Renderer* renderer, const fs::path& path) {
    SDL_Texture* raw = IMG_LoadTexture(renderer, path.string().c_str());
    if (!raw) {
        throw std::runtime_error("Failed to load texture: " + path.string() + " | " + IMG_GetError());
    }

    TextureHandle texture;
    texture.texture.reset(raw, SDL_DestroyTexture);
    if (SDL_QueryTexture(texture.texture.get(), nullptr, nullptr, &texture.width, &texture.height) != 0) {
        throw std::runtime_error("Failed to query texture: " + path.string() + " | " + SDL_GetError());
    }
    return texture;
}

TextureHandle loadTextureOptional(SDL_Renderer* renderer, const fs::path& path) {
    TextureHandle texture;
    if (!fs::exists(path)) {
        return texture;
    }
    SDL_Texture* raw = IMG_LoadTexture(renderer, path.string().c_str());
    if (!raw) {
        std::cerr << "Warning: failed to load texture: " << path.string() << " | " << IMG_GetError() << '\n';
        return texture;
    }
    texture.texture.reset(raw, SDL_DestroyTexture);
    if (SDL_QueryTexture(texture.texture.get(), nullptr, nullptr, &texture.width, &texture.height) != 0) {
        texture.texture.reset();
        return texture;
    }
    SDL_SetTextureBlendMode(texture.texture.get(), SDL_BLENDMODE_BLEND);
    return texture;
}

void setTextureNearestNeighbor(TextureHandle& tex) {
    if (!tex.texture) {
        return;
    }
#if SDL_VERSION_ATLEAST(2, 0, 12)
    SDL_SetTextureScaleMode(tex.texture.get(), SDL_ScaleModeNearest);
#else
    // Fallback: best effort; global hint is avoided to not affect UI.
#endif
}

double doubleFromObjectOrDefault(const JsonValue& obj, const std::string& key, double fallback) {
    const JsonValue* value = obj.get(key);
    return value ? value->asNumber() : fallback;
}

bool boolFromObjectOrDefault(const JsonValue& obj, const std::string& key, bool fallback) {
    const JsonValue* value = obj.get(key);
    return value ? value->asBool() : fallback;
}

int intFromObjectOrDefault(const JsonValue& obj, const std::string& key, int fallback) {
    const JsonValue* value = obj.get(key);
    return value && value->isNumber() ? static_cast<int>(value->asNumber()) : fallback;
}

std::string stringFromObjectOrDefault(const JsonValue& obj, const std::string& key, const std::string& fallback) {
    const JsonValue* value = obj.get(key);
    return value && value->isString() ? value->asString() : fallback;
}

Color parseHexColorString(const std::string& value, const Color& fallback) {
    if (value.size() != 7 || value[0] != '#') {
        return fallback;
    }
    try {
        const auto parse_component = [&](std::size_t offset) -> int {
            return std::stoi(value.substr(offset, 2), nullptr, 16);
        };
        return Color{parse_component(1), parse_component(3), parse_component(5), 255};
    } catch (...) {
        return fallback;
    }
}

void setDrawColor(SDL_Renderer* renderer, const Color& c) {
    SDL_SetRenderDrawColor(renderer, static_cast<Uint8>(c.r), static_cast<Uint8>(c.g), static_cast<Uint8>(c.b),
        static_cast<Uint8>(c.a));
}

void fillRoundedRectScanlines(SDL_Renderer* renderer, int x, int y, int w, int h, int radius, const Color& c) {
    if (w <= 0 || h <= 0) {
        return;
    }
    radius = std::max(0, std::min(radius, std::min(w, h) / 2));
    setDrawColor(renderer, c);
    for (int yy = y; yy < y + h; ++yy) {
        int x0 = x;
        int x1 = x + w;
        if (yy < y + radius) {
            const int dy = yy - (y + radius);
            const int disc = radius * radius - dy * dy;
            if (disc >= 0) {
                const int s = static_cast<int>(std::lround(std::sqrt(static_cast<double>(disc))));
                x0 = (x + radius) - s;
                x1 = (x + w - radius) + s;
            }
        } else if (yy >= y + h - radius) {
            const int dy = yy - (y + h - radius);
            const int disc = radius * radius - dy * dy;
            if (disc >= 0) {
                const int s = static_cast<int>(std::lround(std::sqrt(static_cast<double>(disc))));
                x0 = (x + radius) - s;
                x1 = (x + w - radius) + s;
            }
        }
        if (x1 > x0) {
            SDL_Rect row{x0, yy, x1 - x0, 1};
            SDL_RenderFillRect(renderer, &row);
        }
    }
}

void fillRoundedRingScanlines(
    SDL_Renderer* renderer,
    int x,
    int y,
    int w,
    int h,
    int outer_radius,
    int stroke,
    const Color& border,
    const Color& inner_fill) {
    if (w <= 0 || h <= 0 || stroke <= 0 || stroke * 2 >= w || stroke * 2 >= h) {
        return;
    }
    fillRoundedRectScanlines(renderer, x, y, w, h, outer_radius, border);
    const int inner_r = std::max(0, outer_radius - stroke);
    fillRoundedRectScanlines(renderer, x + stroke, y + stroke, w - 2 * stroke, h - 2 * stroke, inner_r, inner_fill);
}

void fillTriangleScanlines(SDL_Renderer* renderer, int x0, int y0, int x1, int y1, int x2, int y2, const Color& c) {
    struct P {
        int x;
        int y;
    };
    P p[3] = {{x0, y0}, {x1, y1}, {x2, y2}};
    std::sort(p, p + 3, [](const P& a, const P& b) { return a.y < b.y; });

    if (p[0].y == p[2].y) {
        return;
    }

    setDrawColor(renderer, c);
    auto edge_x = [](int x_a, int y_a, int x_b, int y_b, int y) -> double {
        if (y_b == y_a) {
            return static_cast<double>(x_a);
        }
        return static_cast<double>(x_a) +
            static_cast<double>(x_b - x_a) * static_cast<double>(y - y_a) / static_cast<double>(y_b - y_a);
    };

    auto fill_span = [&](int y, double xa, double xb) {
        int x_lo = static_cast<int>(std::floor(std::min(xa, xb)));
        int x_hi = static_cast<int>(std::ceil(std::max(xa, xb)));
        if (x_hi <= x_lo) {
            x_hi = x_lo + 1;
        }
        SDL_Rect row{x_lo, y, x_hi - x_lo, 1};
        SDL_RenderFillRect(renderer, &row);
    };

    if (p[0].y == p[1].y) {
        for (int y = p[0].y; y <= p[2].y; ++y) {
            const double xl = edge_x(p[0].x, p[0].y, p[2].x, p[2].y, y);
            const double xr = edge_x(p[1].x, p[1].y, p[2].x, p[2].y, y);
            fill_span(y, xl, xr);
        }
        return;
    }
    if (p[1].y == p[2].y) {
        for (int y = p[0].y; y <= p[1].y; ++y) {
            const double xl = edge_x(p[0].x, p[0].y, p[1].x, p[1].y, y);
            const double xr = edge_x(p[0].x, p[0].y, p[2].x, p[2].y, y);
            fill_span(y, xl, xr);
        }
        return;
    }

    for (int y = p[0].y; y < p[1].y; ++y) {
        const double xl = edge_x(p[0].x, p[0].y, p[1].x, p[1].y, y);
        const double xr = edge_x(p[0].x, p[0].y, p[2].x, p[2].y, y);
        fill_span(y, xl, xr);
    }
    for (int y = p[1].y; y <= p[2].y; ++y) {
        const double xl = edge_x(p[1].x, p[1].y, p[2].x, p[2].y, y);
        const double xr = edge_x(p[0].x, p[0].y, p[2].x, p[2].y, y);
        fill_span(y, xl, xr);
    }
}

/// Filled stroke along segment (x0,y0)–(x1,y1); used for triangle legs without drawing the base edge.
void fillThickSegmentScanlines(SDL_Renderer* renderer, int x0, int y0, int x1, int y1, int thickness, const Color& c) {
    if (thickness <= 0) {
        return;
    }
    const double dx = static_cast<double>(x1 - x0);
    const double dy = static_cast<double>(y1 - y0);
    const double len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-4) {
        return;
    }
    const double hx = -dy / len * (static_cast<double>(thickness) * 0.5);
    const double hy = dx / len * (static_cast<double>(thickness) * 0.5);
    const int ax0 = x0 + static_cast<int>(std::lround(hx));
    const int ay0 = y0 + static_cast<int>(std::lround(hy));
    const int ax1 = x0 - static_cast<int>(std::lround(hx));
    const int ay1 = y0 - static_cast<int>(std::lround(hy));
    const int bx0 = x1 + static_cast<int>(std::lround(hx));
    const int by0 = y1 + static_cast<int>(std::lround(hy));
    const int bx1 = x1 - static_cast<int>(std::lround(hx));
    const int by1 = y1 - static_cast<int>(std::lround(hy));
    fillTriangleScanlines(renderer, ax0, ay0, ax1, ay1, bx1, by1, c);
    fillTriangleScanlines(renderer, ax0, ay0, bx1, by1, bx0, by0, c);
}

bool focusIdUsesSpeechBubble(FocusNodeId id) {
    if (id >= 1000 && id <= 1029) {
        return true;
    }
    if (id >= 2000 && id <= 2029) {
        return true;
    }
    return id == 1111 || id == 2111;
}

std::string prettySpeciesNameFromSlug(const std::string& slug, int gender = -1, int species_id = -1) {
    if (slug.empty()) {
        return "";
    }
    std::string base = slug;
    if (base.size() > 4 && base.substr(base.size() - 4) == ".png") {
        base.resize(base.size() - 4);
    }
    // Strip variation selectors for consistent matching.
    auto replace_all = [](std::string s, const std::string& from, const std::string& to) {
        std::size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
        return s;
    };
    base = replace_all(std::move(base), u8"\uFE0F", "");
    base = replace_all(std::move(base), u8"\uFE0E", "");
    // Render gendered Nidoran nicely.
    {
        const std::string low = asciiLowerCopy(base);
        const bool is_nidoran = low.find("nidoran") != std::string::npos;
        if (is_nidoran) {
            if (species_id == 32) {
                return "Nidoran\u2642";
            }
            if (species_id == 29) {
                return "Nidoran\u2640";
            }
            if (gender == 0) {
                return "Nidoran\u2642";
            }
            if (gender == 1) {
                return "Nidoran\u2640";
            }
            if (base.find(u8"♂") != std::string::npos || base.find(u8"\u2642") != std::string::npos) {
                return "Nidoran\u2642";
            }
            if (base.find(u8"♀") != std::string::npos || base.find(u8"\u2640") != std::string::npos) {
                return "Nidoran\u2640";
            }
            if (low.find("_m") != std::string::npos || low.find("-m") != std::string::npos || low.find("male") != std::string::npos) {
                return "Nidoran\u2642";
            }
            if (low.find("_f") != std::string::npos || low.find("-f") != std::string::npos || low.find("female") != std::string::npos) {
                return "Nidoran\u2640";
            }
        }
    }
    for (char& ch : base) {
        if (ch == '-' || ch == '_') {
            ch = ' ';
        }
    }
    bool cap_next = true;
    for (char& ch : base) {
        const unsigned char u = static_cast<unsigned char>(ch);
        if (std::isspace(u)) {
            cap_next = true;
        } else if (cap_next) {
            ch = static_cast<char>(std::toupper(u));
            cap_next = false;
        } else {
            ch = static_cast<char>(std::tolower(u));
        }
    }
    return base;
}

std::string replaceAllCopy(std::string s, const std::string& from, const std::string& to) {
    std::size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::string canonicalPokemonNameKey(std::string value) {
    std::string out;
    out.reserve(value.size());
    for (unsigned char ch : value) {
        if (std::isalnum(ch)) {
            out.push_back(static_cast<char>(std::tolower(ch)));
        }
    }
    return out;
}

std::string formatPokemonSpeechLine(
    const GameTransferSpeechBubbleCursorStyle& st,
    const PcSlotSpecies& slot) {
    const std::string species = !slot.species_name.empty()
        ? slot.species_name
        : prettySpeciesNameFromSlug(slot.slug, slot.gender, slot.species_id);
    const std::string nickname = slot.nickname;
    const bool nickname_is_distinct =
        !nickname.empty() &&
        canonicalPokemonNameKey(nickname) != canonicalPokemonNameKey(species);
    const std::string name = nickname_is_distinct ? nickname : species;
    std::string line = st.pokemon_label_format;
    line = replaceAllCopy(std::move(line), "{name}", name);
    line = replaceAllCopy(std::move(line), "{nickname}", nickname_is_distinct ? nickname : "");
    line = replaceAllCopy(std::move(line), "{species}", species);
    const int level = slot.level > 0 ? slot.level : st.default_pokemon_level;
    line = replaceAllCopy(std::move(line), "{level}", std::to_string(level));
    return line;
}

/// Critically damped–style smoothing: approaches `target` faster when far, eases in at the end (inertial feel).
void approachExponential(double& v, double target, double dt, double lambda) {
    if (lambda <= 1e-9) {
        v = target;
        return;
    }
    const double alpha = 1.0 - std::exp(-lambda * dt);
    v += (target - v) * alpha;
    if (std::fabs(target - v) < 0.0005) {
        v = target;
    }
}

int carouselIconClipInset(const GameTransferToolCarouselStyle& st, int vw, int vh, int radius) {
    if (st.viewport_clip_inset > 0) {
        return std::clamp(st.viewport_clip_inset, 1, std::max(1, std::min(vw, vh) / 2 - 1));
    }
    return std::clamp(radius, 1, std::max(1, std::min(vw, vh) / 2 - 1));
}

void getPillTrackBounds(const GameTransferPillToggleStyle& st, int screen_w, int& tx, int& ty, int& tw, int& th) {
    const int right_col_x = screen_w - 40 - BoxViewport::kViewportWidth;
    tx = right_col_x + (BoxViewport::kViewportWidth - st.track_width) / 2;
    ty = kBoxViewportY - st.gap_above_boxes - st.track_height;
    tw = st.track_width;
    th = st.track_height;
}

} // namespace

namespace {
constexpr const char* kDefaultResortProfileId = "default";
}

TransferSystemScreen::TransferSystemScreen(
    SDL_Renderer* renderer,
    const WindowConfig& window_config,
    const std::string& font_path,
    const std::string& project_root,
    std::shared_ptr<PokeSpriteAssets> sprite_assets,
    resort::PokemonResortService* resort_service)
    : window_config_(window_config),
      renderer_(renderer),
      project_root_(project_root),
      font_path_(font_path),
      sprite_assets_(std::move(sprite_assets)),
      resort_service_(resort_service),
      background_(loadTexture(
          renderer,
          resolvePath(project_root_, "assets/transfer_select_save/background.png"))) {
    const transfer_system::LoadedGameTransfer loaded = transfer_system::loadGameTransfer(project_root_);
    resort_pc_box_count_ = std::clamp(loaded.resort_pc_box_count, 1, 512);
    ui_state_.configure(loaded.fade_in_seconds, loaded.fade_out_seconds);
    background_animation_.enabled = loaded.background_animation.enabled;
    background_animation_.scale = loaded.background_animation.scale;
    background_animation_.speed_x = loaded.background_animation.speed_x;
    background_animation_.speed_y = loaded.background_animation.speed_y;
    pill_style_ = loaded.pill_toggle;
    carousel_style_ = loaded.tool_carousel;
    exit_button_enabled_ = loaded.exit_button_enabled;
    exit_button_gap_pixels_ = loaded.exit_button_gap_pixels;
    exit_button_icon_scale_ = loaded.exit_button_icon_scale;
    box_name_dropdown_style_ = loaded.box_name_dropdown;
    selection_cursor_style_ = loaded.selection_cursor;
    mini_preview_style_ = loaded.mini_preview;
    box_viewport_style_ = loaded.box_viewport;
    pokemon_action_menu_style_ = loaded.pokemon_action_menu;
    box_space_long_press_style_ = loaded.box_space_long_press;
    info_banner_style_ = loaded.info_banner;
    pill_font_ = loadFontPreferringUnicode(font_path_, std::max(8, pill_style_.font_pt), project_root_);
    dropdown_item_font_ =
        loadFontPreferringUnicode(font_path_, std::max(8, box_name_dropdown_style_.item_font_pt), project_root_);
    box_rename_modal_body_font_ =
        loadFontPreferringUnicode(font_path_, std::max(22, box_name_dropdown_style_.item_font_pt + 8), project_root_);
    speech_bubble_font_ = loadFontPreferringUnicode(
        font_path_,
        std::max(8, selection_cursor_style_.speech_bubble.font_pt),
        project_root_);
    pokemon_action_menu_font_ = loadFontPreferringUnicode(
        font_path_,
        std::max(8, pokemon_action_menu_style_.font_pt),
        project_root_);
    cachePillLabelTextures(renderer);

    const std::array<std::string, 4> tool_paths{
        carousel_style_.texture_multiple,
        carousel_style_.texture_basic,
        carousel_style_.texture_swap,
        carousel_style_.texture_items};
    for (std::size_t i = 0; i < tool_paths.size(); ++i) {
        tool_icons_[i] = loadTextureOptional(renderer, resolvePath(project_root_, tool_paths[i]));
    }
    exit_button_icon_ = loadTextureOptional(renderer, resolvePath(project_root_, "assets/game_transfer/exit.png"));

    box_space_full_tex_ = loadTextureOptional(renderer, resolvePath(project_root_, "assets/pokesprite/boxes/full.png"));
    box_space_empty_tex_ = loadTextureOptional(renderer, resolvePath(project_root_, "assets/pokesprite/boxes/empty.png"));
    box_space_noempty_tex_ = loadTextureOptional(renderer, resolvePath(project_root_, "assets/pokesprite/boxes/noempty.png"));
    setTextureNearestNeighbor(box_space_full_tex_);
    setTextureNearestNeighbor(box_space_empty_tex_);
    setTextureNearestNeighbor(box_space_noempty_tex_);

    constexpr int kBoxMarginX = 40;
    const int game_box_x = std::max(0, window_config_.virtual_width - kBoxMarginX - BoxViewport::kViewportWidth);
    resort_box_viewport_ = std::make_unique<BoxViewport>(
        renderer,
        project_root,
        font_path,
        loaded.box_viewport,
        BoxViewportRole::ResortStorage,
        kBoxMarginX,
        kBoxViewportY);
    game_save_box_viewport_ = std::make_unique<BoxViewport>(
        renderer,
        project_root,
        font_path,
        loaded.box_viewport,
        BoxViewportRole::ExternalGameSave,
        game_box_x,
        kBoxViewportY);
}

void TransferSystemScreen::cachePillLabelTextures(SDL_Renderer* renderer) {
    const Color black{0, 0, 0, 255};
    const Color white{255, 255, 255, 255};
    pill_label_pokemon_black_ = renderTextTexture(renderer, pill_font_.get(), "Pokemon", black);
    pill_label_items_black_ = renderTextTexture(renderer, pill_font_.get(), "Items", black);
    pill_label_pokemon_white_ = renderTextTexture(renderer, pill_font_.get(), "Pokemon", white);
    pill_label_items_white_ = renderTextTexture(renderer, pill_font_.get(), "Items", white);
}

void TransferSystemScreen::initializeResortPcBoxesFromStorage(SDL_Renderer* renderer) {
    resort_pc_boxes_.clear();
    if (!resort_service_) {
        resort_pc_boxes_.reserve(static_cast<std::size_t>(resort_pc_box_count_));
        for (int b = 0; b < resort_pc_box_count_; ++b) {
            TransferSaveSelection::PcBox box;
            box.name = "RESORT " + std::to_string(b + 1);
            box.slots.assign(30, PcSlotSpecies{});
            resort_pc_boxes_.push_back(std::move(box));
        }
        return;
    }

    resort_service_->ensureProfile(kDefaultResortProfileId);
    const auto headers = resort_service_->listProfileBoxes(kDefaultResortProfileId);
    if (headers.empty()) {
        resort_pc_boxes_.reserve(static_cast<std::size_t>(resort_pc_box_count_));
        for (int b = 0; b < resort_pc_box_count_; ++b) {
            TransferSaveSelection::PcBox box;
            box.name = "RESORT " + std::to_string(b + 1);
            box.slots.assign(30, PcSlotSpecies{});
            resort_pc_boxes_.push_back(std::move(box));
        }
        return;
    }

    resort_pc_boxes_.reserve(headers.size());
    for (const auto& header : headers) {
        const int box_id = header.first;
        TransferSaveSelection::PcBox box;
        box.name = "RESORT " + std::to_string(box_id + 1);
        box.slots.assign(30, PcSlotSpecies{});
        const std::vector<resort::PokemonSlotView> views =
            resort_service_->getBoxSlotViews(kDefaultResortProfileId, box_id);
        for (const auto& view : views) {
            if (view.slot_index >= 0 && view.slot_index < 30) {
                box.slots[static_cast<std::size_t>(view.slot_index)] =
                    pcSlotFromResortSlotView(view, box_id, view.slot_index);
            }
        }
        resort_pc_boxes_.push_back(std::move(box));
    }

    if (sprite_assets_ && renderer) {
        for (const auto& b : resort_pc_boxes_) {
            for (const auto& slot : b.slots) {
                if (slot.occupied()) {
                    (void)sprite_assets_->loadPokemonTexture(renderer, slot);
                }
            }
        }
    }
}

PcSlotSpecies TransferSystemScreen::pcSlotFromResortSlotView(
    const resort::PokemonSlotView& view,
    int box_id,
    int slot_index) const {
    PcSlotSpecies s{};
    s.present = true;
    s.resort_pkrid = view.pkrid;
    s.area = "resort";
    s.box_index = box_id;
    s.slot_index = slot_index;
    s.species_id = static_cast<int>(view.species_id);
    s.form = static_cast<int>(view.form_id);
    s.level = static_cast<int>(view.level);
    s.is_shiny = view.shiny;
    s.gender = static_cast<int>(view.gender);
    if (!view.display_name.empty() && view.display_name != view.pkrid) {
        s.nickname = view.display_name;
    }
    if (view.held_item_id.has_value()) {
        s.held_item_id = static_cast<int>(*view.held_item_id);
    }
    if (sprite_assets_) {
        PokemonSpriteRequest rq{};
        rq.species_id = s.species_id;
        rq.gender = s.gender;
        rq.is_shiny = s.is_shiny;
        const ResolvedPokemonSprite r = sprite_assets_->resolvePokemon(rq);
        s.slug = r.species_slug;
        s.form_key = r.resolved_form_key;
    }
    return s;
}

void TransferSystemScreen::persistResortPokemonDropToStorage(
    const transfer_system::PokemonMoveController::SlotRef& target,
    const transfer_system::PokemonMoveController::SlotRef& return_slot,
    const bool target_was_occupied,
    const bool swap_into_hand,
    const std::string& held_pkrid,
    const std::string& target_pkrid_before) {
    using Move = transfer_system::PokemonMoveController;
    if (!resort_service_ || swap_into_hand) {
        return;
    }
    if (target.panel != Move::Panel::Resort || return_slot.panel != Move::Panel::Resort) {
        return;
    }
    try {
        if (!target_was_occupied && !held_pkrid.empty()) {
            resort_service_->movePokemonToSlot(
                resort::BoxLocation{kDefaultResortProfileId, target.box_index, target.slot_index},
                held_pkrid,
                resort::BoxPlacementPolicy::RejectIfOccupied);
        } else if (target_was_occupied && !held_pkrid.empty() && !target_pkrid_before.empty()) {
            resort_service_->swapResortSlotContents(
                resort::BoxLocation{kDefaultResortProfileId, return_slot.box_index, return_slot.slot_index},
                resort::BoxLocation{kDefaultResortProfileId, target.box_index, target.slot_index});
        }
    } catch (const std::exception& ex) {
        std::cerr << "Warning: could not persist Resort box move to profile.resort.db: " << ex.what() << '\n';
    }
}

#ifdef PR_ENABLE_TEST_HOOKS
std::optional<SDL_Rect> TransferSystemScreen::debugPillTrackBounds() const {
    int tx = 0;
    int ty = 0;
    int tw = 0;
    int th = 0;
    getPillTrackBounds(pill_style_, window_config_.virtual_width, tx, ty, tw, th);
    const int enter_off =
        static_cast<int>(std::lround((1.0 - ui_state_.uiEnter()) * static_cast<double>(-(th + 24))));
    ty += enter_off;
    return SDL_Rect{tx, ty, tw, th};
}

std::optional<int> TransferSystemScreen::debugDropdownRowAtScreen(int logical_x, int logical_y) const {
    return dropdownRowIndexAtScreen(logical_x, logical_y);
}
#endif

void TransferSystemScreen::enter(const TransferSaveSelection& selection, SDL_Renderer* renderer, int initial_game_box_index) {
    closeBoxRenameModal(false);
    ui_state_.enter();
    transfer_selection_ = selection;
    initializeResortPcBoxesFromStorage(renderer);
    resort_box_browser_.enter(static_cast<int>(resort_pc_boxes_.size()), 0);
    pokemon_move_.clear();
    multi_pokemon_move_.clear();
    held_move_sprite_tex_ = {};
    pickup_sfx_requested_ = false;
    putdown_sfx_requested_ = false;
    selection_cursor_hidden_after_mouse_ = false;
    speech_hover_active_ = false;
    dropdown_lmb_down_in_panel_ = false;
    dropdown_lmb_drag_accum_ = 0.0;
    dropdown_labels_dirty_ = false;
    dropdown_item_textures_.clear();
    current_game_key_ = selection.game_key;
    selection_game_title_ = selection.game_title;
    speech_bubble_label_cache_.clear();
    speech_bubble_label_tex_ = {};
    mouse_hover_mini_preview_box_index_ = -1;
    mouse_hover_focus_node_ = -1;
    last_pointer_position_ = SDL_Point{0, 0};
    box_space_drag_active_ = false;
    box_space_drag_last_y_ = 0;
    box_space_drag_accum_ = 0.0;
    box_space_pressed_cell_ = -1;
    multi_select_drag_active_ = false;
    multi_select_drag_rect_ = SDL_Rect{0, 0, 0, 0};
    // Focus graph is rebuilt at end of enter() once bounds are valid.

    if (resort_box_viewport_) {
        // Match game column: controller `resort_box_browser_.enter()` clears Box Space, but the viewport
        // keeps `box_space_active_` / header mode until we reset it (otherwise the footer stays green).
        resort_box_viewport_->setHeaderMode(BoxViewport::HeaderMode::Normal, false);
        resort_box_viewport_->setBoxSpaceActive(false);
        resort_box_viewport_->setModel(resortBoxViewportModel());
        resort_box_viewport_->reloadResortIcon(renderer);
    }
    if (game_save_box_viewport_) {
        game_save_box_viewport_->setModel(BoxViewportModel{});
        game_save_box_viewport_->reloadGameIcon(renderer, selection.game_key);
        game_save_box_viewport_->setHeaderMode(BoxViewport::HeaderMode::Normal, false);
        game_save_box_viewport_->setBoxSpaceActive(false);
    }

    // Capture the PC box list (right box only). Prefer full `pc_boxes`; fall back to legacy box1.
    game_pc_boxes_.clear();
    if (!selection.pc_boxes.empty()) {
        game_pc_boxes_ = selection.pc_boxes;
    } else if (!selection.box1_slots.empty()) {
        TransferSaveSelection::PcBox b;
        b.name = "BOX 1";
        b.slots = selection.box1_slots;
        if (b.slots.size() < 30) {
            b.slots.resize(30);
        } else if (b.slots.size() > 30) {
            b.slots.resize(30);
        }
        game_pc_boxes_.push_back(std::move(b));
    }

    game_box_browser_.enter(static_cast<int>(game_pc_boxes_.size()), initial_game_box_index);

    auto sprite_for = [&](const PcSlotSpecies& slot) -> std::optional<TextureHandle> {
        if (!slot.occupied() || !sprite_assets_) {
            return std::nullopt;
        }
        TextureHandle texture = sprite_assets_->loadPokemonTexture(renderer, slot);
        return texture.texture ? std::optional<TextureHandle>(std::move(texture)) : std::nullopt;
    };

    // Preload all sprites referenced by the save boxes so navigation is smooth (no IO in input handlers).
    {
        for (const auto& b : game_pc_boxes_) {
            for (const auto& slot : b.slots) {
                if (slot.occupied()) {
                    (void)sprite_for(slot);
                }
            }
        }
    }

    auto build_box_model = [&](int box_index) -> BoxViewportModel {
        BoxViewportModel m;
        if (box_index >= 0 && static_cast<std::size_t>(box_index) < game_pc_boxes_.size()) {
            const auto& b = game_pc_boxes_[static_cast<std::size_t>(box_index)];
            m.box_name = b.name;
            for (std::size_t i = 0; i < m.slot_sprites.size() && i < b.slots.size(); ++i) {
                const auto& slot = b.slots[i];
                m.slot_sprites[i] = sprite_for(slot);
                    if (slot.occupied() && slot.held_item_id > 0 && sprite_assets_ && renderer_) {
                        TextureHandle item = sprite_assets_->loadItemTexture(renderer_, slot.held_item_id);
                        m.held_item_sprites[i] =
                            item.texture ? std::optional<TextureHandle>(std::move(item)) : std::nullopt;
                    }
            }
        }
        return m;
    };

    if (game_save_box_viewport_ && !game_pc_boxes_.empty()) {
        game_save_box_viewport_->setModel(build_box_model(game_box_browser_.gameBoxIndex()));
    }
    // Build focus nodes (automatic spatial navigation + overrides).
    {
        std::vector<FocusNode> nodes;
        nodes.reserve(120);
        auto add = [&](FocusNode n) { nodes.push_back(std::move(n)); };

        // Resort grid slots.
        if (resort_box_viewport_) {
            for (int i = 0; i < 30; ++i) {
                add(FocusNode{
                    1000 + i,
                    [this, i]() -> std::optional<SDL_Rect> {
                        if (!resort_box_viewport_) return std::nullopt;
                        SDL_Rect r;
                        if (!resort_box_viewport_->getSlotBounds(i, r)) return std::nullopt;
                        return r;
                    },
                    [this]() { (void)activateFocusedResortSlot(); },
                    nullptr});
            }
            add(FocusNode{1101, [this]() -> std::optional<SDL_Rect> {
                              if (!resort_box_viewport_) return std::nullopt;
                              SDL_Rect r;
                              return resort_box_viewport_->getPrevArrowBounds(r) ? std::optional<SDL_Rect>(r) : std::nullopt;
                          },
                          [this]() { advanceResortBox(-1); },
                          nullptr});
            add(FocusNode{1102, [this]() -> std::optional<SDL_Rect> {
                              if (!resort_box_viewport_) return std::nullopt;
                              SDL_Rect r;
                              return resort_box_viewport_->getNamePlateBounds(r) ? std::optional<SDL_Rect>(r) : std::nullopt;
                          },
                          []() {}, nullptr});
            add(FocusNode{1103, [this]() -> std::optional<SDL_Rect> {
                              if (!resort_box_viewport_) return std::nullopt;
                              SDL_Rect r;
                              return resort_box_viewport_->getNextArrowBounds(r) ? std::optional<SDL_Rect>(r) : std::nullopt;
                          },
                          [this]() { advanceResortBox(1); },
                          nullptr});
            add(FocusNode{1110, [this]() -> std::optional<SDL_Rect> {
                              if (!resort_box_viewport_) return std::nullopt;
                              SDL_Rect r;
                              return resort_box_viewport_->getFooterBoxSpaceBounds(r) ? std::optional<SDL_Rect>(r) : std::nullopt;
                          },
                          [this]() {
                              setResortBoxSpaceMode(!resort_box_browser_.gameBoxSpaceMode());
                              ui_state_.requestButtonSfx();
                              closeResortBoxDropdown();
                          },
                          nullptr});
            add(FocusNode{1111, [this]() -> std::optional<SDL_Rect> {
                              if (!resort_box_viewport_) return std::nullopt;
                              SDL_Rect r;
                              return resort_box_viewport_->getFooterGameIconBounds(r) ? std::optional<SDL_Rect>(r) : std::nullopt;
                          },
                          []() {}, nullptr});
            add(FocusNode{1112, [this]() -> std::optional<SDL_Rect> {
                              if (!resort_box_viewport_) return std::nullopt;
                              SDL_Rect r;
                              return resort_box_viewport_->getResortScrollArrowBounds(r) ? std::optional<SDL_Rect>(r) : std::nullopt;
                          },
                          [this]() { stepResortBoxSpaceRowDown(); },
                          nullptr});
        }

        // Game grid slots.
        if (game_save_box_viewport_) {
            for (int i = 0; i < 30; ++i) {
                add(FocusNode{
                    2000 + i,
                    [this, i]() -> std::optional<SDL_Rect> {
                        if (!game_save_box_viewport_) return std::nullopt;
                        SDL_Rect r;
                        if (!game_save_box_viewport_->getSlotBounds(i, r)) return std::nullopt;
                        return r;
                    },
                    [this]() {
                        (void)activateFocusedGameSlot();
                    },
                    nullptr});
            }
            add(FocusNode{2101, [this]() -> std::optional<SDL_Rect> {
                              if (!game_save_box_viewport_) return std::nullopt;
                              SDL_Rect r;
                              return game_save_box_viewport_->getPrevArrowBounds(r) ? std::optional<SDL_Rect>(r) : std::nullopt;
                          },
                          [this]() { advanceGameBox(-1); },
                          nullptr});
            add(FocusNode{2102, [this]() -> std::optional<SDL_Rect> {
                              if (!game_save_box_viewport_) return std::nullopt;
                              SDL_Rect r;
                              return game_save_box_viewport_->getNamePlateBounds(r) ? std::optional<SDL_Rect>(r) : std::nullopt;
                          },
                          []() {},
                          nullptr});
            add(FocusNode{2103, [this]() -> std::optional<SDL_Rect> {
                              if (!game_save_box_viewport_) return std::nullopt;
                              SDL_Rect r;
                              return game_save_box_viewport_->getNextArrowBounds(r) ? std::optional<SDL_Rect>(r) : std::nullopt;
                          },
                          [this]() { advanceGameBox(1); },
                          nullptr});
            add(FocusNode{2112, [this]() -> std::optional<SDL_Rect> {
                              if (!game_save_box_viewport_) return std::nullopt;
                              SDL_Rect r;
                              return game_save_box_viewport_->getBoxSpaceScrollArrowBounds(r) ? std::optional<SDL_Rect>(r) : std::nullopt;
                          },
                          [this]() { stepGameBoxSpaceRowDown(); },
                          nullptr});
            add(FocusNode{2110, [this]() -> std::optional<SDL_Rect> {
                              if (!game_save_box_viewport_) return std::nullopt;
                              SDL_Rect r;
                              return game_save_box_viewport_->getFooterBoxSpaceBounds(r) ? std::optional<SDL_Rect>(r) : std::nullopt;
                          },
                          [this]() {
                              setGameBoxSpaceMode(!game_box_browser_.gameBoxSpaceMode());
                              ui_state_.requestButtonSfx();
                              closeGameBoxDropdown();
                          },
                          nullptr});
            add(FocusNode{2111, [this]() -> std::optional<SDL_Rect> {
                              if (!game_save_box_viewport_) return std::nullopt;
                              SDL_Rect r;
                              return game_save_box_viewport_->getFooterGameIconBounds(r) ? std::optional<SDL_Rect>(r) : std::nullopt;
                          },
                          []() {}, nullptr});
        }

        // Tool carousel.
        if (exit_button_enabled_) {
            add(FocusNode{
                5000,
                [this]() -> std::optional<SDL_Rect> {
                    const int bs = carousel_style_.viewport_height;
                    const int bx = carousel_style_.offset_from_left_wall;
                    const int by = exitButtonScreenY();
                    if (bs <= 0) return std::nullopt;
                    return SDL_Rect{bx, by, bs, bs};
                },
                [this]() {
                    ui_state_.requestButtonSfx();
                    onBackPressed();
                },
                nullptr});
        }
        add(FocusNode{
            3000,
            [this]() -> std::optional<SDL_Rect> {
                const int vx = carousel_style_.offset_from_left_wall +
                    (exit_button_enabled_ ? (carousel_style_.viewport_height + exit_button_gap_pixels_) : 0);
                const int vy = carouselScreenY();
                return SDL_Rect{vx, vy, carousel_style_.viewport_width, carousel_style_.viewport_height};
            },
            []() {},
            [this](int dx, int dy) -> bool {
                if (dx != 0) {
                    cycleToolCarousel(dx);
                    return true;
                }
                (void)dy;
                return false;
            }});

        // Pill toggle.
        add(FocusNode{
            4000,
            [this]() -> std::optional<SDL_Rect> {
                int tx=0,ty=0,tw=0,th=0;
                getPillTrackBounds(pill_style_, window_config_.virtual_width, tx, ty, tw, th);
                const int enter_off =
                    static_cast<int>(std::lround((1.0 - ui_state_.uiEnter()) * static_cast<double>(-(th + 24))));
                ty += enter_off;
                return SDL_Rect{tx, ty, tw, th};
            },
            [this]() { togglePillTarget(); },
            [this](int dx, int dy) -> bool {
                if (dx != 0) {
                    togglePillTarget();
                    return true;
                }
                (void)dy;
                return false;
            }});

        transfer_system::applyTransferSystemFocusEdges(nodes);
        focus_.setExplicitNavigationOnly(true);
        focus_.setNodes(std::move(nodes));
        focus_.setCurrent(1000); // resort slot 1 (top-left)
    }
    syncBoxViewportPositions();
}

namespace {

void drawRoundedOutlineScanlines(SDL_Renderer* renderer, int x, int y, int w, int h, int radius, const Color& c, int thickness) {
    if (w <= 0 || h <= 0 || thickness <= 0) {
        return;
    }
    radius = std::clamp(radius, 0, std::min(w, h) / 2);
    thickness = std::clamp(thickness, 1, std::min(w, h) / 2);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, static_cast<Uint8>(c.r), static_cast<Uint8>(c.g), static_cast<Uint8>(c.b), static_cast<Uint8>(c.a));

    const int inner_x = x + thickness;
    const int inner_y = y + thickness;
    const int inner_w = w - thickness * 2;
    const int inner_h = h - thickness * 2;
    const int inner_r = std::max(0, radius - thickness);

    auto span_for = [&](int yy, int ox, int oy, int ow, int oh, int r) -> std::pair<int, int> {
        int x0 = ox;
        int x1 = ox + ow;
        if (r > 0) {
            if (yy < oy + r) {
                const int dy = yy - (oy + r);
                const int disc = r * r - dy * dy;
                if (disc >= 0) {
                    const int s = static_cast<int>(std::lround(std::sqrt(static_cast<double>(disc))));
                    x0 = (ox + r) - s;
                    x1 = (ox + ow - r) + s;
                }
            } else if (yy >= oy + oh - r) {
                const int dy = yy - (oy + oh - r);
                const int disc = r * r - dy * dy;
                if (disc >= 0) {
                    const int s = static_cast<int>(std::lround(std::sqrt(static_cast<double>(disc))));
                    x0 = (ox + r) - s;
                    x1 = (ox + ow - r) + s;
                }
            }
        }
        return {x0, x1};
    };

    for (int yy = y; yy < y + h; ++yy) {
        const auto [ox0, ox1] = span_for(yy, x, y, w, h, radius);
        int ix0 = 0;
        int ix1 = 0;
        bool has_inner = inner_w > 0 && inner_h > 0 && yy >= inner_y && yy < inner_y + inner_h;
        if (has_inner) {
            const auto inner = span_for(yy, inner_x, inner_y, inner_w, inner_h, inner_r);
            ix0 = inner.first;
            ix1 = inner.second;
        }

        // Left segment.
        const int left_w = has_inner ? std::max(0, ix0 - ox0) : std::max(0, ox1 - ox0);
        if (left_w > 0) {
            SDL_Rect rct{ox0, yy, left_w, 1};
            SDL_RenderFillRect(renderer, &rct);
        }
        if (has_inner) {
            const int right_w = std::max(0, ox1 - ix1);
            if (right_w > 0) {
                SDL_Rect rct{ix1, yy, right_w, 1};
                SDL_RenderFillRect(renderer, &rct);
            }
        }
    }
}

} // namespace

std::string TransferSystemScreen::speechBubbleLineForFocus(FocusNodeId focus_id) const {
    const GameTransferSpeechBubbleCursorStyle& sb = selection_cursor_style_.speech_bubble;
    if (focus_id == 1111) {
        return sb.resort_game_title;
    }
    if (focus_id == 2111) {
        if (!selection_game_title_.empty()) {
            return selection_game_title_;
        }
        return prettySpeciesNameFromSlug(current_game_key_);
    }
    int slot = -1;
    if (focus_id >= 1000 && focus_id <= 1029) {
        slot = focus_id - 1000;
    } else if (focus_id >= 2000 && focus_id <= 2029) {
        slot = focus_id - 2000;
    } else {
        return "";
    }
    if (slot < 0 || slot >= 30) {
        return "";
    }
    if (focus_id >= 2000) {
        if (game_box_browser_.gameBoxSpaceMode()) {
            const int box_index = game_box_browser_.gameBoxSpaceRowOffset() * 6 + slot;
            if (box_index < 0 || box_index >= static_cast<int>(game_pc_boxes_.size())) {
                return "";
            }
            const std::string& name = game_pc_boxes_[static_cast<std::size_t>(box_index)].name;
            return name.empty() ? std::string("BOX") : name;
        }
        const int game_box_index = game_box_browser_.gameBoxIndex();
        if (game_box_index < 0 || game_box_index >= static_cast<int>(game_pc_boxes_.size())) {
            return sb.empty_slot_label;
        }
        const auto& slots = game_pc_boxes_[static_cast<std::size_t>(game_box_index)].slots;
        if (slot >= static_cast<int>(slots.size())) {
            return sb.empty_slot_label;
        }
        const auto& pc = slots[static_cast<std::size_t>(slot)];
        if (itemToolActive()) {
            return pc.occupied() && pc.held_item_id > 0
                ? (!pc.held_item_name.empty() ? pc.held_item_name : ("Item " + std::to_string(pc.held_item_id)))
                : std::string{};
        }
        if (!pc.occupied()) {
            return sb.empty_slot_label;
        }
        return formatPokemonSpeechLine(sb, pc);
    }
    // Resort column (1000–1029): mirror game slot semantics for species/items/box-space titles.
    if (resort_box_browser_.gameBoxSpaceMode()) {
        const int box_index = resort_box_browser_.gameBoxSpaceRowOffset() * 6 + slot;
        if (box_index < 0 || box_index >= static_cast<int>(resort_pc_boxes_.size())) {
            return "";
        }
        const std::string& name = resort_pc_boxes_[static_cast<std::size_t>(box_index)].name;
        return name.empty() ? std::string("BOX") : name;
    }
    const int resort_box_index = resort_box_browser_.gameBoxIndex();
    if (resort_box_index < 0 || resort_box_index >= static_cast<int>(resort_pc_boxes_.size())) {
        return sb.empty_slot_label;
    }
    const auto& rslots = resort_pc_boxes_[static_cast<std::size_t>(resort_box_index)].slots;
    if (slot >= static_cast<int>(rslots.size())) {
        return sb.empty_slot_label;
    }
    const auto& rpc = rslots[static_cast<std::size_t>(slot)];
    if (itemToolActive()) {
        return rpc.occupied() && rpc.held_item_id > 0
            ? (!rpc.held_item_name.empty() ? rpc.held_item_name : ("Item " + std::to_string(rpc.held_item_id)))
            : std::string{};
    }
    if (!rpc.occupied()) {
        return sb.empty_slot_label;
    }
    return formatPokemonSpeechLine(sb, rpc);
}

void TransferSystemScreen::drawSpeechBubbleCursor(SDL_Renderer* renderer, const SDL_Rect& target, FocusNodeId focus_id) const {
    const GameTransferSpeechBubbleCursorStyle& sb = selection_cursor_style_.speech_bubble;
    if (!sb.enabled) {
        return;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    Color border = carouselFrameColorForIndex(ui_state_.selectedToolIndex());
    border.a = 255;

    const std::string line = speechBubbleLineForFocus(focus_id);
    if (line.empty()) {
        return;
    }
    const int bubble_box_key = [&]() -> int {
        if (focus_id >= 1000 && focus_id <= 1029) {
            const int slot = focus_id - 1000;
            return resort_box_browser_.gameBoxSpaceMode()
                ? resort_box_browser_.gameBoxSpaceRowOffset() * 6 + slot
                : resort_box_browser_.gameBoxIndex();
        }
        if (focus_id >= 2000 && focus_id <= 2029) {
            const int slot = focus_id - 2000;
            return game_box_browser_.gameBoxSpaceMode()
                ? game_box_browser_.gameBoxSpaceRowOffset() * 6 + slot
                : game_box_browser_.gameBoxIndex();
        }
        return 0;
    }();
    const std::string cache_key =
        std::to_string(focus_id) + "|" + line + "|" + std::to_string(sb.font_pt) + "|" +
        std::to_string(bubble_box_key);
    if (cache_key != speech_bubble_label_cache_ || !speech_bubble_label_tex_.texture) {
        speech_bubble_label_cache_ = cache_key;
        speech_bubble_label_tex_ = {};
        if (!line.empty() && speech_bubble_font_.get()) {
            speech_bubble_label_tex_ = renderTextTexture(renderer, speech_bubble_font_.get(), line, sb.text_color);
        }
    }

    int tw = speech_bubble_label_tex_.width;
    int th = speech_bubble_label_tex_.height;

    const int stroke = std::max(1, sb.border_thickness);
    const int gap = std::max(0, sb.gap_above_target);
    const int margin = std::max(0, sb.screen_margin);
    const int sw = window_config_.virtual_width;

    const int tip_x = target.x + target.w / 2;
    const int tip_y = target.y - gap;

    const int min_w_use = line.empty() ? sb.empty_min_width : sb.min_width;
    const int min_h_use = line.empty() ? sb.empty_min_height : sb.min_height;

    int tri_h = std::max(6, sb.triangle_height);
    int pill_h = std::max(min_h_use, th + sb.padding_y * 2);
    int pill_w = std::max(min_w_use, tw + sb.padding_x * 2);
    pill_w = std::min(pill_w, sb.max_width);

    int pill_bottom_y = tip_y - tri_h;
    int pill_top = pill_bottom_y - pill_h;
    if (pill_top < margin) {
        pill_top = margin;
        pill_bottom_y = pill_top + pill_h;
        tri_h = std::max(6, tip_y - pill_bottom_y);
    }

    int pill_left = tip_x - pill_w / 2;
    pill_left = std::clamp(pill_left, margin, std::max(margin, sw - margin - pill_w));

    const int rad = std::clamp(sb.corner_radius, 1, std::max(1, pill_h / 2));

    const int flat_l = pill_left + rad;
    const int flat_r = pill_left + pill_w - rad;
    int half_base = std::max(4, sb.triangle_base_width / 2);
    const int max_half = std::max(0, (flat_r - flat_l) / 2);
    half_base = std::min(half_base, max_half);
    const int base_cx = std::clamp(tip_x, flat_l + half_base, flat_r - half_base);
    const int bx0 = base_cx - half_base;
    const int bx1 = base_cx + half_base;

    fillRoundedRingScanlines(renderer, pill_left, pill_top, pill_w, pill_h, rad, stroke, border, sb.fill_color);

    /// Pull the triangle base up into the pill so white covers the bottom border band — reads as one outline with the capsule.
    const int tri_base_y = pill_bottom_y - stroke;

    fillTriangleScanlines(renderer, bx0, tri_base_y, bx1, tri_base_y, tip_x, tip_y, sb.fill_color);
    /// Border only on the two outer legs (no stroke along the base — meets the pill cleanly).
    fillThickSegmentScanlines(renderer, bx0, tri_base_y, tip_x, tip_y, stroke, border);
    fillThickSegmentScanlines(renderer, bx1, tri_base_y, tip_x, tip_y, stroke, border);

    if (speech_bubble_label_tex_.texture && tw > 0 && th > 0) {
        const int tcx = pill_left + (pill_w - tw) / 2;
        const int tcy = pill_top + (pill_h - th) / 2;
        SDL_Rect dst{tcx, tcy, tw, th};
        SDL_RenderCopy(renderer, speech_bubble_label_tex_.texture.get(), nullptr, &dst);
    }
}

void TransferSystemScreen::drawSelectionCursor(SDL_Renderer* renderer) const {
    if (!selection_cursor_style_.enabled) {
        return;
    }
    if (box_rename_modal_open_) {
        return;
    }
    if (game_box_browser_.dropdownOpenTarget() && game_box_browser_.dropdownExpandT() > 0.04) {
        return;
    }
    const std::optional<SDL_Rect> rb = focus_.currentBounds();
    if (!rb) return;
    const SDL_Rect r = *rb;
    const FocusNodeId focus_id = focus_.current();
    const bool wants_bubble = currentFocusWantsSpeechBubble();
    if (selection_cursor_hidden_after_mouse_) {
        if (wants_bubble) {
            drawSpeechBubbleCursor(renderer, r, focus_id);
        }
        return;
    }

    if (selection_cursor_hidden_after_mouse_) {
        return;
    }

    const double pulse =
        (std::sin(ui_state_.elapsedSeconds() * selection_cursor_style_.beat_speed * 3.14159265358979323846 * 2.0) + 1.0) *
        0.5;
    const int pad = selection_cursor_style_.padding + static_cast<int>(std::lround(selection_cursor_style_.beat_magnitude * pulse));
    const int inner_x = r.x - pad;
    const int inner_y = r.y - pad;
    int inner_w = r.w + pad * 2;
    int inner_h = r.h + pad * 2;
    const int min_w = std::max(0, selection_cursor_style_.min_width);
    const int min_h = std::max(0, selection_cursor_style_.min_height);
    int draw_x = inner_x;
    int draw_y = inner_y;
    if (inner_w < min_w || inner_h < min_h) {
        const int cx = inner_x + inner_w / 2;
        const int cy = inner_y + inner_h / 2;
        inner_w = std::max(inner_w, min_w);
        inner_h = std::max(inner_h, min_h);
        draw_x = cx - inner_w / 2;
        draw_y = cy - inner_h / 2;
    }
    const int corner =
        std::clamp(selection_cursor_style_.corner_radius + pad, 0, std::max(0, std::min(inner_w, inner_h) / 2));
    Color c = selection_cursor_style_.color;
    c.a = std::clamp(selection_cursor_style_.alpha, 0, 255);
    drawRoundedOutlineScanlines(renderer, draw_x, draw_y, inner_w, inner_h, corner, c, selection_cursor_style_.thickness);

    // Always draw bubble last so it sits above the legacy outline.
    if (wants_bubble) {
        drawSpeechBubbleCursor(renderer, r, focus_id);
    }
}

bool TransferSystemScreen::currentFocusWantsSpeechBubble() const {
    if (!selection_cursor_style_.speech_bubble.enabled) {
        return false;
    }
    if (pokemon_move_.active() || multi_pokemon_move_.active() || pokemon_action_menu_.visible() ||
        game_box_browser_.dropdownOpenTarget() || resort_box_browser_.dropdownOpenTarget()) {
        return false;
    }

    const FocusNodeId focus_id = focus_.current();
    if (!focusIdUsesSpeechBubble(focus_id)) {
        return false;
    }

    const bool is_icon = (focus_id == 1111 || focus_id == 2111);
    const bool is_exit_button = (focus_id == 5000);
    const bool is_slot =
        (focus_id >= 1000 && focus_id <= 1029) ||
        (focus_id >= 2000 && focus_id <= 2029);

    bool slot_has_payload = false;
    if (is_slot) {
        const int idx = (focus_id >= 2000) ? (focus_id - 2000) : (focus_id - 1000);
        if (itemToolActive()) {
            slot_has_payload =
                (focus_id >= 2000 && !game_box_browser_.gameBoxSpaceMode() && gameSlotHasHeldItem(idx)) ||
                (focus_id >= 1000 && !resort_box_browser_.gameBoxSpaceMode() && resortSlotHasHeldItem(idx));
        } else if (focus_id >= 1000 && resort_box_browser_.gameBoxSpaceMode()) {
            const int box_index = resort_box_browser_.gameBoxSpaceRowOffset() * 6 + idx;
            slot_has_payload = (box_index >= 0 && box_index < static_cast<int>(resort_pc_boxes_.size()));
        } else if (focus_id >= 2000 && game_box_browser_.gameBoxSpaceMode()) {
            const int box_index = game_box_browser_.gameBoxSpaceRowOffset() * 6 + idx;
            slot_has_payload = (box_index >= 0 && box_index < static_cast<int>(game_pc_boxes_.size()));
        } else {
            slot_has_payload = (focus_id >= 2000) ? gameSaveSlotHasSpecies(idx) : resortSlotHasSpecies(idx);
        }
    }

    if (selection_cursor_hidden_after_mouse_) {
        return speech_hover_active_ && (is_exit_button || is_icon || slot_has_payload);
    }

    return is_exit_button || is_icon || slot_has_payload;
}

void TransferSystemScreen::onNavigate2d(int dx, int dy) {
    selection_cursor_hidden_after_mouse_ = false;
    if (box_rename_modal_open_ && !box_rename_editing_) {
        using S = BoxRenameFocusSlot;
        if (dx != 0 || dy != 0) {
            switch (box_rename_focus_slot_) {
                case S::Field:
                    if (dy > 0) {
                        box_rename_focus_slot_ = S::Cancel;
                        ui_state_.requestUiMoveSfx();
                    }
                    break;
                case S::Cancel:
                    if (dy < 0) {
                        box_rename_focus_slot_ = S::Field;
                        ui_state_.requestUiMoveSfx();
                    } else if (dx > 0) {
                        box_rename_focus_slot_ = S::Confirm;
                        ui_state_.requestUiMoveSfx();
                    }
                    break;
                case S::Confirm:
                    if (dy < 0) {
                        box_rename_focus_slot_ = S::Field;
                        ui_state_.requestUiMoveSfx();
                    } else if (dx < 0) {
                        box_rename_focus_slot_ = S::Cancel;
                        ui_state_.requestUiMoveSfx();
                    }
                    break;
            }
        }
        return;
    }
    if (keyboard_multi_marquee_active_ &&
        ((keyboard_multi_marquee_from_game_ && game_box_browser_.gameBoxSpaceMode()) ||
         (!keyboard_multi_marquee_from_game_ && resort_box_browser_.gameBoxSpaceMode()))) {
        keyboard_multi_marquee_active_ = false;
    }
    if (keyboard_multi_marquee_active_) {
        int row = keyboard_multi_marquee_corner_slot_ / 6;
        int col = keyboard_multi_marquee_corner_slot_ % 6;
        col += dx;
        row += dy;
        row = std::clamp(row, 0, 4);
        col = std::clamp(col, 0, 5);
        keyboard_multi_marquee_corner_slot_ = row * 6 + col;
        focus_.setCurrent((keyboard_multi_marquee_from_game_ ? 2000 : 1000) + keyboard_multi_marquee_corner_slot_);
        ui_state_.requestUiMoveSfx();
        return;
    }
    if (pokemon_action_menu_.visible()) {
        if (dy != 0) {
            pokemon_action_menu_.stepSelection(dy > 0 ? 1 : -1);
            ui_state_.requestUiMoveSfx();
        }
        (void)dx;
        return;
    }
    if (item_action_menu_.visible()) {
        if (dy != 0) {
            item_action_menu_.stepSelection(dy > 0 ? 1 : -1);
            ui_state_.requestUiMoveSfx();
        }
        (void)dx;
        return;
    }
    if (dropdownAcceptsNavigation()) {
        if (dy < 0) {
            stepDropdownHighlight(-1);
        } else if (dy > 0) {
            stepDropdownHighlight(1);
        }
        (void)dx;
        return;
    }

    // Box Space mode: allow vertical navigation to scroll through all rows before leaving the grid.
    if (game_box_browser_.gameBoxSpaceMode() && dy != 0) {
        const FocusNodeId cur = focus_.current();
        if (cur >= 2000 && cur <= 2029) {
            const int slot = cur - 2000;
            const int max_row =
                game_box_browser_.gameBoxSpaceMaxRowOffset(static_cast<int>(game_pc_boxes_.size()));
            if (dy > 0) {
                // At bottom row of the 6×5 grid.
                if (slot >= 24) {
                    if (game_box_browser_.gameBoxSpaceRowOffset() < max_row) {
                        stepGameBoxSpaceRowDown();
                        return;
                    }
                    // At end: only now wrap to the footer Box Space button.
                    focus_.setCurrent(2110);
                    ui_state_.requestUiMoveSfx();
                    return;
                }
            } else if (dy < 0) {
                // At top row of the grid.
                if (slot < 6 && game_box_browser_.gameBoxSpaceRowOffset() > 0) {
                    stepGameBoxSpaceRowUp();
                    return;
                }
            }
        }
    }
    if (resort_box_browser_.gameBoxSpaceMode() && dy != 0) {
        const FocusNodeId cur = focus_.current();
        if (cur >= 1000 && cur <= 1029) {
            const int slot = cur - 1000;
            const int max_row =
                resort_box_browser_.gameBoxSpaceMaxRowOffset(static_cast<int>(resort_pc_boxes_.size()));
            if (dy > 0) {
                if (slot >= 24) {
                    if (resort_box_browser_.gameBoxSpaceRowOffset() < max_row) {
                        stepResortBoxSpaceRowDown();
                        return;
                    }
                    focus_.setCurrent(1110);
                    ui_state_.requestUiMoveSfx();
                    return;
                }
            } else if (dy < 0) {
                if (slot < 6 && resort_box_browser_.gameBoxSpaceRowOffset() > 0) {
                    stepResortBoxSpaceRowUp();
                    return;
                }
            }
        }
    }

    const FocusNodeId focus_before = focus_.current();
    focus_.navigate(dx, dy, window_config_.virtual_width, window_config_.virtual_height);
    if (focus_.current() != focus_before) {
        ui_state_.requestUiMoveSfx();
    }
}

void TransferSystemScreen::updateAnimations(double dt) {
    ui_state_.update(dt, pill_style_, carousel_style_);
}

void TransferSystemScreen::updateEnterExit(double dt) {
    (void)dt;
}

void TransferSystemScreen::updateCarouselSlide(double dt) {
    (void)dt;
}

void TransferSystemScreen::syncBoxViewportPositions() {
    const int screen_w = window_config_.virtual_width;
    const int resort_hidden_x = -BoxViewport::kViewportWidth;
    const int game_hidden_x = screen_w;
    const int resort_rest_x = kLeftBoxColumnX;
    const int game_rest_x = screen_w - 40 - BoxViewport::kViewportWidth;

    const int resort_x =
        static_cast<int>(std::round(resort_hidden_x + (resort_rest_x - resort_hidden_x) * ui_state_.panelsReveal()));
    const int game_x =
        static_cast<int>(std::round(game_hidden_x + (game_rest_x - game_hidden_x) * ui_state_.panelsReveal()));

    if (resort_box_viewport_) {
        resort_box_viewport_->setViewportOrigin(resort_x, kBoxViewportY);
    }
    if (game_save_box_viewport_) {
        game_save_box_viewport_->setViewportOrigin(game_x, kBoxViewportY);
    }
}

BoxViewportModel TransferSystemScreen::gameBoxViewportModelAt(int box_index) const {
    BoxViewportModel incoming;
    if (box_index < 0 || box_index >= static_cast<int>(game_pc_boxes_.size())) {
        return incoming;
    }
    incoming.box_name = game_pc_boxes_[static_cast<std::size_t>(box_index)].name;
    const auto& slots = game_pc_boxes_[static_cast<std::size_t>(box_index)].slots;
    for (std::size_t i = 0; i < incoming.slot_sprites.size() && i < slots.size(); ++i) {
        const auto& pc = slots[i];
        if (!pc.occupied() || !sprite_assets_ || !renderer_) {
            incoming.slot_sprites[i] = std::nullopt;
            continue;
        }
        TextureHandle texture = sprite_assets_->loadPokemonTexture(renderer_, pc);
        incoming.slot_sprites[i] = texture.texture ? std::optional<TextureHandle>(std::move(texture)) : std::nullopt;
        if (pc.held_item_id > 0) {
            TextureHandle item = sprite_assets_->loadItemTexture(renderer_, pc.held_item_id);
            incoming.held_item_sprites[i] =
                item.texture ? std::optional<TextureHandle>(std::move(item)) : std::nullopt;
        }
    }
    return incoming;
}

bool TransferSystemScreen::panelsReadyForInteraction() const {
    return ui_state_.panelsReveal() > 0.85 && ui_state_.uiEnter() > 0.85;
}

bool TransferSystemScreen::dropdownAcceptsNavigation() const {
    return gameDropdownNavigationActive() || resortDropdownNavigationActive();
}

std::optional<int> TransferSystemScreen::focusedGameSlotIndex() const {
    const FocusNodeId current = focus_.current();
    if (current < 2000 || current > 2029) {
        return std::nullopt;
    }
    return current - 2000;
}

std::optional<int> TransferSystemScreen::focusedBoxSpaceBoxIndex() const {
    if (!game_box_browser_.gameBoxSpaceMode()) {
        return std::nullopt;
    }
    const FocusNodeId current = focus_.current();
    if (current < 2000 || current > 2029) {
        return std::nullopt;
    }
    const int cell = current - 2000;
    const int box_index = game_box_browser_.gameBoxSpaceRowOffset() * 6 + cell;
    if (box_index < 0 || box_index >= static_cast<int>(game_pc_boxes_.size())) {
        return std::nullopt;
    }
    return box_index;
}

std::optional<int> TransferSystemScreen::focusedResortBoxSpaceBoxIndex() const {
    if (!resort_box_browser_.gameBoxSpaceMode()) {
        return std::nullopt;
    }
    const FocusNodeId current = focus_.current();
    if (current < 1000 || current > 1029) {
        return std::nullopt;
    }
    const int cell = current - 1000;
    const int box_index = resort_box_browser_.gameBoxSpaceRowOffset() * 6 + cell;
    if (box_index < 0 || box_index >= static_cast<int>(resort_pc_boxes_.size())) {
        return std::nullopt;
    }
    return box_index;
}

std::optional<int> TransferSystemScreen::focusedResortSlotIndex() const {
    const FocusNodeId current = focus_.current();
    if (current < 1000 || current > 1029) {
        return std::nullopt;
    }
    return current - 1000;
}

bool TransferSystemScreen::openResortBoxFromBoxSpaceSelection(int box_index) {
    if (box_index < 0 || box_index >= static_cast<int>(resort_pc_boxes_.size())) {
        return false;
    }
    mini_preview_target_ = 0.0;
    mini_preview_t_ = 0.0;
    mini_preview_box_index_ = -1;
    mouse_hover_mini_preview_box_index_ = -1;
    setResortBoxSpaceMode(false);
    resort_box_browser_.jumpGameBoxToIndex(
        box_index, static_cast<int>(resort_pc_boxes_.size()), panelsReadyForInteraction());
    if (resort_box_viewport_) {
        resort_box_viewport_->snapContentToModel(resortBoxViewportModelAt(box_index));
    }
    ui_state_.requestButtonSfx();
    return true;
}

bool TransferSystemScreen::activateFocusedResortSlot() {
    const std::optional<int> slot = focusedResortSlotIndex();
    if (!slot.has_value()) {
        return false;
    }
    if (!panelsReadyForInteraction() || !resort_box_viewport_) {
        return false;
    }
    if (resort_box_browser_.gameBoxSpaceMode()) {
        const int box_index = resort_box_browser_.gameBoxSpaceRowOffset() * 6 + *slot;
        return openResortBoxFromBoxSpaceSelection(box_index);
    }
    return false;
}

bool TransferSystemScreen::swapGamePcBoxes(int a, int b) {
    if (a < 0 || b < 0 || a >= static_cast<int>(game_pc_boxes_.size()) || b >= static_cast<int>(game_pc_boxes_.size())) {
        return false;
    }
    if (a == b) {
        return true;
    }
    std::swap(game_pc_boxes_[static_cast<std::size_t>(a)], game_pc_boxes_[static_cast<std::size_t>(b)]);
    if (game_save_box_viewport_ && game_box_browser_.gameBoxSpaceMode()) {
        const bool show_down = gameBoxSpaceMaxRowOffset() > 0;
        game_save_box_viewport_->setHeaderMode(BoxViewport::HeaderMode::BoxSpace, show_down);
        game_save_box_viewport_->snapContentToModel(gameBoxSpaceViewportModelAt(game_box_browser_.gameBoxSpaceRowOffset()));
    }
    mini_preview_box_index_ = -1;
    mouse_hover_mini_preview_box_index_ = -1;
    ui_state_.requestButtonSfx();
    return true;
}

bool TransferSystemScreen::swapGameAndResortPcBoxes(int game_box_index, int resort_box_index) {
    if (game_box_index < 0 || resort_box_index < 0 ||
        game_box_index >= static_cast<int>(game_pc_boxes_.size()) ||
        resort_box_index >= static_cast<int>(resort_pc_boxes_.size())) {
        return false;
    }
    std::swap(game_pc_boxes_[static_cast<std::size_t>(game_box_index)],
              resort_pc_boxes_[static_cast<std::size_t>(resort_box_index)]);
    mini_preview_box_index_ = -1;
    mouse_hover_mini_preview_box_index_ = -1;
    mini_preview_model_from_resort_ = false;
    if (game_save_box_viewport_) {
        if (game_box_browser_.gameBoxSpaceMode()) {
            game_save_box_viewport_->snapContentToModel(
                gameBoxSpaceViewportModelAt(game_box_browser_.gameBoxSpaceRowOffset()));
        } else {
            game_save_box_viewport_->snapContentToModel(gameBoxViewportModelAt(game_box_browser_.gameBoxIndex()));
        }
    }
    if (resort_box_viewport_) {
        if (resort_box_browser_.gameBoxSpaceMode()) {
            resort_box_viewport_->snapContentToModel(
                resortBoxSpaceViewportModelAt(resort_box_browser_.gameBoxSpaceRowOffset()));
        } else {
            resort_box_viewport_->snapContentToModel(resortBoxViewportModelAt(resort_box_browser_.gameBoxIndex()));
        }
    }
    refreshGameBoxViewportModel();
    refreshResortBoxViewportModel();
    ui_state_.requestButtonSfx();
    return true;
}

bool TransferSystemScreen::dropHeldPokemonIntoFirstEmptySlotInBox(int box_index) {
    using Move = transfer_system::PokemonMoveController;
    if (!pokemon_move_.active()) {
        return false;
    }
    if (box_index < 0 || box_index >= static_cast<int>(game_pc_boxes_.size())) {
        return false;
    }
    auto& slots = game_pc_boxes_[static_cast<std::size_t>(box_index)].slots;
    for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
        if (!slots[static_cast<std::size_t>(i)].occupied()) {
            return dropHeldPokemonAt(Move::SlotRef{Move::Panel::Game, box_index, i});
        }
    }
    return false;
}

bool TransferSystemScreen::dropHeldPokemonIntoFirstEmptySlotInResortBox(int box_index) {
    using Move = transfer_system::PokemonMoveController;
    if (!pokemon_move_.active()) {
        return false;
    }
    if (box_index < 0 || box_index >= static_cast<int>(resort_pc_boxes_.size())) {
        return false;
    }
    auto& slots = resort_pc_boxes_[static_cast<std::size_t>(box_index)].slots;
    for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
        if (!slots[static_cast<std::size_t>(i)].occupied()) {
            return dropHeldPokemonAt(Move::SlotRef{Move::Panel::Resort, box_index, i});
        }
    }
    return false;
}

bool TransferSystemScreen::gameBoxHasEmptySlot(int box_index) const {
    if (box_index < 0 || box_index >= static_cast<int>(game_pc_boxes_.size())) {
        return false;
    }
    const auto& slots = game_pc_boxes_[static_cast<std::size_t>(box_index)].slots;
    return std::any_of(slots.begin(), slots.end(), [](const PcSlotSpecies& s) { return !s.occupied(); });
}

bool TransferSystemScreen::gameBoxHasPreviewContent(int box_index) const {
    if (box_index < 0 || box_index >= static_cast<int>(game_pc_boxes_.size())) {
        return false;
    }
    const auto& slots = game_pc_boxes_[static_cast<std::size_t>(box_index)].slots;
    return std::any_of(slots.begin(), slots.end(), [](const PcSlotSpecies& slot) {
        return slot.occupied();
    });
}

bool TransferSystemScreen::resortBoxHasPreviewContent(int box_index) const {
    if (box_index < 0 || box_index >= static_cast<int>(resort_pc_boxes_.size())) {
        return false;
    }
    const auto& slots = resort_pc_boxes_[static_cast<std::size_t>(box_index)].slots;
    return std::any_of(slots.begin(), slots.end(), [](const PcSlotSpecies& slot) {
        return slot.occupied();
    });
}

bool TransferSystemScreen::shouldShowMiniPreviewForBox(int box_index, MiniPreviewContext context) const {
    if (box_index < 0 || box_index >= static_cast<int>(game_pc_boxes_.size())) {
        return false;
    }

    switch (context) {
        case MiniPreviewContext::Dropdown:
            return true;
        case MiniPreviewContext::MouseHover:
        case MiniPreviewContext::BoxSpaceFocus:
            return gameBoxHasPreviewContent(box_index);
    }

    return false;
}

bool TransferSystemScreen::shouldShowMiniPreviewForResortBox(int box_index, MiniPreviewContext context) const {
    if (box_index < 0 || box_index >= static_cast<int>(resort_pc_boxes_.size())) {
        return false;
    }

    switch (context) {
        case MiniPreviewContext::Dropdown:
            return true;
        case MiniPreviewContext::MouseHover:
        case MiniPreviewContext::BoxSpaceFocus:
            return resortBoxHasPreviewContent(box_index);
    }

    return false;
}

bool TransferSystemScreen::openGameBoxFromBoxSpaceSelection(int box_index) {
    if (box_index < 0 || box_index >= static_cast<int>(game_pc_boxes_.size())) {
        return false;
    }
    mini_preview_target_ = 0.0;
    mini_preview_t_ = 0.0;
    mini_preview_box_index_ = -1;
    mouse_hover_mini_preview_box_index_ = -1;
    setGameBoxSpaceMode(false);
    // Box Space should feel like a direct jump, not the per-box slide animation.
    game_box_browser_.jumpGameBoxToIndex(box_index, static_cast<int>(game_pc_boxes_.size()), panelsReadyForInteraction());
    if (game_save_box_viewport_) {
        game_save_box_viewport_->snapContentToModel(gameBoxViewportModelAt(box_index));
    }
    ui_state_.requestButtonSfx();
    return true;
}

bool TransferSystemScreen::activateFocusedGameSlot() {
    const std::optional<int> slot = focusedGameSlotIndex();
    if (!slot.has_value()) {
        return false;
    }
    if (!panelsReadyForInteraction() || !game_save_box_viewport_) {
        return false;
    }
    if (game_box_browser_.gameBoxSpaceMode()) {
        const int box_index = game_box_browser_.gameBoxSpaceRowOffset() * 6 + *slot;
        return openGameBoxFromBoxSpaceSelection(box_index);
    }
    return false;
}

int TransferSystemScreen::gameBoxSpaceMaxRowOffset() const {
    return game_box_browser_.gameBoxSpaceMaxRowOffset(static_cast<int>(game_pc_boxes_.size()));
}

BoxViewportModel TransferSystemScreen::gameBoxSpaceViewportModelAt(int row_offset) const {
    BoxViewportModel m;
    m.box_name = "BOX SPACE";

    const int base_box_index = std::max(0, row_offset) * 6;
    const int hide_box_index =
        (held_move_.heldBox() && !pokemon_move_.active() &&
         held_move_.heldBox()->source_panel == transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Game)
        ? held_move_.heldBox()->source_box_index
        : -1;
    for (int i = 0; i < 30; ++i) {
        const int box_index = base_box_index + i;
        if (box_index < 0 || box_index >= static_cast<int>(game_pc_boxes_.size())) {
            m.slot_sprites[static_cast<std::size_t>(i)] = std::nullopt;
            continue;
        }
        if (hide_box_index >= 0 && box_index == hide_box_index) {
            m.slot_sprites[static_cast<std::size_t>(i)] = std::nullopt;
            continue;
        }

        const auto& box = game_pc_boxes_[static_cast<std::size_t>(box_index)];
        int occupied = 0;
        int total = 0;
        for (const auto& slot : box.slots) {
            ++total;
            if (slot.occupied()) {
                ++occupied;
            }
        }

        if (total <= 0) {
            m.slot_sprites[static_cast<std::size_t>(i)] = std::nullopt;
            continue;
        }

        if (occupied == 0) {
            m.slot_sprites[static_cast<std::size_t>(i)] =
                box_space_empty_tex_.texture ? std::optional<TextureHandle>(box_space_empty_tex_) : std::nullopt;
        } else if (occupied >= total) {
            m.slot_sprites[static_cast<std::size_t>(i)] =
                box_space_full_tex_.texture ? std::optional<TextureHandle>(box_space_full_tex_) : std::nullopt;
        } else {
            m.slot_sprites[static_cast<std::size_t>(i)] =
                box_space_noempty_tex_.texture ? std::optional<TextureHandle>(box_space_noempty_tex_) : std::nullopt;
        }
    }
    for (int i = 0; i < 30; ++i) {
        const bool use_wiggle = box_space_interaction_panel_ == BoxSpaceInteractionPanel::Game;
        m.slot_wiggle_dx[static_cast<std::size_t>(i)] =
            use_wiggle ? box_space_slot_wiggle_dx_[static_cast<std::size_t>(i)] : 0;
    }
    return m;
}

BoxViewportModel TransferSystemScreen::resortBoxViewportModelAt(int box_index) const {
    BoxViewportModel incoming;
    if (box_index < 0 || box_index >= static_cast<int>(resort_pc_boxes_.size())) {
        return incoming;
    }
    incoming.box_name = resort_pc_boxes_[static_cast<std::size_t>(box_index)].name;
    const auto& slots = resort_pc_boxes_[static_cast<std::size_t>(box_index)].slots;
    for (std::size_t i = 0; i < incoming.slot_sprites.size() && i < slots.size(); ++i) {
        const auto& pc = slots[i];
        if (!pc.occupied() || !sprite_assets_ || !renderer_) {
            incoming.slot_sprites[i] = std::nullopt;
            continue;
        }
        TextureHandle texture = sprite_assets_->loadPokemonTexture(renderer_, pc);
        incoming.slot_sprites[i] = texture.texture ? std::optional<TextureHandle>(std::move(texture)) : std::nullopt;
        if (pc.held_item_id > 0) {
            TextureHandle item = sprite_assets_->loadItemTexture(renderer_, pc.held_item_id);
            incoming.held_item_sprites[i] =
                item.texture ? std::optional<TextureHandle>(std::move(item)) : std::nullopt;
        }
    }
    return incoming;
}

BoxViewportModel TransferSystemScreen::resortBoxSpaceViewportModelAt(int row_offset) const {
    BoxViewportModel m;
    m.box_name = "BOX SPACE";

    const int base_box_index = std::max(0, row_offset) * 6;
    const int hide_box_index =
        (held_move_.heldBox() && !pokemon_move_.active() &&
         held_move_.heldBox()->source_panel == transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Resort)
        ? held_move_.heldBox()->source_box_index
        : -1;
    for (int i = 0; i < 30; ++i) {
        const int box_index = base_box_index + i;
        if (box_index < 0 || box_index >= static_cast<int>(resort_pc_boxes_.size())) {
            m.slot_sprites[static_cast<std::size_t>(i)] = std::nullopt;
            continue;
        }
        if (hide_box_index >= 0 && box_index == hide_box_index) {
            m.slot_sprites[static_cast<std::size_t>(i)] = std::nullopt;
            continue;
        }

        const auto& box = resort_pc_boxes_[static_cast<std::size_t>(box_index)];
        int occupied = 0;
        int total = 0;
        for (const auto& slot : box.slots) {
            ++total;
            if (slot.occupied()) {
                ++occupied;
            }
        }

        if (total <= 0) {
            m.slot_sprites[static_cast<std::size_t>(i)] = std::nullopt;
            continue;
        }

        if (occupied == 0) {
            m.slot_sprites[static_cast<std::size_t>(i)] =
                box_space_empty_tex_.texture ? std::optional<TextureHandle>(box_space_empty_tex_) : std::nullopt;
        } else if (occupied >= total) {
            m.slot_sprites[static_cast<std::size_t>(i)] =
                box_space_full_tex_.texture ? std::optional<TextureHandle>(box_space_full_tex_) : std::nullopt;
        } else {
            m.slot_sprites[static_cast<std::size_t>(i)] =
                box_space_noempty_tex_.texture ? std::optional<TextureHandle>(box_space_noempty_tex_) : std::nullopt;
        }
    }
    for (int i = 0; i < 30; ++i) {
        const bool use_wiggle = box_space_interaction_panel_ == BoxSpaceInteractionPanel::Resort;
        m.slot_wiggle_dx[static_cast<std::size_t>(i)] =
            use_wiggle ? box_space_slot_wiggle_dx_[static_cast<std::size_t>(i)] : 0;
    }
    return m;
}

int TransferSystemScreen::resortBoxSpaceMaxRowOffset() const {
    return resort_box_browser_.gameBoxSpaceMaxRowOffset(static_cast<int>(resort_pc_boxes_.size()));
}

void TransferSystemScreen::setResortBoxSpaceMode(bool enabled) {
    if (!resort_box_viewport_) {
        resort_box_browser_.setGameBoxSpaceMode(false, static_cast<int>(resort_pc_boxes_.size()));
        return;
    }

    resort_box_browser_.setGameBoxSpaceMode(enabled, static_cast<int>(resort_pc_boxes_.size()));

    if (resort_box_browser_.gameBoxSpaceMode()) {
        const bool show_down = resortBoxSpaceMaxRowOffset() > 0;
        resort_box_viewport_->setHeaderMode(BoxViewport::HeaderMode::BoxSpace, show_down);
        resort_box_viewport_->setBoxSpaceActive(true);
        resort_box_viewport_->snapContentToModel(
            resortBoxSpaceViewportModelAt(resort_box_browser_.gameBoxSpaceRowOffset()));
    } else {
        resort_box_viewport_->setHeaderMode(BoxViewport::HeaderMode::Normal, false);
        resort_box_viewport_->setBoxSpaceActive(false);
        resort_box_viewport_->snapContentToModel(resortBoxViewportModelAt(resort_box_browser_.gameBoxIndex()));
    }

    mini_preview_box_index_ = -1;
    mouse_hover_mini_preview_box_index_ = -1;
    mini_preview_model_from_resort_ = false;
    box_space_drag_active_ = false;
    box_space_drag_last_y_ = 0;
    box_space_drag_accum_ = 0.0;
    box_space_pressed_cell_ = -1;
    box_space_quick_drop_pending_ = false;
    box_space_quick_drop_kind_ = BoxSpaceQuickDropKind::None;
    box_space_quick_drop_elapsed_seconds_ = 0.0;
    box_space_quick_drop_target_box_index_ = -1;
    keyboard_multi_marquee_active_ = false;
    box_space_interaction_panel_ = BoxSpaceInteractionPanel::None;
    if (!resort_box_browser_.gameBoxSpaceMode()) {
        box_space_box_move_hold_.cancel();
        box_space_box_move_source_box_index_ = -1;
        if (held_move_.heldBox()) {
            held_move_.clear();
        }
    }
    ui_state_.requestButtonSfx();
}

void TransferSystemScreen::stepResortBoxSpaceRowDown() {
    if (!resort_box_viewport_) {
        return;
    }
    const int box_count = static_cast<int>(resort_pc_boxes_.size());
    const int max_row = resort_box_browser_.gameBoxSpaceMaxRowOffset(box_count);
    if (!resort_box_browser_.stepGameBoxSpaceRowDown(box_count)) {
        return;
    }
    const bool show_down = max_row > 0;
    resort_box_viewport_->setHeaderMode(BoxViewport::HeaderMode::BoxSpace, show_down);
    resort_box_viewport_->snapContentToModel(resortBoxSpaceViewportModelAt(resort_box_browser_.gameBoxSpaceRowOffset()));
    ui_state_.requestButtonSfx();
}

void TransferSystemScreen::stepResortBoxSpaceRowUp() {
    if (!resort_box_viewport_) {
        return;
    }
    const int box_count = static_cast<int>(resort_pc_boxes_.size());
    if (!resort_box_browser_.stepGameBoxSpaceRowUp()) {
        return;
    }
    const bool show_down = resortBoxSpaceMaxRowOffset() > 0;
    resort_box_viewport_->setHeaderMode(BoxViewport::HeaderMode::BoxSpace, show_down);
    resort_box_viewport_->snapContentToModel(resortBoxSpaceViewportModelAt(resort_box_browser_.gameBoxSpaceRowOffset()));
    ui_state_.requestButtonSfx();
}

void TransferSystemScreen::advanceResortBox(int dir) {
    if (!resort_box_viewport_ || resort_pc_boxes_.empty() || dir == 0) {
        return;
    }
    const int count = static_cast<int>(resort_pc_boxes_.size());
    if (!resort_box_browser_.advanceGameBox(dir, count, ui_state_.panelsReveal() > 0.85 && ui_state_.uiEnter() > 0.85)) {
        return;
    }
    const int next = resort_box_browser_.gameBoxIndex();
    resort_box_viewport_->queueContentSlide(resortBoxViewportModelAt(next), dir);
    ui_state_.requestButtonSfx();
}

void TransferSystemScreen::jumpResortBoxToIndex(int target_index) {
    if (!resort_box_viewport_ || resort_pc_boxes_.empty()) {
        return;
    }
    const int previous_index = resort_box_browser_.gameBoxIndex();
    const int n = static_cast<int>(resort_pc_boxes_.size());
    const bool changed =
        resort_box_browser_.jumpGameBoxToIndex(target_index, n, ui_state_.panelsReveal() > 0.85 && ui_state_.uiEnter() > 0.85);
    if (!changed) {
        return;
    }
    const int next_index = resort_box_browser_.gameBoxIndex();
    const int slide_dir = (next_index >= previous_index) ? 1 : -1;
    resort_box_viewport_->queueContentSlide(resortBoxViewportModelAt(next_index), slide_dir);
    ui_state_.requestButtonSfx();
}

bool TransferSystemScreen::swapResortPcBoxes(int a, int b) {
    if (a < 0 || b < 0 || a >= static_cast<int>(resort_pc_boxes_.size()) || b >= static_cast<int>(resort_pc_boxes_.size())) {
        return false;
    }
    if (a == b) {
        return true;
    }
    std::swap(resort_pc_boxes_[static_cast<std::size_t>(a)], resort_pc_boxes_[static_cast<std::size_t>(b)]);
    if (resort_service_) {
        resort_service_->swapResortBoxContents(kDefaultResortProfileId, a, b);
    }
    if (resort_box_viewport_ && resort_box_browser_.gameBoxSpaceMode()) {
        const bool show_down = resortBoxSpaceMaxRowOffset() > 0;
        resort_box_viewport_->setHeaderMode(BoxViewport::HeaderMode::BoxSpace, show_down);
        resort_box_viewport_->snapContentToModel(resortBoxSpaceViewportModelAt(resort_box_browser_.gameBoxSpaceRowOffset()));
    }
    mini_preview_box_index_ = -1;
    mouse_hover_mini_preview_box_index_ = -1;
    ui_state_.requestButtonSfx();
    return true;
}

void TransferSystemScreen::setGameBoxSpaceMode(bool enabled) {
    if (!game_save_box_viewport_) {
        game_box_browser_.setGameBoxSpaceMode(false, static_cast<int>(game_pc_boxes_.size()));
        return;
    }

    game_box_browser_.setGameBoxSpaceMode(enabled, static_cast<int>(game_pc_boxes_.size()));

    if (game_box_browser_.gameBoxSpaceMode()) {
        const bool show_down = gameBoxSpaceMaxRowOffset() > 0;
        game_save_box_viewport_->setHeaderMode(BoxViewport::HeaderMode::BoxSpace, show_down);
        game_save_box_viewport_->setBoxSpaceActive(true);
        game_save_box_viewport_->snapContentToModel(
            gameBoxSpaceViewportModelAt(game_box_browser_.gameBoxSpaceRowOffset()));
    } else {
        game_save_box_viewport_->setHeaderMode(BoxViewport::HeaderMode::Normal, false);
        game_save_box_viewport_->setBoxSpaceActive(false);
        game_save_box_viewport_->snapContentToModel(gameBoxViewportModelAt(game_box_browser_.gameBoxIndex()));
    }

    box_space_drag_active_ = false;
    box_space_drag_last_y_ = 0;
    box_space_drag_accum_ = 0.0;
    box_space_pressed_cell_ = -1;
    box_space_quick_drop_pending_ = false;
    box_space_quick_drop_kind_ = BoxSpaceQuickDropKind::None;
    box_space_quick_drop_elapsed_seconds_ = 0.0;
    box_space_quick_drop_target_box_index_ = -1;
    keyboard_multi_marquee_active_ = false;
    box_space_interaction_panel_ = BoxSpaceInteractionPanel::None;
    mini_preview_model_from_resort_ = false;
    if (!game_box_browser_.gameBoxSpaceMode()) {
        box_space_box_move_hold_.cancel();
        box_space_box_move_source_box_index_ = -1;
        if (held_move_.heldBox()) {
            held_move_.clear();
        }
    }
}

void TransferSystemScreen::stepGameBoxSpaceRowDown() {
    if (!game_save_box_viewport_) {
        return;
    }
    const int box_count = static_cast<int>(game_pc_boxes_.size());
    const int max_row = game_box_browser_.gameBoxSpaceMaxRowOffset(box_count);
    if (!game_box_browser_.stepGameBoxSpaceRowDown(box_count)) {
        return;
    }
    const bool show_down = max_row > 0;
    game_save_box_viewport_->setHeaderMode(BoxViewport::HeaderMode::BoxSpace, show_down);
    game_save_box_viewport_->snapContentToModel(gameBoxSpaceViewportModelAt(game_box_browser_.gameBoxSpaceRowOffset()));
    ui_state_.requestButtonSfx();
}

void TransferSystemScreen::stepGameBoxSpaceRowUp() {
    if (!game_save_box_viewport_) {
        return;
    }
    if (!game_box_browser_.stepGameBoxSpaceRowUp()) {
        return;
    }
    const bool show_down = gameBoxSpaceMaxRowOffset() > 0;
    game_save_box_viewport_->setHeaderMode(BoxViewport::HeaderMode::BoxSpace, show_down);
    game_save_box_viewport_->snapContentToModel(gameBoxSpaceViewportModelAt(game_box_browser_.gameBoxSpaceRowOffset()));
    ui_state_.requestButtonSfx();
}

void TransferSystemScreen::advanceGameBox(int dir) {
    if (!game_save_box_viewport_ || game_pc_boxes_.empty() || dir == 0) {
        return;
    }
    const int count = static_cast<int>(game_pc_boxes_.size());
    if (!game_box_browser_.advanceGameBox(dir, count, ui_state_.panelsReveal() > 0.85 && ui_state_.uiEnter() > 0.85)) {
        return;
    }
    const int next = game_box_browser_.gameBoxIndex();

    game_save_box_viewport_->queueContentSlide(gameBoxViewportModelAt(next), dir);
    ui_state_.requestButtonSfx();
}

void TransferSystemScreen::jumpGameBoxToIndex(int target_index) {
    if (!game_save_box_viewport_ || game_pc_boxes_.empty()) {
        return;
    }
    const int previous_index = game_box_browser_.gameBoxIndex();
    const int n = static_cast<int>(game_pc_boxes_.size());
    const bool changed =
        game_box_browser_.jumpGameBoxToIndex(target_index, n, ui_state_.panelsReveal() > 0.85 && ui_state_.uiEnter() > 0.85);
    if (!changed) {
        return;
    }
    const int next_index = game_box_browser_.gameBoxIndex();
    const int slide_dir = (next_index >= previous_index) ? 1 : -1;
    game_save_box_viewport_->queueContentSlide(gameBoxViewportModelAt(next_index), slide_dir);
    ui_state_.requestButtonSfx();
}

void TransferSystemScreen::updateGameBoxDropdown(double dt) {
    game_box_browser_.updateDropdown(dt, box_name_dropdown_style_);
    if (!game_box_browser_.dropdownOpenTarget() && game_box_browser_.dropdownExpandT() < 0.02) {
        dropdown_item_textures_.clear();
    }
}

void TransferSystemScreen::updateResortBoxDropdown(double dt) {
    resort_box_browser_.updateDropdown(dt, box_name_dropdown_style_);
    if (!resort_box_browser_.dropdownOpenTarget() && resort_box_browser_.dropdownExpandT() < 0.02) {
        resort_dropdown_item_textures_.clear();
    }
}

void TransferSystemScreen::rebuildResortDropdownItemTextures(SDL_Renderer* renderer) {
    resort_dropdown_item_textures_.clear();
    if (!dropdown_item_font_.get()) {
        resort_dropdown_labels_dirty_ = false;
        return;
    }
    const Color text_color = box_name_dropdown_style_.item_text_color;
    int max_h = 8;
    const int box_n = static_cast<int>(resort_pc_boxes_.size());
    if (transfer_system::GameBoxBrowserController::dropdownListRowCount(box_n) > box_n) {
        TextureHandle rename_tex =
            renderTextTexture(renderer, dropdown_item_font_.get(), "RENAME BOX...", text_color);
        max_h = std::max(max_h, rename_tex.height);
        resort_dropdown_item_textures_.push_back(std::move(rename_tex));
    }
    for (const auto& box : resort_pc_boxes_) {
        TextureHandle tex = renderTextTexture(renderer, dropdown_item_font_.get(), box.name, text_color);
        max_h = std::max(max_h, tex.height);
        resort_dropdown_item_textures_.push_back(std::move(tex));
    }
    resort_box_browser_.setDropdownRowHeightPx(max_h + std::max(0, box_name_dropdown_style_.row_padding_y) * 2);
    resort_dropdown_labels_dirty_ = false;
}

bool TransferSystemScreen::computeResortBoxDropdownOuterRect(
    SDL_Rect& out_outer,
    float expand_scale,
    int& out_list_inner_h,
    int& out_list_clip_top_y) const {
    if (!resort_box_viewport_) {
        return false;
    }
    SDL_Rect pill{};
    if (!resort_box_viewport_->getNamePlateBounds(pill)) {
        return false;
    }
    const int stroke = std::max(0, box_name_dropdown_style_.panel_border_thickness);
    const int panel_w = std::max(1, box_name_dropdown_style_.panel_width_pixels);
    const int cx = pill.x + pill.w / 2;
    const int panel_left = cx - panel_w / 2;
    const int chrome_top = pill.y + pill.h / 2;
    const int list_top = pill.y + pill.h;
    out_list_clip_top_y = list_top;

    const int screen_h = window_config_.virtual_height;
    const int bottom_margin = std::max(0, box_name_dropdown_style_.bottom_margin_pixels);
    const int ref_h = std::max(1, box_name_dropdown_style_.reference_name_plate_height_pixels);
    const float mult = std::max(0.1f, box_name_dropdown_style_.max_height_multiplier);
    const int max_by_spec = static_cast<int>(std::lround(static_cast<float>(ref_h) * mult));
    const int available_for_list = screen_h - list_top - bottom_margin;
    const int raw_list_cap = std::max(1, std::min(max_by_spec, std::max(1, available_for_list)));
    const int count = std::max(1, static_cast<int>(resort_pc_boxes_.size()));
    const int rh = std::max(1, resort_box_browser_.dropdownRowHeightPx());
    const int rows = transfer_system::GameBoxBrowserController::dropdownListRowCount(count);
    const int content_h = rows * rh;
    const int inner_list_max = std::min(raw_list_cap, std::max(rh, content_h));
    const float es = std::clamp(expand_scale, 0.f, 1.f);
    out_list_inner_h = std::max(1, static_cast<int>(std::lround(static_cast<double>(inner_list_max) * static_cast<double>(es))));

    const int outer_top = chrome_top;
    const int inner_fill_top = outer_top + stroke;
    const int stem_h = std::max(0, list_top - inner_fill_top);
    const int total_inner_fill_h = stem_h + out_list_inner_h;
    const int outer_w = panel_w + stroke * 2;
    const int outer_h = total_inner_fill_h + stroke * 2;
    out_outer = SDL_Rect{panel_left - stroke, outer_top, outer_w, outer_h};
    return true;
}

bool TransferSystemScreen::hitTestResortBoxNamePlate(int logical_x, int logical_y) const {
    if (!resort_box_viewport_) {
        return false;
    }
    SDL_Rect r{};
    return resort_box_viewport_->getNamePlateBounds(r) && logical_x >= r.x && logical_x < r.x + r.w &&
           logical_y >= r.y && logical_y < r.y + r.h;
}

std::optional<int> TransferSystemScreen::resortDropdownRowIndexAtScreen(int logical_x, int logical_y) const {
    if (!resort_box_browser_.dropdownOpenTarget() || resort_pc_boxes_.empty()) {
        return std::nullopt;
    }
    SDL_Rect outer{};
    int list_h = 0;
    int list_clip_y = 0;
    if (!computeResortBoxDropdownOuterRect(outer, static_cast<float>(resort_box_browser_.dropdownExpandT()), list_h, list_clip_y)) {
        return std::nullopt;
    }
    const int stroke = std::max(0, box_name_dropdown_style_.panel_border_thickness);
    const int inner_x = outer.x + stroke;
    const int inner_w = outer.w - stroke * 2;
    if (logical_x < inner_x || logical_x >= inner_x + inner_w || logical_y < list_clip_y ||
        logical_y >= list_clip_y + list_h) {
        return std::nullopt;
    }
    const int rh = std::max(1, resort_box_browser_.dropdownRowHeightPx());
    const double rel_y = static_cast<double>(logical_y - list_clip_y) + resort_box_browser_.dropdownScrollPx();
    const int idx = static_cast<int>(std::floor(rel_y / static_cast<double>(rh)));
    const int rows = transfer_system::GameBoxBrowserController::dropdownListRowCount(
        static_cast<int>(resort_pc_boxes_.size()));
    if (idx < 0 || idx >= rows) {
        return std::nullopt;
    }
    return idx;
}

void TransferSystemScreen::clampResortDropdownScroll(int inner_draw_h) {
    resort_box_browser_.clampDropdownScroll(static_cast<int>(resort_pc_boxes_.size()), inner_draw_h);
}

void TransferSystemScreen::syncResortDropdownScrollToHighlight(int inner_draw_h) {
    resort_box_browser_.syncDropdownScrollToHighlight(static_cast<int>(resort_pc_boxes_.size()), inner_draw_h);
}

void TransferSystemScreen::closeResortBoxDropdown() {
    resort_box_browser_.closeGameBoxDropdown();
}

void TransferSystemScreen::toggleResortBoxDropdown() {
    SDL_Rect outer{};
    int list_h = 0;
    int list_clip_y = 0;
    const int inner_h = computeResortBoxDropdownOuterRect(outer, 1.f, list_h, list_clip_y) ? list_h : 0;
    if (resort_box_browser_.toggleGameBoxDropdown(
            box_name_dropdown_style_.enabled,
            resort_box_browser_.gameBoxSpaceMode(),
            static_cast<int>(resort_pc_boxes_.size()),
            inner_h)) {
        resort_dropdown_labels_dirty_ = true;
        ui_state_.requestButtonSfx();
    }
    if (resort_box_browser_.dropdownOpenTarget()) {
        closeGameBoxDropdown();
    }
}

void TransferSystemScreen::stepResortDropdownHighlight(int delta) {
    SDL_Rect outer{};
    int list_h = 0;
    int list_clip_y = 0;
    const int inner_h =
        computeResortBoxDropdownOuterRect(outer, static_cast<float>(resort_box_browser_.dropdownExpandT()), list_h, list_clip_y)
            ? list_h
            : 0;
    resort_box_browser_.stepDropdownHighlight(delta, static_cast<int>(resort_pc_boxes_.size()), inner_h);
}

void TransferSystemScreen::applyResortBoxDropdownSelection() {
    if (!resort_box_viewport_ || resort_pc_boxes_.empty()) {
        return;
    }
    const int n = static_cast<int>(resort_pc_boxes_.size());
    if (n < 2) {
        return;
    }
    const int hi = resort_box_browser_.dropdownHighlightIndex();
    if (hi <= 0) {
        closeResortBoxDropdown();
        openBoxRenameModal(BoxRenameModalPanel::Resort);
        return;
    }
    const int target_box = hi - 1;
    const bool changed =
        resort_box_browser_.jumpGameBoxToIndex(target_box, n, panelsReadyForInteraction());
    if (!changed) {
        return;
    }
    resort_box_viewport_->snapContentToModel(resortBoxViewportModelAt(resort_box_browser_.gameBoxIndex()));
    ui_state_.requestButtonSfx();
}

bool TransferSystemScreen::gameDropdownNavigationActive() const {
    return box_name_dropdown_style_.enabled && game_box_browser_.dropdownOpenTarget() &&
           game_box_browser_.dropdownExpandT() > 0.08 && game_pc_boxes_.size() >= 2;
}

bool TransferSystemScreen::resortDropdownNavigationActive() const {
    return box_name_dropdown_style_.enabled && resort_box_browser_.dropdownOpenTarget() &&
           resort_box_browser_.dropdownExpandT() > 0.08 && resort_pc_boxes_.size() >= 2;
}

void TransferSystemScreen::applyActiveDropdownSelection() {
    if (gameDropdownNavigationActive()) {
        applyGameBoxDropdownSelection();
    } else if (resortDropdownNavigationActive()) {
        applyResortBoxDropdownSelection();
    }
}

void TransferSystemScreen::rebuildDropdownItemTextures(SDL_Renderer* renderer) {
    dropdown_item_textures_.clear();
    if (!dropdown_item_font_.get()) {
        dropdown_labels_dirty_ = false;
        return;
    }
    const Color text_color = box_name_dropdown_style_.item_text_color;
    int max_h = 8;
    const int box_n = static_cast<int>(game_pc_boxes_.size());
    if (transfer_system::GameBoxBrowserController::dropdownListRowCount(box_n) > box_n) {
        TextureHandle rename_tex =
            renderTextTexture(renderer, dropdown_item_font_.get(), "RENAME BOX...", text_color);
        max_h = std::max(max_h, rename_tex.height);
        dropdown_item_textures_.push_back(std::move(rename_tex));
    }
    for (const auto& box : game_pc_boxes_) {
        TextureHandle tex = renderTextTexture(renderer, dropdown_item_font_.get(), box.name, text_color);
        max_h = std::max(max_h, tex.height);
        dropdown_item_textures_.push_back(std::move(tex));
    }
    game_box_browser_.setDropdownRowHeightPx(max_h + std::max(0, box_name_dropdown_style_.row_padding_y) * 2);
    dropdown_labels_dirty_ = false;
}

bool TransferSystemScreen::hitTestGameBoxNamePlate(int logical_x, int logical_y) const {
    if (!game_save_box_viewport_) {
        return false;
    }
    SDL_Rect r{};
    return game_save_box_viewport_->getNamePlateBounds(r) && logical_x >= r.x && logical_x < r.x + r.w &&
           logical_y >= r.y && logical_y < r.y + r.h;
}

void TransferSystemScreen::clampDropdownScroll(int inner_draw_h) {
    game_box_browser_.clampDropdownScroll(static_cast<int>(game_pc_boxes_.size()), inner_draw_h);
}

void TransferSystemScreen::syncDropdownScrollToHighlight(int inner_draw_h) {
    game_box_browser_.syncDropdownScrollToHighlight(static_cast<int>(game_pc_boxes_.size()), inner_draw_h);
}

bool TransferSystemScreen::computeGameBoxDropdownOuterRect(
    SDL_Rect& out_outer,
    float expand_scale,
    int& out_list_inner_h,
    int& out_list_clip_top_y) const {
    if (!game_save_box_viewport_) {
        return false;
    }
    SDL_Rect pill{};
    if (!game_save_box_viewport_->getNamePlateBounds(pill)) {
        return false;
    }
    const int stroke = std::max(0, box_name_dropdown_style_.panel_border_thickness);
    const int panel_w = std::max(1, box_name_dropdown_style_.panel_width_pixels);
    const int cx = pill.x + pill.w / 2;
    const int panel_left = cx - panel_w / 2;
    /// Shell emerges from the pill center (drawn behind / overlapping the lower half of the name plate).
    const int chrome_top = pill.y + pill.h / 2;
    /// List rows and clipping start flush under the full name plate.
    const int list_top = pill.y + pill.h;
    out_list_clip_top_y = list_top;

    const int screen_h = window_config_.virtual_height;
    const int bottom_margin = std::max(0, box_name_dropdown_style_.bottom_margin_pixels);
    const int ref_h = std::max(1, box_name_dropdown_style_.reference_name_plate_height_pixels);
    const float mult = std::max(0.1f, box_name_dropdown_style_.max_height_multiplier);
    const int max_by_spec = static_cast<int>(std::lround(static_cast<float>(ref_h) * mult));
    const int available_for_list = screen_h - list_top - bottom_margin;
    const int raw_list_cap = std::max(1, std::min(max_by_spec, std::max(1, available_for_list)));
    const int count = std::max(1, static_cast<int>(game_pc_boxes_.size()));
    const int rh = std::max(1, game_box_browser_.dropdownRowHeightPx());
    const int rows = transfer_system::GameBoxBrowserController::dropdownListRowCount(count);
    const int content_h = rows * rh;
    const int inner_list_max = std::min(raw_list_cap, std::max(rh, content_h));
    const float es = std::clamp(expand_scale, 0.f, 1.f);
    out_list_inner_h = std::max(1, static_cast<int>(std::lround(static_cast<double>(inner_list_max) * static_cast<double>(es))));

    const int outer_top = chrome_top;
    const int inner_fill_top = outer_top + stroke;
    const int stem_h = std::max(0, list_top - inner_fill_top);
    const int total_inner_fill_h = stem_h + out_list_inner_h;
    const int outer_w = panel_w + stroke * 2;
    const int outer_h = total_inner_fill_h + stroke * 2;
    out_outer = SDL_Rect{panel_left - stroke, outer_top, outer_w, outer_h};
    return true;
}

std::optional<int> TransferSystemScreen::dropdownRowIndexAtScreen(int logical_x, int logical_y) const {
    if (!game_box_browser_.dropdownOpenTarget() || game_pc_boxes_.empty()) {
        return std::nullopt;
    }
    SDL_Rect outer{};
    int list_h = 0;
    int list_clip_y = 0;
    if (!computeGameBoxDropdownOuterRect(outer, static_cast<float>(game_box_browser_.dropdownExpandT()), list_h, list_clip_y)) {
        return std::nullopt;
    }
    const int stroke = std::max(0, box_name_dropdown_style_.panel_border_thickness);
    const int inner_x = outer.x + stroke;
    const int inner_w = outer.w - stroke * 2;
    if (logical_x < inner_x || logical_x >= inner_x + inner_w || logical_y < list_clip_y ||
        logical_y >= list_clip_y + list_h) {
        return std::nullopt;
    }
    const int rh = std::max(1, game_box_browser_.dropdownRowHeightPx());
    const double rel_y = static_cast<double>(logical_y - list_clip_y) + game_box_browser_.dropdownScrollPx();
    const int idx = static_cast<int>(std::floor(rel_y / static_cast<double>(rh)));
    const int rows = transfer_system::GameBoxBrowserController::dropdownListRowCount(
        static_cast<int>(game_pc_boxes_.size()));
    if (idx < 0 || idx >= rows) {
        return std::nullopt;
    }
    return idx;
}

void TransferSystemScreen::stepDropdownHighlight(int delta) {
    if (gameDropdownNavigationActive()) {
        SDL_Rect outer{};
        int list_h = 0;
        int list_clip_y = 0;
        const int inner_h =
            computeGameBoxDropdownOuterRect(outer, static_cast<float>(game_box_browser_.dropdownExpandT()), list_h, list_clip_y)
                ? list_h
                : 0;
        game_box_browser_.stepDropdownHighlight(delta, static_cast<int>(game_pc_boxes_.size()), inner_h);
    } else if (resortDropdownNavigationActive()) {
        stepResortDropdownHighlight(delta);
    }
}

void TransferSystemScreen::closeGameBoxDropdown() {
    game_box_browser_.closeGameBoxDropdown();
}

void TransferSystemScreen::toggleGameBoxDropdown() {
    SDL_Rect outer{};
    int list_h = 0;
    int list_clip_y = 0;
    const int inner_h = computeGameBoxDropdownOuterRect(outer, 1.f, list_h, list_clip_y) ? list_h : 0;
    if (game_box_browser_.toggleGameBoxDropdown(
            box_name_dropdown_style_.enabled,
            game_box_browser_.gameBoxSpaceMode(),
            static_cast<int>(game_pc_boxes_.size()),
            inner_h)) {
        dropdown_labels_dirty_ = true;
        ui_state_.requestButtonSfx();
    }
    if (game_box_browser_.dropdownOpenTarget()) {
        closeResortBoxDropdown();
    }
}

void TransferSystemScreen::applyGameBoxDropdownSelection() {
    if (!game_save_box_viewport_ || game_pc_boxes_.empty()) {
        return;
    }
    const int n = static_cast<int>(game_pc_boxes_.size());
    if (n < 2) {
        return;
    }
    const int hi = game_box_browser_.dropdownHighlightIndex();
    if (hi <= 0) {
        closeGameBoxDropdown();
        openBoxRenameModal(BoxRenameModalPanel::Game);
        return;
    }
    const int target_box = hi - 1;
    const bool changed = game_box_browser_.jumpGameBoxToIndex(target_box, n, panelsReadyForInteraction());
    if (!changed) {
        return;
    }
    game_save_box_viewport_->snapContentToModel(gameBoxViewportModelAt(game_box_browser_.gameBoxIndex()));
    ui_state_.requestButtonSfx();
}

bool TransferSystemScreen::handleGameBoxSpacePointerPressed(int logical_x, int logical_y) {
    if (!game_save_box_viewport_ || !panelsReadyForInteraction()) {
        return false;
    }

    SDL_Rect r{};
    if (game_save_box_viewport_->getFooterBoxSpaceBounds(r) &&
        logical_x >= r.x && logical_x < r.x + r.w && logical_y >= r.y && logical_y < r.y + r.h) {
        setGameBoxSpaceMode(!game_box_browser_.gameBoxSpaceMode());
        ui_state_.requestButtonSfx();
        closeGameBoxDropdown();
        closeResortBoxDropdown();
        return true;
    }
    if (game_save_box_viewport_->hitTestBoxSpaceScrollArrow(logical_x, logical_y)) {
        stepGameBoxSpaceRowDown();
        closeGameBoxDropdown();
        return true;
    }
    if (!game_box_browser_.gameBoxSpaceMode()) {
        return false;
    }

    const SDL_Rect grid_clip = [&]() {
        SDL_Rect s0{};
        SDL_Rect s29{};
        if (!game_save_box_viewport_->getSlotBounds(0, s0) || !game_save_box_viewport_->getSlotBounds(29, s29)) {
            return SDL_Rect{0, 0, 0, 0};
        }
        const int left = s0.x;
        const int top = s0.y;
        const int right = s29.x + s29.w;
        const int bottom = s29.y + s29.h;
        return SDL_Rect{left, top, std::max(0, right - left), std::max(0, bottom - top)};
    }();
    if (grid_clip.w > 0 && grid_clip.h > 0 &&
        logical_x >= grid_clip.x && logical_x < grid_clip.x + grid_clip.w &&
        logical_y >= grid_clip.y && logical_y < grid_clip.y + grid_clip.h) {
        box_space_interaction_panel_ = BoxSpaceInteractionPanel::Game;
        box_space_drag_active_ = true;
        box_space_drag_last_y_ = logical_y;
        box_space_drag_accum_ = 0.0;
        box_space_quick_drop_pending_ = false;
        box_space_quick_drop_kind_ = BoxSpaceQuickDropKind::None;
        box_space_quick_drop_elapsed_seconds_ = 0.0;
        box_space_quick_drop_target_box_index_ = -1;
        if (const std::optional<FocusNodeId> picked = focusNodeAtPointer(logical_x, logical_y)) {
            if (*picked >= 2000 && *picked <= 2029) {
                // In Box Space, pointer press should update focus immediately so any callouts anchor correctly.
                focus_.setCurrent(*picked);
                selection_cursor_hidden_after_mouse_ = true;
                speech_hover_active_ = true;
                box_space_pressed_cell_ = *picked - 2000;
                const int box_index = game_box_browser_.gameBoxSpaceRowOffset() * 6 + box_space_pressed_cell_;
                SDL_Rect cell_bounds{};
                const bool have_cell =
                    box_index >= 0 && box_index < static_cast<int>(game_pc_boxes_.size()) &&
                    game_save_box_viewport_->getSlotBounds(box_space_pressed_cell_, cell_bounds);
                // If not holding Pokémon/items, a long hold on the cell can turn into a box move (drag-to-scroll stays default).
                if (!pokemon_move_.active() && !multi_pokemon_move_.active() && !held_move_.heldItem()) {
                    if (have_cell) {
                        box_space_box_move_source_box_index_ = box_index;
                        box_space_box_move_hold_.start(
                            SDL_Point{logical_x, logical_y},
                            cell_bounds,
                            box_space_long_press_style_.box_swap_hold_seconds);
                    }
                } else if (held_move_.heldItem()) {
                    if (have_cell && ui_state_.selectedToolIndex() == 3) {
                        box_space_quick_drop_pending_ = true;
                        box_space_quick_drop_kind_ = BoxSpaceQuickDropKind::Item;
                        box_space_quick_drop_elapsed_seconds_ = 0.0;
                        box_space_quick_drop_start_pointer_ = SDL_Point{logical_x, logical_y};
                        box_space_quick_drop_start_cell_bounds_ = cell_bounds;
                        box_space_quick_drop_target_box_index_ = box_index;
                    }
                } else {
                    // Holding Pokémon: press-and-hold (without moving) attempts quick-drop into first empty slots.
                    if (have_cell) {
                        box_space_quick_drop_pending_ = true;
                        box_space_quick_drop_kind_ =
                            multi_pokemon_move_.active()
                                ? BoxSpaceQuickDropKind::PokemonMulti
                                : BoxSpaceQuickDropKind::PokemonSingle;
                        box_space_quick_drop_elapsed_seconds_ = 0.0;
                        box_space_quick_drop_start_pointer_ = SDL_Point{logical_x, logical_y};
                        box_space_quick_drop_start_cell_bounds_ = cell_bounds;
                        box_space_quick_drop_target_box_index_ = box_index;
                    }
                }
            }
        }
        return true;
    }

    if (const std::optional<FocusNodeId> picked = focusNodeAtPointer(logical_x, logical_y)) {
        if (*picked >= 2000 && *picked <= 2029) {
            focus_.setCurrent(*picked);
            return activateFocusedGameSlot();
        }
    }
    return false;
}

bool TransferSystemScreen::handleResortBoxSpacePointerPressed(int logical_x, int logical_y) {
    if (!resort_box_viewport_ || !panelsReadyForInteraction()) {
        return false;
    }

    SDL_Rect r{};
    if (resort_box_viewport_->getFooterBoxSpaceBounds(r) &&
        logical_x >= r.x && logical_x < r.x + r.w && logical_y >= r.y && logical_y < r.y + r.h) {
        setResortBoxSpaceMode(!resort_box_browser_.gameBoxSpaceMode());
        ui_state_.requestButtonSfx();
        closeGameBoxDropdown();
        closeResortBoxDropdown();
        return true;
    }
    if (resort_box_browser_.gameBoxSpaceMode()) {
        SDL_Rect scroll_r{};
        if (resort_box_viewport_->getResortScrollArrowBounds(scroll_r) && logical_x >= scroll_r.x &&
            logical_x < scroll_r.x + scroll_r.w && logical_y >= scroll_r.y && logical_y < scroll_r.y + scroll_r.h) {
            stepResortBoxSpaceRowDown();
            closeResortBoxDropdown();
            return true;
        }
    }
    if (!resort_box_browser_.gameBoxSpaceMode()) {
        return false;
    }

    const SDL_Rect grid_clip = [&]() {
        SDL_Rect s0{};
        SDL_Rect s29{};
        if (!resort_box_viewport_->getSlotBounds(0, s0) || !resort_box_viewport_->getSlotBounds(29, s29)) {
            return SDL_Rect{0, 0, 0, 0};
        }
        const int left = s0.x;
        const int top = s0.y;
        const int right = s29.x + s29.w;
        const int bottom = s29.y + s29.h;
        return SDL_Rect{left, top, std::max(0, right - left), std::max(0, bottom - top)};
    }();
    if (grid_clip.w > 0 && grid_clip.h > 0 &&
        logical_x >= grid_clip.x && logical_x < grid_clip.x + grid_clip.w &&
        logical_y >= grid_clip.y && logical_y < grid_clip.y + grid_clip.h) {
        box_space_interaction_panel_ = BoxSpaceInteractionPanel::Resort;
        box_space_drag_active_ = true;
        box_space_drag_last_y_ = logical_y;
        box_space_drag_accum_ = 0.0;
        box_space_quick_drop_pending_ = false;
        box_space_quick_drop_kind_ = BoxSpaceQuickDropKind::None;
        box_space_quick_drop_elapsed_seconds_ = 0.0;
        box_space_quick_drop_target_box_index_ = -1;
        if (const std::optional<FocusNodeId> picked = focusNodeAtPointer(logical_x, logical_y)) {
            if (*picked >= 1000 && *picked <= 1029) {
                focus_.setCurrent(*picked);
                selection_cursor_hidden_after_mouse_ = true;
                speech_hover_active_ = true;
                box_space_pressed_cell_ = *picked - 1000;
                const int box_index = resort_box_browser_.gameBoxSpaceRowOffset() * 6 + box_space_pressed_cell_;
                SDL_Rect cell_bounds{};
                const bool have_cell =
                    box_index >= 0 && box_index < static_cast<int>(resort_pc_boxes_.size()) &&
                    resort_box_viewport_->getSlotBounds(box_space_pressed_cell_, cell_bounds);
                if (!pokemon_move_.active() && !multi_pokemon_move_.active() && !held_move_.heldItem()) {
                    if (have_cell) {
                        box_space_box_move_source_box_index_ = box_index;
                        box_space_box_move_hold_.start(
                            SDL_Point{logical_x, logical_y},
                            cell_bounds,
                            box_space_long_press_style_.box_swap_hold_seconds);
                    }
                } else if (held_move_.heldItem()) {
                    if (have_cell && ui_state_.selectedToolIndex() == 3) {
                        box_space_quick_drop_pending_ = true;
                        box_space_quick_drop_kind_ = BoxSpaceQuickDropKind::Item;
                        box_space_quick_drop_elapsed_seconds_ = 0.0;
                        box_space_quick_drop_start_pointer_ = SDL_Point{logical_x, logical_y};
                        box_space_quick_drop_start_cell_bounds_ = cell_bounds;
                        box_space_quick_drop_target_box_index_ = box_index;
                    }
                } else {
                    if (have_cell) {
                        box_space_quick_drop_pending_ = true;
                        box_space_quick_drop_kind_ =
                            multi_pokemon_move_.active()
                                ? BoxSpaceQuickDropKind::PokemonMulti
                                : BoxSpaceQuickDropKind::PokemonSingle;
                        box_space_quick_drop_elapsed_seconds_ = 0.0;
                        box_space_quick_drop_start_pointer_ = SDL_Point{logical_x, logical_y};
                        box_space_quick_drop_start_cell_bounds_ = cell_bounds;
                        box_space_quick_drop_target_box_index_ = box_index;
                    }
                }
            }
        }
        return true;
    }

    if (const std::optional<FocusNodeId> picked = focusNodeAtPointer(logical_x, logical_y)) {
        if (*picked >= 1000 && *picked <= 1029) {
            focus_.setCurrent(*picked);
            return activateFocusedResortSlot();
        }
    }
    return false;
}

bool TransferSystemScreen::handleDropdownPointerPressed(int logical_x, int logical_y) {
    if (!box_name_dropdown_style_.enabled || game_pc_boxes_.size() < 2) {
        return false;
    }

    if (game_box_browser_.dropdownOpenTarget() && game_box_browser_.dropdownExpandT() > 0.05) {
        SDL_Rect outer{};
        int list_h = 0;
        int list_clip_y = 0;
        if (computeGameBoxDropdownOuterRect(outer, static_cast<float>(game_box_browser_.dropdownExpandT()), list_h, list_clip_y)) {
            const bool in_outer = logical_x >= outer.x && logical_x < outer.x + outer.w && logical_y >= outer.y &&
                                  logical_y < outer.y + outer.h;
            if (in_outer) {
                dropdown_lmb_down_in_panel_ = true;
                dropdown_lmb_last_y_ = logical_y;
                dropdown_lmb_drag_accum_ = 0.0;
                if (const std::optional<int> row = dropdownRowIndexAtScreen(logical_x, logical_y)) {
                    stepDropdownHighlight(*row - game_box_browser_.dropdownHighlightIndex());
                }
                return true;
            }
            if (hitTestGameBoxNamePlate(logical_x, logical_y)) {
                toggleGameBoxDropdown();
                return true;
            }
            closeGameBoxDropdown();
            return true;
        }
    } else if (!game_box_browser_.gameBoxSpaceMode() && panelsReadyForInteraction() &&
               hitTestGameBoxNamePlate(logical_x, logical_y)) {
        toggleGameBoxDropdown();
        return true;
    }

    return false;
}

bool TransferSystemScreen::handleGameBoxNavigationPointerPressed(int logical_x, int logical_y) {
    if (!game_save_box_viewport_ || !panelsReadyForInteraction() || game_pc_boxes_.empty()) {
        return false;
    }
    int dir = 0;
    if (game_save_box_viewport_->hitTestPrevBoxArrow(logical_x, logical_y)) {
        dir = -1;
    } else if (game_save_box_viewport_->hitTestNextBoxArrow(logical_x, logical_y)) {
        dir = 1;
    }
    if (dir == 0) {
        return false;
    }
    advanceGameBox(dir);
    return true;
}

bool TransferSystemScreen::handleResortBoxNavigationPointerPressed(int logical_x, int logical_y) {
    if (!resort_box_viewport_ || !panelsReadyForInteraction() || resort_pc_boxes_.empty()) {
        return false;
    }
    int dir = 0;
    if (resort_box_viewport_->hitTestPrevBoxArrow(logical_x, logical_y)) {
        dir = -1;
    } else if (resort_box_viewport_->hitTestNextBoxArrow(logical_x, logical_y)) {
        dir = 1;
    }
    if (dir == 0) {
        return false;
    }
    advanceResortBox(dir);
    return true;
}

bool TransferSystemScreen::handleResortDropdownPointerPressed(int logical_x, int logical_y) {
    if (!box_name_dropdown_style_.enabled || resort_pc_boxes_.size() < 2) {
        return false;
    }

    if (resort_box_browser_.dropdownOpenTarget() && resort_box_browser_.dropdownExpandT() > 0.05) {
        SDL_Rect outer{};
        int list_h = 0;
        int list_clip_y = 0;
        if (computeResortBoxDropdownOuterRect(
                outer, static_cast<float>(resort_box_browser_.dropdownExpandT()), list_h, list_clip_y)) {
            const bool in_outer = logical_x >= outer.x && logical_x < outer.x + outer.w && logical_y >= outer.y &&
                                  logical_y < outer.y + outer.h;
            if (in_outer) {
                dropdown_lmb_down_in_panel_ = true;
                dropdown_lmb_last_y_ = logical_y;
                dropdown_lmb_drag_accum_ = 0.0;
                if (const std::optional<int> row = resortDropdownRowIndexAtScreen(logical_x, logical_y)) {
                    stepResortDropdownHighlight(*row - resort_box_browser_.dropdownHighlightIndex());
                }
                return true;
            }
            if (hitTestResortBoxNamePlate(logical_x, logical_y)) {
                toggleResortBoxDropdown();
                return true;
            }
            closeResortBoxDropdown();
            return true;
        }
    } else if (!resort_box_browser_.gameBoxSpaceMode() && panelsReadyForInteraction() &&
               hitTestResortBoxNamePlate(logical_x, logical_y)) {
        toggleResortBoxDropdown();
        return true;
    }

    return false;
}

void TransferSystemScreen::syncBoxSpaceMiniPreviewHoverFromPointer(int logical_x, int logical_y) {
    if (const std::optional<FocusNodeId> picked = focusNodeAtPointer(logical_x, logical_y)) {
        if (*picked >= 2000 && *picked <= 2029 && game_box_browser_.gameBoxSpaceMode() && game_save_box_viewport_) {
            const int cell = *picked - 2000;
            const int idx = game_box_browser_.gameBoxSpaceRowOffset() * 6 + cell;
            if (idx >= 0 && idx < static_cast<int>(game_pc_boxes_.size()) &&
                gameBoxHasPreviewContent(idx)) {
                mouse_hover_mini_preview_box_index_ = idx;
                mini_preview_model_from_resort_ = false;
            }
        } else if (*picked >= 1000 && *picked <= 1029 && resort_box_browser_.gameBoxSpaceMode() && resort_box_viewport_) {
            const int cell = *picked - 1000;
            const int idx = resort_box_browser_.gameBoxSpaceRowOffset() * 6 + cell;
            if (idx >= 0 && idx < static_cast<int>(resort_pc_boxes_.size()) &&
                resortBoxHasPreviewContent(idx)) {
                mouse_hover_mini_preview_box_index_ = idx;
                mini_preview_model_from_resort_ = true;
            }
        }
    }
}

void TransferSystemScreen::syncBoxSpaceMiniPreviewFromFocusedBoxSpaceCell() {
    const FocusNodeId cur = focus_.current();
    if (cur >= 2000 && cur <= 2029 && game_box_browser_.gameBoxSpaceMode() && game_save_box_viewport_) {
        const int cell = cur - 2000;
        const int idx = game_box_browser_.gameBoxSpaceRowOffset() * 6 + cell;
        if (idx >= 0 && idx < static_cast<int>(game_pc_boxes_.size()) && gameBoxHasPreviewContent(idx)) {
            mouse_hover_mini_preview_box_index_ = idx;
            mini_preview_model_from_resort_ = false;
        }
        return;
    }
    if (cur >= 1000 && cur <= 1029 && resort_box_browser_.gameBoxSpaceMode() && resort_box_viewport_) {
        const int cell = cur - 1000;
        const int idx = resort_box_browser_.gameBoxSpaceRowOffset() * 6 + cell;
        if (idx >= 0 && idx < static_cast<int>(resort_pc_boxes_.size()) && resortBoxHasPreviewContent(idx)) {
            mouse_hover_mini_preview_box_index_ = idx;
            mini_preview_model_from_resort_ = true;
        }
    }
}

void TransferSystemScreen::updateMiniPreview(double dt) {
    const bool have_game_preview = game_save_box_viewport_ && !game_pc_boxes_.empty();
    const bool have_resort_preview = resort_box_viewport_ && !resort_pc_boxes_.empty();
    if (!mini_preview_style_.enabled || (!have_game_preview && !have_resort_preview)) {
        mini_preview_target_ = 0.0;
        approachExponential(mini_preview_t_, mini_preview_target_, dt, std::max(1.0, mini_preview_style_.enter_smoothing));
        // Keep the last rendered model while we animate out; clear once fully hidden.
        if (mini_preview_t_ <= 1e-3) {
            mini_preview_box_index_ = -1;
            mini_preview_model_from_resort_ = false;
        }
        return;
    }

    if ((pokemon_move_.active() || multi_pokemon_move_.active()) &&
        (game_box_browser_.gameBoxSpaceMode() || resort_box_browser_.gameBoxSpaceMode())) {
        const FocusNodeId cur = focus_.current();
        if ((cur >= 2000 && cur <= 2029) || (cur >= 1000 && cur <= 1029)) {
            syncBoxSpaceMiniPreviewFromFocusedBoxSpaceCell();
        } else {
            syncBoxSpaceMiniPreviewHoverFromPointer(last_pointer_position_.x, last_pointer_position_.y);
        }
    }

    int wanted = -1;
    bool wanted_from_resort = false;
    const bool game_dropdown_visible =
        box_name_dropdown_style_.enabled && game_pc_boxes_.size() >= 2 && game_box_browser_.dropdownExpandT() > 0.08;
    const bool resort_dropdown_visible =
        box_name_dropdown_style_.enabled && resort_pc_boxes_.size() >= 2 && resort_box_browser_.dropdownExpandT() > 0.08;
    if (game_dropdown_visible) {
        const int hi = game_box_browser_.dropdownHighlightIndex();
        if (hi > 0) {
            const int candidate = hi - 1;
            if (candidate >= 0 && candidate < static_cast<int>(game_pc_boxes_.size()) &&
                shouldShowMiniPreviewForBox(candidate, MiniPreviewContext::Dropdown)) {
                wanted = candidate;
                wanted_from_resort = false;
            }
        }
    } else if (resort_dropdown_visible) {
        const int hi = resort_box_browser_.dropdownHighlightIndex();
        if (hi > 0) {
            const int candidate = hi - 1;
            if (candidate >= 0 && candidate < static_cast<int>(resort_pc_boxes_.size()) &&
                shouldShowMiniPreviewForResortBox(candidate, MiniPreviewContext::Dropdown)) {
                wanted = candidate;
                wanted_from_resort = true;
            }
        }
    } else if (selection_cursor_hidden_after_mouse_) {
        if (mini_preview_model_from_resort_) {
            if (shouldShowMiniPreviewForResortBox(mouse_hover_mini_preview_box_index_, MiniPreviewContext::MouseHover)) {
                wanted = mouse_hover_mini_preview_box_index_;
                wanted_from_resort = true;
            }
        } else if (shouldShowMiniPreviewForBox(mouse_hover_mini_preview_box_index_, MiniPreviewContext::MouseHover)) {
            wanted = mouse_hover_mini_preview_box_index_;
            wanted_from_resort = false;
        }
    } else if (game_box_browser_.gameBoxSpaceMode() || resort_box_browser_.gameBoxSpaceMode()) {
        const FocusNodeId cur = focus_.current();
        if (cur >= 2000 && cur <= 2029 && game_box_browser_.gameBoxSpaceMode()) {
            const int cell = cur - 2000;
            const int candidate = game_box_browser_.gameBoxSpaceRowOffset() * 6 + cell;
            if (shouldShowMiniPreviewForBox(candidate, MiniPreviewContext::BoxSpaceFocus)) {
                wanted = candidate;
                wanted_from_resort = false;
            }
        } else if (cur >= 1000 && cur <= 1029 && resort_box_browser_.gameBoxSpaceMode()) {
            const int cell = cur - 1000;
            const int candidate = resort_box_browser_.gameBoxSpaceRowOffset() * 6 + cell;
            if (shouldShowMiniPreviewForResortBox(candidate, MiniPreviewContext::BoxSpaceFocus)) {
                wanted = candidate;
                wanted_from_resort = true;
            }
        }
    }

    mini_preview_target_ = (wanted >= 0) ? 1.0 : 0.0;
    approachExponential(mini_preview_t_, mini_preview_target_, dt, std::max(1.0, mini_preview_style_.enter_smoothing));

    if (wanted >= 0 &&
        (wanted != mini_preview_box_index_ || wanted_from_resort != mini_preview_model_from_resort_)) {
        mini_preview_box_index_ = wanted;
        mini_preview_model_from_resort_ = wanted_from_resort;
        mini_preview_model_ =
            wanted_from_resort ? resortBoxViewportModelAt(wanted) : gameBoxViewportModelAt(wanted);
    }
    // When no target, animate out smoothly using the last cached model; clear once hidden.
    if (wanted < 0 && mini_preview_t_ <= 1e-3) {
        mini_preview_box_index_ = -1;
        mini_preview_model_from_resort_ = false;
    }
}

namespace {
std::optional<SDL_Rect> computeCenteredScaledRectClampedToBounds(
    const TextureHandle& tex,
    int cx,
    int cy,
    const SDL_Rect& clamp_bounds,
    double desired_scale) {
    if (!tex.texture || clamp_bounds.w <= 0 || clamp_bounds.h <= 0) {
        return std::nullopt;
    }
    desired_scale = std::max(0.01, desired_scale);
    const double dw0 = static_cast<double>(tex.width) * desired_scale;
    const double dh0 = static_cast<double>(tex.height) * desired_scale;
    const double sx = static_cast<double>(clamp_bounds.w) / std::max(1.0, dw0);
    const double sy = static_cast<double>(clamp_bounds.h) / std::max(1.0, dh0);
    const double clamp_scale = std::min(1.0, std::min(sx, sy));
    const int dw = std::max(1, static_cast<int>(std::round(dw0 * clamp_scale)));
    const int dh = std::max(1, static_cast<int>(std::round(dh0 * clamp_scale)));
    SDL_Rect dst{cx - dw / 2, cy - dh / 2, dw, dh};
    dst.x = std::clamp(dst.x, clamp_bounds.x, clamp_bounds.x + clamp_bounds.w - dst.w);
    dst.y = std::clamp(dst.y, clamp_bounds.y, clamp_bounds.y + clamp_bounds.h - dst.h);
    return dst;
}

void drawTextureRect(SDL_Renderer* renderer, const TextureHandle& tex, const SDL_Rect& dst) {
    if (!tex.texture || dst.w <= 0 || dst.h <= 0) {
        return;
    }
    SDL_SetTextureBlendMode(tex.texture.get(), SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(tex.texture.get(), 255);
    SDL_SetTextureColorMod(tex.texture.get(), 255, 255, 255);
    SDL_RenderCopy(renderer, tex.texture.get(), nullptr, &dst);
}
} // namespace

void TransferSystemScreen::drawMiniPreview(SDL_Renderer* renderer) const {
    if (!mini_preview_style_.enabled || mini_preview_t_ <= 1e-3 || mini_preview_box_index_ < 0) {
        return;
    }
#ifdef PR_ENABLE_TEST_HOOKS
    debug_mini_preview_first_sprite_rect_.reset();
    debug_mini_preview_cell_size_ = SDL_Point{0, 0};
#endif

    const Color kBorder{224, 224, 224, 255}; // same gray as box component background
    const Color kFill{251, 251, 251, 255};   // same as slot fill

    const int w = std::max(1, mini_preview_style_.width);
    const int h = std::max(1, mini_preview_style_.height);
    const int pad = std::max(0, mini_preview_style_.edge_pad);
    const int y = std::max(0, mini_preview_style_.offset_y);

    // Game-side preview: slide in from the left. Resort preview: same vertical band, from the right edge.
    const int vw = window_config_.virtual_width;
    int hidden_x = -w - pad;
    int shown_x = pad;
    if (mini_preview_model_from_resort_) {
        hidden_x = vw + pad;
        shown_x = vw - w - pad;
    }
    const int x = static_cast<int>(std::lround(hidden_x + (shown_x - hidden_x) * mini_preview_t_));

    const int r = std::clamp(mini_preview_style_.corner_radius, 0, std::min(w, h) / 2);
    const int stroke = std::clamp(mini_preview_style_.border_thickness, 1, std::min(w, h) / 2);

    fillRoundedRectScanlines(renderer, x, y, w, h, r, kBorder);
    fillRoundedRectScanlines(
        renderer,
        x + stroke,
        y + stroke,
        w - 2 * stroke,
        h - 2 * stroke,
        std::max(0, r - stroke),
        kFill);

    const int inner_x = x + stroke + 10;
    const int inner_y = y + stroke + 10;
    const int inner_w = std::max(1, w - 2 * stroke - 20);
    const int inner_h = std::max(1, h - 2 * stroke - 20);

    constexpr int cols = 6;
    constexpr int rows = 5;
    const int gap = 3;
    const int cell_w = std::max(6, (inner_w - (cols - 1) * gap) / cols);
    const int cell_h = std::max(6, (inner_h - (rows - 1) * gap) / rows);
#ifdef PR_ENABLE_TEST_HOOKS
    debug_mini_preview_cell_size_ = SDL_Point{cell_w, cell_h};
#endif
    const int grid_w = cols * cell_w + (cols - 1) * gap;
    const int grid_h = rows * cell_h + (rows - 1) * gap;
    const int gx = inner_x + (inner_w - grid_w) / 2;
    const int gy = inner_y + (inner_h - grid_h) / 2;
    const SDL_Rect preview_clip{inner_x, inner_y, inner_w, inner_h};

    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const int sx = gx + col * (cell_w + gap);
            const int sy = gy + row * (cell_h + gap);
            fillRoundedRectScanlines(renderer, sx, sy, cell_w, cell_h, 4, kFill);

            const std::size_t idx = static_cast<std::size_t>(row * cols + col);
            if (idx < mini_preview_model_.slot_sprites.size()) {
                const auto& slot = mini_preview_model_.slot_sprites[idx];
                if (slot.has_value() && slot->texture) {
                    const auto dst = computeCenteredScaledRectClampedToBounds(
                        *slot,
                        sx + cell_w / 2,
                        sy + cell_h / 2,
                        preview_clip,
                        mini_preview_style_.sprite_scale);
                    if (dst) {
#ifdef PR_ENABLE_TEST_HOOKS
                        if (idx == 0) {
                            debug_mini_preview_first_sprite_rect_ = *dst;
                        }
#endif
                        drawTextureRect(renderer, *slot, *dst);
                    }
                }
            }
        }
    }
}

void TransferSystemScreen::update(double dt) {
    updateAnimations(dt);
    updateEnterExit(dt);
    updateCarouselSlide(dt);
    if (box_rename_modal_open_) {
        box_rename_caret_blink_phase_ += dt;
        if (box_rename_caret_blink_phase_ > 640.0) {
            box_rename_caret_blink_phase_ -= 640.0;
        }
    }
    updateGameBoxDropdown(dt);
    updateResortBoxDropdown(dt);
    updateMiniPreview(dt);
    updatePokemonActionMenu(dt);
    item_action_menu_.update(dt, pokemon_action_menu_style_);
    if (item_action_menu_.visible() && pokemon_action_menu_font_.get()) {
        int max_w = 0;
        for (int i = 0; i < item_action_menu_.rowCount(); ++i) {
            const std::string& label = item_action_menu_.labelAt(i);
            int w = 0;
            int h = 0;
            if (!label.empty() && TTF_SizeUTF8(pokemon_action_menu_font_.get(), label.c_str(), &w, &h) == 0) {
                max_w = std::max(max_w, w);
            }
        }
        // Match renderer padding: text starts at x+28, add right padding + some slack.
        const int desired = std::max(140, max_w + 88);
        item_action_menu_.setPreferredWidth(desired);
    }

    // Box Space: if the user is holding the mouse down in the grid, allow a hold-to-pickup box move
    // without interfering with normal drag-to-scroll (movement cancels the hold activation).
    if (box_space_drag_active_ &&
        box_space_box_move_hold_.active &&
        !pokemon_move_.active() &&
        !multi_pokemon_move_.active() &&
        !held_move_.heldBox()) {
        const bool game_bs = game_box_browser_.gameBoxSpaceMode();
        const bool resort_bs = resort_box_browser_.gameBoxSpaceMode();
        const bool panel_ok =
            (game_bs && box_space_interaction_panel_ == BoxSpaceInteractionPanel::Game) ||
            (resort_bs && box_space_interaction_panel_ == BoxSpaceInteractionPanel::Resort);
        if (panel_ok && box_space_box_move_hold_.update(dt, last_pointer_position_)) {
            if (box_space_box_move_source_box_index_ >= 0) {
                if (box_space_interaction_panel_ == BoxSpaceInteractionPanel::Resort) {
                    held_move_.pickUpBox(
                        transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Resort,
                        box_space_box_move_source_box_index_,
                        transfer_system::move::HeldMoveController::InputMode::Pointer,
                        last_pointer_position_);
                } else {
                    held_move_.pickUpBox(
                        transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Game,
                        box_space_box_move_source_box_index_,
                        transfer_system::move::HeldMoveController::InputMode::Pointer,
                        last_pointer_position_);
                }
                // Cancel scroll drag and treat the rest of the gesture as a held box move.
                box_space_drag_active_ = false;
                box_space_drag_accum_ = 0.0;
                box_space_pressed_cell_ = -1;
                box_space_box_move_hold_.cancel();
                refreshGameBoxViewportModel();
                refreshResortBoxViewportModel();
                requestPickupSfx();
            }
        }
    }

    // Box Space: pointer press-and-hold tries to quick-drop Pokémon or place an item onto eligible party slots.
    if ((game_box_browser_.gameBoxSpaceMode() || resort_box_browser_.gameBoxSpaceMode()) &&
        box_space_drag_active_ &&
        box_space_quick_drop_pending_) {
        auto in = [](int px, int py, const SDL_Rect& r) {
            return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
        };
        const bool still_in_cell = in(
            last_pointer_position_.x,
            last_pointer_position_.y,
            box_space_quick_drop_start_cell_bounds_);
        const int dx = last_pointer_position_.x - box_space_quick_drop_start_pointer_.x;
        const int dy = last_pointer_position_.y - box_space_quick_drop_start_pointer_.y;
        constexpr int kQuickDropCancelThresholdPx = 6;
        const bool moved_far = (dx * dx + dy * dy) >= kQuickDropCancelThresholdPx * kQuickDropCancelThresholdPx;
        if (!still_in_cell || moved_far) {
            clearBoxSpaceQuickDropGesture();
        } else {
            box_space_quick_drop_elapsed_seconds_ += dt;
            if (box_space_quick_drop_elapsed_seconds_ >= box_space_long_press_style_.quick_drop_hold_seconds) {
                const int target_box = box_space_quick_drop_target_box_index_;
                const bool dropped = completeBoxSpaceQuickDrop(target_box);
                clearBoxSpaceQuickDropGesture();
                if (dropped) {
                    // Prevent the corresponding release from being treated as a click (open box).
                    box_space_drag_active_ = false;
                    box_space_drag_accum_ = 0.0;
                    box_space_pressed_cell_ = -1;
                    refreshGameBoxViewportModel();
                    refreshResortBoxViewportModel();
                    // Force mini-preview + hover preview to refresh (box occupancy and contents changed).
                    mini_preview_box_index_ = -1;
                    mouse_hover_mini_preview_box_index_ = target_box;
                    mini_preview_model_from_resort_ =
                        box_space_interaction_panel_ == BoxSpaceInteractionPanel::Resort;
                } else {
                    box_space_suppress_click_open_on_release_ = true;
                    triggerHeldSpriteRejectFeedback();
                }
            }
        }
    }

    updateBoxSpaceQuickDropVisuals(dt);

    if (!multiPokemonToolActive()) {
        keyboard_multi_marquee_active_ = false;
    }

    const bool item_overlay_active = itemToolActive();
    const bool focus_dimming_active =
        pokemon_action_menu_style_.dim_background_sprites &&
        (pokemon_action_menu_.visible() || item_action_menu_.visible()) &&
        !pokemon_move_.active() &&
        !multi_pokemon_move_.active() &&
        !held_move_.heldItem();
    const std::optional<int> resort_dim_slot =
        focus_dimming_active && !pokemon_action_menu_.fromGameBox() && pokemon_action_menu_.visible()
            ? std::optional<int>(pokemon_action_menu_.slotIndex())
            : (focus_dimming_active && !item_action_menu_.fromGameBox() && item_action_menu_.visible()
                   ? std::optional<int>(item_action_menu_.slotIndex())
                   : std::nullopt);
    const std::optional<int> game_dim_slot =
        focus_dimming_active && pokemon_action_menu_.fromGameBox() && pokemon_action_menu_.visible()
            ? std::optional<int>(pokemon_action_menu_.slotIndex())
            : (focus_dimming_active && item_action_menu_.fromGameBox() && item_action_menu_.visible()
                   ? std::optional<int>(item_action_menu_.slotIndex())
                   : std::nullopt);
    if (resort_box_viewport_) {
        resort_box_viewport_->setItemOverlayActive(item_overlay_active);
        resort_box_viewport_->setFocusDimming(
            focus_dimming_active,
            resort_dim_slot,
            pokemon_action_menu_style_.dim_sprite_mod_color);
        resort_box_viewport_->update(dt);
    }
    if (game_save_box_viewport_) {
        game_save_box_viewport_->setItemOverlayActive(item_overlay_active);
        game_save_box_viewport_->setFocusDimming(
            focus_dimming_active,
            game_dim_slot,
            pokemon_action_menu_style_.dim_sprite_mod_color);
        game_save_box_viewport_->update(dt);
    }
    syncBoxViewportPositions();

    // Commit box index once the content slide finishes.
}

void TransferSystemScreen::updatePokemonActionMenu(double dt) {
    pokemon_action_menu_.update(dt, pokemon_action_menu_style_);
}

void TransferSystemScreen::openPokemonActionMenu(bool from_game_box, int slot_index, const SDL_Rect& anchor_rect) {
    pokemon_action_menu_.open(from_game_box, slot_index, anchor_rect);
    ui_state_.requestButtonSfx();
}

void TransferSystemScreen::closePokemonActionMenu() {
    pokemon_action_menu_.close();
}

bool TransferSystemScreen::pokemonActionMenuInteractive() const {
    return pokemon_action_menu_.interactive();
}

SDL_Rect TransferSystemScreen::pokemonActionMenuFinalRect() const {
    return pokemon_action_menu_.finalRect(
        pokemon_action_menu_style_,
        window_config_.virtual_width,
        pokemonActionMenuBottomLimitY());
}

int TransferSystemScreen::pokemonActionMenuBottomLimitY() const {
    if (!info_banner_style_.enabled) {
        return window_config_.virtual_height;
    }
    constexpr int kGapAboveBanner = 5;
    const int total_h = std::max(0, info_banner_style_.separator_height) + std::max(0, info_banner_style_.info_height);
    const int banner_top = window_config_.virtual_height - total_h;
    return std::max(1, banner_top - kGapAboveBanner);
}

const PcSlotSpecies* TransferSystemScreen::pokemonActionMenuPokemon() const {
    if (!pokemon_action_menu_.visible()) {
        return nullptr;
    }
    const int slot_index = pokemon_action_menu_.slotIndex();
    if (!pokemon_action_menu_.fromGameBox()) {
        const int box_index = resort_box_browser_.gameBoxIndex();
        if (box_index < 0 || box_index >= static_cast<int>(resort_pc_boxes_.size()) || slot_index < 0) {
            return nullptr;
        }
        const auto& slots = resort_pc_boxes_[static_cast<std::size_t>(box_index)].slots;
        if (slot_index >= static_cast<int>(slots.size())) {
            return nullptr;
        }
        const PcSlotSpecies& slot = slots[static_cast<std::size_t>(slot_index)];
        return slot.occupied() ? &slot : nullptr;
    }
    const int box_index = game_box_browser_.gameBoxIndex();
    if (box_index < 0 || box_index >= static_cast<int>(game_pc_boxes_.size()) || slot_index < 0) {
        return nullptr;
    }
    const auto& slots = game_pc_boxes_[static_cast<std::size_t>(box_index)].slots;
    if (slot_index >= static_cast<int>(slots.size())) {
        return nullptr;
    }
    const PcSlotSpecies& slot = slots[static_cast<std::size_t>(slot_index)];
    return slot.occupied() ? &slot : nullptr;
}

std::optional<int> TransferSystemScreen::pokemonActionMenuRowAtPoint(int logical_x, int logical_y) const {
    return pokemon_action_menu_.rowAtPoint(
        logical_x,
        logical_y,
        pokemon_action_menu_style_,
        window_config_.virtual_width,
        pokemonActionMenuBottomLimitY());
}

void TransferSystemScreen::activatePokemonActionMenuRow(int row) {
    if (pokemon_action_menu_.actionForRow(row) == transfer_system::PokemonActionMenuController::Action::Move) {
        using Move = transfer_system::PokemonMoveController;
        const Move::SlotRef ref{
            pokemon_action_menu_.fromGameBox() ? Move::Panel::Game : Move::Panel::Resort,
            pokemon_action_menu_.fromGameBox() ? game_box_browser_.gameBoxIndex() : resort_box_browser_.gameBoxIndex(),
            pokemon_action_menu_.slotIndex()};
        const Move::InputMode input_mode = selection_cursor_hidden_after_mouse_
            ? Move::InputMode::Pointer
            : Move::InputMode::Keyboard;
        (void)beginPokemonMoveFromSlot(ref, input_mode, Move::PickupSource::ActionMenu, last_pointer_position_);
        return;
    }
    closePokemonActionMenu();
    ui_state_.requestButtonSfx();
}

void TransferSystemScreen::hoverPokemonActionMenuRow(int logical_x, int logical_y) {
    if (const std::optional<int> row = pokemonActionMenuRowAtPoint(logical_x, logical_y)) {
        pokemon_action_menu_.selectRow(*row);
    }
}

bool TransferSystemScreen::activateFocusedPokemonSlotActionMenu() {
    if (!normalPokemonToolActive()) {
        return false;
    }
    const FocusNodeId cur = focus_.current();
    SDL_Rect r{};
    if (cur >= 2000 && cur <= 2029 && game_save_box_viewport_) {
        if (game_box_browser_.gameBoxSpaceMode()) {
            return false;
        }
        const int slot = cur - 2000;
        if (gameSaveSlotHasSpecies(slot) && game_save_box_viewport_->getSlotBounds(slot, r)) {
            openPokemonActionMenu(true, slot, r);
            return true;
        }
    }
    if (cur >= 1000 && cur <= 1029 && resort_box_viewport_) {
        if (resort_box_browser_.gameBoxSpaceMode()) {
            return false;
        }
        const int slot = cur - 1000;
        if (resortSlotHasSpecies(slot) && resort_box_viewport_->getSlotBounds(slot, r)) {
            openPokemonActionMenu(false, slot, r);
            return true;
        }
    }
    return false;
}

bool TransferSystemScreen::handlePokemonActionMenuPointerPressed(int logical_x, int logical_y) {
    if (!pokemon_action_menu_.visible()) {
        return false;
    }
    const std::optional<int> row = pokemonActionMenuRowAtPoint(logical_x, logical_y);
    if (row.has_value()) {
        activatePokemonActionMenuRow(*row);
        return true;
    }
    closePokemonActionMenu();
    return true;
}

bool TransferSystemScreen::handleItemActionMenuPointerPressed(int logical_x, int logical_y) {
    if (!item_action_menu_.visible()) {
        return false;
    }
    const std::optional<int> row = item_action_menu_.rowAtPoint(
        logical_x,
        logical_y,
        pokemon_action_menu_style_,
        window_config_.virtual_width,
        pokemonActionMenuBottomLimitY());
    if (row.has_value()) {
        ui_state_.requestButtonSfx();
        item_action_menu_.selectRow(*row);
        const auto action = item_action_menu_.actionForRow(*row);
        if (action == transfer_system::ItemActionMenuController::Action::MoveItem) {
            using Move = transfer_system::PokemonMoveController;
            const Move::SlotRef ref{
                item_action_menu_.fromGameBox() ? Move::Panel::Game : Move::Panel::Resort,
                item_action_menu_.fromGameBox() ? game_box_browser_.gameBoxIndex() : resort_box_browser_.gameBoxIndex(),
                item_action_menu_.slotIndex()};
            PcSlotSpecies* src = mutablePokemonAt(ref);
            if (src && src->occupied() && src->held_item_id > 0) {
                held_move_.pickUpItem(
                    src->held_item_id,
                    src->held_item_name,
                    transfer_system::move::HeldMoveController::PokemonSlotRef{
                        item_action_menu_.fromGameBox()
                            ? transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Game
                            : transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Resort,
                        item_action_menu_.fromGameBox() ? game_box_browser_.gameBoxIndex() : resort_box_browser_.gameBoxIndex(),
                        item_action_menu_.slotIndex()},
                    transfer_system::move::HeldMoveController::InputMode::Pointer,
                    last_pointer_position_);
                src->held_item_id = -1;
                src->held_item_name.clear();
                refreshResortBoxViewportModel();
                refreshGameBoxViewportModel();
                requestPickupSfx();
            }
            item_action_menu_.close();
            return true;
        }
        if (action == transfer_system::ItemActionMenuController::Action::PutAway) {
            item_action_menu_.goToPutAwayPage();
            return true;
        }
        if (action == transfer_system::ItemActionMenuController::Action::Back) {
            item_action_menu_.goToRootPage();
            return true;
        }
        // Not implemented yet.
        item_action_menu_.close();
        return true;
    }
    item_action_menu_.close();
    return true;
}

bool TransferSystemScreen::handlePokemonSlotActionPointerPressed(int logical_x, int logical_y) {
    if (!normalPokemonToolActive() && !swapToolActive()) {
        return false;
    }
    auto in = [](int px, int py, const SDL_Rect& r) {
        return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
    };
    SDL_Rect r{};
    if (game_save_box_viewport_ && !game_box_browser_.gameBoxSpaceMode()) {
        if (pointerOverExpandedGameDropdown(logical_x, logical_y)) {
            return false;
        }
        for (int i = 0; i < 30; ++i) {
            if (game_save_box_viewport_->getSlotBounds(i, r) && in(logical_x, logical_y, r)) {
                if (!gameSaveSlotHasSpecies(i)) {
                    return false;
                }
                if (swapToolActive()) {
                    return beginPokemonMoveFromSlot(
                        transfer_system::PokemonMoveController::SlotRef{
                            transfer_system::PokemonMoveController::Panel::Game,
                            game_box_browser_.gameBoxIndex(),
                            i},
                        transfer_system::PokemonMoveController::InputMode::Pointer,
                        transfer_system::PokemonMoveController::PickupSource::SwapTool,
                        last_pointer_position_);
                }
                openPokemonActionMenu(true, i, r);
                return true;
            }
        }
    }
    if (resort_box_viewport_ && !resort_box_browser_.gameBoxSpaceMode()) {
        if (pointerOverExpandedResortDropdown(logical_x, logical_y)) {
            return false;
        }
        for (int i = 0; i < 30; ++i) {
            if (resort_box_viewport_->getSlotBounds(i, r) && in(logical_x, logical_y, r)) {
                if (!resortSlotHasSpecies(i)) {
                    return false;
                }
                if (swapToolActive()) {
                    return beginPokemonMoveFromSlot(
                        transfer_system::PokemonMoveController::SlotRef{
                            transfer_system::PokemonMoveController::Panel::Resort,
                            resort_box_browser_.gameBoxIndex(),
                            i},
                        transfer_system::PokemonMoveController::InputMode::Pointer,
                        transfer_system::PokemonMoveController::PickupSource::SwapTool,
                        last_pointer_position_);
                }
                openPokemonActionMenu(false, i, r);
                return true;
            }
        }
    }
    return false;
}

bool TransferSystemScreen::handleItemSlotActionPointerPressed(int logical_x, int logical_y) {
    if (!itemToolActive() || game_box_browser_.dropdownOpenTarget() || resort_box_browser_.dropdownOpenTarget() ||
        held_move_.heldItem() || pokemon_move_.active() || multi_pokemon_move_.active()) {
        return false;
    }
    auto in = [](int px, int py, const SDL_Rect& r) {
        return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
    };
    SDL_Rect r{};
    if (game_save_box_viewport_ && !game_box_browser_.gameBoxSpaceMode()) {
        if (pointerOverExpandedGameDropdown(logical_x, logical_y)) {
            return false;
        }
        for (int i = 0; i < 30; ++i) {
            if (game_save_box_viewport_->getSlotBounds(i, r) && in(logical_x, logical_y, r)) {
                if (!gameSlotHasHeldItem(i)) {
                    return false;
                }
                item_action_menu_.setPutAwayGameLabel(selection_game_title_);
                item_action_menu_.open(true, i, r);
                ui_state_.requestButtonSfx();
                return true;
            }
        }
    }
    if (resort_box_viewport_ && !resort_box_browser_.gameBoxSpaceMode()) {
        if (pointerOverExpandedResortDropdown(logical_x, logical_y)) {
            return false;
        }
        const int resort_bi = resort_box_browser_.gameBoxIndex();
        if (resort_bi < 0 || resort_bi >= static_cast<int>(resort_pc_boxes_.size())) {
            return false;
        }
        const auto& resort_slots = resort_pc_boxes_[static_cast<std::size_t>(resort_bi)].slots;
        for (int i = 0; i < 30; ++i) {
            if (resort_box_viewport_->getSlotBounds(i, r) && in(logical_x, logical_y, r)) {
                if (i < 0 || i >= static_cast<int>(resort_slots.size())) {
                    return false;
                }
                if (!resort_slots[static_cast<std::size_t>(i)].occupied() ||
                    resort_slots[static_cast<std::size_t>(i)].held_item_id <= 0) {
                    return false;
                }
                item_action_menu_.setPutAwayGameLabel(selection_game_title_);
                item_action_menu_.open(false, i, r);
                ui_state_.requestButtonSfx();
                return true;
            }
        }
    }
    return false;
}

void TransferSystemScreen::togglePillTarget() {
    ui_state_.togglePillTarget();
}

bool TransferSystemScreen::hitTestPillTrack(int logical_x, int logical_y) const {
    int tx = 0;
    int ty = 0;
    int tw = 0;
    int th = 0;
    getPillTrackBounds(pill_style_, window_config_.virtual_width, tx, ty, tw, th);
    // Pill enters from above on first open / exit only.
    const int enter_off =
        static_cast<int>(std::lround((1.0 - ui_state_.uiEnter()) * static_cast<double>(-(th + 24))));
    ty += enter_off;
    return logical_x >= tx && logical_x < tx + tw && logical_y >= ty && logical_y < ty + th;
}

int TransferSystemScreen::carouselScreenY() const {
    const double t = ui_state_.panelsReveal();
    const double y = static_cast<double>(carousel_style_.rest_y) +
        (1.0 - t) * static_cast<double>(carousel_style_.hidden_y - carousel_style_.rest_y);
    return static_cast<int>(std::round(y));
}

int TransferSystemScreen::exitButtonScreenY() const {
    // Keep the exit button anchored during Items view transitions.
    // It should still slide in/out with the global UI enter/exit.
    const double t = ui_state_.uiEnter();
    const double y = static_cast<double>(carousel_style_.rest_y) +
        (1.0 - t) * static_cast<double>(carousel_style_.hidden_y - carousel_style_.rest_y);
    return static_cast<int>(std::round(y));
}

Color TransferSystemScreen::carouselFrameColorForIndex(int tool_index) const {
    switch (tool_index) {
        case 0:
            return carousel_style_.frame_multiple;
        case 1:
            return carousel_style_.frame_basic;
        case 2:
            return carousel_style_.frame_swap;
        case 3:
            return carousel_style_.frame_items;
        default:
            return carousel_style_.frame_basic;
    }
}

bool TransferSystemScreen::carouselSlideAnimating() const {
    return ui_state_.carouselSlideAnimating();
}

bool TransferSystemScreen::itemToolActive() const {
    return ui_state_.selectedToolIndex() == 3;
}

bool TransferSystemScreen::normalPokemonToolActive() const {
    return ui_state_.selectedToolIndex() == 1;
}

bool TransferSystemScreen::multiPokemonToolActive() const {
    return ui_state_.selectedToolIndex() == 0;
}

bool TransferSystemScreen::swapToolActive() const {
    return ui_state_.selectedToolIndex() == 2;
}

bool TransferSystemScreen::pokemonMoveActive() const {
    return pokemon_move_.active() || multi_pokemon_move_.active();
}

void TransferSystemScreen::requestPickupSfx() {
    pickup_sfx_requested_ = true;
}

void TransferSystemScreen::requestPutdownSfx() {
    putdown_sfx_requested_ = true;
}

bool TransferSystemScreen::gameSlotHasHeldItem(int slot_index) const {
    if (slot_index < 0 || slot_index >= 30) {
        return false;
    }
    const int game_box_index = game_box_browser_.gameBoxIndex();
    if (game_box_index < 0 || game_box_index >= static_cast<int>(game_pc_boxes_.size())) {
        return false;
    }
    const auto& slots = game_pc_boxes_[static_cast<std::size_t>(game_box_index)].slots;
    return slot_index < static_cast<int>(slots.size()) &&
           slots[static_cast<std::size_t>(slot_index)].occupied() &&
           slots[static_cast<std::size_t>(slot_index)].held_item_id > 0;
}

bool TransferSystemScreen::resortSlotHasHeldItem(int slot_index) const {
    if (slot_index < 0 || slot_index >= 30) {
        return false;
    }
    const int resort_box_index = resort_box_browser_.gameBoxIndex();
    if (resort_box_index < 0 || resort_box_index >= static_cast<int>(resort_pc_boxes_.size())) {
        return false;
    }
    const auto& slots = resort_pc_boxes_[static_cast<std::size_t>(resort_box_index)].slots;
    return slot_index < static_cast<int>(slots.size()) &&
           slots[static_cast<std::size_t>(slot_index)].occupied() &&
           slots[static_cast<std::size_t>(slot_index)].held_item_id > 0;
}

std::string TransferSystemScreen::gameSlotHeldItemName(int slot_index) const {
    if (!gameSlotHasHeldItem(slot_index)) {
        return {};
    }
    const int game_box_index = game_box_browser_.gameBoxIndex();
    const auto& slot = game_pc_boxes_[static_cast<std::size_t>(game_box_index)].slots[static_cast<std::size_t>(slot_index)];
    return !slot.held_item_name.empty() ? slot.held_item_name : ("Item " + std::to_string(slot.held_item_id));
}

BoxViewportModel TransferSystemScreen::resortBoxViewportModel() const {
    return resortBoxViewportModelAt(resort_box_browser_.gameBoxIndex());
}

void TransferSystemScreen::refreshResortBoxViewportModel() {
    if (!resort_box_viewport_) {
        return;
    }
    if (resort_box_browser_.gameBoxSpaceMode()) {
        const bool show_down = resortBoxSpaceMaxRowOffset() > 0;
        resort_box_viewport_->setHeaderMode(BoxViewport::HeaderMode::BoxSpace, show_down);
        resort_box_viewport_->snapContentToModel(
            resortBoxSpaceViewportModelAt(resort_box_browser_.gameBoxSpaceRowOffset()));
    } else {
        resort_box_viewport_->setModel(resortBoxViewportModelAt(resort_box_browser_.gameBoxIndex()));
    }
}

void TransferSystemScreen::refreshGameBoxViewportModel() {
    if (!game_save_box_viewport_ || game_pc_boxes_.empty()) {
        return;
    }
    if (game_box_browser_.gameBoxSpaceMode()) {
        game_save_box_viewport_->snapContentToModel(
            gameBoxSpaceViewportModelAt(game_box_browser_.gameBoxSpaceRowOffset()));
    } else {
        game_save_box_viewport_->setModel(gameBoxViewportModelAt(game_box_browser_.gameBoxIndex()));
    }
}

std::optional<transfer_system::PokemonMoveController::SlotRef> TransferSystemScreen::slotRefForFocus(FocusNodeId focus_id) const {
    using Move = transfer_system::PokemonMoveController;
    if (focus_id >= 1000 && focus_id <= 1029 && !resort_box_browser_.gameBoxSpaceMode()) {
        return Move::SlotRef{Move::Panel::Resort, resort_box_browser_.gameBoxIndex(), focus_id - 1000};
    }
    if (focus_id >= 2000 && focus_id <= 2029 && !game_box_browser_.gameBoxSpaceMode()) {
        return Move::SlotRef{Move::Panel::Game, game_box_browser_.gameBoxIndex(), focus_id - 2000};
    }
    return std::nullopt;
}

std::optional<transfer_system::PokemonMoveController::SlotRef> TransferSystemScreen::slotRefAtPointer(int logical_x, int logical_y) const {
    const std::optional<FocusNodeId> focus_id = focusNodeAtPointer(logical_x, logical_y);
    return focus_id ? slotRefForFocus(*focus_id) : std::nullopt;
}

bool TransferSystemScreen::pointerOverExpandedGameDropdown(int logical_x, int logical_y) const {
    if (!box_name_dropdown_style_.enabled || !game_box_browser_.dropdownOpenTarget() ||
        game_box_browser_.dropdownExpandT() <= 0.08f || static_cast<int>(game_pc_boxes_.size()) < 2) {
        return false;
    }
    SDL_Rect outer{};
    int list_h = 0;
    int list_clip_y = 0;
    if (!computeGameBoxDropdownOuterRect(outer, static_cast<float>(game_box_browser_.dropdownExpandT()), list_h, list_clip_y)) {
        return false;
    }
    return logical_x >= outer.x && logical_x < outer.x + outer.w && logical_y >= outer.y && logical_y < outer.y + outer.h;
}

bool TransferSystemScreen::pointerOverExpandedResortDropdown(int logical_x, int logical_y) const {
    if (!box_name_dropdown_style_.enabled || !resort_box_browser_.dropdownOpenTarget() ||
        resort_box_browser_.dropdownExpandT() <= 0.08f || static_cast<int>(resort_pc_boxes_.size()) < 2) {
        return false;
    }
    SDL_Rect outer{};
    int list_h = 0;
    int list_clip_y = 0;
    if (!computeResortBoxDropdownOuterRect(
            outer, static_cast<float>(resort_box_browser_.dropdownExpandT()), list_h, list_clip_y)) {
        return false;
    }
    return logical_x >= outer.x && logical_x < outer.x + outer.w && logical_y >= outer.y && logical_y < outer.y + outer.h;
}

std::optional<transfer_system::PokemonMoveController::SlotRef> TransferSystemScreen::multiPokemonAnchorSlotAtPointer(
    int logical_x,
    int logical_y) const {
    if (const auto ref = slotRefAtPointer(logical_x, logical_y)) {
        return ref;
    }
    return std::nullopt;
}

std::optional<transfer_system::PokemonMoveController::SlotRef> TransferSystemScreen::heldMultiPokemonAnchorSlot() const {
    if (!multi_pokemon_move_.active()) {
        return std::nullopt;
    }
    if (multi_pokemon_move_.inputMode() == transfer_system::MultiPokemonMoveController::InputMode::Pointer) {
        return multiPokemonAnchorSlotAtPointer(multi_pokemon_move_.pointer().x, multi_pokemon_move_.pointer().y);
    }
    return slotRefForFocus(focus_.current());
}

std::vector<transfer_system::PokemonMoveController::SlotRef> TransferSystemScreen::multiSlotRefsIntersectingRect(
    bool from_game,
    const SDL_Rect& rect) const {
    using Move = transfer_system::PokemonMoveController;
    std::vector<Move::SlotRef> refs;
    if (rect.w <= 0 || rect.h <= 0) {
        return refs;
    }
    auto intersects = [](const SDL_Rect& a, const SDL_Rect& b) {
        return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
    };

    SDL_Rect slot_bounds{};
    if (from_game) {
        if (!game_save_box_viewport_ || game_box_browser_.gameBoxSpaceMode()) {
            return refs;
        }
        const int box_index = game_box_browser_.gameBoxIndex();
        for (int i = 0; i < 30; ++i) {
            if (game_save_box_viewport_->getSlotBounds(i, slot_bounds) &&
                intersects(rect, slot_bounds) &&
                gameSaveSlotHasSpecies(i)) {
                refs.push_back(Move::SlotRef{Move::Panel::Game, box_index, i});
            }
        }
        return refs;
    }

    if (!resort_box_viewport_ || resort_box_browser_.gameBoxSpaceMode()) {
        return refs;
    }
    const int resort_box_index = resort_box_browser_.gameBoxIndex();
    for (int i = 0; i < 30; ++i) {
        if (resort_box_viewport_->getSlotBounds(i, slot_bounds) &&
            intersects(rect, slot_bounds) &&
            resortSlotHasSpecies(i)) {
            refs.push_back(Move::SlotRef{Move::Panel::Resort, resort_box_index, i});
        }
    }
    return refs;
}

std::vector<transfer_system::PokemonMoveController::SlotRef> TransferSystemScreen::keyboardMultiMarqueeOccupiedRefs()
    const {
    using Move = transfer_system::PokemonMoveController;
    std::vector<Move::SlotRef> refs;
    if (!keyboard_multi_marquee_active_) {
        return refs;
    }
    const int a = keyboard_multi_marquee_anchor_slot_;
    const int c = keyboard_multi_marquee_corner_slot_;
    const int r0 = std::min(a / 6, c / 6);
    const int r1 = std::max(a / 6, c / 6);
    const int col0 = std::min(a % 6, c % 6);
    const int col1 = std::max(a % 6, c % 6);

    if (keyboard_multi_marquee_from_game_) {
        const int box_index = game_box_browser_.gameBoxIndex();
        if (box_index < 0 || box_index >= static_cast<int>(game_pc_boxes_.size())) {
            return refs;
        }
        for (int r = r0; r <= r1; ++r) {
            for (int col = col0; col <= col1; ++col) {
                const int idx = r * 6 + col;
                if (idx >= 0 && idx < 30 && gameSaveSlotHasSpecies(idx)) {
                    refs.push_back(Move::SlotRef{Move::Panel::Game, box_index, idx});
                }
            }
        }
    } else {
        const int resort_box_index = resort_box_browser_.gameBoxIndex();
        for (int r = r0; r <= r1; ++r) {
            for (int col = col0; col <= col1; ++col) {
                const int idx = r * 6 + col;
                if (idx >= 0 && idx < 30 && resortSlotHasSpecies(idx)) {
                    refs.push_back(Move::SlotRef{Move::Panel::Resort, resort_box_index, idx});
                }
            }
        }
    }
    return refs;
}

SDL_Rect TransferSystemScreen::keyboardMultiMarqueeScreenRect() const {
    SDL_Rect out{0, 0, 0, 0};
    if (!keyboard_multi_marquee_active_) {
        return out;
    }
    const int a = keyboard_multi_marquee_anchor_slot_;
    const int c = keyboard_multi_marquee_corner_slot_;
    const int r0 = std::min(a / 6, c / 6);
    const int r1 = std::max(a / 6, c / 6);
    const int col0 = std::min(a % 6, c % 6);
    const int col1 = std::max(a % 6, c % 6);

    bool any = false;
    int min_x = 0;
    int min_y = 0;
    int max_x = 0;
    int max_y = 0;
    for (int r = r0; r <= r1; ++r) {
        for (int col = col0; col <= col1; ++col) {
            const int idx = r * 6 + col;
            SDL_Rect b{};
            const bool ok = keyboard_multi_marquee_from_game_
                ? (game_save_box_viewport_ && game_save_box_viewport_->getSlotBounds(idx, b))
                : (resort_box_viewport_ && resort_box_viewport_->getSlotBounds(idx, b));
            if (!ok) {
                continue;
            }
            if (!any) {
                min_x = b.x;
                min_y = b.y;
                max_x = b.x + b.w;
                max_y = b.y + b.h;
                any = true;
            } else {
                min_x = std::min(min_x, b.x);
                min_y = std::min(min_y, b.y);
                max_x = std::max(max_x, b.x + b.w);
                max_y = std::max(max_y, b.y + b.h);
            }
        }
    }
    if (!any) {
        return out;
    }
    out.x = min_x;
    out.y = min_y;
    out.w = std::max(0, max_x - min_x);
    out.h = std::max(0, max_y - min_y);
    return out;
}

PcSlotSpecies* TransferSystemScreen::mutablePokemonAt(const transfer_system::PokemonMoveController::SlotRef& ref) {
    using Move = transfer_system::PokemonMoveController;
    if (ref.slot_index < 0 || ref.slot_index >= 30) {
        return nullptr;
    }
    if (ref.panel == Move::Panel::Resort) {
        if (ref.box_index < 0 || ref.box_index >= static_cast<int>(resort_pc_boxes_.size())) {
            return nullptr;
        }
        auto& slots = resort_pc_boxes_[static_cast<std::size_t>(ref.box_index)].slots;
        if (ref.slot_index < 0 || ref.slot_index >= static_cast<int>(slots.size())) {
            return nullptr;
        }
        return &slots[static_cast<std::size_t>(ref.slot_index)];
    }
    if (ref.box_index < 0 || ref.box_index >= static_cast<int>(game_pc_boxes_.size())) {
        return nullptr;
    }
    auto& slots = game_pc_boxes_[static_cast<std::size_t>(ref.box_index)].slots;
    if (ref.slot_index >= static_cast<int>(slots.size())) {
        return nullptr;
    }
    return &slots[static_cast<std::size_t>(ref.slot_index)];
}

const PcSlotSpecies* TransferSystemScreen::pokemonAt(const transfer_system::PokemonMoveController::SlotRef& ref) const {
    return const_cast<TransferSystemScreen*>(this)->mutablePokemonAt(ref);
}

void TransferSystemScreen::clearPokemonAt(const transfer_system::PokemonMoveController::SlotRef& ref) {
    if (PcSlotSpecies* slot = mutablePokemonAt(ref)) {
        *slot = PcSlotSpecies{};
    }
}

void TransferSystemScreen::setPokemonAt(const transfer_system::PokemonMoveController::SlotRef& ref, PcSlotSpecies pokemon) {
    if (PcSlotSpecies* slot = mutablePokemonAt(ref)) {
        pokemon.present = true;
        pokemon.slot_index = ref.slot_index;
        pokemon.box_index = ref.box_index;
        pokemon.area = ref.panel == transfer_system::PokemonMoveController::Panel::Resort ? "resort" : "box";
        *slot = std::move(pokemon);
    }
}

bool TransferSystemScreen::beginPokemonMoveFromSlot(
    const transfer_system::PokemonMoveController::SlotRef& ref,
    transfer_system::PokemonMoveController::InputMode input_mode,
    transfer_system::PokemonMoveController::PickupSource source,
    SDL_Point pointer) {
    if (pokemon_move_.active() || multi_pokemon_move_.active()) {
        return false;
    }
    const PcSlotSpecies* slot = pokemonAt(ref);
    if (!slot || !slot->occupied()) {
        return false;
    }
    pokemon_move_.pickUp(*slot, ref, input_mode, source, pointer);
    clearPokemonAt(ref);
    refreshHeldMoveSpriteTexture();
    refreshResortBoxViewportModel();
    refreshGameBoxViewportModel();
    pokemon_action_menu_.clear();
    requestPickupSfx();
    return true;
}

bool TransferSystemScreen::dropHeldPokemonAt(const transfer_system::PokemonMoveController::SlotRef& target) {
    using Move = transfer_system::PokemonMoveController;
    Move::HeldPokemon* held = pokemon_move_.held();
    if (!held) {
        return false;
    }
    PcSlotSpecies* target_slot = mutablePokemonAt(target);
    if (!target_slot) {
        return false;
    }

    const bool target_occupied = target_slot->occupied();
    PcSlotSpecies held_pokemon = held->pokemon;
    const Move::SlotRef return_slot = held->return_slot;
    const std::string held_pkrid_snapshot = held_pokemon.resort_pkrid;
    const std::string target_pkrid_before = target_occupied ? target_slot->resort_pkrid : "";
    const bool swap_into_hand =
        held->source == Move::PickupSource::SwapTool
            ? pokemon_action_menu_style_.swap_tool_swaps_into_hand
            : pokemon_action_menu_style_.modal_move_swaps_into_hand;

    if (!target_occupied) {
        setPokemonAt(target, std::move(held_pokemon));
        pokemon_move_.clear();
        held_move_sprite_tex_ = {};
        requestPutdownSfx();
    } else if (swap_into_hand) {
        PcSlotSpecies target_pokemon = *target_slot;
        setPokemonAt(target, std::move(held_pokemon));
        pokemon_move_.swapHeldWith(target_pokemon, return_slot);
        requestPickupSfx();
    } else {
        if (target != return_slot) {
            const PcSlotSpecies* return_pokemon = pokemonAt(return_slot);
            if (return_pokemon && return_pokemon->occupied()) {
                // Keep both Pokemon safe: if the configured return slot is unexpectedly occupied,
                // fall back to hand-swap semantics rather than overwriting anything.
                PcSlotSpecies target_pokemon = *target_slot;
                setPokemonAt(target, std::move(held_pokemon));
                pokemon_move_.swapHeldWith(target_pokemon, return_slot);
                requestPickupSfx();
                refreshResortBoxViewportModel();
                refreshGameBoxViewportModel();
                refreshHeldMoveSpriteTexture();
                if (!game_box_browser_.gameBoxSpaceMode()) {
                    if (target.panel == Move::Panel::Game) {
                        focus_.setCurrent(2000 + target.slot_index);
                    } else {
                        focus_.setCurrent(1000 + target.slot_index);
                    }
                    selection_cursor_hidden_after_mouse_ = false;
                }
                return true;
            }
            setPokemonAt(return_slot, *target_slot);
        }
        setPokemonAt(target, std::move(held_pokemon));
        pokemon_move_.clear();
        held_move_sprite_tex_ = {};
        requestPutdownSfx();
    }

    persistResortPokemonDropToStorage(
        target,
        return_slot,
        target_occupied,
        swap_into_hand,
        held_pkrid_snapshot,
        target_pkrid_before);

    refreshResortBoxViewportModel();
    refreshGameBoxViewportModel();
    if (!pokemon_move_.active()) {
        held_move_sprite_tex_ = {};
    } else {
        refreshHeldMoveSpriteTexture();
    }

    if (!game_box_browser_.gameBoxSpaceMode()) {
        if (target.panel == Move::Panel::Game) {
            focus_.setCurrent(2000 + target.slot_index);
        } else {
            focus_.setCurrent(1000 + target.slot_index);
        }
        selection_cursor_hidden_after_mouse_ = false;
    }

    return true;
}

bool TransferSystemScreen::cancelHeldPokemonMove() {
    using Move = transfer_system::PokemonMoveController;
    Move::HeldPokemon* held = pokemon_move_.held();
    if (!held) {
        return false;
    }
    const Move::SlotRef return_slot = held->return_slot;
    const PcSlotSpecies* occupant = pokemonAt(return_slot);
    if (occupant && occupant->occupied()) {
        return false;
    }
    setPokemonAt(return_slot, held->pokemon);
    pokemon_move_.clear();
    held_move_sprite_tex_ = {};
    refreshResortBoxViewportModel();
    refreshGameBoxViewportModel();
    requestPutdownSfx();
    return true;
}

bool TransferSystemScreen::beginMultiPokemonMoveFromSlots(
    const std::vector<transfer_system::PokemonMoveController::SlotRef>& refs,
    transfer_system::MultiPokemonMoveController::InputMode input_mode,
    SDL_Point pointer) {
    using Move = transfer_system::PokemonMoveController;
    using Multi = transfer_system::MultiPokemonMoveController;
    if (refs.empty() || pokemon_move_.active() || multi_pokemon_move_.active() || held_move_.heldItem() || held_move_.heldBox()) {
        return false;
    }

    std::vector<Move::SlotRef> unique_refs;
    unique_refs.reserve(refs.size());
    for (const Move::SlotRef& ref : refs) {
        if (std::find(unique_refs.begin(), unique_refs.end(), ref) == unique_refs.end()) {
            unique_refs.push_back(ref);
        }
    }
    if (unique_refs.empty()) {
        return false;
    }

    int min_row = 99;
    int min_col = 99;
    constexpr int kCols = 6;
    for (const Move::SlotRef& ref : unique_refs) {
        const PcSlotSpecies* slot = pokemonAt(ref);
        if (!slot || !slot->occupied()) {
            return false;
        }
        min_row = std::min(min_row, ref.slot_index / kCols);
        min_col = std::min(min_col, ref.slot_index % kCols);
    }

    std::sort(unique_refs.begin(), unique_refs.end(), [](const Move::SlotRef& a, const Move::SlotRef& b) {
        if (a.panel != b.panel) return static_cast<int>(a.panel) < static_cast<int>(b.panel);
        if (a.box_index != b.box_index) return a.box_index < b.box_index;
        return a.slot_index < b.slot_index;
    });

    std::vector<Multi::Entry> entries;
    entries.reserve(unique_refs.size());
    for (const Move::SlotRef& ref : unique_refs) {
        const PcSlotSpecies* slot = pokemonAt(ref);
        if (!slot || !slot->occupied()) {
            return false;
        }
        Multi::Entry entry;
        entry.pokemon = *slot;
        entry.return_slot = ref;
        entry.row_offset = (ref.slot_index / kCols) - min_row;
        entry.col_offset = (ref.slot_index % kCols) - min_col;
        entries.push_back(std::move(entry));
    }

    for (const Move::SlotRef& ref : unique_refs) {
        clearPokemonAt(ref);
    }
    multi_pokemon_move_.pickUp(std::move(entries), input_mode, pointer);
    refreshResortBoxViewportModel();
    refreshGameBoxViewportModel();
    requestPickupSfx();
    return true;
}

bool TransferSystemScreen::dropHeldMultiPokemonAt(const transfer_system::PokemonMoveController::SlotRef& target) {
    if (!multi_pokemon_move_.active()) {
        return false;
    }
    const auto slots = multi_pokemon_move_.targetSlotsFor(target);
    if (!slots || slots->size() != multi_pokemon_move_.entries().size()) {
        return false;
    }

    for (const auto& ref : *slots) {
        const PcSlotSpecies* dst = pokemonAt(ref);
        if (!dst || dst->occupied()) {
            return false;
        }
    }

    const auto entries = multi_pokemon_move_.entries();
    for (std::size_t i = 0; i < entries.size(); ++i) {
        setPokemonAt((*slots)[i], entries[i].pokemon);
    }
    multi_pokemon_move_.clear();
    refreshResortBoxViewportModel();
    refreshGameBoxViewportModel();
    requestPutdownSfx();

    if (!game_box_browser_.gameBoxSpaceMode()) {
        const auto& first = slots->front();
        focus_.setCurrent((first.panel == transfer_system::PokemonMoveController::Panel::Game ? 2000 : 1000) + first.slot_index);
        selection_cursor_hidden_after_mouse_ = false;
    }
    return true;
}

bool TransferSystemScreen::cancelHeldMultiPokemonMove() {
    if (!multi_pokemon_move_.active()) {
        return false;
    }
    for (const auto& entry : multi_pokemon_move_.entries()) {
        const PcSlotSpecies* dst = pokemonAt(entry.return_slot);
        if (!dst || dst->occupied()) {
            return false;
        }
    }
    const auto entries = multi_pokemon_move_.entries();
    for (const auto& entry : entries) {
        setPokemonAt(entry.return_slot, entry.pokemon);
    }
    multi_pokemon_move_.clear();
    refreshResortBoxViewportModel();
    refreshGameBoxViewportModel();
    requestPutdownSfx();
    return true;
}

bool TransferSystemScreen::gameBoxHasEmptySlots(int box_index, int required_count) const {
    if (required_count <= 0) {
        return true;
    }
    if (box_index < 0 || box_index >= static_cast<int>(game_pc_boxes_.size())) {
        return false;
    }
    const auto& slots = game_pc_boxes_[static_cast<std::size_t>(box_index)].slots;
    int empty_count = 0;
    for (const PcSlotSpecies& slot : slots) {
        if (!slot.occupied() && ++empty_count >= required_count) {
            return true;
        }
    }
    return false;
}

bool TransferSystemScreen::resortBoxHasEmptySlots(int box_index, int required_count) const {
    if (required_count <= 0) {
        return true;
    }
    if (box_index < 0 || box_index >= static_cast<int>(resort_pc_boxes_.size())) {
        return false;
    }
    const auto& slots = resort_pc_boxes_[static_cast<std::size_t>(box_index)].slots;
    int empty_count = 0;
    for (const PcSlotSpecies& slot : slots) {
        if (!slot.occupied() && ++empty_count >= required_count) {
            return true;
        }
    }
    return false;
}

bool TransferSystemScreen::dropHeldMultiPokemonIntoFirstEmptyResortBox(int box_index) {
    if (!multi_pokemon_move_.active() || !resortBoxHasEmptySlots(box_index, multi_pokemon_move_.count())) {
        return false;
    }
    std::vector<transfer_system::PokemonMoveController::SlotRef> targets;
    targets.reserve(static_cast<std::size_t>(multi_pokemon_move_.count()));
    const auto& slots = resort_pc_boxes_[static_cast<std::size_t>(box_index)].slots;
    for (int i = 0; i < static_cast<int>(slots.size()) && static_cast<int>(targets.size()) < multi_pokemon_move_.count(); ++i) {
        if (!slots[static_cast<std::size_t>(i)].occupied()) {
            targets.push_back(transfer_system::PokemonMoveController::SlotRef{
                transfer_system::PokemonMoveController::Panel::Resort,
                box_index,
                i});
        }
    }
    if (static_cast<int>(targets.size()) != multi_pokemon_move_.count()) {
        return false;
    }
    const auto entries = multi_pokemon_move_.entries();
    for (std::size_t i = 0; i < entries.size(); ++i) {
        setPokemonAt(targets[i], entries[i].pokemon);
    }
    multi_pokemon_move_.clear();
    refreshGameBoxViewportModel();
    refreshResortBoxViewportModel();
    requestPutdownSfx();
    return true;
}

bool TransferSystemScreen::dropHeldMultiPokemonIntoFirstEmptySlotsInBox(int box_index) {
    if (!multi_pokemon_move_.active() || !gameBoxHasEmptySlots(box_index, multi_pokemon_move_.count())) {
        return false;
    }
    std::vector<transfer_system::PokemonMoveController::SlotRef> targets;
    targets.reserve(static_cast<std::size_t>(multi_pokemon_move_.count()));
    const auto& slots = game_pc_boxes_[static_cast<std::size_t>(box_index)].slots;
    for (int i = 0; i < static_cast<int>(slots.size()) && static_cast<int>(targets.size()) < multi_pokemon_move_.count(); ++i) {
        if (!slots[static_cast<std::size_t>(i)].occupied()) {
            targets.push_back(transfer_system::PokemonMoveController::SlotRef{
                transfer_system::PokemonMoveController::Panel::Game,
                box_index,
                i});
        }
    }
    if (static_cast<int>(targets.size()) != multi_pokemon_move_.count()) {
        return false;
    }
    const auto entries = multi_pokemon_move_.entries();
    for (std::size_t i = 0; i < entries.size(); ++i) {
        setPokemonAt(targets[i], entries[i].pokemon);
    }
    multi_pokemon_move_.clear();
    refreshGameBoxViewportModel();
    refreshResortBoxViewportModel();
    requestPutdownSfx();
    return true;
}

void TransferSystemScreen::refreshHeldMoveSpriteTexture() {
    if (!pokemon_move_.active() || !sprite_assets_ || !renderer_) {
        held_move_sprite_tex_ = {};
        return;
    }
    if (const auto* h = pokemon_move_.held()) {
        held_move_sprite_tex_ = sprite_assets_->loadPokemonTexture(renderer_, h->pokemon);
    }
}

void TransferSystemScreen::cycleToolCarousel(int dir) {
    if (pokemon_move_.active() || multi_pokemon_move_.active()) {
        return;
    }
    closePokemonActionMenu();
    ui_state_.cycleToolCarousel(dir, carousel_style_);
}

bool TransferSystemScreen::hitTestToolCarousel(int logical_x, int logical_y) const {
    const int vx = carousel_style_.offset_from_left_wall +
        (exit_button_enabled_ ? (carousel_style_.viewport_height + exit_button_gap_pixels_) : 0);
    const int vy = carouselScreenY();
    const int vw = carousel_style_.viewport_width;
    const int vh = carousel_style_.viewport_height;
    return logical_x >= vx && logical_x < vx + vw && logical_y >= vy && logical_y < vy + vh;
}

std::optional<FocusNodeId> TransferSystemScreen::focusNodeAtPointer(int logical_x, int logical_y) const {
    auto in = [](int px, int py, const SDL_Rect& r) {
        return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
    };

    if (exit_button_enabled_) {
        const int bs = carousel_style_.viewport_height;
        const int bx = carousel_style_.offset_from_left_wall;
        const int by = exitButtonScreenY();
        if (bs > 0 && logical_x >= bx && logical_x < bx + bs && logical_y >= by && logical_y < by + bs) {
            return 5000;
        }
    }
    if (ui_state_.panelsReveal() > 0.02 && hitTestToolCarousel(logical_x, logical_y)) {
        return 3000;
    }
    if (hitTestPillTrack(logical_x, logical_y)) {
        return 4000;
    }

    SDL_Rect r{};
    if (game_save_box_viewport_) {
        if (pointerOverExpandedGameDropdown(logical_x, logical_y)) {
            return 2102;
        }
        if (game_save_box_viewport_->getPrevArrowBounds(r) && in(logical_x, logical_y, r)) {
            return 2101;
        }
        if (game_save_box_viewport_->getNextArrowBounds(r) && in(logical_x, logical_y, r)) {
            return 2103;
        }
        if (game_save_box_viewport_->getBoxSpaceScrollArrowBounds(r) && in(logical_x, logical_y, r)) {
            return 2112;
        }
        if (game_save_box_viewport_->getNamePlateBounds(r) && in(logical_x, logical_y, r)) {
            return 2102;
        }
        for (int i = 0; i < 30; ++i) {
            if (game_save_box_viewport_->getSlotBounds(i, r) && in(logical_x, logical_y, r)) {
                return 2000 + i;
            }
        }
        if (game_save_box_viewport_->getFooterBoxSpaceBounds(r) && in(logical_x, logical_y, r)) {
            return 2110;
        }
        if (game_save_box_viewport_->getFooterGameIconBounds(r) && in(logical_x, logical_y, r)) {
            return 2111;
        }
    }

    if (resort_box_viewport_) {
        if (pointerOverExpandedResortDropdown(logical_x, logical_y)) {
            return 1102;
        }
        if (resort_box_viewport_->getPrevArrowBounds(r) && in(logical_x, logical_y, r)) {
            return 1101;
        }
        if (resort_box_viewport_->getNextArrowBounds(r) && in(logical_x, logical_y, r)) {
            return 1103;
        }
        if (resort_box_viewport_->getNamePlateBounds(r) && in(logical_x, logical_y, r)) {
            return 1102;
        }
        for (int i = 0; i < 30; ++i) {
            if (resort_box_viewport_->getSlotBounds(i, r) && in(logical_x, logical_y, r)) {
                return 1000 + i;
            }
        }
        if (resort_box_viewport_->getFooterBoxSpaceBounds(r) && in(logical_x, logical_y, r)) {
            return 1110;
        }
        if (resort_box_viewport_->getFooterGameIconBounds(r) && in(logical_x, logical_y, r)) {
            return 1111;
        }
        if (resort_box_viewport_->getResortScrollArrowBounds(r) && in(logical_x, logical_y, r)) {
            return 1112;
        }
    }

    return std::nullopt;
}

bool TransferSystemScreen::gameSaveSlotHasSpecies(int slot_index) const {
    if (!game_save_box_viewport_ || slot_index < 0 || slot_index >= 30) {
        return false;
    }
    const int game_box_index = game_box_browser_.gameBoxIndex();
    if (game_box_index < 0 || game_box_index >= static_cast<int>(game_pc_boxes_.size())) {
        return false;
    }
    const auto& slots = game_pc_boxes_[static_cast<std::size_t>(game_box_index)].slots;
    if (slot_index >= static_cast<int>(slots.size())) {
        return false;
    }
    return slots[static_cast<std::size_t>(slot_index)].occupied();
}

bool TransferSystemScreen::resortSlotHasSpecies(int slot_index) const {
    if (!resort_box_viewport_ || slot_index < 0 || slot_index >= 30) {
        return false;
    }
    const int bi = resort_box_browser_.gameBoxIndex();
    if (bi < 0 || bi >= static_cast<int>(resort_pc_boxes_.size())) {
        return false;
    }
    const auto& slots = resort_pc_boxes_[static_cast<std::size_t>(bi)].slots;
    if (slot_index >= static_cast<int>(slots.size())) {
        return false;
    }
    return slots[static_cast<std::size_t>(slot_index)].occupied();
}

std::optional<std::pair<FocusNodeId, SDL_Rect>> TransferSystemScreen::speechBubbleTargetAtPointer(
    int logical_x,
    int logical_y) const {
    auto in = [](int px, int py, const SDL_Rect& r) {
        return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
    };
    SDL_Rect r{};

    if (game_save_box_viewport_) {
        for (int i = 0; i < 30; ++i) {
            if (game_save_box_viewport_->getSlotBounds(i, r) && in(logical_x, logical_y, r)) {
                if (game_box_browser_.gameBoxSpaceMode()) {
                    const int box_index = game_box_browser_.gameBoxSpaceRowOffset() * 6 + i;
                    if (box_index < 0 || box_index >= static_cast<int>(game_pc_boxes_.size())) {
                        return std::nullopt;
                    }
                } else if (itemToolActive()) {
                    if (!gameSlotHasHeldItem(i)) {
                        return std::nullopt;
                    }
                } else if (!gameSaveSlotHasSpecies(i)) {
                    return std::nullopt;
                }
                return std::make_pair(2000 + i, r);
            }
        }
        if (game_save_box_viewport_->getFooterGameIconBounds(r) && in(logical_x, logical_y, r)) {
            return std::make_pair(2111, r);
        }
    }
    if (resort_box_viewport_) {
        for (int i = 0; i < 30; ++i) {
            if (resort_box_viewport_->getSlotBounds(i, r) && in(logical_x, logical_y, r)) {
                if (resort_box_browser_.gameBoxSpaceMode()) {
                    const int box_index = resort_box_browser_.gameBoxSpaceRowOffset() * 6 + i;
                    if (box_index < 0 || box_index >= static_cast<int>(resort_pc_boxes_.size())) {
                        return std::nullopt;
                    }
                    return std::make_pair(1000 + i, r);
                }
                if (itemToolActive()) {
                    if (!resortSlotHasHeldItem(i)) {
                        return std::nullopt;
                    }
                } else if (!resortSlotHasSpecies(i)) {
                    return std::nullopt;
                }
                return std::make_pair(1000 + i, r);
            }
        }
        if (resort_box_viewport_->getFooterGameIconBounds(r) && in(logical_x, logical_y, r)) {
            return std::make_pair(1111, r);
        }
    }
    return std::nullopt;
}

void TransferSystemScreen::onAdvancePressed() {
    selection_cursor_hidden_after_mouse_ = false;
    if (box_rename_modal_open_) {
        syncBoxRenameModalLayout();
        if (box_rename_editing_) {
            box_rename_editing_ = false;
            SDL_StopTextInput();
            box_rename_focus_slot_ = BoxRenameFocusSlot::Confirm;
            ui_state_.requestButtonSfx();
            return;
        }
        switch (box_rename_focus_slot_) {
            case BoxRenameFocusSlot::Field:
                box_rename_editing_ = true;
                SDL_StartTextInput();
                SDL_SetTextInputRect(&box_rename_text_field_rect_virt_);
                break;
            case BoxRenameFocusSlot::Confirm:
                closeBoxRenameModal(true);
                break;
            case BoxRenameFocusSlot::Cancel:
                closeBoxRenameModal(false);
                break;
        }
        ui_state_.requestButtonSfx();
        return;
    }
    if (held_move_.heldBox() && !pokemon_move_.active()) {
        if (dropdownAcceptsNavigation()) {
            // While holding a Box Space box, Accept should still confirm dropdown choices.
            applyActiveDropdownSelection();
            return;
        }
        const auto* hb = held_move_.heldBox();
        const int from = hb->source_box_index;
        const auto src_panel = hb->source_panel;
        const auto game_tgt = focusedBoxSpaceBoxIndex();
        const auto resort_tgt = focusedResortBoxSpaceBoxIndex();

        if (src_panel == transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Game) {
            if (game_tgt.has_value()) {
                held_move_.clear();
                (void)swapGamePcBoxes(from, *game_tgt);
                refreshGameBoxViewportModel();
                refreshResortBoxViewportModel();
                requestPutdownSfx();
                return;
            }
            if (resort_tgt.has_value()) {
                held_move_.clear();
                (void)swapGameAndResortPcBoxes(from, *resort_tgt);
                refreshGameBoxViewportModel();
                refreshResortBoxViewportModel();
                requestPutdownSfx();
                return;
            }
        } else {
            if (resort_tgt.has_value()) {
                held_move_.clear();
                (void)swapResortPcBoxes(from, *resort_tgt);
                refreshGameBoxViewportModel();
                refreshResortBoxViewportModel();
                requestPutdownSfx();
                return;
            }
            if (game_tgt.has_value()) {
                held_move_.clear();
                (void)swapGameAndResortPcBoxes(*game_tgt, from);
                refreshGameBoxViewportModel();
                refreshResortBoxViewportModel();
                requestPutdownSfx();
                return;
            }
        }
    }
    if (multi_pokemon_move_.active()) {
        if (dropdownAcceptsNavigation()) {
            applyActiveDropdownSelection();
            return;
        }
        if (game_box_browser_.gameBoxSpaceMode()) {
            const FocusNodeId cur = focus_.current();
            if (cur >= 2000 && cur <= 2029) {
                const int box_index =
                    game_box_browser_.gameBoxSpaceRowOffset() * 6 + (cur - 2000);
                if (box_index >= 0 && box_index < static_cast<int>(game_pc_boxes_.size())) {
                    if (!dropHeldMultiPokemonIntoFirstEmptySlotsInBox(box_index)) {
                        triggerHeldSpriteRejectFeedback();
                    }
                    return;
                }
            }
            if (activateFocusedGameSlot()) {
                return;
            }
        } else if (resort_box_browser_.gameBoxSpaceMode()) {
            const FocusNodeId cur = focus_.current();
            if (cur >= 1000 && cur <= 1029) {
                const int box_index =
                    resort_box_browser_.gameBoxSpaceRowOffset() * 6 + (cur - 1000);
                if (box_index >= 0 && box_index < static_cast<int>(resort_pc_boxes_.size())) {
                    if (!dropHeldMultiPokemonIntoFirstEmptyResortBox(box_index)) {
                        triggerHeldSpriteRejectFeedback();
                    }
                    return;
                }
            }
            if (activateFocusedResortSlot()) {
                return;
            }
        } else if (const auto target = slotRefForFocus(focus_.current())) {
            if (!dropHeldMultiPokemonAt(*target)) {
                triggerHeldSpriteRejectFeedback();
            }
            return;
        }
        if (focus_.current() == 2101) {
            advanceGameBox(-1);
            return;
        }
        if (focus_.current() == 2103) {
            advanceGameBox(1);
            return;
        }
        if (box_name_dropdown_style_.enabled && !game_box_browser_.dropdownOpenTarget() && focus_.current() == 2102 &&
            game_pc_boxes_.size() >= 2) {
            toggleGameBoxDropdown();
            return;
        }
        if (focus_.current() == 2110) {
            setGameBoxSpaceMode(!game_box_browser_.gameBoxSpaceMode());
            closeGameBoxDropdown();
            return;
        }
        if (focus_.current() == 1101) {
            advanceResortBox(-1);
            return;
        }
        if (focus_.current() == 1103) {
            advanceResortBox(1);
            return;
        }
        if (box_name_dropdown_style_.enabled && !resort_box_browser_.dropdownOpenTarget() && focus_.current() == 1102 &&
            resort_pc_boxes_.size() >= 2) {
            toggleResortBoxDropdown();
            return;
        }
        if (focus_.current() == 1110) {
            setResortBoxSpaceMode(!resort_box_browser_.gameBoxSpaceMode());
            closeResortBoxDropdown();
            return;
        }
        return;
    }
    if (pokemon_move_.active()) {
        if (dropdownAcceptsNavigation()) {
            applyActiveDropdownSelection();
            return;
        }
        if (const auto target = slotRefForFocus(focus_.current())) {
            (void)dropHeldPokemonAt(*target);
            return;
        }
        if (game_box_browser_.gameBoxSpaceMode()) {
            if (activateFocusedGameSlot()) {
                return;
            }
        }
        if (resort_box_browser_.gameBoxSpaceMode()) {
            if (activateFocusedResortSlot()) {
                return;
            }
        }
        if (focus_.current() == 2101) {
            advanceGameBox(-1);
            return;
        }
        if (focus_.current() == 2103) {
            advanceGameBox(1);
            return;
        }
        if (box_name_dropdown_style_.enabled && !game_box_browser_.dropdownOpenTarget() && focus_.current() == 2102 &&
            game_pc_boxes_.size() >= 2) {
            toggleGameBoxDropdown();
            return;
        }
        if (focus_.current() == 2110) {
            setGameBoxSpaceMode(!game_box_browser_.gameBoxSpaceMode());
            closeGameBoxDropdown();
            return;
        }
        if (focus_.current() == 1101) {
            advanceResortBox(-1);
            return;
        }
        if (focus_.current() == 1103) {
            advanceResortBox(1);
            return;
        }
        if (box_name_dropdown_style_.enabled && !resort_box_browser_.dropdownOpenTarget() && focus_.current() == 1102 &&
            resort_pc_boxes_.size() >= 2) {
            toggleResortBoxDropdown();
            return;
        }
        if (focus_.current() == 1110) {
            setResortBoxSpaceMode(!resort_box_browser_.gameBoxSpaceMode());
            closeResortBoxDropdown();
            return;
        }
        return;
    }
    if (held_move_.heldItem()) {
        if (dropdownAcceptsNavigation()) {
            applyActiveDropdownSelection();
            return;
        }
        // Holding an item: drop onto a focused Pokemon slot if it has no held item.
        if (const auto target = slotRefForFocus(focus_.current())) {
            using Move = transfer_system::PokemonMoveController;
            const int slot = target->slot_index;
            const int box_index = target->box_index;
            const bool in_game = target->panel == Move::Panel::Game;
            PcSlotSpecies* dst = mutablePokemonAt(transfer_system::PokemonMoveController::SlotRef{
                in_game ? Move::Panel::Game : Move::Panel::Resort, box_index, slot});
            if (dst && dst->occupied()) {
                const auto* held = held_move_.heldItem();
                if (dst->held_item_id > 0) {
                    // Swap: target item becomes the new held item and returns to this slot on cancel.
                    const int next_item_id = dst->held_item_id;
                    std::string next_item_name = dst->held_item_name;
                    dst->held_item_id = held->item_id;
                    dst->held_item_name = held->item_name;
                    held_move_.swapHeldItemWith(
                        next_item_id,
                        std::move(next_item_name),
                        transfer_system::move::HeldMoveController::PokemonSlotRef{
                            in_game ? transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Game
                                    : transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Resort,
                            box_index,
                            slot});
                    refreshResortBoxViewportModel();
                    refreshGameBoxViewportModel();
                    requestPickupSfx();
                    return;
                }
                if (dst->held_item_id <= 0) {
                    dst->held_item_id = held->item_id;
                    dst->held_item_name = held->item_name;
                    held_move_.clear();
                    refreshResortBoxViewportModel();
                    refreshGameBoxViewportModel();
                    requestPutdownSfx();
                    return;
                }
            }
        }
        // Allow box navigation / Box Space open behavior even while holding an item.
        if (game_box_browser_.gameBoxSpaceMode()) {
            if (activateFocusedGameSlot()) {
                return;
            }
        }
        if (resort_box_browser_.gameBoxSpaceMode()) {
            if (activateFocusedResortSlot()) {
                return;
            }
        }
        if (focus_.current() == 2101) {
            advanceGameBox(-1);
            return;
        }
        if (focus_.current() == 2103) {
            advanceGameBox(1);
            return;
        }
        if (box_name_dropdown_style_.enabled && !game_box_browser_.dropdownOpenTarget() && focus_.current() == 2102 &&
            game_pc_boxes_.size() >= 2) {
            toggleGameBoxDropdown();
            return;
        }
        if (focus_.current() == 2110) {
            setGameBoxSpaceMode(!game_box_browser_.gameBoxSpaceMode());
            closeGameBoxDropdown();
            return;
        }
        if (focus_.current() == 1101) {
            advanceResortBox(-1);
            return;
        }
        if (focus_.current() == 1103) {
            advanceResortBox(1);
            return;
        }
        if (box_name_dropdown_style_.enabled && !resort_box_browser_.dropdownOpenTarget() && focus_.current() == 1102 &&
            resort_pc_boxes_.size() >= 2) {
            toggleResortBoxDropdown();
            return;
        }
        if (focus_.current() == 1110) {
            setResortBoxSpaceMode(!resort_box_browser_.gameBoxSpaceMode());
            closeResortBoxDropdown();
            return;
        }
        return;
    }
    if (pokemon_action_menu_.visible()) {
        activatePokemonActionMenuRow(pokemon_action_menu_.selectedRow());
        return;
    }
    if (item_action_menu_.visible()) {
        ui_state_.requestButtonSfx();
        const auto action = item_action_menu_.actionForRow(item_action_menu_.selectedRow());
        if (action == transfer_system::ItemActionMenuController::Action::MoveItem) {
            using Move = transfer_system::PokemonMoveController;
            const Move::SlotRef ref{
                item_action_menu_.fromGameBox() ? Move::Panel::Game : Move::Panel::Resort,
                item_action_menu_.fromGameBox() ? game_box_browser_.gameBoxIndex() : resort_box_browser_.gameBoxIndex(),
                item_action_menu_.slotIndex()};
            PcSlotSpecies* src = mutablePokemonAt(ref);
            if (src && src->occupied() && src->held_item_id > 0) {
                held_move_.pickUpItem(
                    src->held_item_id,
                    src->held_item_name,
                    transfer_system::move::HeldMoveController::PokemonSlotRef{
                        item_action_menu_.fromGameBox()
                            ? transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Game
                            : transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Resort,
                        item_action_menu_.fromGameBox() ? game_box_browser_.gameBoxIndex() : resort_box_browser_.gameBoxIndex(),
                        item_action_menu_.slotIndex()},
                    transfer_system::move::HeldMoveController::InputMode::Keyboard,
                    last_pointer_position_);
                src->held_item_id = -1;
                src->held_item_name.clear();
                refreshResortBoxViewportModel();
                refreshGameBoxViewportModel();
                requestPickupSfx();
            }
            item_action_menu_.close();
        } else if (action == transfer_system::ItemActionMenuController::Action::PutAway) {
            item_action_menu_.goToPutAwayPage();
        } else if (action == transfer_system::ItemActionMenuController::Action::Back) {
            item_action_menu_.goToRootPage();
        } else if (
            action == transfer_system::ItemActionMenuController::Action::PutAwayResort ||
            action == transfer_system::ItemActionMenuController::Action::PutAwayGame) {
            // Not implemented yet: bag/storage destinations.
            item_action_menu_.close();
        } else {
            item_action_menu_.close();
        }
        return;
    }
    if (dropdownAcceptsNavigation()) {
        applyActiveDropdownSelection();
        return;
    }
    if (multiPokemonToolActive()) {
        if (keyboard_multi_marquee_active_) {
            const auto refs = keyboardMultiMarqueeOccupiedRefs();
            if (!refs.empty()) {
                if (beginMultiPokemonMoveFromSlots(
                        refs,
                        transfer_system::MultiPokemonMoveController::InputMode::Keyboard,
                        last_pointer_position_)) {
                    keyboard_multi_marquee_active_ = false;
                    return;
                }
            }
            ui_state_.requestErrorSfx();
            keyboard_multi_marquee_active_ = false;
            return;
        }
        if (const auto ref = slotRefForFocus(focus_.current())) {
            const bool game_slot =
                ref->panel == transfer_system::PokemonMoveController::Panel::Game &&
                gameSaveSlotHasSpecies(ref->slot_index);
            const bool resort_slot =
                ref->panel == transfer_system::PokemonMoveController::Panel::Resort &&
                resortSlotHasSpecies(ref->slot_index);
            if (game_slot || resort_slot) {
                keyboard_multi_marquee_active_ = true;
                keyboard_multi_marquee_from_game_ =
                    ref->panel == transfer_system::PokemonMoveController::Panel::Game;
                keyboard_multi_marquee_anchor_slot_ = ref->slot_index;
                keyboard_multi_marquee_corner_slot_ = ref->slot_index;
                ui_state_.requestButtonSfx();
                return;
            }
        }
    }
    if (swapToolActive()) {
        if (const auto ref = slotRefForFocus(focus_.current())) {
            if (beginPokemonMoveFromSlot(
                    *ref,
                    transfer_system::PokemonMoveController::InputMode::Keyboard,
                    transfer_system::PokemonMoveController::PickupSource::SwapTool,
                    last_pointer_position_)) {
                return;
            }
        }
    }
    if (activateFocusedPokemonSlotActionMenu()) {
        return;
    }
    // Yellow tool: allow opening an item modal on slots with held items.
    if (itemToolActive()) {
        const FocusNodeId cur = focus_.current();
        SDL_Rect r{};
        if (cur >= 2000 && cur <= 2029 && game_save_box_viewport_) {
            if (game_box_browser_.gameBoxSpaceMode()) {
                return;
            }
            const int slot = cur - 2000;
            if (gameSlotHasHeldItem(slot) && game_save_box_viewport_->getSlotBounds(slot, r)) {
                item_action_menu_.setPutAwayGameLabel(selection_game_title_);
                item_action_menu_.open(true, slot, r);
                ui_state_.requestButtonSfx();
                return;
            }
        }
        if (cur >= 1000 && cur <= 1029 && resort_box_viewport_) {
            if (resort_box_browser_.gameBoxSpaceMode()) {
                return;
            }
            const int slot = cur - 1000;
            SDL_Rect rr{};
            const int rbi = resort_box_browser_.gameBoxIndex();
            if (rbi >= 0 && rbi < static_cast<int>(resort_pc_boxes_.size()) &&
                resort_box_viewport_->getSlotBounds(slot, rr)) {
                const auto& rs = resort_pc_boxes_[static_cast<std::size_t>(rbi)].slots;
                if (slot >= 0 && slot < static_cast<int>(rs.size()) && rs[static_cast<std::size_t>(slot)].occupied() &&
                    rs[static_cast<std::size_t>(slot)].held_item_id > 0) {
                    item_action_menu_.setPutAwayGameLabel(selection_game_title_);
                    item_action_menu_.open(false, slot, rr);
                    ui_state_.requestButtonSfx();
                    return;
                }
            }
        }
    }
    if (activateFocusedGameSlot()) {
        return;
    }
    if (activateFocusedResortSlot()) {
        return;
    }
    if (box_name_dropdown_style_.enabled && !game_box_browser_.dropdownOpenTarget() && focus_.current() == 2102 &&
        game_pc_boxes_.size() >= 2) {
        toggleGameBoxDropdown();
        return;
    }
    if (box_name_dropdown_style_.enabled && !resort_box_browser_.dropdownOpenTarget() && focus_.current() == 1102 &&
        resort_pc_boxes_.size() >= 2) {
        toggleResortBoxDropdown();
        return;
    }
    focus_.activate();
}

bool TransferSystemScreen::captureAdvanceForLongPress() const {
    const bool game_focus =
        game_box_browser_.gameBoxSpaceMode() && focusedBoxSpaceBoxIndex().has_value();
    const bool resort_focus =
        resort_box_browser_.gameBoxSpaceMode() && focusedResortBoxSpaceBoxIndex().has_value();
    if (!game_focus && !resort_focus) {
        return false;
    }
    if (pokemon_move_.active() || multi_pokemon_move_.active()) {
        return box_space_long_press_style_.quick_drop_hold_seconds > 1e-6;
    }
    return box_space_long_press_style_.box_swap_hold_seconds > 1e-6;
}

std::optional<double> TransferSystemScreen::advanceLongPressSeconds() const {
    const bool game_focus =
        game_box_browser_.gameBoxSpaceMode() && focusedBoxSpaceBoxIndex().has_value();
    const bool resort_focus =
        resort_box_browser_.gameBoxSpaceMode() && focusedResortBoxSpaceBoxIndex().has_value();
    if (!game_focus && !resort_focus) {
        return std::nullopt;
    }
    return (pokemon_move_.active() || multi_pokemon_move_.active())
        ? std::optional<double>(box_space_long_press_style_.quick_drop_hold_seconds)
        : std::optional<double>(box_space_long_press_style_.box_swap_hold_seconds);
}

void TransferSystemScreen::onAdvanceLongPress() {
    std::optional<int> box_index;
    bool resort_panel = false;
    if (resort_box_browser_.gameBoxSpaceMode()) {
        box_index = focusedResortBoxSpaceBoxIndex();
        resort_panel = box_index.has_value();
    }
    if (!box_index.has_value() && game_box_browser_.gameBoxSpaceMode()) {
        box_index = focusedBoxSpaceBoxIndex();
        resort_panel = false;
    }
    if (!box_index.has_value()) {
        return;
    }
    box_space_interaction_panel_ =
        resort_panel ? BoxSpaceInteractionPanel::Resort : BoxSpaceInteractionPanel::Game;
    if (pokemon_move_.active() || multi_pokemon_move_.active()) {
        if (!completeBoxSpaceQuickDrop(*box_index)) {
            triggerHeldSpriteRejectFeedback();
        }
        clearBoxSpaceQuickDropGesture();
        return;
    }
    held_move_.pickUpBox(
        resort_panel ? transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Resort
                     : transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Game,
        *box_index,
        transfer_system::move::HeldMoveController::InputMode::Keyboard,
        last_pointer_position_);
    refreshGameBoxViewportModel();
    refreshResortBoxViewportModel();
    requestPickupSfx();
    clearBoxSpaceQuickDropGesture();
}

void TransferSystemScreen::applyKeyboardBoxSpaceQuickDropCharge(double elapsed_seconds, KeyboardBoxSpaceChargeKind source) {
    if (!pokemon_move_.active() && !multi_pokemon_move_.active()) {
        return;
    }
    if (!game_box_browser_.gameBoxSpaceMode() && !resort_box_browser_.gameBoxSpaceMode()) {
        return;
    }
    const FocusNodeId cur = focus_.current();

    if (cur >= 1000 && cur <= 1029 && resort_box_browser_.gameBoxSpaceMode() && resort_box_viewport_) {
        const auto tgt = focusedResortBoxSpaceBoxIndex();
        if (!tgt.has_value()) {
            return;
        }
        box_space_quick_drop_kind_ =
            multi_pokemon_move_.active() ? BoxSpaceQuickDropKind::PokemonMulti : BoxSpaceQuickDropKind::PokemonSingle;
        box_space_keyboard_charge_kind_ = source;
        box_space_quick_drop_pending_ = true;
        box_space_quick_drop_elapsed_seconds_ = elapsed_seconds;
        box_space_interaction_panel_ = BoxSpaceInteractionPanel::Resort;
        box_space_pressed_cell_ = cur - 1000;
        box_space_quick_drop_target_box_index_ = *tgt;
        SDL_Rect cell_bounds{};
        if (resort_box_viewport_->getSlotBounds(box_space_pressed_cell_, cell_bounds)) {
            box_space_quick_drop_start_cell_bounds_ = cell_bounds;
            box_space_quick_drop_start_pointer_ =
                SDL_Point{cell_bounds.x + cell_bounds.w / 2, cell_bounds.y + cell_bounds.h / 2};
        }
        return;
    }
    if (cur >= 2000 && cur <= 2029 && game_box_browser_.gameBoxSpaceMode() && game_save_box_viewport_) {
        const auto tgt = focusedBoxSpaceBoxIndex();
        if (!tgt.has_value()) {
            return;
        }
        box_space_quick_drop_kind_ =
            multi_pokemon_move_.active() ? BoxSpaceQuickDropKind::PokemonMulti : BoxSpaceQuickDropKind::PokemonSingle;
        box_space_keyboard_charge_kind_ = source;
        box_space_quick_drop_pending_ = true;
        box_space_quick_drop_elapsed_seconds_ = elapsed_seconds;
        box_space_interaction_panel_ = BoxSpaceInteractionPanel::Game;
        box_space_pressed_cell_ = cur - 2000;
        box_space_quick_drop_target_box_index_ = *tgt;
        SDL_Rect cell_bounds{};
        if (game_save_box_viewport_->getSlotBounds(box_space_pressed_cell_, cell_bounds)) {
            box_space_quick_drop_start_cell_bounds_ = cell_bounds;
            box_space_quick_drop_start_pointer_ =
                SDL_Point{cell_bounds.x + cell_bounds.w / 2, cell_bounds.y + cell_bounds.h / 2};
        }
    }
}

void TransferSystemScreen::applyKeyboardBoxSpaceAdvanceBoxPickupCharge(double elapsed_seconds) {
    if (pokemon_move_.active() || multi_pokemon_move_.active() || held_move_.heldItem() || held_move_.heldBox()) {
        return;
    }
    const FocusNodeId cur = focus_.current();

    if (cur >= 1000 && cur <= 1029 && resort_box_browser_.gameBoxSpaceMode() && resort_box_viewport_) {
        const auto tgt = focusedResortBoxSpaceBoxIndex();
        if (!tgt.has_value()) {
            return;
        }
        box_space_keyboard_charge_kind_ = KeyboardBoxSpaceChargeKind::AdvanceBoxPickup;
        box_space_quick_drop_pending_ = true;
        box_space_quick_drop_kind_ = BoxSpaceQuickDropKind::None;
        box_space_quick_drop_elapsed_seconds_ = elapsed_seconds;
        box_space_interaction_panel_ = BoxSpaceInteractionPanel::Resort;
        box_space_pressed_cell_ = cur - 1000;
        box_space_quick_drop_target_box_index_ = *tgt;
        SDL_Rect cell_bounds{};
        if (resort_box_viewport_->getSlotBounds(box_space_pressed_cell_, cell_bounds)) {
            box_space_quick_drop_start_cell_bounds_ = cell_bounds;
            box_space_quick_drop_start_pointer_ =
                SDL_Point{cell_bounds.x + cell_bounds.w / 2, cell_bounds.y + cell_bounds.h / 2};
        }
        return;
    }
    if (cur >= 2000 && cur <= 2029 && game_box_browser_.gameBoxSpaceMode() && game_save_box_viewport_) {
        const auto tgt = focusedBoxSpaceBoxIndex();
        if (!tgt.has_value()) {
            return;
        }
        box_space_keyboard_charge_kind_ = KeyboardBoxSpaceChargeKind::AdvanceBoxPickup;
        box_space_quick_drop_pending_ = true;
        box_space_quick_drop_kind_ = BoxSpaceQuickDropKind::None;
        box_space_quick_drop_elapsed_seconds_ = elapsed_seconds;
        box_space_interaction_panel_ = BoxSpaceInteractionPanel::Game;
        box_space_pressed_cell_ = cur - 2000;
        box_space_quick_drop_target_box_index_ = *tgt;
        SDL_Rect cell_bounds{};
        if (game_save_box_viewport_->getSlotBounds(box_space_pressed_cell_, cell_bounds)) {
            box_space_quick_drop_start_cell_bounds_ = cell_bounds;
            box_space_quick_drop_start_pointer_ =
                SDL_Point{cell_bounds.x + cell_bounds.w / 2, cell_bounds.y + cell_bounds.h / 2};
        }
    }
}

void TransferSystemScreen::onAdvanceLongPressCharge(double elapsed_seconds) {
    if (pokemon_move_.active() || multi_pokemon_move_.active()) {
        applyKeyboardBoxSpaceQuickDropCharge(elapsed_seconds, KeyboardBoxSpaceChargeKind::AdvanceQuickDrop);
        return;
    }
    applyKeyboardBoxSpaceAdvanceBoxPickupCharge(elapsed_seconds);
}

void TransferSystemScreen::onAdvanceLongPressEnded(bool long_press_action_fired) {
    if (box_space_keyboard_charge_kind_ != KeyboardBoxSpaceChargeKind::AdvanceQuickDrop &&
        box_space_keyboard_charge_kind_ != KeyboardBoxSpaceChargeKind::AdvanceBoxPickup) {
        return;
    }
    box_space_keyboard_charge_kind_ = KeyboardBoxSpaceChargeKind::None;
    if (!long_press_action_fired) {
        clearBoxSpaceQuickDropGesture();
    }
}

bool TransferSystemScreen::captureNavigate2dForLongPress(int dx, int dy) const {
    if (dx != 0 || dy != 1) {
        return false;
    }
    if (keyboard_multi_marquee_active_) {
        return false;
    }
    if (!pokemon_move_.active() && !multi_pokemon_move_.active()) {
        return false;
    }
    const bool game_focus =
        game_box_browser_.gameBoxSpaceMode() && focusedBoxSpaceBoxIndex().has_value();
    const bool resort_focus =
        resort_box_browser_.gameBoxSpaceMode() && focusedResortBoxSpaceBoxIndex().has_value();
    return (game_focus || resort_focus) && box_space_long_press_style_.quick_drop_hold_seconds > 1e-6;
}

std::optional<double> TransferSystemScreen::navigate2dLongPressSeconds(int dx, int dy) const {
    if (dx != 0 || dy != 1) {
        return std::nullopt;
    }
    if (keyboard_multi_marquee_active_) {
        return std::nullopt;
    }
    if (!pokemon_move_.active() && !multi_pokemon_move_.active()) {
        return std::nullopt;
    }
    const bool game_focus =
        game_box_browser_.gameBoxSpaceMode() && focusedBoxSpaceBoxIndex().has_value();
    const bool resort_focus =
        resort_box_browser_.gameBoxSpaceMode() && focusedResortBoxSpaceBoxIndex().has_value();
    if (!game_focus && !resort_focus) {
        return std::nullopt;
    }
    return box_space_long_press_style_.quick_drop_hold_seconds;
}

void TransferSystemScreen::onNavigate2dLongPress(int dx, int dy) {
    if (dx != 0 || dy != 1) {
        return;
    }
    if (!pokemon_move_.active() && !multi_pokemon_move_.active()) {
        return;
    }
    std::optional<int> box_index;
    bool resort_panel = false;
    if (resort_box_browser_.gameBoxSpaceMode()) {
        box_index = focusedResortBoxSpaceBoxIndex();
        resort_panel = box_index.has_value();
    }
    if (!box_index.has_value() && game_box_browser_.gameBoxSpaceMode()) {
        box_index = focusedBoxSpaceBoxIndex();
        resort_panel = false;
    }
    if (!box_index.has_value()) {
        return;
    }
    box_space_interaction_panel_ =
        resort_panel ? BoxSpaceInteractionPanel::Resort : BoxSpaceInteractionPanel::Game;
    if (!completeBoxSpaceQuickDrop(*box_index)) {
        triggerHeldSpriteRejectFeedback();
    }
    clearBoxSpaceQuickDropGesture();
}

void TransferSystemScreen::onNavigationLongPressCharge(double elapsed_seconds, int dx, int dy) {
    if (dx != 0 || dy != 1) {
        return;
    }
    applyKeyboardBoxSpaceQuickDropCharge(elapsed_seconds, KeyboardBoxSpaceChargeKind::NavigateQuickDrop);
}

void TransferSystemScreen::onNavigationLongPressEnded(bool long_press_action_fired) {
    if (box_space_keyboard_charge_kind_ != KeyboardBoxSpaceChargeKind::NavigateQuickDrop) {
        return;
    }
    box_space_keyboard_charge_kind_ = KeyboardBoxSpaceChargeKind::None;
    if (!long_press_action_fired) {
        clearBoxSpaceQuickDropGesture();
    }
}

void TransferSystemScreen::onBackPressed() {
    if (box_rename_modal_open_) {
        if (box_rename_editing_) {
            box_rename_editing_ = false;
            SDL_StopTextInput();
            ui_state_.requestButtonSfx();
            return;
        }
        closeBoxRenameModal(false);
        ui_state_.requestButtonSfx();
        return;
    }
    if (keyboard_multi_marquee_active_) {
        keyboard_multi_marquee_active_ = false;
        ui_state_.requestButtonSfx();
        return;
    }
    if (held_move_.heldItem()) {
        // Cancel item move: always send the currently-held item back to the original pickup Pokemon.
        using Move = transfer_system::PokemonMoveController;
        const auto* held = held_move_.heldItem();
        const Move::SlotRef origin{
            held->origin_slot.panel == transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Game
                ? Move::Panel::Game
                : Move::Panel::Resort,
            held->origin_slot.box_index,
            held->origin_slot.slot_index};
        if (PcSlotSpecies* src = mutablePokemonAt(origin)) {
            if (src->occupied()) {
                src->held_item_id = held->item_id;
                src->held_item_name = held->item_name;
            }
        }
        held_move_.clear();
        refreshResortBoxViewportModel();
        refreshGameBoxViewportModel();
        requestPutdownSfx();
        return;
    }
    if (held_move_.heldBox() || box_space_box_move_hold_.active) {
        held_move_.clear();
        box_space_box_move_hold_.cancel();
        box_space_box_move_source_box_index_ = -1;
        refreshGameBoxViewportModel();
        refreshResortBoxViewportModel();
        requestPutdownSfx();
        return;
    }
    if (multi_pokemon_move_.active()) {
        (void)cancelHeldMultiPokemonMove();
        return;
    }
    if (pokemon_move_.active()) {
        (void)cancelHeldPokemonMove();
        return;
    }
    if (pokemon_action_menu_.visible()) {
        closePokemonActionMenu();
        return;
    }
    if (item_action_menu_.visible()) {
        item_action_menu_.close();
        return;
    }
    if (game_box_browser_.dropdownOpenTarget()) {
        closeGameBoxDropdown();
        return;
    }
    if (resort_box_browser_.dropdownOpenTarget()) {
        closeResortBoxDropdown();
        return;
    }
    // Pull UI away before returning.
    ui_state_.startExit();
}

bool TransferSystemScreen::handlePointerPressed(int logical_x, int logical_y) {
    last_pointer_position_ = SDL_Point{logical_x, logical_y};
    pointer_drag_pickup_pending_ = false;
    pointer_drag_item_pickup_pending_ = false;

    if (box_rename_modal_open_) {
        return handleBoxRenameModalPointerPressed(logical_x, logical_y);
    }

    // Dropdown is modal: while open, block other interactions.
    if (box_name_dropdown_style_.enabled && game_box_browser_.dropdownOpenTarget() && game_box_browser_.dropdownExpandT() > 0.08 &&
        game_pc_boxes_.size() >= 2) {
        SDL_Rect outer{};
        int list_h = 0;
        int list_clip_y = 0;
        const bool has_outer =
            computeGameBoxDropdownOuterRect(outer, static_cast<float>(game_box_browser_.dropdownExpandT()), list_h, list_clip_y);
        const bool in_outer =
            has_outer && logical_x >= outer.x && logical_x < outer.x + outer.w && logical_y >= outer.y &&
            logical_y < outer.y + outer.h;
        if (!in_outer && !hitTestGameBoxNamePlate(logical_x, logical_y)) {
            closeGameBoxDropdown();
            ui_state_.requestButtonSfx();
            return true;
        }
    }
    if (box_name_dropdown_style_.enabled && resort_box_browser_.dropdownOpenTarget() && resort_box_browser_.dropdownExpandT() > 0.08 &&
        resort_pc_boxes_.size() >= 2) {
        SDL_Rect outer{};
        int list_h = 0;
        int list_clip_y = 0;
        const bool has_outer =
            computeResortBoxDropdownOuterRect(outer, static_cast<float>(resort_box_browser_.dropdownExpandT()), list_h, list_clip_y);
        const bool in_outer =
            has_outer && logical_x >= outer.x && logical_x < outer.x + outer.w && logical_y >= outer.y &&
            logical_y < outer.y + outer.h;
        if (!in_outer && !hitTestResortBoxNamePlate(logical_x, logical_y)) {
            closeResortBoxDropdown();
            ui_state_.requestButtonSfx();
            return true;
        }
    }
    if (multi_pokemon_move_.active()) {
        multi_pokemon_move_.updatePointer(last_pointer_position_, window_config_.virtual_width, window_config_.virtual_height);
        if (handleDropdownPointerPressed(logical_x, logical_y) ||
            handleResortDropdownPointerPressed(logical_x, logical_y) ||
            handleGameBoxSpacePointerPressed(logical_x, logical_y) ||
            handleResortBoxSpacePointerPressed(logical_x, logical_y) ||
            handleGameBoxNavigationPointerPressed(logical_x, logical_y) ||
            handleResortBoxNavigationPointerPressed(logical_x, logical_y)) {
            return true;
        }
        if (hitTestToolCarousel(logical_x, logical_y) || hitTestPillTrack(logical_x, logical_y)) {
            return true;
        }
        if (game_box_browser_.gameBoxSpaceMode()) {
            if (const auto picked = focusNodeAtPointer(logical_x, logical_y)) {
                if (*picked >= 2000 && *picked <= 2029) {
                    const int cell = *picked - 2000;
                    const int box_index = game_box_browser_.gameBoxSpaceRowOffset() * 6 + cell;
                    if (box_index >= 0 && box_index < static_cast<int>(game_pc_boxes_.size())) {
                        focus_.setCurrent(*picked);
                        const bool dropped = dropHeldMultiPokemonIntoFirstEmptySlotsInBox(box_index);
                        if (!dropped) {
                            triggerHeldSpriteRejectFeedback();
                        }
                        return true;
                    }
                }
            }
        }
        if (resort_box_browser_.gameBoxSpaceMode()) {
            if (const auto picked = focusNodeAtPointer(logical_x, logical_y)) {
                if (*picked >= 1000 && *picked <= 1029) {
                    const int cell = *picked - 1000;
                    const int box_index = resort_box_browser_.gameBoxSpaceRowOffset() * 6 + cell;
                    if (box_index >= 0 && box_index < static_cast<int>(resort_pc_boxes_.size())) {
                        focus_.setCurrent(*picked);
                        const bool dropped = dropHeldMultiPokemonIntoFirstEmptyResortBox(box_index);
                        if (!dropped) {
                            triggerHeldSpriteRejectFeedback();
                        }
                        return true;
                    }
                }
            }
        }
        if (const auto target = slotRefAtPointer(logical_x, logical_y)) {
            const bool dropped = dropHeldMultiPokemonAt(*target);
            if (!dropped) {
                triggerHeldSpriteRejectFeedback();
            }
            return true;
        }
        return true;
    }

    if (pokemon_move_.active()) {
        pokemon_move_.updatePointer(last_pointer_position_, window_config_.virtual_width, window_config_.virtual_height);
        if (handleDropdownPointerPressed(logical_x, logical_y) ||
            handleResortDropdownPointerPressed(logical_x, logical_y) ||
            handleGameBoxSpacePointerPressed(logical_x, logical_y) ||
            handleResortBoxSpacePointerPressed(logical_x, logical_y) ||
            handleGameBoxNavigationPointerPressed(logical_x, logical_y) ||
            handleResortBoxNavigationPointerPressed(logical_x, logical_y)) {
            return true;
        }
        if (hitTestToolCarousel(logical_x, logical_y) || hitTestPillTrack(logical_x, logical_y)) {
            return true;
        }
        if (const auto target = slotRefAtPointer(logical_x, logical_y)) {
            return dropHeldPokemonAt(*target);
        }
        return true;
    }

    if (held_move_.heldItem()) {
        held_move_.updatePointer(last_pointer_position_, window_config_.virtual_width, window_config_.virtual_height);
        if (handleDropdownPointerPressed(logical_x, logical_y) ||
            handleResortDropdownPointerPressed(logical_x, logical_y) ||
            handleGameBoxSpacePointerPressed(logical_x, logical_y) ||
            handleResortBoxSpacePointerPressed(logical_x, logical_y) ||
            handleGameBoxNavigationPointerPressed(logical_x, logical_y) ||
            handleResortBoxNavigationPointerPressed(logical_x, logical_y)) {
            return true;
        }
        if (hitTestToolCarousel(logical_x, logical_y) || hitTestPillTrack(logical_x, logical_y)) {
            return true;
        }
        if (const auto target = slotRefAtPointer(logical_x, logical_y)) {
            using Move = transfer_system::PokemonMoveController;
            PcSlotSpecies* dst = mutablePokemonAt(*target);
            if (dst && dst->occupied()) {
                const auto* held = held_move_.heldItem();
                if (dst->held_item_id > 0) {
                    const int next_item_id = dst->held_item_id;
                    std::string next_item_name = dst->held_item_name;
                    dst->held_item_id = held->item_id;
                    dst->held_item_name = held->item_name;
                    held_move_.swapHeldItemWith(
                        next_item_id,
                        std::move(next_item_name),
                        transfer_system::move::HeldMoveController::PokemonSlotRef{
                            target->panel == Move::Panel::Game
                                ? transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Game
                                : transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Resort,
                            target->box_index,
                            target->slot_index});
                    refreshResortBoxViewportModel();
                    refreshGameBoxViewportModel();
                    requestPickupSfx();
                } else {
                    dst->held_item_id = held->item_id;
                    dst->held_item_name = held->item_name;
                    held_move_.clear();
                    refreshResortBoxViewportModel();
                    refreshGameBoxViewportModel();
                    requestPutdownSfx();
                }
            }
            return true;
        }
        return true;
    }

    if (handlePokemonActionMenuPointerPressed(logical_x, logical_y)) {
        return true;
    }
    if (handleItemActionMenuPointerPressed(logical_x, logical_y)) {
        return true;
    }

    if (handleDropdownPointerPressed(logical_x, logical_y)) {
        return true;
    }
    if (handleResortDropdownPointerPressed(logical_x, logical_y)) {
        return true;
    }

    if (const std::optional<FocusNodeId> picked = focusNodeAtPointer(logical_x, logical_y)) {
        focus_.setCurrent(*picked);
        // Pointer input puts us in "mouse mode" (no legacy yellow rectangle).
        selection_cursor_hidden_after_mouse_ = true;
        speech_hover_active_ = true;
        if (*picked == 5000) {
            ui_state_.requestButtonSfx();
            onBackPressed();
            return true;
        }
    }

    if (multiPokemonToolActive() && selection_cursor_hidden_after_mouse_ && panelsReadyForInteraction() &&
        !game_box_browser_.dropdownOpenTarget() && !resort_box_browser_.dropdownOpenTarget() &&
        !multi_pokemon_move_.active() && !pokemon_move_.active()) {
        const std::optional<FocusNodeId> picked = focusNodeAtPointer(logical_x, logical_y);
        if (picked &&
            ((*picked >= 1000 && *picked <= 1029 && !resort_box_browser_.gameBoxSpaceMode()) ||
             (*picked >= 2000 && *picked <= 2029 && !game_box_browser_.gameBoxSpaceMode()))) {
            multi_select_drag_active_ = true;
            multi_select_from_game_ = *picked >= 2000;
            multi_select_drag_start_ = last_pointer_position_;
            multi_select_drag_current_ = last_pointer_position_;
            multi_select_drag_rect_ = SDL_Rect{logical_x, logical_y, 1, 1};
            return true;
        }
    }

    // Mouse mode: allow click-drag pickup directly from a slot with the normal tool.
    if (normalPokemonToolActive() && selection_cursor_hidden_after_mouse_ && panelsReadyForInteraction() &&
        !game_box_browser_.dropdownOpenTarget() && !resort_box_browser_.dropdownOpenTarget() &&
        !pokemon_action_menu_.visible() && !pokemon_move_.active()) {
        auto in = [](int px, int py, const SDL_Rect& r) {
            return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
        };
        SDL_Rect r{};
        if (game_save_box_viewport_ && !game_box_browser_.gameBoxSpaceMode()) {
            if (!pointerOverExpandedGameDropdown(logical_x, logical_y)) {
                for (int i = 0; i < 30; ++i) {
                    if (game_save_box_viewport_->getSlotBounds(i, r) && in(logical_x, logical_y, r) && gameSaveSlotHasSpecies(i)) {
                        pointer_drag_pickup_pending_ = true;
                        pointer_drag_pickup_from_game_ = true;
                        pointer_drag_pickup_slot_index_ = i;
                        pointer_drag_pickup_bounds_ = r;
                        pointer_drag_pickup_start_ = last_pointer_position_;
                        return true;
                    }
                }
            }
        }
        if (resort_box_viewport_ && !resort_box_browser_.gameBoxSpaceMode()) {
            if (!pointerOverExpandedResortDropdown(logical_x, logical_y)) {
                for (int i = 0; i < 30; ++i) {
                    if (resort_box_viewport_->getSlotBounds(i, r) && in(logical_x, logical_y, r) && resortSlotHasSpecies(i)) {
                        pointer_drag_pickup_pending_ = true;
                        pointer_drag_pickup_from_game_ = false;
                        pointer_drag_pickup_slot_index_ = i;
                        pointer_drag_pickup_bounds_ = r;
                        pointer_drag_pickup_start_ = last_pointer_position_;
                        return true;
                    }
                }
            }
        }
    }

    // Mouse mode: allow click-drag pickup directly from a slot with the item tool.
    if (itemToolActive() && selection_cursor_hidden_after_mouse_ && panelsReadyForInteraction() &&
        !game_box_browser_.dropdownOpenTarget() && !resort_box_browser_.dropdownOpenTarget() &&
        !item_action_menu_.visible() && !held_move_.heldItem() && !pokemon_move_.active()) {
        auto in = [](int px, int py, const SDL_Rect& r) {
            return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
        };
        SDL_Rect r{};
        if (game_save_box_viewport_ && !game_box_browser_.gameBoxSpaceMode()) {
            if (!pointerOverExpandedGameDropdown(logical_x, logical_y)) {
                for (int i = 0; i < 30; ++i) {
                    if (game_save_box_viewport_->getSlotBounds(i, r) && in(logical_x, logical_y, r) && gameSlotHasHeldItem(i)) {
                        pointer_drag_item_pickup_pending_ = true;
                        pointer_drag_item_pickup_from_game_ = true;
                        pointer_drag_item_pickup_slot_index_ = i;
                        pointer_drag_item_pickup_bounds_ = r;
                        pointer_drag_item_pickup_start_ = last_pointer_position_;
                        return true;
                    }
                }
            }
        }
        if (resort_box_viewport_ && !resort_box_browser_.gameBoxSpaceMode()) {
            if (!pointerOverExpandedResortDropdown(logical_x, logical_y)) {
                const int rbi = resort_box_browser_.gameBoxIndex();
                if (rbi >= 0 && rbi < static_cast<int>(resort_pc_boxes_.size())) {
                    const auto& rs = resort_pc_boxes_[static_cast<std::size_t>(rbi)].slots;
                    for (int i = 0; i < 30; ++i) {
                        if (resort_box_viewport_->getSlotBounds(i, r) && in(logical_x, logical_y, r)) {
                            if (i >= 0 && i < static_cast<int>(rs.size()) && rs[static_cast<std::size_t>(i)].occupied() &&
                                rs[static_cast<std::size_t>(i)].held_item_id > 0) {
                                pointer_drag_item_pickup_pending_ = true;
                                pointer_drag_item_pickup_from_game_ = false;
                                pointer_drag_item_pickup_slot_index_ = i;
                                pointer_drag_item_pickup_bounds_ = r;
                                pointer_drag_item_pickup_start_ = last_pointer_position_;
                                return true;
                            }
                        }
                    }
                }
            }
        }
    }

    if (handlePokemonSlotActionPointerPressed(logical_x, logical_y)) {
        return true;
    }
    if (handleItemSlotActionPointerPressed(logical_x, logical_y)) {
        return true;
    }

    if (handleGameBoxSpacePointerPressed(logical_x, logical_y)) {
        return true;
    }
    if (handleResortBoxSpacePointerPressed(logical_x, logical_y)) {
        return true;
    }

    if (handleGameBoxNavigationPointerPressed(logical_x, logical_y)) {
        return true;
    }

    if (handleResortBoxNavigationPointerPressed(logical_x, logical_y)) {
        return true;
    }

    if (ui_state_.panelsReveal() > 0.02 && !carouselSlideAnimating() && hitTestToolCarousel(logical_x, logical_y)) {
        const int vx = carousel_style_.offset_from_left_wall +
            (exit_button_enabled_ ? (carousel_style_.viewport_height + exit_button_gap_pixels_) : 0);
        const int vw = carousel_style_.viewport_width;
        const int rel = logical_x - vx;
        if (rel * 2 < vw) {
            cycleToolCarousel(-1);
        } else {
            cycleToolCarousel(1);
        }
        ui_state_.requestButtonSfx();
        return true;
    }
    if (hitTestPillTrack(logical_x, logical_y)) {
        togglePillTarget();
        return true;
    }
    return false;
}

void TransferSystemScreen::handlePointerMoved(int logical_x, int logical_y) {
    const bool pointer_moved =
        logical_x != last_pointer_position_.x || logical_y != last_pointer_position_.y;
    last_pointer_position_ = SDL_Point{logical_x, logical_y};
    if (box_rename_modal_open_) {
        return;
    }
    if (pointer_moved) {
        selection_cursor_hidden_after_mouse_ = true;
        if (pokemon_move_.active()) {
            if (auto* h = pokemon_move_.held()) {
                h->input_mode = transfer_system::PokemonMoveController::InputMode::Pointer;
            }
        }
        if (multi_pokemon_move_.active()) {
            multi_pokemon_move_.updatePointer(last_pointer_position_, window_config_.virtual_width, window_config_.virtual_height);
        }
        if (auto* h = held_move_.heldItem()) {
            h->input_mode = transfer_system::move::HeldMoveController::InputMode::Pointer;
        }
    }
    mouse_hover_mini_preview_box_index_ = -1;
    mouse_hover_focus_node_ = -1;

    // Exit button hover should drive info-banner tooltips (not speech bubbles).
    if (exit_button_enabled_) {
        const int bs = carousel_style_.viewport_height;
        const int bx = carousel_style_.offset_from_left_wall;
        const int by = exitButtonScreenY();
        if (bs > 0 && logical_x >= bx && logical_x < bx + bs && logical_y >= by && logical_y < by + bs) {
            mouse_hover_focus_node_ = 5000;
            focus_.setCurrent(5000);
            selection_cursor_hidden_after_mouse_ = true;
            speech_hover_active_ = false;
            return;
        }
    }

    if (pokemon_move_.active()) {
        pokemon_move_.updatePointer(last_pointer_position_, window_config_.virtual_width, window_config_.virtual_height);
    }
    if (multi_pokemon_move_.active()) {
        multi_pokemon_move_.updatePointer(last_pointer_position_, window_config_.virtual_width, window_config_.virtual_height);
    }
    if (held_move_.heldItem()) {
        held_move_.updatePointer(last_pointer_position_, window_config_.virtual_width, window_config_.virtual_height);
    }

    if (held_move_.heldBox()) {
        held_move_.updatePointer(last_pointer_position_, window_config_.virtual_width, window_config_.virtual_height);
    }

    if (box_space_quick_drop_pending_) {
        auto in = [](int px, int py, const SDL_Rect& r) {
            return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
        };
        const int dx = logical_x - box_space_quick_drop_start_pointer_.x;
        const int dy = logical_y - box_space_quick_drop_start_pointer_.y;
        constexpr int kQuickDropCancelThresholdPx = 6;
        const bool moved_far = (dx * dx + dy * dy) >= kQuickDropCancelThresholdPx * kQuickDropCancelThresholdPx;
        if (!in(logical_x, logical_y, box_space_quick_drop_start_cell_bounds_) || moved_far) {
            clearBoxSpaceQuickDropGesture();
        }
    }

    if (multi_select_drag_active_) {
        multi_select_drag_current_ = last_pointer_position_;
        const int x0 = std::min(multi_select_drag_start_.x, multi_select_drag_current_.x);
        const int y0 = std::min(multi_select_drag_start_.y, multi_select_drag_current_.y);
        const int x1 = std::max(multi_select_drag_start_.x, multi_select_drag_current_.x);
        const int y1 = std::max(multi_select_drag_start_.y, multi_select_drag_current_.y);
        multi_select_drag_rect_ = SDL_Rect{x0, y0, std::max(1, x1 - x0), std::max(1, y1 - y0)};
        return;
    }

    if (pointer_drag_pickup_pending_) {
        const int dx = logical_x - pointer_drag_pickup_start_.x;
        const int dy = logical_y - pointer_drag_pickup_start_.y;
        const int dist2 = dx * dx + dy * dy;
        auto in = [](int px, int py, const SDL_Rect& r) {
            return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
        };
        constexpr int kDragPickupThresholdPx = 7;
        const bool moved_far = dist2 >= kDragPickupThresholdPx * kDragPickupThresholdPx;
        const bool left_bounds = !in(logical_x, logical_y, pointer_drag_pickup_bounds_);
        if (moved_far || left_bounds) {
            using Move = transfer_system::PokemonMoveController;
            const Move::SlotRef ref{
                pointer_drag_pickup_from_game_ ? Move::Panel::Game : Move::Panel::Resort,
                pointer_drag_pickup_from_game_ ? game_box_browser_.gameBoxIndex() : resort_box_browser_.gameBoxIndex(),
                pointer_drag_pickup_slot_index_};
            pointer_drag_pickup_pending_ = false;
            (void)beginPokemonMoveFromSlot(ref, Move::InputMode::Pointer, Move::PickupSource::ActionMenu, last_pointer_position_);
        }
    }

    if (pointer_drag_item_pickup_pending_) {
        const int dx = logical_x - pointer_drag_item_pickup_start_.x;
        const int dy = logical_y - pointer_drag_item_pickup_start_.y;
        const int dist2 = dx * dx + dy * dy;
        auto in = [](int px, int py, const SDL_Rect& r) {
            return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
        };
        constexpr int kDragPickupThresholdPx = 7;
        const bool moved_far = dist2 >= kDragPickupThresholdPx * kDragPickupThresholdPx;
        const bool left_bounds = !in(logical_x, logical_y, pointer_drag_item_pickup_bounds_);
        if (moved_far || left_bounds) {
            using Move = transfer_system::PokemonMoveController;
            const Move::SlotRef ref{
                pointer_drag_item_pickup_from_game_ ? Move::Panel::Game : Move::Panel::Resort,
                pointer_drag_item_pickup_from_game_ ? game_box_browser_.gameBoxIndex() : resort_box_browser_.gameBoxIndex(),
                pointer_drag_item_pickup_slot_index_};
            pointer_drag_item_pickup_pending_ = false;

            PcSlotSpecies* src = mutablePokemonAt(ref);
            if (src && src->occupied() && src->held_item_id > 0) {
                held_move_.pickUpItem(
                    src->held_item_id,
                    src->held_item_name,
                    transfer_system::move::HeldMoveController::PokemonSlotRef{
                        pointer_drag_item_pickup_from_game_
                            ? transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Game
                            : transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Resort,
                        pointer_drag_item_pickup_from_game_ ? game_box_browser_.gameBoxIndex() : resort_box_browser_.gameBoxIndex(),
                        pointer_drag_item_pickup_slot_index_},
                    transfer_system::move::HeldMoveController::InputMode::Pointer,
                    last_pointer_position_);
                src->held_item_id = -1;
                src->held_item_name.clear();
                refreshResortBoxViewportModel();
                refreshGameBoxViewportModel();
                requestPickupSfx();
            }
        }
    }

    if (pokemon_action_menu_.visible()) {
        hoverPokemonActionMenuRow(logical_x, logical_y);
        return;
    }
    if (item_action_menu_.visible()) {
        if (const std::optional<int> row = item_action_menu_.rowAtPoint(
                logical_x,
                logical_y,
                pokemon_action_menu_style_,
                window_config_.virtual_width,
                pokemonActionMenuBottomLimitY())) {
            item_action_menu_.selectRow(*row);
        }
        return;
    }

    if (game_box_browser_.gameBoxSpaceMode() && box_space_drag_active_ && game_save_box_viewport_ &&
        box_space_interaction_panel_ == BoxSpaceInteractionPanel::Game) {
        const int dy = logical_y - box_space_drag_last_y_;
        box_space_drag_last_y_ = logical_y;
        box_space_drag_accum_ += static_cast<double>(dy);

        // Scroll threshold tuned by feel; avoids jitter.
        constexpr double kRowStepThresholdPx = 42.0;
        const int max_row = gameBoxSpaceMaxRowOffset();
        while (box_space_drag_accum_ >= kRowStepThresholdPx) {
            // Dragging down should reveal earlier rows (scroll up).
            if (game_box_browser_.gameBoxSpaceRowOffset() > 0) {
                stepGameBoxSpaceRowUp();
            }
            box_space_drag_accum_ -= kRowStepThresholdPx;
            if (game_box_browser_.gameBoxSpaceRowOffset() <= 0) {
                break;
            }
        }
        while (box_space_drag_accum_ <= -kRowStepThresholdPx) {
            if (game_box_browser_.gameBoxSpaceRowOffset() < max_row) {
                stepGameBoxSpaceRowDown();
            }
            box_space_drag_accum_ += kRowStepThresholdPx;
            if (game_box_browser_.gameBoxSpaceRowOffset() >= max_row) {
                break;
            }
        }
        if (pokemon_move_.active()) {
            syncBoxSpaceMiniPreviewHoverFromPointer(logical_x, logical_y);
        }
        return;
    }

    if (resort_box_browser_.gameBoxSpaceMode() && box_space_drag_active_ && resort_box_viewport_ &&
        box_space_interaction_panel_ == BoxSpaceInteractionPanel::Resort) {
        const int dy = logical_y - box_space_drag_last_y_;
        box_space_drag_last_y_ = logical_y;
        box_space_drag_accum_ += static_cast<double>(dy);

        constexpr double kRowStepThresholdPx = 42.0;
        const int max_row = resortBoxSpaceMaxRowOffset();
        while (box_space_drag_accum_ >= kRowStepThresholdPx) {
            if (resort_box_browser_.gameBoxSpaceRowOffset() > 0) {
                stepResortBoxSpaceRowUp();
            }
            box_space_drag_accum_ -= kRowStepThresholdPx;
            if (resort_box_browser_.gameBoxSpaceRowOffset() <= 0) {
                break;
            }
        }
        while (box_space_drag_accum_ <= -kRowStepThresholdPx) {
            if (resort_box_browser_.gameBoxSpaceRowOffset() < max_row) {
                stepResortBoxSpaceRowDown();
            }
            box_space_drag_accum_ += kRowStepThresholdPx;
            if (resort_box_browser_.gameBoxSpaceRowOffset() >= max_row) {
                break;
            }
        }
        if (pokemon_move_.active()) {
            syncBoxSpaceMiniPreviewHoverFromPointer(logical_x, logical_y);
        }
        return;
    }

    if (box_name_dropdown_style_.enabled && game_box_browser_.dropdownOpenTarget() &&
        game_box_browser_.dropdownExpandT() > 0.05 &&
        dropdown_lmb_down_in_panel_) {
        SDL_Rect outer{};
        int list_h = 0;
        int list_clip_y = 0;
        if (computeGameBoxDropdownOuterRect(outer, static_cast<float>(game_box_browser_.dropdownExpandT()), list_h, list_clip_y)) {
            const int dy = logical_y - dropdown_lmb_last_y_;
            dropdown_lmb_last_y_ = logical_y;
            dropdown_lmb_drag_accum_ += std::fabs(static_cast<double>(dy));
            game_box_browser_.scrollDropdownBy(
                -static_cast<double>(dy) * box_name_dropdown_style_.scroll_drag_multiplier,
                static_cast<int>(game_pc_boxes_.size()),
                list_h);
        }
        return;
    }
    if (box_name_dropdown_style_.enabled && game_box_browser_.dropdownOpenTarget() &&
        game_box_browser_.dropdownExpandT() > 0.15 &&
        !dropdown_lmb_down_in_panel_) {
        if (const std::optional<int> row = dropdownRowIndexAtScreen(logical_x, logical_y)) {
            const int current = game_box_browser_.dropdownHighlightIndex();
            stepDropdownHighlight(*row - current);
            SDL_Rect outer{};
            int list_h = 0;
            int list_clip_y = 0;
            if (computeGameBoxDropdownOuterRect(outer, static_cast<float>(game_box_browser_.dropdownExpandT()), list_h, list_clip_y)) {
                syncDropdownScrollToHighlight(list_h);
            }
        }
        return;
    }

    if (box_name_dropdown_style_.enabled && resort_box_browser_.dropdownOpenTarget() &&
        resort_box_browser_.dropdownExpandT() > 0.05 &&
        dropdown_lmb_down_in_panel_) {
        SDL_Rect outer{};
        int list_h = 0;
        int list_clip_y = 0;
        if (computeResortBoxDropdownOuterRect(
                outer, static_cast<float>(resort_box_browser_.dropdownExpandT()), list_h, list_clip_y)) {
            const int dy = logical_y - dropdown_lmb_last_y_;
            dropdown_lmb_last_y_ = logical_y;
            dropdown_lmb_drag_accum_ += std::fabs(static_cast<double>(dy));
            resort_box_browser_.scrollDropdownBy(
                -static_cast<double>(dy) * box_name_dropdown_style_.scroll_drag_multiplier,
                static_cast<int>(resort_pc_boxes_.size()),
                list_h);
        }
        return;
    }
    if (box_name_dropdown_style_.enabled && resort_box_browser_.dropdownOpenTarget() &&
        resort_box_browser_.dropdownExpandT() > 0.15 &&
        !dropdown_lmb_down_in_panel_) {
        if (const std::optional<int> row = resortDropdownRowIndexAtScreen(logical_x, logical_y)) {
            const int current = resort_box_browser_.dropdownHighlightIndex();
            stepResortDropdownHighlight(*row - current);
            SDL_Rect outer{};
            int list_h = 0;
            int list_clip_y = 0;
            if (computeResortBoxDropdownOuterRect(
                    outer, static_cast<float>(resort_box_browser_.dropdownExpandT()), list_h, list_clip_y)) {
                syncResortDropdownScrollToHighlight(list_h);
            }
        }
        return;
    }

    if (ui_state_.uiEnter() > 0.85 && hitTestPillTrack(logical_x, logical_y)) {
        mouse_hover_focus_node_ = 4000;
        focus_.setCurrent(4000);
        selection_cursor_hidden_after_mouse_ = true;
        speech_hover_active_ = false;
        return;
    }

    if (pokemon_move_.active() || multi_pokemon_move_.active()) {
        if ((game_box_browser_.gameBoxSpaceMode() || resort_box_browser_.gameBoxSpaceMode()) &&
            ui_state_.panelsReveal() > 0.85 && ui_state_.uiEnter() > 0.85) {
            syncBoxSpaceMiniPreviewHoverFromPointer(logical_x, logical_y);
        }
        return;
    }

    if (!(box_name_dropdown_style_.enabled && game_box_browser_.dropdownOpenTarget() &&
          game_box_browser_.dropdownExpandT() > 0.05) &&
        !(box_name_dropdown_style_.enabled && resort_box_browser_.dropdownOpenTarget() &&
          resort_box_browser_.dropdownExpandT() > 0.05) &&
        ui_state_.panelsReveal() > 0.85 && ui_state_.uiEnter() > 0.85) {
        if (const std::optional<FocusNodeId> hovered = focusNodeAtPointer(logical_x, logical_y)) {
            mouse_hover_focus_node_ = *hovered;
            focus_.setCurrent(*hovered);
            selection_cursor_hidden_after_mouse_ = true;
        }
        if (const auto hit = speechBubbleTargetAtPointer(logical_x, logical_y)) {
            focus_.setCurrent(hit->first);
            // Mouse hover should not show the legacy yellow rectangle; keep "mouse mode" on.
            selection_cursor_hidden_after_mouse_ = true;
            speech_hover_active_ = true;
        } else {
            speech_hover_active_ = false;
        }

        if (game_box_browser_.gameBoxSpaceMode() || resort_box_browser_.gameBoxSpaceMode()) {
            syncBoxSpaceMiniPreviewHoverFromPointer(logical_x, logical_y);
        }
    }
}

bool TransferSystemScreen::handlePointerReleased(int logical_x, int logical_y) {
    last_pointer_position_ = SDL_Point{logical_x, logical_y};
    if (multi_select_drag_active_) {
        multi_select_drag_active_ = false;
        multi_select_drag_current_ = last_pointer_position_;
        const int x0 = std::min(multi_select_drag_start_.x, multi_select_drag_current_.x);
        const int y0 = std::min(multi_select_drag_start_.y, multi_select_drag_current_.y);
        const int x1 = std::max(multi_select_drag_start_.x, multi_select_drag_current_.x);
        const int y1 = std::max(multi_select_drag_start_.y, multi_select_drag_current_.y);
        multi_select_drag_rect_ = SDL_Rect{x0, y0, std::max(1, x1 - x0), std::max(1, y1 - y0)};
        const auto refs = multiSlotRefsIntersectingRect(multi_select_from_game_, multi_select_drag_rect_);
        if (!refs.empty()) {
            (void)beginMultiPokemonMoveFromSlots(
                refs,
                transfer_system::MultiPokemonMoveController::InputMode::Pointer,
                last_pointer_position_);
        }
        return true;
    }
    if (pointer_drag_pickup_pending_) {
        // Treat as a click: open the action menu. Drag pickup would have cleared `pointer_drag_pickup_pending_`.
        const bool from_game = pointer_drag_pickup_from_game_;
        const int slot = pointer_drag_pickup_slot_index_;
        const SDL_Rect r = pointer_drag_pickup_bounds_;
        pointer_drag_pickup_pending_ = false;
        if (from_game) {
            openPokemonActionMenu(true, slot, r);
        } else {
            openPokemonActionMenu(false, slot, r);
        }
        return true;
    }
    if (pointer_drag_item_pickup_pending_) {
        // Treat as a click: open the item action menu. Drag pickup clears this flag.
        const bool from_game = pointer_drag_item_pickup_from_game_;
        const int slot = pointer_drag_item_pickup_slot_index_;
        const SDL_Rect r = pointer_drag_item_pickup_bounds_;
        pointer_drag_item_pickup_pending_ = false;
        item_action_menu_.setPutAwayGameLabel(selection_game_title_);
        item_action_menu_.open(from_game, slot, r);
        ui_state_.requestButtonSfx();
        return true;
    }
    if (pokemon_move_.active()) {
        pokemon_move_.updatePointer(last_pointer_position_, window_config_.virtual_width, window_config_.virtual_height);
    }
    if (multi_pokemon_move_.active()) {
        multi_pokemon_move_.updatePointer(last_pointer_position_, window_config_.virtual_width, window_config_.virtual_height);
    }
    if (box_space_drag_active_) {
        constexpr double kClickDragThresholdPx = 6.0;
        const bool treat_as_click = std::fabs(box_space_drag_accum_) < kClickDragThresholdPx;
        box_space_drag_active_ = false;
        box_space_drag_accum_ = 0.0;
        if (box_space_suppress_click_open_on_release_) {
            box_space_suppress_click_open_on_release_ = false;
            box_space_pressed_cell_ = -1;
            box_space_box_move_hold_.cancel();
            box_space_box_move_source_box_index_ = -1;
            return true;
        }
        if (treat_as_click && box_space_pressed_cell_ >= 0 && box_space_pressed_cell_ < 30 &&
            box_space_interaction_panel_ == BoxSpaceInteractionPanel::Game && game_box_browser_.gameBoxSpaceMode() &&
            game_save_box_viewport_) {
            const int box_index = game_box_browser_.gameBoxSpaceRowOffset() * 6 + box_space_pressed_cell_;
            box_space_pressed_cell_ = -1;
            box_space_box_move_hold_.cancel();
            box_space_box_move_source_box_index_ = -1;
            box_space_interaction_panel_ = BoxSpaceInteractionPanel::None;
            (void)openGameBoxFromBoxSpaceSelection(box_index);
        } else if (
            treat_as_click && box_space_pressed_cell_ >= 0 && box_space_pressed_cell_ < 30 &&
            box_space_interaction_panel_ == BoxSpaceInteractionPanel::Resort && resort_box_browser_.gameBoxSpaceMode() &&
            resort_box_viewport_) {
            const int box_index = resort_box_browser_.gameBoxSpaceRowOffset() * 6 + box_space_pressed_cell_;
            box_space_pressed_cell_ = -1;
            box_space_box_move_hold_.cancel();
            box_space_box_move_source_box_index_ = -1;
            box_space_interaction_panel_ = BoxSpaceInteractionPanel::None;
            (void)openResortBoxFromBoxSpaceSelection(box_index);
        } else {
            box_space_pressed_cell_ = -1;
            box_space_box_move_hold_.cancel();
            box_space_box_move_source_box_index_ = -1;
            box_space_interaction_panel_ = BoxSpaceInteractionPanel::None;
        }
        return true;
    }

    if (held_move_.heldBox()) {
        int target_box_index = -1;
        bool target_resort = false;
        if (const auto picked = focusNodeAtPointer(logical_x, logical_y)) {
            if (*picked >= 2000 && *picked <= 2029 && game_box_browser_.gameBoxSpaceMode()) {
                target_box_index = game_box_browser_.gameBoxSpaceRowOffset() * 6 + (*picked - 2000);
                target_resort = false;
            } else if (*picked >= 1000 && *picked <= 1029 && resort_box_browser_.gameBoxSpaceMode()) {
                target_box_index = resort_box_browser_.gameBoxSpaceRowOffset() * 6 + (*picked - 1000);
                target_resort = true;
            }
        }
        const auto* hb = held_move_.heldBox();
        const int from = hb->source_box_index;
        const auto from_panel = hb->source_panel;
        held_move_.clear();
        if (from >= 0 && target_box_index >= 0) {
            if (from_panel == transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Game && !target_resort) {
                (void)swapGamePcBoxes(from, target_box_index);
            } else if (
                from_panel == transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Resort && target_resort) {
                (void)swapResortPcBoxes(from, target_box_index);
            } else if (
                from_panel == transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Game && target_resort) {
                (void)swapGameAndResortPcBoxes(from, target_box_index);
            } else if (
                from_panel == transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Resort && !target_resort) {
                (void)swapGameAndResortPcBoxes(target_box_index, from);
            }
        }
        refreshGameBoxViewportModel();
        refreshResortBoxViewportModel();
        requestPutdownSfx();
        return true;
    }
    // If the item action menu is open, consume pointer release to avoid treating the press as a click-through.
    if (item_action_menu_.visible()) {
        return true;
    }
    if (!dropdown_lmb_down_in_panel_) {
        return false;
    }
    dropdown_lmb_down_in_panel_ = false;

    if (box_name_dropdown_style_.enabled && game_box_browser_.dropdownOpenTarget() &&
        game_box_browser_.dropdownExpandT() > 0.05 &&
        game_pc_boxes_.size() >= 2) {
        constexpr double kClickDragThresholdPx = 6.0;
        const bool treat_as_click = dropdown_lmb_drag_accum_ < kClickDragThresholdPx;
        dropdown_lmb_drag_accum_ = 0.0;

        if (treat_as_click) {
            SDL_Rect outer{};
            int list_h = 0;
            int list_clip_y = 0;
            if (computeGameBoxDropdownOuterRect(outer, static_cast<float>(game_box_browser_.dropdownExpandT()), list_h, list_clip_y)) {
                const bool in_outer = logical_x >= outer.x && logical_x < outer.x + outer.w && logical_y >= outer.y &&
                                       logical_y < outer.y + outer.h;
                if (in_outer) {
                    if (const std::optional<int> row = dropdownRowIndexAtScreen(logical_x, logical_y)) {
                        stepDropdownHighlight(*row - game_box_browser_.dropdownHighlightIndex());
                        applyGameBoxDropdownSelection();
                        return true;
                    }
                }
            }
        }
    } else if (
        box_name_dropdown_style_.enabled && resort_box_browser_.dropdownOpenTarget() &&
        resort_box_browser_.dropdownExpandT() > 0.05 &&
        resort_pc_boxes_.size() >= 2) {
        constexpr double kClickDragThresholdPx = 6.0;
        const bool treat_as_click = dropdown_lmb_drag_accum_ < kClickDragThresholdPx;
        dropdown_lmb_drag_accum_ = 0.0;

        if (treat_as_click) {
            SDL_Rect outer{};
            int list_h = 0;
            int list_clip_y = 0;
            if (computeResortBoxDropdownOuterRect(outer, static_cast<float>(resort_box_browser_.dropdownExpandT()), list_h, list_clip_y)) {
                const bool in_outer = logical_x >= outer.x && logical_x < outer.x + outer.w && logical_y >= outer.y &&
                                       logical_y < outer.y + outer.h;
                if (in_outer) {
                    if (const std::optional<int> row = resortDropdownRowIndexAtScreen(logical_x, logical_y)) {
                        stepResortDropdownHighlight(*row - resort_box_browser_.dropdownHighlightIndex());
                        applyResortBoxDropdownSelection();
                        return true;
                    }
                }
            }
        }
    } else {
        dropdown_lmb_drag_accum_ = 0.0;
    }
    return false;
}

void TransferSystemScreen::clearBoxSpaceQuickDropGesture() {
    box_space_quick_drop_pending_ = false;
    box_space_quick_drop_kind_ = BoxSpaceQuickDropKind::None;
    box_space_quick_drop_target_box_index_ = -1;
    box_space_quick_drop_elapsed_seconds_ = 0.0;
    box_space_pressed_cell_ = -1;
    box_space_interaction_panel_ = BoxSpaceInteractionPanel::None;
    box_space_keyboard_charge_kind_ = KeyboardBoxSpaceChargeKind::None;
    box_space_wiggle_phase_ = 0.0;
    for (int& w : box_space_slot_wiggle_dx_) {
        w = 0;
    }
}

void TransferSystemScreen::triggerHeldSpriteRejectFeedback() {
    ui_state_.requestErrorSfx();
    held_sprite_shake_timer_ = std::max(held_sprite_shake_timer_, 0.28);
}

bool TransferSystemScreen::tryGiveHeldItemToFirstEligiblePokemonInGameBox(int box_index) {
    const auto* held = held_move_.heldItem();
    if (!held || box_index < 0 || box_index >= static_cast<int>(game_pc_boxes_.size())) {
        return false;
    }
    auto& slots = game_pc_boxes_[static_cast<std::size_t>(box_index)].slots;
    for (auto& slot : slots) {
        if (slot.occupied() && slot.held_item_id <= 0) {
            slot.held_item_id = held->item_id;
            slot.held_item_name = held->item_name;
            held_move_.clear();
            refreshGameBoxViewportModel();
            refreshResortBoxViewportModel();
            requestPutdownSfx();
            return true;
        }
    }
    return false;
}

bool TransferSystemScreen::tryGiveHeldItemToFirstEligiblePokemonInResortBox(int box_index) {
    const auto* held = held_move_.heldItem();
    if (!held || box_index < 0 || box_index >= static_cast<int>(resort_pc_boxes_.size())) {
        return false;
    }
    auto& slots = resort_pc_boxes_[static_cast<std::size_t>(box_index)].slots;
    for (auto& slot : slots) {
        if (slot.occupied() && slot.held_item_id <= 0) {
            slot.held_item_id = held->item_id;
            slot.held_item_name = held->item_name;
            held_move_.clear();
            refreshGameBoxViewportModel();
            refreshResortBoxViewportModel();
            requestPutdownSfx();
            return true;
        }
    }
    return false;
}

bool TransferSystemScreen::completeBoxSpaceQuickDrop(int target_box) {
    BoxSpaceQuickDropKind kind = box_space_quick_drop_kind_;
    if (kind == BoxSpaceQuickDropKind::None) {
        if (multi_pokemon_move_.active()) {
            kind = BoxSpaceQuickDropKind::PokemonMulti;
        } else if (pokemon_move_.active()) {
            kind = BoxSpaceQuickDropKind::PokemonSingle;
        } else if (held_move_.heldItem()) {
            kind = BoxSpaceQuickDropKind::Item;
        }
    }
    const bool resort_panel = box_space_interaction_panel_ == BoxSpaceInteractionPanel::Resort;
    switch (kind) {
        case BoxSpaceQuickDropKind::PokemonMulti:
            return resort_panel ? dropHeldMultiPokemonIntoFirstEmptyResortBox(target_box)
                                : dropHeldMultiPokemonIntoFirstEmptySlotsInBox(target_box);
        case BoxSpaceQuickDropKind::PokemonSingle:
            return resort_panel ? dropHeldPokemonIntoFirstEmptySlotInResortBox(target_box)
                                : dropHeldPokemonIntoFirstEmptySlotInBox(target_box);
        case BoxSpaceQuickDropKind::Item:
            return resort_panel ? tryGiveHeldItemToFirstEligiblePokemonInResortBox(target_box)
                                : tryGiveHeldItemToFirstEligiblePokemonInGameBox(target_box);
        default:
            return false;
    }
}

void TransferSystemScreen::updateBoxSpaceQuickDropVisuals(double dt) {
    for (int& w : box_space_slot_wiggle_dx_) {
        w = 0;
    }

    held_sprite_shake_offset_px_ = 0;
    if (held_sprite_shake_timer_ > 0.0) {
        held_sprite_shake_timer_ -= dt;
        held_sprite_shake_phase_ += dt * 52.0;
        held_sprite_shake_offset_px_ =
            static_cast<int>(std::lround(std::sin(held_sprite_shake_phase_) * 7.0));
        if (held_sprite_shake_timer_ <= 0.0) {
            held_sprite_shake_timer_ = 0.0;
            held_sprite_shake_offset_px_ = 0;
        }
    }

    float collapse_target = 0.f;
    if (box_space_quick_drop_pending_ &&
        box_space_quick_drop_kind_ == BoxSpaceQuickDropKind::PokemonMulti &&
        multi_pokemon_move_.active() &&
        ui_state_.selectedToolIndex() == 0 &&
        box_space_quick_drop_elapsed_seconds_ >= box_space_long_press_style_.long_press_feedback_seconds &&
        box_space_interaction_panel_ != BoxSpaceInteractionPanel::None) {
        collapse_target = 1.f;
    }
    box_space_multi_collapse_t_ +=
        (collapse_target - box_space_multi_collapse_t_) * static_cast<float>(std::clamp(dt * 14.0, 0.0, 1.0));

    const double feedback_sec = box_space_long_press_style_.long_press_feedback_seconds;
    const bool quick_drop_slot_wiggle =
        box_space_quick_drop_pending_ && box_space_quick_drop_elapsed_seconds_ >= feedback_sec &&
        box_space_pressed_cell_ >= 0 && box_space_pressed_cell_ < 30;
    // Mouse: empty-hand long press on a tile uses HoldWithinRect (box move), not quick-drop pending.
    const bool box_pickup_pointer_wiggle =
        box_space_box_move_hold_.active && box_space_box_move_hold_.elapsed_seconds >= feedback_sec &&
        box_space_pressed_cell_ >= 0 && box_space_pressed_cell_ < 30;

    if (quick_drop_slot_wiggle || box_pickup_pointer_wiggle) {
        box_space_wiggle_phase_ += dt * 32.0;
        box_space_slot_wiggle_dx_[static_cast<std::size_t>(box_space_pressed_cell_)] =
            static_cast<int>(std::lround(std::sin(box_space_wiggle_phase_) * 5.0));
    }

    const bool any_wiggle =
        std::any_of(box_space_slot_wiggle_dx_.begin(), box_space_slot_wiggle_dx_.end(), [](int x) { return x != 0; });
    const bool pending_game =
        box_space_quick_drop_pending_ && box_space_interaction_panel_ == BoxSpaceInteractionPanel::Game;
    const bool pending_resort =
        box_space_quick_drop_pending_ && box_space_interaction_panel_ == BoxSpaceInteractionPanel::Resort;
    const bool game_box_space_visual =
        game_save_box_viewport_ && game_box_browser_.gameBoxSpaceMode() &&
        (pending_game || box_space_multi_collapse_t_ > 0.02f || held_sprite_shake_timer_ > 0.0 ||
         (box_pickup_pointer_wiggle && box_space_interaction_panel_ == BoxSpaceInteractionPanel::Game) ||
         (any_wiggle && box_space_interaction_panel_ == BoxSpaceInteractionPanel::Game));
    const bool resort_box_space_visual =
        resort_box_viewport_ && resort_box_browser_.gameBoxSpaceMode() &&
        (pending_resort || box_space_multi_collapse_t_ > 0.02f || held_sprite_shake_timer_ > 0.0 ||
         (box_pickup_pointer_wiggle && box_space_interaction_panel_ == BoxSpaceInteractionPanel::Resort) ||
         (any_wiggle && box_space_interaction_panel_ == BoxSpaceInteractionPanel::Resort));
    if (game_box_space_visual) {
        game_save_box_viewport_->snapContentToModel(gameBoxSpaceViewportModelAt(game_box_browser_.gameBoxSpaceRowOffset()));
    }
    if (resort_box_space_visual) {
        resort_box_viewport_->snapContentToModel(
            resortBoxSpaceViewportModelAt(resort_box_browser_.gameBoxSpaceRowOffset()));
    }
}

bool TransferSystemScreen::consumeErrorSfxRequest() {
    return ui_state_.consumeErrorSfxRequest();
}

bool TransferSystemScreen::consumeButtonSfxRequest() {
    return ui_state_.consumeButtonSfxRequest();
}

bool TransferSystemScreen::consumeUiMoveSfxRequest() {
    return ui_state_.consumeUiMoveSfxRequest();
}

bool TransferSystemScreen::consumePickupSfxRequest() {
    const bool requested = pickup_sfx_requested_;
    pickup_sfx_requested_ = false;
    return requested;
}

bool TransferSystemScreen::consumePutdownSfxRequest() {
    const bool requested = putdown_sfx_requested_;
    putdown_sfx_requested_ = false;
    return requested;
}

bool TransferSystemScreen::consumeReturnToTicketListRequest() {
    return ui_state_.consumeReturnToTicketListRequest();
}

void TransferSystemScreen::requestReturnToTicketList() {
    // Kept for call sites that still phrase this as a screen-level request.
    ui_state_.startExit();
}

bool TransferSystemScreen::capturesUnroutedKeyboardFocus() const {
    return box_rename_modal_open_ && box_rename_editing_;
}

bool TransferSystemScreen::handleUnroutedSdlEvent(const SDL_Event& event) {
    if (!box_rename_modal_open_ || !box_rename_editing_) {
        return false;
    }
    constexpr std::size_t kMaxBytes = 120;
    if (event.type == SDL_TEXTINPUT) {
        std::string chunk(event.text.text);
        while (box_rename_text_utf8_.size() + chunk.size() > kMaxBytes && !chunk.empty()) {
            utf8_pop_back_last(chunk);
        }
        box_rename_text_utf8_ += chunk;
        box_rename_ime_utf8_.clear();
        return true;
    }
    if (event.type == SDL_TEXTEDITING) {
        box_rename_ime_utf8_ = std::string(event.edit.text);
        return true;
    }
    if (event.type == SDL_KEYDOWN) {
        const SDL_Keycode key = event.key.keysym.sym;
        if (key == SDLK_ESCAPE && event.key.repeat == 0) {
            box_rename_editing_ = false;
            SDL_StopTextInput();
            ui_state_.requestButtonSfx();
            return true;
        }
        if ((key == SDLK_RETURN || key == SDLK_KP_ENTER) && event.key.repeat == 0) {
            box_rename_editing_ = false;
            SDL_StopTextInput();
            box_rename_focus_slot_ = BoxRenameFocusSlot::Confirm;
            ui_state_.requestButtonSfx();
            return true;
        }
        if (key == SDLK_BACKSPACE) {
            utf8_pop_back_last(box_rename_text_utf8_);
            box_rename_ime_utf8_.clear();
            return true;
        }
        if (key == SDLK_DELETE && event.key.repeat == 0) {
            box_rename_ime_utf8_.clear();
            return true;
        }
        return true;
    }
    return false;
}

void TransferSystemScreen::openBoxRenameModal(BoxRenameModalPanel panel) {
    closeGameBoxDropdown();
    closeResortBoxDropdown();
    box_rename_modal_panel_ = panel;
    const int idx =
        panel == BoxRenameModalPanel::Game ? game_box_browser_.gameBoxIndex() : resort_box_browser_.gameBoxIndex();
    box_rename_box_index_ = idx;
    box_rename_original_utf8_.clear();
    if (panel == BoxRenameModalPanel::Game && idx >= 0 && idx < static_cast<int>(game_pc_boxes_.size())) {
        box_rename_original_utf8_ = game_pc_boxes_[static_cast<std::size_t>(idx)].name;
    } else if (panel == BoxRenameModalPanel::Resort && idx >= 0 && idx < static_cast<int>(resort_pc_boxes_.size())) {
        box_rename_original_utf8_ = resort_pc_boxes_[static_cast<std::size_t>(idx)].name;
    }
    box_rename_text_utf8_ = box_rename_original_utf8_;
    box_rename_ime_utf8_.clear();
    box_rename_caret_blink_phase_ = 0.0;
    box_rename_editing_ = false;
    box_rename_focus_slot_ = BoxRenameFocusSlot::Field;
    box_rename_modal_open_ = true;
    syncBoxRenameModalLayout();
}

void TransferSystemScreen::syncBoxRenameModalLayout() {
    if (!box_rename_modal_open_) {
        return;
    }
    const int vw = window_config_.virtual_width;
    const int vh = window_config_.virtual_height;
    const int card_w = std::min(720, vw - 48);
    const int pad = 40;
    const int field_h = 56;
    const int gap = 20;
    const int btn_h = 52;
    const int btn_gap = 16;
    const int inner_w = card_w - pad * 2;
    const int btn_w = (inner_w - btn_gap) / 2;
    const int card_h = pad + field_h + gap + btn_h + pad;
    const int card_x = (vw - card_w) / 2;
    const int card_y = (vh - card_h) / 2;
    box_rename_card_rect_virt_ = SDL_Rect{card_x, card_y, card_w, card_h};
    const int field_y = card_y + pad;
    box_rename_text_field_rect_virt_ = SDL_Rect{card_x + pad, field_y, inner_w, field_h};
    const int btn_y = field_y + field_h + gap;
    box_rename_cancel_button_rect_virt_ = SDL_Rect{card_x + pad, btn_y, btn_w, btn_h};
    box_rename_ok_button_rect_virt_ = SDL_Rect{card_x + pad + btn_w + btn_gap, btn_y, btn_w, btn_h};
}

void TransferSystemScreen::closeBoxRenameModal(bool commit) {
    if (!box_rename_modal_open_) {
        SDL_StopTextInput();
        return;
    }
    box_rename_editing_ = false;
    box_rename_modal_open_ = false;
    if (commit) {
        std::string next = trimAsciiWhitespaceCopy(box_rename_text_utf8_);
        if (next.empty()) {
            next = box_rename_original_utf8_;
        }
        const int idx = box_rename_box_index_;
        if (box_rename_modal_panel_ == BoxRenameModalPanel::Game && idx >= 0 &&
            idx < static_cast<int>(game_pc_boxes_.size())) {
            game_pc_boxes_[static_cast<std::size_t>(idx)].name = std::move(next);
            refreshGameBoxViewportModel();
            dropdown_labels_dirty_ = true;
        } else if (
            box_rename_modal_panel_ == BoxRenameModalPanel::Resort && idx >= 0 &&
            idx < static_cast<int>(resort_pc_boxes_.size())) {
            resort_pc_boxes_[static_cast<std::size_t>(idx)].name = std::move(next);
            refreshResortBoxViewportModel();
            resort_dropdown_labels_dirty_ = true;
        }
    }
    box_rename_box_index_ = -1;
    box_rename_original_utf8_.clear();
    box_rename_text_utf8_.clear();
    box_rename_ime_utf8_.clear();
    SDL_StopTextInput();
}

bool TransferSystemScreen::handleBoxRenameModalPointerPressed(int logical_x, int logical_y) {
    syncBoxRenameModalLayout();
    auto within = [](int px, int py, const SDL_Rect& r) {
        return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
    };
    if (!within(logical_x, logical_y, box_rename_card_rect_virt_)) {
        closeBoxRenameModal(false);
        ui_state_.requestButtonSfx();
        return true;
    }
    if (within(logical_x, logical_y, box_rename_ok_button_rect_virt_)) {
        closeBoxRenameModal(true);
        ui_state_.requestButtonSfx();
        return true;
    }
    if (within(logical_x, logical_y, box_rename_cancel_button_rect_virt_)) {
        closeBoxRenameModal(false);
        ui_state_.requestButtonSfx();
        return true;
    }
    if (within(logical_x, logical_y, box_rename_text_field_rect_virt_)) {
        box_rename_focus_slot_ = BoxRenameFocusSlot::Field;
        if (!box_rename_editing_) {
            box_rename_editing_ = true;
            SDL_StartTextInput();
            SDL_SetTextInputRect(&box_rename_text_field_rect_virt_);
        }
        return true;
    }
    return true;
}

void TransferSystemScreen::drawBoxRenameFocusRing(SDL_Renderer* renderer) const {
    if (!selection_cursor_style_.enabled || !box_rename_modal_open_ || box_rename_editing_) {
        return;
    }
    SDL_Rect r{};
    switch (box_rename_focus_slot_) {
        case BoxRenameFocusSlot::Field:
            r = box_rename_text_field_rect_virt_;
            break;
        case BoxRenameFocusSlot::Cancel:
            r = box_rename_cancel_button_rect_virt_;
            break;
        case BoxRenameFocusSlot::Confirm:
            r = box_rename_ok_button_rect_virt_;
            break;
    }
    if (r.w <= 0 || r.h <= 0) {
        return;
    }
    const double pulse =
        (std::sin(ui_state_.elapsedSeconds() * selection_cursor_style_.beat_speed * 3.14159265358979323846 * 2.0) + 1.0) *
        0.5;
    const int pad = selection_cursor_style_.padding + static_cast<int>(std::lround(selection_cursor_style_.beat_magnitude * pulse));
    const int inner_x = r.x - pad;
    const int inner_y = r.y - pad;
    int inner_w = r.w + pad * 2;
    int inner_h = r.h + pad * 2;
    const int min_w = std::max(0, selection_cursor_style_.min_width);
    const int min_h = std::max(0, selection_cursor_style_.min_height);
    int draw_x = inner_x;
    int draw_y = inner_y;
    if (inner_w < min_w || inner_h < min_h) {
        const int cx = inner_x + inner_w / 2;
        const int cy = inner_y + inner_h / 2;
        inner_w = std::max(inner_w, min_w);
        inner_h = std::max(inner_h, min_h);
        draw_x = cx - inner_w / 2;
        draw_y = cy - inner_h / 2;
    }
    const int corner =
        std::clamp(selection_cursor_style_.corner_radius + pad, 0, std::max(0, std::min(inner_w, inner_h) / 2));
    Color c = selection_cursor_style_.color;
    c.a = std::clamp(selection_cursor_style_.alpha, 0, 255);
    drawRoundedOutlineScanlines(renderer, draw_x, draw_y, inner_w, inner_h, corner, c, selection_cursor_style_.thickness);
}

void TransferSystemScreen::drawBoxRenameModal(SDL_Renderer* renderer) {
    if (!box_rename_modal_open_ || !renderer) {
        return;
    }
    TTF_Font* body_f = box_rename_modal_body_font_.get();
    if (!body_f) {
        body_f = dropdown_item_font_.get();
    }
    if (!body_f) {
        return;
    }

    syncBoxRenameModalLayout();

    const int vw = window_config_.virtual_width;
    const int vh = window_config_.virtual_height;
    setDrawColor(renderer, Color{0, 0, 0, 165});
    SDL_Rect dim_rect{0, 0, vw, vh};
    SDL_RenderFillRect(renderer, &dim_rect);

    const int card_x = box_rename_card_rect_virt_.x;
    const int card_y = box_rename_card_rect_virt_.y;
    const int card_w = box_rename_card_rect_virt_.w;
    const int card_h = box_rename_card_rect_virt_.h;
    fillRoundedRingScanlines(
        renderer,
        card_x,
        card_y,
        card_w,
        card_h,
        18,
        2,
        Color{72, 74, 88, 255},
        Color{252, 252, 254, 255});

    const int input_x = box_rename_text_field_rect_virt_.x;
    const int field_y = box_rename_text_field_rect_virt_.y;
    const int input_inner_w = box_rename_text_field_rect_virt_.w;
    const int row_h = box_rename_text_field_rect_virt_.h;

    fillRoundedRingScanlines(
        renderer,
        input_x,
        field_y,
        input_inner_w,
        row_h,
        12,
        2,
        Color{168, 172, 188, 255},
        Color{246, 247, 251, 255});

    const Color btn_border{156, 160, 174, 255};
    const Color btn_fill{218, 220, 228, 255};
    fillRoundedRingScanlines(
        renderer,
        box_rename_cancel_button_rect_virt_.x,
        box_rename_cancel_button_rect_virt_.y,
        box_rename_cancel_button_rect_virt_.w,
        box_rename_cancel_button_rect_virt_.h,
        10,
        2,
        btn_border,
        btn_fill);
    fillRoundedRingScanlines(
        renderer,
        box_rename_ok_button_rect_virt_.x,
        box_rename_ok_button_rect_virt_.y,
        box_rename_ok_button_rect_virt_.w,
        box_rename_ok_button_rect_virt_.h,
        10,
        2,
        btn_border,
        btn_fill);

    const Color icon_col{56, 58, 72, 255};
    TextureHandle x_tex = renderTextTexture(renderer, body_f, "\xc3\x97", icon_col);
    if (x_tex.texture) {
        const SDL_Rect& br = box_rename_cancel_button_rect_virt_;
        SDL_Rect xd{
            br.x + (br.w - x_tex.width) / 2,
            br.y + (br.h - x_tex.height) / 2,
            x_tex.width,
            x_tex.height};
        SDL_RenderCopy(renderer, x_tex.texture.get(), nullptr, &xd);
    }
    TextureHandle check_tex = renderTextTexture(renderer, body_f, "\xe2\x9c\x93", icon_col);
    if (check_tex.texture) {
        const SDL_Rect& br = box_rename_ok_button_rect_virt_;
        SDL_Rect chk{
            br.x + (br.w - check_tex.width) / 2,
            br.y + (br.h - check_tex.height) / 2,
            check_tex.width,
            check_tex.height};
        SDL_RenderCopy(renderer, check_tex.texture.get(), nullptr, &chk);
    }

    const std::string display = box_rename_text_utf8_ + box_rename_ime_utf8_;
    const Color text_col = display.empty() ? Color{160, 164, 178, 255} : Color{34, 36, 48, 255};
    TextureHandle value_tex =
        display.empty() ? renderTextTexture(renderer, body_f, "Enter name", text_col)
                          : renderTextTexture(renderer, body_f, display, text_col);
    const int text_pad = 14;
    const int max_text_w = input_inner_w - text_pad * 2;
    if (value_tex.texture) {
        const int ty = field_y + (row_h - value_tex.height) / 2;
        SDL_Rect vd{input_x + text_pad, ty, std::min(value_tex.width, max_text_w), value_tex.height};
        SDL_RenderCopy(renderer, value_tex.texture.get(), nullptr, &vd);

        if (box_rename_editing_) {
            const bool caret_on =
                std::fmod(box_rename_caret_blink_phase_, 1.06) < 0.53 || !box_rename_ime_utf8_.empty();
            if (caret_on && !display.empty()) {
                int tw = 0;
                int th = 0;
                if (TTF_SizeUTF8(body_f, display.c_str(), &tw, &th) == 0) {
                    const int cx_caret = input_x + text_pad + std::min(tw, max_text_w);
                    setDrawColor(renderer, Color{34, 36, 48, 255});
                    SDL_Rect caret{cx_caret, field_y + 12, 3, row_h - 24};
                    SDL_RenderFillRect(renderer, &caret);
                }
            }
        }
    }

    drawBoxRenameFocusRing(renderer);

    if (box_rename_editing_) {
        SDL_SetTextInputRect(&box_rename_text_field_rect_virt_);
    }
}

} // namespace pr
