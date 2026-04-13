#include "ui/TransferSystemScreen.hpp"

#include "core/Font.hpp"
#include "core/Json.hpp"

#include <SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;

namespace pr {

namespace {

const Color kBackgroundColor{38, 42, 48, 255};
const Color kPanelColor{232, 228, 224, 255};
const Color kPanelShadowColor{0, 0, 0, 80};
const Color kTitleColor{255, 255, 255, 255};
const Color kSubtitleColor{210, 216, 224, 255};
const Color kBodyColor{31, 31, 31, 255};
const Color kMutedColor{94, 94, 94, 255};
const Color kBackButtonColor{0, 47, 97, 255};
const Color kBackButtonTextColor{255, 255, 255, 255};

FontHandle loadStyledFont(const std::string& font_path, int pt_size, int style, const std::string& project_root) {
    FontHandle font = loadFont(font_path, pt_size, project_root);
    TTF_SetFontStyle(font.get(), style);
    return font;
}

} // namespace

TransferSystemScreen::TransferSystemScreen(
    SDL_Renderer* renderer,
    const WindowConfig& window_config,
    const std::string& font_path,
    const std::string& project_root)
    : window_config_(window_config),
      font_path_(font_path),
      project_root_(project_root),
      title_font_(loadStyledFont(font_path_, 28, TTF_STYLE_NORMAL, project_root_)),
      body_font_(loadStyledFont(font_path_, 16, TTF_STYLE_NORMAL, project_root_)),
      button_font_(loadStyledFont(font_path_, 18, TTF_STYLE_NORMAL, project_root_)) {
    (void)renderer;
    loadTransferSystemConfig();
}

void TransferSystemScreen::enter(const TransferSaveSelection& selection, SDL_Renderer* renderer) {
    selection_ = selection;
    restart_game_requested_ = false;
    play_button_sfx_requested_ = false;
    elapsed_seconds_ = 0.0;
    rebuildText(renderer);
}

void TransferSystemScreen::update(double dt) {
    elapsed_seconds_ += dt;
}

void TransferSystemScreen::render(SDL_Renderer* renderer) const {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(
        renderer,
        static_cast<Uint8>(kBackgroundColor.r),
        static_cast<Uint8>(kBackgroundColor.g),
        static_cast<Uint8>(kBackgroundColor.b),
        static_cast<Uint8>(kBackgroundColor.a));
    SDL_RenderClear(renderer);

    drawTextureCentered(renderer, title_text_, window_config_.design_width / 2, 44);
    drawTextureCentered(renderer, subtitle_text_, window_config_.design_width / 2, 70);

    SDL_SetRenderDrawColor(
        renderer,
        static_cast<Uint8>(kPanelShadowColor.r),
        static_cast<Uint8>(kPanelShadowColor.g),
        static_cast<Uint8>(kPanelShadowColor.b),
        static_cast<Uint8>(kPanelShadowColor.a));
    SDL_Rect shadow{sx(48), sy(104), sx(416), sy(190)};
    SDL_RenderFillRect(renderer, &shadow);

    SDL_SetRenderDrawColor(
        renderer,
        static_cast<Uint8>(kPanelColor.r),
        static_cast<Uint8>(kPanelColor.g),
        static_cast<Uint8>(kPanelColor.b),
        static_cast<Uint8>(kPanelColor.a));
    SDL_Rect panel{sx(44), sy(100), sx(416), sy(190)};
    SDL_RenderFillRect(renderer, &panel);

    int y = 120;
    for (const TextureHandle& line : detail_textures_) {
        drawTextureTopLeft(renderer, line, 70, y);
        y += 23;
    }

    const SDL_Rect back = backButtonRect();
    SDL_SetRenderDrawColor(
        renderer,
        static_cast<Uint8>(kBackButtonColor.r),
        static_cast<Uint8>(kBackButtonColor.g),
        static_cast<Uint8>(kBackButtonColor.b),
        static_cast<Uint8>(kBackButtonColor.a));
    SDL_Rect scaled_back{sx(back.x), sy(back.y), sx(back.w), sy(back.h)};
    SDL_RenderFillRect(renderer, &scaled_back);
    drawTextureCentered(renderer, back_text_, back.x + back.w / 2, back.y + back.h / 2);

    if (fade_in_seconds_ > 0.0) {
        const double progress = std::max(0.0, std::min(1.0, elapsed_seconds_ / fade_in_seconds_));
        const int alpha = static_cast<int>(std::round((1.0 - progress) * 255.0));
        if (alpha > 0) {
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, static_cast<Uint8>(alpha));
            SDL_Rect overlay{0, 0, sx(window_config_.design_width), sy(window_config_.design_height)};
            SDL_RenderFillRect(renderer, &overlay);
        }
    }
}

