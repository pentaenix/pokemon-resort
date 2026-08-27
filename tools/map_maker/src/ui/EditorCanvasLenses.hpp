#pragma once

#include "mapmaker/ui/EditorShell.hpp"

#include <dear-imgui/imgui.h>

#include <cstddef>

namespace pr::mapmaker::canvas_lenses {

void drawCell(const EditorUiModel& model, std::size_t index, const ImVec2& cell_min,
    const ImVec2& cell_max, float cell_size, ImDrawList& draw);
void drawLegend(const EditorUiModel& model, const ImVec2& canvas_min,
    const ImVec2& canvas_max, ImDrawList& draw);
float markerOpacity(EditorTool tool, TopDownMarkerKind kind);

} // namespace pr::mapmaker::canvas_lenses
