#include "renderer.h"
#include "logic.h"

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
  float ox, oy, sw, sh, zoom, pan_x, pan_y;

  // Layout coordinates → screen coordinates
  [[nodiscard]] float sx(double lx) const {
    return ox + (static_cast<float>(lx) / static_cast<float>(Layout::CANVAS_W) * sw) * zoom + pan_x;
  }
  [[nodiscard]] float sy(double ly) const {
    return oy + (static_cast<float>(ly) / static_cast<float>(Layout::CANVAS_H) * sh) * zoom + pan_y;
  }
};

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------
static void dashed_line(const ImDrawList *dl, ImVec2 p0, ImVec2 p1, ImU32 col, float thick,
                        float dash_len = 4.0f, float gap_len = 3.0f) {
  float dx = p1.x - p0.x;
  float dy = p1.y - p0.y;
  float len = std::sqrt(dx * dx + dy * dy);
  if (len < 0.5f) {
    return;
  }
  float nx = dx / len;
  float ny = dy / len;
  float pos = 0.0f;
  bool drawing = true;
  while (pos < len) {
    float seg = drawing ? dash_len : gap_len;
    float end = pos + seg;
    if (end > len) {
      end = len;
    }
    if (drawing) {
      dl->AddLine({p0.x + nx * pos, p0.y + ny * pos}, {p0.x + nx * end, p0.y + ny * end}, col,
                  thick);
    }
    pos = end;
    drawing = !drawing;
  }
}

static void arrow_tip(const ImDrawList *dl, ImVec2 tip, ImVec2 from_pt, ImU32 col,
                      float size = 7.0f) {
  float dx = tip.x - from_pt.x;
  float dy = tip.y - from_pt.y;
  float len = std::sqrt(dx * dx + dy * dy);
  if (len < 0.5f) {
    return;
  }
  float nx = dx / len;
  float ny = dy / len;
  float px = -ny;
  float py = nx;
  float half = size * 0.38f;
  ImVec2 base = {tip.x - nx * size, tip.y - ny * size};
  dl->AddTriangleFilled(tip, {base.x + px * half, base.y + py * half},
                        {base.x - px * half, base.y - py * half}, col);
}

// ---------------------------------------------------------------------------
// Rect rendering
// ---------------------------------------------------------------------------
static void draw_rect(const ImDrawList *dl, const Rect &r, const Layout &layout, const VP &vp,
                      ImU32 fill_col, ImU32 border_col) {
  float x1 = vp.sx(layout.time_to_px(r.time_lo));
  float x2 = vp.sx(layout.time_to_px(r.time_lo) + layout.rect_pw(r));
  float y1 = vp.sy(layout.out_to_py(r.out_hi));
  float y2 = vp.sy(layout.out_to_py(r.out_hi) + layout.rect_ph(r));

  // Fill
  dl->AddRectFilled({x1, y1}, {x2, y2}, fill_col);

  // Per-side borders: left=time_lo, right=time_hi, top=out_hi, bottom=out_lo
  float thick = 1.0f;
  if (r.time_lo_closed) {
    dl->AddLine({x1, y1}, {x1, y2}, border_col, thick);
  } else {
    dashed_line(dl, {x1, y1}, {x1, y2}, border_col, thick);
  }

  if (r.time_hi_closed) {
    dl->AddLine({x2, y1}, {x2, y2}, border_col, thick);
  } else {
    dashed_line(dl, {x2, y1}, {x2, y2}, border_col, thick);
  }

  if (r.out_hi_closed) {
    dl->AddLine({x1, y1}, {x2, y1}, border_col, thick);
  } else {
    dashed_line(dl, {x1, y1}, {x2, y1}, border_col, thick);
  }

  if (r.out_lo_closed) {
    dl->AddLine({x1, y2}, {x2, y2}, border_col, thick);
  } else {
    dashed_line(dl, {x1, y2}, {x2, y2}, border_col, thick);
  }

  // Multiplicity label ×N in top-right corner
  if (r.multiplicity > 1) {
    std::array<char, 32> buf;
    std::snprintf(buf.data(), buf.size(), "\xc3\x97%u", r.multiplicity); // ×N
    ImVec2 tsz = ImGui::CalcTextSize(buf.data());
    dl->AddText({x2 - tsz.x - 2.0f, y1 + 1.0f}, border_col, buf.data());
  }
}

