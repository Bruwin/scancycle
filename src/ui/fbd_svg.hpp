#pragma once
// Draws one rung as a Function Block Diagram: the same network the ladder
// renderer draws, with junctions as AND (&) and OR (>=1) blocks instead of
// contacts on a rail. Both drawings take the same rung and the same edit routes,
// which is what makes the view a choice of drawing rather than a second editor:
// a contact is still /toggle, and an AND that should have been an OR is /swap.
//
// A counter's reset network is labelled, not drawn. It is a second network with
// no path in the editor yet, and drawing its contacts would make them look
// clickable when the click would land on the main condition.
#include "plc/netlist.hpp"

#include <cstddef>
#include <string>

namespace sc::ui {

struct FbdSvgOptions {
    std::size_t rung = 0;   // for the edit URLs
    bool clickable = true;  // false for a read-only preview
};

std::string fbd_rung_svg(const plc::Rung& rung, const FbdSvgOptions& options);

}  // namespace sc::ui
