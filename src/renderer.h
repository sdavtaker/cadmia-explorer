#pragma once
#include "app.h"
#include "layout.h"
#include "reader.h"

#include <imgui.h>

#ifdef CADVIS_GUI
struct GpuRectRenderer; // forward declaration
#endif

/// Render one component's plot onto an ImDrawList.
///
/// Rect fills and per-side dashed/solid borders are drawn via GPU instancing
/// (injected through an ImDrawList::AddCallback). Edges, axes, and multiplicity
/// labels remain in ImDrawList.
///
/// gpu_rect must be non-null when CADVIS_GUI is defined; pass nullptr for
/// headless builds where the GPU path is not compiled in.
void render_component(ImDrawList *dl, const Component &comp, const Layout &layout, AppState &state,
                      ImVec2 canvas_pos, ImVec2 canvas_size, ImVec2 display_size, ImVec4 clip_rect,
                      GpuRectRenderer *gpu_rect);
