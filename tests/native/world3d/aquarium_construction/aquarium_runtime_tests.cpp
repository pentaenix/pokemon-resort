#include "gameplay/world3d/aquarium/construction/AquariumCollisionOverlay.hpp"
#include "gameplay/world3d/aquarium/construction/AquariumConstructionCamera.hpp"
#include "gameplay/world3d/aquarium/construction/AquariumConstructionSession.hpp"
#include "gameplay/world3d/aquarium/construction/AquariumConstructionVisual.hpp"
#include "gameplay/world3d/aquarium/construction/AquariumDesignStore.hpp"
#include "gameplay/world3d/aquarium/construction/AquariumPlayerRuntime.hpp"
#include "gameplay/world3d/aquarium/rendering/AquariumResourceGeneration.hpp"
#include "gameplay/world3d/interiors/InteriorFloorCutout.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace aq = pr::gameplay::world3d::aquarium;
namespace construction = aq::construction;
namespace geo = pr::aquarium::geometry;
namespace fs = std::filesystem;

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

construction::AquariumDesignDocument emptyDocument() {
    construction::AquariumDesignDocument document;
    document.design_id = "aqd_runtime_test";
    document.map_id = "aquarium12";
    return document;
}

aq::AquariumConstructionConfig constructionConfig() {
    aq::AquariumConstructionConfig config;
    config.enabled = true;
    for (int row = 10; row <= 15; ++row) {
        for (int column = 10; column <= 21; ++column) {
            config.allowed_cells.push_back({column, row});
        }
    }
    return config;
}

void stateMachinePreservesCommittedDataOnCancel() {
    construction::AquariumConstructionSession unavailable;
    unavailable.configure("outdoor", {}, {}, emptyDocument());
    require(!unavailable.enter({0, 0}),
        "construction entered on a map without explicit construction metadata");

    construction::AquariumConstructionSession session;
    session.configure("aquarium12", constructionConfig(), {}, emptyDocument());
    require(session.enter({9, 9}), "construction did not enter on enabled map");
    require(session.beginRectangle(), "rectangle draft did not begin");
    session.moveCursor(1, 1);
    require(!session.draftValid(), "2x2 draft must remain invalid");
    require(session.reviewDraft(), "invalid draft did not enter review");
    require(!session.prepareCommit(), "invalid draft prepared a commit");
    require(session.cancel(), "draft cancel was not consumed");
    require(session.committedDesign().revision == 0 && session.committedDesign().tanks.empty(),
        "cancel mutated committed aquarium data");
    session.exit();
    require(session.enter({10, 10}), "construction did not re-enter on an allowed player cell");
    require(session.beginRectangle(), "player-cell rectangle draft did not begin");
    session.moveCursor(2, 2);
    require(session.draftValid(),
        "hidden player cell still blocks construction despite doorway relocation on exit");
}

construction::ConstructionCommitCandidate buildFirstTank(
    construction::AquariumConstructionSession& session) {
    require(session.beginRectangle(), "rectangle draft did not begin");
    session.moveCursor(2, 2);
    require(session.draftValid(), "3x3 draft should be valid");
    require(session.reviewDraft(), "valid rectangle did not enter draft review");
    auto candidate = session.prepareCommit();
    require(candidate.has_value(), "valid rectangle did not prepare a commit");
    geo::AquariumBuildRequest request;
    request.tank = candidate->document.tanks.back();
    require(geo::buildAquarium(request).validation.valid(),
        "prepared rectangle geometry is invalid");
    return std::move(*candidate);
}

void drawnRectanglesMergeOnlyWhenTheyTouch() {
    construction::AquariumConstructionSession session;
    session.configure("aquarium12", constructionConfig(), {}, emptyDocument());
    require(session.enter({9, 9}), "construction did not enter");
    auto candidate = buildFirstTank(session);
    require(candidate.document.revision == 1 && candidate.document.tanks.size() == 1,
        "confirmed command did not advance revision exactly once");
    session.publish(std::move(candidate));
    require(session.committedDesign().tanks.front().id == "tank_1",
        "tank stable ID changed during publication");

    session.pointAt({10, 10});
    require(session.selectAtCursor(), "existing tank could not be selected");
    session.pointAt({13, 10});
    require(session.beginRectangle(),
        "selected state could not begin a stable rectangle gesture");
    session.pointAt({15, 12});
    const auto touching_preview = session.previewTank();
    require(touching_preview && touching_preview->id == "tank_1" &&
            touching_preview->footprint.width_cells == 6,
        "edge-touching rectangle did not preview as an extension");
    require(session.reviewDraft() && session.draftValid(),
        "edge-touching extension was not valid");
    auto extension = session.prepareCommit();
    require(extension && extension->command.kind == construction::AquariumCommandKind::EditTankSet &&
            extension->document.tanks.size() == 1U,
        "edge-touching rectangle did not prepare one merged tank edit");
    require(session.publish(std::move(*extension)) &&
            session.committedDesign().tanks.front().id == "tank_1",
        "merged rectangle did not preserve the existing stable tank ID");
    auto undo = session.prepareUndo();
    require(undo && undo->document.tanks.size() == 1U &&
            undo->document.tanks.front().footprint.width_cells == 3,
        "merged rectangle was not reversible as one command");

    construction::AquariumConstructionSession separated;
    separated.configure("aquarium12", constructionConfig(), {}, emptyDocument());
    require(separated.enter({10, 10}), "separate fixture did not enter");
    auto first = buildFirstTank(separated);
    require(separated.publish(std::move(first)), "separate fixture tank did not publish");
    separated.pointAt({14, 10});
    require(separated.beginRectangle(), "separate rectangle did not begin");
    separated.pointAt({16, 12});
    require(separated.reviewDraft() && separated.draftValid(),
        "rectangle with one empty cell of separation was not valid");
    auto separate = separated.prepareCommit();
    require(separate && separate->command.kind == construction::AquariumCommandKind::CreateTank &&
            separate->document.tanks.size() == 2U,
        "one empty-cell gap unexpectedly merged distinct tanks");

    geo::TankDesign overlapping = separated.committedDesign().tanks.front();
    overlapping.id = "drawn_overlap";
    overlapping.footprint.origin_cell = {11, 10};
    const auto overlap = construction::resolveDrawnTank(
        separated.committedDesign().tanks, overlapping);
    require(overlap.extends_existing && overlap.primary_tank_id == "tank_1" &&
            overlap.tanks.size() == 1U,
        "overlapping rectangle did not resolve as an extension");

    geo::TankDesign second = separated.committedDesign().tanks.front();
    second.id = "tank_bridge_target";
    second.footprint.origin_cell = {16, 10};
    geo::TankDesign bridge = second;
    bridge.id = "drawn_bridge";
    bridge.footprint.origin_cell = {13, 10};
    const auto bridged = construction::resolveDrawnTank(
        {separated.committedDesign().tanks.front(), second}, bridge);
    require(bridged.extends_existing && bridged.tanks.size() == 1U &&
            bridged.preview_tank.footprint.width_cells == 9,
        "rectangle touching two tanks did not merge the complete connected footprint");
}

void directPaintGesturesCommitAsSingleUndoableCommands() {
    construction::AquariumConstructionSession session;
    session.configure("aquarium12", constructionConfig(), {}, emptyDocument());
    require(session.enter({9, 9}), "paint fixture did not enter construction");
    auto created = buildFirstTank(session);
    require(session.publish(std::move(created)), "paint fixture tank did not publish");
    session.pointAt({10, 10});
    require(session.selectAtCursor(), "paint fixture tank was not selected");
    session.pointAt({13, 11});
    require(session.beginPaintSelected(false) && !session.draftValid(),
        "one-cell-wide add gesture was not held as an invalid preview");
    session.pointAt({13, 10});
    session.pointAt({13, 12});
    require(session.draftValid(),
        "three-cell-wide add gesture did not expand the selected tank");
    require(session.reviewDraft(), "add gesture did not finish");
    auto expanded = session.prepareCommit();
    require(expanded && expanded->document.tanks.front().footprint.width_cells == 4,
        "add gesture did not prepare one expanded footprint command");
    require(session.publish(std::move(*expanded)) && session.undoCount() == 2,
        "add gesture was not recorded as one undoable command");

    const auto expanded_cells = geo::footprintCells(
        session.committedDesign().tanks.front().footprint);
    int min_column = expanded_cells.front().column;
    int min_row = expanded_cells.front().row;
    for (const geo::GridCell cell : expanded_cells) {
        min_column = std::min(min_column, cell.column);
        min_row = std::min(min_row, cell.row);
    }
    session.pointAt({min_column, min_row});
    require(session.beginPaintSelected(true) && !session.draftValid(),
        "narrow corner subtraction was not retained as an invalid preview");
    session.pointAt({min_column + 1, min_row});
    const auto corner_cut_cells = session.draftCells();
    require(corner_cut_cells.size() == 2 && corner_cut_cells.front().column == min_column &&
            corner_cut_cells.back().column == min_column + 1,
        "minus mode did not preview the full rectangular cell selection");
    require(session.cancel(), "corner-subtraction regression draft did not cancel");

    int max_column = min_column;
    int max_row = min_row;
    for (const auto cell : expanded_cells) {
        max_column = std::max(max_column, cell.column);
        max_row = std::max(max_row, cell.row);
    }
    session.pointAt({min_column, min_row});
    require(session.beginPaintSelected(true), "subtract gesture did not begin");
    session.pointAt({max_column, max_row});
    require(session.draftValid(),
        "selecting the complete footprint did not become a valid delete gesture");
    require(session.reviewDraft(), "delete-by-subtraction did not finish");
    auto removed = session.prepareCommit();
    require(removed && removed->document.tanks.empty(),
        "delete-by-subtraction did not prepare a delete command");
}

