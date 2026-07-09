#include "ui/AttendTestScreen.hpp"

#include "core/app/audio/PokemonCryPlayer.hpp"
#include "core/config/Json.hpp"

#include <SDL.h>
#include <SDL_image.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pr {

namespace {

constexpr const char* kHandCursorPath = "assets/characters/objects/hand.charbin";

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string titleCasePokemonId(std::string value) {
    bool next_upper = true;
    for (char& c : value) {
        if (c == '_' || c == '-') {
            c = ' ';
            next_upper = true;
        } else if (next_upper) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            next_upper = false;
        } else {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return value;
}

std::string pokemonIdFromModelStem(std::string stem) {
    const std::string lowered = lower(stem);
    if (lowered.size() > 10 &&
        lowered.rfind("pm", 0) == 0 &&
        std::isdigit(static_cast<unsigned char>(lowered[2])) &&
        std::isdigit(static_cast<unsigned char>(lowered[3])) &&
        std::isdigit(static_cast<unsigned char>(lowered[4])) &&
        std::isdigit(static_cast<unsigned char>(lowered[5])) &&
        lowered[6] == '_') {
        const std::size_t species_name_sep = lowered.find('_', 7);
        if (species_name_sep != std::string::npos && species_name_sep + 1 < stem.size()) {
            stem = stem.substr(species_name_sep + 1);
        }
    }
    stem = lower(stem);
    for (char& c : stem) {
        if (c == ' ' || c == '-') c = '_';
    }
    return stem;
}

std::string spriteSlugFromPokemonId(std::string value) {
    value = lower(std::move(value));
    for (char& c : value) {
        if (c == '_' || c == ' ') c = '-';
    }
    return value;
}

std::string formKeyToken(std::string value) {
    value = lower(std::move(value));
    for (char& c : value) {
        if (c == '_' || c == ' ') c = '-';
    }
    return value;
}

float smoothEdge(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    return value * value * (3.0f - 2.0f * value);
}

std::uint32_t readU32Le(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

std::vector<std::uint8_t> readBinaryFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return {};
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    in.seekg(0, std::ios::beg);
    if (size <= 0) return {};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(bytes.data()), size);
    return bytes;
}

std::string readLengthPrefixedString(const std::vector<std::uint8_t>& bytes, std::size_t& offset) {
    if (offset + 4 > bytes.size()) return {};
    const std::uint32_t len = readU32Le(bytes.data() + offset);
    offset += 4;
    if (offset + len > bytes.size()) return {};
    std::string out(reinterpret_cast<const char*>(bytes.data() + offset), static_cast<std::size_t>(len));
    offset += len;
    return out;
}

int intOr(const JsonValue* value, int fallback) {
    return value && value->isNumber() ? static_cast<int>(value->asNumber()) : fallback;
}

std::string strOr(const JsonValue* value, const std::string& fallback = {}) {
    return value && value->isString() ? value->asString() : fallback;
}

bool boolOr(const JsonValue* value, bool fallback) {
    return value && value->isBool() ? value->asBool() : fallback;
}

float screenRelativeScale(int width, int height, int design_width, int design_height) {
    const float sx = static_cast<float>(std::max(1, width)) / static_cast<float>(std::max(1, design_width));
    const float sy = static_cast<float>(std::max(1, height)) / static_cast<float>(std::max(1, design_height));
    return std::max(0.01f, std::min(sx, sy));
}

struct CursorAnimation {
    std::vector<int> frames;
    int frame_time_ms = 120;
    bool loop = false;
};

} // namespace

class AttendTestScreen::HandCursor {
public:
    ~HandCursor() { clear(); }

    bool load(
        const std::string& project_root,
        float scale,
        float hotspot_x_ratio,
        float hotspot_y_ratio,
        float pet_animation_speed) {
        clear();
        scale_ = std::clamp(scale, 0.5f, 4.0f);
        hotspot_x_ratio_ = std::clamp(hotspot_x_ratio, 0.0f, 1.0f);
        hotspot_y_ratio_ = std::clamp(hotspot_y_ratio, 0.0f, 1.0f);
        pet_animation_speed_ = std::clamp(pet_animation_speed, 0.1f, 8.0f);
        const std::filesystem::path path = std::filesystem::path(project_root) / kHandCursorPath;
        const std::vector<std::uint8_t> bytes = readBinaryFile(path);
        if (bytes.size() < 20 || std::memcmp(bytes.data(), "SPMKCHAR", 8) != 0) {
            std::cerr << "[AttendTest] Could not load hand cursor charbin: " << path << '\n';
            return false;
        }
        const std::uint32_t json_len = readU32Le(bytes.data() + 12);
        if (16 + static_cast<std::size_t>(json_len) + 4 > bytes.size()) return false;
        const std::string json_blob(
            reinterpret_cast<const char*>(bytes.data() + 16),
            static_cast<std::size_t>(json_len));
        const JsonValue root = parseJsonText(json_blob);
        if (!root.isObject()) return false;

        std::size_t offset = 16 + static_cast<std::size_t>(json_len);
        const std::uint32_t asset_count = readU32Le(bytes.data() + offset);
        offset += 4;
        std::unordered_map<std::string, std::vector<std::uint8_t>> assets;
        for (std::uint32_t i = 0; i < asset_count; ++i) {
            const std::string asset_id = readLengthPrefixedString(bytes, offset);
            (void)readLengthPrefixedString(bytes, offset);
            if (offset + 4 > bytes.size()) return false;
            const std::uint32_t blob_len = readU32Le(bytes.data() + offset);
            offset += 4;
            if (offset + blob_len > bytes.size()) return false;
            assets[asset_id] = std::vector<std::uint8_t>(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                                                         bytes.begin() + static_cast<std::ptrdiff_t>(offset + blob_len));
            offset += blob_len;
        }

        const JsonValue* sheets = root.get("spriteSheets");
        if (!sheets || !sheets->isArray() || sheets->asArray().empty() || !sheets->asArray().front().isObject()) {
            return false;
        }
        const JsonValue& sheet = sheets->asArray().front();
        const std::string asset_id = strOr(sheet.get("assetId"));
        auto asset = assets.find(asset_id);
        if (asset == assets.end()) return false;

        frame_width_ = 32;
        frame_height_ = 32;
        columns_ = 4;
        if (const JsonValue* overrides = sheet.get("profileOverrides"); overrides && overrides->isObject()) {
            frame_width_ = intOr(overrides->get("frameWidth"), frame_width_);
            frame_height_ = intOr(overrides->get("frameHeight"), frame_height_);
            columns_ = intOr(overrides->get("columns"), columns_);
        }

        const JsonValue* sheet_anims = sheet.get("animations");
        if (sheet_anims && sheet_anims->isObject()) {
            for (const auto& [name, value] : sheet_anims->asObject()) {
                if (!value.isObject()) continue;
                CursorAnimation anim;
                anim.frame_time_ms = std::max(1, intOr(value.get("frameTimeMs"), 120));
                anim.loop = boolOr(value.get("loop"), false);
                if (const JsonValue* frames = value.get("frames"); frames && frames->isArray()) {
                    for (const JsonValue& frame : frames->asArray()) {
                        if (frame.isNumber()) anim.frames.push_back(static_cast<int>(frame.asNumber()));
                    }
                }
                if (!anim.frames.empty()) {
                    if (name == "pet") {
                        anim.frame_time_ms = std::max(
                            1,
                            static_cast<int>(std::round(static_cast<float>(anim.frame_time_ms) / pet_animation_speed_)));
                    }
                    animations_[name] = std::move(anim);
                }
            }
        }

        SDL_RWops* rw = SDL_RWFromConstMem(asset->second.data(), static_cast<int>(asset->second.size()));
        if (!rw) return false;
        SDL_Surface* loaded = IMG_Load_RW(rw, 1);
        if (!loaded) return false;
        SDL_Surface* sheet_surface = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
        SDL_FreeSurface(loaded);
        if (!sheet_surface) return false;

        const int frame_count = (sheet_surface->w / std::max(1, frame_width_)) *
            (sheet_surface->h / std::max(1, frame_height_));
        cursors_.resize(static_cast<std::size_t>(std::max(0, frame_count)), nullptr);
        for (int frame = 0; frame < frame_count; ++frame) {
            SDL_Surface* frame_surface = SDL_CreateRGBSurfaceWithFormat(
                0,
                frame_width_,
                frame_height_,
                32,
                SDL_PIXELFORMAT_RGBA32);
            if (!frame_surface) continue;
            const SDL_Rect src{
                (frame % columns_) * frame_width_,
                (frame / columns_) * frame_height_,
                frame_width_,
                frame_height_};
            SDL_BlitSurface(sheet_surface, &src, frame_surface, nullptr);
            SDL_Surface* cursor_surface = frame_surface;
            SDL_Surface* scaled_surface = nullptr;
            const int scaled_w = std::max(1, static_cast<int>(std::round(static_cast<float>(frame_width_) * scale_)));
            const int scaled_h = std::max(1, static_cast<int>(std::round(static_cast<float>(frame_height_) * scale_)));
            if (scaled_w != frame_width_ || scaled_h != frame_height_) {
                scaled_surface = SDL_CreateRGBSurfaceWithFormat(0, scaled_w, scaled_h, 32, SDL_PIXELFORMAT_RGBA32);
                if (scaled_surface) {
                    SDL_SetSurfaceBlendMode(frame_surface, SDL_BLENDMODE_NONE);
                    SDL_Rect dst{0, 0, scaled_w, scaled_h};
                    SDL_BlitScaled(frame_surface, nullptr, scaled_surface, &dst);
                    cursor_surface = scaled_surface;
                }
            }
            const int hot_x = std::clamp(
                static_cast<int>(std::round(static_cast<float>(cursor_surface->w - 1) * hotspot_x_ratio_)),
                0,
                cursor_surface->w - 1);
            const int hot_y = std::clamp(
                static_cast<int>(std::round(static_cast<float>(cursor_surface->h - 1) * hotspot_y_ratio_)),
                0,
                cursor_surface->h - 1);
            cursors_[static_cast<std::size_t>(frame)] = SDL_CreateColorCursor(cursor_surface, hot_x, hot_y);
            if (scaled_surface) SDL_FreeSurface(scaled_surface);
            SDL_FreeSurface(frame_surface);
        }
        SDL_FreeSurface(sheet_surface);

        loaded_ = !animations_.empty() && !cursors_.empty();
        setAnimation("open");
        return loaded_;
    }

