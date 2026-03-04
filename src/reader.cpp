#include "reader.h"
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace {

// Little-endian reads from a raw buffer with bounds checking.
struct Reader {
    const uint8_t* data;
    size_t         size;
    size_t         pos = 0;

    void need(size_t n) const {
        if (pos + n > size)
            throw std::runtime_error("Unexpected end of .cadvis file");
    }

    uint8_t  u8()  { need(1); return data[pos++]; }
    uint16_t u16() { need(2); uint16_t v; std::memcpy(&v, data+pos, 2); pos += 2; return v; }
    uint32_t u32() { need(4); uint32_t v; std::memcpy(&v, data+pos, 4); pos += 4; return v; }
    double   f64() { need(8); double   v; std::memcpy(&v, data+pos, 8); pos += 8; return v; }

    std::string str(uint32_t len) {
        need(len);
        std::string s(reinterpret_cast<const char*>(data + pos), len);
        pos += len;
        return s;
    }

    void skip(size_t n) { need(n); pos += n; }
};

} // namespace

CadvisFile read_cadvis(const std::string& path) {
    // Read entire file into memory
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
        throw std::runtime_error("Cannot open: " + path);
    auto file_size = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<uint8_t> buf(file_size);
    f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(file_size));
    if (!f)
        throw std::runtime_error("Read error: " + path);

    Reader r{buf.data(), file_size};

    // Header
    char magic[4];
    std::memcpy(magic, buf.data(), 4);
    r.pos = 4;
    if (std::memcmp(magic, "CADV", 4) != 0)
        throw std::runtime_error("Not a .cadvis file (bad magic)");

    uint16_t version = r.u16();
    if (version != 1)
        throw std::runtime_error("Unsupported .cadvis version: " + std::to_string(version));
    r.u16(); // flags
    uint32_t n_components = r.u32();

    // String pool
    uint32_t n_strings = r.u32();
    std::vector<std::string> pool(n_strings);
    for (uint32_t i = 0; i < n_strings; ++i) {
        uint32_t len = r.u32();
        pool[i] = r.str(len);
    }

    // Components
    CadvisFile result;
    result.components.resize(n_components);

    for (uint32_t ci = 0; ci < n_components; ++ci) {
        uint32_t name_idx = r.u32();
        uint32_t n_rects  = r.u32();
        uint32_t n_edges  = r.u32();

        Component& comp = result.components[ci];
        comp.name = pool.at(name_idx);
        comp.rects.resize(n_rects);
        comp.edges.resize(n_edges);

        for (uint32_t ri = 0; ri < n_rects; ++ri) {
            Rect& rect = comp.rects[ri];
            rect.time_lo = r.f64();
            rect.time_hi = r.f64();
            rect.out_lo  = r.f64();
            rect.out_hi  = r.f64();
            uint32_t label_idx   = r.u32();
            rect.multiplicity    = r.u32();
            uint8_t  flags       = r.u8();
            r.skip(7); // padding
            rect.time_lo_closed = (flags & 1) != 0;
            rect.time_hi_closed = (flags & 2) != 0;
            rect.out_lo_closed  = (flags & 4) != 0;
            rect.out_hi_closed  = (flags & 8) != 0;
            rect.out_label = pool.at(label_idx);
        }

        for (uint32_t ei = 0; ei < n_edges; ++ei) {
            comp.edges[ei].from = r.u32();
            comp.edges[ei].to   = r.u32();
        }
    }

    return result;
}
