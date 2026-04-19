#include "ui/TransferSystemScreen.hpp"

#include "core/Json.hpp"

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

int intFromObjectOrDefault(const JsonValue& obj, const std::string& key, int fallback) {
    const JsonValue* value = obj.get(key);
    return value && value->isNumber() ? static_cast<int>(value->asNumber()) : fallback;
}

bool boolFromObjectOrDefault(const JsonValue& obj, const std::string& key, bool fallback) {
    const JsonValue* value = obj.get(key);
    return value ? value->asBool() : fallback;
}

void applyPointFromObject(Point& out, const JsonValue& obj) {
    out.x = intFromObjectOrDefault(obj, "x", out.x);
    out.y = intFromObjectOrDefault(obj, "y", out.y);
}

std::vector<std::string> readStringSlotsFromJsonRoot(const JsonValue& root) {
    std::vector<std::string> slots;
    const JsonValue* arr = root.get("slots");
    if (!arr || !arr->isArray()) {
        return slots;
    }
    for (const JsonValue& item : arr->asArray()) {
        if (item.isString()) {
            slots.push_back(item.asString());
        } else {
            slots.push_back({});
        }
    }
    return slots;
}

std::string spriteFilenameForSlug(const std::string& slug) {
    fs::path sprite_path(slug);
    if (sprite_path.has_extension()) {
        return slug;
    }
    return slug + ".png";
}

