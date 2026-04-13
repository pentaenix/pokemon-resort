#include "ui/TransferTicketScreen.hpp"

#include "core/Font.hpp"
#include "core/Json.hpp"

#include <SDL_image.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace pr {

namespace {

constexpr double kPi = 3.14159265358979323846;

const Color kTitleColor{0x1f, 0x1f, 0x1f, 255};
const Color kTrainerColor{0x5e, 0x5e, 0x5e, 255};
const Color kDataLabelColor{0x5e, 0x5e, 0x5e, 255};
const Color kDataValueColor{0x46, 0x46, 0x46, 255};
const Color kBoardingPassColor{0xe9, 0xe6, 0xe3, 255};
const Color kBoardingPassDarkColor{0x1f, 0x1f, 0x1f, 255};
const Color kRubyFallback{0xB3, 0x3A, 0x32, 255};

fs::path resolvePath(const std::string& root, const std::string& configured) {
    fs::path path(configured);
    return path.is_absolute() ? path : (fs::path(root) / path);
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

FontHandle loadStyledFont(const std::string& font_path, int pt_size, int style, const std::string& project_root) {
    FontHandle font = loadFont(font_path, pt_size, project_root);
    TTF_SetFontStyle(font.get(), style);
    return font;
}

int clampChannel(int value) {
    return std::max(0, std::min(255, value));
}

Color parseHexColor(const std::string& value) {
    if (value.size() != 7 || value[0] != '#') {
        throw std::runtime_error("Expected color in #RRGGBB format");
    }

    const auto parse_component = [&](std::size_t offset) -> int {
        return std::stoi(value.substr(offset, 2), nullptr, 16);
    };

    return Color{
        clampChannel(parse_component(1)),
        clampChannel(parse_component(3)),
        clampChannel(parse_component(5)),
        255
    };
}

int intFromObjectOrDefault(const JsonValue& obj, const std::string& key, int fallback) {
    const JsonValue* value = obj.get(key);
    return value ? static_cast<int>(value->asNumber()) : fallback;
}

double doubleFromObjectOrDefault(const JsonValue& obj, const std::string& key, double fallback) {
    const JsonValue* value = obj.get(key);
    return value ? value->asNumber() : fallback;
}

bool boolFromObjectOrDefault(const JsonValue& obj, const std::string& key, bool fallback) {
    const JsonValue* value = obj.get(key);
    return value ? value->asBool() : fallback;
}

double clamp01(double value) {
    return std::max(0.0, std::min(1.0, value));
}

std::string stringFromObjectOrDefault(const JsonValue& obj, const std::string& key, const std::string& fallback) {
    const JsonValue* value = obj.get(key);
    return value ? value->asString() : fallback;
}

void applyPointFromObject(Point& out, const JsonValue& obj) {
    out.x = intFromObjectOrDefault(obj, "x", out.x);
    out.y = intFromObjectOrDefault(obj, "y", out.y);
}

std::string spriteFilenameForPartyName(const std::string& party_name) {
    fs::path sprite_path(party_name);
    if (sprite_path.has_extension()) {
        return party_name;
    }
    return party_name + ".png";
}

bool isLightTicketColor(const Color& color) {
    const double luminance =
        0.2126 * static_cast<double>(color.r) +
        0.7152 * static_cast<double>(color.g) +
        0.0722 * static_cast<double>(color.b);
    return luminance >= 190.0;
}

} // namespace

TransferTicketScreen::TransferTicketScreen(
    SDL_Renderer* renderer,
    const WindowConfig& window_config,
    const std::string& font_path,
    const std::string& project_root)
    : window_config_(window_config),
      font_path_(font_path),
      project_root_(project_root) {
    const fs::path root = resolvePath(project_root_, "assets/transfer_select_save");
    assets_.background = loadTexture(renderer, root / "background.png");
    assets_.banner = loadTexture(renderer, root / "transfer_top_banner.png");
    assets_.backdrop = loadTexture(renderer, root / "backdrop.png");
    assets_.main_left = loadTexture(renderer, root / "main_left.png");
    assets_.main_right = loadTexture(renderer, root / "main_right.png");
    assets_.color_left = loadTexture(renderer, root / "color_left.png");
    assets_.color_right = loadTexture(renderer, root / "color_right.png");
    assets_.game_icon_back = loadTexture(renderer, root / "game_icon_back.png");
    assets_.game_icon_front = loadTexture(renderer, root / "game_icon_front.png");
    assets_.icon_boat = loadTexture(renderer, root / "icon_boat.png");

    loadTransferConfig();
    fonts_.banner_title = loadStyledFont(font_path_, screen_header_.title_font_size, TTF_STYLE_NORMAL, project_root_);
    fonts_.banner_subtitle = loadStyledFont(font_path_, screen_header_.subtitle_font_size, TTF_STYLE_NORMAL, project_root_);
    fonts_.title = loadStyledFont(font_path_, layout_.font_sizes.title, TTF_STYLE_NORMAL, project_root_);
    fonts_.trainer = loadStyledFont(font_path_, layout_.font_sizes.trainer, TTF_STYLE_NORMAL, project_root_);
    fonts_.data_label = loadStyledFont(font_path_, layout_.font_sizes.data_label, TTF_STYLE_NORMAL, project_root_);
    fonts_.data_value = loadStyledFont(font_path_, layout_.font_sizes.data_value, TTF_STYLE_NORMAL, project_root_);
    fonts_.boarding_pass = loadStyledFont(font_path_, layout_.font_sizes.boarding_pass, TTF_STYLE_BOLD, project_root_);
    buildScreenTextTextures(renderer);
}

void TransferTicketScreen::enter() {
    const auto count = static_cast<std::size_t>(ticketCount());
    right_stub_offsets_.assign(count, 0);
    rip_elapsed_seconds_.assign(count, 0.0);
    rip_animation_active_.assign(count, false);
    ripped_.assign(count, false);
    play_rip_sfx_requested_ = false;
    open_transfer_system_requested_ = false;
    fade_to_black_active_ = false;
    fade_to_black_elapsed_seconds_ = 0.0;
    pointer_pressed_on_ticket_ = false;
    pointer_pressed_ticket_index_ = -1;
    selected_ticket_index_ = count > 0 ? 0 : -1;
    activating_ticket_index_ = -1;
    scroll_offset_y_ = 0.0;
    target_scroll_offset_y_ = 0.0;
    return_to_main_menu_requested_ = false;
}

void TransferTicketScreen::setSaveSelections(
    SDL_Renderer* renderer,
    const std::vector<TransferSaveSelection>& selections) {
    try {
        buildTextTextures(renderer, selections);
    } catch (const std::exception& ex) {
        std::cerr << "Warning: failed to build transfer save tickets: "
                  << ex.what() << '\n';
        tickets_.clear();
    }
    const auto count = static_cast<std::size_t>(ticketCount());
    selected_ticket_index_ = count > 0 ? 0 : -1;
    activating_ticket_index_ = -1;
    scroll_offset_y_ = 0.0;
    target_scroll_offset_y_ = 0.0;
    right_stub_offsets_.assign(count, 0);
    rip_elapsed_seconds_.assign(count, 0.0);
    rip_animation_active_.assign(count, false);
    ripped_.assign(count, false);
}

void TransferTicketScreen::update(double dt) {
    elapsed_seconds_ += dt;

    const double scroll_speed = std::max(0.0, list_layout_.scroll_speed);
    if (scroll_speed <= 0.0) {
        scroll_offset_y_ = target_scroll_offset_y_;
    } else {
        const double scroll_alpha = 1.0 - std::exp(-scroll_speed * std::max(0.0, dt));
        scroll_offset_y_ += (target_scroll_offset_y_ - scroll_offset_y_) * scroll_alpha;
        if (std::abs(target_scroll_offset_y_ - scroll_offset_y_) < 0.1) {
            scroll_offset_y_ = target_scroll_offset_y_;
        }
    }

    if (fade_to_black_active_) {
        fade_to_black_elapsed_seconds_ += dt;
        if (selection_transition_.fade_to_black_seconds <= 0.0 ||
            fade_to_black_elapsed_seconds_ >= selection_transition_.fade_to_black_seconds) {
            fade_to_black_active_ = false;
            open_transfer_system_requested_ = true;
        }
    }

    const double pre_tug_duration = std::max(0.0, rip_animation_.pre_tug_duration_seconds);
    const double rip_duration = std::max(0.001, rip_animation_.duration_seconds);
    const double total_duration = pre_tug_duration + rip_duration;
    for (std::size_t i = 0; i < rip_animation_active_.size(); ++i) {
        if (!rip_animation_active_[i]) {
            continue;
        }

        rip_elapsed_seconds_[i] += dt;
        if (pre_tug_duration > 0.0 && rip_elapsed_seconds_[i] < pre_tug_duration) {
            const double t = std::min(1.0, rip_elapsed_seconds_[i] / pre_tug_duration);
            const double tug = std::sin(t * kPi);
            right_stub_offsets_[i] = -static_cast<int>(std::round(
                static_cast<double>(rip_animation_.pre_tug_distance) * tug));
            continue;
        }

        const double t = std::min(1.0, (rip_elapsed_seconds_[i] - pre_tug_duration) / rip_duration);
        const double eased = 1.0 - ((1.0 - t) * (1.0 - t));
        const double start = -static_cast<double>(rip_animation_.pre_tug_distance);
        const double end = static_cast<double>(rip_animation_.distance);
        right_stub_offsets_[i] = static_cast<int>(std::round(start + (end - start) * eased));
        if (rip_elapsed_seconds_[i] >= total_duration) {
            right_stub_offsets_[i] = rip_animation_.distance;
            rip_animation_active_[i] = false;
            ripped_[i] = true;
            if (static_cast<int>(i) == activating_ticket_index_ &&
                activating_ticket_index_ >= 0 &&
                activating_ticket_index_ < static_cast<int>(tickets_.size())) {
                beginOrCompleteHandoff();
            }
        }
    }
}

void TransferTicketScreen::render(SDL_Renderer* renderer) const {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    drawTextureTopLeft(renderer, assets_.background, 0, 0);
    drawTextureTopLeft(renderer, assets_.backdrop, 0, 0);

    SDL_Rect clip{
        sx(list_layout_.viewport.x),
        sy(list_layout_.viewport.y),
        sx(list_layout_.viewport.w),
        sy(list_layout_.viewport.h)
    };
    SDL_RenderSetClipRect(renderer, &clip);
    const int rounded_scroll_offset_y = static_cast<int>(std::round(scroll_offset_y_));
    for (int i = 0; i < ticketCount(); ++i) {
        renderTicket(
            renderer,
            i,
            list_layout_.start.x,
            list_layout_.start.y + i * list_layout_.separation_y - rounded_scroll_offset_y,
            i == selected_ticket_index_);
    }

    SDL_RenderSetClipRect(renderer, nullptr);

    drawTextureTopLeft(renderer, assets_.banner, 0, 0);
    drawTextureCentered(renderer, screen_text_.title, screen_header_.title_center.x, screen_header_.title_center.y);
    drawTextureCentered(renderer, screen_text_.subtitle, screen_header_.subtitle_center.x, screen_header_.subtitle_center.y);

    if (fade_to_black_active_ && selection_transition_.fade_to_black_seconds > 0.0) {
        const double t = clamp01(fade_to_black_elapsed_seconds_ / selection_transition_.fade_to_black_seconds);
        const int alpha = static_cast<int>(std::round(
            static_cast<double>(selection_transition_.fade_to_black_max_alpha) * t));
        if (alpha > 0) {
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(
                renderer,
                0,
                0,
                0,
                static_cast<Uint8>(std::max(0, std::min(255, alpha))));
            SDL_Rect overlay{0, 0, sx(window_config_.design_width), sy(window_config_.design_height)};
            SDL_RenderFillRect(renderer, &overlay);
        }
    }
}

void TransferTicketScreen::onNavigate(int delta) {
    const int count = ticketCount();
    if (count <= 0 || delta == 0) {
        return;
    }
    if (activating_ticket_index_ >= 0) {
        return;
    }

    const int previous = selected_ticket_index_;
    selected_ticket_index_ = (selected_ticket_index_ + delta) % count;
    if (selected_ticket_index_ < 0) {
        selected_ticket_index_ += count;
    }
    if (selected_ticket_index_ != previous) {
        requestButtonSfx();
        updateScrollOffset();
    }
}

void TransferTicketScreen::onAdvancePressed() {
    if (selected_ticket_index_ < 0 ||
        selected_ticket_index_ >= static_cast<int>(ripped_.size()) ||
        rip_animation_active_[static_cast<std::size_t>(selected_ticket_index_)] ||
        ripped_[static_cast<std::size_t>(selected_ticket_index_)]) {
        return;
    }

    activating_ticket_index_ = selected_ticket_index_;
    beginRipForActivatingTicket();
}

void TransferTicketScreen::onBackPressed() {
    requestButtonSfx();
    return_to_main_menu_requested_ = true;
}

void TransferTicketScreen::handlePointerMoved(int logical_x, int logical_y) {
    (void)logical_x;
    if (!pointer_pressed_on_ticket_ || activating_ticket_index_ >= 0) {
        return;
    }

    constexpr int kDragThresholdPixels = 4;
    const int delta_y = logical_y - pointer_press_y_;
    if (!pointer_dragging_list_ && std::abs(delta_y) >= kDragThresholdPixels) {
        pointer_dragging_list_ = true;
    }
    if (!pointer_dragging_list_) {
        return;
    }

    pointer_last_y_ = logical_y;
    target_scroll_offset_y_ = std::max(
        0.0,
        std::min(maxScrollOffset(), pointer_press_scroll_offset_y_ - static_cast<double>(delta_y)));
    scroll_offset_y_ = target_scroll_offset_y_;
}

bool TransferTicketScreen::handlePointerPressed(int logical_x, int logical_y) {
    pointer_pressed_on_ticket_ = false;
    pointer_dragging_list_ = false;
    pointer_pressed_ticket_index_ = -1;
    if (activating_ticket_index_ >= 0) {
        return false;
    }
    const bool inside_viewport =
        logical_x >= list_layout_.viewport.x &&
        logical_x < list_layout_.viewport.x + list_layout_.viewport.w &&
        logical_y >= list_layout_.viewport.y &&
        logical_y < list_layout_.viewport.y + list_layout_.viewport.h;
    if (!inside_viewport) {
        return false;
    }

    pointer_pressed_on_ticket_ = true;
    pointer_press_y_ = logical_y;
    pointer_last_y_ = logical_y;
    pointer_press_scroll_offset_y_ = scroll_offset_y_;
    for (int i = ticketCount() - 1; i >= 0; --i) {
        if (pointInTicket(logical_x, logical_y, i)) {
            pointer_pressed_ticket_index_ = i;
            break;
        }
    }
    return true;
}

bool TransferTicketScreen::handlePointerReleased(int logical_x, int logical_y) {
    const bool started_on_ticket = pointer_pressed_on_ticket_;
    const int pressed_ticket_index = pointer_pressed_ticket_index_;
    const bool dragged_list = pointer_dragging_list_;
    pointer_pressed_on_ticket_ = false;
    pointer_dragging_list_ = false;
    pointer_pressed_ticket_index_ = -1;
    if (!started_on_ticket) {
        return false;
    }
    if (dragged_list) {
        return true;
    }

    if (pressed_ticket_index < 0 || !pointInTicket(logical_x, logical_y, pressed_ticket_index)) {
        return false;
    }

    selected_ticket_index_ = pressed_ticket_index;
    onAdvancePressed();
    return true;
}

bool TransferTicketScreen::consumeButtonSfxRequest() {
    const bool requested = play_button_sfx_requested_;
    play_button_sfx_requested_ = false;
    return requested;
}

bool TransferTicketScreen::consumeRipSfxRequest() {
    const bool requested = play_rip_sfx_requested_;
    play_rip_sfx_requested_ = false;
    return requested;
}

bool TransferTicketScreen::consumeReturnToMainMenuRequest() {
    const bool requested = return_to_main_menu_requested_;
    return_to_main_menu_requested_ = false;
    return requested;
}

bool TransferTicketScreen::consumeOpenTransferSystemRequest(TransferSaveSelection& out_selection) {
    const bool requested = open_transfer_system_requested_;
    if (requested) {
        out_selection = selected_transfer_save_;
    }
    open_transfer_system_requested_ = false;
    return requested;
}

const std::string& TransferTicketScreen::musicPath() const {
    return music_.path;
}

double TransferTicketScreen::musicSilenceSeconds() const {
    return music_.silence_seconds;
}

double TransferTicketScreen::musicFadeInSeconds() const {
    return music_.fade_in_seconds;
}

void TransferTicketScreen::requestButtonSfx() {
    play_button_sfx_requested_ = true;
}

void TransferTicketScreen::beginRipForActivatingTicket() {
    if (activating_ticket_index_ < 0 ||
        activating_ticket_index_ >= static_cast<int>(ripped_.size())) {
        return;
    }

    const auto index = static_cast<std::size_t>(activating_ticket_index_);
    play_rip_sfx_requested_ = true;
    if (rip_animation_.enabled && rip_animation_.duration_seconds > 0.0) {
        rip_elapsed_seconds_[index] = 0.0;
        rip_animation_active_[index] = true;
    } else {
        right_stub_offsets_[index] = rip_animation_.distance;
        ripped_[index] = true;
        beginOrCompleteHandoff();
    }
}

void TransferTicketScreen::beginOrCompleteHandoff() {
    if (activating_ticket_index_ < 0 ||
        activating_ticket_index_ >= static_cast<int>(tickets_.size())) {
        return;
    }

    selected_transfer_save_ = tickets_[static_cast<std::size_t>(activating_ticket_index_)].data;
    if (selection_transition_.fade_to_black_seconds > 0.0 &&
        selection_transition_.fade_to_black_max_alpha > 0) {
        fade_to_black_elapsed_seconds_ = 0.0;
        fade_to_black_active_ = true;
    } else {
        open_transfer_system_requested_ = true;
    }
}

void TransferTicketScreen::loadTransferConfig() {
    const fs::path path = resolvePath(project_root_, "config/transfer_select_save.json");
    if (!fs::exists(path)) {
        return;
    }

    JsonValue root = parseJsonFile(path.string());
    if (!root.isObject()) {
        throw std::runtime_error("transfer_select_save.json root must be an object");
    }

    const JsonValue* ticket_config = root.get("ticket");
    const JsonValue* transfer_screen_config = root.get("transfer_screen");

    if (ticket_config) {
        if (const JsonValue* font_sizes = ticket_config->get("font_sizes")) {
            layout_.font_sizes.title = intFromObjectOrDefault(*font_sizes, "title", layout_.font_sizes.title);
            layout_.font_sizes.trainer = intFromObjectOrDefault(*font_sizes, "trainer", layout_.font_sizes.trainer);
            layout_.font_sizes.data_label = intFromObjectOrDefault(*font_sizes, "data_label", layout_.font_sizes.data_label);
            layout_.font_sizes.data_value = intFromObjectOrDefault(*font_sizes, "data_value", layout_.font_sizes.data_value);
            layout_.font_sizes.boarding_pass = intFromObjectOrDefault(*font_sizes, "boarding_pass", layout_.font_sizes.boarding_pass);
        }

        if (const JsonValue* layout = ticket_config->get("layout")) {
            if (const JsonValue* boarding_pass = layout->get("boarding_pass")) {
                applyPointFromObject(layout_.boarding_pass, *boarding_pass);
                layout_.boarding_pass_angle = doubleFromObjectOrDefault(
                    *boarding_pass,
                    "angle_degrees",
                    layout_.boarding_pass_angle);
            }
            if (const JsonValue* game_title = layout->get("game_title")) applyPointFromObject(layout_.game_title, *game_title);
            if (const JsonValue* trainer_name = layout->get("trainer_name")) applyPointFromObject(layout_.trainer_name, *trainer_name);
            if (const JsonValue* party = layout->get("party")) {
                if (const JsonValue* start = party->get("start")) applyPointFromObject(layout_.party_start, *start);
                layout_.party_count = intFromObjectOrDefault(*party, "count", layout_.party_count);
                layout_.party_spacing = intFromObjectOrDefault(*party, "spacing", layout_.party_spacing);
                layout_.party_scale = doubleFromObjectOrDefault(*party, "scale", layout_.party_scale);
            }
            if (const JsonValue* stats = layout->get("stats")) {
                if (const JsonValue* origin = stats->get("origin")) applyPointFromObject(layout_.stats_origin, *origin);
                if (const JsonValue* pokedex_label = stats->get("pokedex_label")) applyPointFromObject(layout_.pokedex_label, *pokedex_label);
                if (const JsonValue* pokedex_value = stats->get("pokedex_value")) applyPointFromObject(layout_.pokedex_value, *pokedex_value);
                if (const JsonValue* time_label = stats->get("time_label")) applyPointFromObject(layout_.time_label, *time_label);
                if (const JsonValue* time_value = stats->get("time_value")) applyPointFromObject(layout_.time_value, *time_value);
                if (const JsonValue* badges_label = stats->get("badges_label")) applyPointFromObject(layout_.badges_label, *badges_label);
                if (const JsonValue* badges_value = stats->get("badges_value")) applyPointFromObject(layout_.badges_value, *badges_value);
            }
        }

        if (const JsonValue* selection = ticket_config->get("selection")) {
            selection_.enabled = boolFromObjectOrDefault(*selection, "enabled", selection_.enabled);
            selection_.border_color = parseHexColor(stringFromObjectOrDefault(*selection, "border_color", "#F4CD48"));
            selection_.border_alpha = intFromObjectOrDefault(*selection, "border_alpha", selection_.border_alpha);
            selection_.border_thickness = intFromObjectOrDefault(*selection, "border_thickness", selection_.border_thickness);
            selection_.border_padding = intFromObjectOrDefault(*selection, "border_padding", selection_.border_padding);
            selection_.border_radius = intFromObjectOrDefault(*selection, "border_radius", selection_.border_radius);
            selection_.beat_speed = doubleFromObjectOrDefault(*selection, "beat_speed", selection_.beat_speed);
            selection_.beat_magnitude = doubleFromObjectOrDefault(*selection, "beat_magnitude", selection_.beat_magnitude);
        }

        if (const JsonValue* rip = ticket_config->get("rip_animation")) {
            rip_animation_.enabled = boolFromObjectOrDefault(*rip, "enabled", rip_animation_.enabled);
            rip_animation_.distance = intFromObjectOrDefault(*rip, "distance", rip_animation_.distance);
            rip_animation_.pre_tug_distance = intFromObjectOrDefault(
                *rip,
                "pre_tug_distance",
                rip_animation_.pre_tug_distance);
            rip_animation_.pre_tug_duration_seconds = doubleFromObjectOrDefault(
                *rip,
                "pre_tug_duration_seconds",
                rip_animation_.pre_tug_duration_seconds);
            rip_animation_.duration_seconds = doubleFromObjectOrDefault(
                *rip,
                "duration_seconds",
                rip_animation_.duration_seconds);
            rip_animation_.rotation_degrees = doubleFromObjectOrDefault(
                *rip,
                "rotation_degrees",
                rip_animation_.rotation_degrees);
            if (const JsonValue* rotation_pivot = rip->get("rotation_pivot")) {
                applyPointFromObject(rip_animation_.rotation_pivot, *rotation_pivot);
            }
        }

        if (const JsonValue* transition = ticket_config->get("selection_transition")) {
            selection_transition_.fade_to_black_seconds = doubleFromObjectOrDefault(
                *transition,
                "fade_to_black_seconds",
                selection_transition_.fade_to_black_seconds);
            selection_transition_.fade_to_black_max_alpha = intFromObjectOrDefault(
                *transition,
                "fade_to_black_max_alpha",
                selection_transition_.fade_to_black_max_alpha);
        }
    }

    if (transfer_screen_config) {
        if (const JsonValue* header = transfer_screen_config->get("header")) {
            screen_header_.title = stringFromObjectOrDefault(*header, "title", screen_header_.title);
            screen_header_.subtitle = stringFromObjectOrDefault(*header, "subtitle", screen_header_.subtitle);
            screen_header_.title_font_size = intFromObjectOrDefault(
                *header,
                "title_font_size",
                screen_header_.title_font_size);
            screen_header_.subtitle_font_size = intFromObjectOrDefault(
                *header,
                "subtitle_font_size",
                screen_header_.subtitle_font_size);
            screen_header_.title_color = parseHexColor(
                stringFromObjectOrDefault(*header, "title_color", "#1F1F1F"));
            screen_header_.subtitle_color = parseHexColor(
                stringFromObjectOrDefault(*header, "subtitle_color", "#5E5E5E"));
            if (const JsonValue* title_center = header->get("title_center")) {
                applyPointFromObject(screen_header_.title_center, *title_center);
            }
            if (const JsonValue* subtitle_center = header->get("subtitle_center")) {
                applyPointFromObject(screen_header_.subtitle_center, *subtitle_center);
            }
        }
        if (const JsonValue* list = transfer_screen_config->get("list")) {
            if (const JsonValue* start = list->get("start")) {
                applyPointFromObject(list_layout_.start, *start);
            }
            list_layout_.separation_y = intFromObjectOrDefault(*list, "separation_y", list_layout_.separation_y);
            list_layout_.scroll_speed = doubleFromObjectOrDefault(*list, "scroll_speed", list_layout_.scroll_speed);
            if (const JsonValue* viewport = list->get("viewport")) {
                list_layout_.viewport.x = intFromObjectOrDefault(*viewport, "x", list_layout_.viewport.x);
                list_layout_.viewport.y = intFromObjectOrDefault(*viewport, "y", list_layout_.viewport.y);
                list_layout_.viewport.w = intFromObjectOrDefault(*viewport, "w", list_layout_.viewport.w);
                list_layout_.viewport.h = intFromObjectOrDefault(*viewport, "h", list_layout_.viewport.h);
            }
        }
        if (const JsonValue* audio = transfer_screen_config->get("audio")) {
            music_.path = stringFromObjectOrDefault(*audio, "music", music_.path);
            music_.silence_seconds = std::max(
                0.0,
                doubleFromObjectOrDefault(*audio, "silence_seconds", music_.silence_seconds));
            music_.fade_in_seconds = std::max(
                0.0,
                doubleFromObjectOrDefault(*audio, "fade_in_seconds", music_.fade_in_seconds));
        }
    }

    const JsonValue* palette = root.get("palette");
    if (!palette || !palette->isObject()) {
        return;
    }

    const JsonValue* game_colors = palette->get("game_colors");
    if (!game_colors || !game_colors->isObject()) {
        return;
    }

    for (const auto& entry : game_colors->asObject()) {
        if (!entry.second.isString()) {
            continue;
        }
        game_palette_[entry.first] = parseHexColor(entry.second.asString());
    }
}

void TransferTicketScreen::buildScreenTextTextures(SDL_Renderer* renderer) {
    screen_text_.title = renderTextTexture(
        renderer,
        fonts_.banner_title.get(),
        screen_header_.title,
        screen_header_.title_color);
    screen_text_.subtitle = renderTextTexture(
        renderer,
        fonts_.banner_subtitle.get(),
        screen_header_.subtitle,
        screen_header_.subtitle_color);
}

void TransferTicketScreen::buildTextTextures(
    SDL_Renderer* renderer,
    const std::vector<TransferSaveSelection>& selections) {
    std::vector<TicketEntry> built_tickets;
    built_tickets.reserve(selections.size());
    const fs::path sprite_root = resolvePath(project_root_, "assets/sprites");
    for (const TransferSaveSelection& selection : selections) {
        try {
            TicketEntry ticket;
            ticket.data = selection;
            ticket.game_color = colorForGame(selection.game_key, kRubyFallback);
            ticket.text.game_title = renderTextTexture(renderer, fonts_.title.get(), selection.game_title, kTitleColor);
            ticket.text.trainer_name = renderTextTexture(renderer, fonts_.trainer.get(), selection.trainer_name, kTrainerColor);
            ticket.text.time_label = renderTextTexture(renderer, fonts_.data_label.get(), "Time:", kDataLabelColor);
            ticket.text.time_value = renderTextTexture(renderer, fonts_.data_value.get(), selection.time, kDataValueColor);
            ticket.text.badges_label = renderTextTexture(renderer, fonts_.data_label.get(), "Badges:", kDataLabelColor);
            ticket.text.badges_value = renderTextTexture(renderer, fonts_.data_value.get(), selection.badges, kDataValueColor);
            ticket.text.pokedex_label = renderTextTexture(renderer, fonts_.data_label.get(), "Pokedex:", kDataLabelColor);
            ticket.text.pokedex_value = renderTextTexture(renderer, fonts_.data_value.get(), selection.pokedex, kDataValueColor);
            const Color boarding_pass_color = isLightTicketColor(ticket.game_color) ? kBoardingPassDarkColor : kBoardingPassColor;
            ticket.text.boarding_pass = renderTextTexture(renderer, fonts_.boarding_pass.get(), "Boarding Pass", boarding_pass_color);

            ticket.party_sprites.reserve(selection.party_sprites.size());
            for (const std::string& filename : selection.party_sprites) {
                const fs::path path = sprite_root / spriteFilenameForPartyName(filename);
                if (fs::exists(path)) {
                    try {
                        ticket.party_sprites.push_back(loadTexture(renderer, path));
                    } catch (const std::exception& ex) {
                        std::cerr << "Warning: skipping transfer party sprite "
                                  << path << ": " << ex.what() << '\n';
                    }
                }
            }
            built_tickets.push_back(std::move(ticket));
        } catch (const std::exception& ex) {
            std::cerr << "Warning: skipping transfer save ticket for file "
                      << selection.source_filename << ": " << ex.what() << '\n';
        }
    }
    tickets_ = std::move(built_tickets);
}

Color TransferTicketScreen::colorForGame(const std::string& key, const Color& fallback) const {
    auto it = game_palette_.find(key);
    return it == game_palette_.end() ? fallback : it->second;
}

void TransferTicketScreen::drawTextureTopLeft(SDL_Renderer* renderer, const TextureHandle& texture, int x, int y) const {
    if (!texture.texture) {
        return;
    }

    SDL_SetTextureBlendMode(texture.texture.get(), SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(texture.texture.get(), 255);
    SDL_SetTextureColorMod(texture.texture.get(), 255, 255, 255);

    SDL_Rect dst{sx(x), sy(y), sx(texture.width), sy(texture.height)};
    SDL_RenderCopy(renderer, texture.texture.get(), nullptr, &dst);
}

void TransferTicketScreen::drawTextureTopLeftTinted(
    SDL_Renderer* renderer,
    const TextureHandle& texture,
    int x,
    int y,
    const Color& tint) const {
    if (!texture.texture) {
        return;
    }

    SDL_SetTextureBlendMode(texture.texture.get(), SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(texture.texture.get(), tint.a);
    SDL_SetTextureColorMod(texture.texture.get(), tint.r, tint.g, tint.b);

    SDL_Rect dst{sx(x), sy(y), sx(texture.width), sy(texture.height)};
    SDL_RenderCopy(renderer, texture.texture.get(), nullptr, &dst);
}

void TransferTicketScreen::drawTextureCentered(SDL_Renderer* renderer, const TextureHandle& texture, int x, int y) const {
    if (!texture.texture) {
        return;
    }

    SDL_SetTextureBlendMode(texture.texture.get(), SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(texture.texture.get(), 255);
    SDL_SetTextureColorMod(texture.texture.get(), 255, 255, 255);

    const int w = sx(texture.width);
    const int h = sy(texture.height);
    SDL_Rect dst{sx(x) - w / 2, sy(y) - h / 2, w, h};
    SDL_RenderCopy(renderer, texture.texture.get(), nullptr, &dst);
}

void TransferTicketScreen::drawTextureCenteredTinted(
    SDL_Renderer* renderer,
    const TextureHandle& texture,
    int x,
    int y,
    const Color& tint) const {
    if (!texture.texture) {
        return;
    }

    SDL_SetTextureBlendMode(texture.texture.get(), SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(texture.texture.get(), tint.a);
    SDL_SetTextureColorMod(texture.texture.get(), tint.r, tint.g, tint.b);

    const int w = sx(texture.width);
    const int h = sy(texture.height);
    SDL_Rect dst{sx(x) - w / 2, sy(y) - h / 2, w, h};
    SDL_RenderCopy(renderer, texture.texture.get(), nullptr, &dst);
}

void TransferTicketScreen::drawTextureCenteredScaled(
    SDL_Renderer* renderer,
    const TextureHandle& texture,
    int x,
    int y,
    double scale) const {
    if (!texture.texture) {
        return;
    }

    SDL_SetTextureBlendMode(texture.texture.get(), SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(texture.texture.get(), 255);
    SDL_SetTextureColorMod(texture.texture.get(), 255, 255, 255);

    const int w = std::max(1, static_cast<int>(std::round(sx(texture.width) * scale)));
    const int h = std::max(1, static_cast<int>(std::round(sy(texture.height) * scale)));
    SDL_Rect dst{sx(x) - w / 2, sy(y) - h / 2, w, h};
    SDL_RenderCopy(renderer, texture.texture.get(), nullptr, &dst);
}

void TransferTicketScreen::drawTextureTopLeftRotated(
    SDL_Renderer* renderer,
    const TextureHandle& texture,
    int x,
    int y,
    double angle_degrees) const {
    if (!texture.texture) {
        return;
    }

    SDL_SetTextureBlendMode(texture.texture.get(), SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(texture.texture.get(), 255);
    SDL_SetTextureColorMod(texture.texture.get(), 255, 255, 255);

    SDL_Rect dst{sx(x), sy(y), sx(texture.width), sy(texture.height)};
    SDL_RenderCopyEx(renderer, texture.texture.get(), nullptr, &dst, angle_degrees, nullptr, SDL_FLIP_NONE);
}

void TransferTicketScreen::drawTextureTopLeftRotatedAround(
    SDL_Renderer* renderer,
    const TextureHandle& texture,
    int x,
    int y,
    double angle_degrees,
    int pivot_x,
    int pivot_y) const {
    if (!texture.texture) {
        return;
    }

    SDL_SetTextureBlendMode(texture.texture.get(), SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(texture.texture.get(), 255);
    SDL_SetTextureColorMod(texture.texture.get(), 255, 255, 255);

    SDL_Rect dst{sx(x), sy(y), sx(texture.width), sy(texture.height)};
    SDL_Point center{sx(pivot_x - x), sy(pivot_y - y)};
    SDL_RenderCopyEx(renderer, texture.texture.get(), nullptr, &dst, angle_degrees, &center, SDL_FLIP_NONE);
}

void TransferTicketScreen::drawTextureTopLeftTintedRotatedAround(
    SDL_Renderer* renderer,
    const TextureHandle& texture,
    int x,
    int y,
    const Color& tint,
    double angle_degrees,
    int pivot_x,
    int pivot_y) const {
    if (!texture.texture) {
        return;
    }

    SDL_SetTextureBlendMode(texture.texture.get(), SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(texture.texture.get(), tint.a);
    SDL_SetTextureColorMod(texture.texture.get(), tint.r, tint.g, tint.b);

    SDL_Rect dst{sx(x), sy(y), sx(texture.width), sy(texture.height)};
    SDL_Point center{sx(pivot_x - x), sy(pivot_y - y)};
    SDL_RenderCopyEx(renderer, texture.texture.get(), nullptr, &dst, angle_degrees, &center, SDL_FLIP_NONE);
}

void TransferTicketScreen::drawSelectionOutline(
    SDL_Renderer* renderer,
    int x,
    int y,
    int width,
    int height) const {
    if (!selection_.enabled || selection_.border_thickness <= 0) {
        return;
    }

    const double pulse = (std::sin(elapsed_seconds_ * selection_.beat_speed * kPi * 2.0) + 1.0) * 0.5;
    const int animated_padding = selection_.border_padding +
        static_cast<int>(std::round(selection_.beat_magnitude * pulse));
    const int alpha = std::max(0, std::min(255, selection_.border_alpha));

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(
        renderer,
        static_cast<Uint8>(selection_.border_color.r),
        static_cast<Uint8>(selection_.border_color.g),
        static_cast<Uint8>(selection_.border_color.b),
        static_cast<Uint8>(alpha));

    for (int i = 0; i < selection_.border_thickness; ++i) {
        drawRoundedSelectionRect(
            renderer,
            x - animated_padding - i,
            y - animated_padding - i,
            width + (animated_padding + i) * 2,
            height + (animated_padding + i) * 2,
            selection_.border_radius + animated_padding + i);
    }
}

void TransferTicketScreen::drawRoundedSelectionRect(
    SDL_Renderer* renderer,
    int x,
    int y,
    int width,
    int height,
    int radius) const {
    const int clamped_radius = std::max(0, std::min(radius, std::min(width, height) / 2));
    if (clamped_radius <= 0) {
        SDL_Rect rect{sx(x), sy(y), sx(width), sy(height)};
        SDL_RenderDrawRect(renderer, &rect);
        return;
    }

    SDL_RenderDrawLine(renderer, sx(x + clamped_radius), sy(y), sx(x + width - clamped_radius), sy(y));
    SDL_RenderDrawLine(renderer, sx(x + clamped_radius), sy(y + height), sx(x + width - clamped_radius), sy(y + height));
    SDL_RenderDrawLine(renderer, sx(x), sy(y + clamped_radius), sx(x), sy(y + height - clamped_radius));
    SDL_RenderDrawLine(renderer, sx(x + width), sy(y + clamped_radius), sx(x + width), sy(y + height - clamped_radius));

    for (int degrees = 0; degrees <= 90; ++degrees) {
        const double angle = static_cast<double>(degrees) * kPi / 180.0;
        const int dx = static_cast<int>(std::round(std::cos(angle) * clamped_radius));
        const int dy = static_cast<int>(std::round(std::sin(angle) * clamped_radius));
        SDL_RenderDrawPoint(renderer, sx(x + clamped_radius - dx), sy(y + clamped_radius - dy));
        SDL_RenderDrawPoint(renderer, sx(x + width - clamped_radius + dx), sy(y + clamped_radius - dy));
        SDL_RenderDrawPoint(renderer, sx(x + clamped_radius - dx), sy(y + height - clamped_radius + dy));
        SDL_RenderDrawPoint(renderer, sx(x + width - clamped_radius + dx), sy(y + height - clamped_radius + dy));
    }
}

void TransferTicketScreen::renderTicket(SDL_Renderer* renderer, int index, int x, int y, bool selected) const {
    if (index < 0 || index >= static_cast<int>(tickets_.size())) {
        return;
    }

    const TicketEntry& ticket = tickets_[static_cast<std::size_t>(index)];
    const int ticket_width = assets_.main_left.width;
    const int ticket_height = assets_.main_left.height;
    const int ticket_left = x;
    const int ticket_top = y;
    const auto state_index = static_cast<std::size_t>(std::max(0, std::min(index, static_cast<int>(right_stub_offsets_.size()) - 1)));
    const int right_offset = right_stub_offsets_.empty() ? 0 : right_stub_offsets_[state_index];
    const int right_x = ticket_left + right_offset;
    const bool ticket_ripping =
        state_index < rip_animation_active_.size() && rip_animation_active_[state_index];
    const bool ticket_ripped =
        state_index < ripped_.size() && ripped_[state_index];
    const double rip_progress =
        rip_animation_.distance == 0
            ? 0.0
            : std::max(0.0, std::min(1.0, static_cast<double>(right_offset) / static_cast<double>(rip_animation_.distance)));
    const double right_rotation = rip_animation_.rotation_degrees * rip_progress;
    const Point right_pivot{
        right_x + rip_animation_.rotation_pivot.x,
        ticket_top + rip_animation_.rotation_pivot.y
    };

    drawTextureTopLeft(renderer, assets_.main_left, ticket_left, ticket_top);
    drawTextureTopLeftTinted(renderer, assets_.color_left, ticket_left, ticket_top, ticket.game_color);
    drawTextureTopLeftRotatedAround(renderer, assets_.main_right, right_x, ticket_top, right_rotation, right_pivot.x, right_pivot.y);
    drawTextureTopLeftTintedRotatedAround(
        renderer,
        assets_.color_right,
        right_x,
        ticket_top,
        ticket.game_color,
        right_rotation,
        right_pivot.x,
        right_pivot.y);

    drawTextureTopLeftTinted(renderer, assets_.game_icon_back, ticket_left, ticket_top, ticket.game_color);
    drawTextureTopLeft(renderer, assets_.game_icon_front, ticket_left, ticket_top);
    if (isLightTicketColor(ticket.game_color)) {
        drawTextureTopLeftTintedRotatedAround(
            renderer,
            assets_.icon_boat,
            right_x,
            ticket_top,
            kBoardingPassDarkColor,
            right_rotation,
            right_pivot.x,
            right_pivot.y);
    } else {
        drawTextureTopLeftRotatedAround(renderer, assets_.icon_boat, right_x, ticket_top, right_rotation, right_pivot.x, right_pivot.y);
    }

    drawTextureTopLeftRotated(
        renderer,
        ticket.text.boarding_pass,
        ticket_left + layout_.boarding_pass.x,
        ticket_top + layout_.boarding_pass.y,
        layout_.boarding_pass_angle);

    drawTextureTopLeft(renderer, ticket.text.game_title, ticket_left + layout_.game_title.x, ticket_top + layout_.game_title.y);
    drawTextureTopLeft(renderer, ticket.text.trainer_name, ticket_left + layout_.trainer_name.x, ticket_top + layout_.trainer_name.y);

    const int visible_party_count = std::min(
        layout_.party_count,
        static_cast<int>(ticket.party_sprites.size()));
    for (int i = 0; i < visible_party_count; ++i) {
        drawTextureCenteredScaled(
            renderer,
            ticket.party_sprites[static_cast<std::size_t>(i)],
            ticket_left + layout_.party_start.x + layout_.party_spacing * i,
            ticket_top + layout_.party_start.y,
            layout_.party_scale);
    }

    const int stats_x = right_x + layout_.stats_origin.x;
    const int stats_y = ticket_top + layout_.stats_origin.y;
    drawTextureTopLeftRotatedAround(renderer, ticket.text.pokedex_label, stats_x + layout_.pokedex_label.x, stats_y + layout_.pokedex_label.y, right_rotation, right_pivot.x, right_pivot.y);
    drawTextureTopLeftRotatedAround(renderer, ticket.text.pokedex_value, stats_x + layout_.pokedex_value.x, stats_y + layout_.pokedex_value.y, right_rotation, right_pivot.x, right_pivot.y);
    drawTextureTopLeftRotatedAround(renderer, ticket.text.time_label, stats_x + layout_.time_label.x, stats_y + layout_.time_label.y, right_rotation, right_pivot.x, right_pivot.y);
    drawTextureTopLeftRotatedAround(renderer, ticket.text.time_value, stats_x + layout_.time_value.x, stats_y + layout_.time_value.y, right_rotation, right_pivot.x, right_pivot.y);
    drawTextureTopLeftRotatedAround(renderer, ticket.text.badges_label, stats_x + layout_.badges_label.x, stats_y + layout_.badges_label.y, right_rotation, right_pivot.x, right_pivot.y);
    drawTextureTopLeftRotatedAround(renderer, ticket.text.badges_value, stats_x + layout_.badges_value.x, stats_y + layout_.badges_value.y, right_rotation, right_pivot.x, right_pivot.y);

    if (selected && !ticket_ripping && !ticket_ripped) {
        drawSelectionOutline(renderer, ticket_left, ticket_top, ticket_width, ticket_height);
    }
}

void TransferTicketScreen::updateScrollOffset() {
    if (ticketCount() <= 0 || selected_ticket_index_ <= 0) {
        target_scroll_offset_y_ = 0.0;
        return;
    }

    const double ticket_center_y =
        static_cast<double>(list_layout_.start.y) +
        static_cast<double>(selected_ticket_index_ * list_layout_.separation_y) +
        static_cast<double>(assets_.main_left.height) * 0.5;
    const double viewport_center_y =
        static_cast<double>(list_layout_.viewport.y) +
        static_cast<double>(list_layout_.viewport.h) * 0.5;

    target_scroll_offset_y_ = std::max(
        0.0,
        std::min(maxScrollOffset(), ticket_center_y - viewport_center_y));
}

bool TransferTicketScreen::pointInTicket(int logical_x, int logical_y, int index) const {
    const int ticket_width = assets_.main_left.width;
    const int ticket_height = assets_.main_left.height;
    const int rounded_scroll_offset_y = static_cast<int>(std::round(scroll_offset_y_));
    const int ticket_left = list_layout_.start.x;
    const int ticket_top = list_layout_.start.y + index * list_layout_.separation_y - rounded_scroll_offset_y;

    const bool inside_viewport =
        logical_x >= list_layout_.viewport.x &&
        logical_x < list_layout_.viewport.x + list_layout_.viewport.w &&
        logical_y >= list_layout_.viewport.y &&
        logical_y < list_layout_.viewport.y + list_layout_.viewport.h;
    return inside_viewport &&
           logical_x >= ticket_left &&
           logical_x < ticket_left + ticket_width &&
           logical_y >= ticket_top &&
           logical_y < ticket_top + ticket_height;
}

int TransferTicketScreen::ticketCount() const {
    return static_cast<int>(tickets_.size());
}

double TransferTicketScreen::maxScrollOffset() const {
    const int count = ticketCount();
    if (count <= 1) {
        return 0.0;
    }

    const double last_ticket_center_y =
        static_cast<double>(list_layout_.start.y) +
        static_cast<double>((count - 1) * list_layout_.separation_y) +
        static_cast<double>(assets_.main_left.height) * 0.5;
    const double viewport_center_y =
        static_cast<double>(list_layout_.viewport.y) +
        static_cast<double>(list_layout_.viewport.h) * 0.5;
    return std::max(0.0, last_ticket_center_y - viewport_center_y);
}

double TransferTicketScreen::scaleX() const {
    return static_cast<double>(window_config_.virtual_width) /
           static_cast<double>(window_config_.design_width > 0 ? window_config_.design_width : 512);
}

double TransferTicketScreen::scaleY() const {
    return static_cast<double>(window_config_.virtual_height) /
           static_cast<double>(window_config_.design_height > 0 ? window_config_.design_height : 384);
}

int TransferTicketScreen::sx(int value) const {
    return static_cast<int>(std::round(static_cast<double>(value) * scaleX()));
}

int TransferTicketScreen::sy(int value) const {
    return static_cast<int>(std::round(static_cast<double>(value) * scaleY()));
}

} // namespace pr
