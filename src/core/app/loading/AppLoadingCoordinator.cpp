#include "core/app/loading/AppLoadingCoordinator.hpp"

#include "ui/loading/LoadingScreenFactory.hpp"

#include <utility>

namespace pr {

AppLoadingCoordinator::AppLoadingCoordinator(
    SDL_Renderer* renderer,
    WindowConfig window_config,
    std::string font_path,
    std::string project_root)
    : renderer_(renderer),
      window_config_(std::move(window_config)),
      font_path_(std::move(font_path)),
      project_root_(std::move(project_root)),
      config_(loadResortTransferLoadingConfig(project_root_)) {
    resort_transfer_screen_ = createLoadingScreen(
        LoadingScreenType::ResortTransfer,
        renderer_,
        window_config_,
        font_path_,
        project_root_);
    pokeball_screen_ = createLoadingScreen(
        LoadingScreenType::Pokeball,
        renderer_,
        window_config_,
        font_path_,
        project_root_);
    quick_boat_pass_screen_ = createLoadingScreen(
        LoadingScreenType::QuickBoatPass,
        renderer_,
        window_config_,
        font_path_,
        project_root_);
    active_screen_ = resort_transfer_screen_.get();
}

LoadingScreenBase* AppLoadingCoordinator::activeScreen() {
    return active_screen_;
}

void AppLoadingCoordinator::beginResortTransfer() {
    temporal_loading_elapsed_seconds_ = 0.0;
    temporal_simulated_load_duration_seconds_ = 0.0;
    temporal_loading_completion_sent_ = true;
    successful_save_quick_pass_waits_for_external_complete_ = false;
    active_screen_ = resort_transfer_screen_.get();
    resort_transfer_screen_->enterWithMessageKey("message_transport_pokemon");
}

void AppLoadingCoordinator::beginTradeDemo() {
    temporal_loading_elapsed_seconds_ = 0.0;
    successful_save_quick_pass_waits_for_external_complete_ = false;
    temporal_loading_completion_sent_ = false;
    if (auto it = config_.temporal.find("trade_button"); it != config_.temporal.end()) {
        temporal_loading_type_ = it->second.loading_type;
        temporal_simulated_load_duration_seconds_ = it->second.simulated_load_duration_seconds;
        if (temporal_loading_type_ == TemporalLoadingDemoType::Pokeball) {
            active_screen_ = pokeball_screen_.get();
            active_screen_->enter();
        } else if (temporal_loading_type_ == TemporalLoadingDemoType::QuickBoatPass) {
            active_screen_ = quick_boat_pass_screen_.get();
            active_screen_->beginQuickPass(temporal_simulated_load_duration_seconds_ > 0.0);
        } else {
            active_screen_ = resort_transfer_screen_.get();
            active_screen_->beginLoadingWithMessageKey(it->second.message_key);
        }
    } else {
        temporal_loading_type_ = TemporalLoadingDemoType::QuickBoatPass;
        temporal_simulated_load_duration_seconds_ = 0.0;
        active_screen_ = quick_boat_pass_screen_.get();
        active_screen_->beginQuickPass();
    }

    if (temporal_simulated_load_duration_seconds_ <= 0.0 &&
        temporal_loading_type_ != TemporalLoadingDemoType::QuickBoatPass) {
        active_screen_->markLoadingComplete();
        temporal_loading_completion_sent_ = true;
    } else if (temporal_simulated_load_duration_seconds_ <= 0.0) {
        temporal_loading_completion_sent_ = true;
    }
}

void AppLoadingCoordinator::beginSuccessfulSaveQuickPass(const std::string& message_key) {
    temporal_loading_elapsed_seconds_ = 0.0;
    temporal_simulated_load_duration_seconds_ = 0.0;
    temporal_loading_completion_sent_ = false;
    successful_save_quick_pass_waits_for_external_complete_ = true;
    active_screen_ = quick_boat_pass_screen_.get();
    if (!message_key.empty()) {
        quick_boat_pass_screen_->setLoadingMessageKey(message_key);
    }
    active_screen_->beginQuickPass(true);
}

void AppLoadingCoordinator::markSuccessfulSaveQuickPassWorkComplete() {
    if (!successful_save_quick_pass_waits_for_external_complete_ || !active_screen_) {
        return;
    }
    successful_save_quick_pass_waits_for_external_complete_ = false;
    active_screen_->markLoadingComplete();
    temporal_loading_completion_sent_ = true;
}

void AppLoadingCoordinator::update(double dt) {
    if (!active_screen_) {
        return;
    }

    active_screen_->update(dt);
    if (!temporal_loading_completion_sent_ && !successful_save_quick_pass_waits_for_external_complete_) {
        temporal_loading_elapsed_seconds_ += dt;
        if (temporal_loading_elapsed_seconds_ >= temporal_simulated_load_duration_seconds_) {
            active_screen_->markLoadingComplete();
            temporal_loading_completion_sent_ = true;
        }
    }
}

bool AppLoadingCoordinator::consumeReturnToMenuRequest() {
    return active_screen_ && active_screen_->consumeReturnToMenuRequest();
}

bool AppLoadingCoordinator::isLoadingAnimationComplete() const {
    return active_screen_ && active_screen_->isLoadingAnimationComplete();
}

void AppLoadingCoordinator::rebindRenderer(SDL_Renderer* renderer) {
    renderer_ = renderer;
    if (!renderer_) {
        resort_transfer_screen_.reset();
        pokeball_screen_.reset();
        quick_boat_pass_screen_.reset();
        active_screen_ = nullptr;
        return;
    }

    enum class ActiveKind { ResortTransfer, Pokeball, QuickBoatPass } active_kind = ActiveKind::ResortTransfer;
    if (active_screen_ == pokeball_screen_.get()) {
        active_kind = ActiveKind::Pokeball;
    } else if (active_screen_ == quick_boat_pass_screen_.get()) {
        active_kind = ActiveKind::QuickBoatPass;
    }

    resort_transfer_screen_ = createLoadingScreen(
        LoadingScreenType::ResortTransfer,
        renderer_,
        window_config_,
        font_path_,
        project_root_);
    pokeball_screen_ = createLoadingScreen(
        LoadingScreenType::Pokeball,
        renderer_,
        window_config_,
        font_path_,
        project_root_);
    quick_boat_pass_screen_ = createLoadingScreen(
        LoadingScreenType::QuickBoatPass,
        renderer_,
        window_config_,
        font_path_,
        project_root_);

    switch (active_kind) {
        case ActiveKind::Pokeball:
            active_screen_ = pokeball_screen_.get();
            break;
        case ActiveKind::QuickBoatPass:
            active_screen_ = quick_boat_pass_screen_.get();
            break;
        case ActiveKind::ResortTransfer:
            active_screen_ = resort_transfer_screen_.get();
            break;
    }
}

} // namespace pr