void exteriorPaintMergesAndErasesMultipleTanksUndoably() {
    auto document = emptyDocument();
    geo::TankDesign alpha;
    alpha.id = "tank_alpha";
    alpha.footprint.origin_cell = {10, 10};
    alpha.footprint.width_cells = 3;
    alpha.footprint.depth_cells = 3;
    geo::TankDesign beta = alpha;
    beta.id = "tank_beta";
    beta.footprint.origin_cell = {14, 10};
    document.tanks = {alpha, beta};

    construction::AquariumConstructionSession merge;
    merge.configure("aquarium12", constructionConfig(), {}, document);
    require(merge.enter({21, 15}), "merge fixture did not enter construction");
    merge.pointAt({10, 11});
    require(merge.selectAtCursor(), "merge fixture did not select its primary tank");
    merge.pointAt({13, 11});
    require(merge.beginPaintSelected(false) && !merge.draftValid(),
        "one-cell bridge did not remain an invalid preview");
    merge.pointAt({13, 10});
    merge.pointAt({13, 12});
    merge.pointAt({14, 11});
    require(merge.draftValid(), "three-cell-wide bridge into a second tank was invalid");
    require(merge.reviewDraft(), "merged paint gesture did not enter review");
    auto merged = merge.prepareCommit();
    require(merged && merged->document.tanks.size() == 1 &&
            merged->document.tanks.front().id == "tank_alpha",
        "plus paint did not merge tanks under the selected stable ID");
    geo::AquariumBuildRequest merged_request;
    merged_request.tank = merged->document.tanks.front();
    const auto merged_build = geo::buildAquarium(merged_request);
    require(merged_build.validation.valid() &&
            merged_build.collision.blocked_cells.size() ==
                geo::footprintCells(merged_request.tank.footprint).size(),
        "merged L-shaped footprint did not generate matching geometry and collision");
    require(merge.publish(std::move(*merged)) && merge.canUndo(),
        "merged tank command was not published to undo history");
    auto undo_merge = merge.prepareUndo();
    require(undo_merge && merge.publish(std::move(*undo_merge)) &&
            merge.committedDesign().tanks.size() == 2 && merge.canRedo(),
        "undo did not restore both tanks after a merge");
    auto redo_merge = merge.prepareRedo();
    require(redo_merge && merge.publish(std::move(*redo_merge)) &&
            merge.committedDesign().tanks.size() == 1,
        "redo did not reapply the exterior merge");

    construction::AquariumConstructionSession erase;
    erase.configure("aquarium12", constructionConfig(), {}, document);
    require(erase.enter({21, 15}), "erase fixture did not enter construction");
    erase.pointAt({19, 15});
    require(erase.beginPaintSelected(true),
        "minus paint could not arm from empty space without a prior selection");
    require(erase.draftValid() && erase.reviewDraft() && erase.finishNoOpDraft(),
        "empty minus selection did not finish as a harmless no-op");
    require(erase.state() == construction::ConstructionState::Browse &&
            !erase.selectedTankId() && erase.committedDesign().tanks.size() == 2,
        "empty minus selection did not return to unchanged browse state");
    erase.pointAt({19, 15});
    require(erase.beginPaintSelected(true),
        "minus selection could not restart after explicit cancellation");
    erase.pointAt({10, 10});
    require(erase.draftValid() && erase.draftCells().size() == 60,
        "one rectangular minus selection did not cover both tanks from exterior space");
    require(erase.reviewDraft(), "multi-delete paint did not enter review");
    auto deleted = erase.prepareCommit();
    require(deleted && deleted->document.tanks.empty(),
        "multi-delete did not prepare one empty tank-set transaction");
    require(erase.publish(std::move(*deleted)), "multi-delete did not publish");
    auto undo_delete = erase.prepareUndo();
    require(undo_delete && erase.publish(std::move(*undo_delete)) &&
            erase.committedDesign().tanks.size() == 2 && erase.canRedo(),
        "undo arrow history could not restore all erased tanks");
    auto redo_delete = erase.prepareRedo();
    require(redo_delete && erase.publish(std::move(*redo_delete)) &&
            erase.committedDesign().tanks.empty(),
        "redo arrow history could not erase all tanks again");
}

void draftReviewIsNonMutatingAndAdjustmentIsReversible() {
    construction::AquariumConstructionSession session;
    session.configure("aquarium12", constructionConfig(), {}, emptyDocument());
    require(session.enter({9, 9}), "draft-review fixture did not enter construction");
    require(session.beginRectangle(), "draft-review fixture did not choose an anchor");
    for (int step = 0; step < 10; ++step) session.moveCursor(1, 0);
    require(session.cursor().column == 20 && session.cursor().row == 10,
        "ten construction movement steps did not advance ten whole cells");
    session.moveCursor(-8, 2);
    require(session.reviewDraft(), "opposite-corner placement did not enter draft review");
    require(session.state() == construction::ConstructionState::DraftReview &&
            session.committedDesign().revision == 0 && session.committedDesign().tanks.empty(),
        "draft review mutated the authoritative aquarium");
    const auto locked_review_cells = session.draftCells();
    session.pointAt({20, 15});
    const auto after_pointer_move = session.draftCells();
    require(after_pointer_move.size() == locked_review_cells.size() &&
            after_pointer_move.front().column == locked_review_cells.front().column &&
            after_pointer_move.front().row == locked_review_cells.front().row &&
            after_pointer_move.back().column == locked_review_cells.back().column &&
            after_pointer_move.back().row == locked_review_cells.back().row,
        "pointer movement toward review controls changed the locked footprint");
    require(session.adjustDraft() &&
            session.state() == construction::ConstructionState::ResizeFootprint,
        "back from draft review did not restore footprint adjustment");
    session.moveCursor(1, 0);
    require(session.reviewDraft(), "adjusted draft did not return to review");
    require(session.cancel() && session.state() == construction::ConstructionState::Browse &&
            session.committedDesign().revision == 0,
        "discarding a reviewed draft changed committed data");
}

void editingHistoryIsTransactionalStableAndStaleSafe() {
    auto document = emptyDocument();
    document.revision = 5;
    geo::TankDesign tank;
    tank.id = "tank_stable";
    tank.footprint.origin_cell = {10, 10};
    tank.footprint.width_cells = 3;
    tank.footprint.depth_cells = 3;
    document.tanks.push_back(tank);
    construction::AquariumConstructionSession session;
    session.configure("aquarium12", constructionConfig(), {}, document);
    require(session.enter({9, 9}), "editing fixture did not enter construction");
    session.pointAt({10, 10});
    require(session.selectAtCursor() && session.selectedTankId() == "tank_stable",
        "player tank was not selectable by its occupied cell");
    require(session.beginMoveSelected(), "selected tank did not enter move mode");
    session.moveCursor(3, 0);
    require(session.reviewDraft() && session.draftValid(),
        "valid move did not enter review");
    auto moved = session.prepareCommit();
    require(moved && moved->document.revision == 6 &&
            moved->document.tanks[0].id == "tank_stable" &&
            moved->document.tanks[0].footprint.origin_cell.column == 13,
        "move candidate changed stable identity or revision semantics");
    require(session.publish(std::move(*moved)) && session.undoCount() == 1 &&
            session.state() == construction::ConstructionState::Selected,
        "published move did not enter history or retain selection");

    auto stale_undo = session.prepareUndo();
    require(stale_undo && session.cancel(), "undo cancellation fixture did not start and cancel");
    require(!session.candidateCurrent(*stale_undo) && !session.publish(std::move(*stale_undo)) &&
            session.committedDesign().revision == 6 && session.undoCount() == 1,
        "cancelled async candidate published or consumed history");

    auto undo = session.prepareUndo();
    const auto durable_undo = undo
        ? construction::parseAquariumDesign(
            construction::serializeAquariumDesignCanonical(undo->document))
        : construction::AquariumDesignLoadResult{};
    require(durable_undo.document && durable_undo.document->revision == 7 &&
            durable_undo.document->tanks[0].footprint.origin_cell.column == 10,
        "undo candidate did not survive the canonical transactional-save representation");
    require(undo && session.publish(std::move(*undo)) &&
            session.committedDesign().revision == 7 &&
            session.committedDesign().tanks[0].footprint.origin_cell.column == 10 &&
            session.canRedo(),
        "undo did not restore the exact tank through a new durable revision");
    auto redo = session.prepareRedo();
    require(redo && session.publish(std::move(*redo)) &&
            session.committedDesign().revision == 8 &&
            session.committedDesign().tanks[0].footprint.origin_cell.column == 13,
        "redo did not reapply the move through a new durable revision");

    require(session.beginResizeSelected(construction::AquariumResizeHandle::SouthEast),
        "selected tank did not expose its south-east resize handle");
    session.moveCursor(1, 1);
    require(session.reviewDraft() && session.draftValid(),
        "valid resize did not enter review");
    auto resized = session.prepareCommit();
    require(resized && session.publish(std::move(*resized)) &&
            session.committedDesign().revision == 9 &&
            session.committedDesign().tanks[0].footprint.width_cells == 4 &&
            session.committedDesign().tanks[0].footprint.depth_cells == 4,
        "resize did not preserve identity and publish discrete dimensions");

    require(session.requestDeleteSelected(), "selected tank did not enter delete confirmation");
    auto deleted = session.prepareDelete();
    require(deleted && session.publish(std::move(*deleted)) &&
            session.committedDesign().revision == 10 && session.committedDesign().tanks.empty(),
        "confirmed deletion did not publish as an undoable command");
    auto restore = session.prepareUndo();
    require(restore && session.publish(std::move(*restore)) &&
            session.committedDesign().revision == 11 &&
            session.committedDesign().tanks.size() == 1 &&
            session.committedDesign().tanks[0].id == "tank_stable",
        "undo deletion did not restore the stable tank");
}

