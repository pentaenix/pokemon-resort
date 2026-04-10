#include "ui/TransferTicketSandboxScreen.hpp"

#include "core/Font.hpp"
#include "core/Json.hpp"

#include <SDL_image.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace pr {

namespace {

constexpr int kDesignWidth = 1024;
constexpr int kDesignHeight = 768;

const Color kTitleColor{0x1f, 0x1f, 0x1f, 255};
const Color kTrainerColor{0x5e, 0x5e, 0x5e, 255};
const Color kDataLabelColor{0x5e, 0x5e, 0x5e, 255};
const Color kDataValueColor{0x46, 0x46, 0x46, 255};
const Color kBoardingPassColor{0xe9, 0xe6, 0xe3, 255};
const Color kWhite{255, 255, 255, 255};
const Color kRubyFallback{0xB3, 0x3A, 0x32, 255};
const Color kHintColor{0x8A, 0x84, 0x7F, 255};

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

} // namespace

TransferTicketSandboxScreen::TransferTicketSandboxScreen(
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
    assets_.sub_right = loadTexture(renderer, root / "sub_left.png");
    assets_.color_left = loadTexture(renderer, root / "color_left.png");
    assets_.color_right = loadTexture(renderer, root / "color_right.png");
    assets_.game_icon_back = loadTexture(renderer, root / "game_icon_back.png");
    assets_.game_icon_front = loadTexture(renderer, root / "game_icon_front.png");
    assets_.icon_boat = loadTexture(renderer, root / "icon_boat.png");
    assets_.party_sprite = loadTexture(renderer, resolvePath(project_root_, "assets/sprites/wingull.png"));

    fonts_.banner_title = loadStyledFont(font_path_, 56, TTF_STYLE_NORMAL, project_root_);
    fonts_.banner_subtitle = loadStyledFont(font_path_, 18, TTF_STYLE_NORMAL, project_root_);
    fonts_.title = loadStyledFont(font_path_, 56, TTF_STYLE_NORMAL, project_root_);
    fonts_.trainer = loadStyledFont(font_path_, 34, TTF_STYLE_NORMAL, project_root_);
    fonts_.data_label = loadStyledFont(font_path_, 30, TTF_STYLE_NORMAL, project_root_);
    fonts_.data_value = loadStyledFont(font_path_, 32, TTF_STYLE_NORMAL, project_root_);
    fonts_.boarding_pass = loadStyledFont(font_path_, 34, TTF_STYLE_BOLD, project_root_);
    fonts_.footer_hint = loadStyledFont(font_path_, 16, TTF_STYLE_NORMAL, project_root_);

    loadPaletteConfig();
    active_game_color_ = colorForGame("pokemon_ruby", kRubyFallback);
    buildTextTextures(renderer);
}

void TransferTicketSandboxScreen::enter() {
    right_stub_offset_x_ = 0;
    return_to_main_menu_requested_ = false;
}

void TransferTicketSandboxScreen::update(double dt) {
    (void)dt;
}

void TransferTicketSandboxScreen::render(SDL_Renderer* renderer) const {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    renderTicket(renderer);
}

void TransferTicketSandboxScreen::onNavigate(int delta) {
    (void)delta;
}

void TransferTicketSandboxScreen::onAdvancePressed() {
}

void TransferTicketSandboxScreen::onBackPressed() {
    requestButtonSfx();
    return_to_main_menu_requested_ = true;
}

void TransferTicketSandboxScreen::handlePointerMoved(int logical_x, int logical_y) {
    (void)logical_x;
    (void)logical_y;
}

bool TransferTicketSandboxScreen::handlePointerPressed(int logical_x, int logical_y) {
    (void)logical_x;
    (void)logical_y;
    return false;
}

bool TransferTicketSandboxScreen::handlePointerReleased(int logical_x, int logical_y) {
    (void)logical_x;
    (void)logical_y;
    return false;
}

bool TransferTicketSandboxScreen::consumeButtonSfxRequest() {
    const bool requested = play_button_sfx_requested_;
    play_button_sfx_requested_ = false;
    return requested;
}

bool TransferTicketSandboxScreen::consumeReturnToMainMenuRequest() {
    const bool requested = return_to_main_menu_requested_;
    return_to_main_menu_requested_ = false;
    return requested;
}

void TransferTicketSandboxScreen::requestButtonSfx() {
    play_button_sfx_requested_ = true;
}

