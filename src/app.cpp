#include "app.h"
#include "layout.h"
#include "logic.h"
#include "reader.h"
#include "renderer.h"

#ifdef CADVIS_GUI
#include "gpu_rect.h"
#endif

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <vector>

static constexpr float SIDEBAR_W = 230.0f;
#ifdef __EMSCRIPTEN__
static constexpr const char *GLSL_VERSION = "#version 300 es";
#else
static constexpr const char *GLSL_VERSION = "#version 330";
#endif

// ---------------------------------------------------------------------------
// Global application context (needed for Emscripten main-loop callback)
// ---------------------------------------------------------------------------
struct AppContext {
  CadvisFile file;
  AppState state;
  Layout layout;
  int layout_comp = -1;
  GLFWwindow *window = nullptr;
#ifdef CADVIS_GUI
  GpuRectRenderer gpu_rect;
  bool rects_dirty = true;
#endif
};

static AppContext g_ctx; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

// ---------------------------------------------------------------------------
// Helper: (re)initialise AppState for a given component index
// ---------------------------------------------------------------------------
static void reset_component(AppState &state, const CadvisFile &file, int idx) {
  state.cur_comp = idx;
  state.pan_x = 0.0f;
  state.pan_y = 0.0f;
  state.zoom_x = 1.0f;
  state.zoom_y = 1.0f;
  state.selected_rect = -1;
  state.selected_sub = 0;
  state.selected_sub_count = 0;

  const Component &comp = file.components[static_cast<size_t>(idx)];
  state.rev_adj = build_reverse_adj(comp);
  state.is_ancestor.assign(comp.rects.size(), false);
}

