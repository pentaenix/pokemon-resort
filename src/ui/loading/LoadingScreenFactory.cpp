#include "ui/loading/LoadingScreenFactory.hpp"

#include "ui/loading/PokeballLoadingScreen.hpp"
#include "ui/loading/ResortTransferLoadingScreen.hpp"

#include <stdexcept>

namespace pr {

std::unique_ptr<LoadingScreenBase> createLoadingScreen(
    LoadingScreenType type,
    SDL_Renderer* renderer,
    const WindowConfig& window_config,
    const std::string& fallback_font_path,
    const std::string& project_root) {
    switch (type) {
        case LoadingScreenType::Pokeball:
            return std::make_unique<PokeballLoadingScreen>(renderer, window_config, fallback_font_path, project_root);
        case LoadingScreenType::QuickBoatPass:
        case LoadingScreenType::ResortTransfer:
            return std::make_unique<ResortTransferLoadingScreen>(renderer, window_config, project_root, type);
    }
    throw std::runtime_error("Unknown loading screen type");
}

} // namespace pr
