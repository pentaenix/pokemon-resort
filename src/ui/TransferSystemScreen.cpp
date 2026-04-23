#include "ui/TransferSystemScreen.hpp"

#include "core/Assets.hpp"
#include "core/Json.hpp"

#include <SDL_image.h>

#include <algorithm>
#include <cctype>
#include <cmath>
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

/// Wires deterministic directional edges for the transfer screen (two PC grids + chrome). Pure spatial
/// navigation fails here because unrelated controls share similar screen coordinates (carousel vs pill).
void attachTransferFocusEdges(std::vector<FocusNode>& nodes) {
    auto byId = [&](FocusNodeId id) -> FocusNode* {
        for (auto& n : nodes) {
            if (n.id == id) {
                return &n;
            }
        }
        return nullptr;
    };
    auto connect = [&](FocusNodeId from, int dir, FocusNodeId to) {
        FocusNode* a = byId(from);
        FocusNode* b = byId(to);
        if (!a || !b) {
            return;
        }
        a->neighbors[static_cast<std::size_t>(dir)] = to;
    };

    constexpr int kCols = 6;
    constexpr FocusNodeId kRes0 = 1000;
    constexpr FocusNodeId kGame0 = 2000;
    constexpr FocusNodeId kRPrev = 1101;
    constexpr FocusNodeId kRName = 1102;
    constexpr FocusNodeId kRNext = 1103;
    constexpr FocusNodeId kRBoxSpace = 1110;
    constexpr FocusNodeId kRIcon = 1111;
    constexpr FocusNodeId kRScroll = 1112;
    constexpr FocusNodeId kGPrev = 2101;
    constexpr FocusNodeId kGName = 2102;
    constexpr FocusNodeId kGNext = 2103;
    constexpr FocusNodeId kGBoxSpace = 2110;
    constexpr FocusNodeId kGIcon = 2111;
    constexpr FocusNodeId kCarousel = 3000;
    constexpr FocusNodeId kPill = 4000;

    auto resortSlot = [&](int idx) -> FocusNodeId { return kRes0 + idx; };
    auto gameSlot = [&](int idx) -> FocusNodeId { return kGame0 + idx; };

    const bool hasGameGrid = byId(gameSlot(0)) != nullptr;
    const bool hasResortGrid = byId(resortSlot(0)) != nullptr;

    if (hasResortGrid) {
        for (int i = 0; i < 30; ++i) {
            const int row = i / kCols;
            const int col = i % kCols;
            const FocusNodeId id = resortSlot(i);

            if (col > 0) {
                connect(id, kFocusNeighborLeft, resortSlot(i - 1));
            } else if (hasGameGrid) {
                connect(id, kFocusNeighborLeft, gameSlot(row * kCols + (kCols - 1)));
            } else {
                connect(id, kFocusNeighborLeft, resortSlot(row * kCols + (kCols - 1)));
            }

            if (col < kCols - 1) {
                connect(id, kFocusNeighborRight, resortSlot(i + 1));
            } else if (hasGameGrid) {
                connect(id, kFocusNeighborRight, gameSlot(row * kCols));
            } else {
                connect(id, kFocusNeighborRight, resortSlot(row * kCols));
            }

            if (row > 0) {
                connect(id, kFocusNeighborUp, resortSlot(i - kCols));
            } else if (col == 0) {
                connect(id, kFocusNeighborUp, kRPrev);
            } else if (col == kCols - 1) {
                connect(id, kFocusNeighborUp, kRNext);
            } else {
                connect(id, kFocusNeighborUp, kRName);
            }

            if (row < 4) {
                connect(id, kFocusNeighborDown, resortSlot(i + kCols));
            } else if (col == 0) {
                connect(id, kFocusNeighborDown, kRIcon);
            } else if (col == kCols - 1) {
                connect(id, kFocusNeighborDown, kRBoxSpace);
            } else {
                connect(id, kFocusNeighborDown, kRScroll);
            }
        }

        connect(kRPrev, kFocusNeighborRight, kRName);
        connect(kRName, kFocusNeighborLeft, kRPrev);
        connect(kRName, kFocusNeighborRight, kRNext);
        connect(kRNext, kFocusNeighborLeft, kRName);

        if (hasGameGrid) {
            connect(kRPrev, kFocusNeighborLeft, kGNext);
            connect(kRNext, kFocusNeighborRight, kGPrev);
        } else {
            connect(kRPrev, kFocusNeighborLeft, kRNext);
            connect(kRNext, kFocusNeighborRight, kRPrev);
        }

        connect(kRPrev, kFocusNeighborUp, kCarousel);
        connect(kRName, kFocusNeighborUp, kCarousel);
        connect(kRNext, kFocusNeighborUp, kCarousel);

        connect(kRPrev, kFocusNeighborDown, resortSlot(0));
        connect(kRName, kFocusNeighborDown, resortSlot(2));
        connect(kRNext, kFocusNeighborDown, resortSlot(kCols - 1));

        connect(kRIcon, kFocusNeighborRight, kRScroll);
        connect(kRScroll, kFocusNeighborLeft, kRIcon);
        connect(kRScroll, kFocusNeighborRight, kRBoxSpace);
        connect(kRBoxSpace, kFocusNeighborLeft, kRScroll);
        // Footer row wraps across the whole window (resort ↔ game), not within the same panel.
        if (hasGameGrid) {
            connect(kRIcon, kFocusNeighborLeft, kGIcon);
            connect(kRBoxSpace, kFocusNeighborRight, kGBoxSpace);
        } else {
            connect(kRIcon, kFocusNeighborLeft, kRBoxSpace);
            connect(kRBoxSpace, kFocusNeighborRight, kRIcon);
        }

        connect(kRIcon, kFocusNeighborUp, resortSlot(24));
        connect(kRScroll, kFocusNeighborUp, resortSlot(26));
        connect(kRBoxSpace, kFocusNeighborUp, resortSlot(29));
    }

    if (hasGameGrid) {
        for (int i = 0; i < 30; ++i) {
            const int row = i / kCols;
            const int col = i % kCols;
            const FocusNodeId id = gameSlot(i);

            if (col > 0) {
                connect(id, kFocusNeighborLeft, gameSlot(i - 1));
            } else if (hasResortGrid) {
                connect(id, kFocusNeighborLeft, resortSlot(row * kCols + (kCols - 1)));
            } else {
                connect(id, kFocusNeighborLeft, gameSlot(row * kCols + (kCols - 1)));
            }

            if (col < kCols - 1) {
                connect(id, kFocusNeighborRight, gameSlot(i + 1));
            } else if (hasResortGrid) {
                connect(id, kFocusNeighborRight, resortSlot(row * kCols));
            } else {
                connect(id, kFocusNeighborRight, gameSlot(row * kCols));
            }

            if (row > 0) {
                connect(id, kFocusNeighborUp, gameSlot(i - kCols));
            } else if (col == 0) {
                connect(id, kFocusNeighborUp, kGPrev);
            } else if (col == kCols - 1) {
                connect(id, kFocusNeighborUp, kGNext);
            } else {
                connect(id, kFocusNeighborUp, kGName);
            }

            if (row < 4) {
                connect(id, kFocusNeighborDown, gameSlot(i + kCols));
            } else if (col <= 2) {
                connect(id, kFocusNeighborDown, kGBoxSpace);
            } else {
                connect(id, kFocusNeighborDown, kGIcon);
            }
        }

        connect(kGPrev, kFocusNeighborRight, kGName);
        connect(kGName, kFocusNeighborLeft, kGPrev);
        connect(kGName, kFocusNeighborRight, kGNext);
        connect(kGNext, kFocusNeighborLeft, kGName);

        if (hasResortGrid) {
            connect(kGPrev, kFocusNeighborLeft, kRNext);
            connect(kGNext, kFocusNeighborRight, kRPrev);
        } else {
            connect(kGPrev, kFocusNeighborLeft, kGNext);
            connect(kGNext, kFocusNeighborRight, kGPrev);
        }

        connect(kGPrev, kFocusNeighborUp, kPill);
        connect(kGName, kFocusNeighborUp, kPill);
        connect(kGNext, kFocusNeighborUp, kPill);

        connect(kGPrev, kFocusNeighborDown, gameSlot(0));
        connect(kGName, kFocusNeighborDown, gameSlot(2));
        connect(kGNext, kFocusNeighborDown, gameSlot(kCols - 1));

        // Footer row wraps across the whole window (game ↔ resort), not within the same panel.
        if (hasResortGrid) {
            connect(kGBoxSpace, kFocusNeighborRight, kRBoxSpace);
            connect(kGIcon, kFocusNeighborLeft, kRIcon);
            connect(kGBoxSpace, kFocusNeighborLeft, kRBoxSpace);
            connect(kGIcon, kFocusNeighborRight, kRIcon);
        } else {
            connect(kGBoxSpace, kFocusNeighborRight, kGIcon);
            connect(kGIcon, kFocusNeighborLeft, kGBoxSpace);
            connect(kGBoxSpace, kFocusNeighborLeft, kGIcon);
            connect(kGIcon, kFocusNeighborRight, kGBoxSpace);
        }

        connect(kGBoxSpace, kFocusNeighborUp, gameSlot(24));
        connect(kGIcon, kFocusNeighborUp, gameSlot(29));
    }

    if (byId(kCarousel) && byId(kRName)) {
        connect(kCarousel, kFocusNeighborDown, kRName);
    }
    if (byId(kPill) && byId(kGName)) {
        connect(kPill, kFocusNeighborDown, kGName);
    }
    /// Top chrome ↔ column footers: carousel sits above resort; pill above game save column.
    if (byId(kCarousel) && byId(kRBoxSpace)) {
        connect(kCarousel, kFocusNeighborUp, kRBoxSpace);
    }
    if (byId(kPill) && byId(kGBoxSpace)) {
        connect(kPill, kFocusNeighborUp, kGBoxSpace);
    }
    /// From footer icons / box-space down into the matching top control (not the opposite column).
    if (byId(kCarousel)) {
        if (byId(kRIcon)) {
            connect(kRIcon, kFocusNeighborDown, kCarousel);
        }
        if (byId(kRScroll)) {
            connect(kRScroll, kFocusNeighborDown, kCarousel);
        }
        if (byId(kRBoxSpace)) {
            connect(kRBoxSpace, kFocusNeighborDown, kCarousel);
        }
    }
    if (byId(kPill)) {
        if (byId(kGIcon)) {
            connect(kGIcon, kFocusNeighborDown, kPill);
        }
        if (byId(kGBoxSpace)) {
            connect(kGBoxSpace, kFocusNeighborDown, kPill);
        }
    }
}

struct BackgroundAnimLoaded {
    bool enabled = false;
    double scale = 1.0;
    double speed_x = 0.0;
    double speed_y = 0.0;
};

struct LoadedGameTransfer {
    double fade_in_seconds = 0;
    double fade_out_seconds = 0.12;
    BackgroundAnimLoaded background_animation{};
    GameTransferBoxViewportStyle box_viewport{};
    GameTransferPillToggleStyle pill_toggle{};
    GameTransferToolCarouselStyle tool_carousel{};
    GameTransferBoxNameDropdownStyle box_name_dropdown{};
    GameTransferSelectionCursorStyle selection_cursor{};
};

