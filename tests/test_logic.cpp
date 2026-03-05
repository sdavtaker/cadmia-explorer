#include "layout.h"
#include "logic.h"
#include "reader.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

// ---------------------------------------------------------------------------
// Helpers for building test Component objects
// ---------------------------------------------------------------------------
static Rect make_rect(double t0, double t1, double y0, double y1) {
  Rect r{};
  r.time_lo = t0;
  r.time_hi = t1;
  r.out_lo = y0;
  r.out_hi = y1;
  r.time_lo_closed = r.time_hi_closed = r.out_lo_closed = r.out_hi_closed = true;
  r.out_label = "x";
  r.multiplicity = 1;
  return r;
}

// Linear chain:  0 → 1 → 2
static Component make_chain() {
  Component c;
  c.rects = {make_rect(0, 1, 0, 1), make_rect(1, 2, 0, 1), make_rect(2, 3, 0, 1)};
  c.edges = {{0, 1}, {1, 2}};
  return c;
}

// Fork: two roots (0 and 1) both point to a shared rect (2)
//   0 ─╮
//       ├─ 2
//   1 ─╯
static Component make_fork() {
  Component c;
  c.rects = {make_rect(0, 1, 0, 0.5), make_rect(0, 1, 0.5, 1), make_rect(1, 2, 0, 1)};
  c.edges = {{0, 2}, {1, 2}};
  return c;
}

// ---------------------------------------------------------------------------
// build_reverse_adj
// ---------------------------------------------------------------------------
TEST_CASE("build_reverse_adj: empty component") {
  Component c;
  auto rev = build_reverse_adj(c);
  REQUIRE(rev.empty());
}

TEST_CASE("build_reverse_adj: chain") {
  auto c = make_chain();
  auto rev = build_reverse_adj(c);
  REQUIRE(rev.size() == 3);
  REQUIRE(rev[0].empty()); // rect 0 has no incoming
  REQUIRE(rev[1] == std::vector<uint32_t>{0});
  REQUIRE(rev[2] == std::vector<uint32_t>{1});
}

TEST_CASE("build_reverse_adj: fork") {
  auto c = make_fork();
  auto rev = build_reverse_adj(c);
  REQUIRE(rev.size() == 3);
  REQUIRE(rev[0].empty());
  REQUIRE(rev[1].empty());
  // rect 2 has two incoming edges; order matches edge insertion order
  REQUIRE(rev[2].size() == 2);
  REQUIRE(rev[2][0] == 0);
  REQUIRE(rev[2][1] == 1);
}

// ---------------------------------------------------------------------------
// compute_ancestors
// ---------------------------------------------------------------------------
TEST_CASE("compute_ancestors: invalid selected_rect") {
  auto c = make_chain();
  auto rev = build_reverse_adj(c);
  auto result = compute_ancestors(c, rev, -1, 0);
  REQUIRE(result == std::vector<bool>(3, false));

  result = compute_ancestors(c, rev, 99, 0);
  REQUIRE(result == std::vector<bool>(3, false));
}

TEST_CASE("compute_ancestors: root rect (no ancestors)") {
  auto c = make_chain();
  auto rev = build_reverse_adj(c);
  auto result = compute_ancestors(c, rev, 0, 0);
  REQUIRE(result[0] == true);
  REQUIRE(result[1] == false);
  REQUIRE(result[2] == false);
}

TEST_CASE("compute_ancestors: leaf of chain gets full lineage") {
  auto c = make_chain();
  auto rev = build_reverse_adj(c);
  auto result = compute_ancestors(c, rev, 2, 0);
  REQUIRE(result[0] == true);
  REQUIRE(result[1] == true);
  REQUIRE(result[2] == true);
}

TEST_CASE("compute_ancestors: middle of chain") {
  auto c = make_chain();
  auto rev = build_reverse_adj(c);
  auto result = compute_ancestors(c, rev, 1, 0);
  REQUIRE(result[0] == true);
  REQUIRE(result[1] == true);
  REQUIRE(result[2] == false);
}

TEST_CASE("compute_ancestors: fork sub=0 selects first path") {
  auto c = make_fork();
  auto rev = build_reverse_adj(c);
  // rect 2 has incoming edges from rect 0 (sub=0) and rect 1 (sub=1)
  auto result = compute_ancestors(c, rev, 2, 0);
  REQUIRE(result[0] == true);  // ancestor via sub=0
  REQUIRE(result[1] == false); // not on this path
  REQUIRE(result[2] == true);  // selected
}

