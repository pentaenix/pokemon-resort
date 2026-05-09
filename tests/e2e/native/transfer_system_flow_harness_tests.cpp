#include "core/config/ConfigLoader.hpp"
#include "core/assets/PokeSpriteAssets.hpp"
#include "ui/TransferSystemScreen.hpp"

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
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
            fs::exists(current / "config" / "title_screen.json")) {
            return current;
        }
        current = current.parent_path();
    }
    throw TestFailure("Could not locate repository root from " + fs::current_path().string());
}

struct SdlHarness {
    SdlHarness() {
        setenv("SDL_VIDEODRIVER", "dummy", 1);
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            throw TestFailure(std::string("SDL_Init failed: ") + SDL_GetError());
        }
        if ((IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) == 0) {
            throw TestFailure(std::string("IMG_Init failed: ") + IMG_GetError());
        }
        if (TTF_Init() != 0) {
            throw TestFailure(std::string("TTF_Init failed: ") + TTF_GetError());
        }

        window.reset(SDL_CreateWindow("transfer-system-test", 0, 0, 1280, 800, SDL_WINDOW_HIDDEN));
        if (!window) {
            throw TestFailure(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
        }

        renderer.reset(SDL_CreateRenderer(window.get(), -1, SDL_RENDERER_SOFTWARE));
        if (!renderer) {
            throw TestFailure(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
        }
    }

    ~SdlHarness() {
        renderer.reset();
        window.reset();
        TTF_Quit();
        IMG_Quit();
        SDL_Quit();
    }

    struct WindowDeleter {
        void operator()(SDL_Window* value) const {
            if (value) {
                SDL_DestroyWindow(value);
            }
        }
    };

    struct RendererDeleter {
        void operator()(SDL_Renderer* value) const {
            if (value) {
                SDL_DestroyRenderer(value);
            }
        }
    };

    std::unique_ptr<SDL_Window, WindowDeleter> window;
    std::unique_ptr<SDL_Renderer, RendererDeleter> renderer;
};

pr::TransferSaveSelection makeSelection() {
    pr::TransferSaveSelection selection;
    selection.game_key = "diamond";
    selection.game_title = "Pokemon Diamond";
    selection.trainer_name = "Test";
    selection.time = "0:00";
    selection.pokedex = "0/0";
    selection.badges = "0";

    for (int i = 0; i < 31; ++i) {
        pr::TransferSaveSelection::PcBox box;
        box.name = (i == 0) ? "ボックス？1" : "BOX " + std::to_string(i + 1);
        box.slots.resize(30);
        selection.pc_boxes.push_back(std::move(box));
    }
    selection.pc_boxes[0].slots[0].present = true;
    selection.pc_boxes[0].slots[0].slug = "piplup";
    selection.pc_boxes[0].slots[0].species_name = "Piplup";
    selection.pc_boxes[0].slots[0].nickname = "Buddy";
    selection.pc_boxes[0].slots[0].ball_id = 4;
    selection.pc_boxes[0].slots[0].ot_name = "Dawn";
    selection.pc_boxes[0].slots[0].nature = "Calm";
    selection.pc_boxes[0].slots[0].ability_id = 67;
    selection.pc_boxes[0].slots[0].ability_name = "Torrent";
    selection.pc_boxes[0].slots[0].primary_type = "Water";
    selection.pc_boxes[0].slots[0].pokerus_status = "infected";
    selection.pc_boxes[0].slots[0].markings = 1;
    selection.pc_boxes[0].slots[0].species_id = 393;
    selection.pc_boxes[0].slots[0].level = 5;
    selection.pc_boxes[0].slots[0].source_game_id = 21;
    selection.pc_boxes[0].slots[0].source_game_key = "pokemon_black";
    selection.pc_boxes[0].slots[0].source_save_trainer_name = selection.trainer_name;
    selection.pc_boxes[0].slots[0].source_save_play_time = selection.time;
    selection.pc_boxes[0].slots[0].source_save_badges = selection.badges;
    selection.pc_boxes[0].slots[0].held_item_id = 234;
    selection.pc_boxes[0].slots[0].held_item_name = "Leftovers";
    selection.pc_boxes[0].slots[1].present = true;
    selection.pc_boxes[0].slots[1].slug = "starly";
    selection.pc_boxes[0].slots[1].species_name = "Starly";
    selection.pc_boxes[0].slots[1].nickname = "Scout";
    selection.pc_boxes[0].slots[1].species_id = 396;
    selection.pc_boxes[0].slots[1].level = 4;

    selection.pc_boxes[0].slots[3].present = true;
    selection.pc_boxes[0].slots[3].slug = "shinx";
    selection.pc_boxes[0].slots[3].species_name = "Shinx";
    selection.pc_boxes[0].slots[3].nickname = "Spark";
    selection.pc_boxes[0].slots[3].species_id = 403;
    selection.pc_boxes[0].slots[3].level = 4;
    selection.pc_boxes[0].slots[3].held_item_id = 213;
    selection.pc_boxes[0].slots[3].held_item_name = "Sitrus Berry";
    selection.box1_slots.resize(30);
    return selection;
}

pr::TransferSaveSelection makeGen12Selection() {
    pr::TransferSaveSelection selection;
    selection.game_key = "pokemon_red";
    selection.game_title = "Pokemon Red";
    selection.trainer_name = "Test";
    selection.time = "0:00";
    selection.pokedex = "0/0";
    selection.badges = "0";

    for (int i = 0; i < 2; ++i) {
        pr::TransferSaveSelection::PcBox box;
        box.name = "BOX " + std::to_string(i + 1);
        box.native_slot_count = 20;
        box.slots.resize(20);
        selection.pc_boxes.push_back(std::move(box));
    }

    selection.pc_boxes[0].slots[17].present = true;
    selection.pc_boxes[0].slots[17].slug = "pikachu";
    selection.pc_boxes[0].slots[17].species_name = "Pikachu";
    selection.pc_boxes[0].slots[17].nickname = "A";
    selection.pc_boxes[0].slots[17].species_id = 25;
    selection.pc_boxes[0].slots[18].present = true;
    selection.pc_boxes[0].slots[18].slug = "bulbasaur";
    selection.pc_boxes[0].slots[18].species_name = "Bulbasaur";
    selection.pc_boxes[0].slots[18].nickname = "B";
    selection.pc_boxes[0].slots[18].species_id = 1;
    selection.pc_boxes[0].slots[19].present = true;
    selection.pc_boxes[0].slots[19].slug = "charmander";
    selection.pc_boxes[0].slots[19].species_name = "Charmander";
    selection.pc_boxes[0].slots[19].nickname = "C";
    selection.pc_boxes[0].slots[19].species_id = 4;
    return selection;
}

class TransferSystemHarness {
public:
    explicit TransferSystemHarness(pr::TransferSaveSelection selection = makeSelection())
        : repo_root_(repositoryRoot()),
          project_root_(repo_root_.string()),
          app_config_(pr::loadAppConfigFromJson((repo_root_ / "config" / "app.json").string())),
          title_config_(pr::loadConfigFromJson((repo_root_ / "config" / "title_screen.json").string())),
          sprite_assets_(pr::PokeSpriteAssets::create(project_root_)),
          screen_(
              sdl_.renderer.get(),
              app_config_.window,
              title_config_.assets.font,
              project_root_,
              sprite_assets_,
              (repo_root_ / ".test_save").string(),
              nullptr) {
        screen_.enter(selection, sdl_.renderer.get(), 0);
        screen_.update(1.0);
    }

