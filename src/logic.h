#pragma once
// Pure-compute functions with no GL or ImGui dependencies.
// Suitable for direct unit testing without a graphics context.
#include "layout.h"
#include "reader.h"

#include <cstdint>
#include <queue>
#include <vector>

/// Build a reverse-adjacency list for a component's edge graph.
/// result[i] contains the indices of all rects that have an edge pointing TO rect i.
inline std::vector<std::vector<uint32_t>>
build_reverse_adj(const Component& comp)
{
    std::vector<std::vector<uint32_t>> rev(comp.rects.size());
    for (const auto& e : comp.edges) {
        if (e.from < comp.rects.size() && e.to < comp.rects.size())
            rev[e.to].push_back(e.from);
    }
    return rev;
}

/// Compute which rects are ancestors of selected_rect along the selected_sub-th
/// incoming path.
///
/// Algorithm:
///   1. If selected_rect has no incoming edges (root), return {selected_rect}.
///   2. Otherwise, seed the BFS with the selected_sub-th entry in rev_adj[selected_rect].
///   3. BFS backwards, following ALL incoming edges of each visited node.
///
/// Returns a vector<bool> of size comp.rects.size().
/// selected_rect itself is always marked true.
inline std::vector<bool>
compute_ancestors(const Component& comp,
                  const std::vector<std::vector<uint32_t>>& rev_adj,
                  int selected_rect, int selected_sub)
{
    int n = static_cast<int>(comp.rects.size());
    std::vector<bool> result(n, false);
    if (selected_rect < 0 || selected_rect >= n)
        return result;

    result[selected_rect] = true;

    const auto& incoming = rev_adj[static_cast<size_t>(selected_rect)];
    if (incoming.empty())
        return result; // root rect — no ancestors

    int sub = (selected_sub >= 0 && selected_sub < static_cast<int>(incoming.size()))
              ? selected_sub : 0;

    std::queue<uint32_t> q;
    q.push(incoming[static_cast<size_t>(sub)]);

    while (!q.empty()) {
        uint32_t cur = q.front();
        q.pop();
        if (cur >= static_cast<uint32_t>(n) || result[cur])
            continue;
        result[cur] = true;
        for (uint32_t pred : rev_adj[cur]) {
            if (pred < static_cast<uint32_t>(n) && !result[pred])
                q.push(pred);
        }
    }

    return result;
}

/// Find the topmost rect (highest index in rects array) whose screen bounding
/// box contains the cursor at (mx, my).  Returns -1 if none.
///
/// Coordinate transform (layout → screen):
///   screen_x = canvas_ox + (layout_px / CANVAS_W * canvas_w) * zoom + pan_x
///   screen_y = canvas_oy + (layout_py / CANVAS_H * canvas_h) * zoom + pan_y
///
/// All parameters are plain floats — no ImGui types.
inline int
find_rect_under_cursor(const Component& comp, const Layout& layout,
                       float canvas_ox, float canvas_oy,
                       float canvas_w,  float canvas_h,
                       float pan_x, float pan_y, float zoom,
                       float mx, float my)
{
    if (canvas_w <= 0.0f || canvas_h <= 0.0f || zoom <= 0.0f)
        return -1;

    // Inverse transform: screen → layout coordinates
    float lx = (mx - canvas_ox - pan_x) / zoom * static_cast<float>(Layout::CANVAS_W) / canvas_w;
    float ly = (my - canvas_oy - pan_y) / zoom * static_cast<float>(Layout::CANVAS_H) / canvas_h;

    int hit = -1;
    for (int i = 0; i < static_cast<int>(comp.rects.size()); ++i) {
        const Rect& r = comp.rects[static_cast<size_t>(i)];
        float x1 = static_cast<float>(layout.time_to_px(r.time_lo));
        float x2 = x1 + static_cast<float>(layout.rect_pw(r));
        float y1 = static_cast<float>(layout.out_to_py(r.out_hi)); // top (lower py)
        float y2 = y1 + static_cast<float>(layout.rect_ph(r));

        if (lx >= x1 && lx <= x2 && ly >= y1 && ly <= y2)
            hit = i; // take the last (topmost z-order) match
    }
    return hit;
}
