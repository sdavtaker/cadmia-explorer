#ifdef CADVIS_GUI

#include "gpu_rect.h"

// On Emscripten (WebGL2) the GLES3 header provides all entry points directly.
// On desktop Linux/Mac, <GL/glext.h> with GL_GLEXT_PROTOTYPES declares every
// GL 3.x core function; Mesa/libGL exports them from the linked libGL.so.
#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

// ---------------------------------------------------------------------------
// Shader sources
// ---------------------------------------------------------------------------

// Preambles (version + precision) are prepended separately so the body is
// shared between desktop (330 core) and WASM (300 es).
#ifdef __EMSCRIPTEN__
static constexpr const char *VERT_PREAMBLE = "#version 300 es\n"
                                             "precision highp float;\n"
                                             "precision highp int;\n"
                                             "precision highp isampler2D;\n";
static constexpr const char *FRAG_PREAMBLE = "#version 300 es\n"
                                             "precision highp float;\n"
                                             "precision highp int;\n"
                                             "precision highp isampler2D;\n";
#else
static constexpr const char *VERT_PREAMBLE = "#version 330 core\n";
static constexpr const char *FRAG_PREAMBLE = "#version 330 core\n";
#endif

// ---------------------------------------------------------------------------
// Vertex shader body
// ---------------------------------------------------------------------------
// Per-instance attribs (divisor=1):
//   location 0: float a_time_lo
//   location 1: float a_time_hi
//   location 2: float a_out_lo
//   location 3: float a_out_hi
//   location 4: uint  a_flags
//
// Coordinate transform (collapses VP::sx/sy + time_to_px/out_to_py):
//   screen_x = A_x * t   + B_x
//   screen_y = A_y * y   + B_y   (A_y is negative — Y inverted)
// ---------------------------------------------------------------------------
static constexpr const char *VERT_BODY = R"GLSL(
layout(location = 0) in float a_time_lo;
layout(location = 1) in float a_time_hi;
layout(location = 2) in float a_out_lo;
layout(location = 3) in float a_out_hi;
layout(location = 4) in uint  a_flags;

uniform vec2 u_x_transform;     // (A_x, B_x)
uniform vec2 u_y_transform;     // (A_y, B_y)  — A_y negative (Y inverted)
uniform vec4 u_clip_rect;       // (x1,y1,x2,y2) screen px — canvas clip rect
uniform vec2 u_display_size;    // (fb_w, fb_h) for NDC conversion
uniform isampler2D u_state_tex; // 1 x N_RECTS, GL_R8UI

out vec2      v_uv;       // [0,1]^2 in rect
out vec2      v_rect_size;
flat out uint v_flags;
flat out int  v_state;

const vec2 CORNERS[6] = vec2[6](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
    vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));

void main() {
    float sx1 = u_x_transform.x * a_time_lo + u_x_transform.y;
    float sx2 = u_x_transform.x * a_time_hi + u_x_transform.y;
    // out_hi maps to the top (smaller screen-y); out_lo maps to the bottom
    float sy1 = u_y_transform.x * a_out_hi + u_y_transform.y;
    float sy2 = u_y_transform.x * a_out_lo + u_y_transform.y;

    // Viewport cull: emit degenerate triangle if entirely outside clip rect
    if (sx2 < u_clip_rect.x || sx1 > u_clip_rect.z ||
        sy2 < u_clip_rect.y || sy1 > u_clip_rect.w) {
        gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
        v_uv = vec2(0.0);
        v_rect_size = vec2(0.0);
        v_flags = 0u;
        v_state = 0;
        return;
    }

    vec2 c  = CORNERS[gl_VertexID % 6];
    float sx = mix(sx1, sx2, c.x);
    float sy = mix(sy1, sy2, c.y);

    // NDC: Y-down screen to Y-up clip (matches ImGui orthographic projection)
    gl_Position = vec4(
        2.0 * sx / u_display_size.x - 1.0,
        1.0 - 2.0 * sy / u_display_size.y,
        0.0, 1.0);

    v_uv        = c;
    v_rect_size = vec2(sx2 - sx1, sy2 - sy1);
    v_flags     = a_flags;
    v_state     = int(texelFetch(u_state_tex, ivec2(gl_InstanceID, 0), 0).r);
}
)GLSL";

