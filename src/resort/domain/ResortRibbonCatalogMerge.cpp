#include "resort/domain/ResortRibbonCatalogMerge.hpp"

#include <algorithm>

namespace pr::resort {

namespace {

bool isEffectivelyEmptyIncomingValue(const pr::JsonValue& v) {
    if (v.isBool() && !v.asBool()) {
        return true;
    }
    return false;
}

} // namespace

pr::JsonValue mergeRibbonCatalogMapsGainOnly(const pr::JsonValue& existing, const pr::JsonValue& incoming) {
    if (!incoming.isObject()) {
        return existing;
    }
    if (!existing.isObject()) {
        pr::JsonValue::Object out;
        for (const auto& [k, v] : incoming.asObject()) {
            if (isEffectivelyEmptyIncomingValue(v)) {
                continue;
            }
            out.emplace(k, v);
        }
        return pr::JsonValue(out);
    }

    auto merged = existing.asObject();
    for (const auto& [k, v] : incoming.asObject()) {
        auto it = merged.find(k);
        if (it == merged.end()) {
            if (isEffectivelyEmptyIncomingValue(v)) {
                continue;
            }
            merged.emplace(k, v);
            continue;
        }

        const pr::JsonValue& ev = it->second;
        if (ev.isBool() && v.isBool()) {
            it->second = pr::JsonValue(ev.asBool() || v.asBool());
        } else if (ev.isNumber() && v.isNumber()) {
            it->second = pr::JsonValue(std::max(ev.asNumber(), v.asNumber()));
        } else if (ev.isObject() && v.isObject()) {
            it->second = mergeRibbonCatalogMapsGainOnly(ev, v);
        } else if (ev.isBool() && ev.asBool() && v.isBool() && !v.asBool()) {
            continue;
        } else {
            if (!isEffectivelyEmptyIncomingValue(v)) {
                it->second = v;
            }
        }
    }
    return pr::JsonValue(merged);
}

} // namespace pr::resort