// ---------------------------------------------------------------------------
// Edge rendering
// ---------------------------------------------------------------------------
static void draw_edges(ImDrawList *dl, const Component &comp, const Layout &layout, const VP &vp) {
  ImU32 edge_col = IM_COL32(136, 136, 136, 128);

  for (const auto &e : comp.edges) {
    if (e.from >= comp.rects.size() || e.to >= comp.rects.size())
      continue;
    const Rect &fr = comp.rects[e.from];
    const Rect &tr = comp.rects[e.to];

    // Source: center-right of source rect
    double fr_rx = layout.time_to_px(fr.time_lo) + layout.rect_pw(fr);
    ImVec2 sp = {vp.sx(fr_rx), vp.sy(layout.out_to_py(fr.out_hi) + layout.rect_ph(fr) / 2.0)};

    // Target: center-left of target rect
    double tr_lx = layout.time_to_px(tr.time_lo);
    ImVec2 tp = {vp.sx(tr_lx), vp.sy(layout.out_to_py(tr.out_hi) + layout.rect_ph(tr) / 2.0)};

    float dx = std::abs(tp.x - sp.x);
    if (dx < 10.0f) {
      // Near-vertical: use bezier with horizontal offsets
      ImVec2 cp1 = {sp.x - 40.0f, sp.y};
      ImVec2 cp2 = {tp.x + 40.0f, tp.y};
      dl->AddBezierCubic(sp, cp1, cp2, tp, edge_col, 1.0f);
    } else {
      dl->AddLine(sp, tp, edge_col, 1.0f);
    }
    arrow_tip(dl, tp, sp, edge_col);
  }
}

// ---------------------------------------------------------------------------
// Axis rendering
// ---------------------------------------------------------------------------
static void draw_axes(const ImDrawList *dl, const Component &comp, const Layout &layout,
                      const VP &vp) {
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
  for (const auto &r : comp.rects)
    y_vals.insert(r.out_lo);

  for (double y : y_vals) {
    float py = vp.sy(layout.out_to_py(y));
    dl->AddLine({ay_x - 5.0f, py}, {ay_x, py}, axis_col, 1.0f);

    // Find the label for this out_lo
    const char *lbl = "";
    for (const auto &r : comp.rects) {
      if (r.out_lo == y) {
        lbl = r.out_label.c_str();
        break;
      }
    }
    ImVec2 tsz = ImGui::CalcTextSize(lbl);
    dl->AddText({ay_x - 8.0f - tsz.x, py - tsz.y * 0.5f}, text_col, lbl);
  }
}

// ---------------------------------------------------------------------------
// Main entry point
// ---------------------------------------------------------------------------
void render_component(ImDrawList *dl, const Component &comp, const Layout &layout, AppState &state,
                      ImVec2 canvas_pos, ImVec2 canvas_size) {
  VP vp{canvas_pos.x, canvas_pos.y, canvas_size.x, canvas_size.y,
        state.zoom,   state.pan_x,  state.pan_y};

  bool anything_selected = (state.selected_rect >= 0);

  // Edges first (lower z-order)
  draw_edges(dl, comp, layout, vp);

  // Rects
  for (int i = 0; i < static_cast<int>(comp.rects.size()); ++i) {
    const Rect &r = comp.rects[static_cast<size_t>(i)];

    ImU32 fill_col, border_col;
    if (!anything_selected) {
      fill_col = IM_COL32(70, 130, 180, 100);
      border_col = IM_COL32(50, 90, 140, 220);
    } else if (i == state.selected_rect) {
      fill_col = IM_COL32(255, 170, 30, 200);
      border_col = IM_COL32(180, 100, 0, 255);
    } else if (static_cast<size_t>(i) < state.is_ancestor.size() &&
               state.is_ancestor[static_cast<size_t>(i)]) {
      fill_col = IM_COL32(70, 130, 180, 200);
      border_col = IM_COL32(50, 90, 140, 255);
    } else {
      fill_col = IM_COL32(70, 130, 180, 28);
      border_col = IM_COL32(50, 90, 140, 60);
    }

    draw_rect(dl, r, layout, vp, fill_col, border_col);
  }

  draw_axes(dl, comp, layout, vp);
}
