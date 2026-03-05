#pragma once
#include "reader.h"

#include <cstdint>
#include <vector>

/// Application state shared between the event loop (app.cpp) and renderer.
struct AppState {
    int cur_comp = 0;           ///< Index into CadvisFile::components

    float pan_x = 0.0f;        ///< Canvas pan, screen pixels
    float pan_y = 0.0f;
    float zoom  = 1.0f;        ///< Canvas zoom multiplier

    int selected_rect      = -1; ///< Index of clicked rect, or -1
    int selected_sub       =  0; ///< Which incoming edge to follow for ancestor highlighting
    int selected_sub_count =  0; ///< Number of incoming edges to selected_rect

    std::vector<bool>                     is_ancestor; ///< Per-rect: on ancestor path?
    std::vector<std::vector<uint32_t>>    rev_adj;     ///< Reverse adjacency (rebuilt on comp change)
};

/// Open the interactive viewer window. Blocks until the window is closed.
void run_app(const CadvisFile& file);
