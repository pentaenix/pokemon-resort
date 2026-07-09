#include "ui/attend/AttendOverlay.hpp"

#include <algorithm>

namespace pr {

namespace {

Color toUiColor(const gameplay::attend::Color4& color) {
    const auto c = [](float v) {
        return static_cast<int>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    return Color{c(color.r), c(color.g), c(color.b), c(color.a)};
}

OverlayAnchor parseAnchor(const std::string& value) {
    if (value == "top_left") return OverlayAnchor::TopLeft;
    if (value == "bottom_left") return OverlayAnchor::BottomLeft;
    if (value == "bottom_right") return OverlayAnchor::BottomRight;
    return OverlayAnchor::TopRight;
}

} // namespace

AttendOverlay::AttendOverlay(int logical_w, int logical_h)
    : canvas_(logical_w, logical_h) {}

void AttendOverlay::setLogicalSize(int logical_w, int logical_h) {
    canvas_.setLogicalSize(logical_w, logical_h);
}

void AttendOverlay::setConfig(const gameplay::attend::AttendUiConfig& config) {
    config_ = config;
    canvas_.invalidateText();
}

OverlayButton AttendOverlay::weatherButton(const std::string& weather_label) const {
    const auto& style_config = config_.weather_button;
    OverlayButton button;
    button.id = "weather";
    button.label = weatherButtonLabel(weather_label);
    button.anchor = parseAnchor(style_config.anchor);
    button.style.fill = toUiColor(style_config.fill);
    button.style.stroke = toUiColor(style_config.stroke);
    button.style.text = toUiColor(style_config.text);
    button.style.width = style_config.width;
    button.style.height = style_config.height;
    button.style.margin_x = style_config.margin_x;
    button.style.margin_y = style_config.margin_y;
    button.style.padding_x = style_config.padding_x;
    button.style.corner_radius = style_config.corner_radius;
    button.style.stroke_width = style_config.stroke_width;
    button.style.font_size = style_config.font_size;
    return button;
}

OverlayButton AttendOverlay::viewButton(const std::string& view_label) const {
    const auto& style_config = config_.view_button;
    OverlayButton button;
    button.id = "view";
    button.label = viewButtonLabel(view_label);
    button.anchor = parseAnchor(style_config.anchor);
    button.style.fill = toUiColor(style_config.fill);
    button.style.stroke = toUiColor(style_config.stroke);
    button.style.text = toUiColor(style_config.text);
    button.style.width = style_config.width;
    button.style.height = style_config.height;
    button.style.margin_x = style_config.margin_x;
    button.style.margin_y = style_config.margin_y;
    button.style.padding_x = style_config.padding_x;
    button.style.corner_radius = style_config.corner_radius;
    button.style.stroke_width = style_config.stroke_width;
    button.style.font_size = style_config.font_size;
    return button;
}

OverlayButton AttendOverlay::pokemonButton(const std::string& pokemon_label) const {
    const auto& style_config = config_.pokemon_button;
    OverlayButton button;
    button.id = "pokemon";
    button.label = pokemonButtonLabel(pokemon_label);
    button.anchor = parseAnchor(style_config.anchor);
    button.style.fill = toUiColor(style_config.fill);
    button.style.stroke = toUiColor(style_config.stroke);
    button.style.text = toUiColor(style_config.text);
    button.style.width = style_config.width;
    button.style.height = style_config.height;
    button.style.margin_x = style_config.margin_x;
    button.style.margin_y = style_config.margin_y;
    button.style.padding_x = style_config.padding_x;
    button.style.corner_radius = style_config.corner_radius;
    button.style.stroke_width = style_config.stroke_width;
    button.style.font_size = style_config.font_size;
    return button;
}

OverlayButton AttendOverlay::textureVariantButton(const std::string& texture_variant_label) const {
    const auto& style_config = config_.texture_variant_button;
    OverlayButton button;
    button.id = "texture_variant";
    button.label = textureVariantButtonLabel(texture_variant_label);
    button.anchor = parseAnchor(style_config.anchor);
    button.style.fill = toUiColor(style_config.fill);
    button.style.stroke = toUiColor(style_config.stroke);
    button.style.text = toUiColor(style_config.text);
    button.style.width = style_config.width;
    button.style.height = style_config.height;
    button.style.margin_x = style_config.margin_x;
    button.style.margin_y = style_config.margin_y;
    button.style.padding_x = style_config.padding_x;
    button.style.corner_radius = style_config.corner_radius;
    button.style.stroke_width = style_config.stroke_width;
    button.style.font_size = style_config.font_size;
    return button;
}

OverlayButton AttendOverlay::formVariantButton(const std::string& form_variant_label) const {
    const auto& style_config = config_.form_variant_button;
    OverlayButton button;
    button.id = "form_variant";
    button.label = formVariantButtonLabel(form_variant_label);
    button.anchor = parseAnchor(style_config.anchor);
    button.style.fill = toUiColor(style_config.fill);
    button.style.stroke = toUiColor(style_config.stroke);
    button.style.text = toUiColor(style_config.text);
    button.style.width = style_config.width;
    button.style.height = style_config.height;
    button.style.margin_x = style_config.margin_x;
    button.style.margin_y = style_config.margin_y;
    button.style.padding_x = style_config.padding_x;
    button.style.corner_radius = style_config.corner_radius;
    button.style.stroke_width = style_config.stroke_width;
    button.style.font_size = style_config.font_size;
    return button;
}

OverlayButton AttendOverlay::skyButton(const std::string& sky_label) const {
    const auto& style_config = config_.sky_button;
    OverlayButton button;
    button.id = "sky";
    button.label = skyButtonLabel(sky_label);
    button.anchor = parseAnchor(style_config.anchor);
    button.style.fill = toUiColor(style_config.fill);
    button.style.stroke = toUiColor(style_config.stroke);
    button.style.text = toUiColor(style_config.text);
    button.style.width = style_config.width;
    button.style.height = style_config.height;
    button.style.margin_x = style_config.margin_x;
    button.style.margin_y = style_config.margin_y;
    button.style.padding_x = style_config.padding_x;
    button.style.corner_radius = style_config.corner_radius;
    button.style.stroke_width = style_config.stroke_width;
    button.style.font_size = style_config.font_size;
    return button;
}

OverlayButton AttendOverlay::emoteButton() const {
    const auto& style_config = config_.emote_button;
    OverlayButton button;
    button.id = "emote";
    button.label = emoteButtonLabel();
    button.anchor = parseAnchor(style_config.anchor);
    button.style.fill = toUiColor(style_config.fill);
    button.style.stroke = toUiColor(style_config.stroke);
    button.style.text = toUiColor(style_config.text);
    button.style.width = style_config.width;
    button.style.height = style_config.height;
    button.style.margin_x = style_config.margin_x;
    button.style.margin_y = style_config.margin_y;
    button.style.padding_x = style_config.padding_x;
    button.style.corner_radius = style_config.corner_radius;
    button.style.stroke_width = style_config.stroke_width;
    button.style.font_size = style_config.font_size;
    return button;
}

OverlayButton AttendOverlay::sleepButton() const {
    const auto& style_config = config_.sleep_button;
    OverlayButton button;
    button.id = "sleep";
    button.label = sleepButtonLabel();
    button.anchor = parseAnchor(style_config.anchor);
    button.style.fill = toUiColor(style_config.fill);
    button.style.stroke = toUiColor(style_config.stroke);
    button.style.text = toUiColor(style_config.text);
    button.style.width = style_config.width;
    button.style.height = style_config.height;
    button.style.margin_x = style_config.margin_x;
    button.style.margin_y = style_config.margin_y;
    button.style.padding_x = style_config.padding_x;
    button.style.corner_radius = style_config.corner_radius;
    button.style.stroke_width = style_config.stroke_width;
    button.style.font_size = style_config.font_size;
    return button;
}

OverlayButton AttendOverlay::cryButton() const {
    const auto& style_config = config_.cry_button;
    OverlayButton button;
    button.id = "cry";
    button.label = cryButtonLabel();
    button.anchor = parseAnchor(style_config.anchor);
    button.style.fill = toUiColor(style_config.fill);
    button.style.stroke = toUiColor(style_config.stroke);
    button.style.text = toUiColor(style_config.text);
    button.style.width = style_config.width;
    button.style.height = style_config.height;
    button.style.margin_x = style_config.margin_x;
    button.style.margin_y = style_config.margin_y;
    button.style.padding_x = style_config.padding_x;
    button.style.corner_radius = style_config.corner_radius;
    button.style.stroke_width = style_config.stroke_width;
    button.style.font_size = style_config.font_size;
    return button;
}

std::string AttendOverlay::weatherButtonLabel(const std::string& weather_label) const {
    return config_.weather_button.label_prefix + weather_label;
}

std::string AttendOverlay::viewButtonLabel(const std::string& view_label) const {
    return config_.view_button.label_prefix + view_label;
}

std::string AttendOverlay::pokemonButtonLabel(const std::string& pokemon_label) const {
    return config_.pokemon_button.label_prefix + pokemon_label;
}

std::string AttendOverlay::textureVariantButtonLabel(const std::string& texture_variant_label) const {
    return config_.texture_variant_button.label_prefix + texture_variant_label;
}

std::string AttendOverlay::formVariantButtonLabel(const std::string& form_variant_label) const {
    return config_.form_variant_button.label_prefix + form_variant_label;
}

std::string AttendOverlay::skyButtonLabel(const std::string& sky_label) const {
    return config_.sky_button.label_prefix + sky_label;
}

std::string AttendOverlay::emoteButtonLabel() const {
    return config_.emote_button.label_prefix;
}

std::string AttendOverlay::sleepButtonLabel() const {
    return config_.sleep_button.label_prefix;
}

std::string AttendOverlay::cryButtonLabel() const {
    return config_.cry_button.label_prefix;
}

void AttendOverlay::render(
    SDL_Renderer* renderer,
    const std::string& project_root,
    const std::string& weather_label,
    const std::string& view_label,
    const std::string& pokemon_label,
    const std::string& texture_variant_label,
    const std::string& form_variant_label,
    const std::string& sky_label) {
    if (config_.weather_button.enabled) {
        canvas_.renderButton(renderer, project_root, weatherButton(weather_label));
    }
    if (config_.view_button.enabled) {
        canvas_.renderButton(renderer, project_root, viewButton(view_label));
    }
    if (config_.pokemon_button.enabled) {
        canvas_.renderButton(renderer, project_root, pokemonButton(pokemon_label));
    }
    if (config_.texture_variant_button.enabled) {
        canvas_.renderButton(renderer, project_root, textureVariantButton(texture_variant_label));
    }
    if (config_.form_variant_button.enabled) {
        canvas_.renderButton(renderer, project_root, formVariantButton(form_variant_label));
    }
    if (config_.sky_button.enabled) {
        canvas_.renderButton(renderer, project_root, skyButton(sky_label));
    }
    if (config_.emote_button.enabled) {
        canvas_.renderButton(renderer, project_root, emoteButton());
    }
    if (config_.sleep_button.enabled) {
        canvas_.renderButton(renderer, project_root, sleepButton());
    }
    if (config_.cry_button.enabled) {
        canvas_.renderButton(renderer, project_root, cryButton());
    }
}

SDL_Rect AttendOverlay::weatherButtonRect() const {
    return canvas_.buttonRect(weatherButton({}));
}

SDL_Rect AttendOverlay::viewButtonRect() const {
    return canvas_.buttonRect(viewButton({}));
}

SDL_Rect AttendOverlay::pokemonButtonRect() const {
    return canvas_.buttonRect(pokemonButton({}));
}

SDL_Rect AttendOverlay::textureVariantButtonRect() const {
    return canvas_.buttonRect(textureVariantButton({}));
}

SDL_Rect AttendOverlay::formVariantButtonRect() const {
    return canvas_.buttonRect(formVariantButton({}));
}

SDL_Rect AttendOverlay::skyButtonRect() const {
    return canvas_.buttonRect(skyButton({}));
}

SDL_Rect AttendOverlay::emoteButtonRect() const {
    return canvas_.buttonRect(emoteButton());
}

SDL_Rect AttendOverlay::sleepButtonRect() const {
    return canvas_.buttonRect(sleepButton());
}

SDL_Rect AttendOverlay::cryButtonRect() const {
    return canvas_.buttonRect(cryButton());
}

bool AttendOverlay::hitWeatherButton(int logical_x, int logical_y) const {
    if (!config_.weather_button.enabled) return false;
    const SDL_Rect r = weatherButtonRect();
    return logical_x >= r.x && logical_x < r.x + r.w && logical_y >= r.y && logical_y < r.y + r.h;
}

bool AttendOverlay::hitViewButton(int logical_x, int logical_y) const {
    if (!config_.view_button.enabled) return false;
    const SDL_Rect r = viewButtonRect();
    return logical_x >= r.x && logical_x < r.x + r.w && logical_y >= r.y && logical_y < r.y + r.h;
}

bool AttendOverlay::hitPokemonButton(int logical_x, int logical_y) const {
    if (!config_.pokemon_button.enabled) return false;
    const SDL_Rect r = pokemonButtonRect();
    return logical_x >= r.x && logical_x < r.x + r.w && logical_y >= r.y && logical_y < r.y + r.h;
}

bool AttendOverlay::hitTextureVariantButton(int logical_x, int logical_y) const {
    if (!config_.texture_variant_button.enabled) return false;
    const SDL_Rect r = textureVariantButtonRect();
    return logical_x >= r.x && logical_x < r.x + r.w && logical_y >= r.y && logical_y < r.y + r.h;
}

bool AttendOverlay::hitFormVariantButton(int logical_x, int logical_y) const {
    if (!config_.form_variant_button.enabled) return false;
    const SDL_Rect r = formVariantButtonRect();
    return logical_x >= r.x && logical_x < r.x + r.w && logical_y >= r.y && logical_y < r.y + r.h;
}

bool AttendOverlay::hitSkyButton(int logical_x, int logical_y) const {
    if (!config_.sky_button.enabled) return false;
    const SDL_Rect r = skyButtonRect();
    return logical_x >= r.x && logical_x < r.x + r.w && logical_y >= r.y && logical_y < r.y + r.h;
}

bool AttendOverlay::hitEmoteButton(int logical_x, int logical_y) const {
    if (!config_.emote_button.enabled) return false;
    const SDL_Rect r = emoteButtonRect();
    return logical_x >= r.x && logical_x < r.x + r.w && logical_y >= r.y && logical_y < r.y + r.h;
}

bool AttendOverlay::hitSleepButton(int logical_x, int logical_y) const {
    if (!config_.sleep_button.enabled) return false;
    const SDL_Rect r = sleepButtonRect();
    return logical_x >= r.x && logical_x < r.x + r.w && logical_y >= r.y && logical_y < r.y + r.h;
}

bool AttendOverlay::hitCryButton(int logical_x, int logical_y) const {
    if (!config_.cry_button.enabled) return false;
    const SDL_Rect r = cryButtonRect();
    return logical_x >= r.x && logical_x < r.x + r.w && logical_y >= r.y && logical_y < r.y + r.h;
}

void AttendOverlay::invalidate() {
    canvas_.invalidateText();
}

} // namespace pr