// ---------------------------------------------------------------------------
// Fragment shader body
// ---------------------------------------------------------------------------
// Border detection via pixel distance from each edge.
// Dashed pattern for open-bound sides using mod().
// ---------------------------------------------------------------------------
static constexpr const char *FRAG_BODY = R"GLSL(
in  vec2      v_uv;
in  vec2      v_rect_size;
flat in uint  v_flags;
flat in int   v_state;

out vec4 frag_color;

const float BWIDTH     = 1.5;   // border width in pixels
const float DASH_ON    = 4.0;   // pixels on
const float DASH_CYCLE = 7.0;   // on + off

bool dash_visible(float coord) { return mod(coord, DASH_CYCLE) < DASH_ON; }

// Fill colours (match IM_COL32 constants in renderer.cpp)
// state: 0=dim  1=normal  2=ancestor  3=selected
vec4 fill_col() {
    if (v_state == 3) { return vec4(1.000, 0.667, 0.118, 0.784); } // orange
    if (v_state == 2) { return vec4(0.275, 0.510, 0.706, 0.784); } // bright blue
    if (v_state == 0) { return vec4(0.275, 0.510, 0.706, 0.110); } // dim blue
    return vec4(0.275, 0.510, 0.706, 0.392);                        // normal blue
}
vec4 border_col() {
    if (v_state == 3) { return vec4(0.706, 0.392, 0.000, 1.0); }
    if (v_state == 2) { return vec4(0.196, 0.353, 0.549, 1.0); }
    if (v_state == 0) { return vec4(0.196, 0.353, 0.549, 0.235); }
    return vec4(0.196, 0.353, 0.549, 0.863);
}

void main() {
    vec2 px  = v_uv * v_rect_size;           // pixels from top-left
    vec2 pxr = v_rect_size - px;             // pixels from bottom-right

    bool on_left  = px.x  < BWIDTH;
    bool on_right = pxr.x < BWIDTH;
    bool on_top   = px.y  < BWIDTH;
    bool on_bot   = pxr.y < BWIDTH;

    // flag bits: 0=time_lo_closed(left) 1=time_hi_closed(right)
    //            2=out_lo_closed(bot)   3=out_hi_closed(top)
    bool is_border = false;
    if (on_left  && (bool(v_flags & 1u) || dash_visible(px.y)))  { is_border = true; }
    if (on_right && (bool(v_flags & 2u) || dash_visible(px.y)))  { is_border = true; }
    if (on_top   && (bool(v_flags & 8u) || dash_visible(px.x)))  { is_border = true; }
    if (on_bot   && (bool(v_flags & 4u) || dash_visible(px.x)))  { is_border = true; }

    frag_color = is_border ? border_col() : fill_col();
}
)GLSL";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static unsigned int compile_shader(GLenum type, const char *preamble, const char *body) {
  unsigned int shader = glCreateShader(type);
  std::array<const char *, 2> srcs = {preamble, body};
  glShaderSource(shader, 2, srcs.data(), nullptr);
  glCompileShader(shader);
  GLint ok = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (ok == GL_FALSE) {
    std::array<char, 1024> info_log{};
    glGetShaderInfoLog(shader, 1024, nullptr, info_log.data());
    std::fprintf(stderr, "gpu_rect: shader compile error (%s):\n%s\n",
                 type == GL_VERTEX_SHADER ? "vert" : "frag", info_log.data());
  }
  return shader;
}

