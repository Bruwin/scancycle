#pragma once
// Shared chrome for the graphical editor surfaces. Ladder/FBD and SFC are
// separate servers, but they are one product: the brand bar, tab treatment,
// type scale and spacing live here so the three pages cannot drift apart.
#include <string>
#include <string_view>

namespace sc::app {

// `active` is "ladder", "fbd" or "sfc". Ladder and FBD share the view tabs;
// the SFC server has no cross-route, so it gets a single active tab.
std::string editor_page_head(std::string_view title, std::string_view active);

std::string editor_page_foot();

// A diagram inside a pannable, zoomable viewport. `label` is static UI text
// inserted into an aria-label as-is; `drawing` is trusted inline SVG markup.
std::string diagram_canvas(std::string_view label, std::string drawing);

}  // namespace sc::app
