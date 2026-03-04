#pragma once
#include "reader.h"
#include "layout.h"
#include <string>

/// Render a component to an SVG string.
std::string render_svg(const Component& comp, const Layout& layout);
