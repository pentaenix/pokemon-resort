#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"

#include <optional>
#include <string>
#include <vector>

namespace pr::gameplay::world3d::interactions {

enum class InteractionTargetKind {
    Character,
    Pokemon,
};

enum class InteractionActionKind {
    FacePlayer,
    FaceDirection,
    FaceAway,
    TextLiteral,
    TextFree,
    Cry,
    Emoticon,
    PokemonInteractionSession,
};

struct InteractionAction {
    InteractionActionKind kind = InteractionActionKind::TextFree;
    FacingDirection direction = FacingDirection::South;
    std::string value;
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

InteractionBehaviorCatalog loadInteractionBehaviorCatalog(const std::string& project_root);
InteractionBehaviorCatalog defaultInteractionBehaviorCatalog();
/// Returns no activity for profiles that intentionally use behavior-only interactions.
std::optional<std::string> interactionActivityIdForPokemonSize(const std::string& pokemon_size);
FacingDirection oppositeFacing(FacingDirection direction);
FacingDirection facingTowardTiles(int from_tx, int from_ty, int to_tx, int to_ty, FacingDirection fallback);

} // namespace pr::gameplay::world3d::interactions
