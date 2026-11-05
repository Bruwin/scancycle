#pragma once
#include "plc/ast.hpp"
#include "plc/diagnostic.hpp"
#include "plc/program.hpp"

namespace sc::plc {

struct CompileResult {
    bool ok = false;
    CompiledProgram program;
    Diagnostics diagnostics;
};

CompileResult compile(const ProgramDecl& declaration);

}  // namespace sc::plc