void subtractEditingCommitsUndoablyAndRejectsEnclosedCuts() {
    auto document = emptyDocument();
    geo::TankDesign tank;
    tank.id = "tank_subtract";
    tank.footprint.origin_cell = {10, 10};
    tank.footprint.width_cells = 7;
    tank.footprint.depth_cells = 5;
    document.tanks.push_back(tank);

    construction::AquariumConstructionSession session;
    session.configure("aquarium12", constructionConfig(), {}, document);
    require(session.enter({9, 9}), "subtract fixture did not enter construction");
    session.pointAt({13, 10});
    require(session.selectAtCursor() && session.beginSubtractSelected(),
        "selected rectangle did not enter subtract editing");
    session.pointAt({13, 10});
    require(session.toggleSubtractedCell() && session.draftValid(),
        "edge-connected subtraction was not accepted as a valid draft");
    require(session.reviewDraft(), "valid subtraction did not enter review");
    auto cut = session.prepareCommit();
    require(cut && session.publish(std::move(*cut)) &&
            session.committedDesign().revision == 1 &&
            session.committedDesign().tanks[0].footprint.subtracted_cells.size() == 1,
        "subtraction did not publish as one transactional command");

    auto undo = session.prepareUndo();
    require(undo && session.publish(std::move(*undo)) &&
            session.committedDesign().revision == 2 &&
            session.committedDesign().tanks[0].footprint.subtracted_cells.empty(),
        "undo did not restore the uncut rectangle through a new revision");

    session.pointAt({13, 12});
    require(session.beginSubtractSelected() && session.toggleSubtractedCell(),
        "enclosed-cut fixture could not toggle its interior cell");
    require(!session.draftValid(), "an enclosed interior cut was accepted");
    require(session.reviewDraft() && !session.prepareCommit(),
        "an enclosed interior cut prepared a commit");
    require(session.cancel() && session.committedDesign().revision == 2 &&
            session.committedDesign().tanks[0].footprint.subtracted_cells.empty(),
        "cancelling the invalid subtraction changed committed data");
}

void discreteShapePropertiesAreDraftedAndTransactional() {
    auto document = emptyDocument();
    document.revision = 2;
    geo::TankDesign tank;
    tank.id = "tank_properties";
    tank.footprint.origin_cell = {10, 10};
    tank.footprint.width_cells = 5;
    tank.footprint.depth_cells = 5;
    document.tanks.push_back(tank);
    construction::AquariumConstructionSession session;
    session.configure("aquarium12", constructionConfig(), {}, document);
    require(session.enter({9, 9}), "property fixture did not enter construction");
    session.pointAt({10, 10});
    require(session.selectAtCursor(), "property fixture could not select its tank");
    require(!session.adjustTankProperty(construction::AquariumTankProperty::NotchWidth, 1) &&
            session.state() == construction::ConstructionState::Selected && !session.draft(),
        "rectangle-only notch input created a draft");
    require(session.adjustTankProperty(construction::AquariumTankProperty::Shape, 1) &&
            session.adjustTankProperty(construction::AquariumTankProperty::Height, 1) &&
            session.adjustTankProperty(construction::AquariumTankProperty::Depth, 1) &&
            session.adjustTankProperty(construction::AquariumTankProperty::Roundness, 1) &&
            session.adjustTankProperty(construction::AquariumTankProperty::Rotation, 1) &&
            session.adjustTankProperty(construction::AquariumTankProperty::NotchWidth, -1) &&
            session.adjustTankProperty(construction::AquariumTankProperty::NotchDepth, -1),
        "discrete property controls did not update one shared draft");
    const auto preview = session.previewTank();
    require(preview && preview->footprint.shape == geo::FootprintShape::L &&
            preview->height_steps == 9 && preview->depth_steps == 1 &&
            preview->corner_radius_steps == 1 &&
            preview->footprint.rotation_quarter_turns == 1 &&
            preview->footprint.notch_width_cells == 2 &&
            preview->footprint.notch_depth_cells == 2 && session.draftValid(),
        "property draft does not expose the expected discrete L-tank preview");
    require(session.committedDesign().revision == 2 &&
            session.committedDesign().tanks.front().footprint.shape == geo::FootprintShape::Rectangle,
        "property preview mutated the authoritative design");
    auto candidate = session.prepareCommit();
    require(candidate && session.publish(std::move(*candidate)) &&
            session.committedDesign().revision == 3 &&
            session.committedDesign().tanks.front().id == "tank_properties" &&
            session.committedDesign().tanks.front().footprint.shape == geo::FootprintShape::L,
        "property command did not publish transactionally with stable identity");

    require(session.adjustTankProperty(construction::AquariumTankProperty::Shape, 1),
        "L tank did not cycle to U");
    const auto u_preview = session.previewTank();
    require(u_preview && u_preview->footprint.shape == geo::FootprintShape::U &&
            session.cancel() && session.state() == construction::ConstructionState::Selected &&
            session.committedDesign().revision == 3 &&
            session.committedDesign().tanks.front().footprint.shape == geo::FootprintShape::L,
        "cancelling a U-shape property draft changed the committed L tank");
}

void invalidMoveAndResizeCannotPrepareCommands() {
    auto document = emptyDocument();
    geo::TankDesign first;
    first.id = "tank_first";
    first.footprint.origin_cell = {10, 10};
    first.footprint.width_cells = 3;
    first.footprint.depth_cells = 3;
    geo::TankDesign second = first;
    second.id = "tank_second";
    second.footprint.origin_cell = {16, 10};
    document.tanks = {first, second};
    construction::AquariumConstructionSession session;
    session.configure("aquarium12", constructionConfig(), {}, document);
    require(session.enter({9, 9}), "invalid-edit fixture did not enter construction");
    session.pointAt({10, 10});
    require(session.selectAtCursor() && session.beginMoveSelected(),
        "invalid overlap fixture could not select and move a player tank");
    session.moveCursor(6, 0);
    require(session.reviewDraft() && !session.draftValid() && !session.prepareCommit(),
        "move overlapping another player tank prepared a command");
    require(session.adjustDraft(), "invalid move could not return to adjustment");
    session.pointAt({21, 15});
    require(session.reviewDraft() && !session.draftValid() && !session.prepareCommit(),
        "out-of-bounds move prepared a command");
}

void everyEditingStateCancelsWithoutChangingTheDocument() {
    auto document = emptyDocument();
    document.revision = 3;
    geo::TankDesign tank;
    tank.id = "tank_cancel";
    tank.footprint.origin_cell = {12, 11};
    tank.footprint.width_cells = 3;
    tank.footprint.depth_cells = 3;
    document.tanks.push_back(tank);
    construction::AquariumConstructionSession session;
    session.configure("aquarium12", constructionConfig(), {{10, 10}}, document);
    require(session.enter({9, 9}), "editing-cancel fixture did not enter construction");
    session.pointAt({10, 10});
    require(!session.selectAtCursor() && session.state() == construction::ConstructionState::Browse,
        "authored obstacle was selectable as a player tank");
    session.pointAt({12, 11});
    require(session.selectAtCursor(), "editing-cancel fixture could not select its player tank");

    require(session.beginMoveSelected(), "move cancellation fixture did not begin");
    session.moveCursor(2, 1);
    require(session.cancel() && session.state() == construction::ConstructionState::Selected,
        "cancelling move did not return to the selected tank");
    require(session.committedDesign().revision == 3 &&
            session.committedDesign().tanks[0].footprint.origin_cell.column == 12,
        "cancelling move changed the committed document");

    require(session.beginResizeSelected(construction::AquariumResizeHandle::West),
        "resize cancellation fixture did not begin");
    session.moveCursor(-2, 0);
    require(session.reviewDraft() && session.cancel() &&
            session.state() == construction::ConstructionState::Selected,
        "cancelling reviewed resize did not restore selection");
    require(session.committedDesign().revision == 3 &&
            session.committedDesign().tanks[0].footprint.width_cells == 3,
        "cancelling resize changed the committed dimensions");

    require(session.requestDeleteSelected() && session.cancelDelete() &&
            session.state() == construction::ConstructionState::Selected,
        "cancelling deletion did not restore selection");
    require(session.committedDesign().revision == 3 && session.committedDesign().tanks.size() == 1,
        "cancelling deletion changed the committed tank list");
    require(session.cancel() && session.state() == construction::ConstructionState::Browse,
        "cancelling selection did not return to Browse");
}

void newerAsyncOperationInvalidatesEveryOlderCandidate() {
    auto document = emptyDocument();
    geo::TankDesign tank;
    tank.id = "tank_race";
    tank.footprint.origin_cell = {12, 11};
    tank.footprint.width_cells = 3;
    tank.footprint.depth_cells = 3;
    document.tanks.push_back(tank);
    construction::AquariumConstructionSession session;
    session.configure("aquarium12", constructionConfig(), {}, document);
    require(session.enter({9, 9}), "worker-race fixture did not enter construction");
    session.pointAt({12, 11});
    require(session.selectAtCursor() && session.beginMoveSelected(),
        "worker-race fixture could not begin its first edit");
    session.moveCursor(1, 0);
    require(session.reviewDraft(), "worker-race move did not enter review");
    auto old_candidate = session.prepareCommit();
    require(old_candidate && session.cancel(), "worker-race first candidate did not cancel");

    require(session.beginResizeSelected(construction::AquariumResizeHandle::South),
        "worker-race fixture could not begin its replacement edit");
    session.moveCursor(0, 1);
    require(session.reviewDraft(), "worker-race resize did not enter review");
    auto current_candidate = session.prepareCommit();
    require(current_candidate &&
            current_candidate->operation_token != old_candidate->operation_token &&
            !session.candidateCurrent(*old_candidate) &&
            session.candidateCurrent(*current_candidate),
        "new async operation did not make the older generation token stale");
    require(!session.publish(std::move(*old_candidate)) &&
            session.publish(std::move(*current_candidate)) &&
            session.committedDesign().revision == 1 &&
            session.committedDesign().tanks[0].footprint.depth_cells == 4,
        "stale worker result published or displaced the current candidate");
}

