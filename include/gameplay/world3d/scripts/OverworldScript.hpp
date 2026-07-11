#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"

#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace pr::gameplay::world3d::scripts {

enum class ScriptKind { Idle, Interaction, Npc };
enum class ScriptActionKind { Wait, Face, Move, Wander, Jump, TextLiteral, TextFree, PokemonInteractionSession, Cry, Emoticon };

struct ScriptCondition {
    std::vector<std::string> all_tags;
    std::vector<std::string> none_tags;
    std::string close_to_tag;
    int close_to_tiles = -1;
};

struct ScriptAction {
    ScriptActionKind kind = ScriptActionKind::Wait;
    FacingDirection direction = FacingDirection::South;
    std::string value;
    int tiles = 0;
    int height_pixels = 0;
    double duration_seconds = 0.0;
};

struct OverworldScript {
    std::string id;
    ScriptKind kind = ScriptKind::Interaction;
    std::vector<std::string> target_gates;
    std::string trigger;
    int priority = 0;
    int weight = 1;
    double cooldown_seconds = 0.0;
    ScriptCondition when;
    std::vector<ScriptAction> actions;
};

struct ScriptCatalog { std::vector<OverworldScript> scripts; };

struct ScriptContext {
    std::unordered_set<std::string> tags;
    std::unordered_map<std::string, int> nearest_tag_distance_tiles;
};

struct ScriptValidationIssue { std::string script_id; std::string message; };

class ScriptCooldowns {
public:
    bool available(const std::string& id, double now_seconds) const;
    void markUsed(const OverworldScript& script, double now_seconds);
private:
    std::unordered_map<std::string, double> expires_at_;
};

ScriptCatalog loadScriptCatalog(const std::string& project_root, std::vector<ScriptValidationIssue>* issues = nullptr);
std::vector<ScriptValidationIssue> validateScriptCatalog(const ScriptCatalog& catalog);
const OverworldScript* selectScript(
    const ScriptCatalog& catalog,
    ScriptKind kind,
    const ScriptContext& context,
    ScriptCooldowns& cooldowns,
    double now_seconds,
    std::mt19937& rng);
bool scriptMatches(const OverworldScript& script, ScriptKind kind, const ScriptContext& context);
std::string normalizeScriptTag(std::string tag);
ScriptActionKind scriptActionKindFromString(const std::string& value, bool* known = nullptr);
std::string scriptActionKindName(ScriptActionKind kind);

} // namespace pr::gameplay::world3d::scripts
