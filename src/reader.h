#pragma once
#include <cstdint>
#include <string>
#include <vector>

/// One rectangle in a per-component plot.
struct Rect {
  double time_lo, time_hi; ///< X extent (simulation time)
  double out_lo, out_hi;   ///< Y extent (output value, mapped to float)
  bool time_lo_closed, time_hi_closed;
  bool out_lo_closed, out_hi_closed;
  std::string out_label; ///< Original output string (for Y-axis labels)
  uint32_t multiplicity; ///< How many branches collapsed to this rect
};

/// Directed edge between two rectangles in the same component.
struct Edge {
  uint32_t from; ///< Index into Component::rects
  uint32_t to;
};

struct Component {
  std::string name;
  std::vector<Rect> rects;
  std::vector<Edge> edges;
};

struct CadvisFile {
  std::vector<Component> components;
};

/// Load a .cadvis binary file. Throws std::runtime_error on failure.
CadvisFile read_cadvis(const std::string &path);
