#include "ui/loading/ResortTransferLoadingScreen.hpp"

#include "core/assets/Font.hpp"

#include <SDL_image.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <sstream>
#include <utility>
#include <vector>

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
        throw std::runtime_error("Failed to load resort transfer loading texture: " + path.string() + " | " + IMG_GetError());
    }

    TextureHandle texture;
    texture.texture.reset(raw, SDL_DestroyTexture);
    if (SDL_QueryTexture(texture.texture.get(), nullptr, nullptr, &texture.width, &texture.height) != 0) {
        throw std::runtime_error("Failed to query resort transfer loading texture: " + path.string() + " | " + SDL_GetError());
    }
    SDL_SetTextureBlendMode(texture.texture.get(), SDL_BLENDMODE_BLEND);
    return texture;
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::stringstream stream(text);
    std::string line;
    while (std::getline(stream, line, '\n')) {
        lines.push_back(line);
    }
    if (lines.empty()) {
        lines.push_back("");
    }
    return lines;
}

std::string messageTextForKey(const ResortTransferLoadingConfig& config, const std::string& key) {
    if (auto it = config.message.texts.find(key); it != config.message.texts.end()) {
        return it->second;
    }
    if (auto it = config.message.texts.find(config.message.default_key); it != config.message.texts.end()) {
        return it->second;
    }
    return config.message.texts.empty() ? std::string{} : config.message.texts.begin()->second;
}

TextureHandle renderMultilineTextTexture(
    SDL_Renderer* renderer,
    TTF_Font* font,
    const std::string& text,
    const Color& color,
    int line_spacing) {
    TextureHandle handle;
    if (!renderer || !font || text.empty()) {
        return handle;
    }

    struct SurfaceDeleter {
        void operator()(SDL_Surface* value) const {
            if (value) SDL_FreeSurface(value);
        }
    };

    const SDL_Color sdl_color{
        static_cast<Uint8>(color.r),
        static_cast<Uint8>(color.g),
        static_cast<Uint8>(color.b),
        static_cast<Uint8>(color.a)
    };
    const int font_line_height = std::max(1, TTF_FontLineSkip(font));
    const std::vector<std::string> lines = splitLines(text);
    std::vector<std::unique_ptr<SDL_Surface, SurfaceDeleter>> line_surfaces;
    line_surfaces.reserve(lines.size());

    int width = 1;
    int height = 0;
    for (const std::string& line : lines) {
        int line_width = 1;
        int line_height = font_line_height;
        std::unique_ptr<SDL_Surface, SurfaceDeleter> surface;
        if (!line.empty()) {
            SDL_Surface* raw = TTF_RenderUTF8_Blended(font, line.c_str(), sdl_color);
            if (!raw) {
                throw std::runtime_error(std::string("Failed to render resort loading message text: ") + TTF_GetError());
            }
            surface.reset(raw);
            SDL_SetSurfaceBlendMode(surface.get(), SDL_BLENDMODE_BLEND);
            line_width = surface->w;
            line_height = surface->h;
        }
        width = std::max(width, line_width);
        height += line_height;
        line_surfaces.push_back(std::move(surface));
    }
    height += std::max(0, static_cast<int>(lines.size()) - 1) * line_spacing;
    height = std::max(1, height);

    std::unique_ptr<SDL_Surface, SurfaceDeleter> combined(
        SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32));
    if (!combined) {
        throw std::runtime_error(std::string("Failed to create resort loading message surface: ") + SDL_GetError());
    }
    SDL_FillRect(combined.get(), nullptr, SDL_MapRGBA(combined->format, 0, 0, 0, 0));

    int y = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const int line_height = line_surfaces[i] ? line_surfaces[i]->h : font_line_height;
        if (line_surfaces[i]) {
            SDL_Rect dst{(width - line_surfaces[i]->w) / 2, y, line_surfaces[i]->w, line_surfaces[i]->h};
            SDL_BlitSurface(line_surfaces[i].get(), nullptr, combined.get(), &dst);
        }
        y += line_height + line_spacing;
    }

    SDL_Texture* raw_texture = SDL_CreateTextureFromSurface(renderer, combined.get());
    if (!raw_texture) {
        throw std::runtime_error(std::string("Failed to create resort loading message texture: ") + SDL_GetError());
    }
    handle.texture.reset(raw_texture, SDL_DestroyTexture);
    handle.width = width;
    handle.height = height;
    SDL_SetTextureBlendMode(handle.texture.get(), SDL_BLENDMODE_BLEND);
    return handle;
}