    void update(double dt) {
        if (!loaded_) return;
        const CursorAnimation* anim = currentAnimation();
        if (!anim || anim->frames.empty()) return;
        elapsed_ms_ += dt * 1000.0;
        while (elapsed_ms_ >= anim->frame_time_ms) {
            elapsed_ms_ -= static_cast<double>(anim->frame_time_ms);
            if (frame_index_ + 1U < anim->frames.size()) {
                ++frame_index_;
            } else if (anim->loop) {
                frame_index_ = 0U;
            } else {
                finished_once_ = true;
                frame_index_ = anim->frames.size() - 1U;
                break;
            }
        }
        applyCursor();
    }

    void setAnimation(const std::string& name) {
        if (!loaded_ || current_name_ == name || animations_.find(name) == animations_.end()) return;
        current_name_ = name;
        frame_index_ = 0;
        elapsed_ms_ = 0.0;
        finished_once_ = false;
        applyCursor();
    }

    bool finished() const { return finished_once_; }

    void clear() {
        for (SDL_Cursor* cursor : cursors_) {
            if (cursor) SDL_FreeCursor(cursor);
        }
        cursors_.clear();
        animations_.clear();
        loaded_ = false;
    }

private:
    const CursorAnimation* currentAnimation() const {
        auto it = animations_.find(current_name_);
        return it == animations_.end() ? nullptr : &it->second;
    }

    void applyCursor() {
        const CursorAnimation* anim = currentAnimation();
        if (!anim || anim->frames.empty()) return;
        const int frame = anim->frames[frame_index_ % anim->frames.size()];
        if (frame < 0 || frame >= static_cast<int>(cursors_.size())) return;
        SDL_Cursor* cursor = cursors_[static_cast<std::size_t>(frame)];
        if (cursor) {
            SDL_SetCursor(cursor);
            SDL_ShowCursor(SDL_ENABLE);
        }
    }

    bool loaded_ = false;
    int frame_width_ = 32;
    int frame_height_ = 32;
    int columns_ = 4;
    float scale_ = 1.0f;
    float hotspot_x_ratio_ = 0.5f;
    float hotspot_y_ratio_ = 0.5f;
    float pet_animation_speed_ = 1.0f;
    std::unordered_map<std::string, CursorAnimation> animations_;
    std::vector<SDL_Cursor*> cursors_;
    std::string current_name_;
    std::size_t frame_index_ = 0;
    double elapsed_ms_ = 0.0;
    bool finished_once_ = false;
};

AttendTestScreen::AttendTestScreen(std::string project_root, AppConfig app_config)
    : project_root_(std::move(project_root)),
      app_config_(std::move(app_config)),
      scene_config_(gameplay::attend::loadAttendSceneConfig(project_root_)),
      pointer_space_w_(std::max(1, app_config_.window.virtual_width)),
      pointer_space_h_(std::max(1, app_config_.window.virtual_height)),
      overlay_(pointer_space_w_, pointer_space_h_) {
    weather_index_ = scene_config_.floor.active_weather;
    overlay_.setConfig(scene_config_.ui);
    refreshAvailablePokemonModels();
    resetPokemonInactivity();
}

AttendTestScreen::~AttendTestScreen() {
    restoreSystemCursor();
}

void AttendTestScreen::update(double dt) {
    scene_time_seconds_ += dt;
    if (!hand_cursor_) {
        hand_cursor_ = std::make_unique<HandCursor>();
        if (!hand_cursor_->load(
                project_root_,
                scene_config_.ui.hand_cursor_scale,
                scene_config_.ui.hand_cursor_hotspot_x_ratio,
                scene_config_.ui.hand_cursor_hotspot_y_ratio,
                scene_config_.ui.hand_cursor_pet_animation_speed)) {
            hand_cursor_.reset();
        }
        if (hand_cursor_) {
            hand_cursor_->setAnimation(
                pointer_pet_active_ ? "pet" : (pointer_over_pokemon_ ? "hover" : "open"));
        }
    }
    if (hand_cursor_) {
        if (pointer_pet_active_ && hand_cursor_->finished()) {
            hand_cursor_->setAnimation("hold_pet");
        }
        hand_cursor_->update(dt);
    }
    updateFreeCamera(dt);
    updateCornerButtonVisibility(dt);
    updateIdleBehavior();
}

void AttendTestScreen::render(SDL_Renderer* renderer) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    renderPresentationOverlay(renderer);
}

void AttendTestScreen::renderPresentationOverlay(SDL_Renderer* renderer) {
    if (!debug_ui_visible_ ||
        !renderer || !environmentControlsEnabled() ||
        (!scene_config_.ui.weather_button.enabled &&
         !scene_config_.ui.view_button.enabled &&
         !scene_config_.ui.pokemon_button.enabled &&
         !scene_config_.ui.texture_variant_button.enabled &&
         !scene_config_.ui.form_variant_button.enabled &&
         !scene_config_.ui.sky_button.enabled &&
         !scene_config_.ui.emote_button.enabled &&
         !scene_config_.ui.sleep_button.enabled &&
         !scene_config_.ui.cry_button.enabled)) {
        return;
    }
    int logical_w = 0;
    int logical_h = 0;
    SDL_RenderGetLogicalSize(renderer, &logical_w, &logical_h);
    if (logical_w > 0 && logical_h > 0) {
        updatePointerSpace(logical_w, logical_h);
    }
    overlay_.render(
        renderer,
        project_root_,
        currentWeatherLabel(),
        currentViewLabel(),
        currentPokemonLabel(),
        currentTextureVariantLabel(),
        currentFormVariantLabel(),
        currentSkyLabel());
}

