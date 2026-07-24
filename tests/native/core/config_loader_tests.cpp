#include "core/config/ConfigLoader.hpp"
#include "core/save/SavePaths.hpp"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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
            fs::exists(current / "config" / "title_screen.json")) {
            return current;
        }
        current = current.parent_path();
    }
    throw TestFailure("Could not locate repository root from " + fs::current_path().string());
}

fs::path writeTempJson(const std::string& name, const std::string& contents) {
    const fs::path path = fs::temp_directory_path() / ("pokemon_resort_" + name + ".json");
    std::ofstream out(path);
    if (!out) {
        throw TestFailure("Could not open temp config for writing: " + path.string());
    }
    out << contents;
    return path;
}

void testCommittedAppConfigKeepsInputContract() {
    const fs::path path = repositoryRoot() / "config" / "app.json";
    const pr::AppConfig config = pr::loadAppConfigFromJson(path.string());

    expect(config.window.width == 640, "app.json window.width should keep the compact desktop preview");
    expect(config.window.virtual_width == 1280, "app.json virtual_width must remain the logical render width");
    expect(config.window.design_height == 800, "app.json design_height must remain 800 for UI hit tests");
    expect(config.input.accept_mouse, "app.json should enable mouse input unless intentionally disabled");
    expect(config.input.accept_controller, "app.json should enable controller input unless intentionally disabled");
    expect(config.input.navigate_up_keys == std::vector<std::string>({"UP", "W"}),
           "app.json navigate_up_keys changed; update input tests/docs if this is intentional");
    expect(config.input.forward_keys == std::vector<std::string>({"M", "RETURN", "SPACE"}),
           "app.json forward_keys changed; update input tests/docs if this is intentional");
    expect(config.input.back_keys == std::vector<std::string>({"N", "ESCAPE", "BACKSPACE"}),
           "app.json back_keys changed; update input tests/docs if this is intentional");
}

void testCommittedTitleConfigParsesWithExpectedMenuContract() {
    const fs::path path = repositoryRoot() / "config" / "title_screen.json";
    const pr::TitleScreenConfig config = pr::loadConfigFromJson(path.string());

    expect(config.window.virtual_width == 1280, "title_screen.json virtual_width must stay aligned with app config");
    expect(config.window.virtual_height == 800, "title_screen.json virtual_height must stay aligned with app config");
    expect(config.menu.items[0] == "RESORT", "title menu item 0 should remain RESORT unless menu action mapping changes");
    expect(config.menu.items[1] == "TRANSFER", "title menu item 1 should remain TRANSFER unless menu action mapping changes");
    expect(config.menu.items[2] == "TRADE", "title menu item 2 should remain TRADE unless menu action mapping changes");
    expect(config.menu.items[3] == "OPTIONS", "title menu item 3 should remain OPTIONS unless menu action mapping changes");
    expect(config.persistence.resort_profile_file_name == "profile.resort.db",
           "title_screen.json resort_profile_file_name should stay aligned with PokemonResortService defaults unless intentionally renamed");
}

void testPersistenceResortProfileFileNameOverrides() {
    const fs::path path = writeTempJson("resort_db_name", R"json({
        "persistence": {
            "resort_profile_file_name": "custom_resort.sqlite"
        }
    })json");

    const pr::TitleScreenConfig config = pr::loadConfigFromJson(path.string());
    fs::remove(path);

    expect(config.persistence.resort_profile_file_name == "custom_resort.sqlite",
           "resort_profile_file_name should be authorable for portable save layout");

    const fs::path resolved = pr::resortProfileDatabasePath(
        fs::path("/tmp/pr_save_probe"),
        config.persistence);
    expect(resolved.filename() == "custom_resort.sqlite",
           "Resort DB path should combine save directory with configured file name");
}

void testAppConfigCanOverrideEveryInputBindingVector() {
    const fs::path path = writeTempJson("custom_input_contract", R"json({
        "input": {
            "accept_any_key": false,
            "accept_mouse": false,
            "accept_controller": false,
            "navigate_up_keys": ["I"],
            "navigate_down_keys": ["K"],
            "navigate_left_keys": ["J"],
            "navigate_right_keys": ["L"],
            "forward_keys": ["P"],
            "back_keys": ["O"],
            "run_keys": ["B"],
            "run_toggle_keys": ["V"],
            "attend_keys": ["X"]
        }
    })json");

    const pr::AppConfig config = pr::loadAppConfigFromJson(path.string());
    fs::remove(path);

    expect(!config.input.accept_any_key, "custom input accept_any_key should load from JSON");
    expect(!config.input.accept_mouse, "custom input accept_mouse should load from JSON");
    expect(!config.input.accept_controller, "custom input accept_controller should load from JSON");
    expect(config.input.navigate_up_keys == std::vector<std::string>({"I"}), "navigate_up_keys should be fully data-driven");
    expect(config.input.navigate_down_keys == std::vector<std::string>({"K"}), "navigate_down_keys should be fully data-driven");
    expect(config.input.navigate_left_keys == std::vector<std::string>({"J"}), "navigate_left_keys should be fully data-driven");
    expect(config.input.navigate_right_keys == std::vector<std::string>({"L"}), "navigate_right_keys should be fully data-driven");
    expect(config.input.forward_keys == std::vector<std::string>({"P"}), "forward_keys should be fully data-driven");
    expect(config.input.back_keys == std::vector<std::string>({"O"}), "back_keys should be fully data-driven");
    expect(config.input.run_keys == std::vector<std::string>({"B"}), "run_keys should be fully data-driven");
    expect(config.input.run_toggle_keys == std::vector<std::string>({"V"}), "run_toggle_keys should be fully data-driven");
    expect(config.input.attend_keys == std::vector<std::string>({"X"}), "attend_keys should be fully data-driven");
}

void testInvalidInputVectorsFailWithActionName() {
    const fs::path path = writeTempJson("bad_input_contract", R"json({
        "input": {
            "navigate_up_keys": "UP"
        }
    })json");

    try {
        (void)pr::loadAppConfigFromJson(path.string());
        fs::remove(path);
        throw TestFailure("Expected invalid navigate_up_keys to fail");
    } catch (const std::runtime_error& ex) {
        fs::remove(path);
        const std::string message = ex.what();
        expect(message.find("input.navigate_up_keys") != std::string::npos,
               "invalid input vector error should name input.navigate_up_keys, got: " + message);
    }
}

} // namespace

int main() {
    const std::vector<std::pair<const char*, void (*)()>> tests{
        {"committed app config keeps input contract", testCommittedAppConfigKeepsInputContract},
        {"committed title config parses with expected menu contract", testCommittedTitleConfigParsesWithExpectedMenuContract},
        {"app config can override every input binding vector", testAppConfigCanOverrideEveryInputBindingVector},
        {"invalid input vectors fail with action name", testInvalidInputVectorsFailWithActionName},
        {"persistence resort_profile_file_name overrides", testPersistenceResortProfileFileNameOverrides},
    };

    int failures = 0;
    for (const auto& test : tests) {
        try {
            test.second();
            std::cout << "[PASS] " << test.first << '\n';
        } catch (const std::exception& ex) {
            ++failures;
            std::cerr << "[FAIL] " << test.first << ": " << ex.what() << '\n';
        }
    }

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