int alignLineX(int inner_width, int line_width, LoadingMessageHorizontalAlign align) {
    switch (align) {
        case LoadingMessageHorizontalAlign::Left:
            return 0;
        case LoadingMessageHorizontalAlign::Center:
            return (inner_width - line_width) / 2;
        case LoadingMessageHorizontalAlign::Right:
            return inner_width - line_width;
    }
    return 0;
}

TextureHandle renderQuickPassMessageTexture(
    SDL_Renderer* renderer,
    TTF_Font* font,
    const std::string& text,
    const Color& color,
    int line_spacing,
    const std::optional<Color>& shadow_color,
    double shadow_offset_x,
    double shadow_offset_y,
    LoadingMessageHorizontalAlign align) {
    TextureHandle handle;
    if (!renderer || !font || text.empty()) {
        return handle;
    }

    struct SurfaceDeleter {
        void operator()(SDL_Surface* value) const {
            if (value) SDL_FreeSurface(value);
        }
    };

    const SDL_Color sdl_color{
        static_cast<Uint8>(color.r),
        static_cast<Uint8>(color.g),
        static_cast<Uint8>(color.b),
        static_cast<Uint8>(color.a)
    };
    const int font_line_height = std::max(1, TTF_FontLineSkip(font));
    const std::vector<std::string> lines = splitLines(text);
    std::vector<std::unique_ptr<SDL_Surface, SurfaceDeleter>> line_surfaces_main;
    std::vector<std::unique_ptr<SDL_Surface, SurfaceDeleter>> line_surfaces_shadow;
    line_surfaces_main.reserve(lines.size());
    line_surfaces_shadow.reserve(lines.size());

    int inner_width = 1;
    int height = 0;
    for (const std::string& line : lines) {
        int line_width = 1;
        int line_height = font_line_height;
        std::unique_ptr<SDL_Surface, SurfaceDeleter> surface_main;
        std::unique_ptr<SDL_Surface, SurfaceDeleter> surface_shadow;
        if (!line.empty()) {
            SDL_Surface* raw = TTF_RenderUTF8_Blended(font, line.c_str(), sdl_color);
            if (!raw) {
                throw std::runtime_error(std::string("Failed to render resort loading message text: ") + TTF_GetError());
            }
            surface_main.reset(raw);
            SDL_SetSurfaceBlendMode(surface_main.get(), SDL_BLENDMODE_BLEND);
            line_width = surface_main->w;
            line_height = surface_main->h;
            if (shadow_color.has_value()) {
                const SDL_Color sdl_shadow{
                    static_cast<Uint8>(shadow_color->r),
                    static_cast<Uint8>(shadow_color->g),
                    static_cast<Uint8>(shadow_color->b),
                    static_cast<Uint8>(shadow_color->a)
                };
                SDL_Surface* raw_sh = TTF_RenderUTF8_Blended(font, line.c_str(), sdl_shadow);
                if (!raw_sh) {
                    throw std::runtime_error(std::string("Failed to render resort loading shadow text: ") + TTF_GetError());
                }
                surface_shadow.reset(raw_sh);
                SDL_SetSurfaceBlendMode(surface_shadow.get(), SDL_BLENDMODE_BLEND);
            }
        }
        inner_width = std::max(inner_width, line_width);
        height += line_height;
        line_surfaces_main.push_back(std::move(surface_main));
        line_surfaces_shadow.push_back(std::move(surface_shadow));
    }
    height += std::max(0, static_cast<int>(lines.size()) - 1) * line_spacing;
    height = std::max(1, height);

    const bool has_shadow = shadow_color.has_value();
    const int pad_l = has_shadow ? static_cast<int>(std::ceil(std::max(0.0, -shadow_offset_x))) : 0;
    const int pad_t = has_shadow ? static_cast<int>(std::ceil(std::max(0.0, -shadow_offset_y))) : 0;
    const int pad_r = has_shadow ? static_cast<int>(std::ceil(std::max(0.0, shadow_offset_x))) : 0;
    const int pad_b = has_shadow ? static_cast<int>(std::ceil(std::max(0.0, shadow_offset_y))) : 0;

    const int full_width = inner_width + pad_l + pad_r;
    const int full_height = height + pad_t + pad_b;

    std::unique_ptr<SDL_Surface, SurfaceDeleter> combined(
        SDL_CreateRGBSurfaceWithFormat(0, full_width, full_height, 32, SDL_PIXELFORMAT_RGBA32));
    if (!combined) {
        throw std::runtime_error(std::string("Failed to create resort loading quick message surface: ") + SDL_GetError());
    }
    SDL_FillRect(combined.get(), nullptr, SDL_MapRGBA(combined->format, 0, 0, 0, 0));

    int y = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const int line_height = line_surfaces_main[i] ? line_surfaces_main[i]->h : font_line_height;
        const int line_w = line_surfaces_main[i] ? line_surfaces_main[i]->w : 1;
        const int x_inner = alignLineX(inner_width, line_w, align);
        if (has_shadow && line_surfaces_shadow[i]) {
            SDL_Rect dst_shadow{
                pad_l + x_inner + static_cast<int>(std::lround(shadow_offset_x)),
                pad_t + y + static_cast<int>(std::lround(shadow_offset_y)),
                line_surfaces_shadow[i]->w,
                line_surfaces_shadow[i]->h};
            SDL_BlitSurface(line_surfaces_shadow[i].get(), nullptr, combined.get(), &dst_shadow);
        }
        if (line_surfaces_main[i]) {
            SDL_Rect dst_main{pad_l + x_inner, pad_t + y, line_surfaces_main[i]->w, line_surfaces_main[i]->h};
            SDL_BlitSurface(line_surfaces_main[i].get(), nullptr, combined.get(), &dst_main);
        }
        y += line_height + line_spacing;
    }

    SDL_Texture* raw_texture = SDL_CreateTextureFromSurface(renderer, combined.get());
    if (!raw_texture) {
        throw std::runtime_error(std::string("Failed to create resort loading quick message texture: ") + SDL_GetError());
    }
    handle.texture.reset(raw_texture, SDL_DestroyTexture);
    handle.width = full_width;
    handle.height = full_height;
    SDL_SetTextureBlendMode(handle.texture.get(), SDL_BLENDMODE_BLEND);
    return handle;
}