// ---------------------------------------------------------------------------
// GpuRectRenderer::init
// ---------------------------------------------------------------------------
void GpuRectRenderer::init() {
  // Compile shaders
  unsigned int vs = compile_shader(GL_VERTEX_SHADER, VERT_PREAMBLE, VERT_BODY);
  unsigned int fs = compile_shader(GL_FRAGMENT_SHADER, FRAG_PREAMBLE, FRAG_BODY);

  prog = glCreateProgram();
  glAttachShader(prog, vs);
  glAttachShader(prog, fs);
  glLinkProgram(prog);
  {
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (ok == GL_FALSE) {
      std::array<char, 1024> info_log{};
      glGetProgramInfoLog(prog, 1024, nullptr, info_log.data());
      std::fprintf(stderr, "gpu_rect: link error:\n%s\n", info_log.data());
    }
  }
  glDeleteShader(vs);
  glDeleteShader(fs);

  // Cache uniform locations
  u_x_transform = glGetUniformLocation(prog, "u_x_transform");
  u_y_transform = glGetUniformLocation(prog, "u_y_transform");
  u_clip_rect = glGetUniformLocation(prog, "u_clip_rect");
  u_display_size = glGetUniformLocation(prog, "u_display_size");
  u_state_tex = glGetUniformLocation(prog, "u_state_tex");

  // VAO + VBO
  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);

  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);

  // loc 0: time_lo (float)
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 1, GL_FLOAT, GL_FALSE, sizeof(RectInstance),
                        reinterpret_cast<const void *>(
                            offsetof(RectInstance, time_lo))); // NOLINT(performance-no-int-to-ptr)
  glVertexAttribDivisor(0, 1);

  // loc 1: time_hi (float)
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(RectInstance),
                        reinterpret_cast<const void *>(
                            offsetof(RectInstance, time_hi))); // NOLINT(performance-no-int-to-ptr)
  glVertexAttribDivisor(1, 1);

  // loc 2: out_lo (float)
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(RectInstance),
                        reinterpret_cast<const void *>(
                            offsetof(RectInstance, out_lo))); // NOLINT(performance-no-int-to-ptr)
  glVertexAttribDivisor(2, 1);

  // loc 3: out_hi (float)
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(RectInstance),
                        reinterpret_cast<const void *>(
                            offsetof(RectInstance, out_hi))); // NOLINT(performance-no-int-to-ptr)
  glVertexAttribDivisor(3, 1);

  // loc 4: flags (uint) — must use IPointer to preserve integer bits
  glEnableVertexAttribArray(4);
  glVertexAttribIPointer(4, 1, GL_UNSIGNED_INT, sizeof(RectInstance),
                         reinterpret_cast<const void *>(
                             offsetof(RectInstance, flags))); // NOLINT(performance-no-int-to-ptr)
  glVertexAttribDivisor(4, 1);

  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  // State texture: 1×1 placeholder; resized in upload_state()
  glGenTextures(1, &state_tex);
  glBindTexture(GL_TEXTURE_2D, state_tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, 1, 1, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
}

// ---------------------------------------------------------------------------
// GpuRectRenderer::destroy
// ---------------------------------------------------------------------------
void GpuRectRenderer::destroy() {
  if (vao != 0U) {
    glDeleteVertexArrays(1, &vao);
    vao = 0;
  }
  if (vbo != 0U) {
    glDeleteBuffers(1, &vbo);
    vbo = 0;
  }
  if (prog != 0U) {
    glDeleteProgram(prog);
    prog = 0;
  }
  if (state_tex != 0U) {
    glDeleteTextures(1, &state_tex);
    state_tex = 0;
  }
  n_instances = 0;
}

