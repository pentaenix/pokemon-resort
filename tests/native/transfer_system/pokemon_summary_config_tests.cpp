#include "ui/transfer_system/summary/PokemonSummaryConfig.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

struct TempProject {
    fs::path root;
    fs::path config_dir;

    TempProject() {
        root = fs::temp_directory_path() / ("pokemon_resort_summary_config_" + std::to_string(::getpid()));
        fs::remove_all(root);
        config_dir = root / "config";
        fs::create_directories(config_dir);
    }

    ~TempProject() {
        std::error_code error;
        fs::remove_all(root, error);
    }
};

void writeText(const fs::path& path, const std::string& text) {
    std::ofstream out(path);
    out << text;
}

void testPokemonSummaryConfigParsesPanelShell() {
    TempProject temp;
    writeText(
        temp.config_dir / "design.json",
        R"json({"tokens":{"paper_1":"#f8f4e8","outline_0":"#c9be93","ink_0":"#625c2e"}})json");
    writeText(
        temp.config_dir / "pokemon_summary.json",
        R"json({
  "panel": {
    "enabled": true,
    "enter_smoothing": 21.5,
    "exit_smoothing": 12.25,
    "retracted_box_smoothing": 31.0,
    "width": 650,
    "height": 577,
    "top_y": 100,
    "corner_radius": 16,
    "border_thickness": 3,
    "fill_color": "$paper_1",
    "border_color": "$outline_0",
    "open_when_game_box_absent": false,
    "temporary_name_font_pt": 36,
    "temporary_name_color": "$ink_0"
  }
})json");

    const auto loaded = pr::transfer_system::loadPokemonSummary(temp.root.string());
    expect(loaded.panel.enabled, "summary panel enabled should parse");
    expect(loaded.panel.enter_smoothing == 21.5, "summary enter smoothing should parse");
    expect(loaded.panel.exit_smoothing == 12.25, "summary exit smoothing should parse");
    expect(loaded.panel.retracted_box_smoothing == 31.0, "summary retracted box smoothing should parse");
    expect(loaded.panel.width == 650, "summary width should parse");
    expect(loaded.panel.height == 577, "summary height should parse");
    expect(loaded.panel.top_y == 100, "summary top y should parse");
    expect(loaded.panel.corner_radius == 16, "summary corner radius should parse");
    expect(loaded.panel.border_thickness == 3, "summary border thickness should parse");
    expect(loaded.panel.fill_color.r == 0xf8, "summary fill token should parse");
    expect(loaded.panel.border_color.g == 0xbe, "summary border token should parse");
    expect(!loaded.panel.open_when_game_box_absent, "single-box open hint should parse");
    expect(loaded.panel.temporary_name_font_pt == 36, "temporary summary name font should parse");
    expect(loaded.panel.temporary_name_color.g == 0x5c, "temporary summary name color token should parse");
}

} // namespace

int main() {
    testPokemonSummaryConfigParsesPanelShell();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    return 0;
}
