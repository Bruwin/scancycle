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

}  // namespace sc::app