LoadedGameTransfer loadGameTransfer(const std::string& project_root) {
    LoadedGameTransfer out;
    const fs::path path = fs::path(project_root) / "config" / "game_transfer.json";
    if (!fs::exists(path)) {
        return out;
    }

    JsonValue root = parseJsonFile(path.string());
    if (!root.isObject()) {
        return out;
    }

    if (const JsonValue* fade = root.get("fade_in_seconds")) {
        if (fade->isNumber()) {
            out.fade_in_seconds = std::max(0.0, fade->asNumber());
        }
    }
    if (const JsonValue* fade = root.get("fade_out_seconds")) {
        if (fade->isNumber()) {
            out.fade_out_seconds = std::max(0.0, fade->asNumber());
        }
    }
    if (const JsonValue* fade = root.get("fade")) {
        if (fade->isObject()) {
            out.fade_in_seconds = std::max(0.0, doubleFromObjectOrDefault(*fade, "in_seconds", out.fade_in_seconds));
            out.fade_out_seconds = std::max(0.0, doubleFromObjectOrDefault(*fade, "out_seconds", out.fade_out_seconds));
        }
    }

    if (const JsonValue* background_animation = root.get("background_animation")) {
        if (background_animation->isObject()) {
            out.background_animation.enabled = boolFromObjectOrDefault(
                *background_animation,
                "enabled",
                out.background_animation.enabled);
            out.background_animation.scale = std::max(
                0.01,
                doubleFromObjectOrDefault(
                    *background_animation,
                    "scale",
                    out.background_animation.scale));
            out.background_animation.speed_x = doubleFromObjectOrDefault(
                *background_animation,
                "speed_x",
                out.background_animation.speed_x);
            out.background_animation.speed_y = doubleFromObjectOrDefault(
                *background_animation,
                "speed_y",
                out.background_animation.speed_y);
        }
    }

    if (const JsonValue* bv = root.get("box_viewport")) {
        if (bv->isObject()) {
            const JsonValue& o = *bv;
            out.box_viewport.arrow_texture =
                stringFromObjectOrDefault(o, "arrow_texture", out.box_viewport.arrow_texture);
            if (const JsonValue* c = o.get("arrow_mod_color")) {
                if (c->isString()) {
                    out.box_viewport.arrow_mod_color =
                        parseHexColorString(c->asString(), out.box_viewport.arrow_mod_color);
                }
            }
            out.box_viewport.box_name_font_pt =
                intFromObjectOrDefault(o, "box_name_font_pt", out.box_viewport.box_name_font_pt);
            if (const JsonValue* c = o.get("box_name_color")) {
                if (c->isString()) {
                    out.box_viewport.box_name_color =
                        parseHexColorString(c->asString(), out.box_viewport.box_name_color);
                }
            }
            out.box_viewport.box_space_font_pt =
                intFromObjectOrDefault(o, "box_space_font_pt", out.box_viewport.box_space_font_pt);
            if (const JsonValue* c = o.get("box_space_color")) {
                if (c->isString()) {
                    out.box_viewport.box_space_color =
                        parseHexColorString(c->asString(), out.box_viewport.box_space_color);
                }
            }
            out.box_viewport.footer_scroll_arrow_offset_y =
                intFromObjectOrDefault(o, "footer_scroll_arrow_offset_y", out.box_viewport.footer_scroll_arrow_offset_y);
            out.box_viewport.content_slide_smoothing =
                doubleFromObjectOrDefault(o, "content_slide_smoothing", out.box_viewport.content_slide_smoothing);
            out.box_viewport.sprite_scale =
                doubleFromObjectOrDefault(o, "sprite_scale", out.box_viewport.sprite_scale);
            out.box_viewport.sprite_offset_y =
                intFromObjectOrDefault(o, "sprite_offset_y", out.box_viewport.sprite_offset_y);

            if (const JsonValue* bs = o.get("box_space_sprites")) {
                if (bs->isObject()) {
                    out.box_viewport.box_space_sprite_scale =
                        doubleFromObjectOrDefault(*bs, "sprite_scale", out.box_viewport.box_space_sprite_scale);
                    out.box_viewport.box_space_sprite_offset_x =
                        intFromObjectOrDefault(*bs, "sprite_offset_x", out.box_viewport.box_space_sprite_offset_x);
                    out.box_viewport.box_space_sprite_offset_y =
                        intFromObjectOrDefault(*bs, "sprite_offset_y", out.box_viewport.box_space_sprite_offset_y);
                }
            }
        }
    }

    if (const JsonValue* pt = root.get("pill_toggle")) {
        if (pt->isObject()) {
            const JsonValue& o = *pt;
            out.pill_toggle.track_width = intFromObjectOrDefault(o, "track_width", out.pill_toggle.track_width);
            out.pill_toggle.track_height = intFromObjectOrDefault(o, "track_height", out.pill_toggle.track_height);
            out.pill_toggle.pill_width = intFromObjectOrDefault(o, "pill_width", out.pill_toggle.pill_width);
            out.pill_toggle.pill_height = intFromObjectOrDefault(o, "pill_height", out.pill_toggle.pill_height);
            out.pill_toggle.pill_inset = intFromObjectOrDefault(o, "pill_inset", out.pill_toggle.pill_inset);
            if (const JsonValue* gab = o.get("gap_above_boxes")) {
                if (gab->isNumber()) {
                    out.pill_toggle.gap_above_boxes = static_cast<int>(gab->asNumber());
                }
            } else {
                out.pill_toggle.gap_above_boxes =
                    intFromObjectOrDefault(o, "gap_above_left_box", out.pill_toggle.gap_above_boxes);
            }
            out.pill_toggle.font_pt = intFromObjectOrDefault(o, "font_pt", out.pill_toggle.font_pt);
            if (const JsonValue* c = o.get("track_color")) {
                if (c->isString()) {
                    out.pill_toggle.track_color = parseHexColorString(c->asString(), out.pill_toggle.track_color);
                }
            }
            if (const JsonValue* c = o.get("pill_color")) {
                if (c->isString()) {
                    out.pill_toggle.pill_color = parseHexColorString(c->asString(), out.pill_toggle.pill_color);
                }
            }
            out.pill_toggle.toggle_smoothing =
                doubleFromObjectOrDefault(o, "toggle_smoothing", out.pill_toggle.toggle_smoothing);
            out.pill_toggle.box_smoothing = doubleFromObjectOrDefault(o, "box_smoothing", out.pill_toggle.box_smoothing);
            if (!o.get("toggle_smoothing")) {
                const double legacy_dur =
                    doubleFromObjectOrDefault(o, "toggle_slide_duration_seconds", 0.18);
                if (legacy_dur > 1e-9) {
                    out.pill_toggle.toggle_smoothing = 5.0 / legacy_dur;
                }
            }
            if (!o.get("box_smoothing")) {
                const double legacy_dur =
                    doubleFromObjectOrDefault(o, "box_panel_slide_duration_seconds", 0.42);
                if (legacy_dur > 1e-9) {
                    out.pill_toggle.box_smoothing = 4.0 / legacy_dur;
                }
            }
        }
    }

    if (const JsonValue* tc = root.get("tool_carousel")) {
        if (tc->isObject()) {
            const JsonValue& o = *tc;
            out.tool_carousel.viewport_width =
                intFromObjectOrDefault(o, "viewport_width", out.tool_carousel.viewport_width);
            out.tool_carousel.viewport_height =
                intFromObjectOrDefault(o, "viewport_height", out.tool_carousel.viewport_height);
            out.tool_carousel.offset_from_left_wall =
                intFromObjectOrDefault(o, "offset_from_left_wall", out.tool_carousel.offset_from_left_wall);
            out.tool_carousel.rest_y = intFromObjectOrDefault(o, "rest_y", out.tool_carousel.rest_y);
            out.tool_carousel.hidden_y = intFromObjectOrDefault(o, "hidden_y", out.tool_carousel.hidden_y);
            out.tool_carousel.viewport_corner_radius =
                intFromObjectOrDefault(o, "viewport_corner_radius", out.tool_carousel.viewport_corner_radius);
            out.tool_carousel.viewport_clip_inset =
                intFromObjectOrDefault(o, "viewport_clip_inset", out.tool_carousel.viewport_clip_inset);
            if (const JsonValue* c = o.get("viewport_color")) {
                if (c->isString()) {
                    out.tool_carousel.viewport_color =
                        parseHexColorString(c->asString(), out.tool_carousel.viewport_color);
                }
            }
            out.tool_carousel.icon_size = intFromObjectOrDefault(o, "icon_size", out.tool_carousel.icon_size);
            out.tool_carousel.selection_frame_size =
                intFromObjectOrDefault(o, "selection_frame_size", out.tool_carousel.selection_frame_size);
            out.tool_carousel.selection_stroke =
                intFromObjectOrDefault(o, "selection_stroke", out.tool_carousel.selection_stroke);
            if (const JsonValue* s = o.get("selector_size")) {
                if (s->isNumber()) {
                    out.tool_carousel.selection_frame_size = static_cast<int>(s->asNumber());
                }
            }
            if (const JsonValue* s = o.get("selector_thickness")) {
                if (s->isNumber()) {
                    out.tool_carousel.selection_stroke = static_cast<int>(s->asNumber());
                }
            }
            out.tool_carousel.selector_corner_radius =
                intFromObjectOrDefault(o, "selector_corner_radius", out.tool_carousel.selector_corner_radius);
            out.tool_carousel.slide_span_pixels =
                intFromObjectOrDefault(o, "slide_span_pixels", out.tool_carousel.slide_span_pixels);
            out.tool_carousel.belt_spacing_pixels =
                intFromObjectOrDefault(o, "belt_spacing_pixels", out.tool_carousel.belt_spacing_pixels);
            out.tool_carousel.slide_smoothing = doubleFromObjectOrDefault(o, "slide_smoothing", out.tool_carousel.slide_smoothing);
            out.tool_carousel.slot_center_left =
                intFromObjectOrDefault(o, "slot_center_left", out.tool_carousel.slot_center_left);
            out.tool_carousel.slot_center_middle =
                intFromObjectOrDefault(o, "slot_center_middle", out.tool_carousel.slot_center_middle);
            out.tool_carousel.slot_center_right =
                intFromObjectOrDefault(o, "slot_center_right", out.tool_carousel.slot_center_right);
            out.tool_carousel.texture_multiple =
                stringFromObjectOrDefault(o, "texture_multiple", out.tool_carousel.texture_multiple);
            out.tool_carousel.texture_basic =
                stringFromObjectOrDefault(o, "texture_basic", out.tool_carousel.texture_basic);
            out.tool_carousel.texture_swap =
                stringFromObjectOrDefault(o, "texture_swap", out.tool_carousel.texture_swap);
            out.tool_carousel.texture_items =
                stringFromObjectOrDefault(o, "texture_items", out.tool_carousel.texture_items);
            if (const JsonValue* c = o.get("frame_multiple")) {
                if (c->isString()) {
                    out.tool_carousel.frame_multiple =
                        parseHexColorString(c->asString(), out.tool_carousel.frame_multiple);
                }
            }
            if (const JsonValue* c = o.get("frame_basic")) {
                if (c->isString()) {
                    out.tool_carousel.frame_basic = parseHexColorString(c->asString(), out.tool_carousel.frame_basic);
                }
            }
            if (const JsonValue* c = o.get("frame_swap")) {
                if (c->isString()) {
                    out.tool_carousel.frame_swap = parseHexColorString(c->asString(), out.tool_carousel.frame_swap);
                }
            }
            if (const JsonValue* c = o.get("frame_items")) {
                if (c->isString()) {
                    out.tool_carousel.frame_items = parseHexColorString(c->asString(), out.tool_carousel.frame_items);
                }
            }
        }
    }

    if (const JsonValue* dd = root.get("box_name_dropdown")) {
        if (dd->isObject()) {
            const JsonValue& o = *dd;
            out.box_name_dropdown.enabled = boolFromObjectOrDefault(o, "enabled", out.box_name_dropdown.enabled);
            out.box_name_dropdown.panel_width_pixels =
                intFromObjectOrDefault(o, "panel_width_pixels", out.box_name_dropdown.panel_width_pixels);
            out.box_name_dropdown.max_height_multiplier = static_cast<float>(doubleFromObjectOrDefault(
                o,
                "max_height_multiplier",
                static_cast<double>(out.box_name_dropdown.max_height_multiplier)));
            out.box_name_dropdown.reference_name_plate_height_pixels = intFromObjectOrDefault(
                o,
                "reference_name_plate_height_pixels",
                out.box_name_dropdown.reference_name_plate_height_pixels);
            out.box_name_dropdown.item_font_pt =
                intFromObjectOrDefault(o, "item_font_pt", out.box_name_dropdown.item_font_pt);
            out.box_name_dropdown.row_padding_y =
                intFromObjectOrDefault(o, "row_padding_y", out.box_name_dropdown.row_padding_y);
            out.box_name_dropdown.panel_corner_radius =
                intFromObjectOrDefault(o, "panel_corner_radius", out.box_name_dropdown.panel_corner_radius);
            out.box_name_dropdown.panel_border_thickness =
                intFromObjectOrDefault(o, "panel_border_thickness", out.box_name_dropdown.panel_border_thickness);
            if (const JsonValue* c = o.get("panel_color")) {
                if (c->isString()) {
                    out.box_name_dropdown.panel_color =
                        parseHexColorString(c->asString(), out.box_name_dropdown.panel_color);
                }
            }
            if (const JsonValue* c = o.get("panel_border_color")) {
                if (c->isString()) {
                    out.box_name_dropdown.panel_border_color =
                        parseHexColorString(c->asString(), out.box_name_dropdown.panel_border_color);
                }
            }
            if (const JsonValue* c = o.get("item_text_color")) {
                if (c->isString()) {
                    out.box_name_dropdown.item_text_color =
                        parseHexColorString(c->asString(), out.box_name_dropdown.item_text_color);
                }
            }
            if (const JsonValue* c = o.get("selected_row_tint")) {
                if (c->isString()) {
                    out.box_name_dropdown.selected_row_tint =
                        parseHexColorString(c->asString(), out.box_name_dropdown.selected_row_tint);
                }
            }
            out.box_name_dropdown.selected_row_tint.a = std::clamp(
                intFromObjectOrDefault(o, "selected_row_tint_alpha", out.box_name_dropdown.selected_row_tint.a),
                0,
                255);
            out.box_name_dropdown.open_smoothing =
                doubleFromObjectOrDefault(o, "open_smoothing", out.box_name_dropdown.open_smoothing);
            out.box_name_dropdown.close_smoothing =
                doubleFromObjectOrDefault(o, "close_smoothing", out.box_name_dropdown.close_smoothing);
            out.box_name_dropdown.bottom_margin_pixels =
                intFromObjectOrDefault(o, "bottom_margin_pixels", out.box_name_dropdown.bottom_margin_pixels);
            out.box_name_dropdown.scroll_drag_multiplier =
                doubleFromObjectOrDefault(o, "scroll_drag_multiplier", out.box_name_dropdown.scroll_drag_multiplier);
        }
    }

    if (const JsonValue* sc = root.get("selection_cursor")) {
        if (sc->isObject()) {
            const JsonValue& o = *sc;
            out.selection_cursor.enabled = boolFromObjectOrDefault(o, "enabled", out.selection_cursor.enabled);
            if (const JsonValue* c = o.get("color")) {
                if (c->isString()) {
                    out.selection_cursor.color =
                        parseHexColorString(c->asString(), out.selection_cursor.color);
                }
            }
            out.selection_cursor.alpha = intFromObjectOrDefault(o, "alpha", out.selection_cursor.alpha);
            out.selection_cursor.thickness = intFromObjectOrDefault(o, "thickness", out.selection_cursor.thickness);
            out.selection_cursor.padding = intFromObjectOrDefault(o, "padding", out.selection_cursor.padding);
            out.selection_cursor.min_width = intFromObjectOrDefault(o, "min_width", out.selection_cursor.min_width);
            out.selection_cursor.min_height = intFromObjectOrDefault(o, "min_height", out.selection_cursor.min_height);
            out.selection_cursor.corner_radius = intFromObjectOrDefault(o, "corner_radius", out.selection_cursor.corner_radius);
            out.selection_cursor.beat_speed = doubleFromObjectOrDefault(o, "beat_speed", out.selection_cursor.beat_speed);
            out.selection_cursor.beat_magnitude = doubleFromObjectOrDefault(o, "beat_magnitude", out.selection_cursor.beat_magnitude);

            if (const JsonValue* sb = o.get("speech_bubble")) {
                if (sb->isObject()) {
                    const JsonValue& b = *sb;
                    out.selection_cursor.speech_bubble.enabled =
                        boolFromObjectOrDefault(b, "enabled", out.selection_cursor.speech_bubble.enabled);
                    out.selection_cursor.speech_bubble.font_pt =
                        intFromObjectOrDefault(b, "font_pt", out.selection_cursor.speech_bubble.font_pt);
                    if (const JsonValue* c = b.get("text_color")) {
                        if (c->isString()) {
                            out.selection_cursor.speech_bubble.text_color =
                                parseHexColorString(c->asString(), out.selection_cursor.speech_bubble.text_color);
                        }
                    }
                    if (const JsonValue* c = b.get("fill_color")) {
                        if (c->isString()) {
                            out.selection_cursor.speech_bubble.fill_color =
                                parseHexColorString(c->asString(), out.selection_cursor.speech_bubble.fill_color);
                        }
                    }
                    out.selection_cursor.speech_bubble.border_thickness =
                        intFromObjectOrDefault(b, "border_thickness", out.selection_cursor.speech_bubble.border_thickness);
                    out.selection_cursor.speech_bubble.corner_radius =
                        intFromObjectOrDefault(b, "corner_radius", out.selection_cursor.speech_bubble.corner_radius);
                    out.selection_cursor.speech_bubble.padding_x =
                        intFromObjectOrDefault(b, "padding_x", out.selection_cursor.speech_bubble.padding_x);
                    out.selection_cursor.speech_bubble.padding_y =
                        intFromObjectOrDefault(b, "padding_y", out.selection_cursor.speech_bubble.padding_y);
                    out.selection_cursor.speech_bubble.min_width =
                        intFromObjectOrDefault(b, "min_width", out.selection_cursor.speech_bubble.min_width);
                    out.selection_cursor.speech_bubble.max_width =
                        intFromObjectOrDefault(b, "max_width", out.selection_cursor.speech_bubble.max_width);
                    out.selection_cursor.speech_bubble.min_height =
                        intFromObjectOrDefault(b, "min_height", out.selection_cursor.speech_bubble.min_height);
                    out.selection_cursor.speech_bubble.empty_min_width =
                        intFromObjectOrDefault(b, "empty_min_width", out.selection_cursor.speech_bubble.empty_min_width);
                    out.selection_cursor.speech_bubble.empty_min_height =
                        intFromObjectOrDefault(b, "empty_min_height", out.selection_cursor.speech_bubble.empty_min_height);
                    out.selection_cursor.speech_bubble.triangle_base_width =
                        intFromObjectOrDefault(b, "triangle_base_width", out.selection_cursor.speech_bubble.triangle_base_width);
                    out.selection_cursor.speech_bubble.triangle_height =
                        intFromObjectOrDefault(b, "triangle_height", out.selection_cursor.speech_bubble.triangle_height);
                    out.selection_cursor.speech_bubble.gap_above_target =
                        intFromObjectOrDefault(b, "gap_above_target", out.selection_cursor.speech_bubble.gap_above_target);
                    out.selection_cursor.speech_bubble.screen_margin =
                        intFromObjectOrDefault(b, "screen_margin", out.selection_cursor.speech_bubble.screen_margin);
                    out.selection_cursor.speech_bubble.resort_game_title =
                        stringFromObjectOrDefault(b, "resort_game_title", out.selection_cursor.speech_bubble.resort_game_title);
                    out.selection_cursor.speech_bubble.pokemon_label_format = stringFromObjectOrDefault(
                        b,
                        "pokemon_label_format",
                        out.selection_cursor.speech_bubble.pokemon_label_format);
                    out.selection_cursor.speech_bubble.default_pokemon_level = intFromObjectOrDefault(
                        b,
                        "default_pokemon_level",
                        out.selection_cursor.speech_bubble.default_pokemon_level);
                    out.selection_cursor.speech_bubble.empty_slot_label =
                        stringFromObjectOrDefault(b, "empty_slot_label", out.selection_cursor.speech_bubble.empty_slot_label);
                }
            }
        }
    }

    return out;
}

} // namespace

