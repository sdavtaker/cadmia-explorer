#include "reader.h"
#include <array>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace {

// Hard resource caps — prevent RAM exhaustion from crafted files.
constexpr size_t MAX_FILE_SIZE = 256ULL * 1024 * 1024; // 256 MB
constexpr size_t MIN_HEADER_SIZE =
    16; // magic(4) + version(2) + flags(2) + n_components(4) + n_strings(4)
constexpr uint32_t MAX_COMPONENTS = 1'000'000;
constexpr uint32_t MAX_STRINGS = 1'000'000;
constexpr uint32_t MAX_STRING_LEN = 65'536; // 64 KB
constexpr uint32_t MAX_RECTS = 1'000'000;
constexpr uint32_t MAX_EDGES = 1'000'000;
// A rect/component can reference the same pool string via a 4-byte index, so
// bounding each string's length and the pool's element count isn't enough —
// a small file can still re-copy a large pool string into millions of rects.
// Cap the total bytes materialized this way across the whole file.
constexpr size_t MAX_TOTAL_LABEL_BYTES = 256ULL * 1024 * 1024; // 256 MB

// Little-endian reads from a raw buffer with bounds checking.
struct Reader {
  const uint8_t *data;
  size_t size;
  size_t pos = 0;

  void need(size_t n) const {
    if (pos + n > size) {
      throw std::runtime_error("Unexpected end of .cadvis file");
    }
  }

  size_t remaining() const { return size - pos; }

  // Reject counts that claim more entries than the file could possibly still
  // contain, before allocating storage for them. Without this, a truncated
  // file can still force allocation up to the relevant cap before need()
  // throws on the first actual read.
  void require_remaining(size_t n, const char *what) const {
    if (remaining() < n) {
      throw std::runtime_error(std::string("Truncated .cadvis file: not enough data for ") + what);
    }
  }

  uint8_t u8() {
    need(1);
    return data[pos++];
  }
  uint16_t u16() {
    need(2);
    uint16_t v;
    std::memcpy(&v, data + pos, 2);
    pos += 2;
    return v;
  }
  uint32_t u32() {
    need(4);
    uint32_t v;
    std::memcpy(&v, data + pos, 4);
    pos += 4;
    return v;
  }
  double f64() {
    need(8);
    double v;
    std::memcpy(&v, data + pos, 8);
    pos += 8;
    return v;
  }

  std::string str(uint32_t len) {
    need(len);
    std::string s(reinterpret_cast<const char *>(data + pos), len);
    pos += len;
    return s;
  }

  void skip(size_t n) {
    need(n);
    pos += n;
  }
};

} // namespace

CadvisFile read_cadvis(const std::string &path) {
  // Determine file size and enforce caps before allocating/reading anything.
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) {
    throw std::runtime_error("Cannot open: " + path);
  }
  auto tell = f.tellg();
  if (tell < 0) {
    throw std::runtime_error("Cannot determine file size: " + path);
  }
  auto file_size = static_cast<size_t>(tell);

  if (file_size < MIN_HEADER_SIZE) {
    throw std::runtime_error("File too small to be a .cadvis file: " + path);
  }
  if (file_size > MAX_FILE_SIZE) {
    throw std::runtime_error("File exceeds maximum allowed size: " + path);
  }

  // Read entire file into memory
  f.seekg(0);
  std::vector<uint8_t> buf(file_size);
  f.read(reinterpret_cast<char *>(buf.data()), static_cast<std::streamsize>(file_size));
  if (!f) {
    throw std::runtime_error("Read error: " + path);
  }

  Reader r{buf.data(), file_size};

  // Header
  std::array<char, 4> magic;
  std::memcpy(magic.data(), buf.data(), 4);
  r.pos = 4;
  if (std::memcmp(magic.data(), "CADV", 4) != 0) {
    throw std::runtime_error("Not a .cadvis file (bad magic)");
  }

  uint16_t version = r.u16();
  if (version != 1) {
    throw std::runtime_error("Unsupported .cadvis version: " + std::to_string(version));
  }
  r.u16(); // flags
  uint32_t n_components = r.u32();
  if (n_components > MAX_COMPONENTS) {
    throw std::runtime_error("Component count exceeds limit: " + std::to_string(n_components));
  }

  // String pool
  uint32_t n_strings = r.u32();
  if (n_strings > MAX_STRINGS) {
    throw std::runtime_error("String pool size exceeds limit: " + std::to_string(n_strings));
  }
  r.require_remaining(static_cast<size_t>(n_strings) * 4, "the declared string pool");
  std::vector<std::string> pool(n_strings);
  for (uint32_t i = 0; i < n_strings; ++i) {
    uint32_t len = r.u32();
    if (len > MAX_STRING_LEN) {
      throw std::runtime_error("String length exceeds limit: " + std::to_string(len));
    }
    pool[i] = r.str(len);
  }

  // Components
  r.require_remaining(static_cast<size_t>(n_components) * 12, "the declared component list");
  CadvisFile result;
  result.components.resize(n_components);

  // A pool string can be referenced by index from any number of components/
  // rects; without a running total, a small file could re-copy one large
  // string millions of times and exhaust RAM despite every per-field cap
  // above being satisfied.
  size_t total_label_bytes = 0;
  auto resolve_label = [&](uint32_t idx) -> const std::string & {
    const std::string &s = pool.at(idx);
    total_label_bytes += s.size();
    if (total_label_bytes > MAX_TOTAL_LABEL_BYTES) {
      throw std::runtime_error("Total label data exceeds limit");
    }
    return s;
  };

  for (uint32_t ci = 0; ci < n_components; ++ci) {
    uint32_t name_idx = r.u32();
    uint32_t n_rects = r.u32();
    if (n_rects > MAX_RECTS) {
      throw std::runtime_error("Rect count exceeds limit: " + std::to_string(n_rects));
    }
    uint32_t n_edges = r.u32();
    if (n_edges > MAX_EDGES) {
      throw std::runtime_error("Edge count exceeds limit: " + std::to_string(n_edges));
    }

    r.require_remaining(static_cast<size_t>(n_rects) * 48 + static_cast<size_t>(n_edges) * 8,
                        "this component's declared rects and edges");

    Component &comp = result.components[ci];
    comp.name = resolve_label(name_idx);
    comp.rects.resize(n_rects);
    comp.edges.resize(n_edges);

    for (uint32_t ri = 0; ri < n_rects; ++ri) {
      Rect &rect = comp.rects[ri];
      rect.time_lo = r.f64();
      rect.time_hi = r.f64();
      rect.out_lo = r.f64();
      rect.out_hi = r.f64();
      uint32_t label_idx = r.u32();
      rect.multiplicity = r.u32();
      uint8_t flags = r.u8();
      r.skip(7); // padding
      rect.time_lo_closed = (flags & 1) != 0;
      rect.time_hi_closed = (flags & 2) != 0;
      rect.out_lo_closed = (flags & 4) != 0;
      rect.out_hi_closed = (flags & 8) != 0;
      rect.out_label = resolve_label(label_idx);
    }

    for (uint32_t ei = 0; ei < n_edges; ++ei) {
      comp.edges[ei].from = r.u32();
      comp.edges[ei].to = r.u32();
    }
  }

  return result;
}
