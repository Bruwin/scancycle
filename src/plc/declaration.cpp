#include "plc/declaration.hpp"

#include <utility>

namespace sc::plc {

bool declare_tags(std::span<const TagDecl> vars, CompiledProgram& program,
                  Diagnostics& diagnostics) {
    for (const TagDecl& var : vars) {
        if (program.find_var(var.name) >= 0) {
            diagnostics.push_back({0, 0, "variable '" + var.name + "' is declared twice"});
            continue;
        }
        VarInfo info;
        info.name = var.name;
        info.type = var.type;
        program.vars.push_back(std::move(info));
    }
    return diagnostics.empty();
}

}  // namespace sc::plc