void rendererSize(SDL_Renderer* renderer, const WindowConfig& window_config, int& w, int& h) {
    w = 0;
    h = 0;
    SDL_RenderGetLogicalSize(renderer, &w, &h);
    if (w <= 0 || h <= 0) {
        SDL_GetRendererOutputSize(renderer, &w, &h);
    }
    if (w <= 0 || h <= 0) {
        w = window_config.virtual_width;
        h = window_config.virtual_height;
    }
}

} // namespace

ResortTransferLoadingScreen::ResortTransferLoadingScreen(
    SDL_Renderer* renderer,
    WindowConfig window_config,
    std::string project_root,
    LoadingScreenType loading_screen_type)
    : window_config_(std::move(window_config)),
      sdl_renderer_(renderer),
      project_root_(std::move(project_root)),
      loading_screen_type_(loading_screen_type),
      config_(loadResortTransferLoadingConfig(project_root_)),
      message_key_(config_.message.default_key) {
    if (!renderer) {
        return;
    }

    textures_.boat = loadTexture(renderer, resolvePath(project_root_, config_.assets.boat));
    textures_.cloud1 = loadTexture(renderer, resolvePath(project_root_, config_.assets.cloud1));
    textures_.cloud2 = loadTexture(renderer, resolvePath(project_root_, config_.assets.cloud2));
    refreshMessageTexture();
    rebuildRenderer();
}

void ResortTransferLoadingScreen::setLoadingMessageKey(const std::string& message_key) {
    message_key_ = message_key.empty() ? config_.message.default_key : message_key;
    refreshMessageTexture();
    rebuildRenderer();
}

void ResortTransferLoadingScreen::setMinimumLoopSeconds(double minimum_loop_seconds) {
    minimum_loop_seconds_ = minimum_loop_seconds < 0.0
        ? config_.minimum_loop_seconds
        : std::max(0.0, minimum_loop_seconds);
}

void ResortTransferLoadingScreen::enter() {
    return_to_menu_requested_ = false;
    state_time_ = 0.0;
    loop_elapsed_seconds_ = 0.0;
    loop_time_ = 0.0;
    cloud_drift_ = 0.0;
    foam_phase_ = 0.0;
    speed_crest_phase_ = 0.0;
    previous_boat_x_ = 0.0;
    boat_velocity_x_ = 0.0;
    quick_pass_completion_time_ = 0.0;
    has_previous_boat_x_ = false;
    auto_flow_ = false;
    loading_complete_requested_ = false;
    loading_animation_complete_ = false;
    suppress_message_ = false;
    state_ = ResortTransferLoadingState::WhiteIdle;
}

