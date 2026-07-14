#include "gameplay/world3d/interactions/InteractionText.hpp"

#include "core/config/Json.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <numeric>
#include <utility>

namespace pr::gameplay::world3d::interactions {

namespace fs = std::filesystem;

namespace {

std::string stringOr(const JsonValue* value, const std::string& fallback) {
    return value && value->isString() ? value->asString() : fallback;
}

int intOr(const JsonValue* value, int fallback) {
    return value && value->isNumber() ? static_cast<int>(value->asNumber()) : fallback;
}

double numOr(const JsonValue* value, double fallback) {
    return value && value->isNumber() ? value->asNumber() : fallback;
}

std::vector<std::string> tagsFromJson(const JsonValue* value) {
    std::vector<std::string> out;
    if (!value || !value->isArray()) return out;
    for (const JsonValue& item : value->asArray()) {
        if (item.isString()) {
            out.push_back(normalizeInteractionTag(item.asString()));
        }
    }
    return out;
}

bool entryMatches(const InteractionTextEntry& entry, const InteractionTextContext& context) {
    for (const std::string& tag : entry.required_tags) {
        const std::string normalized = normalizeInteractionTag(tag);
        if (context.tags.find(normalized) == context.tags.end()) {
            return false;
        }
    }
    return true;
}

} // namespace

std::string normalizeInteractionTag(std::string tag) {
    std::string out;
    out.reserve(tag.size());
    for (unsigned char c : tag) {
        if (std::isalnum(c)) {
            out.push_back(static_cast<char>(std::toupper(c)));
        } else if (c == '-' || c == ' ' || c == '.') {
            out.push_back('_');
        } else if (c == '_') {
            out.push_back('_');
        }
    }
    return out;
}

bool InteractionTextCooldowns::available(const std::string& id, double now_seconds) const {
    const auto it = expires_at_.find(id);
    return it == expires_at_.end() || it->second <= now_seconds;
}

void InteractionTextCooldowns::markUsed(
    const std::string& id,
    double now_seconds,
    double cooldown_seconds) {
    if (id.empty() || cooldown_seconds <= 0.0) return;
    expires_at_[id] = now_seconds + cooldown_seconds;
}

void InteractionTextCooldowns::reset(const std::vector<std::string>& ids) {
    for (const std::string& id : ids) expires_at_.erase(id);
}

InteractionTextCatalog loadInteractionTextCatalog(const std::string& project_root) {
    InteractionTextCatalog catalog{};
    const fs::path path = fs::path(project_root) / "config" / "gameplay" / "world3d" / "interaction_text.json";
    try {
        const JsonValue root = parseJsonFile(path.string());
        const JsonValue* entries = root.isObject() ? root.get("texts") : nullptr;
        if (!entries || !entries->isArray()) {
            return catalog;
        }
        for (const JsonValue& item : entries->asArray()) {
            if (!item.isObject()) continue;
            InteractionTextEntry entry{};
            entry.id = stringOr(item.get("id"), "");
            entry.body = stringOr(item.get("body"), "");
            entry.required_tags = tagsFromJson(item.get("requiredTags"));
            entry.weight = std::max(1, intOr(item.get("weight"), 1));
            entry.global_cooldown_seconds = std::max(0.0, numOr(item.get("globalCooldownSeconds"), 0.0));
            if (!entry.id.empty()) {
                catalog.entries.push_back(std::move(entry));
            }
        }
    } catch (...) {
        return catalog;
    }
    return catalog;
}

InteractionTextSelection selectInteractionText(
    const InteractionTextCatalog& catalog,
    const InteractionTextContext& context,
    InteractionTextCooldowns& cooldowns,
    double now_seconds,
    std::mt19937& rng) {
    std::vector<const InteractionTextEntry*> candidates;
    int best_specificity = -1;
    for (const InteractionTextEntry& entry : catalog.entries) {
        if (!cooldowns.available(entry.id, now_seconds) || !entryMatches(entry, context)) {
            continue;
        }
        const int specificity = static_cast<int>(entry.required_tags.size());
        if (specificity > best_specificity) {
            candidates.clear();
            best_specificity = specificity;
        }
        if (specificity == best_specificity) {
            candidates.push_back(&entry);
        }
    }
    if (candidates.empty()) {
        std::vector<std::string> matching_ids;
        for (const InteractionTextEntry& entry : catalog.entries) {
            if (entryMatches(entry, context)) matching_ids.push_back(entry.id);
        }
        if (matching_ids.empty()) return {};

        // Every matching line has been used. Start a fresh randomized cycle instead
        // of presenting an empty textbox, then run the normal specificity selection again.
        cooldowns.reset(matching_ids);
        return selectInteractionText(catalog, context, cooldowns, now_seconds, rng);
    }

    const int total_weight = std::accumulate(
        candidates.begin(),
        candidates.end(),
        0,
        [](int total, const InteractionTextEntry* entry) {
            return total + std::max(1, entry->weight);
        });
    std::uniform_int_distribution<int> pick(1, std::max(1, total_weight));
    int cursor = pick(rng);
    const InteractionTextEntry* selected = candidates.front();
    for (const InteractionTextEntry* entry : candidates) {
        cursor -= std::max(1, entry->weight);
        if (cursor <= 0) {
            selected = entry;
            break;
        }
    }

    cooldowns.markUsed(selected->id, now_seconds, selected->global_cooldown_seconds);
    return InteractionTextSelection{
        true,
        selected->id,
        renderInteractionTextTemplate(selected->body, context.variables)};
}

std::string renderInteractionTextTemplate(
    const std::string& text,
    const std::unordered_map<std::string, std::string>& variables) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] != '{') {
            out.push_back(text[i++]);
            continue;
        }
        const std::size_t close = text.find('}', i + 1);
        if (close == std::string::npos) {
            out.push_back(text[i++]);
            continue;
        }
        const std::string key = text.substr(i + 1, close - i - 1);
        const auto it = variables.find(key);
        if (it != variables.end()) {
            out += it->second;
        }
        i = close + 1;
    }
    return out;
}

} // namespace pr::gameplay::world3d::interactions
