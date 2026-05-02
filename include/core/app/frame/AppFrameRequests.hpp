#pragma once

#include "core/app/audio/AppAudioDirector.hpp"

namespace pr {

class AppFrameRequests {
public:
    void requestButtonSfx();
    void requestButtonSfxIf(bool condition);
    void requestRipSfxIf(bool condition);
    void requestUiMoveSfxIf(bool condition);
    void requestPickupSfxIf(bool condition);
    void requestPutdownSfxIf(bool condition);
    void requestErrorSfxIf(bool condition);
    void requestSaveSfx();
    void requestUserSettingsSave();

    AppSfxRequests consumeSfxRequests();
    bool consumeUserSettingsSaveRequest();

private:
    AppSfxRequests sfx_requests_;
    bool user_settings_save_requested_ = false;
};

} // namespace pr
