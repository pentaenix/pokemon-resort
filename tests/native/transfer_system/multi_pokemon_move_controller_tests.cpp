#include "ui/transfer_system/MultiPokemonMoveController.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

struct TestFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure(message);
    }
}

pr::PcSlotSpecies pokemon(std::string name) {
    pr::PcSlotSpecies p;
    p.present = true;
    p.slug = name;
    p.species_name = std::move(name);
    return p;
}

using Controller = pr::transfer_system::MultiPokemonMoveController;
using Move = pr::transfer_system::PokemonMoveController;

void testTargetSlotsPreserveLayoutOffsets() {
    Controller controller;
    std::vector<Controller::Entry> entries;
    entries.push_back(Controller::Entry{
        pokemon("Piplup"),
        Move::SlotRef{Move::Panel::Game, 0, 0},
        0,
        0});
    entries.push_back(Controller::Entry{
        pokemon("Starly"),
        Move::SlotRef{Move::Panel::Game, 0, 1},
        0,
        1});
    entries.push_back(Controller::Entry{
        pokemon("Shinx"),
        Move::SlotRef{Move::Panel::Game, 0, 6},
        1,
        0});

    controller.pickUp(std::move(entries), Controller::InputMode::Keyboard, SDL_Point{0, 0});

    const auto slots = controller.targetSlotsFor(Move::SlotRef{Move::Panel::Resort, 0, 7});
    expect(slots.has_value(), "multi move should produce target slots when the shape fits");
    expect(slots->size() == 3, "multi move should preserve all selected Pokemon");
    expect((*slots)[0].slot_index == 7, "first selected Pokemon should land on the anchor slot");
    expect((*slots)[1].slot_index == 8, "second selected Pokemon should preserve its horizontal offset");
    expect((*slots)[2].slot_index == 13, "third selected Pokemon should preserve its row offset");
    expect((*slots)[0].panel == Move::Panel::Resort, "target slots should use the destination panel");
}

void testRejectsPatternThatWouldOverflowBoxGrid() {
    Controller controller;
    std::vector<Controller::Entry> entries;
    entries.push_back(Controller::Entry{pokemon("Piplup"), Move::SlotRef{Move::Panel::Game, 0, 0}, 0, 0});
    entries.push_back(Controller::Entry{pokemon("Starly"), Move::SlotRef{Move::Panel::Game, 0, 1}, 0, 1});
    controller.pickUp(std::move(entries), Controller::InputMode::Keyboard, SDL_Point{0, 0});

    const auto slots = controller.targetSlotsFor(Move::SlotRef{Move::Panel::Game, 1, 5});
    expect(!slots.has_value(), "multi move should reject a placement whose preserved shape overflows the right edge");
    expect(controller.active(), "rejected placement should keep the selected group in hand");
}

void testPointerIsClampedAndSwitchesInputMode() {
    Controller controller;
    std::vector<Controller::Entry> entries;
    entries.push_back(Controller::Entry{pokemon("Piplup"), Move::SlotRef{Move::Panel::Game, 0, 0}, 0, 0});
    controller.pickUp(std::move(entries), Controller::InputMode::Keyboard, SDL_Point{10, 10});

    controller.updatePointer(SDL_Point{-50, 900}, 1280, 800);
    expect(controller.inputMode() == Controller::InputMode::Pointer, "pointer movement should switch multi move into pointer mode");
    expect(controller.pointer().x == 0, "multi move pointer x should clamp to the screen");
    expect(controller.pointer().y == 800, "multi move pointer y should clamp to the screen");
}

} // namespace

int main() {
    try {
        testTargetSlotsPreserveLayoutOffsets();
        testRejectsPatternThatWouldOverflowBoxGrid();
        testPointerIsClampedAndSwitchesInputMode();
        return EXIT_SUCCESS;
    } catch (const TestFailure& failure) {
        std::cerr << "FAIL: " << failure.what() << '\n';
        return EXIT_FAILURE;
    } catch (const std::exception& ex) {
        std::cerr << "ERROR: unexpected exception: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }
}
