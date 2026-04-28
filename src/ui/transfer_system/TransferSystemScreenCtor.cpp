#include "ui/TransferSystemScreen.hpp"

#include "ui/transfer_system/GameTransferConfig.hpp"
#include "ui/transfer_system/TransferSaveConfig.hpp"
#include "ui/transfer_system/detail/TextureLoad.hpp"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <utility>

namespace pr {

using transfer_system::detail::loadTexture;
using transfer_system::detail::loadTextureOptional;
using transfer_system::detail::resolvePath;
using transfer_system::detail::setTextureNearestNeighbor;

TransferSystemScreen::TransferSystemScreen(
    SDL_Renderer* renderer,
    const WindowConfig& window_config,
    const std::string& font_path,
    const std::string& project_root,
    std::shared_ptr<PokeSpriteAssets> sprite_assets,
    std::string save_directory,
    const char* bridge_argv0,
    resort::PokemonResortService* resort_service)
    : window_config_(window_config),
      renderer_(renderer),
      project_root_(project_root),
      save_directory_(std::move(save_directory)),
      bridge_argv0_(bridge_argv0),
      font_path_(font_path),
      sprite_assets_(std::move(sprite_assets)),
      resort_service_(resort_service),
      background_(loadTexture(
          renderer,
          resolvePath(project_root_, "assets/transfer_select_save/background.png"))) {
    const transfer_system::LoadedGameTransfer loaded = transfer_system::loadGameTransfer(project_root_);
    const transfer_system::LoadedTransferSave transfer_save = transfer_system::loadTransferSave(project_root_);
    resort_pc_box_count_ = std::clamp(loaded.resort_pc_box_count, 1, 512);
    ui_state_.configure(loaded.fade_in_seconds, loaded.fade_out_seconds);
    background_animation_.enabled = loaded.background_animation.enabled;
    background_animation_.scale = loaded.background_animation.scale;
    background_animation_.speed_x = loaded.background_animation.speed_x;
    background_animation_.speed_y = loaded.background_animation.speed_y;
    pill_style_ = loaded.pill_toggle;
    carousel_style_ = loaded.tool_carousel;
    exit_button_enabled_ = loaded.exit_button_enabled;
    exit_button_gap_pixels_ = loaded.exit_button_gap_pixels;
    exit_button_icon_scale_ = loaded.exit_button_icon_scale;
    exit_button_icon_mod_color_ = loaded.exit_button_icon_mod_color;
    box_name_dropdown_style_ = loaded.box_name_dropdown;
    selection_cursor_style_ = loaded.selection_cursor;
    mini_preview_style_ = loaded.mini_preview;
    box_viewport_style_ = loaded.box_viewport;
    pokemon_action_menu_style_ = loaded.pokemon_action_menu;
    box_space_long_press_style_ = loaded.box_space_long_press;
    info_banner_style_ = loaded.info_banner;
    exit_save_modal_style_ = transfer_save.exit_save_modal;
    pill_font_ = loadFontPreferringUnicode(font_path_, std::max(8, pill_style_.font_pt), project_root_);
    dropdown_item_font_ =
        loadFontPreferringUnicode(font_path_, std::max(8, box_name_dropdown_style_.item_font_pt), project_root_);
    box_rename_modal_body_font_ =
        loadFontPreferringUnicode(font_path_, std::max(22, box_name_dropdown_style_.item_font_pt + 8), project_root_);
    speech_bubble_font_ = loadFontPreferringUnicode(
        font_path_,
        std::max(8, selection_cursor_style_.speech_bubble.font_pt),
        project_root_);
    pokemon_action_menu_font_ = loadFontPreferringUnicode(
        font_path_,
        std::max(8, pokemon_action_menu_style_.font_pt),
        project_root_);
    exit_save_modal_font_ = loadFontPreferringUnicode(
        font_path_,
        std::max(8, exit_save_modal_style_.font_pt),
        project_root_);
    cachePillLabelTextures(renderer);

    const std::array<std::string, 4> tool_paths{
        carousel_style_.texture_multiple,
        carousel_style_.texture_basic,
        carousel_style_.texture_swap,
        carousel_style_.texture_items};
    for (std::size_t i = 0; i < tool_paths.size(); ++i) {
        tool_icons_[i] = loadTextureOptional(renderer, resolvePath(project_root_, tool_paths[i]));
    }
    exit_button_icon_ = loadTextureOptional(renderer, resolvePath(project_root_, "assets/game_transfer/exit.png"));

    box_space_full_tex_ = loadTextureOptional(renderer, resolvePath(project_root_, "assets/pokesprite/boxes/full.png"));
    box_space_empty_tex_ = loadTextureOptional(renderer, resolvePath(project_root_, "assets/pokesprite/boxes/empty.png"));
    box_space_noempty_tex_ = loadTextureOptional(renderer, resolvePath(project_root_, "assets/pokesprite/boxes/noempty.png"));
    setTextureNearestNeighbor(box_space_full_tex_);
    setTextureNearestNeighbor(box_space_empty_tex_);
    setTextureNearestNeighbor(box_space_noempty_tex_);

    constexpr int kBoxMarginX = 40;
    constexpr int kBoxViewportY = 100;
    const int game_box_x = std::max(0, window_config_.virtual_width - kBoxMarginX - BoxViewport::kViewportWidth);
    resort_box_viewport_ = std::make_unique<BoxViewport>(
        renderer,
        project_root,
        font_path,
        loaded.box_viewport,
        BoxViewportRole::ResortStorage,
        kBoxMarginX,
        kBoxViewportY);
    game_save_box_viewport_ = std::make_unique<BoxViewport>(
        renderer,
        project_root,
        font_path,
        loaded.box_viewport,
        BoxViewportRole::ExternalGameSave,
        game_box_x,
        kBoxViewportY);
}

} // namespace pr

