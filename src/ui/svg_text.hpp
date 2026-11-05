#pragma once
// Helpers both network drawings need: escaping text for XML and printing a
// netlist path the way a URL carries it. Kept out of either drawing so the
// ladder and the function block diagram cannot disagree about a link format.
#include "plc/netlist_edit.hpp"

#include <string>
#include <string_view>

namespace sc::ui {

// Escapes the four characters that would end an attribute or open a tag. There
// is no validation here: the caller owns the text, which is a tag name or a
// diagnostic message the editor already accepted.
std::string svg_escape(std::string_view text);

// "0.1.2" for the path through a network, empty for the root. This is the same
// form parse_path accepts in the page router.
std::string svg_path_text(const plc::NetPath& path);

// A preset the way a drawing labels it: 1000 ms prints as "1s", 60000 as "1m".
// Both editor surfaces show the same preset, so neither keeps its own copy.
std::string duration_label(i64 milliseconds);

}  // namespace sc::ui