bool AttendTestScreen::renderBgfx(
    SDL_Window* window,
    int framebuffer_w,
    int framebuffer_h,
    void* sdl_metal_view) {
    if (!wantsBgfxRenderer()) {
        return false;
    }
    SDL_GetWindowSize(window, &bgfx_window_w_, &bgfx_window_h_);
    bgfx_window_w_ = std::max(1, bgfx_window_w_);
    bgfx_window_h_ = std::max(1, bgfx_window_h_);
    updatePointerSpace(app_config_.window.virtual_width, app_config_.window.virtual_height);
    if (!bgfx_renderer_) {
#if defined(__APPLE__)
        if (!sdl_metal_view) {
            std::cerr << "[AttendTest] Refusing bgfx::init on macOS without SDL_MetalView.\n";
            return false;
        }
#endif
        bgfx_renderer_ =
            std::make_unique<gameplay::attend::rendering::AttendBgfxRenderer>(project_root_, scene_config_);
        if (!bgfx_renderer_->initialize(
                window,
                framebuffer_w,
                framebuffer_h,
                app_config_.renderer.bgfx_preference,
                sdl_metal_view)) {
            std::cerr << "[AttendTest] bgfx renderer initialization failed: "
                      << bgfx_renderer_->lastError()
                      << ". Falling back to black SDL placeholder.\n";
            bgfx_renderer_.reset();
            bgfx_init_failed_ = true;
            return false;
        }
        texture_variant_index_ = bgfx_renderer_->textureVariantIndex();
        form_variant_index_ = bgfx_renderer_->formVariantIndex();
    }
    if (!pending_bgfx_screenshot_.empty()) {
        bgfx_renderer_->queueScreenshot(pending_bgfx_screenshot_);
        pending_bgfx_screenshot_.clear();
    }
    bgfx_renderer_->setPetting(pointer_pet_active_);
    bgfx_renderer_->setPetContact(pointer_pet_contact_bias_);
    bgfx_renderer_->setFaceLook(pointer_face_x_, pointer_face_y_);
    bgfx_renderer_->setViewportLook(viewport_look_x_, viewport_look_y_);
    bgfx_renderer_->setFreeCamera(freecam_enabled_, freecam_pose_);
    bgfx_renderer_->setWeatherMode(weather_index_);
    if (texture_variant_index_ >= 0) {
        bgfx_renderer_->setTextureVariant(texture_variant_index_);
    } else {
        texture_variant_index_ = bgfx_renderer_->textureVariantIndex();
    }
    if (form_variant_index_ >= 0) {
        bgfx_renderer_->setFormVariant(form_variant_index_);
    } else {
        form_variant_index_ = bgfx_renderer_->formVariantIndex();
    }
    bgfx_renderer_->setFaceView(face_view_);
    if (debug_fps_sample_start_seconds_ < 0.0) {
        debug_fps_sample_start_seconds_ = scene_time_seconds_;
    }
    ++debug_fps_frame_count_;
    const double fps_sample_seconds = scene_time_seconds_ - debug_fps_sample_start_seconds_;
    if (fps_sample_seconds >= 0.5) {
        debug_fps_ = static_cast<double>(debug_fps_frame_count_) / fps_sample_seconds;
        debug_fps_frame_count_ = 0;
        debug_fps_sample_start_seconds_ = scene_time_seconds_;
    }
    std::vector<gameplay::attend::rendering::AttendBgfxOverlayButton> buttons;
    if (debug_ui_visible_ && environmentControlsEnabled()) {
        if (scene_config_.ui.weather_button.enabled) {
            const SDL_Rect weather = overlay_.weatherButtonRect();
            buttons.push_back(gameplay::attend::rendering::AttendBgfxOverlayButton{
                weather.x,
                weather.y,
                weather.w,
                weather.h,
                overlay_.weatherButtonLabel(currentWeatherLabel()),
                scene_config_.ui.weather_button});
        }
        if (faceViewControlEnabled()) {
            const SDL_Rect view = overlay_.viewButtonRect();
            buttons.push_back(gameplay::attend::rendering::AttendBgfxOverlayButton{
                view.x,
                view.y,
                view.w,
                view.h,
                overlay_.viewButtonLabel(currentViewLabel()),
                scene_config_.ui.view_button});
        }
        if (scene_config_.ui.pokemon_button.enabled) {
            const SDL_Rect pokemon = overlay_.pokemonButtonRect();
            buttons.push_back(gameplay::attend::rendering::AttendBgfxOverlayButton{
                pokemon.x,
                pokemon.y,
                pokemon.w,
                pokemon.h,
                overlay_.pokemonButtonLabel(currentPokemonLabel()),
                scene_config_.ui.pokemon_button});
        }
        if (scene_config_.ui.texture_variant_button.enabled && bgfx_renderer_->textureVariantCount() > 1) {
            const SDL_Rect variant = overlay_.textureVariantButtonRect();
            buttons.push_back(gameplay::attend::rendering::AttendBgfxOverlayButton{
                variant.x,
                variant.y,
                variant.w,
                variant.h,
                overlay_.textureVariantButtonLabel(currentTextureVariantLabel()),
                scene_config_.ui.texture_variant_button});
        }
        if (scene_config_.ui.form_variant_button.enabled && bgfx_renderer_->formVariantCount() > 1) {
            const SDL_Rect variant = overlay_.formVariantButtonRect();
            buttons.push_back(gameplay::attend::rendering::AttendBgfxOverlayButton{
                variant.x,
                variant.y,
                variant.w,
                variant.h,
                overlay_.formVariantButtonLabel(currentFormVariantLabel()),
                scene_config_.ui.form_variant_button});
        }
        if (scene_config_.ui.sky_button.enabled && scene_config_.sky_presets.size() > 1) {
            const SDL_Rect sky = overlay_.skyButtonRect();
            buttons.push_back(gameplay::attend::rendering::AttendBgfxOverlayButton{
                sky.x,
                sky.y,
                sky.w,
                sky.h,
                overlay_.skyButtonLabel(currentSkyLabel()),
                scene_config_.ui.sky_button});
        }
        if (scene_config_.ui.emote_button.enabled) {
            const SDL_Rect emote = overlay_.emoteButtonRect();
            buttons.push_back(gameplay::attend::rendering::AttendBgfxOverlayButton{
                emote.x,
                emote.y,
                emote.w,
                emote.h,
                overlay_.emoteButtonLabel(),
                scene_config_.ui.emote_button});
        }
        if (scene_config_.ui.sleep_button.enabled) {
            const SDL_Rect sleep = overlay_.sleepButtonRect();
            buttons.push_back(gameplay::attend::rendering::AttendBgfxOverlayButton{
                sleep.x,
                sleep.y,
                sleep.w,
                sleep.h,
                overlay_.sleepButtonLabel(),
                scene_config_.ui.sleep_button});
        }
        if (scene_config_.ui.cry_button.enabled) {
            const SDL_Rect cry = overlay_.cryButtonRect();
            buttons.push_back(gameplay::attend::rendering::AttendBgfxOverlayButton{
                cry.x,
                cry.y,
                cry.w,
                cry.h,
                overlay_.cryButtonLabel(),
                scene_config_.ui.cry_button});
        }
        gameplay::attend::AttendOverlayButtonConfig frame_counter_style = scene_config_.ui.weather_button;
        frame_counter_style.enabled = true;
        frame_counter_style.label_prefix.clear();
        frame_counter_style.width = std::max(frame_counter_style.width, 170);
        const int bottom_y = std::max({
            scene_config_.ui.weather_button.enabled
                ? overlay_.weatherButtonRect().y + overlay_.weatherButtonRect().h
                : 0,
            faceViewControlEnabled()
                ? overlay_.viewButtonRect().y + overlay_.viewButtonRect().h
                : 0,
            scene_config_.ui.pokemon_button.enabled
                ? overlay_.pokemonButtonRect().y + overlay_.pokemonButtonRect().h
                : 0,
            scene_config_.ui.texture_variant_button.enabled && bgfx_renderer_->textureVariantCount() > 1
                ? overlay_.textureVariantButtonRect().y + overlay_.textureVariantButtonRect().h
                : 0,
            scene_config_.ui.form_variant_button.enabled && bgfx_renderer_->formVariantCount() > 1
                ? overlay_.formVariantButtonRect().y + overlay_.formVariantButtonRect().h
                : 0});
        const int frame_w = frame_counter_style.width;
        const int frame_h = frame_counter_style.height;
        const int frame_x = std::clamp(
            pointer_space_w_ - frame_counter_style.margin_x - frame_w,
            0,
            std::max(0, pointer_space_w_ - frame_w));
        const int frame_y = std::clamp(
            bottom_y + 8,
            0,
            std::max(0, pointer_space_h_ - frame_h));
        buttons.push_back(gameplay::attend::rendering::AttendBgfxOverlayButton{
            frame_x,
            frame_y,
            frame_w,
            frame_h,
            "FPS: " + std::to_string(static_cast<int>(debug_fps_ + 0.5)),
            frame_counter_style});
    }
    bgfx_renderer_->setOverlayButtons(std::move(buttons), pointer_space_w_, pointer_space_h_);
    std::vector<gameplay::attend::rendering::AttendBgfxCornerButton> corner_buttons;
    const auto add_corner_button = [&](bool left, bool top, const gameplay::attend::AttendCornerButtonConfig& style) {
        if (!style.enabled) return;
        constexpr float kBaseButtonHeight = 112.0f;
        const float responsive_scale = screenRelativeScale(
            pointer_space_w_,
            pointer_space_h_,
            app_config_.window.virtual_width,
            app_config_.window.virtual_height);
        const int button_h = std::max(
            1,
            static_cast<int>(std::round(kBaseButtonHeight * style.scale * responsive_scale)));
        const int button_w = std::max(
            button_h,
            static_cast<int>(std::round(static_cast<float>(button_h) * (1.0f + style.side_extension_ratio))));
        const float hidden = 1.0f - std::clamp(corner_buttons_visibility_, 0.0f, 1.0f);
        const int base_x = left ? 0 : std::max(0, pointer_space_w_ - button_w);
        const int base_y = top ? 0 : std::max(0, pointer_space_h_ - button_h);
        const int x = base_x + static_cast<int>(std::round((left ? -button_w : button_w) * hidden));
        const int y = base_y + static_cast<int>(std::round((top ? -button_h : button_h) * hidden));
        corner_buttons.push_back(gameplay::attend::rendering::AttendBgfxCornerButton{
            left,
            top,
            x,
            y,
            button_w,
            button_h,
            style});
    };
    add_corner_button(true, false, scene_config_.ui.action_button);
    add_corner_button(false, false, scene_config_.ui.items_button);
    add_corner_button(true, true, scene_config_.ui.return_button);
    bgfx_renderer_->setCornerButtons(std::move(corner_buttons), pointer_space_w_, pointer_space_h_);
    gameplay::attend::rendering::AttendBgfxProfilePlate profile_plate{};
    profile_plate.enabled = scene_config_.ui.profile_plate.enabled;
    if (profile_plate.enabled) {
        const float responsive_scale = screenRelativeScale(
            pointer_space_w_,
            pointer_space_h_,
            app_config_.window.virtual_width,
            app_config_.window.virtual_height);
        auto scale_int = [&](int value) {
            return std::max(1, static_cast<int>(std::round(static_cast<float>(value) * responsive_scale)));
        };
        auto scale_non_negative = [&](int value) {
            return std::max(0, static_cast<int>(std::round(static_cast<float>(value) * responsive_scale)));
        };
        profile_plate.style = scene_config_.ui.profile_plate;
        profile_plate.style.width = scale_int(profile_plate.style.width);
        profile_plate.style.height = scale_int(profile_plate.style.height);
        profile_plate.style.margin_x = scale_non_negative(profile_plate.style.margin_x);
        profile_plate.style.margin_y = scale_non_negative(profile_plate.style.margin_y);
        profile_plate.style.padding_x = scale_non_negative(profile_plate.style.padding_x);
        profile_plate.style.padding_y = scale_non_negative(profile_plate.style.padding_y);
        profile_plate.style.corner_radius = scale_non_negative(profile_plate.style.corner_radius);
        profile_plate.style.outer_stroke_width = scale_non_negative(profile_plate.style.outer_stroke_width);
        profile_plate.style.inner_stroke_width = scale_non_negative(profile_plate.style.inner_stroke_width);
        profile_plate.style.sprite_size = scale_int(profile_plate.style.sprite_size);
        profile_plate.style.sprite_gap = scale_non_negative(profile_plate.style.sprite_gap);
        profile_plate.style.sprite_offset_x = scale_int(profile_plate.style.sprite_offset_x);
        profile_plate.style.sprite_offset_y = scale_int(profile_plate.style.sprite_offset_y);
        profile_plate.style.name_text_offset_x = scale_int(profile_plate.style.name_text_offset_x);
        profile_plate.style.name_text_offset_y = scale_int(profile_plate.style.name_text_offset_y);
        profile_plate.style.detail_text_offset_x = scale_int(profile_plate.style.detail_text_offset_x);
        profile_plate.style.detail_text_offset_y = scale_int(profile_plate.style.detail_text_offset_y);
        profile_plate.style.name_font_size = scale_int(profile_plate.style.name_font_size);
        profile_plate.style.detail_font_size = scale_int(profile_plate.style.detail_font_size);
        profile_plate.style.name_row_height = scale_int(profile_plate.style.name_row_height);
        profile_plate.style.detail_row_height = scale_int(profile_plate.style.detail_row_height);
        profile_plate.style.name_row_width = scale_int(profile_plate.style.name_row_width);
        profile_plate.style.characteristic_row_width = scale_int(profile_plate.style.characteristic_row_width);
        profile_plate.style.nature_row_width = scale_int(profile_plate.style.nature_row_width);
        profile_plate.style.row_gap = scale_non_negative(profile_plate.style.row_gap);
        profile_plate.style.left_fade_width = scale_non_negative(profile_plate.style.left_fade_width);
        profile_plate.style.slide_out_x = scale_non_negative(profile_plate.style.slide_out_x);
        profile_plate.w = profile_plate.style.width;
        profile_plate.h = profile_plate.style.height;
        const float hidden = 1.0f - std::clamp(corner_buttons_visibility_, 0.0f, 1.0f);
        const int base_x = std::max(0, pointer_space_w_ - profile_plate.style.margin_x - profile_plate.w);
        const int base_y = profile_plate.style.margin_y;
        profile_plate.x = base_x + static_cast<int>(std::round(static_cast<float>(profile_plate.w + profile_plate.style.slide_out_x) * hidden));
        profile_plate.y = base_y - static_cast<int>(std::round(static_cast<float>(profile_plate.h + profile_plate.style.margin_y) * hidden));
        profile_plate.name = currentPokemonLabel();
        profile_plate.characteristic = profile_plate.style.characteristic;
        profile_plate.nature = profile_plate.style.nature;
        profile_plate.sprite_path = currentPokemonSpritePath();
    }
    bgfx_renderer_->setProfilePlate(std::move(profile_plate), pointer_space_w_, pointer_space_h_);
    bgfx_renderer_->render(scene_time_seconds_, framebuffer_w, framebuffer_h);
    return true;
}

