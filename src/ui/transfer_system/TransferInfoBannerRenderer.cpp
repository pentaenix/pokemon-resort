#include "ui/TransferSystemScreen.hpp"

#include "core/assets/Assets.hpp"
#include "ui/transfer_system/TransferInfoBannerPresenter.hpp"

#include <SDL_image.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace pr {

namespace {

constexpr int kInfoBannerLabelGap = 8;

fs::path resolvePath(const std::string& root, const std::string& configured) {
    fs::path path(configured);
    return path.is_absolute() ? path : (fs::path(root) / path);
}

bool isUtf8ContinuationByte(unsigned char c) {
    return (c & 0xC0) == 0x80;
}

std::vector<std::size_t> utf8CodepointEndOffsets(const std::string& text) {
    std::vector<std::size_t> offsets;
    offsets.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (i == 0 || !isUtf8ContinuationByte(static_cast<unsigned char>(text[i]))) {
            if (i > 0) {
                offsets.push_back(i);
            }
        }
    }
    offsets.push_back(text.size());
    return offsets;
}

void setDrawColor(SDL_Renderer* renderer, const Color& c) {
    SDL_SetRenderDrawColor(
        renderer,
        static_cast<Uint8>(c.r),
        static_cast<Uint8>(c.g),
        static_cast<Uint8>(c.b),
        static_cast<Uint8>(c.a));
}

