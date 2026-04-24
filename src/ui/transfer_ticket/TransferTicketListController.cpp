#include "ui/transfer_ticket/TransferTicketListController.hpp"

#include <algorithm>
#include <cmath>

namespace pr::transfer_ticket {

namespace {

constexpr double kPi = 3.14159265358979323846;

} // namespace

void TransferTicketListController::configure(
    TicketListMetrics metrics,
    TicketRipAnimationConfig rip_animation,
    TicketSelectionTransitionConfig selection_transition) {
    metrics_ = metrics;
    rip_animation_ = rip_animation;
    selection_transition_ = selection_transition;
}

void TransferTicketListController::setSelections(const std::vector<TransferSaveSelection>& selections) {
    selections_ = selections;
    resetTicketState();
}

void TransferTicketListController::enter() {
    resetTicketState();
    return_to_main_menu_requested_ = false;
}

void TransferTicketListController::update(double dt) {
    const double scroll_speed = std::max(0.0, metrics_.scroll_speed);
    if (scroll_speed <= 0.0) {
        scroll_offset_y_ = target_scroll_offset_y_;
    } else {
        const double scroll_alpha = 1.0 - std::exp(-scroll_speed * std::max(0.0, dt));
        scroll_offset_y_ += (target_scroll_offset_y_ - scroll_offset_y_) * scroll_alpha;
        if (std::abs(target_scroll_offset_y_ - scroll_offset_y_) < 0.1) {
            scroll_offset_y_ = target_scroll_offset_y_;
        }
    }

    if (fade_to_black_active_) {
        fade_to_black_elapsed_seconds_ += dt;
        if (selection_transition_.fade_to_black_seconds <= 0.0 ||
            fade_to_black_elapsed_seconds_ >= selection_transition_.fade_to_black_seconds) {
            fade_to_black_active_ = false;
            open_transfer_system_requested_ = true;
        }
    }

    const double pre_tug_duration = std::max(0.0, rip_animation_.pre_tug_duration_seconds);
    const double rip_duration = std::max(0.001, rip_animation_.duration_seconds);
    const double total_duration = pre_tug_duration + rip_duration;
    for (std::size_t i = 0; i < rip_animation_active_.size(); ++i) {
        if (!rip_animation_active_[i]) {
            continue;
        }

        rip_elapsed_seconds_[i] += dt;
        if (pre_tug_duration > 0.0 && rip_elapsed_seconds_[i] < pre_tug_duration) {
            const double t = std::min(1.0, rip_elapsed_seconds_[i] / pre_tug_duration);
            const double tug = std::sin(t * kPi);
            right_stub_offsets_[i] = -static_cast<int>(std::round(
                static_cast<double>(rip_animation_.pre_tug_distance) * tug));
            continue;
        }

        const double t = std::min(1.0, (rip_elapsed_seconds_[i] - pre_tug_duration) / rip_duration);
        const double eased = 1.0 - ((1.0 - t) * (1.0 - t));
        const double start = -static_cast<double>(rip_animation_.pre_tug_distance);
        const double end = static_cast<double>(rip_animation_.distance);
        right_stub_offsets_[i] = static_cast<int>(std::round(start + (end - start) * eased));
        if (rip_elapsed_seconds_[i] >= total_duration) {
            right_stub_offsets_[i] = rip_animation_.distance;
            rip_animation_active_[i] = false;
            ripped_[i] = true;
            if (static_cast<int>(i) == activating_ticket_index_) {
                beginOrCompleteHandoff();
            }
        }
    }
}

bool TransferTicketListController::canNavigate() const {
    return ticketCount() > 0 && activating_ticket_index_ < 0;
}

bool TransferTicketListController::navigate(int delta) {
    const int count = ticketCount();
    if (count <= 0 || delta == 0 || activating_ticket_index_ >= 0) {
        return false;
    }

    const int previous = selected_ticket_index_;
    selected_ticket_index_ = (selected_ticket_index_ + delta) % count;
    if (selected_ticket_index_ < 0) {
        selected_ticket_index_ += count;
    }
    if (selected_ticket_index_ == previous) {
        return false;
    }

    const bool wrapped_to_end = previous == 0 && selected_ticket_index_ == count - 1;
    const bool wrapped_to_start = previous == count - 1 && selected_ticket_index_ == 0;
    updateScrollOffset();
    if (wrapped_to_end || wrapped_to_start) {
        scroll_offset_y_ = target_scroll_offset_y_;
    }
    return true;
}

bool TransferTicketListController::advance() {
    if (selected_ticket_index_ < 0 ||
        selected_ticket_index_ >= static_cast<int>(ripped_.size()) ||
        rip_animation_active_[static_cast<std::size_t>(selected_ticket_index_)] ||
        ripped_[static_cast<std::size_t>(selected_ticket_index_)]) {
        return false;
    }

    activating_ticket_index_ = selected_ticket_index_;
    beginRipForActivatingTicket();
    return true;
}

void TransferTicketListController::back() {
    return_to_main_menu_requested_ = true;
}

bool TransferTicketListController::handlePointerPressed(int logical_x, int logical_y) {
    pointer_pressed_on_ticket_ = false;
    pointer_dragging_list_ = false;
    pointer_pressed_ticket_index_ = -1;
    if (activating_ticket_index_ >= 0) {
        return false;
    }

    const bool inside_viewport =
        logical_x >= metrics_.viewport.x &&
        logical_x < metrics_.viewport.x + metrics_.viewport.w &&
        logical_y >= metrics_.viewport.y &&
        logical_y < metrics_.viewport.y + metrics_.viewport.h;
    if (!inside_viewport) {
        return false;
    }

    pointer_pressed_on_ticket_ = true;
    pointer_press_y_ = logical_y;
    pointer_press_scroll_offset_y_ = scroll_offset_y_;
    for (int i = ticketCount() - 1; i >= 0; --i) {
        if (pointInTicket(logical_x, logical_y, i)) {
            pointer_pressed_ticket_index_ = i;
            break;
        }
    }
    return true;
}

void TransferTicketListController::handlePointerMoved(int logical_x, int logical_y) {
    (void)logical_x;
    if (!pointer_pressed_on_ticket_ || activating_ticket_index_ >= 0) {
        return;
    }

    constexpr int kDragThresholdPixels = 4;
    const int delta_y = logical_y - pointer_press_y_;
    if (!pointer_dragging_list_ && std::abs(delta_y) >= kDragThresholdPixels) {
        pointer_dragging_list_ = true;
    }
    if (!pointer_dragging_list_) {
        return;
    }

    target_scroll_offset_y_ = std::max(
        0.0,
        std::min(maxScrollOffset(), pointer_press_scroll_offset_y_ - static_cast<double>(delta_y)));
    scroll_offset_y_ = target_scroll_offset_y_;
}

bool TransferTicketListController::handlePointerReleased(int logical_x, int logical_y) {
    const bool started_on_ticket = pointer_pressed_on_ticket_;
    const int pressed_ticket_index = pointer_pressed_ticket_index_;
    const bool dragged_list = pointer_dragging_list_;
    pointer_pressed_on_ticket_ = false;
    pointer_dragging_list_ = false;
    pointer_pressed_ticket_index_ = -1;
    if (!started_on_ticket) {
        return false;
    }
    if (dragged_list) {
        return true;
    }

    if (pressed_ticket_index < 0 || !pointInTicket(logical_x, logical_y, pressed_ticket_index)) {
        return false;
    }

    selected_ticket_index_ = pressed_ticket_index;
    advance();
    return true;
}

bool TransferTicketListController::consumeReturnToMainMenuRequest() {
    const bool requested = return_to_main_menu_requested_;
    return_to_main_menu_requested_ = false;
    return requested;
}

bool TransferTicketListController::consumeOpenTransferSystemRequest(TransferSaveSelection& out_selection) {
    const bool requested = open_transfer_system_requested_;
    if (requested) {
        out_selection = selected_transfer_save_;
    }
    open_transfer_system_requested_ = false;
    return requested;
}

void TransferTicketListController::prepareReturnFromGameTransferScreen() {
    const auto count = static_cast<std::size_t>(ticketCount());
    activating_ticket_index_ = -1;
    fade_to_black_active_ = false;
    fade_to_black_elapsed_seconds_ = 0.0;
    open_transfer_system_requested_ = false;
    pointer_pressed_on_ticket_ = false;
    pointer_dragging_list_ = false;
    pointer_pressed_ticket_index_ = -1;

    right_stub_offsets_.assign(count, 0);
    rip_elapsed_seconds_.assign(count, 0.0);
    rip_animation_active_.assign(count, false);
    ripped_.assign(count, false);

    if (selected_ticket_index_ >= static_cast<int>(count)) {
        selected_ticket_index_ = count > 0 ? static_cast<int>(count) - 1 : -1;
    }
    if (selected_ticket_index_ < 0 && count > 0) {
        selected_ticket_index_ = 0;
    }

    updateScrollOffset();
    scroll_offset_y_ = target_scroll_offset_y_;
}

int TransferTicketListController::ticketCount() const {
    return static_cast<int>(selections_.size());
}

int TransferTicketListController::rightStubOffset(int index) const {
    if (index < 0 || index >= static_cast<int>(right_stub_offsets_.size())) {
        return 0;
    }
    return right_stub_offsets_[static_cast<std::size_t>(index)];
}

bool TransferTicketListController::isRipAnimationActive(int index) const {
    return index >= 0 &&
           index < static_cast<int>(rip_animation_active_.size()) &&
           rip_animation_active_[static_cast<std::size_t>(index)];
}

bool TransferTicketListController::isRipped(int index) const {
    return index >= 0 &&
           index < static_cast<int>(ripped_.size()) &&
           ripped_[static_cast<std::size_t>(index)];
}

void TransferTicketListController::resetTicketState() {
    const auto count = static_cast<std::size_t>(ticketCount());
    right_stub_offsets_.assign(count, 0);
    rip_elapsed_seconds_.assign(count, 0.0);
    rip_animation_active_.assign(count, false);
    ripped_.assign(count, false);
    open_transfer_system_requested_ = false;
    fade_to_black_active_ = false;
    fade_to_black_elapsed_seconds_ = 0.0;
    pointer_pressed_on_ticket_ = false;
    pointer_dragging_list_ = false;
    pointer_pressed_ticket_index_ = -1;
    selected_ticket_index_ = count > 0 ? 0 : -1;
    activating_ticket_index_ = -1;
    scroll_offset_y_ = 0.0;
    target_scroll_offset_y_ = 0.0;
}

void TransferTicketListController::updateScrollOffset() {
    if (ticketCount() <= 0 || selected_ticket_index_ <= 0) {
        target_scroll_offset_y_ = 0.0;
        return;
    }

    const double ticket_center_y =
        static_cast<double>(metrics_.start_y) +
        static_cast<double>(selected_ticket_index_ * metrics_.separation_y) +
        static_cast<double>(metrics_.ticket_height) * 0.5;
    const double viewport_center_y =
        static_cast<double>(metrics_.viewport.y) +
        static_cast<double>(metrics_.viewport.h) * 0.5;

    target_scroll_offset_y_ = std::max(
        0.0,
        std::min(maxScrollOffset(), ticket_center_y - viewport_center_y));
}

double TransferTicketListController::maxScrollOffset() const {
    const int count = ticketCount();
    if (count <= 1) {
        return 0.0;
    }

    const double last_ticket_center_y =
        static_cast<double>(metrics_.start_y) +
        static_cast<double>((count - 1) * metrics_.separation_y) +
        static_cast<double>(metrics_.ticket_height) * 0.5;
    const double viewport_center_y =
        static_cast<double>(metrics_.viewport.y) +
        static_cast<double>(metrics_.viewport.h) * 0.5;
    return std::max(0.0, last_ticket_center_y - viewport_center_y);
}

bool TransferTicketListController::pointInTicket(int logical_x, int logical_y, int index) const {
    const int rounded_scroll_offset_y = static_cast<int>(std::round(scroll_offset_y_));
    const int ticket_left = metrics_.start_x;
    const int ticket_top = metrics_.start_y + index * metrics_.separation_y - rounded_scroll_offset_y;

    const bool inside_viewport =
        logical_x >= metrics_.viewport.x &&
        logical_x < metrics_.viewport.x + metrics_.viewport.w &&
        logical_y >= metrics_.viewport.y &&
        logical_y < metrics_.viewport.y + metrics_.viewport.h;
    return inside_viewport &&
           logical_x >= ticket_left &&
           logical_x < ticket_left + metrics_.ticket_width &&
           logical_y >= ticket_top &&
           logical_y < ticket_top + metrics_.ticket_height;
}

void TransferTicketListController::beginRipForActivatingTicket() {
    if (activating_ticket_index_ < 0 ||
        activating_ticket_index_ >= static_cast<int>(ripped_.size())) {
        return;
    }

    const auto index = static_cast<std::size_t>(activating_ticket_index_);
    if (rip_animation_.enabled && rip_animation_.duration_seconds > 0.0) {
        rip_elapsed_seconds_[index] = 0.0;
        rip_animation_active_[index] = true;
    } else {
        right_stub_offsets_[index] = rip_animation_.distance;
        ripped_[index] = true;
        beginOrCompleteHandoff();
    }
}

void TransferTicketListController::beginOrCompleteHandoff() {
    if (activating_ticket_index_ < 0 ||
        activating_ticket_index_ >= static_cast<int>(selections_.size())) {
        return;
    }

    selected_transfer_save_ = selections_[static_cast<std::size_t>(activating_ticket_index_)];
    if (selection_transition_.fade_to_black_seconds > 0.0 &&
        selection_transition_.fade_to_black_max_alpha > 0) {
        fade_to_black_elapsed_seconds_ = 0.0;
        fade_to_black_active_ = true;
    } else {
        open_transfer_system_requested_ = true;
    }
}

} // namespace pr::transfer_ticket
