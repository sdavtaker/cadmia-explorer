#include "layout.h"
#include "logic.h"
#include "reader.h"

#include <atomic>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>

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
                                   (float)Layout::CANVAS_H, 0.0f, 0.0f, 1.0f, 1.0f, 100.0f, 100.0f);
  REQUIRE(hit == -1);
}

TEST_CASE("find_rect_under_cursor: click inside single rect") {
  Component c;
  c.rects = {make_rect(1.0, 2.0, 1.0, 2.0)};
  auto layout = make_layout_for(c);

  // Centre of the rect in layout space
  double cx_l = (layout.time_to_px(1.0) + layout.time_to_px(2.0)) / 2.0;
  double cy_l = (layout.out_to_py(1.0) + layout.out_to_py(2.0)) / 2.0;

  int hit = find_rect_under_cursor(c, layout, 0.0f, 0.0f, (float)Layout::CANVAS_W,
                                   (float)Layout::CANVAS_H, 0.0f, 0.0f, 1.0f, 1.0f, (float)cx_l,
                                   (float)cy_l);
  REQUIRE(hit == 0);
}

TEST_CASE("find_rect_under_cursor: click outside all rects") {
  Component c;
  c.rects = {make_rect(1.0, 2.0, 1.0, 2.0)};
  auto layout = make_layout_for(c);

  // Axis margin area (left of plot)
  int hit = find_rect_under_cursor(c, layout, 0.0f, 0.0f, (float)Layout::CANVAS_W,
                                   (float)Layout::CANVAS_H, 0.0f, 0.0f, 1.0f, 1.0f, 2.0f,
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

  int hit = find_rect_under_cursor(c, layout, 0.0f, 0.0f, (float)Layout::CANVAS_W,
                                   (float)Layout::CANVAS_H, 0.0f, 0.0f, 1.0f, 1.0f, (float)cx_l,
                                   (float)cy_l);
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
                                   (float)Layout::CANVAS_H, pan_x, pan_y, 1.0f, 1.0f,
                                   (float)cx_l + pan_x, (float)cy_l + pan_y);
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
                                   (float)Layout::CANVAS_H, 0.0f, 0.0f, zoom, zoom,
                                   (float)cx_l * zoom, (float)cy_l * zoom);
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
  // X axis always starts at 0; 5% right-side padding: 10 + 0.5 = 10.5
  REQUIRE(layout.t_min == Approx(0.0).epsilon(1e-9));
  REQUIRE(layout.t_max == Approx(10.5).epsilon(1e-9));
  // 10% of span=10 = 1.0 padding each side
  REQUIRE(layout.y_min == Approx(-1.0).epsilon(1e-9));
  REQUIRE(layout.y_max == Approx(11.0).epsilon(1e-9));
}

// ---------------------------------------------------------------------------
// read_cadvis resource cap tests
// ---------------------------------------------------------------------------
// Helpers for building crafted .cadvis buffers in memory.