void cursorChoosesEveryControllerResizeHandle() {
    auto document = emptyDocument();
    geo::TankDesign tank;
    tank.id = "tank_directional_resize";
    tank.footprint.origin_cell = {12, 11};
    tank.footprint.width_cells = 5;
    tank.footprint.depth_cells = 5;
    document.tanks.push_back(tank);
    construction::AquariumConstructionSession session;
    session.configure("aquarium12", constructionConfig(), {}, document);
    require(session.enter({12, 11}) && session.selectAtCursor(),
        "directional-resize fixture could not select its tank");
    const std::array<std::pair<geo::GridCell, construction::AquariumResizeHandle>, 8> cases{{
        {geo::GridCell{12, 11}, construction::AquariumResizeHandle::NorthWest},
        {geo::GridCell{14, 11}, construction::AquariumResizeHandle::North},
        {geo::GridCell{16, 11}, construction::AquariumResizeHandle::NorthEast},
        {geo::GridCell{16, 13}, construction::AquariumResizeHandle::East},
        {geo::GridCell{16, 15}, construction::AquariumResizeHandle::SouthEast},
        {geo::GridCell{14, 15}, construction::AquariumResizeHandle::South},
        {geo::GridCell{12, 15}, construction::AquariumResizeHandle::SouthWest},
        {geo::GridCell{12, 13}, construction::AquariumResizeHandle::West},
    }};
    for (const auto& [cell, expected] : cases) {
        session.pointAt(cell);
        require(session.preferredResizeHandle() == expected,
            "grid cursor did not choose the matching resize handle");
    }
    session.pointAt({12, 11});
    const auto expected = session.preferredResizeHandle();
    require(session.beginResizeSelected(expected) && session.draft() &&
            session.draft()->resize_handle == construction::AquariumResizeHandle::NorthWest,
        "controller-style resize did not grab the visibly selected handle");
}

void authoredObstaclesRemainVisibleAtBuildZoneEdges() {
    const std::vector<geo::GridCell> allowed{{10, 10}, {11, 10}, {10, 11}, {11, 11}};
    const std::vector<geo::GridCell> authored{
        {10, 10}, {9, 10}, {12, 11}, {8, 8}, {30, 30}, {9, 10}};
    const auto locked = construction::aquariumConstructionContextLockedCells(
        allowed, authored, 2);
    const std::vector<geo::GridCell> expected{{8, 8}, {9, 10}, {10, 10}, {12, 11}};
    const bool matches = locked.size() == expected.size() &&
        std::equal(locked.begin(), locked.end(), expected.begin(),
            [](geo::GridCell lhs, geo::GridCell rhs) {
                return lhs.column == rhs.column && lhs.row == rhs.row;
            });
    require(matches,
        "authored obstacle overlay omitted nearby locked cells or retained distant clutter");
}

