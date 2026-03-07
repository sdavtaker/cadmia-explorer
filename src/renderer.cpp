#include "renderer.h"
#include "logic.h"

#ifdef CADVIS_GUI
#include "gpu_rect.h"
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>

// ---------------------------------------------------------------------------
// Viewport transform helpers
// ---------------------------------------------------------------------------
struct VP {
  float ox, oy, sw, sh, zoom_x, zoom_y, pan_x, pan_y;

  // Layout coordinates → screen coordinates
  [[nodiscard]] float sx(double lx) const {
    return ox + (static_cast<float>(lx) / static_cast<float>(Layout::CANVAS_W) * sw) * zoom_x +
           pan_x;
  }
  [[nodiscard]] float sy(double ly) const {
    return oy + (static_cast<float>(ly) / static_cast<float>(Layout::CANVAS_H) * sh) * zoom_y +
           pan_y;
  }
};

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------
static void arrow_tip(ImDrawList *dl, ImVec2 tip, ImVec2 from_pt, ImU32 col, float size = 7.0f) {
  float dx = tip.x - from_pt.x;
  float dy = tip.y - from_pt.y;
  float len = std::sqrt(dx * dx + dy * dy);
  if (len < 0.5f) {
    return;
  }
  float nx = dx / len;
  float ny = dy / len;
  float ppx = -ny;
  float ppy = nx;
  float half = size * 0.38f;
  ImVec2 base = {tip.x - nx * size, tip.y - ny * size};
  dl->AddTriangleFilled(tip, {base.x + ppx * half, base.y + ppy * half},
                        {base.x - ppx * half, base.y - ppy * half}, col);
}

// ---------------------------------------------------------------------------
// Edge rendering
// ---------------------------------------------------------------------------
static void draw_edges(ImDrawList *dl, const Component &comp, const Layout &layout, const VP &vp,
                       const AppState &state) {
  bool anything_selected = (state.selected_rect >= 0);

  for (const auto &e : comp.edges) {
    if (e.from >= comp.rects.size() || e.to >= comp.rects.size()) {
      continue;
    }
    const Rect &fr = comp.rects[e.from];
    const Rect &tr = comp.rects[e.to];

    bool on_path = anything_selected && e.from < state.is_ancestor.size() &&
                   state.is_ancestor[e.from] && e.to < state.is_ancestor.size() &&
                   state.is_ancestor[e.to];

    ImU32 edge_col;
    float thickness;
    if (!anything_selected) {
      edge_col = IM_COL32(136, 136, 136, 128);
      thickness = 1.0f;
    } else if (on_path) {
      edge_col = IM_COL32(50, 90, 140, 230);
      thickness = 2.0f;
    } else {
      edge_col = IM_COL32(136, 136, 136, 35);
      thickness = 1.0f;
    }

    double fr_rx = layout.time_to_px(fr.time_lo) + layout.rect_pw(fr);
    ImVec2 sp = {vp.sx(fr_rx), vp.sy(layout.out_to_py(fr.out_hi) + layout.rect_ph(fr) / 2.0)};

    double tr_lx = layout.time_to_px(tr.time_lo);
    ImVec2 tp = {vp.sx(tr_lx), vp.sy(layout.out_to_py(tr.out_hi) + layout.rect_ph(tr) / 2.0)};

    float ddx = std::abs(tp.x - sp.x);
    if (ddx < 10.0f) {
      ImVec2 cp1 = {sp.x - 40.0f, sp.y};
      ImVec2 cp2 = {tp.x + 40.0f, tp.y};
      dl->AddBezierCubic(sp, cp1, cp2, tp, edge_col, thickness);
    } else {
      dl->AddLine(sp, tp, edge_col, thickness);
    }
    arrow_tip(dl, tp, sp, edge_col);
  }
}

