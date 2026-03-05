#pragma once
#include "reader.h"
#include <algorithm>
#include <limits>

/// Canvas and axis geometry for a single component's SVG.
struct Layout {
  // Canvas size
  static constexpr double CANVAS_W = 1200.0;
  static constexpr double CANVAS_H = 900.0;

  // Margins around the plot area
  static constexpr double MARGIN_L = 80.0;
  static constexpr double MARGIN_R = 20.0;
  static constexpr double MARGIN_T = 30.0;
  static constexpr double MARGIN_B = 60.0;

  // Plot area dimensions (derived)
  static constexpr double PLOT_X = MARGIN_L;
  static constexpr double PLOT_Y = MARGIN_T;
  static constexpr double PLOT_W = CANVAS_W - MARGIN_L - MARGIN_R;
  static constexpr double PLOT_H = CANVAS_H - MARGIN_T - MARGIN_B;

  // Minimum rendered rect size in pixels
  static constexpr double MIN_PX = 2.0;

  double t_min, t_max; ///< Time axis range
  double y_min, y_max; ///< Output axis range

  /// Build layout from a component's rects. Returns false if no rects.
  static bool from_component(const Component &comp, Layout &out);

  /// Map simulation time to SVG x-coordinate.
  [[nodiscard]] double time_to_px(double t) const {
    double span = (t_max > t_min) ? (t_max - t_min) : 1.0;
    return PLOT_X + (t - t_min) / span * PLOT_W;
  }

  /// Map output value to SVG y-coordinate (inverted: higher value = lower y).
  [[nodiscard]] double out_to_py(double y) const {
    double span = (y_max > y_min) ? (y_max - y_min) : 1.0;
    return PLOT_Y + PLOT_H - (y - y_min) / span * PLOT_H;
  }

  /// Pixel width/height of a rect, clamped to MIN_PX.
  [[nodiscard]] double rect_pw(const Rect &rect) const {
    return std::max(MIN_PX, time_to_px(rect.time_hi) - time_to_px(rect.time_lo));
  }
  [[nodiscard]] double rect_ph(const Rect &rect) const {
    return std::max(MIN_PX, out_to_py(rect.out_lo) - out_to_py(rect.out_hi));
  }
};