bool AttendTestScreen::wantsBgfxRenderer() const {
    const std::string backend = lower(app_config_.renderer.world3d_backend);
    return !bgfx_init_failed_ && (backend == "bgfx" || backend == "auto");
}

void AttendTestScreen::queueBgfxScreenshot(const std::string& output_path) {
    pending_bgfx_screenshot_ = output_path;
}

void AttendTestScreen::shutdownBgfx() {
    if (bgfx_renderer_) {
        bgfx_renderer_->shutdown();
        bgfx_renderer_.reset();
    }
}

bool AttendTestScreen::capturesUnroutedKeyboardFocus() const {
    return true;
}

bool AttendTestScreen::handleUnroutedSdlEvent(const SDL_Event& event) {
    if (event.type == SDL_KEYDOWN && event.key.repeat == 0 && event.key.keysym.sym == SDLK_1) {
        reloadSceneConfig();
        return true;
    }
    if (event.type == SDL_KEYDOWN && event.key.repeat == 0 && event.key.keysym.sym == SDLK_q) {
        toggleFreeCamera();
        return true;
    }
    if (event.type == SDL_KEYDOWN && event.key.repeat == 0 && !freecam_enabled_ && event.key.keysym.sym == SDLK_d) {
        debug_ui_visible_ = !debug_ui_visible_;
        overlay_.invalidate();
        return true;
    }
    if (event.type == SDL_KEYDOWN && event.key.repeat == 0 && debug_ui_visible_ && environmentControlsEnabled()) {
        switch (event.key.keysym.sym) {
            case SDLK_2:
                cycleWeatherMode();
                return true;
            case SDLK_3:
                if (faceViewControlEnabled()) {
                    toggleViewMode();
                    return true;
                }
                break;
            case SDLK_4:
                cyclePokemonModel();
                return true;
            case SDLK_5:
                cycleTextureVariant();
                return true;
            case SDLK_6:
                cycleFormVariant();
                return true;
            case SDLK_7:
                cycleSkyPreset();
                return true;
            case SDLK_8:
                triggerIdleEmote();
                resetPokemonInactivity();
                return true;
            case SDLK_9:
                triggerIdleSleep();
                resetPokemonInactivity();
                return true;
            case SDLK_0:
                requestActivePokemonCry();
                resetPokemonInactivity();
                return true;
            default:
                break;
        }
    }
    if (freecam_enabled_ && event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        freecam_mouse_dragging_ = true;
        return true;
    }
    if (freecam_enabled_ && event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
        freecam_mouse_dragging_ = false;
        return true;
    }
    if (freecam_enabled_ && event.type == SDL_MOUSEMOTION && freecam_mouse_dragging_) {
        if ((event.motion.state & SDL_BUTTON_LMASK) == 0) {
            freecam_mouse_dragging_ = false;
            return true;
        }
        freecam_pose_.yaw_degrees += static_cast<float>(event.motion.xrel) * scene_config_.camera.freecam_mouse_sensitivity;
        freecam_pose_.pitch_degrees -= static_cast<float>(event.motion.yrel) * scene_config_.camera.freecam_mouse_sensitivity;
        freecam_pose_.pitch_degrees = std::clamp(
            freecam_pose_.pitch_degrees,
            scene_config_.camera.freecam_pitch_min_degrees,
            scene_config_.camera.freecam_pitch_max_degrees);
        return true;
    }
    return false;
}

void AttendTestScreen::onBackPressed() {
    if (freecam_enabled_) {
        freecam_enabled_ = false;
        freecam_mouse_dragging_ = false;
    }
    restoreSystemCursor();
    return_requested_ = true;
}

void AttendTestScreen::handlePointerMoved(int logical_x, int logical_y) {
    mapPointerToLogical(logical_x, logical_y);
    if (freecam_enabled_) return;
    updatePointerFaceTarget(logical_x, logical_y);
    updatePointerEdgeLook(logical_x, logical_y);
    if (pointerOverOverlayButton(logical_x, logical_y)) {
        pointer_over_pokemon_ = false;
        if (pointer_pet_active_) {
            pointer_pet_active_ = false;
            pointer_pet_contact_bias_ = 0.0f;
            pointer_pet_started_seconds_ = -1.0;
            if (bgfx_renderer_) {
                bgfx_renderer_->setPetting(false);
                bgfx_renderer_->setPetContact(0.0f);
            }
        }
        if (hand_cursor_) hand_cursor_->setAnimation("open");
        return;
    }
    pointer_over_pokemon_ = pointerOverPokemon(logical_x, logical_y);
    if (pointer_pet_active_) {
        if (pointer_over_pokemon_) {
            updatePetContactBias(logical_x, logical_y);
        } else {
            pointer_pet_active_ = false;
            pointer_pet_contact_bias_ = 0.0f;
            pointer_pet_started_seconds_ = -1.0;
            if (hand_cursor_) hand_cursor_->setAnimation("open");
            if (bgfx_renderer_) {
                bgfx_renderer_->setPetting(false);
                bgfx_renderer_->setPetContact(0.0f);
            }
        }
    }
    if (hand_cursor_ && !pointer_pet_active_) {
        hand_cursor_->setAnimation(pointer_over_pokemon_ ? "hover" : "open");
    }
}