// ---------------------------------------------------------------------------
// GpuRectRenderer::upload_rects
// ---------------------------------------------------------------------------
void GpuRectRenderer::upload_rects(const Component &comp) {
  std::vector<RectInstance> instances;
  instances.reserve(comp.rects.size());

  for (const auto &r : comp.rects) {
    RectInstance inst{};
    inst.time_lo = static_cast<float>(r.time_lo);
    inst.time_hi = static_cast<float>(r.time_hi);
    inst.out_lo = static_cast<float>(r.out_lo);
    inst.out_hi = static_cast<float>(r.out_hi);
    uint32_t flags = 0U;
    if (r.time_lo_closed) {
      flags |= 1U;
    }
    if (r.time_hi_closed) {
      flags |= 2U;
    }
    if (r.out_lo_closed) {
      flags |= 4U;
    }
    if (r.out_hi_closed) {
      flags |= 8U;
    }
    inst.flags = flags;
    instances.push_back(inst);
  }

  n_instances = static_cast<int>(instances.size());

  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(instances.size() * sizeof(RectInstance)),
               instances.empty() ? nullptr : instances.data(), GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// ---------------------------------------------------------------------------
// GpuRectRenderer::upload_state
// ---------------------------------------------------------------------------
void GpuRectRenderer::upload_state(const AppState &state) const {
  if (n_instances <= 0) {
    return;
  }

  bool anything_selected = (state.selected_rect >= 0);
  std::vector<uint8_t> tex(static_cast<size_t>(n_instances));

  for (int i = 0; i < n_instances; ++i) {
    uint8_t s = 1; // normal
    if (anything_selected) {
      if (i == state.selected_rect) {
        s = 3; // selected
      } else if (static_cast<size_t>(i) < state.is_ancestor.size() &&
                 state.is_ancestor[static_cast<size_t>(i)]) {
        s = 2; // ancestor
      } else {
        s = 0; // dim
      }
    }
    tex[static_cast<size_t>(i)] = s;
  }

  glBindTexture(GL_TEXTURE_2D, state_tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, n_instances, 1, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE,
               tex.data());
  glBindTexture(GL_TEXTURE_2D, 0);
}

// ---------------------------------------------------------------------------
// Callback payload — heap-allocated in draw(), deleted in callback
// ---------------------------------------------------------------------------
struct RectDrawPayload {
  GpuRectRenderer *renderer;
  float A_x, B_x; // x screen transform
  float A_y, B_y; // y screen transform (A_y negative)
  float clip_x1, clip_y1, clip_x2, clip_y2;
  float display_w, display_h;
};

// ---------------------------------------------------------------------------
// gpu_rect_callback — injected into ImDrawList via AddCallback
// ---------------------------------------------------------------------------
static void gpu_rect_callback(const ImDrawList * /*dl*/, const ImDrawCmd *cmd) {
  auto *payload = // NOLINT(cppcoreguidelines-owning-memory)
      static_cast<RectDrawPayload *>(cmd->UserCallbackData);
  GpuRectRenderer *r = payload->renderer;

  if (r->n_instances <= 0) {
    delete payload; // NOLINT(cppcoreguidelines-owning-memory)
    return;
  }

  // ── save GL state ─────────────────────────────────────────────────────────
  GLint prev_prog = 0;
  glGetIntegerv(GL_CURRENT_PROGRAM, &prev_prog);
  GLint prev_vao = 0;
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prev_vao);
  GLint prev_tex_unit = 0;
  glGetIntegerv(GL_ACTIVE_TEXTURE, &prev_tex_unit);
  glActiveTexture(GL_TEXTURE0);
  GLint prev_tex2d = 0;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex2d);
  GLboolean prev_blend = glIsEnabled(GL_BLEND);
  GLint prev_blend_src_rgb = 0;
  GLint prev_blend_dst_rgb = 0;
  GLint prev_blend_src_alpha = 0;
  GLint prev_blend_dst_alpha = 0;
  glGetIntegerv(GL_BLEND_SRC_RGB, &prev_blend_src_rgb);
  glGetIntegerv(GL_BLEND_DST_RGB, &prev_blend_dst_rgb);
  glGetIntegerv(GL_BLEND_SRC_ALPHA, &prev_blend_src_alpha);
  glGetIntegerv(GL_BLEND_DST_ALPHA, &prev_blend_dst_alpha);
  std::array<GLint, 4> prev_scissor{};
  glGetIntegerv(GL_SCISSOR_BOX, prev_scissor.data());

  // ── scissor (OpenGL Y is bottom-up; payload Y is top-down) ───────────────
  glScissor(static_cast<GLint>(payload->clip_x1),
            static_cast<GLint>(payload->display_h - payload->clip_y2),
            static_cast<GLsizei>(payload->clip_x2 - payload->clip_x1),
            static_cast<GLsizei>(payload->clip_y2 - payload->clip_y1));

  // ── blending ──────────────────────────────────────────────────────────────
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

  // ── bind program + uniforms ───────────────────────────────────────────────
  glUseProgram(r->prog);
  glUniform2f(r->u_x_transform, payload->A_x, payload->B_x);
  glUniform2f(r->u_y_transform, payload->A_y, payload->B_y);
  glUniform4f(r->u_clip_rect, payload->clip_x1, payload->clip_y1, payload->clip_x2,
              payload->clip_y2);
  glUniform2f(r->u_display_size, payload->display_w, payload->display_h);
  glUniform1i(r->u_state_tex, 0); // texture unit 0

  // ── state texture + draw ──────────────────────────────────────────────────
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, r->state_tex);
  glBindVertexArray(r->vao);
  glDrawArraysInstanced(GL_TRIANGLES, 0, 6, r->n_instances);
  glBindVertexArray(0);

  // ── restore GL state ──────────────────────────────────────────────────────
  glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prev_tex2d));
  glActiveTexture(static_cast<GLenum>(prev_tex_unit));
  glUseProgram(static_cast<GLuint>(prev_prog));
  glBindVertexArray(static_cast<GLuint>(prev_vao));
  if (prev_blend == GL_FALSE) {
    glDisable(GL_BLEND);
  }
  glBlendFuncSeparate(
      static_cast<GLenum>(prev_blend_src_rgb), static_cast<GLenum>(prev_blend_dst_rgb),
      static_cast<GLenum>(prev_blend_src_alpha), static_cast<GLenum>(prev_blend_dst_alpha));
  glScissor(prev_scissor[0], prev_scissor[1], prev_scissor[2], prev_scissor[3]);

  delete payload; // NOLINT(cppcoreguidelines-owning-memory)
}