TEST_CASE("compute_ancestors: fork sub=1 selects second path") {
  auto c = make_fork();
  auto rev = build_reverse_adj(c);
  auto result = compute_ancestors(c, rev, 2, 1);
  REQUIRE(result[0] == false);
  REQUIRE(result[1] == true); // ancestor via sub=1
  REQUIRE(result[2] == true); // selected
}

TEST_CASE("compute_ancestors: sub out of range clamps to 0") {
  auto c = make_fork();
  auto rev = build_reverse_adj(c);
  auto result_clamped = compute_ancestors(c, rev, 2, 99);
  auto result_zero = compute_ancestors(c, rev, 2, 0);
  REQUIRE(result_clamped == result_zero);
}

TEST_CASE("compute_ancestors: self-loop does not hang") {
  Component c;
  c.rects = {make_rect(0, 1, 0, 1)};
  c.edges = {{0, 0}}; // self-loop
  auto rev = build_reverse_adj(c);
  auto result = compute_ancestors(c, rev, 0, 0);
  REQUIRE(result[0] == true); // selected rect always true
}

// ---------------------------------------------------------------------------
// find_rect_under_cursor
// ---------------------------------------------------------------------------
// In these tests we use zoom=1, pan=(0,0), canvas at (0,0) with size = CANVAS dimensions.
// Under these conditions: screen coords == layout coords.
static Layout make_layout_for(const Component &comp) {
  Layout layout;
  Layout::from_component(comp, layout);
  return layout;
}

TEST_CASE("find_rect_under_cursor: no rects") {
  Component c;
  Layout layout{};
  int hit = find_rect_under_cursor(c, layout, 0.0f, 0.0f, (float)Layout::CANVAS_W,
                                   (float)Layout::CANVAS_H, 0.0f, 0.0f, 1.0f, 100.0f, 100.0f);
  REQUIRE(hit == -1);
}

TEST_CASE("find_rect_under_cursor: click inside single rect") {
  Component c;
  c.rects = {make_rect(1.0, 2.0, 1.0, 2.0)};
  auto layout = make_layout_for(c);

  // Centre of the rect in layout space
  double cx_l = (layout.time_to_px(1.0) + layout.time_to_px(2.0)) / 2.0;
  double cy_l = (layout.out_to_py(1.0) + layout.out_to_py(2.0)) / 2.0;

  int hit =
      find_rect_under_cursor(c, layout, 0.0f, 0.0f, (float)Layout::CANVAS_W,
                             (float)Layout::CANVAS_H, 0.0f, 0.0f, 1.0f, (float)cx_l, (float)cy_l);
  REQUIRE(hit == 0);
}

TEST_CASE("find_rect_under_cursor: click outside all rects") {
  Component c;
  c.rects = {make_rect(1.0, 2.0, 1.0, 2.0)};
  auto layout = make_layout_for(c);

  // Axis margin area (left of plot)
  int hit = find_rect_under_cursor(c, layout, 0.0f, 0.0f, (float)Layout::CANVAS_W,
                                   (float)Layout::CANVAS_H, 0.0f, 0.0f, 1.0f, 2.0f,
                                   450.0f); // deep in left margin
  REQUIRE(hit == -1);
}

TEST_CASE("find_rect_under_cursor: last overlapping rect wins") {
  Component c;
  // Two rects at the same position (multiplicity scenario)
  c.rects = {make_rect(1.0, 2.0, 1.0, 2.0), make_rect(1.0, 2.0, 1.0, 2.0)};
  auto layout = make_layout_for(c);

  double cx_l = (layout.time_to_px(1.0) + layout.time_to_px(2.0)) / 2.0;
  double cy_l = (layout.out_to_py(1.0) + layout.out_to_py(2.0)) / 2.0;

  int hit =
      find_rect_under_cursor(c, layout, 0.0f, 0.0f, (float)Layout::CANVAS_W,
                             (float)Layout::CANVAS_H, 0.0f, 0.0f, 1.0f, (float)cx_l, (float)cy_l);
  REQUIRE(hit == 1); // higher index wins
}