void ResortTransferLoadingScreen::beginLoadingWithMessageKey(
    const std::string& message_key,
    double minimum_loop_seconds) {
    setLoadingMessageKey(message_key);
    setMinimumLoopSeconds(minimum_loop_seconds);
    enter();
    auto_flow_ = true;
    changeState(ResortTransferLoadingState::Intro);
}

void ResortTransferLoadingScreen::beginQuickPass(bool wait_for_completion) {
    enter();
    auto_flow_ = true;
    suppress_message_ = true;
    loading_complete_requested_ = !wait_for_completion;
    quick_pass_completion_time_ = 0.0;
    changeState(ResortTransferLoadingState::QuickPass);
}

void ResortTransferLoadingScreen::update(double dt) {
    dt = std::max(0.0, dt);
    state_time_ += dt;
    if (state_ != ResortTransferLoadingState::WhiteIdle) {
        loop_time_ += dt;
        cloud_drift_ += dt * config_.clouds.drift_speed;
        const double enter_progress = state_ == ResortTransferLoadingState::Intro
            ? loadingStageProgress(config_.timing.intro_boat, state_time_)
            : 1.0;
        const double exit_progress = state_ == ResortTransferLoadingState::Outro
            ? loadingStageProgress(config_.timing.outro_boat, state_time_)
            : 0.0;
        const double quick_progress = state_ == ResortTransferLoadingState::QuickPass
            ? applyLoadingEase(config_.quick_pass.ease, state_time_ / std::max(0.01, config_.quick_pass.duration_seconds))
            : 0.0;
        const double boat_x = state_ == ResortTransferLoadingState::QuickPass
            ? quickPassBoatCenterX(quick_progress, window_config_.virtual_width)
            : boatCenterXForProgress(enter_progress, exit_progress, window_config_.virtual_width);
        boat_velocity_x_ = has_previous_boat_x_ && dt > 0.0 ? (boat_x - previous_boat_x_) / dt : 0.0;
        previous_boat_x_ = boat_x;
        has_previous_boat_x_ = true;
        // Keep boat-velocity boost in the same direction as base scroll so negative
        // `scroll_speed` reverses the whole foam motion instead of fighting it.
        const double scroll = config_.foam.scroll_speed;
        if (scroll == 0.0) {
            foam_phase_ += dt * std::abs(boat_velocity_x_) * config_.foam.velocity_influence;
        } else {
            const double dir = scroll > 0.0 ? 1.0 : -1.0;
            foam_phase_ += dt * dir *
                (std::abs(scroll) + std::abs(boat_velocity_x_) * config_.foam.velocity_influence);
        }

        if (config_.foam.speed_crest_scroll_speed != 0.0) {
            const double cs = config_.foam.speed_crest_scroll_speed;
            const double crest_dir = cs > 0.0 ? 1.0 : -1.0;
            speed_crest_phase_ += dt * crest_dir *
                (std::abs(cs) + std::abs(boat_velocity_x_) * config_.foam.velocity_influence);
        }
    }

    if (state_ == ResortTransferLoadingState::QuickPass &&
        quickPassExitProgress(config_.quick_pass.water_out_start, config_.quick_pass.water_out_fraction) >= 1.0 &&
        quickPassExitProgress(config_.quick_pass.cloud_exit_start, config_.quick_pass.cloud_exit_fraction) >= 1.0) {
        changeState(ResortTransferLoadingState::WhiteIdle);
        loading_animation_complete_ = true;
    } else if (state_ == ResortTransferLoadingState::Intro && state_time_ >= introDuration()) {
        if (auto_flow_ && loading_complete_requested_ && minimum_loop_seconds_ <= 0.0) {
            changeState(ResortTransferLoadingState::Outro);
        } else {
            changeState(ResortTransferLoadingState::LoadingLoop);
        }
    } else if (state_ == ResortTransferLoadingState::LoadingLoop) {
        loop_elapsed_seconds_ += dt;
        if (auto_flow_ && loading_complete_requested_ && loop_elapsed_seconds_ >= minimum_loop_seconds_) {
            changeState(ResortTransferLoadingState::Outro);
        }
    } else if (state_ == ResortTransferLoadingState::Outro && state_time_ >= outroDuration()) {
        changeState(ResortTransferLoadingState::WhiteIdle);
        if (auto_flow_) {
            loading_animation_complete_ = true;
        }
    }
}

