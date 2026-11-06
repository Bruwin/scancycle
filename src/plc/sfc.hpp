#pragma once
// Sequential Function Chart: steps, the transitions between them, and the
// actions a step runs. A chart is state, not an expression, so it does not lower
// like a rung: each step becomes a tag whose value is "this step is active", and
// the chart lowers into rungs that compute the next state of every step from the
// current one.
//
// Lowering is where SFC gets simultaneity without a scheduler. Every next-state
// rung runs before any state rung, so each step is computed from the state the
// chart had at the start of the scan. Two transitions that both pass in one scan
// therefore move the chart one step, not two. Actions are written after the
// state rungs, so an action follows the step it belongs to in the same scan the
// step activates, which is what an operator sees on a real chart.
//
// A chart declares only the tags its actions drive. Step tags and their
// next-state slots are declared by the compiler: a step is chart structure, not
// something a program addresses by hand. An action is non-stored - the tag is on
// while the step is active - because stored actions need their own set/reset
// semantics and the model does not pretend to have them yet.
#include "plc/declaration.hpp"
#include "plc/diagnostic.hpp"
#include "plc/netlist.hpp"
#include "plc/program.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace sc::plc {

struct SfcAction {
    std::string tag;  // BOOL tag driven while the step is active
};

struct SfcStep {
    std::string name;
    std::vector<SfcAction> actions;
};

struct SfcTransition {
    std::string from;
    std::string to;
    NetPtr guard;  // null: the transition passes as soon as the source step is active
    int line = 0;  // for diagnostics; not part of what the chart means
};

struct SfcChart {
    std::vector<TagDecl> vars;
    std::vector<SfcStep> steps;
    std::vector<SfcTransition> transitions;
    std::size_t initial = 0;
};

struct SfcParseResult {
    bool ok = false;
    SfcChart chart;
    Diagnostics diagnostics;
};

struct SfcResult {
    bool ok = false;
    CompiledProgram program;
    Diagnostics diagnostics;
};

// The slot a step's next state is computed into: "__sfc_fill_next".
std::string sfc_next_tag(std::string_view step);

// A chart file is one statement per line. Comments are the same (* *) the other
// front ends accept.
//
//     VAR
//         motor : BOOL;
//         start : BOOL;
//     END_VAR
//
//     INITIAL idle
//
//     STEP idle
//     END_STEP
//
//     STEP fill
//         ACTION motor
//     END_STEP
//
//     TRANSITION idle -> fill := start
//     TRANSITION fill -> idle := !start
//
// A transition without `:=` passes unconditionally, which is how a chart shows a
// step that always follows its predecessor.
SfcParseResult parse_sfc_source(std::string_view source);
std::string format_sfc_source(const SfcChart& chart);

// Lowers the chart to rungs plus the tags they need. Exposed so a drawing can
// show the ladder a chart becomes and a test can run it without a VM of its own.
std::vector<Rung> lower_sfc(const SfcChart& chart, std::vector<TagDecl>& vars,
                            Diagnostics& diagnostics);

SfcResult compile_sfc(std::string_view name, const SfcChart& chart);

}  // namespace sc::plc