bool AttendTestScreen::handlePointerPressed(int logical_x, int logical_y) {
    mapPointerToLogical(logical_x, logical_y);
    if (freecam_enabled_) {
        freecam_mouse_dragging_ = true;
        return true;
    }
    updatePointerFaceTarget(logical_x, logical_y);
    updatePointerEdgeLook(logical_x, logical_y);
    if (pointerOverWeatherButton(logical_x, logical_y)) {
        cycleWeatherMode();
        pointer_pet_active_ = false;
        pointer_pet_contact_bias_ = 0.0f;
        pointer_pet_started_seconds_ = -1.0;
        pointer_over_pokemon_ = false;
        if (bgfx_renderer_) {
            bgfx_renderer_->setPetting(false);
            bgfx_renderer_->setPetContact(0.0f);
        }
        return true;
    }
    if (pointerOverViewButton(logical_x, logical_y)) {
        toggleViewMode();
        pointer_pet_active_ = false;
        pointer_pet_contact_bias_ = 0.0f;
        pointer_pet_started_seconds_ = -1.0;
        pointer_over_pokemon_ = false;
        if (bgfx_renderer_) {
            bgfx_renderer_->setPetting(false);
            bgfx_renderer_->setPetContact(0.0f);
        }
        return true;
    }
    if (pointerOverPokemonButton(logical_x, logical_y)) {
        cyclePokemonModel();
        pointer_pet_active_ = false;
        pointer_pet_contact_bias_ = 0.0f;
        pointer_pet_started_seconds_ = -1.0;
        pointer_over_pokemon_ = false;
        if (bgfx_renderer_) {
            bgfx_renderer_->setPetting(false);
            bgfx_renderer_->setPetContact(0.0f);
        }
        return true;
    }
    if (pointerOverTextureVariantButton(logical_x, logical_y)) {
        cycleTextureVariant();
        pointer_pet_active_ = false;
        pointer_pet_contact_bias_ = 0.0f;
        pointer_pet_started_seconds_ = -1.0;
        pointer_over_pokemon_ = false;
        if (bgfx_renderer_) {
            bgfx_renderer_->setPetting(false);
            bgfx_renderer_->setPetContact(0.0f);
        }
        return true;
    }
    if (pointerOverFormVariantButton(logical_x, logical_y)) {
        cycleFormVariant();
        pointer_pet_active_ = false;
        pointer_pet_contact_bias_ = 0.0f;
        pointer_pet_started_seconds_ = -1.0;
        pointer_over_pokemon_ = false;
        if (bgfx_renderer_) {
            bgfx_renderer_->setPetting(false);
            bgfx_renderer_->setPetContact(0.0f);
        }
        return true;
    }
    if (pointerOverSkyButton(logical_x, logical_y)) {
        cycleSkyPreset();
        pointer_pet_active_ = false;
        pointer_pet_contact_bias_ = 0.0f;
        pointer_pet_started_seconds_ = -1.0;
        pointer_over_pokemon_ = false;
        if (bgfx_renderer_) {
            bgfx_renderer_->setPetting(false);
            bgfx_renderer_->setPetContact(0.0f);
        }
        return true;
    }
    if (pointerOverEmoteButton(logical_x, logical_y)) {
        triggerIdleEmote();
        resetPokemonInactivity();
        pointer_pet_active_ = false;
        pointer_pet_contact_bias_ = 0.0f;
        pointer_pet_started_seconds_ = -1.0;
        pointer_over_pokemon_ = false;
        if (bgfx_renderer_) {
            bgfx_renderer_->setPetting(false);
            bgfx_renderer_->setPetContact(0.0f);
        }
        return true;
    }
    if (pointerOverSleepButton(logical_x, logical_y)) {
        triggerIdleSleep();
        resetPokemonInactivity();
        pointer_pet_active_ = false;
        pointer_pet_contact_bias_ = 0.0f;
        pointer_pet_started_seconds_ = -1.0;
        pointer_over_pokemon_ = false;
        if (bgfx_renderer_) {
            bgfx_renderer_->setPetting(false);
            bgfx_renderer_->setPetContact(0.0f);
        }
        return true;
    }
    if (pointerOverCryButton(logical_x, logical_y)) {
        requestActivePokemonCry();
        resetPokemonInactivity();
        pointer_pet_active_ = false;
        pointer_pet_contact_bias_ = 0.0f;
        pointer_pet_started_seconds_ = -1.0;
        pointer_over_pokemon_ = false;
        if (bgfx_renderer_) {
            bgfx_renderer_->setPetting(false);
            bgfx_renderer_->setPetContact(0.0f);
        }
        return true;
    }
    pointer_press_seconds_ = scene_time_seconds_;
    pointer_press_x_ = logical_x;
    pointer_press_y_ = logical_y;
    pointer_press_face_exit_candidate_ = face_view_ && faceViewControlEnabled();
    pointer_press_head_focus_candidate_ = !face_view_ && pointerOverPokemonHead(logical_x, logical_y);
    pointer_over_pokemon_ = pointerOverPokemon(logical_x, logical_y);
    if (pointer_over_pokemon_) {
        if (scene_time_seconds_ < wake_pet_block_until_seconds_ || wakePokemonFromIdle()) {
            pointer_pet_active_ = false;
            pointer_pet_contact_bias_ = 0.0f;
            pointer_pet_started_seconds_ = -1.0;
            if (hand_cursor_) hand_cursor_->setAnimation("hover");
            if (bgfx_renderer_) {
                bgfx_renderer_->setPetting(false);
                bgfx_renderer_->setPetContact(0.0f);
            }
            return true;
        }
        pointer_pet_active_ = true;
        pointer_pet_started_seconds_ = scene_time_seconds_;
        updatePetContactBias(logical_x, logical_y);
        if (hand_cursor_) hand_cursor_->setAnimation("pet");
        if (bgfx_renderer_) {
            const double fade_seconds = std::min(0.18, static_cast<double>(scene_config_.idle_behavior.fade_out_seconds));
            bgfx_renderer_->blendOutIdleAnimation(scene_time_seconds_, fade_seconds);
            bgfx_renderer_->setPetting(true);
        }
        return true;
    }
    return pointer_press_face_exit_candidate_;
}

bool AttendTestScreen::handlePointerReleased(int logical_x, int logical_y) {
    mapPointerToLogical(logical_x, logical_y);
    if (freecam_enabled_) {
        freecam_mouse_dragging_ = false;
        return true;
    }
    updatePointerFaceTarget(logical_x, logical_y);
    updatePointerEdgeLook(logical_x, logical_y);
    const bool completed_pet = pointer_pet_active_;
    const double pet_seconds = completed_pet && pointer_pet_started_seconds_ >= 0.0
        ? scene_time_seconds_ - pointer_pet_started_seconds_
        : 0.0;
    const int press_dx = logical_x - pointer_press_x_;
    const int press_dy = logical_y - pointer_press_y_;
    const double press_seconds = pointer_press_seconds_ >= 0.0
        ? scene_time_seconds_ - pointer_press_seconds_
        : 999.0;
    constexpr double kTapMaxSeconds = 0.16;
    constexpr int kTapMovePx = 10;
    const bool tap_candidate =
        press_seconds >= 0.0 &&
        press_seconds <= kTapMaxSeconds &&
        press_dx * press_dx + press_dy * press_dy <= kTapMovePx * kTapMovePx &&
        (pointer_press_face_exit_candidate_ ||
         (pointer_press_head_focus_candidate_ && pointerOverPokemonHead(logical_x, logical_y)));
    pointer_pet_active_ = false;
    pointer_pet_contact_bias_ = 0.0f;
    pointer_pet_started_seconds_ = -1.0;
    pointer_over_pokemon_ = pointerOverPokemon(logical_x, logical_y);
    if (hand_cursor_) {
        hand_cursor_->setAnimation(pointer_over_pokemon_ ? "hover" : "open");
    }
    if (bgfx_renderer_) bgfx_renderer_->setPetting(false);
    if (bgfx_renderer_) bgfx_renderer_->setPetContact(0.0f);
    if (tap_candidate && consumePokemonHeadDoubleClick(logical_x, logical_y)) {
        pointer_press_seconds_ = -1.0;
        pointer_press_head_focus_candidate_ = false;
        pointer_press_face_exit_candidate_ = false;
        focusPokemonHead();
        return true;
    }
    if (!tap_candidate && completed_pet) {
        last_head_click_seconds_ = -1.0;
    }
    if (completed_pet && bgfx_renderer_) {
        wakePokemonFromIdle();
        const bool will_play_cry = bgfx_renderer_->canTriggerReaction("pet_happy", pet_seconds);
        bgfx_renderer_->triggerReaction("pet_happy", pet_seconds);
        if (will_play_cry) {
            requestActivePokemonCry(scene_config_.audio.pet_happy_cry_delay_seconds);
        }
    }
    pointer_press_seconds_ = -1.0;
    pointer_press_head_focus_candidate_ = false;
    pointer_press_face_exit_candidate_ = false;
    return pointer_over_pokemon_;
}

bool AttendTestScreen::consumeReturnRequested() {
    const bool requested = return_requested_;
    return_requested_ = false;
    return requested;
}

std::vector<std::string> AttendTestScreen::consumeOneShotSfxRequests() {
    std::vector<std::string> requests;
    std::vector<OneShotSfxRequest> pending;
    pending.reserve(pending_one_shot_sfx_requests_.size());
    for (const OneShotSfxRequest& request : pending_one_shot_sfx_requests_) {
        if (scene_time_seconds_ >= request.play_after_seconds) {
            requests.push_back(request.path);
        } else {
            pending.push_back(request);
        }
    }
    pending_one_shot_sfx_requests_ = std::move(pending);
    return requests;
}

void AttendTestScreen::mapPointerToLogical(int& x, int& y) const {
    if (!bgfx_renderer_) return;
    const int logical_w = std::max(1, app_config_.window.virtual_width);
    const int logical_h = std::max(1, app_config_.window.virtual_height);
    const int window_w = std::max(1, bgfx_window_w_);
    const int window_h = std::max(1, bgfx_window_h_);
    const float scale = std::max(
        0.01f,
        std::min(
            static_cast<float>(window_w) / static_cast<float>(logical_w),
            static_cast<float>(window_h) / static_cast<float>(logical_h)));
    const float view_w = static_cast<float>(logical_w) * scale;
    const float view_h = static_cast<float>(logical_h) * scale;
    const float view_x = (static_cast<float>(window_w) - view_w) * 0.5f;
    const float view_y = (static_cast<float>(window_h) - view_h) * 0.5f;
    x = std::clamp(
        static_cast<int>(std::round((static_cast<float>(x) - view_x) / scale)),
        0,
        logical_w - 1);
    y = std::clamp(
        static_cast<int>(std::round((static_cast<float>(y) - view_y) / scale)),
        0,
        logical_h - 1);
}

bool AttendTestScreen::pointerOverPokemon(int logical_x, int logical_y) const {
    if (bgfx_renderer_) {
        const SDL_Rect r = bgfx_renderer_->pokemonPointerRect();
        if (r.w > 0 && r.h > 0) {
            return logical_x >= r.x && logical_x < r.x + r.w &&
                   logical_y >= r.y && logical_y < r.y + r.h;
        }
    }
    const float w = static_cast<float>(std::max(1, pointer_space_w_));
    const float h = static_cast<float>(std::max(1, pointer_space_h_));
    const float nx = (static_cast<float>(logical_x) - w * 0.5f) / (w * 0.22f);
    const float ny = (static_cast<float>(logical_y) - h * 0.52f) / (h * 0.29f);
    return (nx * nx + ny * ny) <= 1.0f;
}

bool AttendTestScreen::pointerOverPokemonHead(int logical_x, int logical_y) const {
    if (!faceViewControlEnabled() || !bgfx_renderer_) return false;
    const SDL_Rect r = bgfx_renderer_->pokemonPointerRect();
    if (r.w <= 0 || r.h <= 0) return false;
    const int head_h = std::clamp(static_cast<int>(std::round(static_cast<float>(r.h) * 0.42f)), 24, r.h);
    const int head_w = std::clamp(static_cast<int>(std::round(static_cast<float>(r.w) * 0.62f)), 24, r.w);
    const int head_x = r.x + (r.w - head_w) / 2;
    const int head_y = r.y + std::max(0, static_cast<int>(std::round(static_cast<float>(r.h) * 0.02f)));
    return logical_x >= head_x && logical_x < head_x + head_w &&
           logical_y >= head_y && logical_y < head_y + head_h;
}

