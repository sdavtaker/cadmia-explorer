#include "layout.h"
#include <limits>

bool Layout::from_component(const Component& comp, Layout& out) {
    if (comp.rects.empty()) return false;

    double t_min =  std::numeric_limits<double>::max();
    double t_max = -std::numeric_limits<double>::max();
    double y_min =  std::numeric_limits<double>::max();
    double y_max = -std::numeric_limits<double>::max();

    for (const auto& r : comp.rects) {
        t_min = std::min(t_min, r.time_lo);
        t_max = std::max(t_max, r.time_hi);
        y_min = std::min(y_min, r.out_lo);
        y_max = std::max(y_max, r.out_hi);
    }

    // Add 5% padding around the data so rects don't touch the axes
    double t_pad = (t_max > t_min) ? (t_max - t_min) * 0.05 : 0.5;
    double y_pad = (y_max > y_min) ? (y_max - y_min) * 0.10 : 0.5;

    out.t_min = t_min - t_pad;
    out.t_max = t_max + t_pad;
    out.y_min = y_min - y_pad;
    out.y_max = y_max + y_pad;

    return true;
}