// ---------------------------------------------------------------------------
// Axis rendering
// ---------------------------------------------------------------------------
static void draw_axes(ImDrawList *dl, const Component &comp, const Layout &layout, const VP &vp) {
  ImU32 axis_col = IM_COL32(60, 60, 60, 220);
  ImU32 text_col = IM_COL32(40, 40, 40, 220);

  // X axis line
  float ax_y = vp.sy(Layout::PLOT_Y + Layout::PLOT_H);
  float ax_x1 = vp.sx(Layout::PLOT_X);
  float ax_x2 = vp.sx(Layout::PLOT_X + Layout::PLOT_W);
  dl->AddLine({ax_x1, ax_y}, {ax_x2, ax_y}, axis_col, 1.0f);

  // X ticks (~8)
  constexpr int N_TICKS = 8;
  double t_step = (layout.t_max - layout.t_min) / N_TICKS;
  for (int i = 0; i <= N_TICKS; ++i) {
    double t = layout.t_min + i * t_step;
    float tx = vp.sx(layout.time_to_px(t));
    dl->AddLine({tx, ax_y}, {tx, ax_y + 5.0f}, axis_col, 1.0f);
    std::array<char, 32> buf;
    std::snprintf(buf.data(), buf.size(), "%.3g", t);
    ImVec2 tsz = ImGui::CalcTextSize(buf.data());
    dl->AddText({tx - tsz.x * 0.5f, ax_y + 7.0f}, text_col, buf.data());
  }

  // Y axis line
  float ay_x = vp.sx(Layout::PLOT_X);
  float ay_y1 = vp.sy(Layout::PLOT_Y);
  float ay_y2 = vp.sy(Layout::PLOT_Y + Layout::PLOT_H);
  dl->AddLine({ay_x, ay_y1}, {ay_x, ay_y2}, axis_col, 1.0f);

  // Y ticks: one per unique out_lo value
  std::set<double> y_vals;
  for (const auto &r : comp.rects) {
    y_vals.insert(r.out_lo);
  }
  for (double y : y_vals) {
    float pyy = vp.sy(layout.out_to_py(y));
    dl->AddLine({ay_x - 5.0f, pyy}, {ay_x, pyy}, axis_col, 1.0f);
    std::array<char, 32> buf;
    std::snprintf(buf.data(), buf.size(), "%.3g", y);
    ImVec2 tsz = ImGui::CalcTextSize(buf.data());
    dl->AddText({ay_x - 8.0f - tsz.x, pyy - tsz.y * 0.5f}, text_col, buf.data());
  }
}

// ---------------------------------------------------------------------------
// Main entry point
// ---------------------------------------------------------------------------
void render_component(ImDrawList *dl, const Component &comp, const Layout &layout, AppState &state,
                      ImVec2 canvas_pos, ImVec2 canvas_size, ImVec2 display_size, ImVec4 clip_rect,
                      GpuRectRenderer *gpu_rect) {
  VP vp{canvas_pos.x, canvas_pos.y, canvas_size.x, canvas_size.y,
        state.zoom_x, state.zoom_y, state.pan_x,   state.pan_y};

  // GPU-instanced rect fill + per-side dashed/solid borders
#ifdef CADVIS_GUI
  if (gpu_rect != nullptr) {
    gpu_rect->draw(dl, canvas_pos, canvas_size, display_size, clip_rect, layout, state);
  }
#else
  (void)gpu_rect;
  (void)display_size;
  (void)clip_rect;
#endif

  // Edges (remain in ImDrawList)
  draw_edges(dl, comp, layout, vp, state);

  // Multiplicity labels (×N) — GPU shader has no text rendering
  bool anything_selected = (state.selected_rect >= 0);
  for (int i = 0; i < static_cast<int>(comp.rects.size()); ++i) {
    const Rect &r = comp.rects[static_cast<size_t>(i)];
    if (r.multiplicity <= 1) {
      continue;
    }
    float x2 = vp.sx(layout.time_to_px(r.time_lo) + layout.rect_pw(r));
    float y1 = vp.sy(layout.out_to_py(r.out_hi));
    std::array<char, 32> buf;
    std::snprintf(buf.data(), buf.size(), "\xc3\x97%u", r.multiplicity);
    ImVec2 tsz = ImGui::CalcTextSize(buf.data());
    ImU32 label_col;
    if (!anything_selected) {
      label_col = IM_COL32(50, 90, 140, 220);
    } else if (i == state.selected_rect) {
      label_col = IM_COL32(180, 100, 0, 255);
    } else if (static_cast<size_t>(i) < state.is_ancestor.size() &&
               state.is_ancestor[static_cast<size_t>(i)]) {
      label_col = IM_COL32(50, 90, 140, 255);
    } else {
      label_col = IM_COL32(50, 90, 140, 60);
    }
    dl->AddText({x2 - tsz.x - 2.0f, y1 + 1.0f}, label_col, buf.data());
  }

  draw_axes(dl, comp, layout, vp);
}
