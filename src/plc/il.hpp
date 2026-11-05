#pragma once
// Instruction List: IEC 61131-3's accumulator language, and the one a student can
// type without a GUI. LD loads an operand, AND/OR combine it with the accumulator,
// ST writes the accumulator to a tag - which is exactly the stack the bytecode VM
// already has. So this is a translator, not a second interpreter.
//
// Supported: LD, LDN, AND, ANDN, OR, ORN, ST, STN. `//` starts a comment.
// Combining without a value on the accumulator is a diagnostic rather than a
// guess, because guessing turns a missed line into logic that looks right.
#include "plc/declaration.hpp"
#include "plc/diagnostic.hpp"
#include "plc/program.hpp"

#include <span>
#include <string>
#include <string_view>

namespace sc::plc {

struct IlResult {
    bool ok = false;
    CompiledProgram program;
    Diagnostics diagnostics;
};

IlResult compile_il(std::string_view name, std::string_view source,
                    std::span<const TagDecl> vars);

}  // namespace sc::plc