void TransferTicketSandboxScreen::loadPaletteConfig() {
    const fs::path path = resolvePath(project_root_, "config/transfer_select_save.json");
    if (!fs::exists(path)) {
        return;
    }

    JsonValue root = parseJsonFile(path.string());
    if (!root.isObject()) {
        throw std::runtime_error("transfer_select_save.json root must be an object");
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

void TransferTicketSandboxScreen::buildTextTextures(SDL_Renderer* renderer) {
    text_.banner_title = renderTextTexture(renderer, fonts_.banner_title.get(), "TRAVEL DESK", kWhite);
    text_.banner_subtitle = renderTextTexture(renderer, fonts_.banner_subtitle.get(), "Select a save to begin transfer", kWhite);
    text_.game_title = renderTextTexture(renderer, fonts_.title.get(), "Pokemon Ruby", kTitleColor);
    text_.trainer_name = renderTextTexture(renderer, fonts_.trainer.get(), "Niky", kTrainerColor);
    text_.time_label = renderTextTexture(renderer, fonts_.data_label.get(), "Time:", kDataLabelColor);
    text_.time_value = renderTextTexture(renderer, fonts_.data_value.get(), "154:08", kDataValueColor);
    text_.badges_label = renderTextTexture(renderer, fonts_.data_label.get(), "Badges:", kDataLabelColor);
    text_.badges_value = renderTextTexture(renderer, fonts_.data_value.get(), "0", kDataValueColor);
    text_.pokedex_label = renderTextTexture(renderer, fonts_.data_label.get(), "Pokedex:", kDataLabelColor);
    text_.pokedex_value = renderTextTexture(renderer, fonts_.data_value.get(), "15", kDataValueColor);
    text_.boarding_pass = renderTextTexture(renderer, fonts_.boarding_pass.get(), "Boarding Pass", kBoardingPassColor);
    text_.footer_hint = renderTextTexture(renderer, fonts_.footer_hint.get(), "Sandbox Preview  •  Press N or Esc to return", kHintColor);
}

Color TransferTicketSandboxScreen::colorForGame(const std::string& key, const Color& fallback) const {
    auto it = game_palette_.find(key);
    return it == game_palette_.end() ? fallback : it->second;
}

void TransferTicketSandboxScreen::drawTextureTopLeft(SDL_Renderer* renderer, const TextureHandle& texture, int x, int y) const {
    if (!texture.texture) {
        return;
    }

    SDL_SetTextureBlendMode(texture.texture.get(), SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(texture.texture.get(), 255);
    SDL_SetTextureColorMod(texture.texture.get(), 255, 255, 255);

    SDL_Rect dst{sx(x), sy(y), sx(texture.width), sy(texture.height)};
    SDL_RenderCopy(renderer, texture.texture.get(), nullptr, &dst);
}

void TransferTicketSandboxScreen::drawTextureTopLeftTinted(
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

void TransferTicketSandboxScreen::drawTextureCentered(SDL_Renderer* renderer, const TextureHandle& texture, int x, int y) const {
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

void TransferTicketSandboxScreen::drawTextureCenteredTinted(
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

void TransferTicketSandboxScreen::drawTextureCenteredScaled(
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

void TransferTicketSandboxScreen::drawTextureTopLeftRotated(
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

void TransferTicketSandboxScreen::renderTicket(SDL_Renderer* renderer) const {
    const int ticket_width = assets_.main_left.width;
    const int ticket_height = assets_.main_left.height;
    const int ticket_left = (kDesignWidth - ticket_width) / 2;
    const int ticket_top = (kDesignHeight - ticket_height) / 2;
    const int right_stub_x = ticket_left + right_stub_offset_x_;

    drawTextureTopLeft(renderer, assets_.main_left, ticket_left, ticket_top);
    drawTextureTopLeftTinted(renderer, assets_.color_left, ticket_left, ticket_top, active_game_color_);
    drawTextureTopLeft(renderer, assets_.sub_right, right_stub_x, ticket_top);
    drawTextureTopLeftTinted(renderer, assets_.color_right, right_stub_x, ticket_top, active_game_color_);

    drawTextureCenteredTinted(renderer, assets_.game_icon_back, ticket_left + 127, ticket_top + 110, active_game_color_);
    drawTextureCentered(renderer, assets_.game_icon_front, ticket_left + 127, ticket_top + 110);

    drawTextureTopLeftRotated(renderer, text_.boarding_pass, ticket_left + 42, ticket_top + 200, -90.0);
    drawTextureCentered(renderer, assets_.icon_boat, right_stub_x + 852, ticket_top + 34);

    drawTextureTopLeft(renderer, text_.game_title, ticket_left + 190, ticket_top + 18);
    drawTextureTopLeft(renderer, text_.trainer_name, ticket_left + 194, ticket_top + 100);

    drawTextureCenteredScaled(renderer, assets_.party_sprite, ticket_left + 212, ticket_top + 167, 3.0);

    const int right_text_x = right_stub_x + 644;
    drawTextureTopLeft(renderer, text_.time_label, right_text_x, ticket_top + 38);
    drawTextureTopLeft(renderer, text_.time_value, right_text_x + 108, ticket_top + 36);
    drawTextureTopLeft(renderer, text_.badges_label, right_text_x, ticket_top + 96);
    drawTextureTopLeft(renderer, text_.badges_value, right_text_x + 160, ticket_top + 94);
    drawTextureTopLeft(renderer, text_.pokedex_label, right_text_x, ticket_top + 154);
    drawTextureTopLeft(renderer, text_.pokedex_value, right_text_x + 178, ticket_top + 152);
}

double TransferTicketSandboxScreen::scaleX() const {
    return static_cast<double>(window_config_.virtual_width) /
           static_cast<double>(window_config_.design_width > 0 ? window_config_.design_width : kDesignWidth);
}

double TransferTicketSandboxScreen::scaleY() const {
    return static_cast<double>(window_config_.virtual_height) /
           static_cast<double>(window_config_.design_height > 0 ? window_config_.design_height : kDesignHeight);
}

int TransferTicketSandboxScreen::sx(int value) const {
    return static_cast<int>(std::round(static_cast<double>(value) * scaleX()));
}

int TransferTicketSandboxScreen::sy(int value) const {
    return static_cast<int>(std::round(static_cast<double>(value) * scaleY()));
}

} // namespace pr
