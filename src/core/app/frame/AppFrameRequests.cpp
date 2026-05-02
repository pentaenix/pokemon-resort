#include "core/app/frame/AppFrameRequests.hpp"

namespace pr {

void AppFrameRequests::requestButtonSfx() {
    sfx_requests_.button = true;
}

void AppFrameRequests::requestButtonSfxIf(bool condition) {
    sfx_requests_.button = sfx_requests_.button || condition;
}

void AppFrameRequests::requestRipSfxIf(bool condition) {
    sfx_requests_.rip = sfx_requests_.rip || condition;
}

void AppFrameRequests::requestUiMoveSfxIf(bool condition) {
    sfx_requests_.ui_move = sfx_requests_.ui_move || condition;
}

void AppFrameRequests::requestPickupSfxIf(bool condition) {
    sfx_requests_.pickup = sfx_requests_.pickup || condition;
}

void AppFrameRequests::requestPutdownSfxIf(bool condition) {
    sfx_requests_.putdown = sfx_requests_.putdown || condition;
}

void AppFrameRequests::requestErrorSfxIf(bool condition) {
    sfx_requests_.error = sfx_requests_.error || condition;
}

void AppFrameRequests::requestSaveSfx() {
    sfx_requests_.save = true;
}

void AppFrameRequests::requestUserSettingsSave() {
    user_settings_save_requested_ = true;
}

AppSfxRequests AppFrameRequests::consumeSfxRequests() {
    AppSfxRequests requests;
    requests = sfx_requests_;
    sfx_requests_ = {};
    return requests;
}

bool AppFrameRequests::consumeUserSettingsSaveRequest() {
    const bool requested = user_settings_save_requested_;
    user_settings_save_requested_ = false;
    return requested;
}

} // namespace pr
