#include "gameplay/world3d/aquarium/construction/AquariumCollisionOverlay.hpp"
#include "gameplay/world3d/aquarium/construction/AquariumConstructionCamera.hpp"
#include "gameplay/world3d/aquarium/construction/AquariumConstructionSession.hpp"
#include "gameplay/world3d/aquarium/construction/AquariumConstructionVisual.hpp"
#include "gameplay/world3d/aquarium/construction/AquariumDesignStore.hpp"
#include "gameplay/world3d/aquarium/construction/AquariumPlayerRuntime.hpp"
#include "gameplay/world3d/aquarium/rendering/AquariumResourceGeneration.hpp"

#include <chrono>
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
    require(session.beginRectangle(), "protected-cell rectangle draft did not begin");
    session.moveCursor(2, 2);
    require(!session.draftValid(),
        "tank placement could trap the player inside its committed collision shell");
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

void stateMachineBuildsAndRejectsOverlap() {
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
    require(session.beginRectangle(), "overlap draft did not begin");
    session.moveCursor(2, 2);
    require(session.reviewDraft(), "overlap draft did not enter review");
    require(!session.draftValid() && !session.prepareCommit(),
        "overlapping tank was allowed to commit");
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
            session.adjustTankProperty(construction::AquariumTankProperty::Roundness, 1) &&
            session.adjustTankProperty(construction::AquariumTankProperty::Rotation, 1) &&
            session.adjustTankProperty(construction::AquariumTankProperty::NotchWidth, -1) &&
            session.adjustTankProperty(construction::AquariumTankProperty::NotchDepth, -1),
        "discrete property controls did not update one shared draft");
    const auto preview = session.previewTank();
    require(preview && preview->footprint.shape == geo::FootprintShape::L &&
            preview->height_steps == 9 && preview->corner_radius_steps == 1 &&
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
    construction::AquariumConstructionVisual visual;
    visual.visible = true;
    visual.cells = {{{10, 10}, 0.0f, false}, {{11, 10}, 0.0f, false}};
    visual.cursor = {10, 10};
    visual.state = construction::ConstructionState::Browse;
    visual.navigation_hint = "MOVE CURSOR  WASD DPAD STICK MOUSE";
    const auto browse_mesh = construction::buildAquariumConstructionWorldMesh(visual);
    require(browse_mesh.vertices.size() >= 40 && !browse_mesh.indices.empty(),
        "visible allowed cells did not generate filled grid and border geometry");

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
    visual.state = construction::ConstructionState::Selected;
    const auto selected_mesh = construction::buildAquariumConstructionWorldMesh(visual);
    require(selected_mesh.vertices.size() > browse_mesh.vertices.size(),
        "selected tank, locked-cell marker, and edit gizmos did not add visible geometry");

    pr::gameplay::world3d::camera::Gen4CameraPreset preset;
    preset.fov_y_deg = 45.0f;
    preset.near_clip = 0.1f;
    preset.far_clip = 1000.0f;
    pr::gameplay::world3d::camera::Gen4FollowCamera camera(preset);
    camera.setManualPose({168.0f, 180.0f, 340.0f}, 180.0f, -45.0f);
    float screen_x = 0.0f;
    float screen_y = 0.0f;
    float depth = 0.0f;
    require(camera.worldToScreen({168.0f, 0.35f, 168.0f}, 1280, 800,
                screen_x, screen_y, depth),
        "canonical cell centre did not project into the construction viewport");
    const auto hit = construction::hitTestAquariumConstructionCell(
        visual, camera, static_cast<int>(screen_x), static_cast<int>(screen_y), 1280, 800);
    require(hit && hit->column == 10 && hit->row == 10,
        "rendered canonical cell and pointer hit target disagree");

    require(camera.worldToScreen({184.0f, 0.72f, 184.0f}, 1280, 800,
                screen_x, screen_y, depth),
        "selected tank move gizmo did not project into the construction viewport");
    const auto move_gizmo = construction::hitTestAquariumConstructionGizmo(
        visual, camera, static_cast<int>(screen_x), static_cast<int>(screen_y), 1280, 800);
    require(move_gizmo && move_gizmo->kind == construction::ConstructionGizmoKind::Move,
        "selected tank centre did not expose a mouse-hit-testable move gizmo");
    require(camera.worldToScreen({208.0f, 0.72f, 208.0f}, 1280, 800,
                screen_x, screen_y, depth),
        "selected tank resize gizmo did not project into the construction viewport");
    const auto resize_gizmo = construction::hitTestAquariumConstructionGizmo(
        visual, camera, static_cast<int>(screen_x), static_cast<int>(screen_y), 1280, 800);
    require(resize_gizmo && resize_gizmo->kind == construction::ConstructionGizmoKind::Resize &&
            resize_gizmo->resize_handle == construction::AquariumResizeHandle::SouthEast,
        "south-east tank corner did not expose its directional resize gizmo");

    selected.footprint.shape = geo::FootprintShape::L;
    selected.footprint.width_cells = 6;
    selected.footprint.depth_cells = 6;
    selected.footprint.notch_width_cells = 2;
    selected.footprint.notch_depth_cells = 2;
    visual.preview_tank = selected;
    visual.state = construction::ConstructionState::DraftReview;
    visual.property_draft = true;
    visual.focused_action = construction::ConstructionHudAction::Shape;

    const auto hud = construction::aquariumConstructionHudLayout(
        1280, 800, construction::ConstructionState::DraftReview, true);
    require(construction::hitTestAquariumConstructionHud(
                hud, hud.build.x + 2, hud.build.y + 2,
                construction::ConstructionState::DraftReview, true) ==
            construction::ConstructionHudAction::Build,
        "visible Draft Review build control is not hit-testable");
    const auto shape_choices = construction::aquariumConstructionPropertyChoices(hud, visual);
    require(shape_choices.size() == 3U && shape_choices[1].selected &&
            construction::hitTestAquariumConstructionHud(
                hud, visual, shape_choices[2].rect.x + 2, shape_choices[2].rect.y + 2).value == 2,
        "shape silhouettes are not explicit, selected, and mouse-hit-testable");
    visual.focused_action = construction::ConstructionHudAction::Height;
    const auto height_choices = construction::aquariumConstructionPropertyChoices(hud, visual);
    require(height_choices.size() == 9U,
        "height tray does not expose every nonnumeric discrete layer choice");
    visual.focused_action = construction::ConstructionHudAction::Roundness;
    const auto radius_choices = construction::aquariumConstructionPropertyChoices(hud, visual);
    require(radius_choices.size() >= 2U && radius_choices.size() <= 5U &&
            std::any_of(radius_choices.begin(), radius_choices.end(),
                [&](const auto& choice) {
                    return choice.value == selected.corner_radius_steps && choice.selected;
                }),
        "roundness tray did not keep approachable presets and the exact current value");
    visual.focused_action = construction::ConstructionHudAction::NotchWidth;
    const auto inset_choices = construction::aquariumConstructionPropertyChoices(hud, visual);
    require(inset_choices.size() == 3U && inset_choices[0].value == 1 &&
            inset_choices[1].value == 2 && inset_choices[1].selected &&
            inset_choices[2].value == 3,
        "shape inset tray is not a bounded previous/current/next stepper");
    require(!hud.safe_world.contains(hud.property_panel.x + 2, hud.property_panel.y + 2) &&
            construction::aquariumConstructionHudContainsUi(
                hud, hud.property_panel.x + 2, hud.property_panel.y + 2),
        "property tray can leak pointer input into the construction world");
    const auto browse_hud = construction::aquariumConstructionHudLayout(
        1280, 800, construction::ConstructionState::Browse);
    require(construction::hitTestAquariumConstructionHud(
                browse_hud, browse_hud.place.x + 2, browse_hud.place.y + 2,
                construction::ConstructionState::Browse) ==
            construction::ConstructionHudAction::Place,
        "visible Browse place control is not hit-testable");
    const auto resize_hud = construction::aquariumConstructionHudLayout(
        1280, 800, construction::ConstructionState::ResizeFootprint);
    require(construction::hitTestAquariumConstructionHud(
                resize_hud, resize_hud.review.x + 2, resize_hud.review.y + 2,
                construction::ConstructionState::ResizeFootprint) ==
            construction::ConstructionHudAction::Review,
        "visible Resize review control is not hit-testable");
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
            browse_actions.front() == construction::ConstructionHudAction::Place &&
            browse_actions[2] == construction::ConstructionHudAction::Undo &&
            browse_actions[3] == construction::ConstructionHudAction::Redo,
        "browse palette does not expose controller-reachable place/select/history actions");
    require(construction::defaultAquariumConstructionHudAction(
                construction::ConstructionState::Selected) ==
            construction::ConstructionHudAction::Move,
        "selected-tank controller focus does not begin on the move gizmo action");
    const auto property_actions = construction::aquariumConstructionHudActions(
        construction::ConstructionState::DraftReview, true);
    require(std::find(property_actions.begin(), property_actions.end(),
                construction::ConstructionHudAction::Adjust) == property_actions.end() &&
            property_actions.front() == construction::ConstructionHudAction::Build,
        "property-only review exposes an inoperative footprint-adjust action");
    const auto compact_hud = construction::aquariumConstructionHudLayout(
        640, 480, construction::ConstructionState::Selected);
    const construction::ConstructionHudRect compact_rects[]{
        compact_hud.move, compact_hud.resize, compact_hud.shape, compact_hud.height,
        compact_hud.roundness, compact_hud.rotate, compact_hud.notch_width,
        compact_hud.notch_depth, compact_hud.remove, compact_hud.undo,
        compact_hud.redo, compact_hud.done,
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
    const std::string old_version = "\"schemaVersion\": 1";
    const auto version_position = newer.find(old_version);
    require(version_position != std::string::npos, "fault fixture schema version missing");
    newer.replace(version_position, old_version.size(), "\"schemaVersion\": 2");
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
    std::vector<aq::AquariumPokemonActor> populationFor(
        const construction::PlayerTankRuntime& tank,
        const construction::AquariumPopulationContext&,
        std::vector<std::string>*) const override {
        aq::AquariumPokemonActor actor;
        actor.id = tank.design.id + ":test";
        actor.species = "test-species";
        return {actor};
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
    require(runtime.tanks.size() == 1 && runtime.actors.size() == 1,
        "replaceable population policy was not applied once per tank");
    require(runtime.tanks.front().build.navigation.layers.size() == 1,
        "committed rectangle did not derive a navigation volume");
    require(runtime.collision_cells.size() == 8,
        "committed 3x3 rectangle did not derive perimeter collision");
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
        run("stateMachineBuildsAndRejectsOverlap", stateMachineBuildsAndRejectsOverlap);
        run("draftReviewIsNonMutatingAndAdjustmentIsReversible", draftReviewIsNonMutatingAndAdjustmentIsReversible);
        run("editingHistoryIsTransactionalStableAndStaleSafe", editingHistoryIsTransactionalStableAndStaleSafe);
        run("discreteShapePropertiesAreDraftedAndTransactional", discreteShapePropertiesAreDraftedAndTransactional);
        run("invalidMoveAndResizeCannotPrepareCommands", invalidMoveAndResizeCannotPrepareCommands);
        run("everyEditingStateCancelsWithoutChangingTheDocument", everyEditingStateCancelsWithoutChangingTheDocument);
        run("newerAsyncOperationInvalidatesEveryOlderCandidate", newerAsyncOperationInvalidatesEveryOlderCandidate);
        run("cursorChoosesEveryControllerResizeHandle", cursorChoosesEveryControllerResizeHandle);
        run("authoredObstaclesRemainVisibleAtBuildZoneEdges", authoredObstaclesRemainVisibleAtBuildZoneEdges);
        run("constructionVisualBuildsYellowCellsGizmosAndCanonicalHitTargets", constructionVisualBuildsYellowCellsGizmosAndCanonicalHitTargets);
        run("constructionCameraTracksTheCursorInAReadableCentreZone", constructionCameraTracksTheCursorInAReadableCentreZone);
        run("loadedPlacementValidationRejectsBoundsAndOverlap", loadedPlacementValidationRejectsBoundsAndOverlap);
        run("storeRoundTripsAndRecoversBackup", storeRoundTripsAndRecoversBackup);
        run("storePreservesNewerDocumentsAndFailedWrites", storePreservesNewerDocumentsAndFailedWrites);
        run("collisionOverlayCombinesStaticAndDynamicCells", collisionOverlayCombinesStaticAndDynamicCells);
        run("populationPolicyIsReplaceableAndNavigationIsDerived", populationPolicyIsReplaceableAndNavigationIsDerived);
        run("resourceGenerationRejectsCandidatesWithoutTouchingActiveResources", resourceGenerationRejectsCandidatesWithoutTouchingActiveResources);
        std::cout << "aquarium_runtime_tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "aquarium_runtime_tests: " << error.what() << '\n';
        return 1;
    }
}