// ---------------------------------------------------------------------------
// GpuRectRenderer::draw
// ---------------------------------------------------------------------------
void GpuRectRenderer::draw(ImDrawList *dl, ImVec2 canvas_pos, ImVec2 canvas_size,
                           ImVec2 display_size, ImVec4 clip_rect, const Layout &layout,
                           const AppState &state) {
  if (n_instances <= 0) {
    return;
  }

  // Compute linear transform uniforms that collapse the full coordinate chain:
  //   sim_t -> layout_x -> screen_x  (VP::sx composed with time_to_px)
  //   sim_y -> layout_y -> screen_y  (VP::sy composed with out_to_py)
  //
  // screen_x = A_x * t + B_x
  // screen_y = A_y * y + B_y   (A_y negative — Y inverted)

  double t_span = (layout.t_max > layout.t_min) ? (layout.t_max - layout.t_min) : 1e-9;
  double y_span = (layout.y_max > layout.y_min) ? (layout.y_max - layout.y_min) : 1e-9;

  float cw = canvas_size.x;
  float ch = canvas_size.y;
  float ox = canvas_pos.x;
  float oy = canvas_pos.y;
  float zx = state.zoom_x;
  float zy = state.zoom_y;
  float px_pan = state.pan_x;
  float py_pan = state.pan_y;

  auto A_x = static_cast<float>(Layout::PLOT_W / t_span / Layout::CANVAS_W) * cw * zx;
  auto B_x = ox +
             static_cast<float>((Layout::PLOT_X - layout.t_min * Layout::PLOT_W / t_span) /
                                Layout::CANVAS_W) *
                 cw * zx +
             px_pan;

  // A_y is negative because higher sim-y maps to smaller screen-y
  auto A_y = -static_cast<float>(Layout::PLOT_H / y_span / Layout::CANVAS_H) * ch * zy;
  auto B_y = oy +
             static_cast<float>(
                 (Layout::PLOT_Y + Layout::PLOT_H + layout.y_min * Layout::PLOT_H / y_span) /
                 Layout::CANVAS_H) *
                 ch * zy +
             py_pan;

  // Heap-allocate payload; deleted in callback
  auto *payload = // NOLINT(cppcoreguidelines-owning-memory)
      new RectDrawPayload();
  payload->renderer = this;
  payload->A_x = A_x;
  payload->B_x = B_x;
  payload->A_y = A_y;
  payload->B_y = B_y;
  payload->clip_x1 = clip_rect.x;
  payload->clip_y1 = clip_rect.y;
  payload->clip_x2 = clip_rect.z;
  payload->clip_y2 = clip_rect.w;
  payload->display_w = display_size.x;
  payload->display_h = display_size.y;

  dl->AddCallback(gpu_rect_callback, payload);
  // Reset ImGui's render state after raw GL draw
  dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);

  (void)state; // state used only for upload_state(); transform uses layout
}

#endif // CADVIS_GUI
