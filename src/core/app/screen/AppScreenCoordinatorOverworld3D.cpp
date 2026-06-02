#include "core/app/screen/AppScreenCoordinator.hpp"

#include "ui/Overworld3DTestScreen.hpp"
#include "ui/TitleScreen.hpp"

namespace pr {

void AppScreenCoordinator::updateOverworld3D(double dt) {
    overworld3d_test_.update(dt);
    if (overworld3d_test_.consumeReturnToTitleRequested()) {
        overworld3d_test_.resetForNextLaunch();
        title_screen_.returnToMainMenuFromResort();
        active_screen_ = ActiveScreen::Title;
    }
}

} // namespace pr
