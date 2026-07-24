#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"
#include "gameplay/world3d/scripts/OverworldScript.hpp"

#include <optional>
#include <string>
#include <vector>

namespace pr::gameplay::world3d::interactions {

enum class InteractionTargetKind {
    Character,
    Pokemon,
};

enum class NpcInteractionMode { DirectDialogue, Scripted };

enum class InteractionActionKind {
    FacePlayer,
    FaceDirection,
    FaceAway,
    TextLiteral,
    TextFree,
    Cry,
    Emoticon,
    PokemonInteractionSession,
    Jump,
    Wait,
    DisableAttend,
    EnableAttend,
    ExitInteraction,
};

struct InteractionAction {
    InteractionActionKind kind = InteractionActionKind::TextFree;
    FacingDirection direction = FacingDirection::South;
    std::string value;
    double duration_seconds = 0.0;
    int height_pixels = 0;
};

struct InteractionBehavior {
    std::string id;
    std::vector<InteractionTargetKind> gates;
    std::vector<InteractionAction> actions;
};

struct InteractionBehaviorCatalog {
    std::vector<InteractionBehavior> behaviors;
};

class InteractionSequenceController {
public:
    void begin(InteractionTargetKind target_kind, const InteractionBehaviorCatalog& catalog);
    void begin(InteractionTargetKind target_kind, const InteractionBehavior& behavior);
    bool active() const { return active_; }
    bool complete() const { return !active_; }
    InteractionTargetKind targetKind() const { return target_kind_; }
    const InteractionAction* currentAction() const;
    void advance();
    void cancel();

private:
    InteractionTargetKind target_kind_ = InteractionTargetKind::Character;
    std::vector<InteractionAction> actions_;
    std::size_t index_ = 0;
    bool active_ = false;
};

struct InteractionSourceRequest {
    InteractionTargetKind target_kind = InteractionTargetKind::Character;
    NpcInteractionMode npc_mode = NpcInteractionMode::DirectDialogue;
    std::optional<std::string> runtime_script_id;
    scripts::ScriptContext script_context;
};

struct InteractionSourceResolution {
    InteractionBehavior behavior;
    bool entered_script_path = false;
    bool used_fallback = false;
    std::string script_id;
};

NpcInteractionMode npcInteractionModeFromMetadata(const std::string& value);
InteractionSourceResolution resolveInteractionSource(
    const scripts::ScriptCatalog& catalog,
    const InteractionSourceRequest& request,
    scripts::ScriptCooldowns& cooldowns,
    double now_seconds,
    std::mt19937& rng);

InteractionBehaviorCatalog loadInteractionBehaviorCatalog(const std::string& project_root);
InteractionBehaviorCatalog defaultInteractionBehaviorCatalog();
/// Returns no activity for profiles that intentionally use behavior-only interactions.
std::optional<std::string> interactionActivityIdForPokemonSize(const std::string& pokemon_size);
FacingDirection oppositeFacing(FacingDirection direction);
FacingDirection facingTowardTiles(int from_tx, int from_ty, int to_tx, int to_ty, FacingDirection fallback);

} // namespace pr::gameplay::world3d::interactions