    pr::TransferSystemScreen& screen() { return screen_; }

    void navigate(int dx, int dy) {
        screen_.onNavigate2d(dx, dy);
    }

    void advance() {
        screen_.onAdvancePressed();
    }

    void back() {
        screen_.onBackPressed();
    }

    void render() {
        screen_.render(sdl_.renderer.get());
    }

    void update(double dt) {
        screen_.update(dt);
    }

    void movePointer(int x, int y) {
        screen_.handlePointerMoved(x, y);
    }

    void clickPointer(int x, int y) {
        expect(screen_.handlePointerPressed(x, y), "expected pointer press to hit an interactive transfer-system target");
        expect(screen_.handlePointerReleased(x, y), "expected pointer release to complete the transfer-system click");
    }

    void pressPointer(int x, int y) {
        expect(screen_.handlePointerPressed(x, y), "expected pointer press to hit an interactive transfer-system target");
    }

    void releasePointer(int x, int y) {
        expect(screen_.handlePointerReleased(x, y), "expected pointer release to complete the transfer-system pointer action");
    }

    void cycleToMultiTool() {
        constexpr int carousel_left_half_x = 50 + 76 + 8 + 40;
        constexpr int carousel_y = 12 + 38;
        pressPointer(carousel_left_half_x, carousel_y);
        update(1.0);
    }

    void cycleToItemTool() {
        constexpr int carousel_right_half_x = 50 + 76 + 8 + 190;
        constexpr int carousel_y = 12 + 38;
        pressPointer(carousel_right_half_x, carousel_y);
        update(1.0);
        pressPointer(carousel_right_half_x, carousel_y);
        update(1.0);
    }

    void cycleToSwapTool() {
        constexpr int carousel_right_half_x = 50 + 76 + 8 + 190;
        constexpr int carousel_y = 12 + 38;
        pressPointer(carousel_right_half_x, carousel_y);
        update(1.0);
    }

    void moveFocusToGameBoxSpaceButton() {
        for (int i = 0; i < 6; ++i) {
            navigate(1, 0);
        }
        for (int i = 0; i < 5; ++i) {
            navigate(0, 1);
        }
    }

