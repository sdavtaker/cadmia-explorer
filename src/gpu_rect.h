#pragma once
#ifdef CADVIS_GUI

#include "app.h"
#include "layout.h"
#include "reader.h"

#include <cstdint>
#include <imgui.h>

/// Per-instance data uploaded once per component change.
/// 20 bytes; all fields 4-byte aligned.
struct RectInstance {
  float time_lo;  ///< Simulation time left edge
  float time_hi;  ///< Simulation time right edge
  float out_lo;   ///< Output value lower bound
  float out_hi;   ///< Output value upper bound
  uint32_t flags; ///< bits: 0=time_lo_closed 1=time_hi_closed
                  ///<       2=out_lo_closed  3=out_hi_closed
};
static_assert(sizeof(RectInstance) == 20, "RectInstance must be 20 bytes");

/// GPU-accelerated instanced rect renderer.
/// One instance lives inside AppContext (app.cpp).
/// Lifecycle: init() once after GL context is ready, upload_rects() on
/// component change, upload_state() each frame, draw() inside
/// render_component().
struct GpuRectRenderer {
  unsigned int vao = 0;       ///< VAO with per-instance attribs
  unsigned int vbo = 0;       ///< Instance buffer (RectInstance[])
  unsigned int prog = 0;      ///< Linked shader program
  unsigned int state_tex = 0; ///< GL_TEXTURE_2D, 1×N, GL_R8UI
                              ///< 0=dim 1=normal 2=ancestor 3=selected
  int n_instances = 0;

  int u_x_transform = -1; ///< Cached uniform locations
  int u_y_transform = -1;
  int u_clip_rect = -1;
  int u_display_size = -1;
  int u_state_tex = -1;

  /// Compile shaders, build VAO/VBO, allocate state texture.
  /// Must be called after the GL context is current.
  void init();

  /// Free all GL resources.
  void destroy();

  /// Build VBO contents from comp.rects. Call when component changes.
  void upload_rects(const Component &comp);

  /// Update the state texture from is_ancestor / selected_rect.
  /// Call once per frame before render_component().
  void upload_state(const AppState &state) const;

  /// Push an AddCallback into dl that issues glDrawArraysInstanced.
  /// Must be called after upload_state() has run this frame.
  void draw(ImDrawList *dl, ImVec2 canvas_pos, ImVec2 canvas_size, ImVec2 display_size,
            ImVec4 clip_rect, const Layout &layout, const AppState &state);
};

#endif // CADVIS_GUI