TEST_CASE("find_rect_under_cursor: pan offsets cursor correctly") {
  Component c;
  c.rects = {make_rect(1.0, 2.0, 1.0, 2.0)};
  auto layout = make_layout_for(c);

  double cx_l = (layout.time_to_px(1.0) + layout.time_to_px(2.0)) / 2.0;
  double cy_l = (layout.out_to_py(1.0) + layout.out_to_py(2.0)) / 2.0;

  float pan_x = 50.0f;
  float pan_y = 30.0f;
  // With pan, screen position of rect centre is cx_l + pan_x, cy_l + pan_y
  int hit = find_rect_under_cursor(c, layout, 0.0f, 0.0f, (float)Layout::CANVAS_W,
                                   (float)Layout::CANVAS_H, pan_x, pan_y, 1.0f, (float)cx_l + pan_x,
                                   (float)cy_l + pan_y);
  REQUIRE(hit == 0);
}

TEST_CASE("find_rect_under_cursor: zoom scales position") {
  Component c;
  c.rects = {make_rect(1.0, 2.0, 1.0, 2.0)};
  auto layout = make_layout_for(c);

  double cx_l = (layout.time_to_px(1.0) + layout.time_to_px(2.0)) / 2.0;
  double cy_l = (layout.out_to_py(1.0) + layout.out_to_py(2.0)) / 2.0;

  float zoom = 2.0f;
  // Screen position of rect centre at zoom=2: cx_l * zoom, cy_l * zoom
  int hit = find_rect_under_cursor(c, layout, 0.0f, 0.0f, (float)Layout::CANVAS_W,
                                   (float)Layout::CANVAS_H, 0.0f, 0.0f, zoom, (float)cx_l * zoom,
                                   (float)cy_l * zoom);
  REQUIRE(hit == 0);
}

// ---------------------------------------------------------------------------
// Layout coordinate mappings
// ---------------------------------------------------------------------------
TEST_CASE("Layout::from_component returns false for empty component") {
  Component c;
  Layout layout{};
  REQUIRE(!Layout::from_component(c, layout));
}

TEST_CASE("Layout::time_to_px maps t_min to PLOT_X") {
  Component c;
  c.rects = {make_rect(1.0, 3.0, 0.0, 1.0)};
  Layout layout{};
  REQUIRE(Layout::from_component(c, layout));
  REQUIRE(layout.time_to_px(layout.t_min) == Approx(Layout::PLOT_X).epsilon(1e-9));
}

TEST_CASE("Layout::time_to_px maps t_max to PLOT_X + PLOT_W") {
  Component c;
  c.rects = {make_rect(1.0, 3.0, 0.0, 1.0)};
  Layout layout{};
  REQUIRE(Layout::from_component(c, layout));
  REQUIRE(layout.time_to_px(layout.t_max) == Approx(Layout::PLOT_X + Layout::PLOT_W).epsilon(1e-9));
}

TEST_CASE("Layout::out_to_py is inverted (higher output = lower py)") {
  Component c;
  c.rects = {make_rect(0.0, 1.0, 0.0, 10.0)};
  Layout layout{};
  REQUIRE(Layout::from_component(c, layout));
  // Higher output value → smaller py (closer to top)
  REQUIRE(layout.out_to_py(layout.y_max) < layout.out_to_py(layout.y_min));
}

TEST_CASE("Layout::out_to_py maps y_max to PLOT_Y") {
  Component c;
  c.rects = {make_rect(0.0, 1.0, 0.0, 1.0)};
  Layout layout{};
  REQUIRE(Layout::from_component(c, layout));
  REQUIRE(layout.out_to_py(layout.y_max) == Approx(Layout::PLOT_Y).epsilon(1e-9));
}

TEST_CASE("Layout::out_to_py maps y_min to PLOT_Y + PLOT_H") {
  Component c;
  c.rects = {make_rect(0.0, 1.0, 0.0, 1.0)};
  Layout layout{};
  REQUIRE(Layout::from_component(c, layout));
  REQUIRE(layout.out_to_py(layout.y_min) == Approx(Layout::PLOT_Y + Layout::PLOT_H).epsilon(1e-9));
}

TEST_CASE("Layout adds 5% time padding and 10% output padding") {
  Component c;
  c.rects = {make_rect(0.0, 10.0, 0.0, 10.0)};
  Layout layout{};
  REQUIRE(Layout::from_component(c, layout));
  // 5% of span=10 = 0.5 padding each side
  REQUIRE(layout.t_min == Approx(-0.5).epsilon(1e-9));
  REQUIRE(layout.t_max == Approx(10.5).epsilon(1e-9));
  // 10% of span=10 = 1.0 padding each side
  REQUIRE(layout.y_min == Approx(-1.0).epsilon(1e-9));
  REQUIRE(layout.y_max == Approx(11.0).epsilon(1e-9));
}
