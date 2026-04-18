#include "ui/TransferSystemScreen.hpp"

#include "core/Json.hpp"

#include <SDL_image.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

namespace pr {

namespace {

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

double doubleFromObjectOrDefault(const JsonValue& obj, const std::string& key, double fallback) {
    const JsonValue* value = obj.get(key);
    return value ? value->asNumber() : fallback;
}

bool boolFromObjectOrDefault(const JsonValue& obj, const std::string& key, bool fallback) {
    const JsonValue* value = obj.get(key);
    return value ? value->asBool() : fallback;
}

} // namespace

TransferSystemScreen::TransferSystemScreen(
    SDL_Renderer* renderer,
    const WindowConfig& window_config,
    const std::string& font_path,
    const std::string& project_root)
    : window_config_(window_config),
      project_root_(project_root),
      background_(loadTexture(
          renderer,
          resolvePath(project_root_, "assets/transfer_select_save/background.png"))) {
    (void)font_path;
    loadTransferSystemConfig();
}

void TransferSystemScreen::enter(const TransferSaveSelection& selection, SDL_Renderer* renderer) {
    (void)selection;
    (void)renderer;
    restart_game_requested_ = false;
    play_button_sfx_requested_ = false;
    elapsed_seconds_ = 0.0;
}

void TransferSystemScreen::update(double dt) {
    elapsed_seconds_ += dt;
}

void TransferSystemScreen::render(SDL_Renderer* renderer) const {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    drawBackground(renderer);
}

void TransferSystemScreen::onAdvancePressed() {
}

void TransferSystemScreen::onBackPressed() {
    requestRestart();
}

bool TransferSystemScreen::handlePointerPressed(int logical_x, int logical_y) {
    (void)logical_x;
    (void)logical_y;
    return false;
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

void TransferSystemScreen::loadTransferSystemConfig() {
    const fs::path path = fs::path(project_root_) / "config" / "transfer_select_save.json";
    if (!fs::exists(path)) {
        return;
    }

    JsonValue root = parseJsonFile(path.string());
    if (!root.isObject()) {
        return;
    }

    const JsonValue* transfer_screen = root.get("transfer_screen");
    if (!transfer_screen || !transfer_screen->isObject()) {
        return;
    }

    const JsonValue* background_animation = transfer_screen->get("background_animation");
    if (!background_animation || !background_animation->isObject()) {
        return;
    }

    background_animation_.enabled = boolFromObjectOrDefault(
        *background_animation,
        "enabled",
        background_animation_.enabled);
    background_animation_.scale = std::max(
        0.01,
        doubleFromObjectOrDefault(
            *background_animation,
            "scale",
            background_animation_.scale));
    background_animation_.speed_x = doubleFromObjectOrDefault(
        *background_animation,
        "speed_x",
        background_animation_.speed_x);
    background_animation_.speed_y = doubleFromObjectOrDefault(
        *background_animation,
        "speed_y",
        background_animation_.speed_y);
}

void TransferSystemScreen::requestRestart() {
    play_button_sfx_requested_ = true;
    restart_game_requested_ = true;
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