void constructionVisualBuildsYellowCellsGizmosAndCanonicalHitTargets() {
    require(construction::aquariumConstructionCellInWorkingView({20, 18}, {10, 10}) &&
            construction::aquariumConstructionCellInWorkingView({0, 2}, {10, 10}) &&
            !construction::aquariumConstructionCellInWorkingView({21, 10}, {10, 10}) &&
            !construction::aquariumConstructionCellInWorkingView({10, 19}, {10, 10}),
        "construction working view must bound room-grid rendering around the camera");
    construction::AquariumConstructionVisual visual;
    visual.visible = true;
    visual.cells = {{{10, 10}, 0.0f, false}, {{11, 10}, 0.0f, false}};
    visual.cursor = {10, 10};
    visual.state = construction::ConstructionState::Browse;
    visual.navigation_hint = "MOVE CURSOR  WASD DPAD STICK MOUSE";
    const auto browse_mesh = construction::buildAquariumConstructionWorldMesh(visual);
    require(browse_mesh.vertices.size() >= 24 && !browse_mesh.indices.empty(),
        "visible allowed cells did not generate separated fill geometry");

    visual.anchor = geo::GridCell{10, 10};
    visual.draft_cells = {{10, 10}, {11, 10}};
    visual.draft_valid = true;
    visual.state = construction::ConstructionState::DraftReview;
    const auto review_mesh = construction::buildAquariumConstructionWorldMesh(visual);
    require(review_mesh.vertices.size() > browse_mesh.vertices.size(),
        "draft review did not add silhouette and creation gizmos");
    visual.draft_valid = false;
    const auto invalid_mesh = construction::buildAquariumConstructionWorldMesh(visual);
    require(invalid_mesh.vertices.size() > review_mesh.vertices.size(),
        "invalid preview did not add non-color invalid markers");

    geo::TankDesign selected;
    selected.id = "tank_gizmo";
    selected.footprint.origin_cell = {10, 10};
    selected.footprint.width_cells = 3;
    selected.footprint.depth_cells = 3;
    geo::TunnelDesign tunnel;
    tunnel.id = "tunnel_gizmo";
    tunnel.centreline_cells = {{10, 11}, {11, 11}, {12, 11}, {13, 11}};
    selected.tunnels.push_back(tunnel);
    visual.cells.clear();
    for (int row = 10; row <= 12; ++row) {
        for (int column = 10; column <= 12; ++column) {
            visual.cells.push_back({{column, row}, 0.0f, false});
        }
    }
    visual.anchor.reset();
    visual.draft_cells.clear();
    visual.selected_tank = selected;
    visual.selected_cells = construction::tankFootprintCells(selected);
    visual.original_cells = visual.selected_cells;
    visual.locked_cells = {{10, 10}};
    visual.tunnel_portal_cells = {{10, 11}};
    visual.state = construction::ConstructionState::Selected;
    const auto selected_mesh = construction::buildAquariumConstructionWorldMesh(visual);
    require(selected_mesh.vertices.size() > browse_mesh.vertices.size(),
        "selected tank, locked-cell marker, and edit gizmos did not add visible geometry");
    visual.preview_tank = selected;
    require(construction::buildAquariumConstructionDepthPreviewMesh(visual).vertices.empty(),
        "zero-depth tank unexpectedly showed a below-floor outline");
    selected.depth_steps = 6;
    visual.preview_tank = selected;
    const auto height_preview =
        construction::buildAquariumConstructionHeightPreviewMesh(visual);
    require(!height_preview.vertices.empty() &&
            std::any_of(height_preview.vertices.begin(), height_preview.vertices.end(),
                [](const auto& vertex) { return vertex.y >= 64.0f; }),
        "height draft did not expose its actual dotted top outline");
    const auto depth_preview =
        construction::buildAquariumConstructionDepthPreviewMesh(visual);
    require(!depth_preview.vertices.empty() &&
            std::any_of(depth_preview.vertices.begin(), depth_preview.vertices.end(),
                [](const auto& vertex) { return vertex.y <= -48.0f; }),
        "deep tank did not expose its actual dotted bottom outline");

    pr::gameplay::world3d::camera::Gen4CameraPreset preset;
    preset.fov_y_deg = 45.0f;
    preset.near_clip = 0.1f;
    preset.far_clip = 1000.0f;
    pr::gameplay::world3d::camera::Gen4FollowCamera camera(preset);
    camera.setManualPose({168.0f, 180.0f, 340.0f}, 180.0f, -45.0f);
    float screen_x = 0.0f;
    float screen_y = 0.0f;
    float depth = 0.0f;
    require(camera.worldToScreen({179.0f, 0.35f, 176.0f}, 1280, 800,
                screen_x, screen_y, depth),
        "half-cell-offset grid surface did not project into the construction viewport");
    const auto hit = construction::hitTestAquariumConstructionCell(
        visual, camera, static_cast<int>(screen_x), static_cast<int>(screen_y), 1280, 800);
    require(hit && hit->column == 10 && hit->row == 10,
        "rendered half-cell-offset surface and pointer hit target disagree");

    require(camera.worldToScreen({185.0f, 3.65f, 192.0f}, 1280, 800,
                screen_x, screen_y, depth),
        "selected tank move gizmo did not project into the construction viewport");
    const auto move_gizmo = construction::hitTestAquariumConstructionGizmo(
        visual, camera, static_cast<int>(screen_x), static_cast<int>(screen_y), 1280, 800);
    require(move_gizmo && move_gizmo->kind == construction::ConstructionGizmoKind::Move,
        "selected tank centre did not expose a mouse-hit-testable move gizmo");
    require(camera.worldToScreen({199.0f, 3.77f, 192.0f}, 1280, 800,
                screen_x, screen_y, depth),
        "selected tank depth knob did not project into the construction viewport");
    const auto depth_gizmo = construction::hitTestAquariumConstructionGizmo(
        visual, camera, static_cast<int>(screen_x), static_cast<int>(screen_y), 1280, 800);
    require(depth_gizmo && depth_gizmo->kind == construction::ConstructionGizmoKind::Depth,
        "centre knob cluster did not expose a distinct below-floor depth knob");
    require(camera.worldToScreen({168.0f, 3.92f, 184.0f}, 1280, 800,
                screen_x, screen_y, depth),
        "selected tank tunnel portal did not project into the construction viewport");
    const auto portal_gizmo = construction::hitTestAquariumConstructionGizmo(
        visual, camera, static_cast<int>(screen_x), static_cast<int>(screen_y), 1280, 800);
    require(portal_gizmo &&
            portal_gizmo->kind == construction::ConstructionGizmoKind::TunnelPortal &&
            portal_gizmo->portal_cell && portal_gizmo->portal_cell->column == 10 &&
            portal_gizmo->portal_cell->row == 11,
        "glowing tunnel portal was not mouse-hit-testable at its rendered cell");
    require(camera.worldToScreen({200.0f, 4.08f, 184.0f}, 1280, 800,
                screen_x, screen_y, depth),
        "tunnel delete knob did not project into the construction viewport");
    const auto tunnel_delete_gizmo = construction::hitTestAquariumConstructionGizmo(
        visual, camera, static_cast<int>(screen_x), static_cast<int>(screen_y), 1280, 800);
    require(tunnel_delete_gizmo &&
            tunnel_delete_gizmo->kind == construction::ConstructionGizmoKind::TunnelDelete &&
            tunnel_delete_gizmo->tunnel_id == "tunnel_gizmo",
        "tunnel midpoint did not expose its mouse-hit-testable delete knob");
    require(camera.worldToScreen({216.0f, 3.65f, 192.0f}, 1280, 800,
                screen_x, screen_y, depth),
        "selected tank resize gizmo did not project into the construction viewport");
    const auto resize_gizmo = construction::hitTestAquariumConstructionGizmo(
        visual, camera, static_cast<int>(screen_x), static_cast<int>(screen_y), 1280, 800);
    require(resize_gizmo && resize_gizmo->kind == construction::ConstructionGizmoKind::Resize &&
            resize_gizmo->resize_handle == construction::AquariumResizeHandle::East,
        "east tank side did not expose its directional resize gizmo");
    require(camera.worldToScreen({212.464f, 3.77f, 212.464f}, 1280, 800,
                screen_x, screen_y, depth),
        "selected tank corner-radius gizmo did not project into the viewport");
    const auto corner_gizmo = construction::hitTestAquariumConstructionGizmo(
        visual, camera, static_cast<int>(screen_x), static_cast<int>(screen_y), 1280, 800);
    require(corner_gizmo &&
            corner_gizmo->kind == construction::ConstructionGizmoKind::CornerRadius &&
            corner_gizmo->corner_vertex &&
            corner_gizmo->corner_vertex->column == 3 &&
            corner_gizmo->corner_vertex->row == 3,
        "tank corner exposes resize instead of its dedicated roundness knob");

    selected.footprint.shape = geo::FootprintShape::L;
    selected.footprint.width_cells = 6;
    selected.footprint.depth_cells = 6;
    selected.footprint.notch_width_cells = 2;
    selected.footprint.notch_depth_cells = 2;
    visual.preview_tank = selected;
    visual.state = construction::ConstructionState::DraftReview;
    visual.property_draft = true;
    visual.focused_action = construction::ConstructionHudAction::Height;

    const auto hud = construction::aquariumConstructionHudLayout(
        1280, 800, construction::ConstructionState::DraftReview, true);
    require(construction::hitTestAquariumConstructionHud(
                hud, hud.build.x + 2, hud.build.y + 2,
                construction::ConstructionState::DraftReview, true) ==
            construction::ConstructionHudAction::Build,
        "visible Draft Review build control is not hit-testable");
    const auto height_choices = construction::aquariumConstructionPropertyChoices(hud, visual);
    require(height_choices.empty(),
        "minimal construction HUD still exposes the retired height stepper");
    visual.focused_action = construction::ConstructionHudAction::Roundness;
    const auto radius_choices = construction::aquariumConstructionPropertyChoices(hud, visual);
    require(radius_choices.empty(),
        "minimal construction HUD still exposes the retired roundness stepper");
    const auto browse_hud = construction::aquariumConstructionHudLayout(
        1280, 800, construction::ConstructionState::Browse);
    require(construction::hitTestAquariumConstructionHud(
                browse_hud, browse_hud.exit.x + 2, browse_hud.exit.y + 2,
                construction::ConstructionState::Browse) ==
            construction::ConstructionHudAction::Exit,
        "visible Browse finish control is not hit-testable");
    require(construction::hitTestAquariumConstructionHud(
                browse_hud, browse_hud.subtract.x + 2, browse_hud.subtract.y + 2,
                construction::ConstructionState::Browse) ==
            construction::ConstructionHudAction::Subtract,
        "visible minus-mode control is not hit-testable");
    require(construction::hitTestAquariumConstructionHud(
                browse_hud, browse_hud.undo.x - 5,
                browse_hud.undo.y + browse_hud.undo.height / 2,
                construction::ConstructionState::Browse) ==
            construction::ConstructionHudAction::Undo &&
            construction::hitTestAquariumConstructionHud(
                browse_hud, browse_hud.redo.x + browse_hud.redo.width + 5,
                browse_hud.redo.y + browse_hud.redo.height / 2,
                construction::ConstructionState::Browse) ==
            construction::ConstructionHudAction::Redo,
        "undo/redo mouse targets do not include their visible icon edges");
    const auto resize_hud = construction::aquariumConstructionHudLayout(
        1280, 800, construction::ConstructionState::ResizeFootprint);
    require(resize_hud.undo.width == 0 && resize_hud.redo.width == 0,
        "active draft still displays history controls that cannot own input");
    require(construction::hitTestAquariumConstructionHud(
                resize_hud, resize_hud.build.x + 2, resize_hud.build.y + 2,
                construction::ConstructionState::ResizeFootprint) ==
            construction::ConstructionHudAction::Build,
        "visible gesture finish control is not hit-testable");
    require(construction::hitTestAquariumConstructionHud(
                resize_hud, resize_hud.cancel.x + resize_hud.cancel.width / 2,
                resize_hud.cancel.y + resize_hud.cancel.height / 2,
                construction::ConstructionState::ResizeFootprint) ==
            construction::ConstructionHudAction::Cancel,
        "visible gesture cancel control is not hit-testable beside accept");
    const auto selected_hud = construction::aquariumConstructionHudLayout(
        1280, 800, construction::ConstructionState::Selected);
    require(construction::hitTestAquariumConstructionHud(
                selected_hud, selected_hud.remove.x + 2, selected_hud.remove.y + 2,
                construction::ConstructionState::Selected) ==
            construction::ConstructionHudAction::Delete,
        "selected tank delete control is not hit-testable");
    const auto browse_actions = construction::aquariumConstructionHudActions(
        construction::ConstructionState::Browse);
    require(browse_actions.size() == 5 &&
            browse_actions[0] == construction::ConstructionHudAction::Place &&
            browse_actions[1] == construction::ConstructionHudAction::Subtract &&
            browse_actions[2] == construction::ConstructionHudAction::Undo &&
            browse_actions[3] == construction::ConstructionHudAction::Redo &&
            browse_actions[4] == construction::ConstructionHudAction::Exit,
        "minimal browse HUD does not expose mode, history, and finish actions");
    require(construction::defaultAquariumConstructionHudAction(
                construction::ConstructionState::Selected) ==
            construction::ConstructionHudAction::Place,
        "minimal selected-tank focus does not begin on add mode");
    const auto property_actions = construction::aquariumConstructionHudActions(
        construction::ConstructionState::DraftReview, true);
    require(std::find(property_actions.begin(), property_actions.end(),
                construction::ConstructionHudAction::Adjust) == property_actions.end() &&
            property_actions.front() == construction::ConstructionHudAction::Build,
        "property-only review exposes an inoperative footprint-adjust action");
    const auto compact_hud = construction::aquariumConstructionHudLayout(
        640, 480, construction::ConstructionState::Selected);
    const construction::ConstructionHudRect compact_rects[]{
        compact_hud.remove, compact_hud.undo, compact_hud.redo, compact_hud.exit,
    };
    require(std::all_of(std::begin(compact_rects), std::end(compact_rects),
                [](const auto& rect) {
                    return rect.x >= 0 && rect.y >= 0 && rect.x + rect.width <= 640 &&
                        rect.y + rect.height <= 480;
                }),
        "compact construction palette did not wrap inside the viewport");

    const std::string minimum_hint = construction::aquariumConstructionHintForValidation(
        "Expand the tank to at least three cells in both directions");
    require(minimum_hint == "MAKE TANK WIDER" &&
            minimum_hint.find_first_of("0123456789") == std::string::npos,
        "minimum-size feedback should be short and nonnumeric");
    require(construction::aquariumConstructionHintForValidation(
                "Tank footprint overlaps an existing obstacle") == "SPACE IS BLOCKED",
        "overlap feedback should identify blocked space without developer diagnostics");
}

void runtimeFloorCutoutPreservesHalfCellAlignment() {
    pr::gameplay::world3d::SceneConfig scene;
    scene.grid.width = 2;
    scene.grid.height = 2;
    scene.grid.tile_size = 16.0f;
    pr::gameplay::world3d::InteriorFloorCutoutConfig cutout;
    cutout.world_polygon = {{{8.0f, 8.0f}, {24.0f, 8.0f},
        {24.0f, 24.0f}, {8.0f, 24.0f}}};
    scene.interior.floor_cutouts.push_back(std::move(cutout));
    const auto triangles = pr::gameplay::world3d::interiors::
        clipFloorCellAgainstCutouts(scene, 0, 0, 16.0f);
    float area = 0.0f;
    for (const auto& triangle : triangles) {
        area += std::abs(
            (triangle[1].x - triangle[0].x) * (triangle[2].z - triangle[0].z) -
            (triangle[1].z - triangle[0].z) * (triangle[2].x - triangle[0].x)) * 0.5f;
    }
    require(std::abs(area - 192.0f) < 0.01f,
        "world-space floor cutout lost the canonical half-cell offset");

    geo::FootprintDesign shaped;
    shaped.width_cells = 5;
    shaped.depth_cells = 5;
    shaped.subtracted_cells = {{2, 0}, {2, 1}};
    std::vector<std::array<float, 2>> shaped_polygon;
    for (const auto point : geo::footprintBoundaryLocalWorld(shaped, 1)) {
        shaped_polygon.push_back({point.x, point.y});
    }
    const auto pieces = pr::gameplay::world3d::interiors::detail::
        triangulateSimple(shaped_polygon);
    require(!pieces.empty(),
        "rounded exterior-subtracted footprint could not become exact floor cutouts");
}