// ---------------------------------------------------------------------------
// Sidebar rendering
// ---------------------------------------------------------------------------
static void render_sidebar(AppState &state, const CadvisFile &file, bool &comp_changed) {
  comp_changed = false;

#ifdef __EMSCRIPTEN__
  if (ImGui::Button("Open .cadvis...")) {
    // Trigger the hidden file input defined in shell.html
    EM_ASM(document.getElementById('cadvis-input').click();); // NOLINT
  }
  ImGui::Separator();
#endif

  // Component combo
  ImGui::TextUnformatted("Component:");
  ImGui::SetNextItemWidth(-1.0f);
  if (ImGui::BeginCombo("##comp",
                        file.components.empty()
                            ? "(none)"
                            : file.components[static_cast<size_t>(state.cur_comp)].name.c_str())) {
    for (int i = 0; i < static_cast<int>(file.components.size()); ++i) {
      bool selected = (i == state.cur_comp);
      if (ImGui::Selectable(file.components[static_cast<size_t>(i)].name.c_str(), selected)) {
        if (i != state.cur_comp) {
          state.cur_comp = i;
          comp_changed = true;
        }
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }

  ImGui::Separator();

  if (file.components.empty()) {
    ImGui::TextDisabled("No components");
    return;
  }

  const Component &comp = file.components[static_cast<size_t>(state.cur_comp)];
  ImGui::Text("%d rect(s), %d edge(s)", static_cast<int>(comp.rects.size()),
              static_cast<int>(comp.edges.size()));

  ImGui::Separator();
  ImGui::TextUnformatted("Zoom:");
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::SliderFloat("##zoom_x", &state.zoom_x, 0.05f, 40.0f, "H: %.2fx",
                     ImGuiSliderFlags_Logarithmic);
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::SliderFloat("##zoom_y", &state.zoom_y, 0.05f, 40.0f, "V: %.2fx",
                     ImGuiSliderFlags_Logarithmic);
  if (ImGui::Button("Reset view")) {
    state.zoom_x = 1.0f;
    state.zoom_y = 1.0f;
    state.pan_x = 0.0f;
    state.pan_y = 0.0f;
  }

  if (state.selected_rect < 0) {
    ImGui::Separator();
    ImGui::TextDisabled("Click a rect to select");
    return;
  }

  ImGui::Separator();
  ImGui::TextUnformatted("Selected:");

  const Rect &r = comp.rects[static_cast<size_t>(state.selected_rect)];

  // Time interval
  std::array<char, 128> buf;
  std::snprintf(buf.data(), buf.size(), "%s%.4g, %.4g%s", r.time_lo_closed ? "[" : "(", r.time_lo,
                r.time_hi, r.time_hi_closed ? "]" : ")");
  ImGui::Text("  Time: %s", buf.data());

  // Output interval or label
  if (r.out_lo == r.out_hi && r.out_lo_closed && r.out_hi_closed) {
    // Punctual (mapped) output
    ImGui::Text("  Out:  %s", r.out_label.c_str());
  } else {
    std::snprintf(buf.data(), buf.size(), "%s%.4g, %.4g%s", r.out_lo_closed ? "[" : "(", r.out_lo,
                  r.out_hi, r.out_hi_closed ? "]" : ")");
    ImGui::Text("  Out:  %s", buf.data());
  }

  if (r.multiplicity > 1) {
    ImGui::Text("  \xc3\x97%u branches merged", r.multiplicity);
  }

  if (state.selected_sub_count > 1) {
    ImGui::Text("  Path: %d / %d", state.selected_sub + 1, state.selected_sub_count);
    ImGui::TextDisabled("  PgUp/PgDn: cycle");
  }
}

// ---------------------------------------------------------------------------
// Canvas rendering + input
// ---------------------------------------------------------------------------
static void render_canvas(AppState &state, const CadvisFile &file, Layout &layout, float pre_zoom_x,
                          float pre_zoom_y, float pre_pan_x, float pre_pan_y) {
  if (file.components.empty()) {
    ImGui::TextDisabled("No components to display");
    return;
  }

  const Component &comp = file.components[static_cast<size_t>(state.cur_comp)];

  ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
  ImVec2 canvas_size = ImGui::GetContentRegionAvail();
  if (canvas_size.x < 1.0f) {
    canvas_size.x = 1.0f;
  }
  if (canvas_size.y < 1.0f) {
    canvas_size.y = 1.0f;
  }

  // Keep canvas center fixed when zoom changes via sliders.
  // Condition: zoom changed but pan did not (sliders only change zoom;
  // Reset-view and component changes always also modify pan).
  if (state.zoom_x != pre_zoom_x && state.pan_x == pre_pan_x && pre_zoom_x > 0.0f) {
    float lx = (canvas_size.x * 0.5f - state.pan_x) / pre_zoom_x;
    state.pan_x = canvas_size.x * 0.5f - lx * state.zoom_x;
  }
  if (state.zoom_y != pre_zoom_y && state.pan_y == pre_pan_y && pre_zoom_y > 0.0f) {
    float ly = (canvas_size.y * 0.5f - state.pan_y) / pre_zoom_y;
    state.pan_y = canvas_size.y * 0.5f - ly * state.zoom_y;
  }

  // Invisible button captures mouse events for this region
  ImGui::InvisibleButton("##canvas_btn", canvas_size, ImGuiButtonFlags_MouseButtonLeft);
  bool is_hovered = ImGui::IsItemHovered();
  bool is_active = ImGui::IsItemActive();

  ImDrawList *dl = ImGui::GetWindowDrawList();

  // Clip rendering to canvas area
  ImVec2 clip_max = {canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y};
  dl->PushClipRect(canvas_pos, clip_max, true);

  // Background
  dl->AddRectFilled(canvas_pos, clip_max, IM_COL32(250, 250, 250, 255));

  // Component plot
  ImVec2 display_size = ImGui::GetIO().DisplaySize;
  ImVec4 clip_rect{canvas_pos.x, canvas_pos.y, canvas_pos.x + canvas_size.x,
                   canvas_pos.y + canvas_size.y};
  render_component(dl, comp, layout, state, canvas_pos, canvas_size, display_size, clip_rect,
#ifdef CADVIS_GUI
                   &g_ctx.gpu_rect
#else
                   nullptr
#endif
  );

  dl->PopClipRect();

  // ------------------------------------------------------------------
  // Input handling
  // ------------------------------------------------------------------
  ImGuiIO &io = ImGui::GetIO();

  // Pan: drag with left mouse button
  static bool s_was_active = false;
  static bool s_dragged = false;

  if (is_active) {
    if (!s_was_active) {
      s_was_active = true;
      s_dragged = false;
    }
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
      s_dragged = true;
      state.pan_x += io.MouseDelta.x;
      state.pan_y += io.MouseDelta.y;
    }
  }

  // Mouse released: detect click vs drag
  if (s_was_active && !is_active) {
    if (!s_dragged) {
      // Click: hit test
      int hit = find_rect_under_cursor(comp, layout, canvas_pos.x, canvas_pos.y, canvas_size.x,
                                       canvas_size.y, state.pan_x, state.pan_y, state.zoom_x,
                                       state.zoom_y, io.MousePos.x, io.MousePos.y);

      state.selected_rect = hit;
      if (hit >= 0) {
        state.selected_sub_count = static_cast<int>(state.rev_adj[static_cast<size_t>(hit)].size());
        state.selected_sub = 0;
        state.is_ancestor = compute_ancestors(comp, state.rev_adj, hit, 0);
      } else {
        state.selected_sub_count = 0;
        state.selected_sub = 0;
        state.is_ancestor.assign(comp.rects.size(), false);
      }
    }
    s_was_active = false;
    s_dragged = false;
  }

  // Zoom: scroll wheel (uniform zoom around cursor)
  if (is_hovered && io.MouseWheel != 0.0f) {
    float factor = (io.MouseWheel > 0.0f) ? 1.1f : (1.0f / 1.1f);
    // Keep the point under the cursor fixed
    float lx = (io.MousePos.x - canvas_pos.x - state.pan_x) / state.zoom_x;
    float ly = (io.MousePos.y - canvas_pos.y - state.pan_y) / state.zoom_y;
    state.zoom_x = std::clamp(state.zoom_x * factor, 0.05f, 40.0f);
    state.zoom_y = std::clamp(state.zoom_y * factor, 0.05f, 40.0f);
    state.pan_x = io.MousePos.x - canvas_pos.x - lx * state.zoom_x;
    state.pan_y = io.MousePos.y - canvas_pos.y - ly * state.zoom_y;
  }

  // PgUp / PgDn: cycle branch paths for selected multiplicity rect
  if (state.selected_rect >= 0 && state.selected_sub_count > 1) {
    bool pgup = ImGui::IsKeyPressed(ImGuiKey_PageUp, false);
    bool pgdn = ImGui::IsKeyPressed(ImGuiKey_PageDown, false);
    if (pgup || pgdn) {
      int delta = pgup ? -1 : 1;
      state.selected_sub =
          (state.selected_sub + delta + state.selected_sub_count) % state.selected_sub_count;
      state.is_ancestor =
          compute_ancestors(comp, state.rev_adj, state.selected_rect, state.selected_sub);
    }
  }
}

// ---------------------------------------------------------------------------
// Per-frame loop body — shared by the desktop while-loop and Emscripten rAF
// ---------------------------------------------------------------------------
static void run_frame() {
  glfwPollEvents();

  // Rebuild layout when component changes
  if (g_ctx.state.cur_comp != g_ctx.layout_comp) {
    if (!g_ctx.file.components.empty()) {
      reset_component(g_ctx.state, g_ctx.file, g_ctx.state.cur_comp);
      if (!Layout::from_component(g_ctx.file.components[static_cast<size_t>(g_ctx.state.cur_comp)],
                                  g_ctx.layout)) {
        g_ctx.layout = Layout{};
      }
    }
    g_ctx.layout_comp = g_ctx.state.cur_comp;
#ifdef CADVIS_GUI
    g_ctx.rects_dirty = true;
#endif
  }

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

#ifdef CADVIS_GUI
  if (g_ctx.rects_dirty && !g_ctx.file.components.empty()) {
    g_ctx.gpu_rect.upload_rects(g_ctx.file.components[static_cast<size_t>(g_ctx.state.cur_comp)]);
    g_ctx.rects_dirty = false;
  }
  if (!g_ctx.file.components.empty()) {
    g_ctx.gpu_rect.upload_state(g_ctx.state);
  }
#endif

  // Full-screen single window
  const ImGuiWindowFlags wf =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
  ImGui::SetNextWindowPos({0.0f, 0.0f});
  ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
  ImGui::Begin("##root", nullptr, wf);

  // --- Sidebar ---
  // Capture zoom/pan before the sidebar so render_canvas can detect slider-only changes.
  float pre_zoom_x = g_ctx.state.zoom_x;
  float pre_zoom_y = g_ctx.state.zoom_y;
  float pre_pan_x = g_ctx.state.pan_x;
  float pre_pan_y = g_ctx.state.pan_y;

  ImGui::BeginChild("##sidebar", {SIDEBAR_W, 0.0f}, ImGuiChildFlags_Borders);
  bool comp_changed = false;
  render_sidebar(g_ctx.state, g_ctx.file, comp_changed);
  if (comp_changed && !g_ctx.file.components.empty()) {
    reset_component(g_ctx.state, g_ctx.file, g_ctx.state.cur_comp);
    if (!Layout::from_component(g_ctx.file.components[static_cast<size_t>(g_ctx.state.cur_comp)],
                                g_ctx.layout)) {
      g_ctx.layout = Layout{};
    }
    g_ctx.layout_comp = g_ctx.state.cur_comp;
#ifdef CADVIS_GUI
    g_ctx.rects_dirty = true;
#endif
    // Zoom/pan were reset by component change — no center-fix needed.
    pre_zoom_x = g_ctx.state.zoom_x;
    pre_zoom_y = g_ctx.state.zoom_y;
    pre_pan_x = g_ctx.state.pan_x;
    pre_pan_y = g_ctx.state.pan_y;
  }
  ImGui::EndChild();

  ImGui::SameLine();

  // --- Canvas ---
  ImGui::BeginChild("##canvas", {0.0f, 0.0f});
  render_canvas(g_ctx.state, g_ctx.file, g_ctx.layout, pre_zoom_x, pre_zoom_y, pre_pan_x,
                pre_pan_y);
  ImGui::EndChild();

  ImGui::End();

  // Render
  ImGui::Render();
  int fb_w;
  int fb_h;
  glfwGetFramebufferSize(g_ctx.window, &fb_w, &fb_h);
  glViewport(0, 0, fb_w, fb_h);
  glClearColor(0.15f, 0.15f, 0.15f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  glfwSwapBuffers(g_ctx.window);
}

// ---------------------------------------------------------------------------
// Emscripten: exported C function to load a .cadvis file written to MEMFS.
// JavaScript usage:
//   FS.writeFile('/upload.cadvis', new Uint8Array(arrayBuffer));
//   Module.ccall('reload_file', 'void', ['string'], ['/upload.cadvis']);
// ---------------------------------------------------------------------------
#ifdef __EMSCRIPTEN__
extern "C" EMSCRIPTEN_KEEPALIVE void reload_file(const char *path) {
  try {
    g_ctx.file = read_cadvis(path);
  } catch (const std::exception &e) {
    std::fprintf(stderr, "Failed to load '%s': %s\n", path, e.what());
    return;
  } catch (...) {
    std::fprintf(stderr, "Failed to load '%s': unknown error\n", path);
    return;
  }
  if (!g_ctx.file.components.empty()) {
    g_ctx.state = {};
    g_ctx.layout_comp = -1;
    reset_component(g_ctx.state, g_ctx.file, 0);
#ifdef CADVIS_GUI
    g_ctx.rects_dirty = true;
#endif
  }
}
#endif

// ---------------------------------------------------------------------------
// Application entry point
// ---------------------------------------------------------------------------
void run_app(const CadvisFile &file) {
  g_ctx.file = file;

  // ------------------------------------------------------------------
  // GLFW init
  // ------------------------------------------------------------------
  glfwSetErrorCallback(
      [](int err, const char *desc) { std::fprintf(stderr, "GLFW error %d: %s\n", err, desc); });
  if (glfwInit() == GLFW_FALSE) {
    std::fprintf(stderr, "Failed to initialise GLFW\n");
    return;
  }

#ifdef __EMSCRIPTEN__
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#else
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
#endif

  g_ctx.window = glfwCreateWindow(1280, 800, "cadmia-explorer", nullptr, nullptr);
  if (g_ctx.window == nullptr) {
    std::fprintf(stderr, "Failed to create GLFW window\n");
    glfwTerminate();
    return;
  }
  glfwMakeContextCurrent(g_ctx.window);
  glfwSwapInterval(1); // vsync

  // ------------------------------------------------------------------
  // ImGui init
  // ------------------------------------------------------------------
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  ImGui::StyleColorsDark();

  ImGui_ImplGlfw_InitForOpenGL(g_ctx.window, true);
  ImGui_ImplOpenGL3_Init(GLSL_VERSION);

#ifdef CADVIS_GUI
  g_ctx.gpu_rect.init();
#endif

  // ------------------------------------------------------------------
  // Initial state
  // ------------------------------------------------------------------
  g_ctx.state = {};
  g_ctx.layout = {};
  g_ctx.layout_comp = -1;

  // ------------------------------------------------------------------
  // Main loop
  // ------------------------------------------------------------------
#ifdef __EMSCRIPTEN__
  emscripten_set_main_loop(run_frame, 0, 1); // 0 = rAF rate, 1 = simulate_infinite_loop
  return;                                    // unreachable; browser handles cleanup
#else
  while (glfwWindowShouldClose(g_ctx.window) == GLFW_FALSE) {
    run_frame();
  }

  // ------------------------------------------------------------------
  // Cleanup
  // ------------------------------------------------------------------
#ifdef CADVIS_GUI
  g_ctx.gpu_rect.destroy();
#endif
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(g_ctx.window);
  glfwTerminate();
#endif
}