TransferSystemScreen::TransferSystemScreen(
    SDL_Renderer* renderer,
    const WindowConfig& window_config,
    const std::string& font_path,
    const std::string& project_root,
    std::shared_ptr<PokeSpriteAssets> sprite_assets)
    : window_config_(window_config),
      renderer_(renderer),
      project_root_(project_root),
      font_path_(font_path),
      sprite_assets_(std::move(sprite_assets)),
      background_(loadTexture(
          renderer,
          resolvePath(project_root_, "assets/transfer_select_save/background.png"))) {
    const LoadedGameTransfer loaded = loadGameTransfer(project_root_);
    fade_in_seconds_ = loaded.fade_in_seconds;
    fade_out_seconds_ = loaded.fade_out_seconds;
    background_animation_.enabled = loaded.background_animation.enabled;
    background_animation_.scale = loaded.background_animation.scale;
    background_animation_.speed_x = loaded.background_animation.speed_x;
    background_animation_.speed_y = loaded.background_animation.speed_y;
    pill_style_ = loaded.pill_toggle;
    carousel_style_ = loaded.tool_carousel;
    box_name_dropdown_style_ = loaded.box_name_dropdown;
    selection_cursor_style_ = loaded.selection_cursor;
    pill_font_ = loadFont(font_path_, std::max(8, pill_style_.font_pt), project_root_);
    dropdown_item_font_ =
        loadFont(font_path_, std::max(8, box_name_dropdown_style_.item_font_pt), project_root_);
    speech_bubble_font_ = loadFontPreferringUnicode(
        font_path_,
        std::max(8, selection_cursor_style_.speech_bubble.font_pt),
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

void TransferSystemScreen::enter(const TransferSaveSelection& selection, SDL_Renderer* renderer, int initial_game_box_index) {
    return_to_ticket_list_requested_ = false;
    play_button_sfx_requested_ = false;
    play_ui_move_sfx_requested_ = false;
    elapsed_seconds_ = 0.0;
    slider_t_ = 0.0;
    slider_target_ = 0.0;
    panels_reveal_ = 0.0;
    panels_target_ = 1.0;
    selected_tool_index_ = 1;
    carousel_slide_offset_x_ = 0.0;
    carousel_slide_target_x_ = 0.0;
    ui_enter_ = 0.0;
    ui_enter_target_ = 1.0;
    bottom_banner_reveal_ = 0.0;
    bottom_banner_target_ = 1.0;
    exit_in_progress_ = false;
    exit_fade_seconds_ = 0.0;
    selection_cursor_hidden_after_mouse_ = false;
    speech_hover_active_ = false;
    game_box_dropdown_open_target_ = false;
    game_box_dropdown_expand_t_ = 0.0;
    dropdown_scroll_px_ = 0.0;
    dropdown_lmb_down_in_panel_ = false;
    dropdown_lmb_drag_accum_ = 0.0;
    dropdown_labels_dirty_ = false;
    dropdown_item_textures_.clear();
    current_game_key_ = selection.game_key;
    selection_game_title_ = selection.game_title;
    speech_bubble_label_cache_.clear();
    speech_bubble_label_tex_ = {};
    game_box_index_ = 0;
    pending_game_box_index_ = -1;
    game_box_was_sliding_ = false;
    game_box_space_mode_ = false;
    game_box_space_row_offset_ = 0;
    box_space_drag_active_ = false;
    box_space_drag_last_y_ = 0;
    box_space_drag_accum_ = 0.0;
    box_space_pressed_cell_ = -1;
    // Focus graph is rebuilt at end of enter() once bounds are valid.

    if (resort_box_viewport_) {
        resort_box_viewport_->setModel(BoxViewportModel{});
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

    if (!game_pc_boxes_.empty()) {
        const int count = static_cast<int>(game_pc_boxes_.size());
        if (count > 0) {
            game_box_index_ = ((initial_game_box_index % count) + count) % count;
        }
    }

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
            }
        }
        return m;
    };

    if (game_save_box_viewport_ && !game_pc_boxes_.empty()) {
        game_save_box_viewport_->setModel(build_box_model(game_box_index_));
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
                    []() {},
                    nullptr});
            }
            add(FocusNode{1101, [this]() -> std::optional<SDL_Rect> {
                              if (!resort_box_viewport_) return std::nullopt;
                              SDL_Rect r;
                              return resort_box_viewport_->getPrevArrowBounds(r) ? std::optional<SDL_Rect>(r) : std::nullopt;
                          },
                          []() {}, nullptr});
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
                          []() {}, nullptr});
            add(FocusNode{1110, [this]() -> std::optional<SDL_Rect> {
                              if (!resort_box_viewport_) return std::nullopt;
                              SDL_Rect r;
                              return resort_box_viewport_->getFooterBoxSpaceBounds(r) ? std::optional<SDL_Rect>(r) : std::nullopt;
                          },
                          []() {}, nullptr});
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
                          []() {}, nullptr});
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
                    []() {},
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
                              setGameBoxSpaceMode(!game_box_space_mode_);
                              play_button_sfx_requested_ = true;
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
        add(FocusNode{
            3000,
            [this]() -> std::optional<SDL_Rect> {
                const int vx = carousel_style_.offset_from_left_wall;
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
                const int enter_off = static_cast<int>(std::lround((1.0 - ui_enter_) * static_cast<double>(-(th + 24))));
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

        attachTransferFocusEdges(nodes);
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
        if (game_box_space_mode_) {
            const int box_index = game_box_space_row_offset_ * 6 + slot;
            if (box_index < 0 || box_index >= static_cast<int>(game_pc_boxes_.size())) {
                return "";
            }
            const std::string& name = game_pc_boxes_[static_cast<std::size_t>(box_index)].name;
            return name.empty() ? std::string("BOX") : name;
        }
        if (game_box_index_ < 0 || game_box_index_ >= static_cast<int>(game_pc_boxes_.size())) {
            return sb.empty_slot_label;
        }
        const auto& slots = game_pc_boxes_[static_cast<std::size_t>(game_box_index_)].slots;
        if (slot >= static_cast<int>(slots.size())) {
            return sb.empty_slot_label;
        }
        const auto& pc = slots[static_cast<std::size_t>(slot)];
        if (!pc.occupied()) {
            return sb.empty_slot_label;
        }
        return formatPokemonSpeechLine(sb, pc);
    }
    // Resort column: no species payload in this flow yet.
    return sb.empty_slot_label;
}

void TransferSystemScreen::drawSpeechBubbleCursor(SDL_Renderer* renderer, const SDL_Rect& target, FocusNodeId focus_id) const {
    const GameTransferSpeechBubbleCursorStyle& sb = selection_cursor_style_.speech_bubble;
    if (!sb.enabled) {
        return;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    Color border = carouselFrameColorForIndex(selected_tool_index_);
    border.a = 255;

    const std::string line = speechBubbleLineForFocus(focus_id);
    if (line.empty()) {
        return;
    }
    const std::string cache_key =
        std::to_string(focus_id) + "|" + line + "|" + std::to_string(sb.font_pt) + "|" + std::to_string(game_box_index_);
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
    if (game_box_dropdown_open_target_ && game_box_dropdown_expand_t_ > 0.04) {
        return;
    }
    const std::optional<SDL_Rect> rb = focus_.currentBounds();
    if (!rb) return;
    const SDL_Rect r = *rb;
    const FocusNodeId focus_id = focus_.current();
    bool wants_bubble = false;
    if (selection_cursor_style_.speech_bubble.enabled && focusIdUsesSpeechBubble(focus_id)) {
        const bool is_icon = (focus_id == 1111 || focus_id == 2111);
        const bool is_slot =
            (focus_id >= 1000 && focus_id <= 1029) ||
            (focus_id >= 2000 && focus_id <= 2029);
        bool slot_has_payload = false;
        if (is_slot) {
            const int idx = (focus_id >= 2000) ? (focus_id - 2000) : (focus_id - 1000);
            if (focus_id >= 2000 && game_box_space_mode_) {
                const int box_index = game_box_space_row_offset_ * 6 + idx;
                slot_has_payload = (box_index >= 0 && box_index < static_cast<int>(game_pc_boxes_.size()));
            } else {
                slot_has_payload = (focus_id >= 2000) ? gameSaveSlotHasSpecies(idx) : resortSlotHasSpecies(idx);
            }
        }

        wants_bubble = speech_hover_active_ && (is_icon || slot_has_payload);
        /// Mouse mode: speech bubble only (no legacy rectangle).
        if (selection_cursor_hidden_after_mouse_) {
            if (wants_bubble) {
                drawSpeechBubbleCursor(renderer, r, focus_id);
            }
            return;
        }
        /// Keyboard mode: keep the legacy outline too (occupied slots/icons show both; empty slots outline only).
    }

    if (selection_cursor_hidden_after_mouse_) {
        return;
    }

    const double pulse = (std::sin(elapsed_seconds_ * selection_cursor_style_.beat_speed * 3.14159265358979323846 * 2.0) + 1.0) * 0.5;
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

void TransferSystemScreen::onNavigate2d(int dx, int dy) {
    selection_cursor_hidden_after_mouse_ = false;
    if (box_name_dropdown_style_.enabled && game_box_dropdown_open_target_ && game_box_dropdown_expand_t_ > 0.08 &&
        game_pc_boxes_.size() >= 2) {
        if (dy < 0) {
            stepDropdownHighlight(-1);
        } else if (dy > 0) {
            stepDropdownHighlight(1);
        }
        (void)dx;
        return;
    }

    // Box Space mode: allow vertical navigation to scroll through all rows before leaving the grid.
    if (game_box_space_mode_ && dy != 0) {
        const FocusNodeId cur = focus_.current();
        if (cur >= 2000 && cur <= 2029) {
            const int slot = cur - 2000;
            const int max_row = gameBoxSpaceMaxRowOffset();
            if (dy > 0) {
                // At bottom row of the 6×5 grid.
                if (slot >= 24) {
                    if (game_box_space_row_offset_ < max_row) {
                        stepGameBoxSpaceRowDown();
                        return;
                    }
                    // At end: only now wrap to the footer Box Space button.
                    focus_.setCurrent(2110);
                    play_ui_move_sfx_requested_ = true;
                    return;
                }
            } else if (dy < 0) {
                // At top row of the grid.
                if (slot < 6 && game_box_space_row_offset_ > 0) {
                    stepGameBoxSpaceRowUp();
                    return;
                }
            }
        }
    }

    const FocusNodeId focus_before = focus_.current();
    focus_.navigate(dx, dy, window_config_.virtual_width, window_config_.virtual_height);
    if (focus_.current() != focus_before) {
        play_ui_move_sfx_requested_ = true;
    }
}

void TransferSystemScreen::updateAnimations(double dt) {
    approachExponential(slider_t_, slider_target_, dt, pill_style_.toggle_smoothing);
    approachExponential(panels_reveal_, panels_target_, dt, pill_style_.box_smoothing);
}

void TransferSystemScreen::updateEnterExit(double dt) {
    // Use the same smoothing family as the rest of the screen.
    constexpr double kEnterSmoothing = 14.0;
    constexpr double kBannerSmoothing = 12.0;
    approachExponential(ui_enter_, ui_enter_target_, dt, kEnterSmoothing);
    approachExponential(bottom_banner_reveal_, bottom_banner_target_, dt, kBannerSmoothing);

    if (exit_in_progress_) {
        exit_fade_seconds_ += dt;
        const bool ui_gone = ui_enter_ < 0.02 && bottom_banner_reveal_ < 0.02 && panels_reveal_ < 0.02;
        const bool fade_done =
            (fade_out_seconds_ <= 1e-6) || (exit_fade_seconds_ >= fade_out_seconds_);
        if (ui_gone && fade_done && !return_to_ticket_list_requested_) {
            requestReturnToTicketList();
        }
    }
}

void TransferSystemScreen::updateCarouselSlide(double dt) {
    if (std::fabs(carousel_slide_target_x_) < 1e-9) {
        if (std::fabs(carousel_slide_offset_x_) < 1e-4) {
            carousel_slide_offset_x_ = 0.0;
        }
        return;
    }
    const double lambda = std::max(1.0, carousel_style_.slide_smoothing);
    approachExponential(carousel_slide_offset_x_, carousel_slide_target_x_, dt, lambda);
    const double tgt = carousel_slide_target_x_;
    if (std::fabs(tgt) < 1e-9) {
        return;
    }
    if (std::fabs(carousel_slide_offset_x_ - tgt) < 0.75) {
        if (tgt < 0.0) {
            selected_tool_index_ = (selected_tool_index_ + 1) % 4;
        } else {
            selected_tool_index_ = (selected_tool_index_ + 3) % 4;
        }
        carousel_slide_offset_x_ = 0.0;
        carousel_slide_target_x_ = 0.0;
    }
}

void TransferSystemScreen::syncBoxViewportPositions() {
    const int screen_w = window_config_.virtual_width;
    const int resort_hidden_x = -BoxViewport::kViewportWidth;
    const int game_hidden_x = screen_w;
    const int resort_rest_x = kLeftBoxColumnX;
    const int game_rest_x = screen_w - 40 - BoxViewport::kViewportWidth;

    const int resort_x =
        static_cast<int>(std::round(resort_hidden_x + (resort_rest_x - resort_hidden_x) * panels_reveal_));
    const int game_x =
        static_cast<int>(std::round(game_hidden_x + (game_rest_x - game_hidden_x) * panels_reveal_));

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
    }
    return incoming;
}

int TransferSystemScreen::gameBoxSpaceMaxRowOffset() const {
    const int box_count = static_cast<int>(game_pc_boxes_.size());
    if (box_count <= 30) {
        return 0;
    }
    const int extra = box_count - 30;
    return (extra + 5) / 6;
}

BoxViewportModel TransferSystemScreen::gameBoxSpaceViewportModelAt(int row_offset) const {
    BoxViewportModel m;
    m.box_name = "BOX SPACE";

    const int base_box_index = std::max(0, row_offset) * 6;
    for (int i = 0; i < 30; ++i) {
        const int box_index = base_box_index + i;
        if (box_index < 0 || box_index >= static_cast<int>(game_pc_boxes_.size())) {
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
    return m;
}

void TransferSystemScreen::setGameBoxSpaceMode(bool enabled) {
    if (!game_save_box_viewport_) {
        game_box_space_mode_ = false;
        return;
    }

    game_box_space_mode_ = enabled;
    game_box_space_row_offset_ = std::clamp(game_box_space_row_offset_, 0, gameBoxSpaceMaxRowOffset());

    if (enabled) {
        const bool show_down =
            game_pc_boxes_.size() > 30 && game_box_space_row_offset_ < gameBoxSpaceMaxRowOffset();
        game_save_box_viewport_->setHeaderMode(BoxViewport::HeaderMode::BoxSpace, show_down);
        game_save_box_viewport_->setBoxSpaceActive(true);
        game_save_box_viewport_->snapContentToModel(gameBoxSpaceViewportModelAt(game_box_space_row_offset_));
    } else {
        game_save_box_viewport_->setHeaderMode(BoxViewport::HeaderMode::Normal, false);
        game_save_box_viewport_->setBoxSpaceActive(false);
        game_save_box_viewport_->snapContentToModel(gameBoxViewportModelAt(game_box_index_));
    }

    box_space_drag_active_ = false;
    box_space_drag_last_y_ = 0;
    box_space_drag_accum_ = 0.0;
    box_space_pressed_cell_ = -1;
}

void TransferSystemScreen::stepGameBoxSpaceRowDown() {
    if (!game_box_space_mode_ || !game_save_box_viewport_) {
        return;
    }
    const int max_row = gameBoxSpaceMaxRowOffset();
    if (game_box_space_row_offset_ >= max_row) {
        return;
    }
    game_box_space_row_offset_ += 1;
    const bool show_down =
        game_pc_boxes_.size() > 30 && game_box_space_row_offset_ < max_row;
    game_save_box_viewport_->setHeaderMode(BoxViewport::HeaderMode::BoxSpace, show_down);
    game_save_box_viewport_->snapContentToModel(gameBoxSpaceViewportModelAt(game_box_space_row_offset_));
    play_button_sfx_requested_ = true;
}

void TransferSystemScreen::stepGameBoxSpaceRowUp() {
    if (!game_box_space_mode_ || !game_save_box_viewport_) {
        return;
    }
    if (game_box_space_row_offset_ <= 0) {
        return;
    }
    game_box_space_row_offset_ -= 1;
    const bool show_down =
        game_pc_boxes_.size() > 30 && game_box_space_row_offset_ < gameBoxSpaceMaxRowOffset();
    game_save_box_viewport_->setHeaderMode(BoxViewport::HeaderMode::BoxSpace, show_down);
    game_save_box_viewport_->snapContentToModel(gameBoxSpaceViewportModelAt(game_box_space_row_offset_));
    play_button_sfx_requested_ = true;
}

void TransferSystemScreen::advanceGameBox(int dir) {
    if (!game_save_box_viewport_ || game_pc_boxes_.empty() || dir == 0) {
        return;
    }
    if (game_box_space_mode_) {
        return;
    }
    if (panels_reveal_ <= 0.85 || ui_enter_ <= 0.85) {
        return;
    }
    const int count = static_cast<int>(game_pc_boxes_.size());
    const int next = ((game_box_index_ + dir) % count + count) % count;

    game_box_index_ = next;

    game_save_box_viewport_->queueContentSlide(gameBoxViewportModelAt(next), dir);
    play_button_sfx_requested_ = true;
}

void TransferSystemScreen::jumpGameBoxToIndex(int target_index) {
    if (!game_save_box_viewport_ || game_pc_boxes_.empty()) {
        return;
    }
    if (game_box_space_mode_) {
        return;
    }
    if (panels_reveal_ <= 0.85 || ui_enter_ <= 0.85) {
        return;
    }
    const int n = static_cast<int>(game_pc_boxes_.size());
    target_index = (target_index % n + n) % n;
    if (target_index == game_box_index_) {
        closeGameBoxDropdown();
        return;
    }

    game_box_index_ = target_index;
    game_save_box_viewport_->snapContentToModel(gameBoxViewportModelAt(target_index));
    play_button_sfx_requested_ = true;
    closeGameBoxDropdown();
}

void TransferSystemScreen::updateGameBoxDropdown(double dt) {
    if (!box_name_dropdown_style_.enabled) {
        return;
    }
    const double target = game_box_dropdown_open_target_ ? 1.0 : 0.0;
    const double lambda =
        game_box_dropdown_open_target_ ? box_name_dropdown_style_.open_smoothing : box_name_dropdown_style_.close_smoothing;
    approachExponential(game_box_dropdown_expand_t_, target, dt, std::max(1.0, lambda));
    if (!game_box_dropdown_open_target_ && game_box_dropdown_expand_t_ < 0.02) {
        dropdown_item_textures_.clear();
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
    for (const auto& box : game_pc_boxes_) {
        TextureHandle tex = renderTextTexture(renderer, dropdown_item_font_.get(), box.name, text_color);
        max_h = std::max(max_h, tex.height);
        dropdown_item_textures_.push_back(std::move(tex));
    }
    dropdown_row_height_px_ =
        max_h + std::max(0, box_name_dropdown_style_.row_padding_y) * 2;
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
    const int n = static_cast<int>(game_pc_boxes_.size());
    if (n <= 0 || dropdown_row_height_px_ <= 0) {
        dropdown_scroll_px_ = 0.0;
        return;
    }
    const int content_h = n * dropdown_row_height_px_;
    const int max_scroll = std::max(0, content_h - inner_draw_h);
    dropdown_scroll_px_ = std::clamp(dropdown_scroll_px_, 0.0, static_cast<double>(max_scroll));
}

void TransferSystemScreen::syncDropdownScrollToHighlight(int inner_draw_h) {
    const int rh = std::max(1, dropdown_row_height_px_);
    const int top = dropdown_highlight_index_ * rh;
    const int bot = top + rh;
    if (top < static_cast<int>(dropdown_scroll_px_)) {
        dropdown_scroll_px_ = static_cast<double>(top);
    }
    if (bot > static_cast<int>(dropdown_scroll_px_) + inner_draw_h) {
        dropdown_scroll_px_ = static_cast<double>(bot - inner_draw_h);
    }
    clampDropdownScroll(inner_draw_h);
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
    const int rh = std::max(1, dropdown_row_height_px_);
    const int content_h = count * rh;
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
    if (!game_box_dropdown_open_target_ || game_pc_boxes_.empty()) {
        return std::nullopt;
    }
    SDL_Rect outer{};
    int list_h = 0;
    int list_clip_y = 0;
    if (!computeGameBoxDropdownOuterRect(outer, static_cast<float>(game_box_dropdown_expand_t_), list_h, list_clip_y)) {
        return std::nullopt;
    }
    const int stroke = std::max(0, box_name_dropdown_style_.panel_border_thickness);
    const int inner_x = outer.x + stroke;
    const int inner_w = outer.w - stroke * 2;
    if (logical_x < inner_x || logical_x >= inner_x + inner_w || logical_y < list_clip_y ||
        logical_y >= list_clip_y + list_h) {
        return std::nullopt;
    }
    const int rh = std::max(1, dropdown_row_height_px_);
    const double rel_y = static_cast<double>(logical_y - list_clip_y) + dropdown_scroll_px_;
    const int idx = static_cast<int>(std::floor(rel_y / static_cast<double>(rh)));
    if (idx < 0 || idx >= static_cast<int>(game_pc_boxes_.size())) {
        return std::nullopt;
    }
    return idx;
}

void TransferSystemScreen::stepDropdownHighlight(int delta) {
    const int n = static_cast<int>(game_pc_boxes_.size());
    if (n <= 0 || delta == 0) {
        return;
    }
    dropdown_highlight_index_ = ((dropdown_highlight_index_ + delta) % n + n) % n;
    SDL_Rect outer{};
    int list_h = 0;
    int list_clip_y = 0;
    if (computeGameBoxDropdownOuterRect(outer, static_cast<float>(game_box_dropdown_expand_t_), list_h, list_clip_y)) {
        syncDropdownScrollToHighlight(list_h);
    }
}

void TransferSystemScreen::closeGameBoxDropdown() {
    game_box_dropdown_open_target_ = false;
}

void TransferSystemScreen::toggleGameBoxDropdown() {
    if (!box_name_dropdown_style_.enabled || game_pc_boxes_.size() < 2) {
        return;
    }
    if (game_box_space_mode_) {
        return;
    }
    if (game_box_dropdown_open_target_) {
        closeGameBoxDropdown();
        return;
    }
    game_box_dropdown_open_target_ = true;
    dropdown_highlight_index_ = game_box_index_;
    dropdown_labels_dirty_ = true;
    SDL_Rect outer{};
    int list_h = 0;
    int list_clip_y = 0;
    if (computeGameBoxDropdownOuterRect(outer, 1.f, list_h, list_clip_y)) {
        syncDropdownScrollToHighlight(list_h);
    }
}

void TransferSystemScreen::applyGameBoxDropdownSelection() {
    jumpGameBoxToIndex(dropdown_highlight_index_);
}

void TransferSystemScreen::update(double dt) {
    elapsed_seconds_ += dt;
    updateAnimations(dt);
    updateEnterExit(dt);
    updateCarouselSlide(dt);
    updateGameBoxDropdown(dt);
    if (resort_box_viewport_) {
        resort_box_viewport_->update(dt);
    }
    if (game_save_box_viewport_) {
        game_save_box_viewport_->update(dt);
    }
    syncBoxViewportPositions();

    // Commit box index once the content slide finishes.
    const bool sliding = game_save_box_viewport_ && game_save_box_viewport_->isContentSliding();
    if (game_box_was_sliding_ && !sliding && pending_game_box_index_ >= 0) {
        game_box_index_ = pending_game_box_index_;
        pending_game_box_index_ = -1;
    }
    game_box_was_sliding_ = sliding;
}

void TransferSystemScreen::togglePillTarget() {
    if (slider_target_ < 0.5) {
        slider_target_ = 1.0;
        panels_target_ = 0.0;
    } else {
        slider_target_ = 0.0;
        panels_target_ = 1.0;
    }
    play_button_sfx_requested_ = true;
}

bool TransferSystemScreen::hitTestPillTrack(int logical_x, int logical_y) const {
    int tx = 0;
    int ty = 0;
    int tw = 0;
    int th = 0;
    getPillTrackBounds(pill_style_, window_config_.virtual_width, tx, ty, tw, th);
    // Pill enters from above on first open / exit only.
    const int enter_off = static_cast<int>(std::lround((1.0 - ui_enter_) * static_cast<double>(-(th + 24))));
    ty += enter_off;
    return logical_x >= tx && logical_x < tx + tw && logical_y >= ty && logical_y < ty + th;
}

int TransferSystemScreen::carouselScreenY() const {
    const double t = panels_reveal_;
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
    return std::fabs(carousel_slide_target_x_) > 1e-4 || std::fabs(carousel_slide_offset_x_) > 1e-3;
}

void TransferSystemScreen::cycleToolCarousel(int dir) {
    if (carouselSlideAnimating()) {
        return;
    }
    int span = 0;
    if (dir > 0) {
        if (carousel_style_.slide_span_pixels > 0) {
            span = carousel_style_.slide_span_pixels;
        } else if (carousel_style_.belt_spacing_pixels > 0) {
            span = carousel_style_.belt_spacing_pixels;
        } else {
            span = carousel_style_.slot_center_right - carousel_style_.slot_center_middle;
        }
        if (span <= 0) {
            return;
        }
        carousel_slide_target_x_ = -static_cast<double>(span);
    } else {
        if (carousel_style_.slide_span_pixels > 0) {
            span = carousel_style_.slide_span_pixels;
        } else if (carousel_style_.belt_spacing_pixels > 0) {
            span = carousel_style_.belt_spacing_pixels;
        } else {
            span = carousel_style_.slot_center_middle - carousel_style_.slot_center_left;
        }
        if (span <= 0) {
            return;
        }
        carousel_slide_target_x_ = static_cast<double>(span);
    }
    play_button_sfx_requested_ = true;
}

bool TransferSystemScreen::hitTestToolCarousel(int logical_x, int logical_y) const {
    const int vx = carousel_style_.offset_from_left_wall;
    const int vy = carouselScreenY();
    const int vw = carousel_style_.viewport_width;
    const int vh = carousel_style_.viewport_height;
    return logical_x >= vx && logical_x < vx + vw && logical_y >= vy && logical_y < vy + vh;
}

std::optional<FocusNodeId> TransferSystemScreen::focusNodeAtPointer(int logical_x, int logical_y) const {
    auto in = [](int px, int py, const SDL_Rect& r) {
        return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
    };

    if (panels_reveal_ > 0.02 && hitTestToolCarousel(logical_x, logical_y)) {
        return 3000;
    }
    if (hitTestPillTrack(logical_x, logical_y)) {
        return 4000;
    }

    SDL_Rect r{};
    if (game_save_box_viewport_) {
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
    if (game_box_index_ < 0 || game_box_index_ >= static_cast<int>(game_pc_boxes_.size())) {
        return false;
    }
    const auto& slots = game_pc_boxes_[static_cast<std::size_t>(game_box_index_)].slots;
    if (slot_index >= static_cast<int>(slots.size())) {
        return false;
    }
    return slots[static_cast<std::size_t>(slot_index)].occupied();
}

bool TransferSystemScreen::resortSlotHasSpecies(int slot_index) const {
    if (!resort_box_viewport_ || slot_index < 0 || slot_index >= 30) {
        return false;
    }
    const BoxViewportModel& m = resort_box_viewport_->model();
    if (slot_index >= static_cast<int>(m.slot_sprites.size())) {
        return false;
    }
    return m.slot_sprites[static_cast<std::size_t>(slot_index)].has_value();
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
                if (game_box_space_mode_) {
                    const int box_index = game_box_space_row_offset_ * 6 + i;
                    if (box_index < 0 || box_index >= static_cast<int>(game_pc_boxes_.size())) {
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
                if (!resortSlotHasSpecies(i)) {
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

void TransferSystemScreen::drawToolCarousel(SDL_Renderer* renderer) const {
    const int vx = carousel_style_.offset_from_left_wall;
    const int vy = carouselScreenY();
    const int vw = carousel_style_.viewport_width;
    const int vh = carousel_style_.viewport_height;
    if (vw <= 0 || vh <= 0) {
        return;
    }

    const int radius =
        std::clamp(carousel_style_.viewport_corner_radius, 0, std::min(vw, vh) / 2);

    const int sel_i = selected_tool_index_;
    const int cy = vy + vh / 2;
    const int icon = std::max(1, carousel_style_.icon_size);

    /// Horizontal scroll: one belt with extra off-screen slots (clipped), no duplicate focal layer.
    const int scroll = static_cast<int>(std::lround(carousel_slide_offset_x_));
    const int focus_cx = vx + carousel_style_.slot_center_middle;
    const int pitch_l = carousel_style_.slot_center_middle - carousel_style_.slot_center_left;
    const int pitch_r = carousel_style_.slot_center_right - carousel_style_.slot_center_middle;

    auto strip_center_x_at_k = [&](int k) -> int {
        if (carousel_style_.belt_spacing_pixels > 0) {
            return focus_cx + k * carousel_style_.belt_spacing_pixels + scroll;
        }
        if (carousel_style_.slide_span_pixels > 0) {
            return focus_cx + k * carousel_style_.slide_span_pixels + scroll;
        }
        if (k == 0) {
            return focus_cx + scroll;
        }
        if (k < 0) {
            return focus_cx + k * pitch_l + scroll;
        }
        return focus_cx + k * pitch_r + scroll;
    };

    auto strip_tool_at_k = [&](int k) -> int {
        return ((sel_i + k) % 4 + 4) % 4;
    };

    // Fixed window panel (does not scroll with the strip).
    fillRoundedRectScanlines(renderer, vx, vy, vw, vh, radius, carousel_style_.viewport_color);

    const int clip_inset = carouselIconClipInset(carousel_style_, vw, vh, radius);
    const SDL_Rect viewport_clip{vx, vy, vw, vh};
    SDL_Rect inner_clip{vx + clip_inset, vy + clip_inset, vw - 2 * clip_inset, vh - 2 * clip_inset};
    if (inner_clip.w < icon * 2 || inner_clip.h < icon) {
        inner_clip = viewport_clip;
    }

    const int fs = std::max(carousel_style_.selection_frame_size, icon + 2);
    const int stroke = std::clamp(carousel_style_.selection_stroke, 1, fs / 2);
    int fr = carousel_style_.selector_corner_radius;
    if (fr <= 0) {
        fr = std::clamp(radius, 0, fs / 2);
    } else {
        fr = std::clamp(fr, 0, fs / 2);
    }

    auto draw_icon = [&](int tool_i, int center_x) {
        const TextureHandle& tex = tool_icons_[static_cast<std::size_t>(tool_i)];
        if (!tex.texture || tex.width <= 0) {
            return;
        }
        SDL_SetTextureColorMod(tex.texture.get(), 255, 255, 255);
        SDL_SetTextureAlphaMod(tex.texture.get(), 255);
        const int half = icon / 2;
        SDL_Rect dst{center_x - half, cy - half, icon, icon};
        SDL_RenderCopy(renderer, tex.texture.get(), nullptr, &dst);
    };

    // Five-slot belt: k=-2..2 already exists off-screen; clipping hides it until scroll brings it in.
    SDL_RenderSetClipRect(renderer, &inner_clip);
    for (int k = -2; k <= 2; ++k) {
        draw_icon(strip_tool_at_k(k), strip_center_x_at_k(k));
    }

    // Ring on top of the strip; inner fill clears the aperture — redraw only the belt slot nearest focus.
    SDL_RenderSetClipRect(renderer, &viewport_clip);
    const int fx = focus_cx - fs / 2;
    const int fy = cy - fs / 2;
    fillRoundedRingScanlines(
        renderer,
        fx,
        fy,
        fs,
        fs,
        fr,
        stroke,
        carouselFrameColorForIndex(sel_i),
        carousel_style_.viewport_color);

    int punch_cx = strip_center_x_at_k(0);
    int punch_tool = strip_tool_at_k(0);
    int best_d = std::abs(punch_cx - focus_cx);
    for (int k = -2; k <= 2; ++k) {
        const int cx = strip_center_x_at_k(k);
        const int d = std::abs(cx - focus_cx);
        if (d < best_d) {
            best_d = d;
            punch_cx = cx;
            punch_tool = strip_tool_at_k(k);
        }
    }
    SDL_RenderSetClipRect(renderer, &inner_clip);
    draw_icon(punch_tool, punch_cx);

    SDL_RenderSetClipRect(renderer, nullptr);
}

void TransferSystemScreen::drawPillToggle(SDL_Renderer* renderer) const {
    int track_x = 0;
    int track_y = 0;
    int track_w = 0;
    int track_h = 0;
    getPillTrackBounds(pill_style_, window_config_.virtual_width, track_x, track_y, track_w, track_h);
    const int enter_off = static_cast<int>(std::lround((1.0 - ui_enter_) * static_cast<double>(-(track_h + 24))));
    track_y += enter_off;

    const int pad = std::max(0, pill_style_.pill_inset);
    const int inner_x = track_x + pad;
    const int inner_y = track_y + pad;
    const int inner_w = std::max(0, track_w - 2 * pad);
    const int inner_h = std::max(0, track_h - 2 * pad);

    const int track_radius = std::min(track_h / 2, track_w / 2);
    fillRoundedRectScanlines(renderer, track_x, track_y, track_w, track_h, track_radius, pill_style_.track_color);

    const int pill_w = std::min(pill_style_.pill_width, inner_w);
    const int pill_h = std::min(pill_style_.pill_height, inner_h);
    const int max_travel = std::max(0, inner_w - pill_w);
    const int pill_x = inner_x + static_cast<int>(std::round(slider_t_ * static_cast<double>(max_travel)));
    const int pill_y = inner_y + (inner_h - pill_h) / 2;
    const int pill_radius = std::max(4, std::min(pill_h / 2, pill_w / 2));

    const int mid_x = inner_x + inner_w / 2;
    const int pokemon_cx = inner_x + inner_w / 4;
    const int items_cx = inner_x + (3 * inner_w) / 4;
    const int label_cy = inner_y + inner_h / 2;

    if (pill_label_pokemon_white_.texture) {
        SDL_Rect dr{
            pokemon_cx - pill_label_pokemon_white_.width / 2,
            label_cy - pill_label_pokemon_white_.height / 2,
            pill_label_pokemon_white_.width,
            pill_label_pokemon_white_.height};
        SDL_RenderCopy(renderer, pill_label_pokemon_white_.texture.get(), nullptr, &dr);
    }
    if (pill_label_items_white_.texture) {
        SDL_Rect dr{
            items_cx - pill_label_items_white_.width / 2,
            label_cy - pill_label_items_white_.height / 2,
            pill_label_items_white_.width,
            pill_label_items_white_.height};
        SDL_RenderCopy(renderer, pill_label_items_white_.texture.get(), nullptr, &dr);
    }

    fillRoundedRectScanlines(renderer, pill_x, pill_y, pill_w, pill_h, pill_radius, pill_style_.pill_color);

    const int pill_cx = pill_x + pill_w / 2;
    const bool pokemon_selected = pill_cx < mid_x;
    if (pokemon_selected && pill_label_pokemon_black_.texture) {
        SDL_Rect dr{
            pokemon_cx - pill_label_pokemon_black_.width / 2,
            label_cy - pill_label_pokemon_black_.height / 2,
            pill_label_pokemon_black_.width,
            pill_label_pokemon_black_.height};
        SDL_RenderCopy(renderer, pill_label_pokemon_black_.texture.get(), nullptr, &dr);
    } else if (!pokemon_selected && pill_label_items_black_.texture) {
        SDL_Rect dr{
            items_cx - pill_label_items_black_.width / 2,
            label_cy - pill_label_items_black_.height / 2,
            pill_label_items_black_.width,
            pill_label_items_black_.height};
        SDL_RenderCopy(renderer, pill_label_items_black_.texture.get(), nullptr, &dr);
    }
}

void TransferSystemScreen::drawGameBoxNameDropdownChrome(SDL_Renderer* renderer) const {
    if (!box_name_dropdown_style_.enabled || game_pc_boxes_.empty() || game_box_dropdown_expand_t_ <= 1e-6) {
        return;
    }

    SDL_Rect outer{};
    int list_h = 0;
    int list_clip_y = 0;
    if (!computeGameBoxDropdownOuterRect(outer, static_cast<float>(game_box_dropdown_expand_t_), list_h, list_clip_y)) {
        return;
    }

    const int stroke = std::max(1, box_name_dropdown_style_.panel_border_thickness);
    const int rad =
        std::clamp(box_name_dropdown_style_.panel_corner_radius, 0, std::min(outer.w, outer.h) / 2);

    fillRoundedRingScanlines(
        renderer,
        outer.x,
        outer.y,
        outer.w,
        outer.h,
        rad,
        stroke,
        box_name_dropdown_style_.panel_border_color,
        box_name_dropdown_style_.panel_color);
}

void TransferSystemScreen::drawGameBoxNameDropdownList(SDL_Renderer* renderer) const {
    if (!box_name_dropdown_style_.enabled || game_pc_boxes_.empty() || game_box_dropdown_expand_t_ <= 1e-6) {
        return;
    }
    if (dropdown_labels_dirty_) {
        const_cast<TransferSystemScreen*>(this)->rebuildDropdownItemTextures(renderer);
    }
    if (static_cast<int>(dropdown_item_textures_.size()) != static_cast<int>(game_pc_boxes_.size())) {
        return;
    }

    SDL_Rect outer{};
    int list_h = 0;
    int list_clip_y = 0;
    if (!computeGameBoxDropdownOuterRect(outer, static_cast<float>(game_box_dropdown_expand_t_), list_h, list_clip_y)) {
        return;
    }

    const int stroke = std::max(1, box_name_dropdown_style_.panel_border_thickness);
    const int inner_x = outer.x + stroke;
    const int inner_w = outer.w - stroke * 2;
    const int rh = std::max(1, dropdown_row_height_px_);
    const int n = static_cast<int>(game_pc_boxes_.size());

    SDL_Rect clip{inner_x, list_clip_y, inner_w, list_h};
    SDL_RenderSetClipRect(renderer, &clip);

    const Color tint = box_name_dropdown_style_.selected_row_tint;
    for (int i = 0; i < n; ++i) {
        const int row_top = list_clip_y + i * rh - static_cast<int>(std::lround(dropdown_scroll_px_));
        if (row_top + rh < list_clip_y || row_top > list_clip_y + list_h) {
            continue;
        }
        if (i == dropdown_highlight_index_) {
            const int rr = std::max(0, std::min(8, rh / 4));
            fillRoundedRectScanlines(renderer, inner_x, row_top, inner_w, rh, rr, tint);
        }
        if (i < static_cast<int>(dropdown_item_textures_.size()) && dropdown_item_textures_[i].texture) {
            const TextureHandle& tex = dropdown_item_textures_[i];
            const int tcx = inner_x + inner_w / 2 - tex.width / 2;
            const int tcy = row_top + (rh - tex.height) / 2;
            SDL_Rect dst{tcx, tcy, tex.width, tex.height};
            SDL_RenderCopy(renderer, tex.texture.get(), nullptr, &dst);
        }
    }

    SDL_RenderSetClipRect(renderer, nullptr);
}

void TransferSystemScreen::render(SDL_Renderer* renderer) {
    SDL_RenderSetClipRect(renderer, nullptr);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    drawBackground(renderer);
    if (resort_box_viewport_) {
        resort_box_viewport_->render(renderer);
    }
    if (game_save_box_viewport_) {
        const bool game_dropdown_visible = box_name_dropdown_style_.enabled && !game_pc_boxes_.empty() &&
            game_box_dropdown_expand_t_ > 1e-6;
        if (game_dropdown_visible) {
            game_save_box_viewport_->renderBelowNamePlate(renderer);
            drawGameBoxNameDropdownChrome(renderer);
            game_save_box_viewport_->renderNamePlate(renderer);
            drawGameBoxNameDropdownList(renderer);
        } else {
            game_save_box_viewport_->render(renderer);
        }
    }
    drawToolCarousel(renderer);
    drawPillToggle(renderer);
    drawBottomBanner(renderer);
    drawSelectionCursor(renderer);

    if (!exit_in_progress_ && fade_in_seconds_ > 1e-6) {
        const double t = std::clamp(elapsed_seconds_ / fade_in_seconds_, 0.0, 1.0);
        const int a = static_cast<int>(std::lround(255.0 * (1.0 - t)));
        if (a > 0) {
            setDrawColor(renderer, Color{0, 0, 0, a});
            SDL_Rect r{0, 0, window_config_.virtual_width, window_config_.virtual_height};
            SDL_RenderFillRect(renderer, &r);
        }
    }

    if (exit_in_progress_ && fade_out_seconds_ > 1e-6) {
        const double t = std::clamp(exit_fade_seconds_ / fade_out_seconds_, 0.0, 1.0);
        const int a = static_cast<int>(std::lround(255.0 * t));
        setDrawColor(renderer, Color{0, 0, 0, a});
        SDL_Rect r{0, 0, window_config_.virtual_width, window_config_.virtual_height};
        SDL_RenderFillRect(renderer, &r);
    }
}

void TransferSystemScreen::onAdvancePressed() {
    selection_cursor_hidden_after_mouse_ = false;
    if (box_name_dropdown_style_.enabled && game_box_dropdown_open_target_ && game_box_dropdown_expand_t_ > 0.08 &&
        game_pc_boxes_.size() >= 2) {
        applyGameBoxDropdownSelection();
        return;
    }
    if (box_name_dropdown_style_.enabled && !game_box_dropdown_open_target_ && focus_.current() == 2102 &&
        game_pc_boxes_.size() >= 2) {
        toggleGameBoxDropdown();
        return;
    }
    focus_.activate();
}

void TransferSystemScreen::onBackPressed() {
    if (game_box_dropdown_open_target_) {
        closeGameBoxDropdown();
        return;
    }
    // Pull UI away before returning.
    exit_in_progress_ = true;
    ui_enter_target_ = 0.0;
    bottom_banner_target_ = 0.0;
    panels_target_ = 0.0;
}

bool TransferSystemScreen::handlePointerPressed(int logical_x, int logical_y) {
    if (const std::optional<FocusNodeId> picked = focusNodeAtPointer(logical_x, logical_y)) {
        focus_.setCurrent(*picked);
        // Pointer input puts us in "mouse mode" (no legacy yellow rectangle).
        selection_cursor_hidden_after_mouse_ = true;
    }

    // Box Space controls (game panel).
    if (game_save_box_viewport_ && panels_reveal_ > 0.85 && ui_enter_ > 0.85) {
        SDL_Rect r{};
        if (game_save_box_viewport_->getFooterBoxSpaceBounds(r) &&
            logical_x >= r.x && logical_x < r.x + r.w && logical_y >= r.y && logical_y < r.y + r.h) {
            setGameBoxSpaceMode(!game_box_space_mode_);
            play_button_sfx_requested_ = true;
            closeGameBoxDropdown();
            return true;
        }
        if (game_save_box_viewport_->hitTestBoxSpaceScrollArrow(logical_x, logical_y)) {
            stepGameBoxSpaceRowDown();
            closeGameBoxDropdown();
            return true;
        }
        if (game_box_space_mode_) {
            // Drag-to-scroll inside the grid.
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
                box_space_drag_active_ = true;
                box_space_drag_last_y_ = logical_y;
                box_space_drag_accum_ = 0.0;
                // Record which cell we pressed; if there is no drag, release will treat as a click.
                if (const std::optional<FocusNodeId> picked = focusNodeAtPointer(logical_x, logical_y)) {
                    if (*picked >= 2000 && *picked <= 2029) {
                        box_space_pressed_cell_ = *picked - 2000;
                    }
                }
                return true;
            }

            // Clicking a box icon opens that PC box.
            if (const std::optional<FocusNodeId> picked = focusNodeAtPointer(logical_x, logical_y)) {
                if (*picked >= 2000 && *picked <= 2029) {
                    const int cell = *picked - 2000;
                    const int box_index = game_box_space_row_offset_ * 6 + cell;
                    if (box_index >= 0 && box_index < static_cast<int>(game_pc_boxes_.size())) {
                        game_box_index_ = box_index;
                        setGameBoxSpaceMode(false);
                        play_button_sfx_requested_ = true;
                        return true;
                    }
                }
            }
        }
    }

    if (box_name_dropdown_style_.enabled && game_pc_boxes_.size() >= 2) {
        if (game_box_dropdown_open_target_ && game_box_dropdown_expand_t_ > 0.05) {
            SDL_Rect outer{};
            int list_h = 0;
            int list_clip_y = 0;
            if (computeGameBoxDropdownOuterRect(outer, static_cast<float>(game_box_dropdown_expand_t_), list_h, list_clip_y)) {
                const bool in_outer = logical_x >= outer.x && logical_x < outer.x + outer.w && logical_y >= outer.y &&
                                       logical_y < outer.y + outer.h;
                if (in_outer) {
                    dropdown_lmb_down_in_panel_ = true;
                    dropdown_lmb_last_y_ = logical_y;
                    dropdown_lmb_drag_accum_ = 0.0;
                    if (const std::optional<int> row = dropdownRowIndexAtScreen(logical_x, logical_y)) {
                        dropdown_highlight_index_ = *row;
                    }
                    return true;
                }
                if (hitTestGameBoxNamePlate(logical_x, logical_y)) {
                    toggleGameBoxDropdown();
                    return true;
                }
                closeGameBoxDropdown();
            }
        } else if (!game_box_space_mode_ && panels_reveal_ > 0.85 && ui_enter_ > 0.85 &&
                   hitTestGameBoxNamePlate(logical_x, logical_y)) {
            toggleGameBoxDropdown();
            return true;
        }
    }

    // Right (game) box navigation.
    if (game_save_box_viewport_ &&
        panels_reveal_ > 0.85 &&
        ui_enter_ > 0.85 &&
        !game_pc_boxes_.empty()) {
        int dir = 0;
        if (game_save_box_viewport_->hitTestPrevBoxArrow(logical_x, logical_y)) {
            dir = -1;
        } else if (game_save_box_viewport_->hitTestNextBoxArrow(logical_x, logical_y)) {
            dir = 1;
        }
        if (dir != 0) {
            advanceGameBox(dir);
            return true;
        }
    }

    if (panels_reveal_ > 0.02 && !carouselSlideAnimating() && hitTestToolCarousel(logical_x, logical_y)) {
        const int vx = carousel_style_.offset_from_left_wall;
        const int vw = carousel_style_.viewport_width;
        const int rel = logical_x - vx;
        if (rel * 2 < vw) {
            cycleToolCarousel(-1);
        } else {
            cycleToolCarousel(1);
        }
        play_button_sfx_requested_ = true;
        return true;
    }
    if (hitTestPillTrack(logical_x, logical_y)) {
        togglePillTarget();
        return true;
    }
    return false;
}

void TransferSystemScreen::handlePointerMoved(int logical_x, int logical_y) {
    if (game_box_space_mode_ && box_space_drag_active_ && game_save_box_viewport_) {
        const int dy = logical_y - box_space_drag_last_y_;
        box_space_drag_last_y_ = logical_y;
        box_space_drag_accum_ += static_cast<double>(dy);

        // Scroll threshold tuned by feel; avoids jitter.
        constexpr double kRowStepThresholdPx = 42.0;
        const int max_row = gameBoxSpaceMaxRowOffset();
        while (box_space_drag_accum_ >= kRowStepThresholdPx) {
            // Dragging down should reveal earlier rows (scroll up).
            if (game_box_space_row_offset_ > 0) {
                stepGameBoxSpaceRowUp();
            }
            box_space_drag_accum_ -= kRowStepThresholdPx;
            if (game_box_space_row_offset_ <= 0) {
                break;
            }
        }
        while (box_space_drag_accum_ <= -kRowStepThresholdPx) {
            if (game_box_space_row_offset_ < max_row) {
                stepGameBoxSpaceRowDown();
            }
            box_space_drag_accum_ += kRowStepThresholdPx;
            if (game_box_space_row_offset_ >= max_row) {
                break;
            }
        }
        return;
    }

    if (box_name_dropdown_style_.enabled && game_box_dropdown_open_target_ && game_box_dropdown_expand_t_ > 0.05 &&
        dropdown_lmb_down_in_panel_) {
        SDL_Rect outer{};
        int list_h = 0;
        int list_clip_y = 0;
        if (computeGameBoxDropdownOuterRect(outer, static_cast<float>(game_box_dropdown_expand_t_), list_h, list_clip_y)) {
            const int dy = logical_y - dropdown_lmb_last_y_;
            dropdown_lmb_last_y_ = logical_y;
            dropdown_lmb_drag_accum_ += std::fabs(static_cast<double>(dy));
            dropdown_scroll_px_ -= static_cast<double>(dy) * box_name_dropdown_style_.scroll_drag_multiplier;
            clampDropdownScroll(list_h);
        }
        return;
    }
    if (box_name_dropdown_style_.enabled && game_box_dropdown_open_target_ && game_box_dropdown_expand_t_ > 0.15 &&
        !dropdown_lmb_down_in_panel_) {
        if (const std::optional<int> row = dropdownRowIndexAtScreen(logical_x, logical_y)) {
            dropdown_highlight_index_ = *row;
            SDL_Rect outer{};
            int list_h = 0;
            int list_clip_y = 0;
            if (computeGameBoxDropdownOuterRect(outer, static_cast<float>(game_box_dropdown_expand_t_), list_h, list_clip_y)) {
                syncDropdownScrollToHighlight(list_h);
            }
        }
    }

    if (!(box_name_dropdown_style_.enabled && game_box_dropdown_open_target_ && game_box_dropdown_expand_t_ > 0.05) &&
        panels_reveal_ > 0.85 && ui_enter_ > 0.85) {
        if (const auto hit = speechBubbleTargetAtPointer(logical_x, logical_y)) {
            focus_.setCurrent(hit->first);
            // Mouse hover should not show the legacy yellow rectangle; keep "mouse mode" on.
            selection_cursor_hidden_after_mouse_ = true;
            speech_hover_active_ = true;
        } else {
            speech_hover_active_ = false;
        }
    }
}

bool TransferSystemScreen::handlePointerReleased(int logical_x, int logical_y) {
    if (box_space_drag_active_) {
        constexpr double kClickDragThresholdPx = 6.0;
        const bool treat_as_click = std::fabs(box_space_drag_accum_) < kClickDragThresholdPx;
        box_space_drag_active_ = false;
        box_space_drag_accum_ = 0.0;
        if (treat_as_click && game_box_space_mode_ && game_save_box_viewport_ && box_space_pressed_cell_ >= 0 &&
            box_space_pressed_cell_ < 30) {
            const int box_index = game_box_space_row_offset_ * 6 + box_space_pressed_cell_;
            box_space_pressed_cell_ = -1;
            if (box_index >= 0 && box_index < static_cast<int>(game_pc_boxes_.size())) {
                game_box_index_ = box_index;
                setGameBoxSpaceMode(false);
                play_button_sfx_requested_ = true;
            }
        } else {
            box_space_pressed_cell_ = -1;
        }
        return true;
    }
    if (!dropdown_lmb_down_in_panel_) {
        return false;
    }
    dropdown_lmb_down_in_panel_ = false;

    if (box_name_dropdown_style_.enabled && game_box_dropdown_open_target_ && game_box_dropdown_expand_t_ > 0.05 &&
        game_pc_boxes_.size() >= 2) {
        constexpr double kClickDragThresholdPx = 6.0;
        const bool treat_as_click = dropdown_lmb_drag_accum_ < kClickDragThresholdPx;
        dropdown_lmb_drag_accum_ = 0.0;

        if (treat_as_click) {
            SDL_Rect outer{};
            int list_h = 0;
            int list_clip_y = 0;
            if (computeGameBoxDropdownOuterRect(outer, static_cast<float>(game_box_dropdown_expand_t_), list_h, list_clip_y)) {
                const bool in_outer = logical_x >= outer.x && logical_x < outer.x + outer.w && logical_y >= outer.y &&
                                       logical_y < outer.y + outer.h;
                if (in_outer) {
                    if (const std::optional<int> row = dropdownRowIndexAtScreen(logical_x, logical_y)) {
                        dropdown_highlight_index_ = *row;
                        applyGameBoxDropdownSelection();
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

bool TransferSystemScreen::consumeButtonSfxRequest() {
    const bool requested = play_button_sfx_requested_;
    play_button_sfx_requested_ = false;
    return requested;
}

bool TransferSystemScreen::consumeUiMoveSfxRequest() {
    const bool requested = play_ui_move_sfx_requested_;
    play_ui_move_sfx_requested_ = false;
    return requested;
}

bool TransferSystemScreen::consumeReturnToTicketListRequest() {
    const bool requested = return_to_ticket_list_requested_;
    return_to_ticket_list_requested_ = false;
    return requested;
}

void TransferSystemScreen::requestReturnToTicketList() {
    return_to_ticket_list_requested_ = true;
}

void TransferSystemScreen::drawBottomBanner(SDL_Renderer* renderer) const {
    const int screen_w = window_config_.virtual_width;
    const int screen_h = window_config_.virtual_height;
    constexpr int help_h = 33;
    constexpr int stats_h = 75;
    constexpr int top_line_h = 4;
    const int total_h = help_h + stats_h + top_line_h;

    const Color c_help{191, 191, 191, 255};  // #BFBFBF
    const Color c_stats{224, 224, 224, 255}; // #E0E0E0
    const Color c_line{191, 191, 191, 255};  // #BFBFBF

    const int off = static_cast<int>(std::lround((1.0 - bottom_banner_reveal_) * static_cast<double>(total_h)));
    const int y0 = screen_h - total_h + off;

    // Thin line on top.
    SDL_Rect r_line{0, y0, screen_w, top_line_h};
    setDrawColor(renderer, c_line);
    SDL_RenderFillRect(renderer, &r_line);

    // Stats banner.
    SDL_Rect r_stats{0, y0 + top_line_h, screen_w, stats_h};
    setDrawColor(renderer, c_stats);
    SDL_RenderFillRect(renderer, &r_stats);

    // Button help bar at bottom.
    SDL_Rect r_help{0, y0 + top_line_h + stats_h, screen_w, help_h};
    setDrawColor(renderer, c_help);
    SDL_RenderFillRect(renderer, &r_help);
}

void TransferSystemScreen::drawBackground(SDL_Renderer* renderer) const {
    if (!background_.texture) {
        return;
    }

    SDL_SetTextureBlendMode(background_.texture.get(), SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(background_.texture.get(), 255);
    SDL_SetTextureColorMod(background_.texture.get(), 255, 255, 255);

    const double safe_scale = std::max(0.01, background_animation_.scale);
    const int width = std::max(1, static_cast<int>(std::round(
        static_cast<double>(background_.width) * safe_scale)));
    const int height = std::max(1, static_cast<int>(std::round(
        static_cast<double>(background_.height) * safe_scale)));

    if (!background_animation_.enabled ||
        (background_animation_.speed_x == 0.0 && background_animation_.speed_y == 0.0)) {
        SDL_Rect dst{0, 0, width, height};
        SDL_RenderCopy(renderer, background_.texture.get(), nullptr, &dst);
        return;
    }

    const int screen_width = window_config_.virtual_width;
    const int screen_height = window_config_.virtual_height;
    const int offset_x = static_cast<int>(std::floor(background_animation_.speed_x * elapsed_seconds_)) % width;
    const int offset_y = static_cast<int>(std::floor(background_animation_.speed_y * elapsed_seconds_)) % height;
    const int start_x = offset_x > 0 ? offset_x - width : offset_x;
    const int start_y = offset_y > 0 ? offset_y - height : offset_y;

    for (int y = start_y; y < screen_height; y += height) {
        for (int x = start_x; x < screen_width; x += width) {
            SDL_Rect dst{x, y, width, height};
            SDL_RenderCopy(renderer, background_.texture.get(), nullptr, &dst);
        }
    }
}

} // namespace pr