void constructionCameraTracksTheCursorInAReadableCentreZone() {
    constexpr int kViewportWidth = 400;
    constexpr int kViewportHeight = 250;
    constexpr float kTile = 16.0f;
    pr::gameplay::world3d::camera::Gen4CameraPreset preset;
    preset.fov_y_deg = 25.0f;
    preset.near_clip = 0.1f;
    preset.far_clip = 4000.0f;
    pr::gameplay::world3d::camera::Gen4FollowCamera camera(preset);
    construction::AquariumConstructionCameraTrackingState tracking;
    const auto initial = construction::trackAquariumConstructionCursor(
        tracking,
        24, 18, kTile, 0.0f, preset.fov_y_deg,
        static_cast<float>(kViewportWidth) / static_cast<float>(kViewportHeight),
        -55.0f, {12, 9}, false, 0.0);
    camera.setManualPose(initial.position, initial.yaw_degrees, initial.pitch_degrees);
    float first_x = 0.0f;
    float first_y = 0.0f;
    float first_depth = 0.0f;
    float adjacent_x = 0.0f;
    float adjacent_y = 0.0f;
    float adjacent_depth = 0.0f;
    require(initial.pitch_degrees >= -62.0f && initial.pitch_degrees <= -60.0f &&
            camera.worldToScreen({12.5f * kTile, 0.0f, 9.5f * kTile},
                kViewportWidth, kViewportHeight, first_x, first_y, first_depth) &&
            camera.worldToScreen({13.5f * kTile, 0.0f, 9.5f * kTile},
                kViewportWidth, kViewportHeight, adjacent_x, adjacent_y, adjacent_depth) &&
            adjacent_x - first_x >= 12.0f,
        "normal-style construction camera angle or whole-cell readability regressed");

    const float initial_center_x = tracking.center_x;
    const auto within_zone = construction::trackAquariumConstructionCursor(
        tracking, 24, 18, kTile, 0.0f, preset.fov_y_deg,
        static_cast<float>(kViewportWidth) / static_cast<float>(kViewportHeight),
        -55.0f, {10, 9}, false, 1.0 / 60.0);
    require(std::abs(tracking.center_x - initial_center_x) < 0.001f,
        "camera moved while the cursor remained in its centre zone");
    const float before_panel_z = tracking.center_z;
    construction::trackAquariumConstructionCursor(
        tracking, 24, 18, kTile, 0.0f, preset.fov_y_deg,
        static_cast<float>(kViewportWidth) / static_cast<float>(kViewportHeight),
        -55.0f, {12, 9}, true, 0.25);
    require(tracking.property_panel_visible && tracking.center_z > before_panel_z,
        "opening the property tray did not recompose the tank into the safe view");
    construction::trackAquariumConstructionCursor(
        tracking, 24, 18, kTile, 0.0f, preset.fov_y_deg,
        static_cast<float>(kViewportWidth) / static_cast<float>(kViewportHeight),
        -55.0f, {12, 9}, false, 0.25);
    const auto left_edge = construction::trackAquariumConstructionCursor(
        tracking, 24, 18, kTile, 0.0f, preset.fov_y_deg,
        static_cast<float>(kViewportWidth) / static_cast<float>(kViewportHeight),
        -55.0f, {1, 9}, false, 0.25);
    require(tracking.center_x < initial_center_x &&
            left_edge.position.x < within_zone.position.x,
        "camera did not pan when the cursor crossed the left tracking edge");
    const float left_limit = tracking.center_x;
    for (int frame = 0; frame < 120; ++frame) {
        construction::trackAquariumConstructionCursor(
            tracking, 24, 18, kTile, 0.0f, preset.fov_y_deg,
            static_cast<float>(kViewportWidth) / static_cast<float>(kViewportHeight),
            -55.0f, {1, 1}, false, 1.0 / 60.0);
    }
    require(tracking.center_x <= left_limit && tracking.center_x >= 0.0f &&
            tracking.center_z >= 0.0f,
        "edge tracking escaped the room framing bounds");
}

void loadedPlacementValidationRejectsBoundsAndOverlap() {
    auto document = emptyDocument();
    geo::TankDesign first;
    first.id = "tank_outside";
    first.footprint.origin_cell = {9, 10};
    first.footprint.width_cells = 3;
    first.footprint.depth_cells = 3;
    document.tanks.push_back(first);
    require(!construction::validateAquariumPlacement(
                document, constructionConfig(), {}).empty(),
        "loaded out-of-bounds tank passed map placement validation");
    construction::AquariumConstructionSession session;
    session.configure("aquarium12", constructionConfig(), {}, document);
    require(!session.available(), "invalid loaded aquarium remained editable");

    document.tanks.front().footprint.origin_cell = {10, 10};
    auto second = document.tanks.front();
    second.id = "tank_overlap";
    second.footprint.origin_cell = {12, 12};
    document.tanks.push_back(second);
    require(!construction::validateAquariumPlacement(
                document, constructionConfig(), {}).empty(),
        "overlapping loaded tanks passed placement validation");
}

void storeRoundTripsAndRecoversBackup() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() /
        ("pokemon_resort_aquarium_store_" + std::to_string(nonce));
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{root};
    construction::AquariumDesignStore store(root / "aquarium12.aquarium.json");
    auto document = emptyDocument();
    std::string error;
    require(store.saveTransactionally(document, &error), "initial transactional save failed");
    document.revision = 1;
    require(store.saveTransactionally(document, &error), "second transactional save failed");
    const auto loaded = store.load();
    require(loaded.document && loaded.document->revision == 1,
        "transactional save did not round trip latest revision");
    {
        std::ofstream corrupt(store.primaryPath(), std::ios::trunc);
        corrupt << "{not-json";
    }
    const auto recovered = store.load();
    require(recovered.status == construction::AquariumStoreLoadStatus::RecoveredBackup &&
            recovered.document && recovered.document->revision == 0,
        "invalid primary did not recover the validated backup");
}

void storePreservesNewerDocumentsAndFailedWrites() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() /
        ("pokemon_resort_aquarium_fault_" + std::to_string(nonce));
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{root};
    fs::create_directories(root);
    auto document = emptyDocument();
    construction::AquariumDesignStore store(root / "aquarium12.aquarium.json");
    std::string error;
    require(store.saveTransactionally(document, &error), "fault fixture save failed");
    std::string newer = construction::serializeAquariumDesignCanonical(document);
    const std::string old_version = "\"schemaVersion\": 5";
    const auto version_position = newer.find(old_version);
    require(version_position != std::string::npos, "fault fixture schema version missing");
    newer.replace(version_position, old_version.size(), "\"schemaVersion\": 6");
    {
        std::ofstream primary(store.primaryPath(), std::ios::trunc);
        primary << newer;
    }
    require(store.load().status == construction::AquariumStoreLoadStatus::NewerVersion,
        "newer primary was overwritten by an older backup");

    const fs::path blocker = root / "not-a-directory";
    { std::ofstream file(blocker); file << "block"; }
    construction::AquariumDesignStore blocked_store(blocker / "aquarium.json");
    require(!blocked_store.saveTransactionally(document, &error) && !error.empty(),
        "save fault injection unexpectedly committed through a non-directory path");
    require(store.load().status == construction::AquariumStoreLoadStatus::NewerVersion,
        "failed unrelated save damaged the recoverable newer document");
}

void storeRecoversInterruptedPromotionsAndPreservesNewerArtifacts() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() /
        ("pokemon_resort_aquarium_interrupted_" + std::to_string(nonce));
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{root};
    fs::create_directories(root);
    construction::AquariumDesignStore store(root / "aquarium12.aquarium.json");
    auto document = emptyDocument();

    document.revision = 7;
    {
        std::ofstream temporary(store.temporaryPath(), std::ios::trunc);
        temporary << construction::serializeAquariumDesignCanonical(document);
    }
    auto recovered = store.load();
    require(recovered.status == construction::AquariumStoreLoadStatus::RecoveredTemporary &&
            recovered.document && recovered.document->revision == 7,
        "interrupted first-save temporary document was not recovered");

    fs::rename(store.temporaryPath(), store.previousPath());
    recovered = store.load();
    require(recovered.status == construction::AquariumStoreLoadStatus::RecoveredPrevious &&
            recovered.document && recovered.document->revision == 7,
        "displaced primary from an interrupted fallback promotion was not recovered");

    std::string newer = construction::serializeAquariumDesignCanonical(document);
    const std::string old_version = "\"schemaVersion\": 5";
    const auto version_position = newer.find(old_version);
    require(version_position != std::string::npos,
        "interrupted-save fixture schema version missing");
    newer.replace(version_position, old_version.size(), "\"schemaVersion\": 6");
    {
        std::ofstream temporary(store.temporaryPath(), std::ios::trunc);
        temporary << newer;
    }
    fs::copy_file(store.previousPath(), store.primaryPath());
    require(store.load().status == construction::AquariumStoreLoadStatus::NewerVersion,
        "newer interrupted save artifact was hidden by an older valid primary");
    std::string error;
    require(!store.saveTransactionally(document, &error) && !error.empty(),
        "transactional save overwrote a newer interrupted artifact");
    require(store.load().status == construction::AquariumStoreLoadStatus::NewerVersion,
        "failed save did not preserve the newer interrupted artifact");
}

class FlatQuery final : public pr::gameplay::world3d::characters::CharacterTerrainQuery {
public:
    float tileSize() const override { return 16.0f; }
    bool containsTile(int, int) const override { return true; }
    bool tileBlocked(int x, int y) const override { return x == 1 && y == 1; }
    bool tileIsActualWater(int, int) const override { return false; }
    int tileBaseHeightUnits(int, int) const override { return 0; }
    int tileSpecial(int, int) const override { return 0; }
    float tileWorldHeight(int, int) const override { return 0.0f; }
    bool canTraverseTerrainEdge(int, int, int, int, int, int) const override { return true; }
    pr::gameplay::world3d::terrain::ActorTerrainBinding bindActorStanding(
        int, int, float, float) const override { return {}; }
    pr::gameplay::world3d::terrain::GridStepMotor beginStep(
        int, int, int, int, int, int, int, int) const override { return {}; }
    float actorHeightDuringStep(
        float, float, const pr::gameplay::world3d::terrain::GridStepMotor&, float) const override {
        return 0.0f;
    }
};