void TransferSystemScreen::onAdvancePressed() {
    requestRestart();
}

void TransferSystemScreen::onBackPressed() {
    requestRestart();
}

bool TransferSystemScreen::handlePointerPressed(int logical_x, int logical_y) {
    if (!pointInRect(logical_x, logical_y, backButtonRect())) {
        return false;
    }

    requestRestart();
    return true;
}

bool TransferSystemScreen::consumeButtonSfxRequest() {
    const bool requested = play_button_sfx_requested_;
    play_button_sfx_requested_ = false;
    return requested;
}

bool TransferSystemScreen::consumeRestartGameRequest() {
    const bool requested = restart_game_requested_;
    restart_game_requested_ = false;
    return requested;
}

void TransferSystemScreen::rebuildText(SDL_Renderer* renderer) {
    title_text_ = renderTextTexture(renderer, title_font_.get(), "transfer_system", kTitleColor);
    subtitle_text_ = renderTextTexture(renderer, body_font_.get(), "Selected save ready for transfer setup", kSubtitleColor);
    back_text_ = renderTextTexture(renderer, button_font_.get(), "BACK", kBackButtonTextColor);

    detail_textures_.clear();
    detail_textures_.push_back(renderTextTexture(renderer, body_font_.get(), "Game: " + selection_.game_title, kBodyColor));
    if (!selection_.source_filename.empty()) {
        detail_textures_.push_back(renderTextTexture(renderer, body_font_.get(), "File: " + selection_.source_filename, kMutedColor));
    }
    detail_textures_.push_back(renderTextTexture(renderer, body_font_.get(), "Trainer: " + selection_.trainer_name, kBodyColor));
    detail_textures_.push_back(renderTextTexture(renderer, body_font_.get(), "Play Time: " + selection_.time, kBodyColor));
    detail_textures_.push_back(renderTextTexture(renderer, body_font_.get(), "Pokedex: " + selection_.pokedex, kBodyColor));
    detail_textures_.push_back(renderTextTexture(renderer, body_font_.get(), "Badges: " + selection_.badges, kBodyColor));
    detail_textures_.push_back(renderTextTexture(
        renderer,
        body_font_.get(),
        "Party Slots: " + std::to_string(selection_.party_sprites.size()),
        kMutedColor));
}

void TransferSystemScreen::loadTransferSystemConfig() {
    const fs::path path = fs::path(project_root_) / "config" / "transfer_select_save.json";
    if (!fs::exists(path)) {
        return;
    }

    JsonValue root = parseJsonFile(path.string());
    if (!root.isObject()) {
        return;
    }

    const JsonValue* system = root.get("transfer_system");
    if (!system || !system->isObject()) {
        return;
    }

    const JsonValue* fade_in = system->get("fade_in_seconds");
    if (fade_in) {
        fade_in_seconds_ = std::max(0.0, fade_in->asNumber());
    }
}

void TransferSystemScreen::requestRestart() {
    play_button_sfx_requested_ = true;
    restart_game_requested_ = true;
}

void TransferSystemScreen::drawTextureTopLeft(SDL_Renderer* renderer, const TextureHandle& texture, int x, int y) const {
    if (!texture.texture) {
        return;
    }

    SDL_SetTextureBlendMode(texture.texture.get(), SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(texture.texture.get(), 255);
    SDL_SetTextureColorMod(texture.texture.get(), 255, 255, 255);
    SDL_Rect dst{sx(x), sy(y), sx(texture.width), sy(texture.height)};
    SDL_RenderCopy(renderer, texture.texture.get(), nullptr, &dst);
}

void TransferSystemScreen::drawTextureCentered(SDL_Renderer* renderer, const TextureHandle& texture, int x, int y) const {
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

bool TransferSystemScreen::pointInRect(int x, int y, const SDL_Rect& rect) const {
    return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
}

SDL_Rect TransferSystemScreen::backButtonRect() const {
    return SDL_Rect{176, 316, 160, 42};
}

double TransferSystemScreen::scaleX() const {
    return static_cast<double>(window_config_.virtual_width) /
           static_cast<double>(window_config_.design_width > 0 ? window_config_.design_width : 512);
}

double TransferSystemScreen::scaleY() const {
    return static_cast<double>(window_config_.virtual_height) /
           static_cast<double>(window_config_.design_height > 0 ? window_config_.design_height : 384);
}

int TransferSystemScreen::sx(int value) const {
    return static_cast<int>(std::round(static_cast<double>(value) * scaleX()));
}

int TransferSystemScreen::sy(int value) const {
    return static_cast<int>(std::round(static_cast<double>(value) * scaleY()));
}

} // namespace pr
