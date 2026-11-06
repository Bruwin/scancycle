#pragma once
// Draws one rung as SVG. Layout and markup live here, not in the page, so the
// drawing can be checked without a browser: contacts in series run left to
// right, parallel branches stack, and every contact is a link that toggles its
// own negation. The page supplies only CSS and the surrounding controls.
#include "plc/netlist.hpp"

#include <string>

namespace sc::ui {

struct LadderSvgOptions {
    std::size_t rung = 0;   // for the toggle URL
    bool clickable = true;  // false for a read-only preview
};

std::string ladder_rung_svg(const plc::Rung& rung, const LadderSvgOptions& options);

}  // namespace sc::ui