void collisionOverlayCombinesStaticAndDynamicCells() {
    auto overlay = construction::AquariumCollisionOverlay(std::make_shared<FlatQuery>());
    overlay.setBlockedCells({{4, 5}, {4, 5}});
    require(overlay.tileBlocked(1, 1), "overlay discarded static collision");
    require(overlay.tileBlocked(4, 5), "overlay did not expose generated collision");
    require(!overlay.tileBlocked(3, 5), "overlay blocked an unrelated cell");
    require(!overlay.canTraverseTerrainEdge(3, 5, 4, 5, 1, 0),
        "overlay allowed traversal into generated collision");
}

class TestPopulationPolicy final : public construction::AquariumPopulationPolicy {
public:
    std::vector<aq::AquariumSwimmerDefinition> populationFor(
        const construction::PlayerTankRuntime& tank,
        const construction::AquariumPopulationContext&,
        std::vector<std::string>*) const override {
        aq::AquariumSwimmerDefinition swimmer;
        swimmer.actor.id = tank.design.id + ":test";
        swimmer.actor.species = "test-species";
        swimmer.movement.species = swimmer.actor.species;
        return {swimmer};
    }
};

void populationPolicyIsReplaceableAndNavigationIsDerived() {
    auto document = emptyDocument();
    geo::TankDesign tank;
    tank.id = "tank_policy";
    tank.footprint.origin_cell = {10, 10};
    tank.footprint.width_cells = 3;
    tank.footprint.depth_cells = 3;
    document.tanks.push_back(tank);
    pr::gameplay::world3d::SceneConfig scene;
    scene.grid.width = 24;
    scene.grid.height = 18;
    scene.grid.tile_size = 16.0f;
    scene.terrain.heights.assign(18, std::vector<std::uint8_t>(24, 0));
    aq::AquariumMapConfig map;
    map.map_id = "aquarium12";
    TestPopulationPolicy policy;
    const auto runtime = construction::buildPlayerAquariumRuntime(
        document, scene, map, fs::path{}, policy);
    require(runtime.tanks.size() == 1 && runtime.simulation_tanks.size() == 1 &&
            runtime.simulation_tanks.front().swimmers.size() == 1,
        "replaceable population policy was not applied once per tank");
    require(runtime.tanks.front().build.navigation.layers.size() == 1,
        "committed rectangle did not derive a navigation volume");
    const auto& navigation = runtime.simulation_tanks.front().navigation;
    require(navigation.valid && navigation.export_units_per_meter == 16.0f &&
            aq::containsPoint(navigation, navigation.suggested_spawns.front()),
        "kernel navigation was not converted into valid simulation-local metres");
    const auto& inspection = runtime.simulation_tanks.front().inspection_camera;
    require(inspection.enabled && inspection.has_framed_inspection_view &&
            inspection.interaction_reach_tiles == 1.5f &&
            inspection.focused_standoff_tiles == 3.5f,
        "player tank did not receive the standard close inspection camera");
    require(runtime.collision_cells.size() == 16,
        "half-cell-installed 3x3 tank did not cover its south/east overlap cells");

    construction::AquariumPopulationContext context;
    context.milotic_model_path = "milotic-aquarium.glbz";
    context.kyogre_model_path = "kyogre-aquarium.glbz";
    const auto testing_policy = construction::makeTestAquariumPopulationPolicy();
    const auto population = testing_policy->populationFor(
        runtime.tanks.front(), context, nullptr);
    require(population.size() == 2U &&
            std::count_if(population.begin(), population.end(), [](const auto& swimmer) {
                return swimmer.actor.species == "milotic" &&
                    swimmer.actor.animation == "walk" &&
                    swimmer.movement.behavior == "wander";
            }) == 2 && population[0].actor.id != population[1].actor.id,
        "first player tank must contain two distinct walking Milotic only");

    context.tank_index = 1;
    const auto second_population = testing_policy->populationFor(
        runtime.tanks.front(), context, nullptr);
    require(second_population.size() == 1U &&
            std::count_if(second_population.begin(), second_population.end(), [](const auto& swimmer) {
                return swimmer.actor.species == "kyogre" &&
                    swimmer.actor.animation == "idle_default" &&
                    swimmer.movement.behavior == "school" &&
                    swimmer.movement.speed_meters_per_second == 0.42f;
            }) == 1,
        "second player tank must contain one continuously roaming idle-swimming Kyogre only");
}

void tunnelGestureCommitsCancelsAndClearsRuntimeCollision() {
    auto document = emptyDocument();
    geo::TankDesign tank;
    tank.id = "tank_tunnel_edit";
    tank.footprint.origin_cell = {11, 11};
    tank.footprint.width_cells = 6;
    tank.footprint.depth_cells = 4;
    document.tanks.push_back(tank);
    construction::AquariumConstructionSession session;
    session.configure("aquarium12", constructionConfig(), {}, document);
    require(session.enter({13, 11}) && session.selectAtCursor(),
        "tunnel fixture tank was not selectable at its north portal");
    const auto portals = session.tunnelPortalCells();
    require(std::any_of(portals.begin(), portals.end(), [](geo::GridCell cell) {
                return cell.column == 13 && cell.row == 11;
            }), "north tunnel portal was not exposed to mouse/controller focus");
    require(session.beginTunnelSelected(), "tunnel route did not begin from a valid portal");
    session.moveCursor(0, 1);
    require(!session.draftValid(), "unfinished tunnel route became committable");
    require(session.cancel() && session.committedDesign().tanks.front().tunnels.empty(),
        "cancelling a tunnel route changed committed tank data");

    session.pointAt({13, 11});
    require(session.beginTunnelSelected(), "second tunnel route did not begin");
    session.moveCursor(0, 1);
    session.moveCursor(0, 1);
    session.moveCursor(0, 1);
    session.moveCursor(0, 1);
    require(session.draftValid() && session.tunnelRouteCells().size() == 5U,
        "straight portal-to-portal route did not become valid");
    require(session.reviewDraft(), "valid tunnel route did not enter review");
    auto candidate = session.prepareCommit();
    require(candidate && candidate->document.tanks.front().tunnels.size() == 1U,
        "valid tunnel route did not prepare one tank-edit command");
    require(session.publish(std::move(*candidate)) && session.undoCount() == 1U,
        "tunnel edit did not publish as one undoable command");
    require(session.adjustCornerRadius({0, 0}, 1) && session.draftValid(),
        "corner clear of the tunnel could not be rounded");
    require(session.cancel(), "safe tunnel-corner rounding draft did not cancel");
    for (int step = 0; step < 5; ++step) {
        require(session.adjustCornerRadius({0, 0}, 1),
            "tunnel-corner rounding fixture stopped before its fitted limit");
    }
    require(!session.draftValid() &&
            session.validationMessage().find("roundness") != std::string::npos,
        "corner rounding that reached the tunnel portal remained committable");
    require(session.cancel(), "conflicting tunnel-corner rounding draft did not cancel");
    require(session.beginMoveSelected(), "tunnel tank move did not begin");
    session.moveCursor(1, 0);
    const auto moved_preview = session.previewTank();
    require(moved_preview &&
            moved_preview->tunnels.front().centreline_cells.front().column == 14,
        "moving a tank did not keep its authored tunnel aligned");
    require(session.cancel(), "tunnel tank move draft did not cancel");

    pr::gameplay::world3d::SceneConfig scene;
    scene.grid.width = 24;
    scene.grid.height = 18;
    scene.grid.tile_size = 16.0f;
    scene.terrain.heights.assign(18, std::vector<std::uint8_t>(24, 0));
    aq::AquariumMapConfig map;
    map.map_id = "aquarium12";
    TestPopulationPolicy policy;
    const auto runtime = construction::buildPlayerAquariumRuntime(
        session.committedDesign(), scene, map, fs::path{}, policy);
    require(runtime.tanks.size() == 1U &&
            runtime.tanks.front().build.navigation.dry_volumes.size() == 1U,
        "committed tunnel did not reach the runtime navigation set");
    require(std::none_of(runtime.collision_cells.begin(), runtime.collision_cells.end(),
            [](geo::GridCell cell) {
                return cell.column == 13 && cell.row >= 11 && cell.row <= 15;
            }), "verified tunnel corridor remained blocked on the walking grid");
    require(std::any_of(runtime.collision_cells.begin(), runtime.collision_cells.end(),
            [](geo::GridCell cell) {
                return cell.column == 12 && cell.row == 13;
            }) && std::any_of(runtime.collision_cells.begin(), runtime.collision_cells.end(),
            [](geo::GridCell cell) {
                return cell.column == 14 && cell.row == 13;
            }), "tunnel collision cleared a shoulder lane instead of only its centreline");

    session.pointAt({13, 13});
    const auto tunnel_id = session.tunnelIdAtCursor();
    require(tunnel_id.has_value(),
        "selected tunnel centreline did not expose its tunnel ID");
    if (!session.beginRemoveTunnelSelected(*tunnel_id)) {
        throw std::runtime_error(
            "selected tunnel could not begin an undoable removal: " +
            session.validationMessage());
    }
    auto removal = session.prepareCommit();
    require(removal && removal->document.tanks.front().tunnels.empty() &&
            session.publish(std::move(*removal)),
        "tunnel delete knob did not publish a normal tank edit");
    auto undo = session.prepareUndo();
    require(undo && undo->document.tanks.front().tunnels.size() == 1U &&
            session.publish(std::move(*undo)),
        "tunnel removal was not reversible through construction history");
}

