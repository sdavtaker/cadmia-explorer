#include "svg.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <sstream>
#include <string>
#include <vector>

// Use R"SVG(...)SVG" as the raw string delimiter throughout this file.
// Plain R"(...)" would prematurely end when the SVG content contains )".

namespace {

// ---- String helpers --------------------------------------------------------

std::string fmt(const char *fmt_str, ...) {
  // Two-pass: first measure, then format.
  va_list args;
  va_list args2;
  va_start(args, fmt_str);
  va_copy(args2, args);
  int n = vsnprintf(nullptr, 0, fmt_str, args);
  va_end(args);
  std::string s(static_cast<size_t>(n + 1), '\0');
  vsnprintf(s.data(), s.size(), fmt_str, args2);
  va_end(args2);
  s.resize(static_cast<size_t>(n));
  return s;
}

std::string xml_escape(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
    case '&':
      out += "&amp;";
      break;
    case '<':
      out += "&lt;";
      break;
    case '>':
      out += "&gt;";
      break;
    case '"':
      out += "&quot;";
      break;
    default:
      out += c;
    }
  }
  return out;
}

// ---- SVG building blocks ---------------------------------------------------

std::string svg_line(double x1, double y1, double x2, double y2, const char *stroke, double width,
                     const char *dasharray = nullptr) {
  std::string s =
      fmt(R"SVG(<line x1="%.2f" y1="%.2f" x2="%.2f" y2="%.2f" stroke="%s" stroke-width="%.1f")SVG",
          x1, y1, x2, y2, stroke, width);
  if (dasharray != nullptr) {
    s += fmt(R"SVG( stroke-dasharray="%s")SVG", dasharray);
  }
  s += "/>\n";
  return s;
}

std::string svg_text(double x, double y, const std::string &text, const char *anchor = "middle",
                     double font_size = 11.0, const char *fill = "#333") {
  return fmt(
      R"SVG(<text x="%.2f" y="%.2f" text-anchor="%s" font-size="%.0f" fill="%s">%s</text>)SVG"
      "\n",
      x, y, anchor, font_size, fill, xml_escape(text).c_str());
}

// ---- Rect rendering --------------------------------------------------------

std::string draw_rect_sides(double x1, double y1, double x2, double y2, bool left_solid,
                            bool right_solid, bool top_solid, bool bottom_solid,
                            const char *stroke = "#2060a0", double stroke_width = 1.5) {
  const char *dash = "4,3";
  std::string s;
  // Semi-transparent fill
  s += fmt(
      R"SVG(<rect x="%.2f" y="%.2f" width="%.2f" height="%.2f" fill="rgba(70,130,180,0.25)" stroke="none"/>)SVG"
      "\n",
      x1, y1, x2 - x1, y2 - y1);
  // Left edge  (time_lo)
  s += svg_line(x1, y1, x1, y2, stroke, stroke_width, left_solid ? nullptr : dash);
  // Right edge (time_hi)
  s += svg_line(x2, y1, x2, y2, stroke, stroke_width, right_solid ? nullptr : dash);
  // Top edge   (out_hi)
  s += svg_line(x1, y1, x2, y1, stroke, stroke_width, top_solid ? nullptr : dash);
  // Bottom edge (out_lo)
  s += svg_line(x1, y2, x2, y2, stroke, stroke_width, bottom_solid ? nullptr : dash);
  return s;
}

std::string draw_multiplicity(double x2, double y1, uint32_t count) {
  if (count <= 1) {
    return "";
  }
  std::string label = "\xc3\x97" + std::to_string(count); // UTF-8 "×"
  return fmt(
      R"SVG(<text x="%.2f" y="%.2f" text-anchor="end" font-size="9" fill="#c00000">%s</text>)SVG"
      "\n",
      x2 - 2.0, y1 + 10.0, xml_escape(label).c_str());
}

// ---- Edge rendering --------------------------------------------------------

std::string svg_bezier(double x1, double y1, double x2, double y2, double ctrl_offset) {
  double cx1 = x1 + ctrl_offset;
  double cx2 = x2 - ctrl_offset;
  return fmt(
      R"SVG(<path d="M%.2f,%.2f C%.2f,%.2f %.2f,%.2f %.2f,%.2f" fill="none" stroke="#888" stroke-width="1" opacity="0.5" marker-end="url(#arr)"/>)SVG"
      "\n",
      x1, y1, cx1, y1, cx2, y2, x2, y2);
}

std::string svg_arrow_line(double x1, double y1, double x2, double y2) {
  return fmt(
      R"SVG(<line x1="%.2f" y1="%.2f" x2="%.2f" y2="%.2f" stroke="#888" stroke-width="1" opacity="0.5" marker-end="url(#arr)"/>)SVG"
      "\n",
      x1, y1, x2, y2);
}

// ---- Axis helpers ----------------------------------------------------------

std::vector<double> axis_ticks(double lo, double hi, int n_ticks) {
  std::vector<double> ticks;
  if (hi <= lo || n_ticks < 2) {
    return ticks;
  }
  double step = (hi - lo) / (n_ticks - 1);
  for (int i = 0; i < n_ticks; ++i) {
    ticks.push_back(lo + i * step);
  }
  return ticks;
}

std::string fmt_time(double t) {
  std::array<char, 64> buf;
  snprintf(buf.data(), buf.size(), "%.3f", t);
  std::string s = buf.data();
  if (s.find('.') != std::string::npos) {
    while (s.back() == '0') {
      s.pop_back();
    }
    if (s.back() == '.') {
      s.pop_back();
    }
  }
  return s;
}

} // namespace

// ---- Main render function --------------------------------------------------

