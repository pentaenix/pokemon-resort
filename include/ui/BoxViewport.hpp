#pragma once

#include "core/Assets.hpp"
#include "core/Types.hpp"

#include <SDL.h>
#include <array>
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
    const BoxViewportModel& model() const { return model_; }

    /// Loads `assets/game_icons/pokemon_resort_icon.png` (left / Resort column; color from `palette.game_colors`).
    void reloadResortIcon(SDL_Renderer* renderer);

    /// Loads `assets/game_icons/<game_id>_icon.png` where `game_id` matches bridge `game_id` / ticket `game_key`.
    void reloadGameIcon(SDL_Renderer* renderer, const std::string& game_key);

    /// Animated horizontal slide uses this each frame (defaults from constructor).
    void setViewportOrigin(int viewport_x, int viewport_y);

    void render(SDL_Renderer* renderer) const;

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
    TextureHandle game_icon_tex_;
    BoxViewportModel model_;
    mutable TextureHandle cached_title_tex_;
    mutable std::string cached_title_text_;
    mutable bool title_dirty_ = true;
};

} // namespace pr
