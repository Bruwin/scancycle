#pragma once
// Declarations shared by the front ends that are not Structured Text. ST
// discovers its variables from the VAR block it parsed; a ladder or an
// instruction list is handed a tag list by whoever drew it, so the names have to
// become program slots somewhere every front end can reach.
#include "plc/diagnostic.hpp"
#include "plc/program.hpp"
#include "plc/value.hpp"

#include <span>
#include <string>

namespace sc::plc {

struct TagDecl {
    std::string name;
    ValueType type = ValueType::Bool;
};

// Appends `vars` to the program's slots. Returns false and records a diagnostic
// when a name is declared twice: two slots under one name would make find_var
// return the first, and half the program would silently address the other.
bool declare_tags(std::span<const TagDecl> vars, CompiledProgram& program,
                  Diagnostics& diagnostics);

}  // namespace sc::plc
