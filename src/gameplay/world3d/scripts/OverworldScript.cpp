#include "gameplay/world3d/scripts/OverworldScript.hpp"

#include "core/config/Json.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <limits>
#include <numeric>
#include <unordered_set>

namespace pr::gameplay::world3d::scripts {
namespace fs = std::filesystem;
namespace {

std::string strOr(const JsonValue* value, const std::string& fallback = {}) {
    return value && value->isString() ? value->asString() : fallback;
}
int intOr(const JsonValue* value, int fallback) { return value && value->isNumber() ? static_cast<int>(value->asNumber()) : fallback; }
double numOr(const JsonValue* value, double fallback) { return value && value->isNumber() ? value->asNumber() : fallback; }

std::vector<std::string> tagsFromJson(const JsonValue* value) {
    std::vector<std::string> tags;
    if (!value || !value->isArray()) return tags;
    for (const JsonValue& item : value->asArray()) if (item.isString()) tags.push_back(normalizeScriptTag(item.asString()));
    return tags;
}

ScriptKind kindFromString(const std::string& value) {
    const std::string tag = normalizeScriptTag(value);
    if (tag == "IDLE") return ScriptKind::Idle;
    if (tag == "NPC") return ScriptKind::Npc;
    return ScriptKind::Interaction;
}

FacingDirection directionFromString(const std::string& value) {
    const std::string tag = normalizeScriptTag(value);
    if (tag == "NORTH" || tag == "UP") return FacingDirection::North;
    if (tag == "EAST" || tag == "RIGHT") return FacingDirection::East;
    if (tag == "WEST" || tag == "LEFT") return FacingDirection::West;
    return FacingDirection::South;
}

bool actionAvailable(ScriptActionKind kind) {
    return kind != ScriptActionKind::Cry && kind != ScriptActionKind::Emoticon;
}

OverworldScript parseScript(const JsonValue& root, std::vector<ScriptValidationIssue>* issues) {
    OverworldScript script{};
    if (!root.isObject()) return script;
    script.id = strOr(root.get("id"));
    script.kind = kindFromString(strOr(root.get("kind")));
    script.target_gates = tagsFromJson(root.get("targetGates"));
    script.trigger = normalizeScriptTag(strOr(root.get("trigger")));
    script.priority = intOr(root.get("priority"), 0);
    script.weight = std::max(1, intOr(root.get("weight"), 1));
    script.cooldown_seconds = std::max(0.0, numOr(root.get("cooldownSeconds"), 0.0));
    if (const JsonValue* when = root.get("when"); when && when->isObject()) {
        script.when.all_tags = tagsFromJson(when->get("allTags"));
        script.when.none_tags = tagsFromJson(when->get("noneTags"));
        if (const JsonValue* close = when->get("closeTo"); close && close->isObject()) {
            script.when.close_to_tag = normalizeScriptTag(strOr(close->get("tag")));
            script.when.close_to_tiles = std::max(0, intOr(close->get("maxTiles"), -1));
        }
    }
    if (const JsonValue* actions = root.get("actions"); actions && actions->isArray()) {
        for (const JsonValue& node : actions->asArray()) {
            if (!node.isObject()) continue;
            bool known = false;
            ScriptAction action{};
            action.kind = scriptActionKindFromString(strOr(node.get("action")), &known);
            action.direction = directionFromString(strOr(node.get("direction")));
            action.value = strOr(node.get("value"), strOr(node.get("text"), normalizeScriptTag(strOr(node.get("action")))));
            action.tiles = std::max(0, intOr(node.get("tiles"), 0));
            action.height_pixels = std::max(0, intOr(node.get("heightPixels"), 0));
            action.duration_seconds = std::max(0.0, numOr(node.get("durationSeconds"), 0.0));
            if (!known && issues) issues->push_back({script.id, "Unknown action: " + strOr(node.get("action"))});
            script.actions.push_back(std::move(action));
        }
    }
    return script;
}

} // namespace

std::string normalizeScriptTag(std::string tag) {
    std::string out;
    for (unsigned char c : tag) {
        if (std::isalnum(c)) out.push_back(static_cast<char>(std::toupper(c)));
        else if (!out.empty() && out.back() != '_') out.push_back('_');
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out;
}

ScriptActionKind scriptActionKindFromString(const std::string& value, bool* known) {
    const std::string tag = normalizeScriptTag(value);
    if (known) *known = true;
    if (tag == "WAIT") return ScriptActionKind::Wait;
    if (tag == "FACE" || tag == "FACE_DIRECTION" || tag == "FACE_PLAYER" || tag == "FACE_AWAY") return ScriptActionKind::Face;
    if (tag == "MOVE") return ScriptActionKind::Move;
    if (tag == "WANDER") return ScriptActionKind::Wander;
    if (tag == "JUMP") return ScriptActionKind::Jump;
    if (tag == "TEXT" || tag == "TEXT_LITERAL") return ScriptActionKind::TextLiteral;
    if (tag == "TEXT_FREE") return ScriptActionKind::TextFree;
    if (tag == "POKEMON_INTERACTION_SESSION") return ScriptActionKind::PokemonInteractionSession;
    if (tag == "CRY") return ScriptActionKind::Cry;
    if (tag == "EMOTICON") return ScriptActionKind::Emoticon;
    if (known) *known = false;
    return ScriptActionKind::Wait;
}

std::string scriptActionKindName(ScriptActionKind kind) {
    switch (kind) {
        case ScriptActionKind::Wait: return "WAIT"; case ScriptActionKind::Face: return "FACE";
        case ScriptActionKind::Move: return "MOVE"; case ScriptActionKind::Wander: return "WANDER";
        case ScriptActionKind::Jump: return "JUMP"; case ScriptActionKind::TextLiteral: return "TEXT";
        case ScriptActionKind::TextFree: return "TEXT_FREE"; case ScriptActionKind::PokemonInteractionSession: return "POKEMON_INTERACTION_SESSION";
        case ScriptActionKind::Cry: return "CRY"; case ScriptActionKind::Emoticon: return "EMOTICON";
    }
    return "WAIT";
}

ScriptCatalog loadScriptCatalog(const std::string& project_root, std::vector<ScriptValidationIssue>* issues) {
    ScriptCatalog catalog{};
    const fs::path base = fs::path(project_root) / "config" / "gameplay" / "world3d" / "scripts";
    try {
        const JsonValue index = parseJsonFile((base / "script_catalog.json").string());
        const JsonValue* scripts = index.isObject() ? index.get("scripts") : nullptr;
        if (!scripts || !scripts->isArray()) return catalog;
        for (const JsonValue& entry : scripts->asArray()) {
            if (!entry.isObject()) continue;
            const std::string path = strOr(entry.get("path"));
            if (path.empty() || path.find("..") != std::string::npos) continue;
            OverworldScript script = parseScript(parseJsonFile((base / path).string()), issues);
            if (!script.id.empty()) catalog.scripts.push_back(std::move(script));
        }
    } catch (const std::exception& ex) {
        if (issues) issues->push_back({"catalog", ex.what()});
    }
    if (issues) {
        const std::vector<ScriptValidationIssue> validation = validateScriptCatalog(catalog);
        issues->insert(issues->end(), validation.begin(), validation.end());
    }
    return catalog;
}

std::vector<ScriptValidationIssue> validateScriptCatalog(const ScriptCatalog& catalog) {
    std::vector<ScriptValidationIssue> issues;
    std::unordered_set<std::string> ids;
    for (const OverworldScript& script : catalog.scripts) {
        if (!ids.insert(script.id).second) issues.push_back({script.id, "Duplicate script id"});
        if (script.actions.empty()) issues.push_back({script.id, "Script needs at least one action"});
        for (const ScriptAction& action : script.actions) {
            if (!actionAvailable(action.kind)) issues.push_back({script.id, scriptActionKindName(action.kind) + " has no runtime adapter"});
        }
    }
    return issues;
}

bool ScriptCooldowns::available(const std::string& id, double now_seconds) const {
    const auto it = expires_at_.find(id);
    return it == expires_at_.end() || it->second <= now_seconds;
}
void ScriptCooldowns::markUsed(const OverworldScript& script, double now_seconds) {
    if (script.cooldown_seconds > 0.0) expires_at_[script.id] = now_seconds + script.cooldown_seconds;
}

bool scriptMatches(const OverworldScript& script, ScriptKind kind, const ScriptContext& context) {
    if (script.kind != kind) return false;
    for (const std::string& tag : script.target_gates) if (context.tags.count(tag) == 0) return false;
    for (const std::string& tag : script.when.all_tags) if (context.tags.count(tag) == 0) return false;
    for (const std::string& tag : script.when.none_tags) if (context.tags.count(tag) != 0) return false;
    if (!script.when.close_to_tag.empty()) {
        const auto it = context.nearest_tag_distance_tiles.find(script.when.close_to_tag);
        if (it == context.nearest_tag_distance_tiles.end() || it->second > script.when.close_to_tiles) return false;
    }
    return true;
}

const OverworldScript* selectScript(
    const ScriptCatalog& catalog, ScriptKind kind, const ScriptContext& context,
    ScriptCooldowns& cooldowns, double now_seconds, std::mt19937& rng) {
    std::vector<const OverworldScript*> candidates;
    int priority = std::numeric_limits<int>::min();
    for (const OverworldScript& script : catalog.scripts) {
        if (!cooldowns.available(script.id, now_seconds) || !scriptMatches(script, kind, context)) continue;
        if (script.priority > priority) { candidates.clear(); priority = script.priority; }
        if (script.priority == priority) candidates.push_back(&script);
    }
    if (candidates.empty()) return nullptr;
    const int total = std::accumulate(candidates.begin(), candidates.end(), 0, [](int sum, const OverworldScript* s) { return sum + std::max(1, s->weight); });
    std::uniform_int_distribution<int> pick(1, total);
    int remaining = pick(rng);
    for (const OverworldScript* script : candidates) {
        remaining -= std::max(1, script->weight);
        if (remaining <= 0) { cooldowns.markUsed(*script, now_seconds); return script; }
    }
    cooldowns.markUsed(*candidates.back(), now_seconds);
    return candidates.back();
}

} // namespace pr::gameplay::world3d::scripts