bool AttendTestScreen::consumePokemonHeadDoubleClick(int logical_x, int logical_y) {
    const bool valid_click = face_view_ ? faceViewControlEnabled() : pointerOverPokemonHead(logical_x, logical_y);
    if (!valid_click) {
        last_head_click_seconds_ = -1.0;
        return false;
    }
    constexpr double kDoubleClickSeconds = 0.38;
    constexpr int kDoubleClickDistancePx = 38;
    const double elapsed = last_head_click_seconds_ >= 0.0
        ? scene_time_seconds_ - last_head_click_seconds_
        : kDoubleClickSeconds + 1.0;
    const int dx = logical_x - last_head_click_x_;
    const int dy = logical_y - last_head_click_y_;
    const bool double_click =
        elapsed >= 0.0 &&
        elapsed <= kDoubleClickSeconds &&
        dx * dx + dy * dy <= kDoubleClickDistancePx * kDoubleClickDistancePx;
    last_head_click_seconds_ = scene_time_seconds_;
    last_head_click_x_ = logical_x;
    last_head_click_y_ = logical_y;
    if (double_click) {
        last_head_click_seconds_ = -1.0;
    }
    return double_click;
}

void AttendTestScreen::focusPokemonHead() {
    if (!faceViewControlEnabled()) return;
    face_view_ = !face_view_;
    resetPokemonInactivity();
    overlay_.invalidate();
    applyViewMode();
}

bool AttendTestScreen::environmentControlsEnabled() const {
    return scene_config_.floor.enabled &&
           scene_config_.floor.placement_anchor == "world" &&
           scene_config_.viewport_look.enabled;
}

bool AttendTestScreen::faceViewControlEnabled() const {
    return environmentControlsEnabled() &&
           scene_config_.ui.view_button.enabled &&
           bgfx_renderer_ &&
           bgfx_renderer_->faceViewAvailable();
}

bool AttendTestScreen::pointerOverWeatherButton(int logical_x, int logical_y) const {
    if (!debug_ui_visible_ || !environmentControlsEnabled() || !scene_config_.ui.weather_button.enabled) return false;
    return overlay_.hitWeatherButton(logical_x, logical_y);
}

bool AttendTestScreen::pointerOverViewButton(int logical_x, int logical_y) const {
    if (!debug_ui_visible_ || !faceViewControlEnabled()) return false;
    return overlay_.hitViewButton(logical_x, logical_y);
}

bool AttendTestScreen::pointerOverPokemonButton(int logical_x, int logical_y) const {
    if (!debug_ui_visible_ || !environmentControlsEnabled() || !scene_config_.ui.pokemon_button.enabled) return false;
    return overlay_.hitPokemonButton(logical_x, logical_y);
}

bool AttendTestScreen::pointerOverTextureVariantButton(int logical_x, int logical_y) const {
    if (!debug_ui_visible_ || !environmentControlsEnabled() || !scene_config_.ui.texture_variant_button.enabled || !bgfx_renderer_) {
        return false;
    }
    if (bgfx_renderer_->textureVariantCount() <= 1) return false;
    return overlay_.hitTextureVariantButton(logical_x, logical_y);
}

bool AttendTestScreen::pointerOverFormVariantButton(int logical_x, int logical_y) const {
    if (!debug_ui_visible_ || !environmentControlsEnabled() || !scene_config_.ui.form_variant_button.enabled || !bgfx_renderer_) {
        return false;
    }
    if (bgfx_renderer_->formVariantCount() <= 1) return false;
    return overlay_.hitFormVariantButton(logical_x, logical_y);
}

bool AttendTestScreen::pointerOverSkyButton(int logical_x, int logical_y) const {
    if (!debug_ui_visible_ || !environmentControlsEnabled() || !scene_config_.ui.sky_button.enabled) return false;
    if (scene_config_.sky_presets.size() <= 1) return false;
    return overlay_.hitSkyButton(logical_x, logical_y);
}

bool AttendTestScreen::pointerOverEmoteButton(int logical_x, int logical_y) const {
    if (!debug_ui_visible_ || !environmentControlsEnabled() || !scene_config_.ui.emote_button.enabled) return false;
    return overlay_.hitEmoteButton(logical_x, logical_y);
}

bool AttendTestScreen::pointerOverSleepButton(int logical_x, int logical_y) const {
    if (!debug_ui_visible_ || !environmentControlsEnabled() || !scene_config_.ui.sleep_button.enabled) return false;
    return overlay_.hitSleepButton(logical_x, logical_y);
}

bool AttendTestScreen::pointerOverCryButton(int logical_x, int logical_y) const {
    if (!debug_ui_visible_ || !environmentControlsEnabled() || !scene_config_.ui.cry_button.enabled) return false;
    return overlay_.hitCryButton(logical_x, logical_y);
}

bool AttendTestScreen::pointerOverOverlayButton(int logical_x, int logical_y) const {
    return pointerOverWeatherButton(logical_x, logical_y) ||
           pointerOverViewButton(logical_x, logical_y) ||
           pointerOverPokemonButton(logical_x, logical_y) ||
           pointerOverTextureVariantButton(logical_x, logical_y) ||
           pointerOverFormVariantButton(logical_x, logical_y) ||
           pointerOverSkyButton(logical_x, logical_y) ||
           pointerOverEmoteButton(logical_x, logical_y) ||
           pointerOverSleepButton(logical_x, logical_y) ||
           pointerOverCryButton(logical_x, logical_y);
}

std::string AttendTestScreen::currentWeatherLabel() const {
    if (weather_index_ >= 0 && weather_index_ < static_cast<int>(scene_config_.floor.weather_modes.size())) {
        const auto& mode = scene_config_.floor.weather_modes[static_cast<std::size_t>(weather_index_)];
        return mode.label.empty() ? mode.id : mode.label;
    }
    return "Clear";
}

std::string AttendTestScreen::currentViewLabel() const {
    return face_view_ ? "FACE" : "FULL";
}

std::string AttendTestScreen::currentPokemonLabel() const {
    return titleCasePokemonId(scene_config_.pokemon.id);
}

std::string AttendTestScreen::currentFormVariantId() const {
    const int index = form_variant_index_ >= 0
        ? form_variant_index_
        : (bgfx_renderer_ ? bgfx_renderer_->formVariantIndex() : 0);
    return bgfx_renderer_ ? bgfx_renderer_->formVariantId(index) : std::string{};
}

std::string AttendTestScreen::currentPokemonSpritePath() const {
    const std::filesystem::path sprite_style_root =
        std::filesystem::path(project_root_) / "assets" / "pokesprite" / "pokemon-gen8";
    const std::filesystem::path preferred_sprite_root =
        sprite_style_root / (currentTextureVariantIsShiny() ? "shiny" : "regular");
    const std::filesystem::path fallback_sprite_root = sprite_style_root / "regular";
    const std::string species_slug = spriteSlugFromPokemonId(scene_config_.pokemon.id);
    std::string form_key;
    const std::string form_id = currentFormVariantId();
    const auto mapped = scene_config_.pokemon.sprite_form_keys.find(form_id);
    if (mapped != scene_config_.pokemon.sprite_form_keys.end()) {
        form_key = mapped->second;
    }
    if (form_key.empty() && !form_id.empty() && form_id != "00" && form_id != "default") {
        form_key = formKeyToken(form_id);
    }
    auto existing_sprite = [&](const std::filesystem::path& root, const std::string& stem) -> std::string {
        const std::filesystem::path path = root / (stem + ".png");
        return std::filesystem::exists(path) ? path.string() : std::string{};
    };
    if (!form_key.empty()) {
        const std::string formed_stem = species_slug + "-" + form_key;
        if (const std::string preferred = existing_sprite(preferred_sprite_root, formed_stem); !preferred.empty()) {
            return preferred;
        }
        if (const std::string fallback = existing_sprite(fallback_sprite_root, formed_stem); !fallback.empty()) {
            return fallback;
        }
    }
    if (const std::string preferred = existing_sprite(preferred_sprite_root, species_slug); !preferred.empty()) {
        return preferred;
    }
    return (fallback_sprite_root / (species_slug + ".png")).string();
}

std::string AttendTestScreen::currentPokemonCryPath() const {
    const int dex_number = PokemonCryPlayer::speciesIdFromPmModelStem(
        std::filesystem::path(scene_config_.pokemon.model_path).stem().string());
    return PokemonCryPlayer(project_root_).cryPathForSpeciesId(dex_number);
}

