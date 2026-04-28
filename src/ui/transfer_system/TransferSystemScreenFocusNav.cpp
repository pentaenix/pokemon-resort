#include "ui/TransferSystemScreen.hpp"

#include "ui/transfer_system/detail/SdlScanlineDraw.hpp"
#include "ui/transfer_system/detail/StringUtil.hpp"

#include <SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace pr {

using transfer_system::detail::drawRoundedOutlineScanlines;
using transfer_system::detail::fillRoundedRingScanlines;
using transfer_system::detail::fillThickSegmentScanlines;
using transfer_system::detail::fillTriangleScanlines;
using transfer_system::detail::asciiLowerCopy;

namespace {

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

std::string formatPokemonSpeechLine(const GameTransferSpeechBubbleCursorStyle& st, const PcSlotSpecies& slot) {
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

    const int tw = speech_bubble_label_tex_.width;
    const int th = speech_bubble_label_tex_.height;

    const int stroke = std::max(1, sb.border_thickness);
    const int gap = std::max(0, sb.gap_above_target);
    const int margin = std::max(0, sb.screen_margin);
    const int sw = window_config_.virtual_width;

    const int tip_x = target.x + target.w / 2;
    const int tip_y = target.y - gap;

    const int min_w_use = line.empty() ? sb.empty_min_width : sb.min_width;
    const int min_h_use = line.empty() ? sb.empty_min_height : sb.min_height;

    int tri_h = std::max(6, sb.triangle_height);
    const int pill_h = std::max(min_h_use, th + sb.padding_y * 2);
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

    // Pull triangle base up into pill so fill covers bottom border band.
    const int tri_base_y = pill_bottom_y - stroke;

    fillTriangleScanlines(renderer, bx0, tri_base_y, bx1, tri_base_y, tip_x, tip_y, sb.fill_color);
    // Border only on the two outer legs (no stroke along base).
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

    // Always draw bubble last so it sits above the outline.
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

} // namespace pr