namespace {

void write_u16(std::vector<uint8_t> &buf, uint16_t v) {
  buf.push_back(static_cast<uint8_t>(v & 0xFF));
  buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void write_u32(std::vector<uint8_t> &buf, uint32_t v) {
  for (int i = 0; i < 4; ++i) {
    buf.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
  }
}

// Minimal valid header: magic + version=1 + flags=0 + n_components + n_strings.
// Caller can append more bytes or truncate as needed.
std::vector<uint8_t> make_header(uint32_t n_components, uint32_t n_strings) {
  std::vector<uint8_t> buf;
  buf.push_back('C');
  buf.push_back('A');
  buf.push_back('D');
  buf.push_back('V');
  write_u16(buf, 1); // version
  write_u16(buf, 0); // flags
  write_u32(buf, n_components);
  write_u32(buf, n_strings);
  return buf;
}

// Write buffer to a temp file and return the path.  Caller must delete.
std::string write_tmp(const std::vector<uint8_t> &buf) {
  static std::atomic<int> counter{0};
  auto path = (std::filesystem::temp_directory_path() /
               ("cadvis_test_" + std::to_string(++counter) + ".cadvis"))
                  .string();
  std::ofstream f(path, std::ios::binary);
  REQUIRE(f.is_open());
  f.write(reinterpret_cast<const char *>(buf.data()), static_cast<std::streamsize>(buf.size()));
  REQUIRE(f.good());
  return path;
}

// Create a sparse file with the given logical size, without physically
// writing that many bytes to disk. std::ifstream::tellg() (what read_cadvis
// uses to enforce MAX_FILE_SIZE) reports the logical size regardless of how
// the file is backed on disk, so this exercises the size-cap branch cheaply.
std::string write_sparse_tmp(size_t size) {
  REQUIRE(size > 0);
  static std::atomic<int> counter{0};
  auto path = (std::filesystem::temp_directory_path() /
               ("cadvis_test_sparse_" + std::to_string(++counter) + ".cadvis"))
                  .string();
  std::ofstream f(path, std::ios::binary);
  REQUIRE(f.is_open());
  f.seekp(static_cast<std::streamoff>(size - 1));
  REQUIRE(f.good());
  f.put('\0');
  REQUIRE(f.good());
  return path;
}

} // namespace

TEST_CASE("read_cadvis: rejects file exceeding size limit") {
  constexpr size_t MAX_FILE_SIZE = 256ULL * 1024 * 1024;
  auto path = write_sparse_tmp(MAX_FILE_SIZE + 1);
  REQUIRE_THROWS_AS(read_cadvis(path), std::runtime_error);
  std::filesystem::remove(path);
}

TEST_CASE("read_cadvis: accepts a minimal valid file") {
  // Happy path: a complete zero-component zero-string file loads without error.
  auto buf = make_header(0, 0);
  auto path = write_tmp(buf);
  REQUIRE_NOTHROW(read_cadvis(path));
  std::filesystem::remove(path);
}

TEST_CASE("read_cadvis: rejects component count above limit") {
  // n_components = MAX_COMPONENTS + 1 (0x000F4241 = 1,000,001)
  constexpr uint32_t over = 1'000'001;
  auto buf = make_header(over, 0); // n_strings = 0 so string pool is empty
  auto path = write_tmp(buf);
  REQUIRE_THROWS_AS(read_cadvis(path), std::runtime_error);
  std::filesystem::remove(path);
}

TEST_CASE("read_cadvis: rejects string pool size above limit") {
  // n_components = 0, n_strings = 1,000,001
  constexpr uint32_t over = 1'000'001;
  auto buf = make_header(0, over);
  auto path = write_tmp(buf);
  REQUIRE_THROWS_AS(read_cadvis(path), std::runtime_error);
  std::filesystem::remove(path);
}

TEST_CASE("read_cadvis: rejects individual string length above limit") {
  // n_components = 0, n_strings = 1, string[0].len = 65537
  auto buf = make_header(0, 1);
  constexpr uint32_t over_len = 65'537;
  write_u32(buf, over_len); // string length field
  auto path = write_tmp(buf);
  REQUIRE_THROWS_AS(read_cadvis(path), std::runtime_error);
  std::filesystem::remove(path);
}

TEST_CASE("read_cadvis: rejects rect count above limit per component") {
  // One component whose name is pool[0]="A", n_rects = 1,000,001, n_edges = 0
  // String pool: one string "A"
  auto buf = make_header(1, 1);
  write_u32(buf, 1); // pool[0].len = 1
  buf.push_back('A');
  write_u32(buf, 0); // name_idx = 0
  constexpr uint32_t over = 1'000'001;
  write_u32(buf, over); // n_rects
  auto path = write_tmp(buf);
  REQUIRE_THROWS_AS(read_cadvis(path), std::runtime_error);
  std::filesystem::remove(path);
}

TEST_CASE("read_cadvis: rejects total label bytes above limit even when every "
          "per-field cap is individually satisfied") {
  // A single pool string at the max per-string length (64KB), referenced by
  // enough rects that the running total-label-bytes budget (256MB) is
  // exceeded, well before rect count, component count, or file size come
  // anywhere near their own individual caps.
  constexpr uint32_t big_len = 65'536;
  constexpr uint32_t n_rects = 4'100; // 4100 * 64KB ~= 256.25MB > 256MB cap
  auto buf = make_header(1, 1);
  write_u32(buf, big_len); // pool[0].len
  buf.insert(buf.end(), big_len, 'x');
  write_u32(buf, 0);       // name_idx = 0 (also charged against the budget)
  write_u32(buf, n_rects); // n_rects
  write_u32(buf, 0);       // n_edges = 0
  for (uint32_t i = 0; i < n_rects; ++i) {
    write_u32(buf, 0); // time_lo (dummy bit pattern, value unchecked)
    write_u32(buf, 0);
    write_u32(buf, 0); // time_hi
    write_u32(buf, 0);
    write_u32(buf, 0); // out_lo
    write_u32(buf, 0);
    write_u32(buf, 0); // out_hi
    write_u32(buf, 0);
    write_u32(buf, 0); // label_idx -> pool[0], the 64KB string
    write_u32(buf, 1); // multiplicity
    buf.push_back(0);  // flags
    for (int p = 0; p < 7; ++p) {
      buf.push_back(0); // padding
    }
  }
  auto path = write_tmp(buf);
  REQUIRE_THROWS_AS(read_cadvis(path), std::runtime_error);
  std::filesystem::remove(path);
}

TEST_CASE("read_cadvis: rejects edge count above limit per component") {
  // One component, n_rects = 0, n_edges = 1,000,001
  auto buf = make_header(1, 1);
  write_u32(buf, 1); // pool[0].len = 1
  buf.push_back('A');
  write_u32(buf, 0); // name_idx = 0
  write_u32(buf, 0); // n_rects = 0
  constexpr uint32_t over = 1'000'001;
  write_u32(buf, over); // n_edges
  auto path = write_tmp(buf);
  REQUIRE_THROWS_AS(read_cadvis(path), std::runtime_error);
  std::filesystem::remove(path);
}