bool AttendTestScreen::currentTextureVariantIsShiny() const {
    std::string label = currentTextureVariantLabel();
    std::transform(label.begin(), label.end(), label.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return label == "shiny" || label.find("shiny") != std::string::npos;
}

std::string AttendTestScreen::currentTextureVariantLabel() const {
    const int index = texture_variant_index_ >= 0
        ? texture_variant_index_
        : (bgfx_renderer_ ? bgfx_renderer_->textureVariantIndex() : 0);
    return bgfx_renderer_ ? bgfx_renderer_->textureVariantLabel(index) : "Normal";
}

std::string AttendTestScreen::currentFormVariantLabel() const {
    const int index = form_variant_index_ >= 0
        ? form_variant_index_
        : (bgfx_renderer_ ? bgfx_renderer_->formVariantIndex() : 0);
    return bgfx_renderer_ ? bgfx_renderer_->formVariantLabel(index) : "Form";
}

std::string AttendTestScreen::currentSkyLabel() const {
    if (scene_config_.active_sky >= 0 &&
        scene_config_.active_sky < static_cast<int>(scene_config_.sky_presets.size())) {
        const auto& preset = scene_config_.sky_presets[static_cast<std::size_t>(scene_config_.active_sky)];
        return preset.label.empty() ? preset.id : preset.label;
    }
    return scene_config_.wall.id.empty() ? "Clear" : scene_config_.wall.id;
}

void AttendTestScreen::cycleWeatherMode() {
    weather_index_ = scene_config_.floor.weather_modes.empty()
        ? 0
        : (weather_index_ + 1) % static_cast<int>(scene_config_.floor.weather_modes.size());
    overlay_.invalidate();
    applyWeatherMode();
}

void AttendTestScreen::cycleSkyPreset() {
    if (scene_config_.sky_presets.empty()) return;
    scene_config_.active_sky = (scene_config_.active_sky + 1) % static_cast<int>(scene_config_.sky_presets.size());
    const auto& preset = scene_config_.sky_presets[static_cast<std::size_t>(scene_config_.active_sky)];
    scene_config_.wall = preset.wall;
    scene_config_.lighting = preset.lighting;
    shutdownBgfx();
    bgfx_init_failed_ = false;
    pointer_pet_active_ = false;
    pointer_pet_contact_bias_ = 0.0f;
    pointer_pet_started_seconds_ = -1.0;
    overlay_.invalidate();
}

void AttendTestScreen::triggerIdleEmote() {
    if (!bgfx_renderer_) return;
    if (bgfx_renderer_->isIdleSleeping()) return;
    const auto& idle = scene_config_.idle_behavior;
    const bool will_play_cry = bgfx_renderer_->canTriggerSemanticAnimation(idle.emote_animation_semantic);
    bgfx_renderer_->triggerSemanticAnimation(
        idle.emote_animation_semantic,
        idle.emote_duration_seconds,
        idle.fade_in_seconds,
        idle.fade_out_seconds,
        idle.emote_eye_expression,
        idle.emote_mouth_expression);
    if (will_play_cry) {
        requestActivePokemonCry();
    }
}

void AttendTestScreen::triggerIdleSleep() {
    if (!bgfx_renderer_) return;
    const auto& idle = scene_config_.idle_behavior;
    bgfx_renderer_->triggerSemanticAnimation(
        idle.sleep_animation_semantic,
        idle.sleep_duration_seconds,
        idle.fade_in_seconds,
        idle.fade_out_seconds,
        idle.sleep_eye_expression,
        idle.sleep_mouth_expression);
}

void AttendTestScreen::requestActivePokemonCry(double delay_seconds) {
    const std::string cry_path = currentPokemonCryPath();
    if (!cry_path.empty()) {
        pending_one_shot_sfx_requests_.push_back(OneShotSfxRequest{
            cry_path,
            scene_time_seconds_ + std::max(0.0, delay_seconds)});
    }
}

void AttendTestScreen::updateIdleBehavior() {
    const auto& idle = scene_config_.idle_behavior;
    if (!idle.enabled || pointer_pet_active_ || freecam_enabled_) return;
    const double idle_seconds = scene_time_seconds_ - last_pokemon_interaction_seconds_;
    if (!idle_sleep_triggered_ && idle_seconds >= static_cast<double>(idle.sleep_after_seconds)) {
        triggerIdleSleep();
        idle_sleep_triggered_ = true;
        return;
    }
    if (idle_sleep_triggered_ || (bgfx_renderer_ && bgfx_renderer_->isIdleSleeping())) {
        return;
    }
    if (idle_seconds >= static_cast<double>(idle.emote_after_seconds) &&
        scene_time_seconds_ >= next_idle_emote_seconds_) {
        triggerIdleEmote();
        scheduleNextIdleEmote();
    }
}

bool AttendTestScreen::wakePokemonFromIdle() {
    double wake_seconds = 0.0;
    if (bgfx_renderer_) {
        wake_seconds = bgfx_renderer_->wakeFromIdleAnimation();
    }
    resetPokemonInactivity();
    if (wake_seconds > 0.0) {
        wake_pet_block_until_seconds_ = scene_time_seconds_ + wake_seconds;
        return true;
    }
    return false;
}

void AttendTestScreen::resetPokemonInactivity() {
    last_pokemon_interaction_seconds_ = scene_time_seconds_;
    idle_sleep_triggered_ = false;
    scheduleNextIdleEmote();
}

void AttendTestScreen::scheduleNextIdleEmote() {
    const auto& idle = scene_config_.idle_behavior;
    std::uniform_real_distribution<double> delay(
        static_cast<double>(std::max(0.0f, idle.emote_interval_min_seconds)),
        static_cast<double>(std::max(idle.emote_interval_min_seconds, idle.emote_interval_max_seconds)));
    const double earliest = last_pokemon_interaction_seconds_ + static_cast<double>(idle.emote_after_seconds);
    next_idle_emote_seconds_ = std::max(scene_time_seconds_, earliest) + delay(idle_rng_);
}

void AttendTestScreen::toggleFreeCamera() {
    freecam_enabled_ = !freecam_enabled_;
    freecam_mouse_dragging_ = false;
    pointer_pet_active_ = false;
    pointer_pet_contact_bias_ = 0.0f;
    pointer_pet_started_seconds_ = -1.0;
    pointer_over_pokemon_ = false;
    if (bgfx_renderer_) {
        bgfx_renderer_->setPetting(false);
        bgfx_renderer_->setPetContact(0.0f);
    }
    if (freecam_enabled_) {
        gameplay::attend::rendering::AttendFreeCameraPose pose{};
        if (bgfx_renderer_ && bgfx_renderer_->currentCameraPose(pose)) {
            freecam_pose_ = pose;
        } else {
            const float target_x = scene_config_.camera.target_x;
            const float target_y = scene_config_.camera.target_height;
            const float target_z = scene_config_.camera.target_z;
            const float pitch = scene_config_.camera.freecam_initial_pitch_degrees * (3.1415926535f / 180.0f);
            const float yaw = scene_config_.camera.freecam_initial_yaw_degrees * (3.1415926535f / 180.0f);
            const float distance = scene_config_.camera.distance;
            freecam_pose_.x = target_x - std::sin(yaw) * std::cos(pitch) * distance;
            freecam_pose_.y = target_y - std::sin(pitch) * distance;
            freecam_pose_.z = target_z - std::cos(yaw) * std::cos(pitch) * distance;
            freecam_pose_.yaw_degrees = scene_config_.camera.freecam_initial_yaw_degrees;
            freecam_pose_.pitch_degrees = scene_config_.camera.freecam_initial_pitch_degrees;
        }
        freecam_pose_.x += scene_config_.camera.freecam_initial_offset_x;
        freecam_pose_.y += scene_config_.camera.freecam_initial_offset_y;
        freecam_pose_.z += scene_config_.camera.freecam_initial_offset_z;
        freecam_pose_.pitch_degrees = std::clamp(
            freecam_pose_.pitch_degrees,
            scene_config_.camera.freecam_pitch_min_degrees,
            scene_config_.camera.freecam_pitch_max_degrees);
    }
}

void AttendTestScreen::updateFreeCamera(double dt) {
    if (!freecam_enabled_) return;
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    if (!keys) return;
    const float speed = std::max(0.0f, scene_config_.camera.freecam_move_speed) * static_cast<float>(std::max(0.0, dt));
    const float yaw = freecam_pose_.yaw_degrees * (3.1415926535f / 180.0f);
    const float forward_x = std::sin(yaw);
    const float forward_z = std::cos(yaw);
    const float right_x = forward_z;
    const float right_z = -forward_x;
    if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) {
        freecam_pose_.x += forward_x * speed;
        freecam_pose_.z += forward_z * speed;
    }
    if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]) {
        freecam_pose_.x -= forward_x * speed;
        freecam_pose_.z -= forward_z * speed;
    }
    if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) {
        freecam_pose_.x += right_x * speed;
        freecam_pose_.z += right_z * speed;
    }
    if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]) {
        freecam_pose_.x -= right_x * speed;
        freecam_pose_.z -= right_z * speed;
    }
    if (keys[SDL_SCANCODE_SPACE]) {
        freecam_pose_.y += speed;
    }
    if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]) {
        freecam_pose_.y -= speed;
    }
}

void AttendTestScreen::updateCornerButtonVisibility(double dt) {
    const auto& behavior = scene_config_.ui.corner_buttons;
    bool show = true;
    if (pointer_pet_active_ &&
        pointer_pet_started_seconds_ >= 0.0 &&
        scene_time_seconds_ - pointer_pet_started_seconds_ >= static_cast<double>(behavior.hide_after_pet_seconds)) {
        show = false;
        corner_buttons_waiting_return_ = true;
        corner_buttons_return_after_seconds_ = -1.0;
    } else if (corner_buttons_waiting_return_) {
        if (corner_buttons_return_after_seconds_ < 0.0) {
            corner_buttons_return_after_seconds_ =
                scene_time_seconds_ + static_cast<double>(behavior.return_delay_seconds);
        }
        show = scene_time_seconds_ >= corner_buttons_return_after_seconds_;
        if (show) {
            corner_buttons_waiting_return_ = false;
        }
    }

    if (pointer_near_screen_edge_) {
        show = true;
        corner_buttons_waiting_return_ = false;
        corner_buttons_return_after_seconds_ = -1.0;
    }
    if (bgfx_renderer_ && bgfx_renderer_->faceViewTransitionActive()) {
        show = false;
        corner_buttons_waiting_return_ = false;
        corner_buttons_return_after_seconds_ = -1.0;
    }

    const float target = show ? 1.0f : 0.0f;
    const float seconds = std::max(0.01f, show ? behavior.slide_in_seconds : behavior.slide_out_seconds);
    const float step = static_cast<float>(std::max(0.0, dt)) / seconds;
    if (corner_buttons_visibility_ < target) {
        corner_buttons_visibility_ = std::min(target, corner_buttons_visibility_ + step);
    } else if (corner_buttons_visibility_ > target) {
        corner_buttons_visibility_ = std::max(target, corner_buttons_visibility_ - step);
    }
}