    void moveFocusToGameBoxNamePlate() {
        for (int i = 0; i < 6; ++i) {
            navigate(1, 0);
        }
        navigate(1, 0);
        navigate(1, 0);
        navigate(0, -1);
    }

private:
    SdlHarness sdl_;
    fs::path repo_root_;
    std::string project_root_;
    pr::AppConfig app_config_;
    pr::TitleScreenConfig title_config_;
    std::shared_ptr<pr::PokeSpriteAssets> sprite_assets_;
    pr::TransferSystemScreen screen_;
};

void testKeyboardAdvanceOpensFocusedBoxFromBoxSpaceMode() {
    TransferSystemHarness harness;

    harness.moveFocusToGameBoxSpaceButton();
    expect(harness.screen().debugFocusedNode() == 2110, "expected keyboard navigation to reach the game Box Space button");

    harness.advance();
    expect(harness.screen().debugGameBoxSpaceMode(), "advancing on the Box Space button should enter Box Space mode");

    harness.navigate(0, -1);
    expect(harness.screen().debugFocusedNode() == 2024, "up from Box Space button should focus the bottom-left game cell");

    harness.navigate(0, 1);
    expect(harness.screen().debugGameBoxSpaceRowOffset() == 1,
           "down on a bottom-row cell in Box Space mode should scroll to the next row group");
    expect(harness.screen().debugFocusedNode() == 2024,
           "scrolling Box Space rows should keep keyboard focus on the same cell");

    harness.advance();
    expect(!harness.screen().debugGameBoxSpaceMode(),
           "advancing on a Box Space cell should open that box and leave Box Space mode");
    expect(harness.screen().currentGameBoxIndex() == 30,
           "advancing on the focused bottom-left Box Space cell after one row scroll should open box index 30");
    expect(harness.screen().consumeButtonSfxRequest(),
           "opening a box from Box Space mode should still request button SFX");
}

void testBoxSpaceAdvanceOnTopLeftCellOpensThatBoxNotPokemonMenu() {
    TransferSystemHarness harness;

    harness.moveFocusToGameBoxSpaceButton();
    harness.advance();
    expect(harness.screen().debugGameBoxSpaceMode(), "should enter Box Space mode");

    harness.navigate(0, -1);
    expect(harness.screen().debugFocusedNode() == 2024, "expected bottom-left Box Space cell after moving up from footer");
    for (int i = 0; i < 4; ++i) {
        harness.navigate(0, -1);
    }
    expect(harness.screen().debugFocusedNode() == 2000, "expected top-left Box Space cell");

    harness.advance();
    expect(!harness.screen().debugGameBoxSpaceMode(), "accept on top-left tile should leave Box Space");
    expect(!harness.screen().debugPokemonActionMenuVisible(),
           "Box Space tiles must not trigger the normal-slot Pokemon action menu");
    expect(harness.screen().currentGameBoxIndex() == 0,
           "top-left Box Space tile should open game box index 0");
}

void testKeyboardFocusShowsSpeechBubbleForOccupiedGameSlot() {
    TransferSystemHarness harness;

    expect(!harness.screen().debugShouldDrawSpeechBubble(),
           "initial empty resort focus should not show a speech bubble");
    expect(harness.screen().debugInfoBannerVisible(),
           "info banner chrome should remain visible on the transfer screen");
    expect(harness.screen().debugInfoBannerMode() == "empty",
           "initial empty resort focus should use the placeholder info banner state");

    for (int i = 0; i < 6; ++i) {
        harness.navigate(1, 0);
    }

    expect(harness.screen().debugFocusedNode() == 2000,
           "expected keyboard navigation to reach the top-left game slot");
    expect(harness.screen().debugShouldDrawSpeechBubble(),
           "keyboard focus on an occupied game slot should show the speech bubble");
    expect(harness.screen().debugInfoBannerVisible(),
           "keyboard focus on an occupied game slot should show info banner content");
    expect(harness.screen().debugInfoBannerPokemonName() == "Buddy",
           "info banner should expose the focused Pokemon nickname");
}

void testKeyboardFocusShowsSpeechBubbleForGameIconAfterNonBubbleUi() {
    TransferSystemHarness harness;

    harness.moveFocusToGameBoxSpaceButton();
    expect(harness.screen().debugFocusedNode() == 2110,
           "expected keyboard navigation to reach the game Box Space button");
    expect(!harness.screen().debugShouldDrawSpeechBubble(),
           "game Box Space button should not show a speech bubble");

    harness.navigate(0, -1);
    for (int i = 0; i < 5; ++i) {
        harness.navigate(1, 0);
    }
    harness.navigate(0, 1);

    expect(harness.screen().debugFocusedNode() == 2111,
           "expected keyboard navigation from non-bubble game controls to reach the game icon");
    expect(harness.screen().debugShouldDrawSpeechBubble(),
           "keyboard focus on the game icon should show the speech bubble");
    expect(harness.screen().debugInfoBannerVisible(),
           "game icon focus should still use the bottom banner");
    expect(harness.screen().debugInfoBannerMode() == "game_icon",
           "game icon focus should switch the banner into game summary mode");
}

void testMouseHoverShowsAndClearsInfoBannerForPokemonSlots() {
    TransferSystemHarness harness;

    const auto slot_bounds = harness.screen().debugGameSlotBounds(0);
    expect(slot_bounds.has_value(),
           "game slot bounds should be available for mouse info banner testing");

    harness.movePointer(slot_bounds->x + slot_bounds->w / 2, slot_bounds->y + slot_bounds->h / 2);
    expect(harness.screen().debugInfoBannerVisible(),
           "mouse hover on a populated Pokemon slot should show info banner content");
    expect(harness.screen().debugInfoBannerMode() == "pokemon",
           "mouse hover on a populated Pokemon slot should use Pokemon banner content");

    harness.movePointer(10, 10);
    expect(harness.screen().debugInfoBannerVisible(),
           "moving the mouse off Pokemon slots should keep the banner visible");
    expect(harness.screen().debugInfoBannerMode() == "empty",
           "moving the mouse off Pokemon slots should return the banner to placeholder mode");
}

void testMouseHoverShowsInfoBannerForResortSlotsAndKeepsStoredSourceGame() {
    TransferSystemHarness harness;

    const auto game_slot = harness.screen().debugGameSlotBounds(0);
    const auto resort_slot = harness.screen().debugResortSlotBounds(0);
    expect(game_slot.has_value() && resort_slot.has_value(),
           "game and resort slot bounds should be available for Resort hover regression testing");

    harness.pressPointer(game_slot->x + game_slot->w / 2, game_slot->y + game_slot->h / 2);
    harness.movePointer(game_slot->x + game_slot->w + 30, game_slot->y + game_slot->h / 2);
    harness.update(0.1);
    expect(harness.screen().debugPokemonMoveActive(),
           "dragging out of the game slot should pick up the Pokemon before dropping into Resort");
    expect(harness.screen().handlePointerPressed(resort_slot->x + resort_slot->w / 2, resort_slot->y + resort_slot->h / 2),
           "pressing the Resort slot while holding should drop the Pokemon there");

    expect(harness.screen().debugResortSlotPokemonName(0) == "Buddy",
           "Resort slot should receive the moved Pokemon");

    harness.movePointer(resort_slot->x + resort_slot->w / 2, resort_slot->y + resort_slot->h / 2);
    expect(harness.screen().debugInfoBannerMode() == "pokemon",
           "mouse hover on an occupied Resort slot should use Pokemon banner content");
    expect(harness.screen().debugInfoBannerPokemonName() == "Buddy",
           "Resort hover info banner should expose the hovered Pokemon");
    expect(harness.screen().debugInfoBannerFieldText("origin_region") == "Unova (B)",
           "Resort hover info banner should use the Pokemon's stored source game, not the active save");
}

void testExitSaveModalSuppressesUnderlyingPokemonHoverBubble() {
    TransferSystemHarness harness;

    const auto game_slot = harness.screen().debugGameSlotBounds(0);
    const auto resort_slot = harness.screen().debugResortSlotBounds(0);
    expect(game_slot.has_value() && resort_slot.has_value(),
           "slot bounds should be available for save-exit modal hover regression testing");

    harness.pressPointer(game_slot->x + game_slot->w / 2, game_slot->y + game_slot->h / 2);
    harness.movePointer(game_slot->x + game_slot->w + 30, game_slot->y + game_slot->h / 2);
    harness.update(0.1);
    expect(harness.screen().handlePointerPressed(resort_slot->x + resort_slot->w / 2, resort_slot->y + resort_slot->h / 2),
           "pressing the Resort slot while holding should drop the Pokemon there before opening save-exit modal");

    harness.back();
    harness.update(1.0);
    harness.movePointer(resort_slot->x + resort_slot->w / 2, resort_slot->y + resort_slot->h / 2);

    expect(harness.screen().debugInfoBannerMode() == "exit",
           "save-exit modal should keep the info banner in exit prompt mode");
    expect(!harness.screen().debugShouldDrawSpeechBubble(),
           "save-exit modal should suppress Pokemon hover speech bubbles under the modal");
}

void testMouseHoverShowsPillLegendWhenItemPanelsAreHidden() {
    TransferSystemHarness harness;

    const auto pill_bounds = harness.screen().debugPillTrackBounds();
    expect(pill_bounds.has_value(), "pill track should expose debug bounds for hover testing");

    expect(
        harness.screen().handlePointerPressed(pill_bounds->x + pill_bounds->w / 2, pill_bounds->y + pill_bounds->h / 2),
        "expected pointer press on the pill to toggle item mode");
    harness.update(1.0);
    harness.movePointer(pill_bounds->x + pill_bounds->w / 2, pill_bounds->y + pill_bounds->h / 2);

    expect(harness.screen().debugInfoBannerMode() == "pill",
           "hovering the pill should keep the legend visible even after item mode hides the Pokemon panels");
}

void testItemToolHoverOnlyShowsHeldItemBubbles() {
    TransferSystemHarness harness;
    harness.cycleToItemTool();

    const auto item_slot_bounds = harness.screen().debugGameSlotBounds(0);
    const auto empty_item_slot_bounds = harness.screen().debugGameSlotBounds(1);
    expect(item_slot_bounds.has_value(), "game slot with held item should expose bounds");
    expect(empty_item_slot_bounds.has_value(), "game slot without held item should expose bounds");

    harness.movePointer(item_slot_bounds->x + item_slot_bounds->w / 2, item_slot_bounds->y + item_slot_bounds->h / 2);
    expect(harness.screen().debugShouldDrawSpeechBubble(),
           "item tool hover should show a speech bubble for Pokemon holding an item");
    expect(harness.screen().debugSpeechBubbleLineForFocus(2000) == "Leftovers",
           "item tool speech bubble should show the held item name");

    harness.movePointer(
        empty_item_slot_bounds->x + empty_item_slot_bounds->w / 2,
        empty_item_slot_bounds->y + empty_item_slot_bounds->h / 2);
    expect(!harness.screen().debugShouldDrawSpeechBubble(),
           "item tool hover should not show a speech bubble for Pokemon without a held item");
}

void testItemToolMoveItemMovesHeldItemBetweenPokemon() {
    TransferSystemHarness harness;
    harness.cycleToItemTool();

    expect(harness.screen().debugSpeechBubbleLineForFocus(2000) == "Leftovers",
           "precondition: slot 0 should show its held item in item tool mode");

    // Click slot 0 to open the item modal, then choose "Move Item" (row 0).
    const auto source_bounds = harness.screen().debugGameSlotBounds(0);
    expect(source_bounds.has_value(), "expected debug bounds for slot 0");
    harness.clickPointer(
        source_bounds->x + source_bounds->w / 2,
        source_bounds->y + source_bounds->h / 2);
    harness.advance();

    // Drop onto the next slot (slot 1 is occupied and has no held item in harness data).
    const auto target_bounds = harness.screen().debugGameSlotBounds(1);
    expect(target_bounds.has_value(), "expected debug bounds for slot 1");
    harness.clickPointer(
        target_bounds->x + target_bounds->w / 2,
        target_bounds->y + target_bounds->h / 2);

    expect(harness.screen().debugSpeechBubbleLineForFocus(2001) == "Leftovers",
           "after moving, slot 1 should show the moved held item");
    expect(harness.screen().debugSpeechBubbleLineForFocus(2000).empty(),
           "after moving, the source slot should no longer have a held item");
}

void testItemToolSwapAndCancelReturnsHeldItemToSwapTarget() {
    TransferSystemHarness harness;
    harness.cycleToItemTool();

    // Preconditions: slot 0 has Leftovers, slot 3 has Sitrus Berry.
    expect(harness.screen().debugSpeechBubbleLineForFocus(2000) == "Leftovers",
           "precondition: slot 0 should have Leftovers");
    expect(harness.screen().debugSpeechBubbleLineForFocus(2003) == "Sitrus Berry",
           "precondition: slot 3 should have Sitrus Berry");

    // Pick up item from slot 0 via modal.
    const auto source_bounds = harness.screen().debugGameSlotBounds(0);
    expect(source_bounds.has_value(), "expected debug bounds for slot 0");
    harness.clickPointer(source_bounds->x + source_bounds->w / 2, source_bounds->y + source_bounds->h / 2);
    harness.advance(); // Move Item

    // Drop onto slot 3 which already has an item: should swap and keep the target item in hand.
    const auto target_bounds = harness.screen().debugGameSlotBounds(3);
    expect(target_bounds.has_value(), "expected debug bounds for slot 3");
    harness.clickPointer(target_bounds->x + target_bounds->w / 2, target_bounds->y + target_bounds->h / 2);

    expect(harness.screen().debugSpeechBubbleLineForFocus(2003) == "Leftovers",
           "after swap drop, slot 3 should now have Leftovers");
    expect(harness.screen().debugHeldItemName() == "Sitrus Berry",
           "after swap drop, held item should become the swapped-out target item");

    // Cancel should return the held item to the slot it came from (slot 2), not the original source (slot 0).
    harness.back();
    expect(harness.screen().debugSpeechBubbleLineForFocus(2003) == "Leftovers",
           "after cancel, the swapped-in item should remain on the target slot");
    expect(harness.screen().debugSpeechBubbleLineForFocus(2000) == "Sitrus Berry",
           "after cancel, the currently-held item should go back to the original pickup slot");
}

void testNormalToolClickOpensPokemonActionMenuBesideGameSlot() {
    TransferSystemHarness harness;

    const auto slot_bounds = harness.screen().debugGameSlotBounds(0);
    expect(slot_bounds.has_value(), "occupied game slot bounds should be available for action menu testing");

    harness.clickPointer(slot_bounds->x + slot_bounds->w / 2, slot_bounds->y + slot_bounds->h / 2);
    harness.update(1.0);

    expect(harness.screen().debugPokemonActionMenuVisible(),
           "clicking an occupied Pokemon slot with the normal tool should open the action menu");
    expect(!harness.screen().debugShouldDrawSpeechBubble(),
           "speech bubbles should be suppressed while the Pokemon action menu is open");
    expect(harness.screen().debugInfoBannerPokemonName() == "Buddy",
           "opening the Pokemon action menu should keep the info banner on the selected Pokemon");
    expect(harness.screen().debugPokemonActionMenuFromGameBox(),
           "action menu opened from a game slot should remember the game side");
    const auto menu_rect = harness.screen().debugPokemonActionMenuRect();
    expect(menu_rect.has_value(), "open action menu should expose a final rect for placement checks");
    expect(menu_rect->x + menu_rect->w <= slot_bounds->x,
           "game-slot action menu should appear to the left of the clicked Pokemon");
    expect(menu_rect->y + menu_rect->h <= 681,
           "Pokemon action menu should stay at least five pixels above the info banner");

    harness.movePointer(menu_rect->x + menu_rect->w / 2, menu_rect->y + 12 + 54 * 2 + 20);
    expect(harness.screen().debugPokemonActionMenuSelectedRow() == 2,
           "hovering an action menu row should update the selected row");

    harness.pressPointer(menu_rect->x + menu_rect->w / 2, menu_rect->y + 12 + 54 * 2 + 20);
    harness.update(1.0);
    expect(!harness.screen().debugPokemonActionMenuVisible(),
           "clicking any visible action should shrink and close the action menu");
    expect(!harness.screen().debugPokemonMoveActive(),
           "clicking a non-Move action should not pick up the Pokemon");
}

void testNormalToolDragPickupsPokemonWithoutOpeningActionMenu() {
    TransferSystemHarness harness;

    const auto slot_bounds = harness.screen().debugGameSlotBounds(0);
    expect(slot_bounds.has_value(), "occupied game slot bounds should be available for drag pickup test");

    // Click/drag in mouse mode: press inside slot then move outside.
    harness.pressPointer(slot_bounds->x + slot_bounds->w / 2, slot_bounds->y + slot_bounds->h / 2);
    harness.movePointer(slot_bounds->x + slot_bounds->w + 30, slot_bounds->y + slot_bounds->h / 2);
    harness.update(0.1);

    expect(harness.screen().debugPokemonMoveActive(), "dragging out of a slot in mouse mode should pick up the Pokemon");
    expect(!harness.screen().debugPokemonActionMenuVisible(), "drag pickup should not open the Pokemon action menu");
}

void testNormalToolAcceptAndKeyboardNavigationUsePokemonActionMenu() {
    TransferSystemHarness harness;

    for (int i = 0; i < 6; ++i) {
        harness.navigate(1, 0);
    }
    expect(harness.screen().debugFocusedNode() == 2000,
           "expected keyboard navigation to reach the occupied top-left game slot");

    harness.advance();
    harness.update(1.0);
    expect(harness.screen().debugPokemonActionMenuVisible(),
           "accept on an occupied Pokemon slot with the normal tool should open the action menu");
    expect(harness.screen().debugPokemonActionMenuSelectedRow() == 0,
           "newly opened Pokemon action menu should select Move first");

    harness.navigate(0, 1);
    expect(harness.screen().debugPokemonActionMenuSelectedRow() == 1,
           "down should select the next Pokemon action menu row");
    harness.navigate(0, -1);
    expect(harness.screen().debugPokemonActionMenuSelectedRow() == 0,
           "up should select the previous Pokemon action menu row");

    harness.advance();
    harness.update(1.0);
    expect(!harness.screen().debugPokemonActionMenuVisible(),
           "accept on Move should close the action menu");
    expect(harness.screen().debugPokemonMoveActive(),
           "accept on Move should pick up the focused Pokemon");
    expect(harness.screen().debugHeldPokemonName() == "Buddy",
           "the held Pokemon should be the one selected from the action menu");
    expect(!harness.screen().debugShouldDrawSpeechBubble(),
           "speech bubbles should stay suppressed while moving a Pokemon");
    expect(harness.screen().debugInfoBannerPokemonName() == "Buddy",
           "the info banner should follow the Pokemon being moved");

    harness.navigate(1, 0);
    harness.navigate(1, 0);
    expect(harness.screen().debugFocusedNode() == 2002,
           "keyboard movement should move the held Pokemon over the yellow focus cursor");
    harness.advance();
    expect(!harness.screen().debugPokemonMoveActive(),
           "accept on an empty focused slot should put down the held Pokemon");
    expect(harness.screen().debugFocusedNode() == 2002,
           "after dropping a Pokemon, keyboard focus should move to the destination slot for correct speech bubble placement");
    expect(harness.screen().debugGameSlotPokemonName(0).empty(),
           "the original slot should be empty after moving the Pokemon");
    expect(harness.screen().debugGameSlotPokemonName(2) == "Buddy",
           "the destination slot should receive the moved Pokemon");
}

void testSwapToolPointerSwapKeepsTargetInHandAndBackReturnsIt() {
    TransferSystemHarness harness;
    harness.cycleToSwapTool();

    const auto source_bounds = harness.screen().debugGameSlotBounds(0);
    const auto target_bounds = harness.screen().debugGameSlotBounds(1);
    expect(source_bounds.has_value(), "swap source slot should expose bounds");
    expect(target_bounds.has_value(), "swap target slot should expose bounds");

    harness.pressPointer(source_bounds->x + source_bounds->w / 2, source_bounds->y + source_bounds->h / 2);
    expect(harness.screen().debugPokemonMoveActive(),
           "swap tool pointer press on an occupied slot should pick up the Pokemon immediately");
    expect(harness.screen().debugHeldPokemonName() == "Buddy",
           "swap tool should hold the picked Pokemon after click pickup");

    harness.pressPointer(target_bounds->x + target_bounds->w / 2, target_bounds->y + target_bounds->h / 2);
    expect(harness.screen().debugPokemonMoveActive(),
           "clicking an occupied slot while holding should perform a hand swap");
    expect(harness.screen().debugPokemonMoveActive(),
           "swap-in-hand policy should keep the target Pokemon in hand after dropping on an occupied slot");
    expect(harness.screen().debugGameSlotPokemonName(1) == "Buddy",
           "the originally held Pokemon should move into the occupied target slot");
    expect(harness.screen().debugHeldPokemonName() == "Scout",
           "the target Pokemon should now be held");

    harness.screen().onBackPressed();
    expect(!harness.screen().debugPokemonMoveActive(),
           "back should return the currently held Pokemon to its configured return slot");
    expect(harness.screen().debugGameSlotPokemonName(0) == "Scout",
           "after cancelling a hand swap, the currently held Pokemon should return to the original source slot");
}

void testMultiToolDragSelectsAndMovesPokemonAsLayout() {
    TransferSystemHarness harness;
    harness.cycleToMultiTool();

    const auto source0 = harness.screen().debugGameSlotBounds(0);
    const auto source1 = harness.screen().debugGameSlotBounds(1);
    expect(source0.has_value() && source1.has_value(), "multi tool source slots should expose bounds");

    harness.pressPointer(source0->x + 4, source0->y + 4);
    harness.movePointer(source1->x + source1->w - 4, source1->y + source1->h - 4);
    harness.render();
    expect(harness.screen().debugMultiSelectionRect().has_value(),
           "dragging with the multi tool should draw a selection rectangle");
    harness.releasePointer(source1->x + source1->w - 4, source1->y + source1->h - 4);

    expect(harness.screen().debugMultiPokemonMoveActive(),
           "releasing a multi selection over occupied slots should pick up the selected Pokemon group");
    expect(harness.screen().debugHeldMultiPokemonCount() == 2,
           "multi selection should hold both selected Pokemon");
    expect(harness.screen().debugGameSlotPokemonName(0).empty(),
           "multi pickup should clear the first source slot while holding the group");
    expect(harness.screen().debugGameSlotPokemonName(1).empty(),
           "multi pickup should clear the second source slot while holding the group");

    // Drop the 2-wide selection into Resort slots 0 and 1, preserving layout.
    const auto resort0 = harness.screen().debugResortSlotBounds(0);
    expect(resort0.has_value(), "resort target slot should expose bounds");
    harness.pressPointer(resort0->x + resort0->w / 2, resort0->y + resort0->h / 2);

    expect(!harness.screen().debugMultiPokemonMoveActive(),
           "dropping a fitting multi group should clear the held group");
    expect(harness.screen().debugResortSlotPokemonName(0) == "Buddy",
           "multi drop should place the first Pokemon at the anchor slot");
    expect(harness.screen().debugResortSlotPokemonName(1) == "Scout",
           "multi drop should preserve the selected horizontal layout");
}

void testMultiToolPreservesGen12ThreeWideLayoutAtRightEdge() {
    TransferSystemHarness harness(makeGen12Selection());
    harness.cycleToMultiTool();

    const auto source17 = harness.screen().debugGameSlotBounds(17);
    const auto source19 = harness.screen().debugGameSlotBounds(19);
    expect(source17.has_value() && source19.has_value(), "Gen 1/2 edge slots should expose bounds");

    harness.pressPointer(source17->x + 4, source17->y + 4);
    harness.movePointer(source19->x + source19->w - 4, source19->y + source19->h - 4);
    harness.releasePointer(source19->x + source19->w - 4, source19->y + source19->h - 4);

    expect(harness.screen().debugMultiPokemonMoveActive(),
           "selecting three right-edge Gen 1/2 slots should pick up the multi group");
    expect(harness.screen().debugHeldMultiPokemonCount() == 3,
           "Gen 1/2 multi selection should keep the three-slot row together");

    harness.pressPointer(source17->x + source17->w / 2, source17->y + source17->h / 2);
    expect(!harness.screen().debugMultiPokemonMoveActive(),
           "dropping the Gen 1/2 edge group back onto an empty 20-slot grid should succeed");
    expect(harness.screen().debugGameSlotPokemonName(17) == "A",
           "Gen 1/2 multi drop should anchor at the first selected slot");
    expect(harness.screen().debugGameSlotPokemonName(18) == "B",
           "Gen 1/2 multi drop should preserve horizontal order");
    expect(harness.screen().debugGameSlotPokemonName(19) == "C",
           "Gen 1/2 multi drop should keep the rightmost slot on the same row");
}

void testMultiToolCanReturnResortGroupToGen12GameBox() {
    TransferSystemHarness harness(makeGen12Selection());
    harness.cycleToMultiTool();

    const auto source17 = harness.screen().debugGameSlotBounds(17);
    const auto source19 = harness.screen().debugGameSlotBounds(19);
    expect(source17.has_value() && source19.has_value(), "Gen 1/2 source slots should expose bounds");

    harness.pressPointer(source17->x + 4, source17->y + 4);
    harness.movePointer(source19->x + source19->w - 4, source19->y + source19->h - 4);
    harness.releasePointer(source19->x + source19->w - 4, source19->y + source19->h - 4);

    const auto resort0 = harness.screen().debugResortSlotBounds(0);
    expect(resort0.has_value(), "Resort target slot should expose bounds");
    harness.pressPointer(resort0->x + resort0->w / 2, resort0->y + resort0->h / 2);
    expect(!harness.screen().debugMultiPokemonMoveActive(),
           "dropping the Gen 1/2 group into Resort should clear the held multi group");
    expect(harness.screen().debugResortSlotPokemonName(0) == "A",
           "Resort should receive the first Pokemon at the anchor slot");
    expect(harness.screen().debugResortSlotPokemonName(1) == "B",
           "Resort should preserve the second Pokemon next to the anchor");
    expect(harness.screen().debugResortSlotPokemonName(2) == "C",
           "Resort should preserve the third Pokemon on the same row");

    const auto resortSource0 = harness.screen().debugResortSlotBounds(0);
    const auto resortSource2 = harness.screen().debugResortSlotBounds(2);
    expect(resortSource0.has_value() && resortSource2.has_value(), "Resort source slots should expose bounds");
    harness.pressPointer(resortSource0->x + 4, resortSource0->y + 4);
    harness.movePointer(resortSource2->x + resortSource2->w - 4, resortSource2->y + resortSource2->h - 4);
    harness.releasePointer(resortSource2->x + resortSource2->w - 4, resortSource2->y + resortSource2->h - 4);
    expect(harness.screen().debugHeldMultiPokemonCount() == 3,
           "selecting the Resort group should pick up all three Pokemon");

    const auto gameTarget = harness.screen().debugGameSlotBounds(17);
    expect(gameTarget.has_value(), "Gen 1/2 game target should expose bounds");
    harness.pressPointer(gameTarget->x + gameTarget->w / 2, gameTarget->y + gameTarget->h / 2);

    expect(!harness.screen().debugMultiPokemonMoveActive(),
           "dropping the Resort group back into the Gen 1/2 box should succeed");
    expect(harness.screen().debugGameSlotPokemonName(17) == "A",
           "Gen 1/2 return drop should anchor at the requested slot");
    expect(harness.screen().debugGameSlotPokemonName(18) == "B",
           "Gen 1/2 return drop should preserve the middle Pokemon");
    expect(harness.screen().debugGameSlotPokemonName(19) == "C",
           "Gen 1/2 return drop should preserve the rightmost Pokemon");
}

void testMultiToolRejectsNonFittingPatternAndCancelRestoresSources() {
    TransferSystemHarness harness;
    harness.cycleToMultiTool();

    const auto source0 = harness.screen().debugGameSlotBounds(0);
    const auto source1 = harness.screen().debugGameSlotBounds(1);
    expect(source0.has_value() && source1.has_value(), "multi source slots should expose bounds");

    harness.pressPointer(source0->x + 4, source0->y + 4);
    harness.movePointer(source1->x + source1->w - 4, source1->y + source1->h - 4);
    harness.releasePointer(source1->x + source1->w - 4, source1->y + source1->h - 4);
    expect(harness.screen().debugHeldMultiPokemonCount() == 2,
           "precondition: multi group should contain two Pokemon");

    const auto edge = harness.screen().debugGameSlotBounds(5);
    expect(edge.has_value(), "right-edge target slot should expose bounds");
    harness.pressPointer(edge->x + edge->w / 2, edge->y + edge->h / 2);
    expect(harness.screen().debugMultiPokemonMoveActive(),
           "placing a two-wide multi group at the right edge should fail and keep the group in hand");
    expect(harness.screen().debugGameSlotPokemonName(5).empty(),
           "failed multi placement should not partially place Pokemon");

    harness.back();
    expect(!harness.screen().debugMultiPokemonMoveActive(),
           "cancel should clear the held multi group after restoring sources");
    expect(harness.screen().debugGameSlotPokemonName(0) == "Buddy",
           "cancel should restore the first selected Pokemon to its source slot");
    expect(harness.screen().debugGameSlotPokemonName(1) == "Scout",
           "cancel should restore the second selected Pokemon to its source slot");
}

void testMultiToolBoxSpaceQuickDropUsesFirstEmptySlots() {
    TransferSystemHarness harness;
    harness.cycleToMultiTool();

    const auto source0 = harness.screen().debugGameSlotBounds(0);
    const auto source1 = harness.screen().debugGameSlotBounds(1);
    expect(source0.has_value() && source1.has_value(), "multi source slots should expose bounds");

    harness.pressPointer(source0->x + 4, source0->y + 4);
    harness.movePointer(source1->x + source1->w - 4, source1->y + source1->h - 4);
    harness.releasePointer(source1->x + source1->w - 4, source1->y + source1->h - 4);
    expect(harness.screen().debugHeldMultiPokemonCount() == 2,
           "precondition: multi group should contain two Pokemon");

    for (int j = 0; j < 5; ++j) {
        harness.navigate(0, 1);
    }
    harness.advance();
    harness.update(1.0);
    expect(harness.screen().debugGameBoxSpaceMode(), "multi move should be able to enter Box Space while holding");

    harness.navigate(0, -1);
    for (int i = 0; i < 4; ++i) {
        harness.navigate(0, -1);
    }
    harness.navigate(1, 0);
    expect(harness.screen().debugFocusedNode() == 2001, "expected Box Space tile for box index 1");

    harness.screen().onAdvanceLongPress();
    harness.update(0.1);
    expect(!harness.screen().debugMultiPokemonMoveActive(),
           "long press on a box with enough space should quick-drop the multi group");

    harness.advance();
    expect(harness.screen().currentGameBoxIndex() == 1, "accept should open the quick-drop target box");
    expect(harness.screen().debugGameSlotPokemonName(0) == "Buddy",
           "multi Box Space quick drop should use the first empty slot");
    expect(harness.screen().debugGameSlotPokemonName(1) == "Scout",
           "multi Box Space quick drop should continue in first-empty order rather than pattern anchoring");
}

void testUnicodeGameBoxNameBuildsTitleTexture() {
    TransferSystemHarness harness;

    harness.render();

    expect(harness.screen().debugGameBoxTitleTextureReady(),
           "game box header should build a renderable texture for Unicode box names");
    expect(harness.screen().debugGameBoxCachedTitleText() == "ボックス？1",
           "game box header should preserve the Unicode box name text");
}

void testMiniPreviewSpriteScaleCanExceedCellSize() {
    TransferSystemHarness harness;

    harness.moveFocusToGameBoxSpaceButton();
    harness.advance();
    const auto slot_bounds = harness.screen().debugGameSlotBounds(0);
    expect(slot_bounds.has_value(),
           "game slot bounds should be available for mini preview scale testing");
    harness.movePointer(slot_bounds->x + slot_bounds->w / 2, slot_bounds->y + slot_bounds->h / 2);
    harness.update(1.0);
    harness.render();

    const auto sprite_rect = harness.screen().debugMiniPreviewFirstSpriteRect();
    const SDL_Point cell_size = harness.screen().debugMiniPreviewCellSize();

    expect(sprite_rect.has_value(),
           "mini preview should draw the first sprite when Box Space focuses a populated box");
    expect(cell_size.x > 0 && cell_size.y > 0,
           "mini preview should report a valid cell size");
    expect(sprite_rect->w > cell_size.x || sprite_rect->h > cell_size.y,
           "mini preview sprite scale should allow sprites to render larger than a single cell");
}

void testMouseMiniPreviewOnlyShowsWhileHoveringValidBoxSpaceCell() {
    TransferSystemHarness harness;

    harness.moveFocusToGameBoxSpaceButton();
    harness.advance();

    const auto slot_bounds = harness.screen().debugGameSlotBounds(0);
    expect(slot_bounds.has_value(),
           "game slot bounds should be available for mouse mini preview testing");

    harness.movePointer(slot_bounds->x + slot_bounds->w / 2, slot_bounds->y + slot_bounds->h / 2);
    harness.update(1.0);
    harness.render();
    expect(harness.screen().debugMiniPreviewVisible(),
           "mouse hover on a valid Box Space cell should show the mini preview");

    harness.movePointer(10, 10);
    harness.update(1.0);
    harness.render();
    expect(!harness.screen().debugMiniPreviewVisible(),
           "mouse leaving Box Space cells should hide the mini preview");
}

void testMouseMiniPreviewDoesNotShowForEmptyBoxSpaceCell() {
    TransferSystemHarness harness;

    harness.moveFocusToGameBoxSpaceButton();
    harness.advance();

    const auto empty_slot_bounds = harness.screen().debugGameSlotBounds(1);
    expect(empty_slot_bounds.has_value(),
           "empty game slot bounds should be available for mouse mini preview testing");

    harness.movePointer(empty_slot_bounds->x + empty_slot_bounds->w / 2, empty_slot_bounds->y + empty_slot_bounds->h / 2);
    harness.update(1.0);
    harness.render();
    expect(!harness.screen().debugMiniPreviewVisible(),
           "mouse hover on an empty Box Space cell should not show the mini preview");
}

void testOpeningBoxFromBoxSpaceHidesMiniPreviewImmediately() {
    TransferSystemHarness harness;

    harness.moveFocusToGameBoxSpaceButton();
    harness.advance();

    const auto populated_slot_bounds = harness.screen().debugGameSlotBounds(0);
    expect(populated_slot_bounds.has_value(),
           "populated game slot bounds should be available for mini preview dismissal testing");

    harness.movePointer(populated_slot_bounds->x + populated_slot_bounds->w / 2, populated_slot_bounds->y + populated_slot_bounds->h / 2);
    harness.update(1.0);
    harness.render();
    expect(harness.screen().debugMiniPreviewVisible(),
           "mouse hover on a populated Box Space cell should show the mini preview before opening");

    harness.clickPointer(populated_slot_bounds->x + populated_slot_bounds->w / 2,
                         populated_slot_bounds->y + populated_slot_bounds->h / 2);
    harness.render();
    expect(!harness.screen().debugGameBoxSpaceMode(),
           "opening the focused Box Space cell should exit Box Space mode");
    expect(!harness.screen().debugMiniPreviewVisible(),
           "opening a box from Box Space by pointer should hide the mini preview immediately");
}

void testDropdownPreviewAllowsEmptyBoxes() {
    TransferSystemHarness harness;

    harness.moveFocusToGameBoxNamePlate();
    harness.advance();
    harness.update(1.0);
    harness.navigate(0, 1);
    harness.update(1.0);
    harness.render();
    expect(harness.screen().debugMiniPreviewVisible(),
           "opening the game box dropdown should allow preview for the currently highlighted box even when it is empty");
}

void testDropdownSelectionSnapsWithoutSliding() {
    TransferSystemHarness harness;

    harness.moveFocusToGameBoxNamePlate();
    harness.advance();
    harness.update(1.0);
    harness.navigate(0, 1);
    harness.advance();
    expect(harness.screen().currentGameBoxIndex() == 1,
           "selecting the next box from the dropdown should change the active game box");
    expect(!harness.screen().debugGameBoxContentSliding(),
           "selecting a box from the dropdown should snap without sliding");

    harness.update(1.0);
    harness.advance();
    harness.update(1.0);
    harness.navigate(0, -1);
    harness.advance();
    expect(harness.screen().currentGameBoxIndex() == 0,
           "selecting the previous box from the dropdown should change the active game box back");
    expect(!harness.screen().debugGameBoxContentSliding(),
           "dropdown-driven box changes should not slide in either direction");
}

void pickUpBuddyViaNormalToolMove(TransferSystemHarness& harness) {
    for (int i = 0; i < 6; ++i) {
        harness.navigate(1, 0);
    }
    expect(harness.screen().debugFocusedNode() == 2000,
           "expected keyboard navigation to reach the occupied top-left game slot");
    harness.advance();
    harness.update(1.0);
    expect(harness.screen().debugPokemonActionMenuVisible(),
           "accept on an occupied Pokemon slot should open the action menu");
    harness.advance();
    harness.update(1.0);
    expect(harness.screen().debugPokemonMoveActive(),
           "accept on Move should pick up the focused Pokemon");
    expect(harness.screen().debugHeldPokemonName() == "Buddy",
           "modal move should hold the focused Pokemon");
}

void testModalMoveKeyboardAcceptOpensBoxFromBoxSpace() {
    TransferSystemHarness harness;
    pickUpBuddyViaNormalToolMove(harness);

    for (int j = 0; j < 5; ++j) {
        harness.navigate(0, 1);
    }
    expect(harness.screen().debugFocusedNode() == 2110,
           "expected keyboard navigation to reach the game Box Space footer control");
    harness.advance();
    harness.update(1.0);
    expect(harness.screen().debugGameBoxSpaceMode(), "should enter Box Space while holding a Pokemon");
    expect(harness.screen().debugPokemonMoveActive(), "modal move should stay active in Box Space");

    harness.navigate(0, -1);
    expect(harness.screen().debugFocusedNode() == 2024,
           "up from Box Space footer should focus the bottom-left grid cell");
    for (int k = 0; k < 4; ++k) {
        harness.navigate(0, -1);
    }
    expect(harness.screen().debugFocusedNode() == 2000,
           "expected keyboard navigation to the top-left Box Space cell");

    harness.advance();
    expect(!harness.screen().debugGameBoxSpaceMode(),
           "accept on a Box Space cell should open that box while holding");
    expect(harness.screen().currentGameBoxIndex() == 0,
           "top-left Box Space tile should open game box index 0");
    expect(harness.screen().debugPokemonMoveActive(),
           "keyboard accept in Box Space should not cancel the in-hand Pokemon move");
    expect(harness.screen().debugHeldPokemonName() == "Buddy",
           "opening a box from Box Space via keyboard should keep the held Pokemon");
}

void testBoxSpaceLongPressPickupsBoxAndSecondAcceptSwapsBoxes() {
    TransferSystemHarness harness;

    harness.moveFocusToGameBoxSpaceButton();
    harness.advance();
    expect(harness.screen().debugGameBoxSpaceMode(), "should enter Box Space mode");

    harness.navigate(0, -1);
    for (int i = 0; i < 4; ++i) {
        harness.navigate(0, -1);
    }
    expect(harness.screen().debugFocusedNode() == 2000, "expected top-left Box Space cell");

    const std::string before0 = harness.screen().debugSpeechBubbleLineForFocus(2000);
    const std::string before1 = harness.screen().debugSpeechBubbleLineForFocus(2001);

    harness.screen().onAdvanceLongPress();
    harness.update(0.1);

    harness.navigate(1, 0);
    expect(harness.screen().debugFocusedNode() == 2001, "expected second cell for swap target");
    harness.advance();

    const std::string after0 = harness.screen().debugSpeechBubbleLineForFocus(2000);
    const std::string after1 = harness.screen().debugSpeechBubbleLineForFocus(2001);
    expect(after0 == before1 && after1 == before0, "box swap should exchange Box Space labels for the swapped cells");
}

void testBoxSpaceLongPressQuickDropPutsHeldPokemonInFirstEmptySlotOfFocusedBox() {
    TransferSystemHarness harness;
    pickUpBuddyViaNormalToolMove(harness);

    for (int j = 0; j < 5; ++j) {
        harness.navigate(0, 1);
    }
    harness.advance();
    harness.update(1.0);
    expect(harness.screen().debugGameBoxSpaceMode(), "should enter Box Space while holding");

    harness.navigate(0, -1);
    for (int i = 0; i < 4; ++i) {
        harness.navigate(0, -1);
    }
    harness.navigate(1, 0);
    expect(harness.screen().debugFocusedNode() == 2001, "expected Box Space tile for box index 1");

    harness.screen().onAdvanceLongPress();
    harness.update(0.1);
    expect(!harness.screen().debugPokemonMoveActive(), "quick drop should put down the held Pokemon");

    harness.advance();
    expect(harness.screen().currentGameBoxIndex() == 1, "accept on the focused tile should open box index 1");
    expect(harness.screen().debugGameSlotPokemonName(0) == "Buddy",
           "quick drop should place the held Pokemon into the first empty slot of the focused box");
}

void testBoxSpaceMouseHoldQuickDropPutsHeldPokemonInFirstEmptySlot() {
    TransferSystemHarness harness;
    pickUpBuddyViaNormalToolMove(harness);

    for (int j = 0; j < 5; ++j) {
        harness.navigate(0, 1);
    }
    harness.advance();
    harness.update(1.0);
    expect(harness.screen().debugGameBoxSpaceMode(), "should enter Box Space while holding");

    // Press and hold on box index 1 tile without moving.
    const auto cell1 = harness.screen().debugGameSlotBounds(1);
    expect(cell1.has_value(), "box space cell bounds should be available for mouse-hold quick drop");
    harness.pressPointer(cell1->x + cell1->w / 2, cell1->y + cell1->h / 2);
    for (int i = 0; i < 50; ++i) {
        harness.update(0.05);
    }
    expect(!harness.screen().debugPokemonMoveActive(),
           "mouse press-and-hold on a not-full Box Space tile should quick-drop the held Pokemon");
    expect(harness.screen().debugFocusedNode() == 2001,
           "after mouse-hold quick drop, focus should remain on the pressed Box Space tile for correct bubble anchoring");

    // Open that box and confirm Buddy is in its first slot.
    harness.clickPointer(cell1->x + cell1->w / 2, cell1->y + cell1->h / 2);
    expect(harness.screen().currentGameBoxIndex() == 1, "expected to open box index 1 after mouse-hold drop");
    expect(harness.screen().debugGameSlotPokemonName(0) == "Buddy",
           "mouse-hold quick drop should place the held Pokemon into the first empty slot of that box");
}

void testModalMovePointerClickOpensBoxFromBoxSpace() {
    TransferSystemHarness harness;
    pickUpBuddyViaNormalToolMove(harness);

    for (int j = 0; j < 5; ++j) {
        harness.navigate(0, 1);
    }
    expect(harness.screen().debugFocusedNode() == 2110,
           "expected keyboard navigation to reach the game Box Space footer control");
    harness.advance();
    harness.update(1.0);
    expect(harness.screen().debugGameBoxSpaceMode(), "should enter Box Space while holding a Pokemon");
    expect(harness.screen().debugPokemonMoveActive(), "modal move should stay active in Box Space");

    const auto slot0 = harness.screen().debugGameSlotBounds(0);
    expect(slot0.has_value(), "Box Space should still expose slot bounds for the top-left cell");
    const int cx = slot0->x + slot0->w / 2;
    const int cy = slot0->y + slot0->h / 2;
    expect(harness.screen().handlePointerPressed(cx, cy),
           "pointer press on a Box Space cell should be handled while holding a Pokemon");
    expect(harness.screen().handlePointerReleased(cx, cy),
           "pointer release should complete Box Space cell activation (open box) while holding");
    expect(!harness.screen().debugGameBoxSpaceMode(),
           "pointer click release on a Box Space cell should open that box");
    expect(harness.screen().currentGameBoxIndex() == 0,
           "top-left Box Space cell should open game box index 0");
    expect(harness.screen().debugPokemonMoveActive(),
           "opening a box from Box Space should not cancel the in-hand Pokemon move");
    expect(harness.screen().debugHeldPokemonName() == "Buddy",
           "opening a box from Box Space should keep the same Pokemon in hand");
}

void testModalMovePointerClickSelectsBoxFromDropdown() {
    TransferSystemHarness harness;
    pickUpBuddyViaNormalToolMove(harness);

    const auto plate = harness.screen().debugGameNamePlateBounds();
    expect(plate.has_value(), "game box name plate bounds should be available");
    const int px = plate->x + plate->w / 2;
    const int py = plate->y + plate->h / 2;
    expect(harness.screen().handlePointerPressed(px, py),
           "pointer press on the name plate should open the box dropdown while holding");
    (void)harness.screen().handlePointerReleased(px, py);
    for (int t = 0; t < 80; ++t) {
        harness.update(0.05);
    }

    int pick_y = -1;
    const int cx = plate->x + plate->w / 2;
    for (int y = plate->y + plate->h + 2; y < plate->y + plate->h + 480; y += 2) {
        const auto row = harness.screen().debugDropdownRowAtScreen(cx, y);
        if (row.has_value() && *row == 2) {
            pick_y = y + 14;
            break;
        }
    }
    expect(pick_y > 0, "should locate a screen point that hits dropdown row index 2 (box 1 after rename row)");

    expect(harness.screen().handlePointerPressed(cx, pick_y),
           "pointer press on a dropdown row should be handled while holding a Pokemon");
    expect(harness.screen().handlePointerReleased(cx, pick_y),
           "pointer release should apply dropdown row selection while holding");
    expect(harness.screen().currentGameBoxIndex() == 1,
           "pointer click release on a dropdown row should apply the box selection");
    expect(harness.screen().debugPokemonMoveActive(),
           "dropdown selection while holding should not cancel the move");
    expect(harness.screen().debugHeldPokemonName() == "Buddy",
           "dropdown selection should keep the held Pokemon");
}

} // namespace