void ResortTransferLoadingScreen::render(SDL_Renderer* renderer) {
    if (!renderer) {
        return;
    }

    if (renderer_) {
        renderer_->render(renderer, buildFrame(renderer));
        return;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, static_cast<Uint8>(config_.colors.sky.r), static_cast<Uint8>(config_.colors.sky.g),
                           static_cast<Uint8>(config_.colors.sky.b), static_cast<Uint8>(config_.colors.sky.a));
    SDL_RenderClear(renderer);
}

void ResortTransferLoadingScreen::onAdvancePressed() {
    if (auto_flow_) {
        return;
    }
    if (state_ == ResortTransferLoadingState::WhiteIdle) {
        changeState(ResortTransferLoadingState::Intro);
    } else if (state_ == ResortTransferLoadingState::LoadingLoop) {
        changeState(ResortTransferLoadingState::Outro);
    }
}

void ResortTransferLoadingScreen::onBackPressed() {
    return_to_menu_requested_ = true;
}

bool ResortTransferLoadingScreen::handlePointerPressed(int logical_x, int logical_y) {
    (void)logical_x;
    (void)logical_y;
    onAdvancePressed();
    return true;
}

bool ResortTransferLoadingScreen::acceptsAdvanceInput() const {
    return !auto_flow_ &&
        (state_ == ResortTransferLoadingState::WhiteIdle || state_ == ResortTransferLoadingState::LoadingLoop);
}

void ResortTransferLoadingScreen::markLoadingComplete() {
    if (state_ == ResortTransferLoadingState::QuickPass && !loading_complete_requested_) {
        quick_pass_completion_time_ = state_time_;
    }
    loading_complete_requested_ = true;
    if (auto_flow_ && minimum_loop_seconds_ <= 0.0) {
        suppress_message_ = true;
    }
    if (auto_flow_ &&
        state_ == ResortTransferLoadingState::LoadingLoop &&
        loop_elapsed_seconds_ >= minimum_loop_seconds_) {
        changeState(ResortTransferLoadingState::Outro);
    }
}

bool ResortTransferLoadingScreen::isLoadingAnimationComplete() const {
    return loading_animation_complete_;
}

bool ResortTransferLoadingScreen::consumeReturnToMenuRequest() {
    const bool requested = return_to_menu_requested_;
    return_to_menu_requested_ = false;
    return requested;
}

#ifdef PR_ENABLE_TEST_HOOKS
ResortTransferLoadingState ResortTransferLoadingScreen::debugState() const {
    return state_;
}
#endif

void ResortTransferLoadingScreen::changeState(ResortTransferLoadingState next) {
    state_ = next;
    state_time_ = 0.0;
    if (state_ == ResortTransferLoadingState::LoadingLoop) {
        loop_elapsed_seconds_ = 0.0;
    }
    if (state_ == ResortTransferLoadingState::WhiteIdle) {
        loop_elapsed_seconds_ = 0.0;
        loop_time_ = 0.0;
        cloud_drift_ = 0.0;
        foam_phase_ = 0.0;
        speed_crest_phase_ = 0.0;
        previous_boat_x_ = 0.0;
        boat_velocity_x_ = 0.0;
        has_previous_boat_x_ = false;
        suppress_message_ = false;
    }
}

void ResortTransferLoadingScreen::rebuildRenderer() {
    if (sdl_renderer_) {
        renderer_ = std::make_unique<ResortTransferLoadingRenderer>(config_, textures_);
    }
}

void ResortTransferLoadingScreen::refreshMessageTexture() {
    if (!sdl_renderer_) {
        return;
    }
    const std::string text = messageTextForKey(config_, message_key_);
    if (text.empty()) {
        textures_.message = TextureHandle{};
        textures_.message_quick = TextureHandle{};
        return;
    }
    FontHandle font = loadFont(config_.message.font, config_.message.font_size, project_root_);
    textures_.message = renderMultilineTextTexture(
        sdl_renderer_,
        font.get(),
        text,
        config_.message.color,
        config_.message.line_spacing);
    if (config_.quick_pass.message.show_text) {
        const auto& qm = config_.quick_pass.message;
        const std::string quick_font_path = qm.font.empty() ? config_.message.font : qm.font;
        FontHandle quick_font = loadFont(quick_font_path, config_.message.font_size, project_root_);
        textures_.message_quick = renderQuickPassMessageTexture(
            sdl_renderer_,
            quick_font.get(),
            text,
            qm.color,
            qm.line_spacing,
            qm.shadow_color,
            qm.shadow_offset_x,
            qm.shadow_offset_y,
            qm.align);
    } else {
        textures_.message_quick = TextureHandle{};
    }
}