void AttendTestScreen::revealCornerButtons() {
    corner_buttons_waiting_return_ = false;
    corner_buttons_return_after_seconds_ = -1.0;
}

void AttendTestScreen::cycleTextureVariant() {
    if (!bgfx_renderer_ || bgfx_renderer_->textureVariantCount() <= 1) return;
    const int current = texture_variant_index_ >= 0 ? texture_variant_index_ : bgfx_renderer_->textureVariantIndex();
    texture_variant_index_ = (current + 1) % bgfx_renderer_->textureVariantCount();
    overlay_.invalidate();
    applyTextureVariant();
}

void AttendTestScreen::cycleFormVariant() {
    if (!bgfx_renderer_ || bgfx_renderer_->formVariantCount() <= 1) return;
    const int current = form_variant_index_ >= 0 ? form_variant_index_ : bgfx_renderer_->formVariantIndex();
    form_variant_index_ = (current + 1) % bgfx_renderer_->formVariantCount();
    overlay_.invalidate();
    applyFormVariant();
}

void AttendTestScreen::toggleViewMode() {
    if (!faceViewControlEnabled()) {
        face_view_ = false;
        overlay_.invalidate();
        applyViewMode();
        return;
    }
    face_view_ = !face_view_;
    overlay_.invalidate();
    applyViewMode();
}

void AttendTestScreen::cyclePokemonModel() {
    if (available_pokemon_.empty()) {
        refreshAvailablePokemonModels();
    }
    if (available_pokemon_.empty()) return;
    pokemon_index_ = (pokemon_index_ + 1) % static_cast<int>(available_pokemon_.size());
    applyPokemonModelByIndex();
    shutdownBgfx();
    bgfx_init_failed_ = false;
    pointer_pet_active_ = false;
    pointer_pet_contact_bias_ = 0.0f;
    pointer_pet_started_seconds_ = -1.0;
    texture_variant_index_ = -1;
    form_variant_index_ = -1;
    face_view_ = false;
    pointer_press_seconds_ = -1.0;
    pointer_press_head_focus_candidate_ = false;
    pointer_press_face_exit_candidate_ = false;
    last_head_click_seconds_ = -1.0;
    wake_pet_block_until_seconds_ = 0.0;
    resetPokemonInactivity();
    overlay_.invalidate();
}

void AttendTestScreen::applyViewportLook() {
    if (bgfx_renderer_) bgfx_renderer_->setViewportLook(viewport_look_x_, viewport_look_y_);
}

void AttendTestScreen::applyWeatherMode() {
    if (bgfx_renderer_) bgfx_renderer_->setWeatherMode(weather_index_);
}

void AttendTestScreen::applyTextureVariant() {
    if (bgfx_renderer_ && texture_variant_index_ >= 0) bgfx_renderer_->setTextureVariant(texture_variant_index_);
}

void AttendTestScreen::applyFormVariant() {
    if (bgfx_renderer_ && form_variant_index_ >= 0) bgfx_renderer_->setFormVariant(form_variant_index_);
}

void AttendTestScreen::applyViewMode() {
    if (bgfx_renderer_) bgfx_renderer_->setFaceView(face_view_);
}

void AttendTestScreen::refreshAvailablePokemonModels() {
    available_pokemon_.clear();
    const std::filesystem::path dir =
        std::filesystem::path(project_root_) / "assets" / "pokemon_attend" / "pokemon_models";
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) return;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec || !entry.is_regular_file()) continue;
        if (entry.path().extension() != ".glb") continue;
        available_pokemon_.push_back(PokemonModelOption{
            pokemonIdFromModelStem(entry.path().stem().string()),
            entry.path().string()});
    }
    std::sort(
        available_pokemon_.begin(),
        available_pokemon_.end(),
        [](const PokemonModelOption& lhs, const PokemonModelOption& rhs) {
            if (lhs.id != rhs.id) return lhs.id < rhs.id;
            return lhs.path < rhs.path;
        });
    available_pokemon_.erase(
        std::unique(
            available_pokemon_.begin(),
            available_pokemon_.end(),
            [](const PokemonModelOption& lhs, const PokemonModelOption& rhs) {
                return lhs.id == rhs.id;
            }),
        available_pokemon_.end());
    pokemon_index_ = 0;
    for (std::size_t i = 0; i < available_pokemon_.size(); ++i) {
        if (available_pokemon_[i].id == lower(scene_config_.pokemon.id)) {
            pokemon_index_ = static_cast<int>(i);
            break;
        }
    }
}

void AttendTestScreen::applyPokemonModelByIndex() {
    if (pokemon_index_ < 0 || pokemon_index_ >= static_cast<int>(available_pokemon_.size())) return;
    const PokemonModelOption& option = available_pokemon_[static_cast<std::size_t>(pokemon_index_)];
    scene_config_.pokemon.id = option.id;
    scene_config_.pokemon.model_path = option.path;
    scene_config_.pokemon.animation_name.clear();
    scene_config_.interaction_adapter.eye_close_animation.clear();
}

void AttendTestScreen::updatePointerFaceTarget(int logical_x, int logical_y) {
    const float w = static_cast<float>(std::max(1, pointer_space_w_));
    const float h = static_cast<float>(std::max(1, pointer_space_h_));
    pointer_face_x_ = std::clamp((static_cast<float>(logical_x) - w * 0.5f) / (w * 0.5f), -1.0f, 1.0f);
    pointer_face_y_ = std::clamp((static_cast<float>(logical_y) - h * 0.48f) / (h * 0.5f), -1.0f, 1.0f);
}

void AttendTestScreen::updatePointerEdgeLook(int logical_x, int logical_y) {
    if (!environmentControlsEnabled()) {
        viewport_look_x_ = 0.0f;
        viewport_look_y_ = 0.0f;
        pointer_near_screen_edge_ = false;
        applyViewportLook();
        return;
    }
    const float w = static_cast<float>(std::max(1, pointer_space_w_));
    const float h = static_cast<float>(std::max(1, pointer_space_h_));
    const float margin_x = std::max(1.0f, w * scene_config_.viewport_look.edge_margin_ratio);
    const float margin_y = std::max(1.0f, h * scene_config_.viewport_look.edge_margin_ratio);
    const float x = static_cast<float>(logical_x);
    const float y = static_cast<float>(logical_y);

    if (x < margin_x) {
        viewport_look_x_ = smoothEdge((margin_x - x) / margin_x);
    } else if (x > w - margin_x) {
        viewport_look_x_ = -smoothEdge((x - (w - margin_x)) / margin_x);
    } else {
        viewport_look_x_ = 0.0f;
    }

    if (y < margin_y) {
        viewport_look_y_ = smoothEdge((margin_y - y) / margin_y);
    } else if (y > h - margin_y) {
        viewport_look_y_ = -smoothEdge((y - (h - margin_y)) / margin_y);
    } else {
        viewport_look_y_ = 0.0f;
    }
    pointer_near_screen_edge_ = std::abs(viewport_look_x_) > 0.001f || std::abs(viewport_look_y_) > 0.001f;
    if (pointer_near_screen_edge_) {
        revealCornerButtons();
    }
    applyViewportLook();
}

void AttendTestScreen::updatePetContactBias(int logical_x, int logical_y) {
    const float h = static_cast<float>(std::max(1, pointer_space_h_));
    const float contact_y = (static_cast<float>(logical_y) - h * 0.50f) / (h * 0.38f);
    pointer_pet_contact_bias_ = std::clamp((contact_y - 0.06f) * 1.8f, -1.0f, 1.0f);
}

void AttendTestScreen::updatePointerSpace(int w, int h) {
    pointer_space_w_ = std::max(1, w);
    pointer_space_h_ = std::max(1, h);
    overlay_.setLogicalSize(pointer_space_w_, pointer_space_h_);
}

void AttendTestScreen::restoreSystemCursor() {
    pointer_pet_active_ = false;
    pointer_pet_contact_bias_ = 0.0f;
    pointer_pet_started_seconds_ = -1.0;
    if (bgfx_renderer_) bgfx_renderer_->setPetting(false);
    if (bgfx_renderer_) bgfx_renderer_->setPetContact(0.0f);
    if (SDL_Cursor* cursor = SDL_GetDefaultCursor()) {
        SDL_SetCursor(cursor);
    }
    SDL_ShowCursor(SDL_ENABLE);
}

void AttendTestScreen::reloadSceneConfig() {
    try {
        gameplay::attend::AttendSceneConfig reloaded_config =
            gameplay::attend::loadAttendSceneConfig(project_root_);
        shutdownBgfx();
        scene_config_ = std::move(reloaded_config);
        overlay_.setConfig(scene_config_.ui);
        refreshAvailablePokemonModels();
        bgfx_init_failed_ = false;
        pending_bgfx_screenshot_.clear();
        pointer_pet_active_ = false;
        pointer_pet_contact_bias_ = 0.0f;
        pointer_pet_started_seconds_ = -1.0;
        viewport_look_x_ = 0.0f;
        viewport_look_y_ = 0.0f;
        freecam_enabled_ = false;
        freecam_mouse_dragging_ = false;
        weather_index_ = scene_config_.floor.active_weather;
        texture_variant_index_ = -1;
        form_variant_index_ = -1;
        face_view_ = false;
        pointer_press_seconds_ = -1.0;
        pointer_press_head_focus_candidate_ = false;
        pointer_press_face_exit_candidate_ = false;
        last_head_click_seconds_ = -1.0;
        wake_pet_block_until_seconds_ = 0.0;
        resetPokemonInactivity();
        overlay_.invalidate();
        std::cerr << "[AttendTest] Reloaded TEST ATTEND config: " << scene_config_.id << '\n';
    } catch (const std::exception& ex) {
        std::cerr << "[AttendTest] Could not reload TEST ATTEND config: " << ex.what() << '\n';
    }
}

} // namespace pr
