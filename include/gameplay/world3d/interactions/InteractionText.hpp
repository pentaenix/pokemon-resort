#pragma once

#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace pr::gameplay::world3d::interactions {

struct InteractionTextEntry {
    std::string id;
    std::string body;
    std::vector<std::string> required_tags;
    int weight = 1;
    double global_cooldown_seconds = 0.0;
};

struct InteractionTextCatalog {
    std::vector<InteractionTextEntry> entries;
};

struct InteractionTextContext {
    std::unordered_set<std::string> tags;
    std::unordered_map<std::string, std::string> variables;
};

struct InteractionTextSelection {
    bool found = false;
    std::string id;
    std::string body;
};

class InteractionTextCooldowns {
public:
    bool available(const std::string& id, double now_seconds) const;
    void markUsed(const std::string& id, double now_seconds, double cooldown_seconds);
    void reset(const std::vector<std::string>& ids);

private:
    std::unordered_map<std::string, double> expires_at_;
};

InteractionTextCatalog loadInteractionTextCatalog(const std::string& project_root);
InteractionTextSelection selectInteractionText(
    const InteractionTextCatalog& catalog,
    const InteractionTextContext& context,
    InteractionTextCooldowns& cooldowns,
    double now_seconds,
    std::mt19937& rng);
std::string renderInteractionTextTemplate(
    const std::string& text,
    const std::unordered_map<std::string, std::string>& variables);
std::string normalizeInteractionTag(std::string tag);

} // namespace pr::gameplay::world3d::interactions