ResortTransferLoadingFrame ResortTransferLoadingScreen::buildFrame(SDL_Renderer* renderer) const {
    ResortTransferLoadingFrame frame;
    rendererSize(renderer, window_config_, frame.viewport_w, frame.viewport_h);
    frame.idle = state_ == ResortTransferLoadingState::WhiteIdle;
    frame.loop_time = loop_time_;
    frame.cloud_drift = cloud_drift_;
    frame.foam_phase = foam_phase_;
    frame.speed_crest_phase = config_.foam.speed_crest_scroll_speed == 0.0 ? foam_phase_ * 0.55 : speed_crest_phase_;

    if (state_ == ResortTransferLoadingState::QuickPass) {
        const double raw = state_time_ / std::max(0.01, config_.quick_pass.duration_seconds);
        const double progress = applyLoadingEase(config_.quick_pass.ease, raw);
        const double water_in = applyLoadingEase(LoadingEase::EaseOutCubic,
            std::clamp(raw / config_.quick_pass.water_in_fraction, 0.0, 1.0));
        const double water_out = 1.0 -
            quickPassExitProgress(config_.quick_pass.water_out_start, config_.quick_pass.water_out_fraction);
        frame.water_sun = std::min(water_in, water_out);
        frame.boat_enter = 1.0;
        frame.boat_exit = progress;
        frame.clouds_enter = water_in;
        frame.clouds_exit =
            quickPassExitProgress(config_.quick_pass.cloud_exit_start, config_.quick_pass.cloud_exit_fraction);
        frame.use_boat_center_x = true;
        frame.boat_center_x = quickPassBoatCenterX(progress, frame.viewport_w);
        frame.boat_velocity_x = boat_velocity_x_;
        if (config_.quick_pass.message.show_text &&
            textures_.message_quick.texture &&
            textures_.message_quick.width > 0 &&
            textures_.message_quick.height > 0) {
            frame.message_alpha = 1.0;
            frame.use_quick_pass_message_layout = true;
        } else {
            frame.message_alpha = 0.0;
            frame.use_quick_pass_message_layout = false;
        }
    } else if (state_ == ResortTransferLoadingState::Intro) {
        const double message_progress = loadingStageProgress(config_.message.intro, state_time_);
        frame.water_sun = loadingStageProgress(config_.timing.intro_water_sun, state_time_);
        frame.boat_enter = loadingStageProgress(config_.timing.intro_boat, state_time_);
        frame.clouds_enter = loadingStageProgress(config_.timing.intro_clouds, state_time_);
        frame.boat_velocity_x = boat_velocity_x_;
        frame.message_alpha = (suppress_message_ || (auto_flow_ && minimum_loop_seconds_ <= 0.0))
            ? 0.0
            : message_progress;
        frame.message_y_offset = (1.0 - message_progress) * config_.message.enter_y_offset;
        frame.use_quick_pass_message_layout = false;
    } else if (state_ == ResortTransferLoadingState::LoadingLoop) {
        const double message_progress = auto_flow_ && minimum_loop_seconds_ <= 0.0
            ? loadingStageProgress(config_.message.intro, state_time_)
            : 1.0;
        frame.water_sun = 1.0;
        frame.boat_enter = 1.0;
        frame.clouds_enter = 1.0;
        frame.boat_velocity_x = boat_velocity_x_;
        frame.message_alpha = suppress_message_ ? 0.0 : message_progress;
        frame.message_y_offset = (1.0 - message_progress) * config_.message.enter_y_offset;
        frame.use_quick_pass_message_layout = false;
    } else if (state_ == ResortTransferLoadingState::Outro) {
        const double message_out = loadingStageProgress(config_.message.outro, state_time_);
        frame.water_sun = 1.0 - loadingStageProgress(config_.timing.outro_water_sun, state_time_);
        frame.boat_enter = 1.0;
        frame.boat_exit = loadingStageProgress(config_.timing.outro_boat, state_time_);
        frame.clouds_enter = 1.0;
        // Match cloud exit progress (and easing) to the boat so horizontal speeds stay aligned during outro.
        frame.clouds_exit = frame.boat_exit;
        frame.boat_velocity_x = boat_velocity_x_;
        frame.message_alpha = suppress_message_ ? 0.0 : 1.0 - message_out;
        frame.message_y_offset = -message_out * config_.message.enter_y_offset * 0.5;
        frame.use_quick_pass_message_layout = false;
    }
    return frame;
}

} // namespace pr
