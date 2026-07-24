#include "gameplay/world3d/dialogue/OverworldTextboxConfig.hpp"
#include "gameplay/world3d/dialogue/OverworldTextboxController.hpp"
#include "gameplay/world3d/dialogue/OverworldTextboxRenderer.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace {

struct TestFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure(message);
    }
}

fs::path repositoryRoot() {
    fs::path current = fs::current_path();
    while (!current.empty()) {
        if (fs::exists(current / "config" / "app.json") &&
            fs::exists(current / "assets" / "overworld" / "ui" / "text_boxes.png")) {
            return current;
        }
        current = current.parent_path();
    }
    throw TestFailure("Could not locate repository root from " + fs::current_path().string());
}

void testConfigLoadsDedicatedTextboxJson() {
    const fs::path root = repositoryRoot();
    const auto config = pr::gameplay::world3d::dialogue::loadOverworldTextboxConfig(root.string());
    expect(pr::gameplay::world3d::dialogue::overworldTextboxEnabled(config),
        "textbox visibleMode should default to enabled");
    expect(config.selected_skin_index == 0, "selected skin index should load");
    expect(config.valid_skin_count == 13, "valid skin count should ignore the empty 14th cell");
    expect(config.source_cell_width_px == 256, "source cell width should load");
    expect(config.source_cell_height_px == 48, "source cell height should load");
    expect(config.sheet_columns == 2, "sheet column count should load");
    expect(config.bottom_padding_px == 5, "bottom padding should load");
    expect(config.side_padding_px == 5, "side padding should load");
    expect(config.stretch_strip_width_px == 5, "stretch strip width should load");
    expect(config.stretch_strip_center_x_px == 128, "stretch strip source center should load");
    expect(config.text_font_path == "assets/fonts/power clear.ttf", "regular Power font path should load");
    expect(config.text_font_size_px == 14, "textbox font size should load");
    expect(config.text_left_inset_px == 16, "textbox left inset should be data-driven");
    expect(config.text_right_inset_px == 12, "textbox right inset should be data-driven");
    expect(config.text_top_inset_px == 7, "textbox top inset should be data-driven");
    expect(config.attend_button_enabled, "overworld Attend button should load enabled");
    expect(config.attend_button_icon_path == "assets/overworld/ui/attend_icon.png",
        "overworld Attend button icon should be data-driven");
    expect(config.attend_button_top_px == 16 && config.attend_button_right_px == 16,
        "overworld Attend button position should be data-driven");
    expect(config.attend_button_width_px == 48 && config.attend_button_height_px == 48,
        "overworld Attend button size should be data-driven");
}

void testSkinIndexUsesColumnFirstVisualOrder() {
    using pr::gameplay::world3d::dialogue::OverworldTextboxConfig;
    using pr::gameplay::world3d::dialogue::OverworldTextboxRenderer;
    OverworldTextboxConfig config{};
    config.source_cell_width_px = 256;
    config.source_cell_height_px = 48;
    config.sheet_columns = 2;
    config.valid_skin_count = 13;

    config.selected_skin_index = 0;
    SDL_Rect source = OverworldTextboxRenderer::sourceRectForSkin(config, 512, 336);
    expect(source.x == 0 && source.y == 0, "skin 0 should be top-left");

    config.selected_skin_index = 1;
    source = OverworldTextboxRenderer::sourceRectForSkin(config, 512, 336);
    expect(source.x == 0 && source.y == 48, "skin 1 should be directly under skin 0");

    config.selected_skin_index = 6;
    source = OverworldTextboxRenderer::sourceRectForSkin(config, 512, 336);
    expect(source.x == 0 && source.y == 288, "skin 6 should be bottom-left");

    config.selected_skin_index = 7;
    source = OverworldTextboxRenderer::sourceRectForSkin(config, 512, 336);
    expect(source.x == 256 && source.y == 0, "skin 7 should begin the right column");

    config.selected_skin_index = 12;
    source = OverworldTextboxRenderer::sourceRectForSkin(config, 512, 336);
    expect(source.x == 256 && source.y == 240, "skin 12 should be the last valid right-column skin");

    config.selected_skin_index = 13;
    source = OverworldTextboxRenderer::sourceRectForSkin(config, 512, 336);
    expect(source.x == 256 && source.y == 240, "skin 13 should clamp to 12 and avoid the empty bottom-right cell");
}

void testLayoutStretchesOnlyMiddleStrip() {
    using pr::gameplay::world3d::dialogue::OverworldTextboxConfig;
    using pr::gameplay::world3d::dialogue::OverworldTextboxRenderer;
    OverworldTextboxConfig config{};
    config.selected_skin_index = 0;
    config.valid_skin_count = 13;
    config.source_cell_width_px = 256;
    config.source_cell_height_px = 48;
    config.sheet_columns = 2;
    config.side_padding_px = 5;
    config.bottom_padding_px = 5;
    config.stretch_strip_width_px = 5;
    config.stretch_strip_center_x_px = 128;

    const auto layout = OverworldTextboxRenderer::buildLayout(config, 400, 250, 512, 336);
    expect(layout.visible, "textbox layout should be visible");
    expect(layout.left_src.w == 126, "left stylized source side should be preserved");
    expect(layout.middle_src.x == 126 && layout.middle_src.w == 5, "middle source strip should be the configured 5 px");
    expect(layout.right_src.x == 131 && layout.right_src.w == 125, "right stylized source side should be preserved");
    expect(layout.left_dst.w == layout.left_src.w, "left side should not stretch");
    expect(layout.right_dst.w == layout.right_src.w, "right side should not stretch");
    expect(layout.middle_dst.w == 139, "only the middle strip should stretch to fill viewport width");
    expect(layout.left_dst.x == 5, "textbox should use side padding");
    expect(layout.left_dst.y == 197, "textbox should use bottom padding");
    expect(layout.left_dst.h == 48 && layout.middle_dst.h == 48 && layout.right_dst.h == 48,
        "textbox height should remain the source cell height at DS/internal scale");
}

void testControllerTogglesActiveTarget() {
    using pr::gameplay::world3d::dialogue::OverworldTextboxController;
    OverworldTextboxController controller;
    expect(!controller.active(), "controller should start inactive");
    const bool opened = controller.toggleForTarget({
        OverworldTextboxController::TargetKind::NpcActor,
        "npc_test",
    });
    expect(opened && controller.active(), "toggle should open for a real target");
    expect(controller.activeTarget().id == "npc_test", "active target should be retained");
    const bool closed = controller.toggleForTarget({
        OverworldTextboxController::TargetKind::NpcActor,
        "npc_test",
    });
    expect(!closed && !controller.active(), "toggle should close when already active");
}

} // namespace

int main() {
    try {
        testConfigLoadsDedicatedTextboxJson();
        testSkinIndexUsesColumnFirstVisualOrder();
        testLayoutStretchesOnlyMiddleStrip();
        testControllerTogglesActiveTarget();
    } catch (const TestFailure& failure) {
        std::cerr << "[FAIL] " << failure.what() << '\n';
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << "[ERROR] " << ex.what() << '\n';
        return 1;
    }
    std::cout << "[PASS] overworld textbox tests\n";
    return 0;
}