std::string render_svg(const Component &comp, const Layout &L) {
  std::ostringstream out;

  // SVG header
  out << fmt(
      R"SVG(<?xml version="1.0" encoding="UTF-8"?>)SVG"
      "\n"
      R"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="%.0f" height="%.0f" viewBox="0 0 %.0f %.0f">)SVG"
      "\n",
      Layout::CANVAS_W, Layout::CANVAS_H, Layout::CANVAS_W, Layout::CANVAS_H);

  // Arrowhead marker
  out << "<defs>\n"
         R"SVG(  <marker id="arr" markerWidth="8" markerHeight="6" refX="7" refY="3" orient="auto">)SVG"
         "\n"
         R"SVG(    <polygon points="0 0, 8 3, 0 6" fill="#888" opacity="0.6"/>)SVG"
         "\n"
         "  </marker>\n"
         "</defs>\n";

  // Canvas background
  out << fmt(R"SVG(<rect width="%.0f" height="%.0f" fill="#fafafa"/>)SVG"
             "\n",
             Layout::CANVAS_W, Layout::CANVAS_H);

  // Plot area
  out << fmt(
      R"SVG(<rect x="%.2f" y="%.2f" width="%.2f" height="%.2f" fill="#ffffff" stroke="#cccccc" stroke-width="1"/>)SVG"
      "\n",
      Layout::PLOT_X, Layout::PLOT_Y, Layout::PLOT_W, Layout::PLOT_H);

  // Title
  out << fmt(
      R"SVG(<text x="%.2f" y="18" text-anchor="middle" font-size="14" font-weight="bold" fill="#222">%s</text>)SVG"
      "\n",
      Layout::CANVAS_W / 2.0, xml_escape(comp.name).c_str());

  // Edges (drawn under rects)
  for (const auto &edge : comp.edges) {
    if (edge.from >= comp.rects.size() || edge.to >= comp.rects.size()) {
      continue;
    }
    const Rect &fr = comp.rects[edge.from];
    const Rect &tr = comp.rects[edge.to];

    double sx = L.time_to_px(fr.time_hi);
    double sy = (L.out_to_py(fr.out_lo) + L.out_to_py(fr.out_hi)) / 2.0;
    double tx = L.time_to_px(tr.time_lo);
    double ty = (L.out_to_py(tr.out_lo) + L.out_to_py(tr.out_hi)) / 2.0;

    if (std::abs(tx - sx) < 10.0) {
      out << svg_bezier(sx, sy, tx, ty, 40.0);
    } else {
      out << svg_arrow_line(sx, sy, tx, ty);
    }
  }

  // Rectangles
  for (const auto &rect : comp.rects) {
    double x1 = L.time_to_px(rect.time_lo);
    double x2 = L.time_to_px(rect.time_hi);
    double y1 = L.out_to_py(rect.out_hi); // high out = top of rect (small y)
    double y2 = L.out_to_py(rect.out_lo); // low  out = bottom (large y)

    // Enforce min pixel size around center
    if (x2 - x1 < Layout::MIN_PX) {
      double cx = (x1 + x2) / 2.0;
      x1 = cx - Layout::MIN_PX / 2.0;
      x2 = cx + Layout::MIN_PX / 2.0;
    }
    if (y2 - y1 < Layout::MIN_PX) {
      double cy = (y1 + y2) / 2.0;
      y1 = cy - Layout::MIN_PX / 2.0;
      y2 = cy + Layout::MIN_PX / 2.0;
    }

    out << draw_rect_sides(x1, y1, x2, y2, rect.time_lo_closed, rect.time_hi_closed,
                           rect.out_hi_closed, rect.out_lo_closed);
    out << draw_multiplicity(x2, y1, rect.multiplicity);
  }

  // X axis
  {
    double ax_y = Layout::PLOT_Y + Layout::PLOT_H;
    out << svg_line(Layout::PLOT_X, ax_y, Layout::PLOT_X + Layout::PLOT_W, ax_y, "#333", 1.0);
    for (double t : axis_ticks(L.t_min, L.t_max, 8)) {
      double px = L.time_to_px(t);
      if (px < Layout::PLOT_X - 1 || px > Layout::PLOT_X + Layout::PLOT_W + 1) {
        continue;
      }
      out << svg_line(px, ax_y, px, ax_y + 5, "#333", 1.0);
      out << svg_text(px, ax_y + 17, fmt_time(t));
    }
    out << svg_text(Layout::PLOT_X + Layout::PLOT_W / 2.0, ax_y + 40, "simulation time", "middle",
                    11.0, "#555");
  }

  // Y axis
  {
    double ax_x = Layout::PLOT_X;
    out << svg_line(ax_x, Layout::PLOT_Y, ax_x, Layout::PLOT_Y + Layout::PLOT_H, "#333", 1.0);

    // Collect unique Y values with their labels (ordered low→high)
    std::map<double, std::string> y_labels;
    for (const auto &rect : comp.rects) {
      y_labels[rect.out_lo] = rect.out_label;
    }

    int label_stride = (y_labels.size() > 12) ? 2 : 1;
    int idx = 0;
    for (const auto &[y_val, label] : y_labels) {
      double py = L.out_to_py(y_val);
      if (py < Layout::PLOT_Y - 1 || py > Layout::PLOT_Y + Layout::PLOT_H + 1) {
        ++idx;
        continue;
      }
      out << svg_line(ax_x - 5, py, ax_x, py, "#333", 1.0);
      if ((idx % label_stride) == 0) {
        out << svg_text(ax_x - 8, py + 4, label, "end", 10.0, "#333");
      }
      ++idx;
    }
  }

  out << "</svg>\n";
  return out.str();
}
