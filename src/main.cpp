#ifdef CADVIS_GUI
#include "app.h"
#endif
#include "layout.h"
#include "reader.h"
#include "svg.h"

#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static void usage(const char* argv0) {
    std::cerr <<
        "Usage: " << argv0 << " [options] <file.cadvis>\n"
        "\n"
        "Modes:\n"
#ifdef CADVIS_GUI
        "  No output flags     Open interactive viewer (Dear ImGui)\n"
#else
        "  No output flags     (GUI not available in this build; use --all/--component)\n"
#endif
        "  --all               Render all components to SVG files (headless)\n"
        "  --component NAME    Render only component NAME to SVG (headless)\n"
        "\n"
        "Options:\n"
        "  --output-dir DIR    Write SVG files to DIR (default: current directory)\n"
        "  --help              Show this help\n"
        "\n"
        "Output: one SVG file per selected component, named <component>.svg\n";
}

int main(int argc, char* argv[]) {
    std::string input_path;
    std::string filter_component;
    bool render_all = false;
    bool headless_mode = false;
    std::string output_dir = ".";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
            return 0;
        } else if (arg == "--all") {
            render_all = true;
            filter_component.clear();
            headless_mode = true;
        } else if (arg == "--component") {
            if (i + 1 >= argc) { std::cerr << "--component requires an argument\n"; return 1; }
            filter_component = argv[++i];
            render_all = false;
            headless_mode = true;
        } else if (arg == "--output-dir") {
            if (i + 1 >= argc) { std::cerr << "--output-dir requires an argument\n"; return 1; }
            output_dir = argv[++i];
            headless_mode = true;
        } else if (arg.substr(0, 2) == "--") {
            std::cerr << "Unknown option: " << arg << "\n";
            usage(argv[0]);
            return 1;
        } else {
            if (!input_path.empty()) {
                std::cerr << "Multiple input files are not supported\n";
                return 1;
            }
            input_path = arg;
        }
    }

    // --output-dir alone (without --all or --component) implies --all
    if (headless_mode && !render_all && filter_component.empty())
        render_all = true;

    if (input_path.empty()) {
        std::cerr << "Error: no input file specified\n";
        usage(argv[0]);
        return 1;
    }

    // Load the .cadvis file
    CadvisFile data;
    try {
        data = read_cadvis(input_path);
    } catch (const std::exception& e) {
        std::cerr << "Error reading " << input_path << ": " << e.what() << "\n";
        return 1;
    }

    // -----------------------------------------------------------------------
    // GUI mode: open interactive ImGui viewer
    // -----------------------------------------------------------------------
    if (!headless_mode) {
#ifdef CADVIS_GUI
        run_app(data);
        return 0;
#else
        std::cerr << "This build was compiled without GUI support.\n"
                     "Use --all or --component to render SVG files.\n";
        return 1;
#endif
    }

    // -----------------------------------------------------------------------
    // Headless mode: render SVG files (Phase 1 pipeline, unchanged)
    // -----------------------------------------------------------------------
    try {
        fs::create_directories(output_dir);
    } catch (const std::exception& e) {
        std::cerr << "Cannot create output directory " << output_dir << ": " << e.what() << "\n";
        return 1;
    }

    std::cout << "Loaded " << data.components.size() << " component(s) from " << input_path << "\n";

    int rendered = 0;
    for (const auto& comp : data.components) {
        if (!render_all && comp.name != filter_component)
            continue;

        Layout layout;
        if (!Layout::from_component(comp, layout)) {
            std::cout << "  Skipping " << comp.name << " (no rects)\n";
            continue;
        }

        std::string svg = render_svg(comp, layout);

        // Sanitise component name for use as a filename
        std::string safe_name;
        for (char c : comp.name) {
            safe_name += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')
                         ? c : '_';
        }
        fs::path out_path = fs::path(output_dir) / (safe_name + ".svg");

        std::ofstream f(out_path);
        if (!f) {
            std::cerr << "Cannot write " << out_path << "\n";
            return 1;
        }
        f << svg;
        std::cout << "  Wrote " << out_path
                  << " (" << comp.rects.size() << " rects, " << comp.edges.size() << " edges)\n";
        ++rendered;
    }

    if (rendered == 0 && !render_all) {
        std::cerr << "Component '" << filter_component << "' not found in " << input_path << "\n";
        return 1;
    }

    std::cout << "Done. " << rendered << " SVG(s) written.\n";
    return 0;
}
