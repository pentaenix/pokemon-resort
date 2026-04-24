#pragma once

#include "ui/TransferSaveSelection.hpp"

#include <SDL.h>

#include <vector>

namespace pr::transfer_ticket {

struct TicketListMetrics {
    int start_x = 45;
    int start_y = 167;
    int separation_y = 308;
    SDL_Rect viewport{0, 156, 1280, 615};
    double scroll_speed = 14.0;
    int ticket_width = 0;
    int ticket_height = 0;
};

struct TicketRipAnimationConfig {
    bool enabled = true;
    int distance = 28;
    int pre_tug_distance = 3;
    double pre_tug_duration_seconds = 0.08;
    double duration_seconds = 0.35;
};

struct TicketSelectionTransitionConfig {
    double fade_to_black_seconds = 0.12;
    int fade_to_black_max_alpha = 255;
};

class TransferTicketListController {
public:
    void configure(
        TicketListMetrics metrics,
        TicketRipAnimationConfig rip_animation,
        TicketSelectionTransitionConfig selection_transition);

    void setSelections(const std::vector<TransferSaveSelection>& selections);
    void enter();
    void update(double dt);

    bool canNavigate() const;
    bool navigate(int delta);
    bool advance();
    void back();

    bool handlePointerPressed(int logical_x, int logical_y);
    void handlePointerMoved(int logical_x, int logical_y);
    bool handlePointerReleased(int logical_x, int logical_y);

    bool consumeReturnToMainMenuRequest();
    bool consumeOpenTransferSystemRequest(TransferSaveSelection& out_selection);
    void prepareReturnFromGameTransferScreen();

    int ticketCount() const;
    int selectedTicketIndex() const { return selected_ticket_index_; }
    double scrollOffset() const { return scroll_offset_y_; }
    bool fadeToBlackActive() const { return fade_to_black_active_; }
    double fadeToBlackElapsedSeconds() const { return fade_to_black_elapsed_seconds_; }
    int rightStubOffset(int index) const;
    bool isRipAnimationActive(int index) const;
    bool isRipped(int index) const;

private:
    void resetTicketState();
    void updateScrollOffset();
    double maxScrollOffset() const;
    bool pointInTicket(int logical_x, int logical_y, int index) const;
    void beginRipForActivatingTicket();
    void beginOrCompleteHandoff();

    TicketListMetrics metrics_{};
    TicketRipAnimationConfig rip_animation_{};
    TicketSelectionTransitionConfig selection_transition_{};
    std::vector<TransferSaveSelection> selections_;
    std::vector<int> right_stub_offsets_;
    std::vector<double> rip_elapsed_seconds_;
    std::vector<bool> rip_animation_active_;
    std::vector<bool> ripped_;

    bool return_to_main_menu_requested_ = false;
    bool open_transfer_system_requested_ = false;
    TransferSaveSelection selected_transfer_save_{};
    bool fade_to_black_active_ = false;
    double fade_to_black_elapsed_seconds_ = 0.0;
    bool pointer_pressed_on_ticket_ = false;
    bool pointer_dragging_list_ = false;
    int pointer_pressed_ticket_index_ = -1;
    int pointer_press_y_ = 0;
    double pointer_press_scroll_offset_y_ = 0.0;
    int selected_ticket_index_ = -1;
    int activating_ticket_index_ = -1;
    double scroll_offset_y_ = 0.0;
    double target_scroll_offset_y_ = 0.0;
};

} // namespace pr::transfer_ticket
