#pragma once
// Ladder Diagram and Function Block Diagram share this module. A ladder rung and
// an FBD network are the same object drawn differently: junctions of contacts
// feeding sinks. One model, two renderings, one lowering pass into the same
// bytecode a Structured Text program compiles to.
//
// The model is boolean on purpose. A rung that compares integers is built from
// comparison blocks rather than contacts; those arrive with the FBD block
// library and lower through this same emitter, so the shape here does not change
// to accept them.
//
// Invariants:
//   * Every contact and every coil names a declared tag. Names are checked before
//     anything is emitted, so a typo is a diagnostic instead of a contact that
//     reads FALSE forever and a student hunting a bug that is not there.
//   * A null condition means the rung is wired straight to the rail: always true.
//   * Conditions must be pure. A multi-coil rung re-emits its condition once per
//     coil rather than stuffing one value into a temp slot. A timer is the one
//     stateful sink: it gets a rung of its own, holds the condition in its own IN
//     field and lands Q in a BOOL tag, so the network itself stays boolean.
#include "core/types.hpp"
#include "plc/declaration.hpp"
#include "plc/diagnostic.hpp"
#include "plc/program.hpp"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sc::plc {

enum class NetKind : u8 {
    Contact,   // reads one tag, optionally inverted
    Series,    // every operand must conduct
    Parallel,  // any operand must conduct
};

struct NetNode;
using NetPtr = std::unique_ptr<NetNode>;

// A node owns its operands, so a network is built bottom-up and moved into a
// Rung. Nothing aliases: the emitter can walk a network and never meet a cycle.
struct NetNode {
    NetKind kind = NetKind::Contact;
    std::string var;               // Contact only
    bool negated = false;          // normally-closed contact
    std::vector<NetPtr> operands;  // Series and Parallel only
};

struct Coil {
    std::string var;
    bool negated = false;  // a negated coil writes the inverse
};

// A timer sink turns the rung's condition into a delay: `var` reads TRUE once the
// condition has held for `preset_ms`. The TON instance behind it is internal, so a
// rung still reads as "tag = condition" and the netlist never carries time values.
struct TimerSink {
    std::string var;  // BOOL tag that receives Q
    i64 preset_ms = 0;
};

struct Rung {
    NetPtr condition;                // null: always energised
    std::vector<Coil> coils;         // every coil is written from the same condition
    std::optional<TimerSink> timer;  // a timer rung drives this instead of coils
};

struct NetlistResult {
    bool ok = false;
    CompiledProgram program;
    Diagnostics diagnostics;
};

// Lowers `rungs` into bytecode, declaring `vars` first. Diagnostics carry no line
// numbers - a drawing has coordinates, not lines - and name the rung instead.
NetlistResult compile_netlist(std::string_view name, std::span<const Rung> rungs,
                              std::span<const TagDecl> vars);

}  // namespace sc::plc