int main() {
    try {
        testKeyboardAdvanceOpensFocusedBoxFromBoxSpaceMode();
        testBoxSpaceAdvanceOnTopLeftCellOpensThatBoxNotPokemonMenu();
        testKeyboardFocusShowsSpeechBubbleForOccupiedGameSlot();
        testKeyboardFocusShowsSpeechBubbleForGameIconAfterNonBubbleUi();
        testMouseHoverShowsAndClearsInfoBannerForPokemonSlots();
        testMouseHoverShowsInfoBannerForResortSlotsAndKeepsStoredSourceGame();
        testExitSaveModalSuppressesUnderlyingPokemonHoverBubble();
        testMouseHoverShowsPillLegendWhenItemPanelsAreHidden();
        testItemToolHoverOnlyShowsHeldItemBubbles();
        testItemToolMoveItemMovesHeldItemBetweenPokemon();
        testItemToolSwapAndCancelReturnsHeldItemToSwapTarget();
        testNormalToolClickOpensPokemonActionMenuBesideGameSlot();
        testNormalToolDragPickupsPokemonWithoutOpeningActionMenu();
        testNormalToolAcceptAndKeyboardNavigationUsePokemonActionMenu();
        testSwapToolPointerSwapKeepsTargetInHandAndBackReturnsIt();
        testMultiToolDragSelectsAndMovesPokemonAsLayout();
        testMultiToolRejectsNonFittingPatternAndCancelRestoresSources();
        testMultiToolPreservesGen12ThreeWideLayoutAtRightEdge();
        testMultiToolCanReturnResortGroupToGen12GameBox();
        testMultiToolBoxSpaceQuickDropUsesFirstEmptySlots();
        testUnicodeGameBoxNameBuildsTitleTexture();
        testMiniPreviewSpriteScaleCanExceedCellSize();
        testMouseMiniPreviewOnlyShowsWhileHoveringValidBoxSpaceCell();
        testMouseMiniPreviewDoesNotShowForEmptyBoxSpaceCell();
        testOpeningBoxFromBoxSpaceHidesMiniPreviewImmediately();
        testDropdownPreviewAllowsEmptyBoxes();
        testDropdownSelectionSnapsWithoutSliding();
        testBoxSpaceLongPressPickupsBoxAndSecondAcceptSwapsBoxes();
        testBoxSpaceLongPressQuickDropPutsHeldPokemonInFirstEmptySlotOfFocusedBox();
        testBoxSpaceMouseHoldQuickDropPutsHeldPokemonInFirstEmptySlot();
        testModalMoveKeyboardAcceptOpensBoxFromBoxSpace();
        testModalMovePointerClickOpensBoxFromBoxSpace();
        testModalMovePointerClickSelectsBoxFromDropdown();
        return EXIT_SUCCESS;
    } catch (const TestFailure& failure) {
        std::cerr << "FAIL: " << failure.what() << '\n';
        return EXIT_FAILURE;
    } catch (const std::exception& ex) {
        std::cerr << "ERROR: unexpected exception: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }
}
