#include "ui/TransferSystemScreen.hpp"

#include <iostream>

namespace pr {

std::unordered_map<std::string, int> TransferSystemScreen::conservationCounts() const {
    std::unordered_map<std::string, int> out;
    out.reserve(256);
    auto add = [&](const std::string& key) {
        if (key.empty()) return;
        out[key] += 1;
    };
    auto idFor = [&](const PcSlotSpecies& slot) -> std::string {
        if (!slot.occupied()) {
            return {};
        }
        if (!slot.bridge_box_payload_hash_sha256.empty()) {
            return std::string("hash:") + slot.bridge_box_payload_hash_sha256;
        }
        if (!slot.home_tracker.empty()) {
            return std::string("home:") + slot.home_tracker;
        }
        if (!slot.resort_pkrid.empty()) {
            return std::string("pkrid:") + slot.resort_pkrid;
        }
        // Fallback: best-effort stable-ish signature.
        return std::string("sig:") + std::to_string(slot.species_id) + ":" + slot.nickname;
    };

    for (const auto& box : game_pc_boxes_) {
        for (const auto& slot : box.slots) {
            add(idFor(slot));
        }
    }
    for (const auto& box : resort_pc_boxes_) {
        for (const auto& slot : box.slots) {
            add(idFor(slot));
        }
    }
    if (auto* held = pokemon_move_.held()) {
        add(idFor(held->pokemon));
    }
    if (multi_pokemon_move_.active()) {
        for (const auto& entry : multi_pokemon_move_.entries()) {
            add(idFor(entry.pokemon));
        }
    }
    return out;
}

bool TransferSystemScreen::verifyConservation(
    const std::unordered_map<std::string, int>& before,
    const char* context) const {
    const auto after = conservationCounts();
    if (before == after) {
        return true;
    }
    // Fail closed: do not allow operations that change the multiset of Pokemon identities.
    std::cerr << "Safety invariant failed (pokemon conservation) context="
              << (context ? context : "unknown") << '\n';
    for (const auto& [k, before_count] : before) {
        const int after_count = (after.count(k) > 0) ? after.at(k) : 0;
        if (before_count != after_count) {
            std::cerr << "  key=" << k << " before=" << before_count << " after=" << after_count << '\n';
        }
    }
    for (const auto& [k, after_count] : after) {
        if (before.count(k) > 0) {
            continue;
        }
        std::cerr << "  key=" << k << " before=0 after=" << after_count << '\n';
    }
    return false;
}

bool TransferSystemScreen::activateFocusedGameSlot() {
    const std::optional<int> slot = focusedGameSlotIndex();
    if (!slot.has_value()) {
        return false;
    }
    if (!panelsReadyForInteraction() || !game_save_box_viewport_) {
        return false;
    }
    if (game_box_browser_.gameBoxSpaceMode()) {
        const int box_index = game_box_browser_.gameBoxSpaceRowOffset() * 6 + *slot;
        return openGameBoxFromBoxSpaceSelection(box_index);
    }
    return false;
}

void TransferSystemScreen::update(double dt) {
    updateAnimations(dt);
    updateEnterExit(dt);
    updateCarouselSlide(dt);
    updateExitSaveModal(dt);
    if (box_rename_modal_open_) {
        box_rename_caret_blink_phase_ += dt;
        if (box_rename_caret_blink_phase_ > 640.0) {
            box_rename_caret_blink_phase_ -= 640.0;
        }
    }
    updateGameBoxDropdown(dt);
    updateResortBoxDropdown(dt);
    updatePokemonSummaryPanel(dt);
    updateMiniPreview(dt);
    updateActionMenus(dt);
    updateBoxSpaceLongPressGestures(dt);

    if (!multiPokemonToolActive()) {
        keyboard_multi_marquee_active_ = false;
    }
    updateBoxViewportsAndFocusDimming(dt);

    // Commit box index once the content slide finishes.
}

} // namespace pr
