#include "ui/loading/PokeballLoadingScreen.hpp"

#include "core/assets/Font.hpp"
#include "core/config/Json.hpp"

#include <SDL_image.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace pr {

namespace {

fs::path resolvePath(const std::string& root, const std::string& configured) {
    fs::path path(configured);
    return path.is_absolute() ? path : (fs::path(root) / path);
}

int clampChannel(int value) {
    return std::max(0, std::min(255, value));
}

Color parseHexColor(const std::string& value, const Color& fallback) {
    if (value.size() != 7 || value[0] != '#') {
        return fallback;
    }

    try {
        return Color{
            clampChannel(std::stoi(value.substr(1, 2), nullptr, 16)),
            clampChannel(std::stoi(value.substr(3, 2), nullptr, 16)),
            clampChannel(std::stoi(value.substr(5, 2), nullptr, 16)),
            255
        };
    } catch (...) {
        return fallback;
    }
}

std::string stringFromObjectOrDefault(const JsonValue& obj, const std::string& key, const std::string& fallback) {
    const JsonValue* value = obj.get(key);
    return value && value->isString() ? value->asString() : fallback;
}

int intFromObjectOrDefault(const JsonValue& obj, const std::string& key, int fallback) {
    const JsonValue* value = obj.get(key);
    return value && value->isNumber() ? static_cast<int>(value->asNumber()) : fallback;
}

double doubleFromObjectOrDefault(const JsonValue& obj, const std::string& key, double fallback) {
    const JsonValue* value = obj.get(key);
    return value && value->isNumber() ? value->asNumber() : fallback;
}

void applyPointFromObject(Point& out, const JsonValue& obj) {
    out.x = intFromObjectOrDefault(obj, "x", out.x);
    out.y = intFromObjectOrDefault(obj, "y", out.y);
}

TextureHandle loadTexture(SDL_Renderer* renderer, const fs::path& path) {
    SDL_Texture* raw = IMG_LoadTexture(renderer, path.string().c_str());
    if (!raw) {
        throw std::runtime_error("Failed to load loading texture: " + path.string() + " | " + IMG_GetError());
    }

    TextureHandle texture;
    texture.texture.reset(raw, SDL_DestroyTexture);
    if (SDL_QueryTexture(texture.texture.get(), nullptr, nullptr, &texture.width, &texture.height) != 0) {
        throw std::runtime_error("Failed to query loading texture: " + path.string() + " | " + SDL_GetError());
    }
    return texture;
}

double easeInOutQuad(double value) {
    value = std::max(0.0, std::min(1.0, value));
    return value < 0.5
        ? 2.0 * value * value
        : 1.0 - std::pow(-2.0 * value + 2.0, 2.0) / 2.0;
}

} // namespace

PokeballLoadingScreen::PokeballLoadingScreen(
    SDL_Renderer* renderer,
    const WindowConfig& window_config,
    const std::string& fallback_font_path,
    const std::string& project_root)
    : window_config_(window_config),
      fallback_font_path_(fallback_font_path),
      project_root_(project_root),
      rng_(std::random_device{}()) {
    loadConfig();
    if (!config_.text.empty()) {
        font_ = loadFont(fallback_font_path_, config_.font_size, project_root_);
        text_texture_ = renderTextTexture(renderer, font_.get(), config_.text, config_.text_color);
    }
    loadBallTextures(renderer);
    chooseNextBall();
}

void PokeballLoadingScreen::enter() {
    lap_elapsed_seconds_ = 0.0;
    display_elapsed_seconds_ = 0.0;
    loading_complete_requested_ = false;
    loading_animation_complete_ = false;
    chooseNextBall();
}

void PokeballLoadingScreen::setMinimumLoopSeconds(double minimum_loop_seconds) {
    minimum_loop_seconds_ = std::max(0.0, minimum_loop_seconds);
}

void PokeballLoadingScreen::update(double dt) {
    dt = std::max(0.0, dt);
    const double lap_seconds = std::max(0.05, config_.lap_seconds);
    lap_elapsed_seconds_ += dt;
    display_elapsed_seconds_ += dt;
    while (lap_elapsed_seconds_ >= lap_seconds) {
        lap_elapsed_seconds_ -= lap_seconds;
        chooseNextBall();
    }
    if (loading_complete_requested_ && display_elapsed_seconds_ >= minimum_loop_seconds_) {
        loading_animation_complete_ = true;
    }
}

void PokeballLoadingScreen::markLoadingComplete() {
    loading_complete_requested_ = true;
    if (display_elapsed_seconds_ >= minimum_loop_seconds_) {
        loading_animation_complete_ = true;
    }
}

bool PokeballLoadingScreen::isLoadingAnimationComplete() const {
    return loading_animation_complete_;
}

