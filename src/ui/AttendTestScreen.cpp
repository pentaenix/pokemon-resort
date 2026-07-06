#include "ui/AttendTestScreen.hpp"

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
         !scene_config_.ui.form_variant_button.enabled)) {
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
        currentFormVariantLabel());
}

bool AttendTestScreen::renderBgfx(
    SDL_Window* window,
    int framebuffer_w,
    int framebuffer_h,
    void* sdl_metal_view) {
    if (!wantsBgfxRenderer()) {
        return false;
    }
    int window_w = 0;
    int window_h = 0;
    SDL_GetWindowSize(window, &window_w, &window_h);
    updatePointerSpace(window_w, window_h);
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
    if (event.type == SDL_KEYDOWN && event.key.repeat == 0 && event.key.keysym.sym == SDLK_d) {
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
            default:
                break;
        }
    }
    return false;
}

void AttendTestScreen::onBackPressed() {
    restoreSystemCursor();
    return_requested_ = true;
}

void AttendTestScreen::handlePointerMoved(int logical_x, int logical_y) {
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
    pointer_over_pokemon_ = pointerOverPokemon(logical_x, logical_y);
    if (pointer_over_pokemon_) {
        pointer_pet_active_ = true;
        pointer_pet_started_seconds_ = scene_time_seconds_;
        updatePetContactBias(logical_x, logical_y);
        if (hand_cursor_) hand_cursor_->setAnimation("pet");
        if (bgfx_renderer_) bgfx_renderer_->setPetting(true);
        return true;
    }
    return false;
}

bool AttendTestScreen::handlePointerReleased(int logical_x, int logical_y) {
    updatePointerFaceTarget(logical_x, logical_y);
    updatePointerEdgeLook(logical_x, logical_y);
    const bool completed_pet = pointer_pet_active_;
    const double pet_seconds = completed_pet && pointer_pet_started_seconds_ >= 0.0
        ? scene_time_seconds_ - pointer_pet_started_seconds_
        : 0.0;
    pointer_pet_active_ = false;
    pointer_pet_contact_bias_ = 0.0f;
    pointer_pet_started_seconds_ = -1.0;
    pointer_over_pokemon_ = pointerOverPokemon(logical_x, logical_y);
    if (hand_cursor_) {
        hand_cursor_->setAnimation(pointer_over_pokemon_ ? "hover" : "open");
    }
    if (bgfx_renderer_) bgfx_renderer_->setPetting(false);
    if (bgfx_renderer_) bgfx_renderer_->setPetContact(0.0f);
    if (completed_pet && bgfx_renderer_) {
        bgfx_renderer_->triggerReaction("pet_happy", pet_seconds);
    }
    return pointer_over_pokemon_;
}

bool AttendTestScreen::consumeReturnRequested() {
    const bool requested = return_requested_;
    return_requested_ = false;
    return requested;
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

bool AttendTestScreen::pointerOverOverlayButton(int logical_x, int logical_y) const {
    return pointerOverWeatherButton(logical_x, logical_y) ||
           pointerOverViewButton(logical_x, logical_y) ||
           pointerOverPokemonButton(logical_x, logical_y) ||
           pointerOverTextureVariantButton(logical_x, logical_y) ||
           pointerOverFormVariantButton(logical_x, logical_y);
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

void AttendTestScreen::cycleWeatherMode() {
    weather_index_ = scene_config_.floor.weather_modes.empty()
        ? 0
        : (weather_index_ + 1) % static_cast<int>(scene_config_.floor.weather_modes.size());
    overlay_.invalidate();
    applyWeatherMode();
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
    shutdownBgfx();
    try {
        scene_config_ = gameplay::attend::loadAttendSceneConfig(project_root_);
        overlay_.setConfig(scene_config_.ui);
        refreshAvailablePokemonModels();
        bgfx_init_failed_ = false;
        pending_bgfx_screenshot_.clear();
        pointer_pet_active_ = false;
        pointer_pet_contact_bias_ = 0.0f;
        pointer_pet_started_seconds_ = -1.0;
        viewport_look_x_ = 0.0f;
        viewport_look_y_ = 0.0f;
        weather_index_ = scene_config_.floor.active_weather;
        texture_variant_index_ = -1;
        form_variant_index_ = -1;
        face_view_ = false;
        overlay_.invalidate();
        std::cerr << "[AttendTest] Reloaded TEST ATTEND config: " << scene_config_.id << '\n';
    } catch (const std::exception& ex) {
        std::cerr << "[AttendTest] Could not reload TEST ATTEND config: " << ex.what() << '\n';
    }
}

} // namespace pr
