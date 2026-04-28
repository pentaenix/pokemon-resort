#pragma once

#include "core/Assets.hpp"
#include "core/Types.hpp"

#include <SDL.h>
#include <array>
#include <deque>
#include <optional>
#include <string>

namespace pr {

/// Left column is always Resort storage; right column is the opened external save (mirrored footer, no scroll chevron).
enum class BoxViewportRole {
    ResortStorage,
    ExternalGameSave,
};

/// Standard 6×5 Pokémon box chrome for the transfer flow (non-scrollable game box; Resort hyper box is separate).
/// Layout constants match the transfer screen spec; data hooks are grouped in `BoxViewportModel`.
struct BoxViewportModel {
    std::string box_name = "BOX 1";
    /// Per-slot Pokémon sprite; empty optional = empty slot chrome only.
    std::array<std::optional<TextureHandle>, 30> slot_sprites{};
    /// Per-slot held-item sprite used by the item tool overlay.
    std::array<std::optional<TextureHandle>, 30> held_item_sprites{};
    /// Pixel nudge for Box Space long-press feedback (horizontal shake); visual only.
    std::array<int, 30> slot_wiggle_dx{};
};

class BoxViewport {
public:
    static constexpr int kViewportWidth = 560;
    static constexpr int kViewportHeight = 577;

    BoxViewport(
        SDL_Renderer* renderer,
        const std::string& project_root,
        const std::string& font_path,
        const GameTransferBoxViewportStyle& style,
        BoxViewportRole role,
        int viewport_x,
        int viewport_y);

    void setModel(BoxViewportModel model);
    /// Commits immediately with no slide animation (clears any in-flight slide / queue).
    void snapContentToModel(BoxViewportModel model);
    const BoxViewportModel& model() const { return model_; }

    /// Per-box navigation arrows (left/right beside the name plate).
    bool hitTestPrevBoxArrow(int logical_x, int logical_y) const;
    bool hitTestNextBoxArrow(int logical_x, int logical_y) const;
    bool getPrevArrowBounds(SDL_Rect& out) const;
    bool getNextArrowBounds(SDL_Rect& out) const;
    bool getSlotBounds(int slot_index, SDL_Rect& out) const;
    bool getNamePlateBounds(SDL_Rect& out) const;
    bool getFooterBoxSpaceBounds(SDL_Rect& out) const;
    bool getFooterGameIconBounds(SDL_Rect& out) const;
    /// Resort-only down chevron below the grid.
    bool getResortScrollArrowBounds(SDL_Rect& out) const;
    /// Box-space view (external save only): down chevron below the grid.
    bool hitTestBoxSpaceScrollArrow(int logical_x, int logical_y) const;
    bool getBoxSpaceScrollArrowBounds(SDL_Rect& out) const;

    /// Starts a content-only slide (sprites in/out) while keeping the frame + name plate fixed.
    /// `dir`: -1 = previous (incoming from left), +1 = next (incoming from right).
    void queueContentSlide(BoxViewportModel incoming, int dir);
    bool isContentSliding() const { return content_slide_active_; }
    void update(double dt);

    /// Loads `assets/game_icons/pokemon_resort_icon.png` (left / Resort column; color from `palette.game_colors`).
    void reloadResortIcon(SDL_Renderer* renderer);

    /// Loads `assets/game_icons/<game_id>_icon.png` where `game_id` matches bridge `game_id` / ticket `game_key`.
    void reloadGameIcon(SDL_Renderer* renderer, const std::string& game_key);

    /// Animated horizontal slide uses this each frame (defaults from constructor).
    void setViewportOrigin(int viewport_x, int viewport_y);

    enum class HeaderMode {
        Normal,   // name pill + left/right arrows
        BoxSpace, // name pill only (no L/R arrows)
    };
    void setHeaderMode(HeaderMode mode, bool show_down_arrow);
    HeaderMode headerMode() const { return header_mode_; }
    void setBoxSpaceActive(bool active);
    void setItemOverlayActive(bool active);
    void setFocusDimming(bool active, std::optional<int> focused_slot, const Color& dim_color);

    void render(SDL_Renderer* renderer) const;

    /// Background + grid + footer only (used when another layer draws between backdrop and name plate).
    void renderBelowNamePlate(SDL_Renderer* renderer) const;
    /// Name pill, title texture, and prev/next arrows (after optional overlay).
    void renderNamePlate(SDL_Renderer* renderer) const;

#ifdef PR_ENABLE_TEST_HOOKS
    bool debugTitleTextureReady() const { return cached_title_tex_.texture != nullptr; }
    const std::string& debugCachedTitleText() const { return cached_title_text_; }
#endif

private:
    void refreshTitleTexture(SDL_Renderer* renderer) const;
    static std::string gameIconFilenameForGameId(const std::string& game_id);

    std::string project_root_;
    GameTransferBoxViewportStyle style_;
    BoxViewportRole role_;
    int viewport_x_ = 0;
    int viewport_y_ = 0;
    FontHandle title_font_;
    FontHandle label_font_;
    TextureHandle arrow_tex_;
    mutable TextureHandle box_space_label_tex_;
    mutable TextureHandle box_space_label_tex_white_;
    TextureHandle game_icon_tex_;
    BoxViewportModel model_;
    BoxViewportModel incoming_model_;
    std::deque<BoxViewportModel> content_slide_queue_{};
    bool content_slide_active_ = false;
    int content_slide_dir_ = 0;
    double content_slide_offset_x_ = 0.0;
    double content_slide_target_x_ = 0.0;
    mutable TextureHandle cached_title_tex_;
    mutable std::string cached_title_text_;
    mutable bool title_dirty_ = true;

    HeaderMode header_mode_ = HeaderMode::Normal;
    bool box_space_scroll_arrow_visible_ = false;
    bool box_space_active_ = false;
    bool item_overlay_active_ = false;
    double item_overlay_t_ = 0.0;
    bool focus_dimming_active_ = false;
    std::optional<int> focus_dimming_slot_{};
    Color focus_dimming_color_{150, 150, 150, 128};
};

} // namespace pr
