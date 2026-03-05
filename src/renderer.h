#pragma once
#include "app.h"
#include "layout.h"
#include "reader.h"

#include <imgui.h>

/// Render one component's plot onto an ImDrawList.
///
/// Coordinate transform (layout px → screen px):
///   screen_x = canvas_pos.x + (layout_px / CANVAS_W * canvas_size.x) * zoom + pan_x
///   screen_y = canvas_pos.y + (layout_py / CANVAS_H * canvas_size.y) * zoom + pan_y
///
/// Handles: rect fills, per-side dashed/solid borders, edges (line/bezier + arrowhead),
/// axes, multiplicity labels, ancestor highlighting, zoom/pan/click input.
void render_component(ImDrawList* dl,
                      const Component& comp,
                      const Layout& layout,
                      AppState& state,
                      ImVec2 canvas_pos,
                      ImVec2 canvas_size);