void connectedTunnelGesturesCreateThreeAndFourExits() {
    auto document = emptyDocument();
    geo::TankDesign tank;
    tank.id = "tank_tunnel_network";
    tank.footprint.origin_cell = {10, 10};
    tank.footprint.width_cells = 8;
    tank.footprint.depth_cells = 5;
    document.tanks.push_back(tank);
    construction::AquariumConstructionSession session;
    session.configure("aquarium12", constructionConfig(), {}, document);
    require(session.enter({10, 12}) && session.selectAtCursor(),
        "tunnel-network tank was not selectable");

    require(session.beginTunnelSelected(), "tunnel-network trunk did not begin");
    session.pointAt({18, 12});
    require(session.draftValid() && session.reviewDraft(),
        "wall-to-wall tunnel trunk was not valid");
    auto trunk = session.prepareCommit();
    require(trunk && session.publish(std::move(*trunk)),
        "wall-to-wall tunnel trunk did not publish");

    session.pointAt({14, 10});
    require(session.beginTunnelSelected(), "three-exit branch did not begin");
    session.pointAt({14, 12});
    if (!session.draftValid() || !session.reviewDraft()) {
        throw std::runtime_error("route ending on an existing tunnel did not form a T-junction: " +
            session.validationMessage());
    }
    auto tee = session.prepareCommit();
    require(tee && session.publish(std::move(*tee)) &&
            session.committedDesign().tanks.front().tunnels.size() == 2U,
        "three-exit tunnel network did not publish");

    session.pointAt({14, 15});
    require(session.beginTunnelSelected(), "fourth tunnel exit did not begin");
    session.pointAt({14, 12});
    require(session.draftValid() && session.reviewDraft(),
        "second branch did not connect to the shared junction");
    auto crossing = session.prepareCommit();
    require(crossing && session.publish(std::move(*crossing)) &&
            session.committedDesign().tanks.front().tunnels.size() == 3U,
        "four-exit tunnel network did not publish");

    geo::AquariumBuildRequest request;
    request.tank = session.committedDesign().tanks.front();
    const auto result = geo::buildAquarium(request);
    require(result.validation.valid() &&
            result.collision.dry_corridor_cells.size() == 14U,
        "connected tunnel exits did not share one dry crossing cell");
}

void resourceGenerationRejectsCandidatesWithoutTouchingActiveResources() {
    struct FakeResource { int id = 0; };
    aq::rendering::AquariumResourceGeneration<FakeResource> owner;
    std::vector<int> destroyed;
    const auto destroy = [&](FakeResource& resource) { destroyed.push_back(resource.id); };
    require(owner.publish({{1}, {2}}, true, destroy), "initial fake GPU generation did not publish");
    require(owner.generation() == 1 && owner.active().size() == 2,
        "initial fake GPU generation metadata is wrong");
    require(!owner.publish({{3}, {4}}, false, destroy),
        "invalid fake GPU generation unexpectedly published");
    require(owner.generation() == 1 && owner.active()[0].id == 1 &&
            destroyed.size() == 2 && destroyed[0] == 3 && destroyed[1] == 4,
        "candidate rejection changed active resources or leaked candidate resources");
    require(owner.stage({{5}}, true, destroy), "replacement fake GPU generation did not stage");
    require(owner.generation() == 1 && owner.active().size() == 2,
        "staging changed the active fake GPU generation before publication");
    owner.discardStaged(destroy);
    require(owner.generation() == 1 && owner.active().size() == 2 && destroyed.back() == 5,
        "discarding a staged generation changed active resources or leaked the candidate");
    require(owner.stage({{6}}, true, destroy) && owner.publishStaged(destroy),
        "replacement fake GPU generation failed");
    require(owner.generation() == 2 && owner.active().size() == 1 && owner.active()[0].id == 6,
        "replacement did not publish atomically");
    require(destroyed.size() == 5 && destroyed[3] == 1 && destroyed[4] == 2,
        "replacement did not retire the previous resource generation");

    destroyed.clear();
    require(owner.stage({{7}}, true, destroy) && owner.publishStaged(destroy, 3),
        "delayed-retirement fake GPU generation failed");
    require(owner.active()[0].id == 7 && owner.retiredResourceCount() == 1 && destroyed.empty(),
        "publication did not retain the old generation for its safety window");
    owner.advanceRetirements(destroy);
    owner.advanceRetirements(destroy);
    require(owner.retiredResourceCount() == 1 && destroyed.empty(),
        "old generation retired before the configured frame delay");
    owner.advanceRetirements(destroy);
    require(owner.retiredResourceCount() == 0 && destroyed.size() == 1 && destroyed[0] == 6,
        "old generation was not destroyed at the end of the safety window");

    require(owner.stage({{8}}, true, destroy) && owner.publishStaged(destroy, 3),
        "clear-with-retirement fixture did not publish");
    owner.clear(destroy);
    require(owner.active().empty() && owner.retiredResourceCount() == 0 &&
            std::find(destroyed.begin(), destroyed.end(), 7) != destroyed.end() &&
            std::find(destroyed.begin(), destroyed.end(), 8) != destroyed.end(),
        "shutdown did not destroy both active and delayed GPU generations");
}

void resourceGenerationStressRetiresEveryHandleExactlyOnce() {
    struct FakeResource { int id = 0; };
    aq::rendering::AquariumResourceGeneration<FakeResource> owner;
    std::vector<int> destroyed;
    const auto destroy = [&](FakeResource& resource) { destroyed.push_back(resource.id); };
    constexpr int kGenerations = 256;
    for (int generation = 0; generation < kGenerations; ++generation) {
        require(owner.stage({{generation}}, true, destroy) &&
                owner.publishStaged(destroy, 3),
            "resource stress fixture could not publish a valid generation");
        owner.advanceRetirements(destroy);
        require(owner.active().size() == 1 && owner.active()[0].id == generation,
            "resource stress publication did not leave exactly one active generation");
        require(owner.retiredResourceCount() <= 2,
            "resource retirement queue grew beyond its bounded safety window");
    }
    owner.clear(destroy);
    std::sort(destroyed.begin(), destroyed.end());
    require(destroyed.size() == kGenerations,
        "resource stress shutdown leaked or double-destroyed a handle");
    for (int id = 0; id < kGenerations; ++id) {
        require(destroyed[static_cast<std::size_t>(id)] == id,
            "resource stress shutdown did not destroy each handle exactly once");
    }
}

} // namespace

int main() {
    try {
        const auto run = [](const char* name, auto test) {
            std::cerr << "running " << name << '\n';
            try { test(); }
            catch (const std::exception& error) {
                throw std::runtime_error(std::string(name) + ": " + error.what());
            }
        };
        run("stateMachinePreservesCommittedDataOnCancel", stateMachinePreservesCommittedDataOnCancel);
        run("drawnRectanglesMergeOnlyWhenTheyTouch", drawnRectanglesMergeOnlyWhenTheyTouch);
        run("directPaintGesturesCommitAsSingleUndoableCommands", directPaintGesturesCommitAsSingleUndoableCommands);
        run("exteriorPaintMergesAndErasesMultipleTanksUndoably", exteriorPaintMergesAndErasesMultipleTanksUndoably);
        run("draftReviewIsNonMutatingAndAdjustmentIsReversible", draftReviewIsNonMutatingAndAdjustmentIsReversible);
        run("editingHistoryIsTransactionalStableAndStaleSafe", editingHistoryIsTransactionalStableAndStaleSafe);
        run("subtractEditingCommitsUndoablyAndRejectsEnclosedCuts", subtractEditingCommitsUndoablyAndRejectsEnclosedCuts);
        run("discreteShapePropertiesAreDraftedAndTransactional", discreteShapePropertiesAreDraftedAndTransactional);
        run("invalidMoveAndResizeCannotPrepareCommands", invalidMoveAndResizeCannotPrepareCommands);
        run("everyEditingStateCancelsWithoutChangingTheDocument", everyEditingStateCancelsWithoutChangingTheDocument);
        run("newerAsyncOperationInvalidatesEveryOlderCandidate", newerAsyncOperationInvalidatesEveryOlderCandidate);
        run("cursorChoosesEveryControllerResizeHandle", cursorChoosesEveryControllerResizeHandle);
        run("authoredObstaclesRemainVisibleAtBuildZoneEdges", authoredObstaclesRemainVisibleAtBuildZoneEdges);
        run("constructionVisualBuildsYellowCellsGizmosAndCanonicalHitTargets", constructionVisualBuildsYellowCellsGizmosAndCanonicalHitTargets);
        run("runtimeFloorCutoutPreservesHalfCellAlignment", runtimeFloorCutoutPreservesHalfCellAlignment);
        run("constructionCameraTracksTheCursorInAReadableCentreZone", constructionCameraTracksTheCursorInAReadableCentreZone);
        run("loadedPlacementValidationRejectsBoundsAndOverlap", loadedPlacementValidationRejectsBoundsAndOverlap);
        run("storeRoundTripsAndRecoversBackup", storeRoundTripsAndRecoversBackup);
        run("storePreservesNewerDocumentsAndFailedWrites", storePreservesNewerDocumentsAndFailedWrites);
        run("storeRecoversInterruptedPromotionsAndPreservesNewerArtifacts", storeRecoversInterruptedPromotionsAndPreservesNewerArtifacts);
        run("collisionOverlayCombinesStaticAndDynamicCells", collisionOverlayCombinesStaticAndDynamicCells);
        run("populationPolicyIsReplaceableAndNavigationIsDerived", populationPolicyIsReplaceableAndNavigationIsDerived);
        run("tunnelGestureCommitsCancelsAndClearsRuntimeCollision", tunnelGestureCommitsCancelsAndClearsRuntimeCollision);
        run("connectedTunnelGesturesCreateThreeAndFourExits", connectedTunnelGesturesCreateThreeAndFourExits);
        run("resourceGenerationRejectsCandidatesWithoutTouchingActiveResources", resourceGenerationRejectsCandidatesWithoutTouchingActiveResources);
        run("resourceGenerationStressRetiresEveryHandleExactlyOnce", resourceGenerationStressRetiresEveryHandleExactlyOnce);
        std::cout << "aquarium_runtime_tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "aquarium_runtime_tests: " << error.what() << '\n';
        return 1;
    }
}
