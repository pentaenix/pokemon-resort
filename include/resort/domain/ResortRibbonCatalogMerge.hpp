#pragma once

#include "core/config/Json.hpp"

namespace pr::resort {

/// Gain-only merge for `resort_catalog.ribbons` / `ribbon_flags`: bools OR, numbers max, incoming
/// explicit `false` does not create a new key and does not downgrade an existing `true`.
pr::JsonValue mergeRibbonCatalogMapsGainOnly(const pr::JsonValue& existing, const pr::JsonValue& incoming);

} // namespace pr::resort
