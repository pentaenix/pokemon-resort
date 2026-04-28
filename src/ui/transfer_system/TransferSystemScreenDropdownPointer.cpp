#include "ui/TransferSystemScreen.hpp"

#include <SDL.h>

namespace pr {

void TransferSystemScreen::applyActiveDropdownSelection() {
    if (gameDropdownNavigationActive()) {
        applyGameBoxDropdownSelection();
    } else if (resortDropdownNavigationActive()) {
        applyResortBoxDropdownSelection();
    }
}

void TransferSystemScreen::stepDropdownHighlight(int delta) {
    if (gameDropdownNavigationActive()) {
        SDL_Rect outer{};
        int list_h = 0;
        int list_clip_y = 0;
        const int inner_h =
            computeGameBoxDropdownOuterRect(outer, static_cast<float>(game_box_browser_.dropdownExpandT()), list_h, list_clip_y)
                ? list_h
                : 0;
        game_box_browser_.stepDropdownHighlight(delta, static_cast<int>(game_pc_boxes_.size()), inner_h);
    } else if (resortDropdownNavigationActive()) {
        stepResortDropdownHighlight(delta);
    }
}

bool TransferSystemScreen::handleDropdownPointerPressed(int logical_x, int logical_y) {
    if (!box_name_dropdown_style_.enabled || game_pc_boxes_.size() < 2) {
        return false;
    }

    if (game_box_browser_.dropdownOpenTarget() && game_box_browser_.dropdownExpandT() > 0.05) {
        SDL_Rect outer{};
        int list_h = 0;
        int list_clip_y = 0;
        if (computeGameBoxDropdownOuterRect(outer, static_cast<float>(game_box_browser_.dropdownExpandT()), list_h, list_clip_y)) {
            const bool in_outer = logical_x >= outer.x && logical_x < outer.x + outer.w && logical_y >= outer.y &&
                                  logical_y < outer.y + outer.h;
            if (in_outer) {
                dropdown_lmb_down_in_panel_ = true;
                dropdown_lmb_last_y_ = logical_y;
                dropdown_lmb_drag_accum_ = 0.0;
                if (const std::optional<int> row = dropdownRowIndexAtScreen(logical_x, logical_y)) {
                    stepDropdownHighlight(*row - game_box_browser_.dropdownHighlightIndex());
                }
                return true;
            }
            if (hitTestGameBoxNamePlate(logical_x, logical_y)) {
                toggleGameBoxDropdown();
                return true;
            }
            closeGameBoxDropdown();
            return true;
        }
    } else if (!game_box_browser_.gameBoxSpaceMode() && panelsReadyForInteraction() &&
               hitTestGameBoxNamePlate(logical_x, logical_y)) {
        toggleGameBoxDropdown();
        return true;
    }

    return false;
}

bool TransferSystemScreen::handleResortDropdownPointerPressed(int logical_x, int logical_y) {
    if (!box_name_dropdown_style_.enabled || resort_pc_boxes_.size() < 2) {
        return false;
    }

    if (resort_box_browser_.dropdownOpenTarget() && resort_box_browser_.dropdownExpandT() > 0.05) {
        SDL_Rect outer{};
        int list_h = 0;
        int list_clip_y = 0;
        if (computeResortBoxDropdownOuterRect(
                outer, static_cast<float>(resort_box_browser_.dropdownExpandT()), list_h, list_clip_y)) {
            const bool in_outer = logical_x >= outer.x && logical_x < outer.x + outer.w && logical_y >= outer.y &&
                                  logical_y < outer.y + outer.h;
            if (in_outer) {
                dropdown_lmb_down_in_panel_ = true;
                dropdown_lmb_last_y_ = logical_y;
                dropdown_lmb_drag_accum_ = 0.0;
                if (const std::optional<int> row = resortDropdownRowIndexAtScreen(logical_x, logical_y)) {
                    stepResortDropdownHighlight(*row - resort_box_browser_.dropdownHighlightIndex());
                }
                return true;
            }
            if (hitTestResortBoxNamePlate(logical_x, logical_y)) {
                toggleResortBoxDropdown();
                return true;
            }
            closeResortBoxDropdown();
            return true;
        }
    } else if (!resort_box_browser_.gameBoxSpaceMode() && panelsReadyForInteraction() &&
               hitTestResortBoxNamePlate(logical_x, logical_y)) {
        toggleResortBoxDropdown();
        return true;
    }

    return false;
}

} // namespace pr

