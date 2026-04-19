#pragma once

#include "core/Assets.hpp"
#include "core/Json.hpp"
#include "core/Types.hpp"
#include "ui/ScreenInput.hpp"
#include "ui/TransferSaveSelection.hpp"

#include <SDL.h>
#include <optional>
#include <string>
#include <vector>

namespace pr {

class TransferSystemScreen : public ScreenInput {
public:
    TransferSystemScreen(
        SDL_Renderer* renderer,
        const WindowConfig& window_config,
        const std::string& font_path,
        const std::string& project_root);

    void enter(const TransferSaveSelection& selection, SDL_Renderer* renderer);
    void update(double dt);
    void render(SDL_Renderer* renderer) const;

    void onAdvancePressed() override;
    void onBackPressed() override;
    bool handlePointerPressed(int logical_x, int logical_y) override;

    bool consumeButtonSfxRequest();
    bool consumeReturnToTicketListRequest();

private:
    struct BoxOneGridConfig {
        Point start{96, 140};
        double sprite_scale = 2.0;
        int column_spacing = 72;
        int row_spacing = 72;
        int columns = 6;
    };

    void loadTransferSystemConfig();
    void applyGridFromJson(BoxOneGridConfig& cfg, const JsonValue& grid);
    void reloadBoxSlotTextures(SDL_Renderer* renderer, const TransferSaveSelection& selection);
    void reloadResortSlotTextures(SDL_Renderer* renderer);
    void requestReturnToTicketList();
    void drawBackground(SDL_Renderer* renderer) const;
    void drawBoxSlots(SDL_Renderer* renderer) const;
    void drawTextureGrid(
        SDL_Renderer* renderer,
        const BoxOneGridConfig& grid,
        const std::vector<std::optional<TextureHandle>>& textures) const;

    struct BackgroundAnimation {
        bool enabled = false;
        double scale = 1.0;
        double speed_x = 0.0;
        double speed_y = 0.0;
    };

    WindowConfig window_config_;
    std::string project_root_;
    TextureHandle background_;
    BackgroundAnimation background_animation_;
    BoxOneGridConfig box_one_grid_;
    /// Mock resort PC (loaded from `resort_storage_box_path`); drawn on the opposite side from the transfer box.
    BoxOneGridConfig resort_grid_{Point{752, 140}, 2.0, 72, 72, 6};
    std::string resort_storage_box_path_{"../saves/resort_storage_box.json"};
    std::vector<std::optional<TextureHandle>> box_slot_textures_;
    std::vector<std::optional<TextureHandle>> resort_slot_textures_;
    double elapsed_seconds_ = 0.0;
    double fade_in_seconds_ = 0.0;
    bool play_button_sfx_requested_ = false;
    bool return_to_ticket_list_requested_ = false;
};

} // namespace pr
