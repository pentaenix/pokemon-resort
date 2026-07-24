#include "gameplay/world3d/interactions/InteractionSequence.hpp"

#include "gameplay/world3d/scripts/OverworldScript.hpp"

#include "core/config/Json.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <utility>

namespace pr::gameplay::world3d::interactions {

namespace fs = std::filesystem;

namespace {

std::string stringOr(const JsonValue* value, const std::string& fallback) {
    return value && value->isString() ? value->asString() : fallback;
}

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

InteractionTargetKind gateFromString(const std::string& value) {
    const std::string lower = lowerAscii(value);
    return lower == "pokemon" ? InteractionTargetKind::Pokemon : InteractionTargetKind::Character;
}

bool behaviorMatches(const InteractionBehavior& behavior, InteractionTargetKind target_kind) {
    if (behavior.gates.empty()) return true;
    return std::find(behavior.gates.begin(), behavior.gates.end(), target_kind) != behavior.gates.end();
}

InteractionBehavior defaultBehavior(InteractionTargetKind kind) {
    return defaultInteractionBehaviorCatalog().behaviors[kind == InteractionTargetKind::Pokemon ? 0U : 1U];
}

bool usableInteractionScript(const scripts::OverworldScript& script) {
    if (!script.valid || script.kind != scripts::ScriptKind::Interaction || script.actions.empty()) return false;
    if (!script.trigger.empty() && scripts::normalizeScriptTag(script.trigger) != "ACCEPT") return false;
    for (const scripts::ScriptAction& action : script.actions) {
        if (action.kind == scripts::ScriptActionKind::Cry || action.kind == scripts::ScriptActionKind::Emoticon) return false;
    }
    return true;
}

InteractionBehavior behaviorFromScript(const scripts::OverworldScript& script) {
    InteractionBehavior behavior{};
    behavior.id = script.id;
    for (const std::string& gate : script.target_gates) behavior.gates.push_back(gateFromString(gate));
    for (const scripts::ScriptAction& source : script.actions) {
        InteractionAction action{};
        switch (source.kind) {
            case scripts::ScriptActionKind::Face:
                action.kind = source.value == "FACE_PLAYER" ? InteractionActionKind::FacePlayer :
                    (source.value == "FACE_AWAY" ? InteractionActionKind::FaceAway : InteractionActionKind::FaceDirection);
                action.direction = source.direction;
                break;
            case scripts::ScriptActionKind::TextLiteral: action.kind = InteractionActionKind::TextLiteral; action.value = source.value; break;
            case scripts::ScriptActionKind::TextFree: action.kind = InteractionActionKind::TextFree; break;
            case scripts::ScriptActionKind::PokemonInteractionSession: action.kind = InteractionActionKind::PokemonInteractionSession; break;
            case scripts::ScriptActionKind::Jump: action.kind = InteractionActionKind::Jump; action.height_pixels = source.height_pixels; break;
            case scripts::ScriptActionKind::Wait: action.kind = InteractionActionKind::Wait; action.duration_seconds = source.duration_seconds; break;
            case scripts::ScriptActionKind::DisableAttend: action.kind = InteractionActionKind::DisableAttend; break;
            case scripts::ScriptActionKind::EnableAttend: action.kind = InteractionActionKind::EnableAttend; break;
            case scripts::ScriptActionKind::ExitInteraction: action.kind = InteractionActionKind::ExitInteraction; break;
            case scripts::ScriptActionKind::Cry: action.kind = InteractionActionKind::Cry; break;
            case scripts::ScriptActionKind::Emoticon: action.kind = InteractionActionKind::Emoticon; break;
            default: continue;
        }
        behavior.actions.push_back(std::move(action));
    }
    return behavior;
}

FacingDirection directionFromString(const std::string& value, FacingDirection fallback) {
    const std::string lower = lowerAscii(value);
    if (lower == "north" || lower == "up") return FacingDirection::North;
    if (lower == "south" || lower == "down") return FacingDirection::South;
    if (lower == "east" || lower == "right") return FacingDirection::East;
    if (lower == "west" || lower == "left") return FacingDirection::West;
    return fallback;
}

InteractionActionKind actionKindFromString(const std::string& value) {
    const std::string lower = lowerAscii(value);
    if (lower == "faceplayer") return InteractionActionKind::FacePlayer;
    if (lower == "facedirection" || lower == "face") return InteractionActionKind::FaceDirection;
    if (lower == "faceaway") return InteractionActionKind::FaceAway;
    if (lower == "textliteral") return InteractionActionKind::TextLiteral;
    if (lower == "textfree" || lower == "text") return InteractionActionKind::TextFree;
    if (lower == "cry") return InteractionActionKind::Cry;
    if (lower == "emoticon") return InteractionActionKind::Emoticon;
    if (lower == "pokemoninteractionsession") return InteractionActionKind::PokemonInteractionSession;
    return InteractionActionKind::TextFree;
}

InteractionBehavior parseBehavior(const JsonValue& item) {
    InteractionBehavior behavior{};
    if (!item.isObject()) return behavior;
    behavior.id = stringOr(item.get("id"), "");
    if (const JsonValue* gates = item.get("gates"); gates && gates->isArray()) {
        for (const JsonValue& gate : gates->asArray()) {
            if (gate.isString()) {
                behavior.gates.push_back(gateFromString(gate.asString()));
            }
        }
    }
    if (const JsonValue* actions = item.get("actions"); actions && actions->isArray()) {
        for (const JsonValue& node : actions->asArray()) {
            if (!node.isObject()) continue;
            InteractionAction action{};
            action.kind = actionKindFromString(stringOr(node.get("action"), "textFree"));
            action.direction = directionFromString(stringOr(node.get("direction"), ""), FacingDirection::South);
            action.value = stringOr(node.get("value"), stringOr(node.get("text"), ""));
            behavior.actions.push_back(std::move(action));
        }
    }
    return behavior;
}

} // namespace

void InteractionSequenceController::begin(
    InteractionTargetKind target_kind,
    const InteractionBehaviorCatalog& catalog) {
    target_kind_ = target_kind;
    actions_.clear();
    index_ = 0;
    active_ = true;
    for (const InteractionBehavior& behavior : catalog.behaviors) {
        if (behaviorMatches(behavior, target_kind) && !behavior.actions.empty()) {
            actions_ = behavior.actions;
            break;
        }
    }
    if (actions_.empty()) {
        actions_ = defaultInteractionBehaviorCatalog().behaviors[target_kind == InteractionTargetKind::Pokemon ? 0U : 1U].actions;
    }
}

void InteractionSequenceController::begin(InteractionTargetKind target_kind, const InteractionBehavior& behavior) {
    target_kind_ = target_kind;
    actions_ = behavior.actions.empty() ? defaultBehavior(target_kind).actions : behavior.actions;
    index_ = 0;
    active_ = true;
}

const InteractionAction* InteractionSequenceController::currentAction() const {
    if (!active_ || index_ >= actions_.size()) {
        return nullptr;
    }
    return &actions_[index_];
}

void InteractionSequenceController::advance() {
    if (!active_) return;
    ++index_;
    if (index_ >= actions_.size()) {
        active_ = false;
    }
}

void InteractionSequenceController::cancel() {
    active_ = false;
    index_ = actions_.size();
}

InteractionBehaviorCatalog defaultInteractionBehaviorCatalog() {
    InteractionBehaviorCatalog catalog{};
    catalog.behaviors.push_back(InteractionBehavior{
        "pokemon_default",
        {InteractionTargetKind::Pokemon},
        {
            InteractionAction{InteractionActionKind::FacePlayer},
            InteractionAction{InteractionActionKind::PokemonInteractionSession},
            InteractionAction{InteractionActionKind::TextFree},
        }});
    catalog.behaviors.push_back(InteractionBehavior{
        "character_default",
        {InteractionTargetKind::Character},
        {
            InteractionAction{InteractionActionKind::FacePlayer},
            InteractionAction{InteractionActionKind::TextFree},
        }});
    return catalog;
}

InteractionBehaviorCatalog loadInteractionBehaviorCatalog(const std::string& project_root) {
    const scripts::ScriptCatalog script_catalog = scripts::loadScriptCatalog(project_root);
    InteractionBehaviorCatalog script_behaviors{};
    for (const scripts::OverworldScript& script : script_catalog.scripts) {
        if (script.kind != scripts::ScriptKind::Interaction) continue;
        InteractionBehavior behavior{};
        behavior.id = script.id;
        for (const std::string& gate : script.target_gates) {
            behavior.gates.push_back(gateFromString(gate));
        }
        for (const scripts::ScriptAction& source : script.actions) {
            InteractionAction action{};
            switch (source.kind) {
                case scripts::ScriptActionKind::Face:
                    action.kind = source.value == "FACE_PLAYER" ? InteractionActionKind::FacePlayer :
                        (source.value == "FACE_AWAY" ? InteractionActionKind::FaceAway : InteractionActionKind::FaceDirection);
                    action.direction = source.direction;
                    break;
                case scripts::ScriptActionKind::TextLiteral: action.kind = InteractionActionKind::TextLiteral; action.value = source.value; break;
                case scripts::ScriptActionKind::TextFree: action.kind = InteractionActionKind::TextFree; break;
                case scripts::ScriptActionKind::PokemonInteractionSession: action.kind = InteractionActionKind::PokemonInteractionSession; break;
                case scripts::ScriptActionKind::Jump: action.kind = InteractionActionKind::Jump; action.height_pixels = source.height_pixels; break;
                case scripts::ScriptActionKind::Wait: action.kind = InteractionActionKind::Wait; action.duration_seconds = source.duration_seconds; break;
                case scripts::ScriptActionKind::DisableAttend: action.kind = InteractionActionKind::DisableAttend; break;
                case scripts::ScriptActionKind::EnableAttend: action.kind = InteractionActionKind::EnableAttend; break;
                case scripts::ScriptActionKind::ExitInteraction: action.kind = InteractionActionKind::ExitInteraction; break;
                case scripts::ScriptActionKind::Cry: action.kind = InteractionActionKind::Cry; break;
                case scripts::ScriptActionKind::Emoticon: action.kind = InteractionActionKind::Emoticon; break;
                default: continue;
            }
            behavior.actions.push_back(std::move(action));
        }
        if (!behavior.id.empty() && !behavior.actions.empty()) script_behaviors.behaviors.push_back(std::move(behavior));
    }
    if (!script_behaviors.behaviors.empty()) return script_behaviors;

    const fs::path path = fs::path(project_root) / "config" / "gameplay" / "world3d" / "interactions.json";
    try {
        const JsonValue root = parseJsonFile(path.string());
        const JsonValue* behaviors = root.isObject() ? root.get("behaviors") : nullptr;
        if (!behaviors || !behaviors->isArray()) {
            return defaultInteractionBehaviorCatalog();
        }
        InteractionBehaviorCatalog catalog{};
        for (const JsonValue& item : behaviors->asArray()) {
            InteractionBehavior behavior = parseBehavior(item);
            if (!behavior.id.empty() && !behavior.actions.empty()) {
                catalog.behaviors.push_back(std::move(behavior));
            }
        }
        return catalog.behaviors.empty() ? defaultInteractionBehaviorCatalog() : catalog;
    } catch (...) {
        return defaultInteractionBehaviorCatalog();
    }
}

NpcInteractionMode npcInteractionModeFromMetadata(const std::string& value) {
    return lowerAscii(value) == "scripted" ? NpcInteractionMode::Scripted : NpcInteractionMode::DirectDialogue;
}

InteractionSourceResolution resolveInteractionSource(
    const scripts::ScriptCatalog& catalog,
    const InteractionSourceRequest& request,
    scripts::ScriptCooldowns& cooldowns,
    double now_seconds,
    std::mt19937& rng) {
    InteractionSourceResolution out{};
    const auto use_script = [&](const scripts::OverworldScript& script) {
        out.behavior = behaviorFromScript(script);
        out.entered_script_path = true;
        out.script_id = script.id;
        cooldowns.markUsed(script, now_seconds);
    };

    if (request.runtime_script_id) {
        const auto it = std::find_if(catalog.scripts.begin(), catalog.scripts.end(), [&](const scripts::OverworldScript& script) {
            return script.id == *request.runtime_script_id && usableInteractionScript(script) &&
                cooldowns.available(script.id, now_seconds) && scripts::scriptMatches(script, scripts::ScriptKind::Interaction, request.script_context);
        });
        if (it != catalog.scripts.end()) {
            use_script(*it);
            return out;
        }
    }

    const bool request_script = request.target_kind == InteractionTargetKind::Pokemon ||
        request.npc_mode == NpcInteractionMode::Scripted;
    if (request_script) {
        scripts::ScriptCatalog eligible_catalog{};
        for (const scripts::OverworldScript& script : catalog.scripts) {
            if (usableInteractionScript(script)) eligible_catalog.scripts.push_back(script);
        }
        if (const scripts::OverworldScript* selected = scripts::selectScript(
                eligible_catalog, scripts::ScriptKind::Interaction, request.script_context,
                cooldowns, now_seconds, rng)) {
            out.behavior = behaviorFromScript(*selected);
            out.entered_script_path = true;
            out.script_id = selected->id;
            return out;
        }
    }

    out.behavior = defaultBehavior(request.target_kind);
    out.used_fallback = request_script || request.runtime_script_id.has_value();
    // Pokemon still enter the interaction runtime even when the authored script selection falls back.
    out.entered_script_path = request.target_kind == InteractionTargetKind::Pokemon;
    return out;
}

std::optional<std::string> interactionActivityIdForPokemonSize(const std::string& pokemon_size) {
    const std::string lower = lowerAscii(pokemon_size);
    if (lower == "human") return std::nullopt;
    if (lower == "large") return "large_interact";
    if (lower == "medium") return "medium_interact";
    return "small_interact";
}

FacingDirection oppositeFacing(FacingDirection direction) {
    switch (direction) {
        case FacingDirection::North: return FacingDirection::South;
        case FacingDirection::South: return FacingDirection::North;
        case FacingDirection::East: return FacingDirection::West;
        case FacingDirection::West: return FacingDirection::East;
    }
    return FacingDirection::South;
}

FacingDirection facingTowardTiles(
    int from_tx,
    int from_ty,
    int to_tx,
    int to_ty,
    FacingDirection fallback) {
    const int dx = to_tx - from_tx;
    const int dy = to_ty - from_ty;
    if (std::abs(dx) > std::abs(dy)) {
        return dx > 0 ? FacingDirection::East : FacingDirection::West;
    }
    if (dy != 0) {
        return dy > 0 ? FacingDirection::South : FacingDirection::North;
    }
    return fallback;
}

} // namespace pr::gameplay::world3d::interactions
