#pragma once
// A ladder file is one rung per line: the coil on the left, the network that
// drives it on the right. ASCII art does not survive a diff or a merge, so the
// text says what the drawing means instead:
//
//   &    contacts in series    - every one must conduct
//   |    branches in parallel  - any one may conduct
//   !    a normally-closed contact
//   ( )  grouping, because & binds tighter than |
//
//     run_latch = (run_latch | start_btn) & !stop_btn
//
// This parses into the same plc/netlist model the graphical editor will build, so
// the file format and the drawing cannot disagree about what a rung is: one is a
// way of writing the other.
//
// `!` applies to a contact, not to a group. Inverting a whole branch is rare on a
// real drawing and the netlist has no node for it; refusing is better than
// quietly parsing `!(a | b)` as `!a | b`.
#include "plc/declaration.hpp"
#include "plc/diagnostic.hpp"
#include "plc/netlist.hpp"

#include <string_view>
#include <vector>

namespace sc::plc {

struct LadderSource {
    std::vector<TagDecl> vars;
    std::vector<Rung> rungs;
};

struct LadderParseResult {
    bool ok = false;
    LadderSource source;
    Diagnostics diagnostics;
};

// Parses a whole file, comments included. The tags come from the file's VAR
// block; a file without one produces rungs that no compilation can resolve, and
// the netlist compiler says so.
LadderParseResult parse_ladder_source(std::string_view source);

}  // namespace sc::plc