void fillCircle(SDL_Renderer* renderer, int cx, int cy, int radius, const Color& color) {
    if (radius <= 0) {
        return;
    }
    setDrawColor(renderer, color);
    for (int dy = -radius; dy <= radius; ++dy) {
        const int dx = static_cast<int>(std::sqrt(static_cast<double>(radius * radius - dy * dy)));
        SDL_RenderDrawLine(renderer, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

void fillPolygon(SDL_Renderer* renderer, const std::vector<SDL_Point>& points, const Color& color) {
    if (points.size() < 3) {
        return;
    }

    int min_y = points.front().y;
    int max_y = points.front().y;
    for (const SDL_Point& p : points) {
        min_y = std::min(min_y, p.y);
        max_y = std::max(max_y, p.y);
    }

    setDrawColor(renderer, color);
    std::vector<int> intersections;
    intersections.reserve(points.size());
    for (int y = min_y; y <= max_y; ++y) {
        intersections.clear();
        for (std::size_t i = 0; i < points.size(); ++i) {
            const SDL_Point& a = points[i];
            const SDL_Point& b = points[(i + 1) % points.size()];
            if ((a.y <= y && b.y > y) || (b.y <= y && a.y > y)) {
                const double t = static_cast<double>(y - a.y) / static_cast<double>(b.y - a.y);
                intersections.push_back(static_cast<int>(std::lround(static_cast<double>(a.x) + t * (b.x - a.x))));
            }
        }
        std::sort(intersections.begin(), intersections.end());
        for (std::size_t i = 0; i + 1 < intersections.size(); i += 2) {
            SDL_RenderDrawLine(renderer, intersections[i], y, intersections[i + 1], y);
        }
    }
}

void drawMarkingIcon(SDL_Renderer* renderer, const std::string& icon_key, const SDL_Rect& dst) {
    Color color{145, 145, 145, 255};
    if (icon_key.find("_blue") != std::string::npos || icon_key.find("_on") != std::string::npos) {
        color = Color{38, 92, 214, 255};
    } else if (icon_key.find("_red") != std::string::npos) {
        color = Color{218, 54, 54, 255};
    }
    const std::string shape = icon_key.substr(0, icon_key.find('_'));
    const int cx = dst.x + dst.w / 2;
    const int cy = dst.y + dst.h / 2;
    int r = std::max(2, std::min(dst.w, dst.h) / 2 - 2);
    if (shape == "circle" || shape == "triangle" || shape == "square") {
        r = std::max(2, static_cast<int>(std::lround(static_cast<double>(r) * 0.78)));
    }

    if (shape == "circle") {
        fillCircle(renderer, cx, cy, r, color);
    } else if (shape == "triangle") {
        fillPolygon(renderer, {{cx, cy - r}, {cx - r, cy + r}, {cx + r, cy + r}}, color);
    } else if (shape == "square") {
        setDrawColor(renderer, color);
        SDL_Rect rect{cx - r, cy - r, r * 2, r * 2};
        SDL_RenderFillRect(renderer, &rect);
    } else if (shape == "heart") {
        const int lobe = std::max(2, r / 2);
        fillCircle(renderer, cx - lobe, cy - lobe / 2, lobe, color);
        fillCircle(renderer, cx + lobe, cy - lobe / 2, lobe, color);
        fillPolygon(renderer, {{cx - r, cy - lobe / 2}, {cx + r, cy - lobe / 2}, {cx, cy + r}}, color);
    } else if (shape == "star") {
        std::vector<SDL_Point> points;
        points.reserve(10);
        constexpr double pi = 3.14159265358979323846;
        for (int i = 0; i < 10; ++i) {
            const double angle = -pi / 2.0 + static_cast<double>(i) * pi / 5.0;
            const double radius = (i % 2 == 0) ? static_cast<double>(r) : static_cast<double>(r) * 0.42;
            points.push_back(SDL_Point{
                cx + static_cast<int>(std::lround(std::cos(angle) * radius)),
                cy + static_cast<int>(std::lround(std::sin(angle) * radius))});
        }
        fillPolygon(renderer, points, color);
    } else {
        fillPolygon(renderer, {{cx, cy - r}, {cx + r, cy}, {cx, cy + r}, {cx - r, cy}}, color);
    }
}

TextureHandle loadTextureOptional(SDL_Renderer* renderer, const fs::path& path) {
    TextureHandle texture;
    if (!fs::exists(path)) {
        return texture;
    }
    SDL_Texture* raw = IMG_LoadTexture(renderer, path.string().c_str());
    if (!raw) {
        std::cerr << "Warning: failed to load info banner texture: " << path.string() << " | " << IMG_GetError()
                  << '\n';
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

} // namespace

transfer_system::TransferInfoBannerContext TransferSystemScreen::activeInfoBannerContext() const {
    transfer_system::TransferInfoBannerContext context;
    context.source_game_key = current_game_key_;
    context.game_title = selection_game_title_;
    context.trainer_name = transfer_selection_.trainer_name;
    context.play_time = transfer_selection_.time;
    context.pokedex_seen = transfer_selection_.pokedex_seen;
    context.pokedex_caught = transfer_selection_.pokedex_caught;
    context.badges = transfer_selection_.badges;
    context.selected_tool_index = ui_state_.selectedToolIndex();
    context.items_mode = ui_state_.sliderT() >= 0.5;
    context.tooltip_copy = info_banner_style_;
    int resort_occ = 0;
    int resort_cap = 0;
    for (const auto& box : resort_pc_boxes_) {
        resort_cap += static_cast<int>(box.slots.size());
        for (const auto& s : box.slots) {
            if (s.occupied()) {
                ++resort_occ;
            }
        }
    }
    context.resort_storage_occupied_slots = resort_occ;
    context.resort_storage_total_slots = resort_cap;

    if (exit_save_modal_open_) {
        context.mode = "exit";
        context.tooltip_copy.exit_tooltip_title.clear();
        context.tooltip_copy.exit_tooltip_body =
            "Would you like to save your current Box settings and all changes that you've made?";
        return context;
    }
    if (const auto* held = pokemon_move_.held()) {
        context.mode = "pokemon";
        context.slot = &held->pokemon;
        return context;
    }
    if (multi_pokemon_move_.active() && !multi_pokemon_move_.entries().empty()) {
        context.mode = "pokemon";
        context.slot = &multi_pokemon_move_.entries().front().pokemon;
        return context;
    }

    if (const PcSlotSpecies* modal_slot = pokemonActionMenuPokemon()) {
        context.mode = "pokemon";
        context.slot = modal_slot;
        return context;
    }

    FocusNodeId focus_id = focus_.current();
    if (selection_cursor_hidden_after_mouse_) {
        if (mouse_hover_focus_node_ >= 0) {
            focus_id = mouse_hover_focus_node_;
        } else if (!speech_hover_active_) {
            context.mode = "empty";
            return context;
        }
    }
    if (focus_id == 1110) {
        context.mode = "box_space";
        return context;
    }
    if (focus_id == 2110) {
        context.mode = "box_space";
        return context;
    }
    if (focus_id == 1111) {
        context.mode = "resort_icon";
        return context;
    }
    if (focus_id == 2111) {
        context.mode = "game_icon";
        return context;
    }
    if (game_box_browser_.gameBoxSpaceMode() || game_box_browser_.dropdownOpenTarget() ||
        resort_box_browser_.gameBoxSpaceMode() || resort_box_browser_.dropdownOpenTarget()) {
        context.mode = "empty";
        return context;
    }
    if (focus_id >= 2000 && focus_id <= 2029) {
        const int slot_index = focus_id - 2000;
        const int box_index = game_box_browser_.gameBoxIndex();
        if (box_index >= 0 && box_index < static_cast<int>(game_pc_boxes_.size())) {
            const auto& slots = game_pc_boxes_[static_cast<std::size_t>(box_index)].slots;
            if (slot_index >= 0 && slot_index < static_cast<int>(slots.size())) {
                const PcSlotSpecies& slot = slots[static_cast<std::size_t>(slot_index)];
                if (slot.occupied()) {
                    context.mode = "pokemon";
                    context.slot = &slot;
                    return context;
                }
            }
        }
        context.mode = "empty";
        return context;
    }
    if (focus_id >= 1000 && focus_id <= 1029) {
        const int slot_index = focus_id - 1000;
        const int box_index = resort_box_browser_.gameBoxIndex();
        if (box_index >= 0 && box_index < static_cast<int>(resort_pc_boxes_.size())) {
            const auto& slots = resort_pc_boxes_[static_cast<std::size_t>(box_index)].slots;
            if (slot_index >= 0 && slot_index < static_cast<int>(slots.size())) {
                const PcSlotSpecies& slot = slots[static_cast<std::size_t>(slot_index)];
                if (slot.occupied()) {
                    context.mode = "pokemon";
                    context.slot = &slot;
                    return context;
                }
            }
        }
        context.mode = "empty";
        return context;
    }
    if (focus_id == 5000) {
        // Exit button: show a tooltip in the info banner (not a speech bubble).
        context.mode = "exit";
        return context;
    }
    if (focus_id == 3000) {
        context.mode = "tool";
        return context;
    }
    if (focus_id == 4000) {
        context.mode = "pill";
        return context;
    }
    context.mode = "empty";
    return context;
}

const PcSlotSpecies* TransferSystemScreen::activeInfoBannerPokemon() const {
    return activeInfoBannerContext().slot;
}

FontHandle TransferSystemScreen::infoBannerFont(int font_pt) const {
    const int key = std::max(8, font_pt);
    auto it = info_banner_font_cache_.find(key);
    if (it != info_banner_font_cache_.end()) {
        return it->second;
    }
    FontHandle font = loadFontPreferringUnicode(font_path_, key, project_root_);
    info_banner_font_cache_.emplace(key, font);
    return font;
}

TextureHandle TransferSystemScreen::infoBannerTextTexture(
    SDL_Renderer* renderer,
    int font_pt,
    const Color& color,
    const std::string& text) const {
    if (text.empty()) {
        return {};
    }
    const std::string key =
        std::to_string(std::max(8, font_pt)) + "|" + std::to_string(color.r) + "|" + std::to_string(color.g) + "|" +
        std::to_string(color.b) + "|" + text;
    auto it = info_banner_text_cache_.find(key);
    if (it != info_banner_text_cache_.end()) {
        return it->second;
    }
    FontHandle font = infoBannerFont(font_pt);
    TextureHandle texture = renderTextTexture(renderer, font.get(), text, color);
    info_banner_text_cache_.emplace(key, texture);
    return texture;
}

std::string TransferSystemScreen::infoBannerTextFitToReference(
    SDL_Renderer* renderer,
    int font_pt,
    const Color& color,
    const std::string& text,
    const std::string& reference_text) const {
    if (text.empty()) {
        return text;
    }

    const TextureHandle reference = infoBannerTextTexture(renderer, font_pt, color, reference_text);
    const TextureHandle full = infoBannerTextTexture(renderer, font_pt, color, text);
    if (!reference.texture || !full.texture || full.width <= reference.width) {
        return text;
    }

    const std::string ellipsis = "...";
    const TextureHandle ellipsis_texture = infoBannerTextTexture(renderer, font_pt, color, ellipsis);
    if (!ellipsis_texture.texture || ellipsis_texture.width >= reference.width) {
        return ellipsis;
    }

    const std::vector<std::size_t> offsets = utf8CodepointEndOffsets(text);
    std::size_t lo = 0;
    std::size_t hi = offsets.size();
    while (lo < hi) {
        const std::size_t mid = (lo + hi + 1) / 2;
        const std::size_t byte_count = mid == 0 ? 0 : offsets[mid - 1];
        const std::string candidate = text.substr(0, byte_count) + ellipsis;
        const TextureHandle texture = infoBannerTextTexture(renderer, font_pt, color, candidate);
        if (texture.texture && texture.width <= reference.width) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }

    const std::size_t byte_count = lo == 0 ? 0 : offsets[lo - 1];
    return text.substr(0, byte_count) + ellipsis;
}

TextureHandle TransferSystemScreen::infoBannerIconTexture(
    SDL_Renderer* renderer,
    const std::string& icon_group,
    const std::string& icon_key) const {
    const std::string normalized_key = icon_key.empty() ? "unknown" : icon_key;
    const fs::path base_dir = (icon_group == "game")
        ? resolvePath(project_root_, info_banner_style_.game_icon_directory)
        : resolvePath(project_root_, info_banner_style_.icon_directory);
    const fs::path candidate = base_dir / (normalized_key + ".png");
    const fs::path fallback = resolvePath(project_root_, info_banner_style_.unknown_icon);
    const fs::path chosen = fs::exists(candidate) ? candidate : fallback;
    const std::string cache_key = chosen.string();
    auto it = info_banner_icon_cache_.find(cache_key);
    if (it != info_banner_icon_cache_.end()) {
        return it->second;
    }
    TextureHandle texture = loadTextureOptional(renderer, chosen);
    info_banner_icon_cache_.emplace(cache_key, texture);
    return texture;
}

void TransferSystemScreen::drawBottomBanner(SDL_Renderer* renderer) const {
    if (!info_banner_style_.enabled) {
        return;
    }
    const int screen_w = window_config_.virtual_width;
    const int screen_h = window_config_.virtual_height;
    const int stats_h = std::max(0, info_banner_style_.info_height);
    const int top_line_h = std::max(0, info_banner_style_.separator_height);
    const int total_h = stats_h + top_line_h;

    const int off = static_cast<int>(std::lround((1.0 - ui_state_.bottomBannerReveal()) * static_cast<double>(total_h)));
    const int y0 = screen_h - total_h + off;

    SDL_Rect r_line{0, y0, screen_w, top_line_h};
    setDrawColor(renderer, info_banner_style_.separator_color);
    SDL_RenderFillRect(renderer, &r_line);

    SDL_Rect r_stats{0, y0 + top_line_h, screen_w, stats_h};
    setDrawColor(renderer, info_banner_style_.info_background_color);
    SDL_RenderFillRect(renderer, &r_stats);

    const auto context = activeInfoBannerContext();

    const int info_origin_y = y0 + top_line_h;
    std::unordered_map<std::string, int> flow_x;
    for (const auto& field : info_banner_style_.fields) {
        if (std::find(field.contexts.begin(), field.contexts.end(), context.mode) == field.contexts.end()) {
            continue;
        }
        const auto value = transfer_system::resolveTransferInfoBannerField(field.field, context);
        if (!value.visible && field.kind != "text") {
            continue;
        }

        int draw_x = field.x;
        const int draw_y = info_origin_y + field.y;
        if (field.kind == "icon") {
            const int width = field.width > 0 ? field.width : 32;
            const int height = field.height > 0 ? field.height : 32;
            if (!field.flow_group.empty()) {
                auto [it, inserted] = flow_x.emplace(field.flow_group, field.x);
                (void)inserted;
                draw_x = it->second;
                it->second += width + field.flow_gap;
            }
            SDL_Rect dst{draw_x, draw_y, width, height};

            if (value.icon_group == "marking") {
                drawMarkingIcon(renderer, value.icon_key, dst);
                continue;
            }
            if (value.icon_group == "gender-symbol") {
                const bool female = value.icon_key == "female";
                const std::string symbol = female ? u8"\u2640" : u8"\u2642";
                const Color symbol_color = female
                    ? info_banner_style_.gender_symbol_female_color
                    : info_banner_style_.gender_symbol_male_color;
                const int font_pt = info_banner_style_.gender_symbol_font_pt > 0
                    ? info_banner_style_.gender_symbol_font_pt
                    : std::max(8, dst.h + 4);
                TextureHandle symbol_texture = infoBannerTextTexture(renderer, font_pt, symbol_color, symbol);
                if (!symbol_texture.texture) {
                    continue;
                }
                const int tx = dst.x + (dst.w - symbol_texture.width) / 2 + info_banner_style_.gender_symbol_x_adjust;
                const int ty = dst.y + (dst.h - symbol_texture.height) / 2 + info_banner_style_.gender_symbol_y_adjust;
                SDL_Rect symbol_dst{tx, ty, symbol_texture.width, symbol_texture.height};
                SDL_RenderCopy(renderer, symbol_texture.texture.get(), nullptr, &symbol_dst);
                SDL_Rect bold_dst{tx + 1, ty, symbol_texture.width, symbol_texture.height};
                SDL_RenderCopy(renderer, symbol_texture.texture.get(), nullptr, &bold_dst);
                continue;
            }

            TextureHandle texture;
            if (value.use_pokesprite_item && value.pokesprite_item_id > 0 && sprite_assets_) {
                texture = sprite_assets_->loadItemTexture(renderer, value.pokesprite_item_id);
            }
            if (!texture.texture && value.icon_group.rfind("misc:", 0) == 0 && sprite_assets_) {
                texture = sprite_assets_->loadMiscTexture(renderer, value.icon_group.substr(5), value.icon_key);
            }
            if (!texture.texture) {
                texture = infoBannerIconTexture(renderer, value.icon_group, value.icon_key);
            }
            if (!texture.texture) {
                continue;
            }
            dst.w = field.width > 0 ? field.width : texture.width;
            dst.h = field.height > 0 ? field.height : texture.height;
            SDL_RenderCopy(renderer, texture.texture.get(), nullptr, &dst);
            continue;
        }

        std::string text_value = value.text.empty() ? field.empty_text : value.text;
        if (field.field == "nickname") {
            text_value = infoBannerTextFitToReference(renderer, field.font_pt, field.color, text_value, "Abomasnow00");
        }
        if (field.label.empty()) {
            TextureHandle text_texture = infoBannerTextTexture(renderer, field.font_pt, field.color, text_value);
            if (!text_texture.texture) {
                continue;
            }
            SDL_Rect dst{draw_x, draw_y, text_texture.width, text_texture.height};
            if (field.width > 0) {
                dst.w = std::min(dst.w, field.width);
            }
            if (field.height > 0) {
                dst.h = std::min(dst.h, field.height);
            }
            SDL_RenderCopy(renderer, text_texture.texture.get(), nullptr, &dst);
            continue;
        }

        const std::string label_text = field.label + ":";
        TextureHandle label_texture =
            infoBannerTextTexture(renderer, field.label_font_pt, field.label_color, label_text);
        TextureHandle value_texture =
            infoBannerTextTexture(renderer, field.font_pt, field.color, text_value);
        if (!label_texture.texture || !value_texture.texture) {
            continue;
        }

        SDL_Rect label_dst{draw_x, draw_y, label_texture.width, label_texture.height};
        SDL_Rect value_dst{
            draw_x + label_texture.width + kInfoBannerLabelGap,
            draw_y,
            value_texture.width,
            value_texture.height};
        if (field.height > 0) {
            label_dst.h = std::min(label_dst.h, field.height);
            value_dst.h = std::min(value_dst.h, field.height);
        }
        SDL_RenderCopy(renderer, label_texture.texture.get(), nullptr, &label_dst);
        SDL_Rect bold_label_dst{label_dst.x + 1, label_dst.y, label_dst.w, label_dst.h};
        SDL_RenderCopy(renderer, label_texture.texture.get(), nullptr, &bold_label_dst);
        SDL_RenderCopy(renderer, value_texture.texture.get(), nullptr, &value_dst);
    }
}

#ifdef PR_ENABLE_TEST_HOOKS
std::string TransferSystemScreen::debugInfoBannerMode() const {
    return activeInfoBannerContext().mode;
}
#endif

} // namespace pr
