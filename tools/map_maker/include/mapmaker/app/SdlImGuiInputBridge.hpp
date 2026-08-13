#pragma once

#include <SDL.h>

#include <cstdint>
#include <string>

namespace pr::mapmaker {

// SDL event adapter for bgfx's Dear ImGui renderer. The bundled bgfx adapter
// handles pointer state but intentionally has no SDL keyboard/text bridge.
class SdlImGuiInputBridge {
public:
    void handleEvent(const SDL_Event& event);
    void beginFrame(int window_width, int window_height, std::uint16_t view_id = 255);

    bool wantsKeyboard() const;
    bool wantsPointer() const;
    bool wantsTextInput() const;

private:
    int mouse_x_ = 0;
    int mouse_y_ = 0;
    std::uint8_t mouse_buttons_ = 0;
    int scroll_total_ = 0;
    double scroll_accumulator_ = 0.0;
    std::string pending_text_;
};

} // namespace pr::mapmaker