void PokeballLoadingScreen::render(SDL_Renderer* renderer) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    if (current_ball_index_ >= 0 &&
        current_ball_index_ < static_cast<int>(ball_textures_.size())) {
        const TextureHandle& ball = ball_textures_[static_cast<std::size_t>(current_ball_index_)];
        if (ball.texture) {
            const double lap_seconds = std::max(0.05, config_.lap_seconds);
            const double t = std::max(0.0, std::min(1.0, lap_elapsed_seconds_ / lap_seconds));
            const double partial_fraction = std::max(0.05, std::min(0.9, config_.partial_spin_fraction));
            double angle = 0.0;
            if (t < partial_fraction) {
                angle = config_.partial_spin_degrees * easeInOutQuad(t / partial_fraction);
            } else {
                const double spin_t = (t - partial_fraction) / (1.0 - partial_fraction);
                angle = config_.partial_spin_degrees +
                    (config_.full_spin_degrees - config_.partial_spin_degrees) * easeInOutQuad(spin_t);
            }

            SDL_SetTextureBlendMode(ball.texture.get(), SDL_BLENDMODE_BLEND);
            SDL_SetTextureAlphaMod(ball.texture.get(), 255);
            SDL_SetTextureColorMod(ball.texture.get(), 255, 255, 255);
            const int width = static_cast<int>(std::round(static_cast<double>(ball.width) * config_.ball_scale));
            const int height = static_cast<int>(std::round(static_cast<double>(ball.height) * config_.ball_scale));
            SDL_Rect dst{
                sx(config_.ball_center.x) - sx(width) / 2,
                sy(config_.ball_center.y) - sy(height) / 2,
                sx(width),
                sy(height)
            };
            SDL_RenderCopyEx(renderer, ball.texture.get(), nullptr, &dst, angle, nullptr, SDL_FLIP_NONE);
        }
    }

    if (text_texture_.texture) {
        SDL_SetTextureBlendMode(text_texture_.texture.get(), SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(text_texture_.texture.get(), 255);
        SDL_SetTextureColorMod(text_texture_.texture.get(), 255, 255, 255);
        SDL_Rect dst{
            sx(config_.text_center.x) - sx(text_texture_.width) / 2,
            sy(config_.text_center.y) - sy(text_texture_.height) / 2,
            sx(text_texture_.width),
            sy(text_texture_.height)
        };
        SDL_RenderCopy(renderer, text_texture_.texture.get(), nullptr, &dst);
    }
}

void PokeballLoadingScreen::loadConfig() {
    const fs::path path = resolvePath(project_root_, "config/loading_screen.json");
    if (!fs::exists(path)) {
        return;
    }

    const JsonValue root = parseJsonFile(path.string());
    if (!root.isObject()) {
        throw std::runtime_error("loading_screen.json root must be an object");
    }

    config_.balls_directory = stringFromObjectOrDefault(root, "balls_directory", config_.balls_directory);
    config_.text = stringFromObjectOrDefault(root, "text", config_.text);
    config_.font_size = std::max(1, intFromObjectOrDefault(root, "font_size", config_.font_size));
    config_.text_color = parseHexColor(stringFromObjectOrDefault(root, "text_color", "#FFFFFF"), config_.text_color);
    config_.ball_scale = std::max(0.1, doubleFromObjectOrDefault(root, "ball_scale", config_.ball_scale));
    config_.lap_seconds = std::max(0.05, doubleFromObjectOrDefault(root, "lap_seconds", config_.lap_seconds));
    config_.partial_spin_degrees = doubleFromObjectOrDefault(root, "partial_spin_degrees", config_.partial_spin_degrees);
    config_.full_spin_degrees = doubleFromObjectOrDefault(root, "full_spin_degrees", config_.full_spin_degrees);
    config_.partial_spin_fraction = std::max(
        0.05,
        std::min(0.9, doubleFromObjectOrDefault(root, "partial_spin_fraction", config_.partial_spin_fraction)));
    if (const JsonValue* ball_center = root.get("ball_center")) {
        applyPointFromObject(config_.ball_center, *ball_center);
    }
    if (const JsonValue* text_center = root.get("text_center")) {
        applyPointFromObject(config_.text_center, *text_center);
    }
}

void PokeballLoadingScreen::loadBallTextures(SDL_Renderer* renderer) {
    const fs::path balls_directory = resolvePath(project_root_, config_.balls_directory);
    if (!fs::exists(balls_directory) || !fs::is_directory(balls_directory)) {
        std::cerr << "Warning: loading ball directory missing: " << balls_directory << '\n';
        return;
    }

    std::vector<fs::path> paths;
    for (const auto& entry : fs::directory_iterator(balls_directory)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".png") {
            continue;
        }
        paths.push_back(entry.path());
    }
    std::sort(paths.begin(), paths.end());

    for (const fs::path& path : paths) {
        try {
            ball_textures_.push_back(loadTexture(renderer, path));
        } catch (const std::exception& ex) {
            std::cerr << "Warning: could not load loading ball " << path << ": " << ex.what() << '\n';
        }
    }
}

void PokeballLoadingScreen::chooseNextBall() {
    if (ball_textures_.empty()) {
        current_ball_index_ = -1;
        return;
    }

    std::uniform_int_distribution<int> distribution(0, static_cast<int>(ball_textures_.size()) - 1);
    if (ball_textures_.size() == 1) {
        current_ball_index_ = 0;
        return;
    }

    int next = current_ball_index_;
    while (next == current_ball_index_) {
        next = distribution(rng_);
    }
    current_ball_index_ = next;
}

double PokeballLoadingScreen::scaleX() const {
    return static_cast<double>(window_config_.virtual_width) /
           static_cast<double>(window_config_.design_width > 0 ? window_config_.design_width : 512);
}

double PokeballLoadingScreen::scaleY() const {
    return static_cast<double>(window_config_.virtual_height) /
           static_cast<double>(window_config_.design_height > 0 ? window_config_.design_height : 384);
}

int PokeballLoadingScreen::sx(int value) const {
    return static_cast<int>(std::round(static_cast<double>(value) * scaleX()));
}

int PokeballLoadingScreen::sy(int value) const {
    return static_cast<int>(std::round(static_cast<double>(value) * scaleY()));
}

} // namespace pr