void drawTextureTopLeftScaled(
    SDL_Renderer* renderer,
    const TextureHandle& texture,
    int x,
    int y,
    double scale) {
    if (!texture.texture) {
        return;
    }

    SDL_SetTextureBlendMode(texture.texture.get(), SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(texture.texture.get(), 255);
    SDL_SetTextureColorMod(texture.texture.get(), 255, 255, 255);

    const int w = std::max(1, static_cast<int>(std::round(static_cast<double>(texture.width) * scale)));
    const int h = std::max(1, static_cast<int>(std::round(static_cast<double>(texture.height) * scale)));
    SDL_Rect dst{x, y, w, h};
    SDL_RenderCopy(renderer, texture.texture.get(), nullptr, &dst);
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
    return_to_ticket_list_requested_ = false;
    play_button_sfx_requested_ = false;
    elapsed_seconds_ = 0.0;
    reloadBoxSlotTextures(renderer, selection);
    reloadResortSlotTextures(renderer);
}

void TransferSystemScreen::update(double dt) {
    elapsed_seconds_ += dt;
}

void TransferSystemScreen::render(SDL_Renderer* renderer) const {
    // Another screen (e.g. ticket list) may have set a clip rect; clear it so box sprites are drawable.
    SDL_RenderSetClipRect(renderer, nullptr);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    drawBackground(renderer);
    drawBoxSlots(renderer);
}

void TransferSystemScreen::onAdvancePressed() {
}

void TransferSystemScreen::onBackPressed() {
    requestReturnToTicketList();
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

bool TransferSystemScreen::consumeReturnToTicketListRequest() {
    const bool requested = return_to_ticket_list_requested_;
    return_to_ticket_list_requested_ = false;
    return requested;
}

void TransferSystemScreen::loadTransferSystemConfig() {
    const fs::path path = fs::path(project_root_) / "config" / "game_transfer.json";
    if (!fs::exists(path)) {
        return;
    }

    JsonValue root = parseJsonFile(path.string());
    if (!root.isObject()) {
        return;
    }

    if (const JsonValue* fade = root.get("fade_in_seconds")) {
        if (fade->isNumber()) {
            fade_in_seconds_ = std::max(0.0, fade->asNumber());
        }
    }

    if (const JsonValue* background_animation = root.get("background_animation")) {
        if (background_animation->isObject()) {
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
    }

    if (const JsonValue* rsp = root.get("resort_storage_box_path")) {
        if (rsp->isString() && !rsp->asString().empty()) {
            resort_storage_box_path_ = rsp->asString();
        }
    }

    if (const JsonValue* grid = root.get("box_1_grid")) {
        if (grid->isObject()) {
            applyGridFromJson(box_one_grid_, *grid);
        }
    }
    if (const JsonValue* rgrid = root.get("resort_storage_grid")) {
        if (rgrid->isObject()) {
            applyGridFromJson(resort_grid_, *rgrid);
        }
    }
}

void TransferSystemScreen::applyGridFromJson(BoxOneGridConfig& cfg, const JsonValue& grid) {
    if (const JsonValue* start = grid.get("start")) {
        if (start->isObject()) {
            applyPointFromObject(cfg.start, *start);
        }
    }
    cfg.sprite_scale = std::max(0.01, doubleFromObjectOrDefault(grid, "sprite_scale", cfg.sprite_scale));
    cfg.column_spacing = std::max(0, intFromObjectOrDefault(grid, "column_spacing", cfg.column_spacing));
    cfg.row_spacing = std::max(0, intFromObjectOrDefault(grid, "row_spacing", cfg.row_spacing));
    cfg.columns = std::max(1, intFromObjectOrDefault(grid, "columns", cfg.columns));
}

void TransferSystemScreen::reloadBoxSlotTextures(SDL_Renderer* renderer, const TransferSaveSelection& selection) {
    box_slot_textures_.clear();
    box_slot_textures_.reserve(selection.box1_slots.size());

    const fs::path sprite_root = fs::path(project_root_) / "assets" / "sprites";
    std::size_t loaded = 0;
    std::size_t missing_file = 0;

    for (const std::string& slug : selection.box1_slots) {
        if (slug.empty()) {
            box_slot_textures_.push_back(std::nullopt);
            continue;
        }

        const fs::path path = sprite_root / spriteFilenameForSlug(slug);
        if (!fs::exists(path)) {
            std::cerr << "Warning: missing box sprite " << path << '\n';
            ++missing_file;
            box_slot_textures_.push_back(std::nullopt);
            continue;
        }

        try {
            box_slot_textures_.push_back(loadTexture(renderer, path));
            ++loaded;
        } catch (const std::exception& ex) {
            std::cerr << "Warning: could not load box sprite " << path << ": " << ex.what() << '\n';
            box_slot_textures_.push_back(std::nullopt);
        }
    }

    std::cerr << "[TransferSystem] sprites dir=" << sprite_root.string() << " slots=" << selection.box1_slots.size()
              << " textures_loaded=" << loaded;
    if (missing_file > 0) {
        std::cerr << " missing_png=" << missing_file;
    }
    std::cerr << '\n';
}

void TransferSystemScreen::reloadResortSlotTextures(SDL_Renderer* renderer) {
    resort_slot_textures_.clear();

    constexpr std::size_t kResortSlots = 42;
    const fs::path file = resolvePath(project_root_, resort_storage_box_path_);
    if (!fs::exists(file)) {
        std::cerr << "[TransferSystem] resort_storage mock not found: " << file.string() << '\n';
        return;
    }

    std::vector<std::string> slots;
    try {
        const JsonValue root = parseJsonFile(file.string());
        slots = readStringSlotsFromJsonRoot(root);
    } catch (const std::exception& ex) {
        std::cerr << "[TransferSystem] resort_storage JSON error: " << ex.what() << '\n';
        return;
    }

    while (slots.size() < kResortSlots) {
        slots.push_back({});
    }
    if (slots.size() > kResortSlots) {
        slots.resize(kResortSlots);
    }

    resort_slot_textures_.reserve(slots.size());
    const fs::path sprite_root = fs::path(project_root_) / "assets" / "sprites";
    std::size_t loaded = 0;
    for (const std::string& slug : slots) {
        if (slug.empty()) {
            resort_slot_textures_.push_back(std::nullopt);
            continue;
        }
        const fs::path path = sprite_root / spriteFilenameForSlug(slug);
        if (!fs::exists(path)) {
            std::cerr << "Warning: missing resort sprite " << path << '\n';
            resort_slot_textures_.push_back(std::nullopt);
            continue;
        }
        try {
            resort_slot_textures_.push_back(loadTexture(renderer, path));
            ++loaded;
        } catch (const std::exception& ex) {
            std::cerr << "Warning: could not load resort sprite " << path << ": " << ex.what() << '\n';
            resort_slot_textures_.push_back(std::nullopt);
        }
    }

    std::cerr << "[TransferSystem] resort_storage slots=" << slots.size() << " textures_loaded=" << loaded << " file="
              << file.string() << '\n';
}

void TransferSystemScreen::requestReturnToTicketList() {
    play_button_sfx_requested_ = true;
    return_to_ticket_list_requested_ = true;
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

void TransferSystemScreen::drawTextureGrid(
    SDL_Renderer* renderer,
    const BoxOneGridConfig& grid,
    const std::vector<std::optional<TextureHandle>>& textures) const {
    const int cols = std::max(1, grid.columns);
    const double scale = grid.sprite_scale;

    for (std::size_t i = 0; i < textures.size(); ++i) {
        const int col = static_cast<int>(i) % cols;
        const int row = static_cast<int>(i) / cols;
        const int x = grid.start.x + col * grid.column_spacing;
        const int y = grid.start.y + row * grid.row_spacing;

        const auto& tex = textures[i];
        if (tex.has_value()) {
            drawTextureTopLeftScaled(renderer, *tex, x, y, scale);
        }
    }
}

void TransferSystemScreen::drawBoxSlots(SDL_Renderer* renderer) const {
    drawTextureGrid(renderer, box_one_grid_, box_slot_textures_);
    drawTextureGrid(renderer, resort_grid_, resort_slot_textures_);
}

} // namespace pr
