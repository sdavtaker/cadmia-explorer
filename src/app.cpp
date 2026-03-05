#include "app.h"
#include "layout.h"
#include "logic.h"
#include "renderer.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>

#include <cstdio>
#include <string>
#include <vector>

static constexpr float SIDEBAR_W = 230.0f;
static constexpr const char* GLSL_VERSION = "#version 130";

// ---------------------------------------------------------------------------
// Helper: (re)initialise AppState for a given component index
// ---------------------------------------------------------------------------
static void reset_component(AppState& state, const CadvisFile& file, int idx) {
    state.cur_comp        = idx;
    state.pan_x           = 0.0f;
    state.pan_y           = 0.0f;
    state.zoom            = 1.0f;
    state.selected_rect   = -1;
    state.selected_sub    = 0;
    state.selected_sub_count = 0;

    const Component& comp = file.components[static_cast<size_t>(idx)];
    state.rev_adj    = build_reverse_adj(comp);
    state.is_ancestor.assign(comp.rects.size(), false);
}

// ---------------------------------------------------------------------------
// Sidebar rendering
// ---------------------------------------------------------------------------
static void render_sidebar(AppState& state, const CadvisFile& file,
                            bool& comp_changed)
{
    comp_changed = false;

    // Component combo
    ImGui::TextUnformatted("Component:");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##comp",
            file.components.empty() ? "(none)"
            : file.components[static_cast<size_t>(state.cur_comp)].name.c_str()))
    {
        for (int i = 0; i < static_cast<int>(file.components.size()); ++i) {
            bool selected = (i == state.cur_comp);
            if (ImGui::Selectable(
                    file.components[static_cast<size_t>(i)].name.c_str(), selected))
            {
                if (i != state.cur_comp) {
                    state.cur_comp = i;
                    comp_changed = true;
                }
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::Separator();

    if (file.components.empty()) {
        ImGui::TextDisabled("No components");
        return;
    }

    const Component& comp = file.components[static_cast<size_t>(state.cur_comp)];
    ImGui::Text("%d rect(s), %d edge(s)",
                static_cast<int>(comp.rects.size()),
                static_cast<int>(comp.edges.size()));

    if (state.selected_rect < 0) {
        ImGui::Separator();
        ImGui::TextDisabled("Click a rect to select");
        return;
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Selected:");

    const Rect& r = comp.rects[static_cast<size_t>(state.selected_rect)];

    // Time interval
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s%.4g, %.4g%s",
                  r.time_lo_closed ? "[" : "(",
                  r.time_lo, r.time_hi,
                  r.time_hi_closed ? "]" : ")");
    ImGui::Text("  Time: %s", buf);

    // Output interval or label
    if (r.out_lo == r.out_hi && r.out_lo_closed && r.out_hi_closed) {
        // Punctual (mapped) output
        ImGui::Text("  Out:  %s", r.out_label.c_str());
    } else {
        std::snprintf(buf, sizeof(buf), "%s%.4g, %.4g%s",
                      r.out_lo_closed ? "[" : "(",
                      r.out_lo, r.out_hi,
                      r.out_hi_closed ? "]" : ")");
        ImGui::Text("  Out:  %s", buf);
    }

    if (r.multiplicity > 1) {
        ImGui::Text("  \xc3\x97%u branches merged", r.multiplicity);
    }

    if (state.selected_sub_count > 1) {
        ImGui::Text("  Path: %d / %d",
                    state.selected_sub + 1, state.selected_sub_count);
        ImGui::TextDisabled("  PgUp/PgDn: cycle");
    }
}

// ---------------------------------------------------------------------------
// Canvas rendering + input
// ---------------------------------------------------------------------------
static void render_canvas(AppState& state, const CadvisFile& file,
                           Layout& layout)
{
    if (file.components.empty()) {
        ImGui::TextDisabled("No components to display");
        return;
    }

    const Component& comp = file.components[static_cast<size_t>(state.cur_comp)];

    ImVec2 canvas_pos  = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    if (canvas_size.x < 1.0f) canvas_size.x = 1.0f;
    if (canvas_size.y < 1.0f) canvas_size.y = 1.0f;

    // Invisible button captures mouse events for this region
    ImGui::InvisibleButton("##canvas_btn", canvas_size,
                            ImGuiButtonFlags_MouseButtonLeft);
    bool is_hovered = ImGui::IsItemHovered();
    bool is_active  = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Clip rendering to canvas area
    ImVec2 clip_max = {canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y};
    dl->PushClipRect(canvas_pos, clip_max, true);

    // Background
    dl->AddRectFilled(canvas_pos, clip_max, IM_COL32(250, 250, 250, 255));

    // Component plot
    render_component(dl, comp, layout, state, canvas_pos, canvas_size);

    dl->PopClipRect();

    // ------------------------------------------------------------------
    // Input handling
    // ------------------------------------------------------------------
    ImGuiIO& io = ImGui::GetIO();

    // Pan: drag with left mouse button
    static bool s_was_active   = false;
    static bool s_dragged      = false;
    static bool s_drag_started = false;

    if (is_active) {
        if (!s_was_active) {
            s_was_active   = true;
            s_dragged      = false;
            s_drag_started = false;
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
            s_dragged      = true;
            s_drag_started = true;
            state.pan_x   += io.MouseDelta.x;
            state.pan_y   += io.MouseDelta.y;
        }
    }

    // Mouse released: detect click vs drag
    if (s_was_active && !is_active) {
        if (!s_dragged) {
            // Click: hit test
            int hit = find_rect_under_cursor(
                comp, layout,
                canvas_pos.x, canvas_pos.y, canvas_size.x, canvas_size.y,
                state.pan_x, state.pan_y, state.zoom,
                io.MousePos.x, io.MousePos.y);

            state.selected_rect = hit;
            if (hit >= 0) {
                state.selected_sub_count =
                    static_cast<int>(state.rev_adj[static_cast<size_t>(hit)].size());
                state.selected_sub = 0;
                state.is_ancestor  =
                    compute_ancestors(comp, state.rev_adj, hit, 0);
            } else {
                state.selected_sub_count = 0;
                state.selected_sub       = 0;
                state.is_ancestor.assign(comp.rects.size(), false);
            }
        }
        s_was_active   = false;
        s_dragged      = false;
        s_drag_started = false;
    }

    // Zoom: scroll wheel (zoom around cursor)
    if (is_hovered && io.MouseWheel != 0.0f) {
        float factor = (io.MouseWheel > 0.0f) ? 1.1f : (1.0f / 1.1f);
        // Keep the point under the cursor fixed
        float lx = (io.MousePos.x - canvas_pos.x - state.pan_x) / state.zoom;
        float ly = (io.MousePos.y - canvas_pos.y - state.pan_y) / state.zoom;
        state.zoom = std::clamp(state.zoom * factor, 0.05f, 40.0f);
        state.pan_x = io.MousePos.x - canvas_pos.x - lx * state.zoom;
        state.pan_y = io.MousePos.y - canvas_pos.y - ly * state.zoom;
    }

    // PgUp / PgDn: cycle branch paths for selected multiplicity rect
    if (state.selected_rect >= 0 && state.selected_sub_count > 1) {
        bool pgup = ImGui::IsKeyPressed(ImGuiKey_PageUp,   false);
        bool pgdn = ImGui::IsKeyPressed(ImGuiKey_PageDown, false);
        if (pgup || pgdn) {
            int delta = pgup ? -1 : 1;
            state.selected_sub =
                (state.selected_sub + delta + state.selected_sub_count)
                % state.selected_sub_count;
            state.is_ancestor = compute_ancestors(
                comp, state.rev_adj, state.selected_rect, state.selected_sub);
        }
    }
}

// ---------------------------------------------------------------------------
// Application entry point
// ---------------------------------------------------------------------------
void run_app(const CadvisFile& file) {
    // ------------------------------------------------------------------
    // GLFW init
    // ------------------------------------------------------------------
    glfwSetErrorCallback([](int err, const char* desc) {
        std::fprintf(stderr, "GLFW error %d: %s\n", err, desc);
    });
    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to initialise GLFW\n");
        return;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(1280, 800, "cadmia-explorer", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // vsync

    // ------------------------------------------------------------------
    // ImGui init
    // ------------------------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(GLSL_VERSION);

    // ------------------------------------------------------------------
    // Per-component layout cache
    // ------------------------------------------------------------------
    AppState state{};
    Layout   layout{};
    int      layout_comp = -1; // which component the layout was built for

    // ------------------------------------------------------------------
    // Main loop
    // ------------------------------------------------------------------
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Rebuild state / layout when component changes
        if (state.cur_comp != layout_comp) {
            if (!file.components.empty()) {
                reset_component(state, file, state.cur_comp);
                Layout::from_component(
                    file.components[static_cast<size_t>(state.cur_comp)], layout);
            }
            layout_comp = state.cur_comp;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Full-screen single window
        const ImGuiWindowFlags wf =
            ImGuiWindowFlags_NoDecoration  |
            ImGuiWindowFlags_NoMove        |
            ImGuiWindowFlags_NoSavedSettings;
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("##root", nullptr, wf);

        // --- Sidebar ---
        ImGui::BeginChild("##sidebar", {SIDEBAR_W, 0.0f}, ImGuiChildFlags_Borders);
        bool comp_changed = false;
        render_sidebar(state, file, comp_changed);
        if (comp_changed) layout_comp = -1; // force rebuild next frame
        ImGui::EndChild();

        ImGui::SameLine();

        // --- Canvas ---
        ImGui::BeginChild("##canvas", {0.0f, 0.0f});
        render_canvas(state, file, layout);
        ImGui::EndChild();

        ImGui::End();

        // Render
        ImGui::Render();
        int fb_w, fb_h;
        glfwGetFramebufferSize(window, &fb_w, &fb_h);
        glViewport(0, 0, fb_w, fb_h);
        glClearColor(0.15f, 0.15f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    // ------------------------------------------------------------------
    // Cleanup
    // ------------------------------------------------------------------
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
}
